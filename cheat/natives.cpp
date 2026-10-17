// ============================================================================
//  C4pt0r Cheats — Native Invocation Implementation
//  Primary path: FiveM's own exported invoke (handles hash mapping +
//  adhesive handler decryption internally). Fallback: raw table walk.
// ============================================================================

#include "natives.h"
#include "memory.h"
#include "logger.h"
#include <cstdio>
#include <set>
#include <chrono>
#include <atomic>

namespace C4pt0r::Natives
{
    // ---- Exported FiveM entry points (resolved via GetProcAddress) ------
    //
    //  scripting-gta.dll:
    //    bool fx::ScriptEngine::CallNativeHandler(uint64_t hash,
    //                                           fx::ScriptContext& ctx)
    //      - full path: MapNative + decrypt + blocked check + SEH wrapper
    //
    //  rage-scripting-five.dll:
    //    rage::scrEngine::NativeHandler scrEngine::GetNativeHandler(uint64)
    //      - fastPathMap lookup, returns decrypted callable handler
    //    uint64_t MapNative(uint64)
    //      - day-1 hash -> current-build hash
    //
    using FxCallNativeHandler = bool(__cdecl*)(uint64_t hash, void* ctx);
    using RageGetNativeHandler = NativeHandler(__cdecl*)(uint64_t hash);
    using MapNativeFn         = uint64_t(__cdecl*)(uint64_t hash);

    static FxCallNativeHandler  s_CallNative    = nullptr;
    static RageGetNativeHandler s_RageGNH       = nullptr;
    static MapNativeFn          s_MapNative     = nullptr;

    // Raw-table fallback state (pattern scan path)
    static GetNativeHandlerFn s_GetNativeHandler = nullptr;
    static uintptr_t          s_NativeTable      = 0;
    static bool               s_Ready            = false;

    // ---- fx::ScriptContext (scripting-gta) -------------------------------
    //  { void* m_argumentBuffer; void* m_resultBuffer;
    //    int m_numArguments;     int m_numResults; }
    struct FxScriptContext {
        void*     argBuf;    // +0x00
        void*     retBuf;    // +0x08
        int       numArgs;   // +0x10
        int       numResults;// +0x14
        uint64_t  args[32];
        uint64_t  rets[4];

        void Reset() {
            argBuf     = args;
            retBuf     = rets;
            numArgs    = 0;
            numResults = 0;
            memset(args, 0, sizeof(args));
            memset(rets, 0, sizeof(rets));
        }
    };

    // ---- Per-path health tracking -----------------------------------------
    //  If one invocation path starts throwing C++ exceptions every call
    //  (session transition, module reload), it gets retired and the next
    //  path takes over instead of the whole feature set dying silently.
    static int s_FailA = 0, s_FailB = 0, s_FailC = 0;
    static constexpr int PATH_FAIL_LIMIT = 30;

    // telemetria pro menu/diagnostico
    static uint64_t s_OkCount   = 0;
    static uint64_t s_FailCount = 0;
    static int64_t  s_LastOkMs  = 0;

    // ---- Hang diagnostics --------------------------------------------------
    //  If a native call blocks INSIDE the engine (script lock during a
    //  session transition) the whole feature thread dies silently while the
    //  menu/render thread keeps pinging fine — the "menu fantasma". The
    //  supervisor watches g_TickBeat; when it goes stale it retires the
    //  in-flight path (s_BanX) and respawns the loop.
    std::atomic<uint64_t> g_InvokeHash{0};
    std::atomic<int>      g_InvokePath{0};
    std::atomic<int64_t>  g_TickBeat{0};
    std::atomic<int>      g_TickStage{0};
    //  Path A (scripting-gta CallNativeHandler) passa por locks internos da
    //  script engine — e o UNICO caminho que pode travar a thread dentro de
    //  uma call. Como threads criadas pos-injecao nunca iniciam nesse
    //  ambiente, qualquer hang = morte permanente. Banido de nascença:
    //  B (rage GetNativeHandler — funcao pura) e C (raw table) cobrem tudo.
    static std::atomic<bool> s_BanA{true}, s_BanB{false}, s_BanC{false};

    void RetirePath(int path) {
        if (path == 1 && !s_BanA.exchange(true)) {
            s_CallNative = nullptr;
            Log::Write("Path A (CallNativeHandler) APOSENTADO — travou a thread.");
        } else if (path == 2 && !s_BanB.exchange(true)) {
            s_RageGNH = nullptr;
            Log::Write("Path B (rage GetNativeHandler) APOSENTADO — travou a thread.");
        } else if (path == 3 && !s_BanC.exchange(true)) {
            s_GetNativeHandler = nullptr;
            Log::Write("Path C (raw table) APOSENTADO — travou a thread.");
        }
    }

    static int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // SEH-safe invocation — catch(...) does NOT trap access violations and
    // an AV inside a native handler is what silently killed the cheat loop.
    // 'threw' distinguishes "native returned false / not found" (normal)
    // from "the call itself faulted" (path is actually dying).
    static bool InvokePathA(uint64_t hash, FxScriptContext& ctx, bool& threw) {
        threw = false;
        __try { return s_CallNative(hash, &ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER) { threw = true; return false; }
    }
    static bool InvokeHandler(NativeHandler h, NativeCallContext& ctx) {
        __try { h(&ctx); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool TryExports();  // fwd — full impl below

    // Self-heal: when every path has been retired, re-resolve the exports
    // periodically. Modules that got (un)loaded become reachable again.
    static void MaybeReResolve() {
        if (s_CallNative || s_RageGNH || s_GetNativeHandler) return;
        static auto last = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(5)) return;
        last = now;
        Log::Write("All native paths dead — re-resolving exports...");
        TryExports();
    }

    // ---- Unified native call ---------------------------------------------
    //  Fills ret[0..3] (float-packed for Vec3), returns false on failure.
    //  Order: B (direct rage handler — pure function, no script-engine
    //  locks) -> C (raw table) -> A (scripting-gta CallNativeHandler, which
    //  goes through the engine and is the historical source of thread
    //  hangs) last. g_InvokePath marks which path is in flight so the
    //  supervisor can retire exactly the one that hung.
    bool CallNative(uint64_t hash, const uint64_t* args, int nargs,
                    uint64_t* ret)
    {
        g_InvokeHash = hash;   // antes de TUDO — hang no re-resolve tambem
                               // fica atribuido ao hash que disparou
        MaybeReResolve();
        bool anyHandler = false;

        // Path B: exported scrEngine::GetNativeHandler -> call raw handler.
        // O fastPathMap e indexado pelo hash CURRENT-BUILD — natives
        // rehasheados neste build ("no handler" fantasmas) precisam do
        // MapNative antes do lookup, igual o path C ja fazia.
        if (s_RageGNH && !s_BanB) {
            g_InvokePath = 2;
            NativeHandler h = nullptr;
            uint64_t bh = s_MapNative ? s_MapNative(hash) : hash;
            __try { h = s_RageGNH(bh); }
            __except (EXCEPTION_EXECUTE_HANDLER) { h = nullptr; }
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h))) {
                anyHandler = true;
                NativeCallContext ctx;
                for (int i = 0; i < nargs; ++i) ctx.Push(args[i]);
                if (InvokeHandler(h, ctx)) {
                    s_FailB = 0;
                    ++s_OkCount; s_LastOkMs = NowMs();
                    if (ret) {
                        const uint64_t* rv = ctx.returnValue;
                        ret[0] = rv[0]; ret[1] = rv[1];
                        ret[2] = rv[2]; ret[3] = rv[3];
                    }
                    g_InvokePath = 0; g_InvokeHash = 0;
                    return true;
                }
                // handler existia mas explodiu — falha real do path
                if (++s_FailB >= PATH_FAIL_LIMIT) {
                    s_RageGNH = nullptr;
                    Log::Write("rage GetNativeHandler path died (%d faults) — dropping to raw table.", s_FailB);
                }
            }
            // h == nullptr -> native nao registrado nesse build: normal,
            // nao conta contra a saude do path
        }

        // Path C: raw table walk (mapped hash, raw handler — likely
        // adhesive-encrypted on this build; still try as fallback)
        if (s_GetNativeHandler && s_NativeTable && !s_BanC) {
            g_InvokePath = 3;
            NativeHandler h = nullptr;
            uint64_t h2 = s_MapNative ? s_MapNative(hash) : hash;
            __try { h = s_GetNativeHandler(s_NativeTable, h2); }
            __except (EXCEPTION_EXECUTE_HANDLER) { h = nullptr; }
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h))) {
                anyHandler = true;
                NativeCallContext ctx;
                for (int i = 0; i < nargs; ++i) ctx.Push(args[i]);
                if (InvokeHandler(h, ctx)) {
                    s_FailC = 0;
                    ++s_OkCount; s_LastOkMs = NowMs();
                    if (ret) {
                        const uint64_t* rv = ctx.returnValue;
                        ret[0] = rv[0]; ret[1] = rv[1];
                        ret[2] = rv[2]; ret[3] = rv[3];
                    }
                    g_InvokePath = 0; g_InvokeHash = 0;
                    return true;
                }
                if (++s_FailC >= PATH_FAIL_LIMIT) {
                    s_GetNativeHandler = nullptr;
                    Log::Write("Raw table path died (%d faults).", s_FailC);
                }
            }
        }

        // Path A: FiveM's exported high-level invoke (does everything —
        // MapNative + decrypt + context marshal). Goes through script-engine
        // internals, so it's the LAST resort: if it ever hangs, the
        // supervisor retires it and the direct paths keep working.
        if (s_CallNative && !s_BanA) {
            g_InvokePath = 1;
            FxScriptContext ctx;
            ctx.Reset();
            for (int i = 0; i < nargs; ++i) ctx.args[i] = args[i];
            ctx.numArgs = nargs;
            bool threw = false;
            bool ok = InvokePathA(hash, ctx, threw);
            if (ok) {
                s_FailA = 0;
                ++s_OkCount; s_LastOkMs = NowMs();
                if (ret) {
                    ret[0] = ctx.rets[0];
                    ret[1] = ctx.rets[1];
                    ret[2] = ctx.rets[2];
                    ret[3] = ctx.rets[3];
                }
                g_InvokePath = 0; g_InvokeHash = 0;
                return true;
            }
            // false sem fault = native recusado/ausente — NAO penaliza o
            // path (foi isso que aposentava os paths por natives faltando).
            // So conta quando a chamada em si explode (SEH).
            if (threw && ++s_FailA >= PATH_FAIL_LIMIT) {
                s_CallNative = nullptr;
                Log::Write("CallNativeHandler path died (%d faults).", s_FailA);
            }
        }

        g_InvokePath = 0; g_InvokeHash = 0;
        ++s_FailCount;
        if (!anyHandler) LogMissingHash(hash);   // diagnostico: native nao existe
        return false;
    }

    // ---- Initialization -------------------------------------------------
    static bool TryExports()
    {
        HMODULE rsf  = GetModuleHandleW(L"rage-scripting-five.dll");

        // Path A nunca e resolvido — ver s_BanA. Um hang dentro dele seria
        // permanente; e o FORCAR RECONEXAO roda no thread de render, onde
        // um hang congelaria o JOGO inteiro, nao so o menu.
        s_CallNative = nullptr;
        if (rsf) {
            if (!s_BanB) {
                s_RageGNH = reinterpret_cast<RageGetNativeHandler>(GetProcAddress(rsf,
                    "?GetNativeHandler@scrEngine@rage@@SAP6AXPEAVscrNativeCallContext@2@@Z_K@Z"));
                if (s_RageGNH) s_FailB = 0;
            }
            s_MapNative = reinterpret_cast<MapNativeFn>(GetProcAddress(rsf, "MapNative"));
            Log::Write("rage-scr-five GetNativeHandler = 0x%p  MapNative = 0x%p",
                       reinterpret_cast<void*>(s_RageGNH),
                       reinterpret_cast<void*>(s_MapNative));
        }
        return s_CallNative != nullptr || s_RageGNH != nullptr;
    }

    static bool TryPatternScan()
    {
        const char* sitePat = "48 8D 0D ?? ?? ?? ?? 48 8B 14 FA E8 ?? ?? ?? ??";
        const char* bodyPat = "48 89 5C 24 18 48 89 7C 24 20 0F B6 C2 4C 8B D2 4C 8B 04 C1";

        const wchar_t* modules[] = {
            nullptr,
            L"rage-scripting-five.dll",
            L"citizen-scripting-core.dll",
            L"citizen-game-main.dll",
        };

        uintptr_t site = 0;
        for (int m = 0; m < 4 && !site; ++m) {
            site = Memory::PatternScanModule(modules[m], sitePat);
            if (site)
                Log::Write("Registration site hit in %ls @ 0x%p",
                           modules[m] ? modules[m] : L"<exe>",
                           reinterpret_cast<void*>(site));
        }
        if (!site) {
            Log::Write("Registration site pattern not found.");
            return false;
        }

        s_NativeTable = Memory::ResolveRIP(site, 3, 7);
        uintptr_t callAt = site + 11;
        if (*reinterpret_cast<uint8_t*>(callAt) != 0xE8) {
            callAt = 0;
            for (int i = 7; i < 48; ++i) {
                if (*reinterpret_cast<uint8_t*>(site + i) == 0xE8) {
                    callAt = site + i;
                    break;
                }
            }
        }
        if (!callAt) {
            Log::Write("No resolver call near site.");
            return false;
        }
        s_GetNativeHandler = s_BanC ? nullptr : reinterpret_cast<GetNativeHandlerFn>(
            Memory::ResolveRIP(callAt, 1, 5));
        s_FailC = 0;

        uintptr_t body = Memory::PatternScanModule(nullptr, bodyPat);
        Log::Write("NativeTable=0x%p  Resolver=0x%p  body@0x%p",
                   reinterpret_cast<void*>(s_NativeTable),
                   reinterpret_cast<void*>(s_GetNativeHandler),
                   reinterpret_cast<void*>(body));
        return true;
    }

    bool Initialize()
    {
        s_Ready = false;   // stale "ready" during a failing re-init was the
                           // silent-death bug — always reset first
        bool haveExports = TryExports();
        TryPatternScan();   // still useful as fallback + diagnostics

        if (!haveExports && !s_GetNativeHandler) {
            Log::Write("No native path available at all.");
            return false;
        }

        // Probe: call only SAFE natives with correct arg counts.
        // FiveM wraps handler faults in a C++ exception — catch per item so
        // one bad probe can't abort init (that is what killed s_Ready before).
        float w2sX = 0.f, w2sY = 0.f;
        struct ProbeItem { uint64_t hash; uint64_t args[5]; int n; };
        ProbeItem probes[] = {
            { Hash::PLAYER_PED_ID,           {},                                    0 },
            { Hash::NETWORK_IS_PLAYER_ACTIVE,{ 0 },                                 1 },
            { Hash::GET_PLAYER_NAME,         { 0 },                                 1 },
            { Hash::GET_SCREEN_COORD_FROM_WORLD_COORD,
              { 0, 0, 0,
                reinterpret_cast<uint64_t>(&w2sX),
                reinterpret_cast<uint64_t>(&w2sY) },                                5 },
        };
        int resolved = 0;
        for (const auto& p : probes) {
            uint64_t ret[4] = {};
            bool ok = false;
            try {
                ok = CallNative(p.hash, p.args, p.n, ret);
            } catch (...) {
                ok = false;
            }
            Log::Write("  probe 0x%016llX -> ok=%d ret0=0x%p",
                       static_cast<unsigned long long>(p.hash), ok ? 1 : 0,
                       reinterpret_cast<void*>(ret[0]));
            if (ok) ++resolved;
        }
        Log::Write("Probe: %d/%zu natives callable", resolved, _countof(probes));

        if (resolved == 0) {
            Log::Write("No natives callable — all paths failed.");
            return false;
        }

        s_Ready = true;
        Log::Write("Natives initialized.");
        return true;
    }

    bool IsReady() { return s_Ready; }

    void MarkUnhealthy() { s_Ready = false; }

    // cheap liveness probe — PLAYER_PED_ID via every available path
    bool Ping() {
        uint64_t ret[4] = {};
        return CallNative(Hash::PLAYER_PED_ID, nullptr, 0, ret);
    }

    // ms since the last successful native call (for menu diagnostics)
    long long MsSinceOk() {
        if (!s_LastOkMs) return -1;
        return (long long)(NowMs() - s_LastOkMs);
    }

    // "paths[..] ok=N fail=N lastOk=Xms tick=Xms" — shown in Settings
    void HealthString(char* buf, int len) {
        int64_t beat = g_TickBeat.load();
        long long tickMs = beat ? (long long)(NowMs() - beat) : -1;
        uint64_t h = g_InvokeHash.load();
        if (h) {
            snprintf(buf, len,
                     "paths[A:%d B:%d C:%d] ok=%llu fail=%llu lastOk=%lldms tick=%lldms stage=%d | em voo: 0x%llX (path %d)%s",
                     s_FailA, s_FailB, s_FailC,
                     (unsigned long long)s_OkCount, (unsigned long long)s_FailCount,
                     MsSinceOk(), tickMs, g_TickStage.load(),
                     (unsigned long long)h, g_InvokePath.load(),
                     s_Ready ? "" : " NOT-READY");
        } else {
            snprintf(buf, len,
                     "paths[A:%d B:%d C:%d] ok=%llu fail=%llu lastOk=%lldms tick=%lldms stage=%d%s",
                     s_FailA, s_FailB, s_FailC,
                     (unsigned long long)s_OkCount, (unsigned long long)s_FailCount,
                     MsSinceOk(), tickMs, g_TickStage.load(),
                     s_Ready ? "" : " NOT-READY");
        }
    }

    bool ReInitialize() { return Initialize(); }

    // Kept for API compat — direct handler fetch via rage export when present
    NativeHandler GetHandler(uint64_t hash) {
        if (s_RageGNH) {
            NativeHandler h = s_RageGNH(hash);
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h)))
                return h;
        }
        if (s_GetNativeHandler && s_NativeTable) {
            uint64_t h2 = s_MapNative ? s_MapNative(hash) : hash;
            NativeHandler h = s_GetNativeHandler(s_NativeTable, h2);
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h)))
                return h;
        }
        return nullptr;
    }

    void LogMissingHash(uint64_t hash) {
        // array fixo em vez de set<> — evita o lock do heap do CRT
        static uint64_t s_Seen[128];
        static int s_N = 0;
        for (int i = 0; i < s_N; ++i) if (s_Seen[i] == hash) return;
        if (s_N < 128) s_Seen[s_N++] = hash;
        Log::Write("Native 0x%016llX has no handler — feature using it is dead.",
                   static_cast<unsigned long long>(hash));
    }

    // ---- Vec3-returning natives ------------------------------------------
    //  scrVector3 no ABI do FiveM = {x, _pad, y, _pad, z, _pad} — pads sao
    //  sempre 0. O handler escreve isso no returnValue (ret[]). O bug: o
    //  decode antigo lia packed {f0,f1,f2} = (x, pad=0, y) — era o "y=0"
    //  fantasma, e o out-ptr extra ficava zerado. Decodifica certo agora.
    static Vec3 CallVec3(uint64_t hash, const uint64_t* args, int nargs)
    {
        uint64_t ret[4] = {};
        if (!CallNative(hash, args, nargs, ret)) {
            LogMissingHash(hash);
            return {};
        }
        auto* f = reinterpret_cast<float*>(ret);
        // padded scrVector3: f1/f3 sao zero. packed {x,y,z}: f3=prox dado.
        if (f[1] == 0.f && f[3] == 0.f && (f[2] != 0.f || f[4] != 0.f))
            return Vec3(f[0], f[2], f[4]);
        return Vec3(f[0], f[1], f[2]);
    }

    Vec3 InvokeVec3(uint64_t hash, uint64_t arg0, uint64_t arg1, int explicitN)
    {
        int n = (explicitN >= 0) ? explicitN : (arg0 ? (arg1 ? 2 : 1) : 0);
        uint64_t args[3] = { arg0, arg1, 0 };
        return CallVec3(hash, args, n);
    }

    // ====================================================================
    //  High-level wrappers
    // ====================================================================

    int PlayerPedId() {
        uint64_t ret[4] = {};
        CallNative(Hash::PLAYER_PED_ID, nullptr, 0, ret);
        return static_cast<int>(ret[0]);
    }
    int PlayerId() {
        uint64_t ret[4] = {};
        CallNative(Hash::PLAYER_ID, nullptr, 0, ret);
        return static_cast<int>(ret[0]);
    }
    int GetPlayerPed(int player) {
        uint64_t a[] = { static_cast<uint64_t>(player) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_PLAYER_PED, a, 1, ret);
        return static_cast<int>(ret[0]);
    }
    bool IsPlayerPlaying(int player) {
        uint64_t a[] = { static_cast<uint64_t>(player) };
        uint64_t ret[4] = {};
        CallNative(Hash::IS_PLAYER_PLAYING, a, 1, ret);
        return ret[0] != 0;
    }
    bool NetworkIsPlayerActive(int player) {
        uint64_t a[] = { static_cast<uint64_t>(player) };
        uint64_t ret[4] = {};
        CallNative(Hash::NETWORK_IS_PLAYER_ACTIVE, a, 1, ret);
        return ret[0] != 0;
    }
    const char* GetPlayerName(int player) {
        uint64_t a[] = { static_cast<uint64_t>(player) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_PLAYER_NAME, a, 1, ret);
        return reinterpret_cast<const char*>(ret[0]);
    }

    // Entity ---
    Vec3 GetEntityCoords(int entity, bool alive) {
        return InvokeVec3(Hash::GET_ENTITY_COORDS,
                          static_cast<uint64_t>(entity),
                          static_cast<uint64_t>(alive), 2);
    }
    void SetEntityCoordsNoOffset(int entity, float x, float y, float z) {
        uint64_t a[7];
        *reinterpret_cast<int*>(   &a[0]) = entity;
        *reinterpret_cast<float*>( &a[1]) = x;
        *reinterpret_cast<float*>( &a[2]) = y;
        *reinterpret_cast<float*>( &a[3]) = z;
        a[4] = a[5] = a[6] = 0;
        CallNative(Hash::SET_ENTITY_COORDS_NO_OFFSET, a, 7, nullptr);
    }
    void SetEntityInvincible(int entity, bool toggle) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = toggle ? 1 : 0;
        CallNative(Hash::SET_ENTITY_INVINCIBLE, a, 2, nullptr);
    }
    void SetEntityVisible(int entity, bool toggle, bool p2) {
        uint64_t a[3];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = toggle ? 1 : 0;
        a[2] = p2 ? 1 : 0;
        CallNative(Hash::SET_ENTITY_VISIBLE, a, 3, nullptr);
    }
    int GetEntityHealth(int entity) {
        uint64_t a[] = { static_cast<uint64_t>(entity) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_ENTITY_HEALTH, a, 1, ret);
        return static_cast<int>(ret[0]);
    }
    int GetEntityMaxHealth(int entity) {
        uint64_t a[] = { static_cast<uint64_t>(entity) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_ENTITY_MAX_HEALTH, a, 1, ret);
        return static_cast<int>(ret[0]);
    }
    bool IsEntityDead(int entity) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = 0;
        uint64_t ret[4] = {};
        CallNative(Hash::IS_ENTITY_DEAD, a, 2, ret);
        return ret[0] != 0;
    }
    void FreezeEntityPosition(int entity, bool toggle) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = toggle ? 1 : 0;
        CallNative(Hash::FREEZE_ENTITY_POSITION, a, 2, nullptr);
    }
    void SetEntityCollision(int entity, bool toggle, bool keepPhysics) {
        uint64_t a[3];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = toggle ? 1 : 0;
        a[2] = keepPhysics ? 1 : 0;
        CallNative(Hash::SET_ENTITY_COLLISION, a, 3, nullptr);
    }
    void SetEntityVelocity(int entity, float x, float y, float z) {
        uint64_t a[4];
        *reinterpret_cast<int*>  (&a[0]) = entity;
        *reinterpret_cast<float*>(&a[1]) = x;
        *reinterpret_cast<float*>(&a[2]) = y;
        *reinterpret_cast<float*>(&a[3]) = z;
        CallNative(Hash::SET_ENTITY_VELOCITY, a, 4, nullptr);
    }
    void SetEntityHasGravity(int entity, bool toggle) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = entity;
        a[1] = toggle ? 1 : 0;
        CallNative(Hash::SET_ENTITY_HAS_GRAVITY, a, 2, nullptr);
    }

    // Ped ---
    bool IsPedShooting(int ped) {
        uint64_t a[] = { static_cast<uint64_t>(ped) };
        uint64_t ret[4] = {};
        CallNative(Hash::IS_PED_SHOOTING, a, 1, ret);
        return ret[0] != 0;
    }
    Vec3 GetPedBoneCoords(int ped, int boneId) {
        uint64_t a[5];
        *reinterpret_cast<int*>(&a[0]) = ped;
        *reinterpret_cast<int*>(&a[1]) = boneId;
        a[2] = a[3] = a[4] = 0;   // offset xyz floats
        return CallVec3(Hash::GET_PED_BONE_COORDS, a, 5);
    }
    int GetPedArmour(int ped) {
        uint64_t a[] = { static_cast<uint64_t>(ped) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_PED_ARMOUR, a, 1, ret);
        return static_cast<int>(ret[0]);
    }
    bool HasClearLOS(int e1, int e2, int traceType) {
        uint64_t a[3];
        *reinterpret_cast<int*>(&a[0]) = e1;
        *reinterpret_cast<int*>(&a[1]) = e2;
        *reinterpret_cast<int*>(&a[2]) = traceType;
        uint64_t ret[4] = {};
        CallNative(Hash::HAS_ENTITY_CLEAR_LOS_TO_ENTITY, a, 3, ret);
        return ret[0] != 0;
    }

    // Camera ---
    Vec3 GetGameplayCamRot(int order) {
        uint64_t a[] = { static_cast<uint64_t>(order) };
        return CallVec3(Hash::GET_GAMEPLAY_CAM_ROT, a, 1);
    }
    Vec3 GetGameplayCamCoord() {
        return CallVec3(Hash::GET_GAMEPLAY_CAM_COORD, nullptr, 0);
    }
    float GetGameplayCamRelativePitch() {
        uint64_t ret[4] = {};
        CallNative(Hash::GET_GAMEPLAY_CAM_RELATIVE_PITCH, nullptr, 0, ret);
        return *reinterpret_cast<float*>(&ret[0]);
    }
    float GetGameplayCamRelativeHeading() {
        uint64_t ret[4] = {};
        CallNative(Hash::GET_GAMEPLAY_CAM_RELATIVE_HEADING, nullptr, 0, ret);
        return *reinterpret_cast<float*>(&ret[0]);
    }
    void SetGameplayCamRelativeHeading(float h) {
        uint64_t a[1];
        *reinterpret_cast<float*>(&a[0]) = h;
        CallNative(Hash::SET_GAMEPLAY_CAM_RELATIVE_HEADING, a, 1, nullptr);
    }
    void SetGameplayCamRelativePitch(float p, float smoothSpeed) {
        uint64_t a[2];
        *reinterpret_cast<float*>(&a[0]) = p;
        *reinterpret_cast<float*>(&a[1]) = smoothSpeed;
        CallNative(Hash::SET_GAMEPLAY_CAM_RELATIVE_PITCH, a, 2, nullptr);
    }

    // Player misc ---
    void SetRunSprintMultiplier(int player, float mult) {
        uint64_t a[2];
        *reinterpret_cast<int*>  (&a[0]) = player;
        *reinterpret_cast<float*>(&a[1]) = mult;
        CallNative(Hash::SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, a, 2, nullptr);
    }

    // Infinite ammo (used by features.cpp via InvokeVoid fallback path)
    void SetPedInfiniteAmmoClip(int ped, bool toggle) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = ped;
        a[1] = toggle ? 1 : 0;
        CallNative(Hash::SET_PED_INFINITE_AMMO_CLIP, a, 2, nullptr);
    }

    // Inspector / macro helpers ---
    float GetEntityHeading(int entity) {
        uint64_t a[] = { static_cast<uint64_t>(entity) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_ENTITY_HEADING, a, 1, ret);
        return *reinterpret_cast<float*>(&ret[0]);
    }
    float GetEntitySpeed(int entity) {
        uint64_t a[] = { static_cast<uint64_t>(entity) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_ENTITY_SPEED, a, 1, ret);
        return *reinterpret_cast<float*>(&ret[0]);
    }
    uint32_t GetSelectedPedWeapon(int ped) {
        uint64_t a[] = { static_cast<uint64_t>(ped) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_SELECTED_PED_WEAPON, a, 1, ret);
        return static_cast<uint32_t>(ret[0]);
    }
    bool IsControlPressed(int controlId) {
        uint64_t a[2];
        a[0] = 0;  // input group 0 = move
        *reinterpret_cast<int*>(&a[1]) = controlId;
        uint64_t ret[4] = {};
        CallNative(Hash::IS_CONTROL_PRESSED, a, 2, ret);
        return ret[0] != 0;
    }
    void PressControl(int controlId, float value) {
        // SET_CONTROL_VALUE_NEXT_FRAME(group, control, value)
        uint64_t a[3];
        a[0] = 0;
        *reinterpret_cast<int*>  (&a[1]) = controlId;
        *reinterpret_cast<float*>(&a[2]) = value;
        CallNative(Hash::SET_CONTROL_VALUE_NEXT_FRAME, a, 3, nullptr);
    }
    bool IsPedInAnyVehicle(int ped) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = ped;
        a[1] = 0;   // include getting in/out
        uint64_t ret[4] = {};
        CallNative(Hash::IS_PED_IN_ANY_VEHICLE, a, 2, ret);
        return ret[0] != 0;
    }
    int GetVehiclePedIsIn(int ped) {
        uint64_t a[2];
        *reinterpret_cast<int*>(&a[0]) = ped;
        a[1] = 0;
        uint64_t ret[4] = {};
        CallNative(Hash::GET_VEHICLE_PED_IS_IN, a, 2, ret);
        return static_cast<int>(ret[0]);
    }

    // World → screen ---
    bool WorldToScreen(Vec3 world, Vec2& screen) {
        float sx = 0, sy = 0;
        uint64_t a[5];
        *reinterpret_cast<float*>(&a[0]) = world.x;
        *reinterpret_cast<float*>(&a[1]) = world.y;
        *reinterpret_cast<float*>(&a[2]) = world.z;
        a[3] = reinterpret_cast<uint64_t>(&sx);
        a[4] = reinterpret_cast<uint64_t>(&sy);
        uint64_t ret[4] = {};
        bool ok = CallNative(Hash::GET_SCREEN_COORD_FROM_WORLD_COORD, a, 5, ret)
                  && ret[0] != 0;
        if (ok) {
            screen.x = sx;
            screen.y = sy;
        }
        return ok;
    }

    // Waypoint ---
    bool IsWaypointActive() {
        uint64_t a[] = { 8 };
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_FIRST_BLIP_INFO_ID, a, 1, ret)) return false;
        int blip = static_cast<int>(ret[0]);
        uint64_t a2[] = { static_cast<uint64_t>(blip) };
        uint64_t ret2[4] = {};
        CallNative(Hash::DOES_BLIP_EXIST, a2, 1, ret2);
        return ret2[0] != 0;
    }

    Vec3 GetWaypointCoords() {
        uint64_t a[] = { 8 };
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_FIRST_BLIP_INFO_ID, a, 1, ret)) return {};
        int blip = static_cast<int>(ret[0]);
        uint64_t a2[] = { static_cast<uint64_t>(blip) };
        uint64_t ret2[4] = {};
        CallNative(Hash::DOES_BLIP_EXIST, a2, 1, ret2);
        if (!ret2[0]) return {};
        uint64_t a3[] = { static_cast<uint64_t>(blip) };
        return CallVec3(Hash::GET_BLIP_INFO_ID_COORD, a3, 1);
    }

    Vec3 GetWaypointCoordsDirect() {
        return CallVec3(Hash::GET_WAYPOINT_COORDS, nullptr, 0);
    }

    // ---- Ground -------------------------------------------------------------
    bool GetGroundZ(float x, float y, float z, float& outZ) {
        outZ = 0.f;
        uint64_t a[5];
        *reinterpret_cast<float*>(&a[0]) = x;
        *reinterpret_cast<float*>(&a[1]) = y;
        *reinterpret_cast<float*>(&a[2]) = z;
        a[3] = reinterpret_cast<uint64_t>(&outZ);
        a[4] = 1;   // ignoreWater-ish flag (harmless on builds that ignore it)
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_GROUND_Z_FOR_3D_COORD, a, 5, ret)) return false;
        return ret[0] != 0;
    }

    // ---- Vehicles -------------------------------------------------------------
    uint32_t GetHashKey(const char* str) {
        uint64_t a[] = { reinterpret_cast<uint64_t>(str) };
        uint64_t ret[4] = {};
        CallNative(Hash::GET_HASH_KEY, a, 1, ret);
        return static_cast<uint32_t>(ret[0]);
    }
    void RequestModel(uint32_t model) {
        uint64_t a[] = { static_cast<uint64_t>(model) };
        CallNative(Hash::REQUEST_MODEL, a, 1, nullptr);
    }
    bool HasModelLoaded(uint32_t model) {
        uint64_t a[] = { static_cast<uint64_t>(model) };
        uint64_t ret[4] = {};
        CallNative(Hash::HAS_MODEL_LOADED, a, 1, ret);
        return ret[0] != 0;
    }
    void SetModelNoLongerNeeded(uint32_t model) {
        uint64_t a[] = { static_cast<uint64_t>(model) };
        CallNative(Hash::SET_MODEL_AS_NO_LONGER_NEEDED, a, 1, nullptr);
    }
    int CreateVehicle(uint32_t model, float x, float y, float z, float heading) {
        uint64_t a[7];
        a[0] = model;
        *reinterpret_cast<float*>(&a[1]) = x;
        *reinterpret_cast<float*>(&a[2]) = y;
        *reinterpret_cast<float*>(&a[3]) = z;
        *reinterpret_cast<float*>(&a[4]) = heading;
        a[5] = 1;   // isNetwork
        a[6] = 0;   // script-host check off
        uint64_t ret[4] = {};
        CallNative(Hash::CREATE_VEHICLE, a, 7, ret);
        return static_cast<int>(ret[0]);
    }
    void SetPedIntoVehicle(int ped, int vehicle, int seat) {
        uint64_t a[3];
        *reinterpret_cast<int*>(&a[0]) = ped;
        *reinterpret_cast<int*>(&a[1]) = vehicle;
        *reinterpret_cast<int*>(&a[2]) = seat;
        CallNative(Hash::SET_PED_INTO_VEHICLE, a, 3, nullptr);
    }
    void SetVehicleOnGroundProperly(int vehicle) {
        uint64_t a[] = { static_cast<uint64_t>(vehicle) };
        CallNative(Hash::SET_VEHICLE_ON_GROUND_PROPERLY, a, 1, nullptr);
    }

    // ---- Generic by-name invocation ---------------------------------------
    //  joaat: the hash GTA scripts use for native names — lets us call any
    //  native without hardcoding its (possibly wrong) build-specific hash.
    uint32_t JoaatHash(const char* s) {
        uint32_t h = 0;
        for (; *s; ++s) {
            unsigned char c = static_cast<unsigned char>(*s);
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            h += c;
            h += h << 10;
            h ^= h >> 6;
        }
        h += h << 3;
        h ^= h >> 11;
        h += h << 15;
        return h;
    }

    bool CallByName(const char* name, const uint64_t* args, int argCount, uint64_t* ret) {
        if (!name || !*name) return false;
        return CallNative(JoaatHash(name), args, argCount, ret);
    }

    bool HasNative(const char* name) {
        if (!name || !*name) return false;
        return GetHandler(JoaatHash(name)) != nullptr;
    }
}
