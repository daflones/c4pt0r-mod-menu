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

    // ---- Unified native call ---------------------------------------------
    //  Fills ret[0..3] (float-packed for Vec3), returns false on failure.
    bool CallNative(uint64_t hash, const uint64_t* args, int nargs,
                    uint64_t* ret)
    {
        // Path A: FiveM's exported high-level invoke (does everything)
        if (s_CallNative) {
            FxScriptContext ctx;
            ctx.Reset();
            for (int i = 0; i < nargs; ++i) ctx.args[i] = args[i];
            ctx.numArgs = nargs;
            bool ok = s_CallNative(hash, &ctx);
            if (ret) {
                ret[0] = ctx.rets[0];
                ret[1] = ctx.rets[1];
                ret[2] = ctx.rets[2];
                ret[3] = ctx.rets[3];
            }
            return ok;
        }

        // Path B: exported scrEngine::GetNativeHandler -> call raw handler
        if (s_RageGNH) {
            NativeHandler h = s_RageGNH(hash);
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h))) {
                NativeCallContext ctx;
                for (int i = 0; i < nargs; ++i) ctx.Push(args[i]);
                h(&ctx);
                if (ret) {
                    const uint64_t* rv = ctx.returnValue;
                    ret[0] = rv[0]; ret[1] = rv[1];
                    ret[2] = rv[2]; ret[3] = rv[3];
                }
                return true;
            }
        }

        // Path C: raw table walk (mapped hash, raw handler — likely
        // adhesive-encrypted on this build; still try as last resort)
        if (s_GetNativeHandler && s_NativeTable) {
            uint64_t h2 = s_MapNative ? s_MapNative(hash) : hash;
            NativeHandler h = s_GetNativeHandler(s_NativeTable, h2);
            if (h && Memory::IsExecutable(reinterpret_cast<uintptr_t>(h))) {
                NativeCallContext ctx;
                for (int i = 0; i < nargs; ++i) ctx.Push(args[i]);
                h(&ctx);
                if (ret) {
                    const uint64_t* rv = ctx.returnValue;
                    ret[0] = rv[0]; ret[1] = rv[1];
                    ret[2] = rv[2]; ret[3] = rv[3];
                }
                return true;
            }
        }
        return false;
    }

    // ---- Initialization -------------------------------------------------
    static bool TryExports()
    {
        HMODULE sgta = GetModuleHandleW(L"scripting-gta.dll");
        HMODULE rsf  = GetModuleHandleW(L"rage-scripting-five.dll");

        if (sgta) {
            s_CallNative = reinterpret_cast<FxCallNativeHandler>(GetProcAddress(sgta,
                "?CallNativeHandler@ScriptEngine@fx@@SA_N_KAEAVScriptContext@2@@Z"));
            Log::Write("scripting-gta CallNativeHandler = 0x%p",
                       reinterpret_cast<void*>(s_CallNative));
        }
        if (rsf) {
            s_RageGNH = reinterpret_cast<RageGetNativeHandler>(GetProcAddress(rsf,
                "?GetNativeHandler@scrEngine@rage@@SAP6AXPEAVscrNativeCallContext@2@@Z_K@Z"));
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
        s_GetNativeHandler = reinterpret_cast<GetNativeHandlerFn>(
            Memory::ResolveRIP(callAt, 1, 5));

        uintptr_t body = Memory::PatternScanModule(nullptr, bodyPat);
        Log::Write("NativeTable=0x%p  Resolver=0x%p  body@0x%p",
                   reinterpret_cast<void*>(s_NativeTable),
                   reinterpret_cast<void*>(s_GetNativeHandler),
                   reinterpret_cast<void*>(body));
        return true;
    }

    bool Initialize()
    {
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
        static std::set<uint64_t> s_Reported;
        if (s_Reported.insert(hash).second)
            Log::Write("Native 0x%016llX has no handler — feature using it is dead.",
                       static_cast<unsigned long long>(hash));
    }

    // ---- Vec3-returning natives ------------------------------------------
    Vec3 InvokeVec3(uint64_t hash, uint64_t arg0, uint64_t arg1, int explicitN)
    {
        uint64_t args[2] = { arg0, arg1 };
        int n = (explicitN >= 0) ? explicitN : (arg0 ? (arg1 ? 2 : 1) : 0);
        uint64_t ret[4] = {};
        if (!CallNative(hash, args, n, ret)) { LogMissingHash(hash); return {}; }
        auto* r = reinterpret_cast<float*>(ret);
        return Vec3(r[0], r[1], r[2]);
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
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_PED_BONE_COORDS, a, 5, ret)) return {};
        auto* r = reinterpret_cast<float*>(ret);
        return Vec3(r[0], r[1], r[2]);
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
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_GAMEPLAY_CAM_ROT, a, 1, ret)) return {};
        auto* r = reinterpret_cast<float*>(ret);
        return Vec3(r[0], r[1], r[2]);
    }
    Vec3 GetGameplayCamCoord() {
        uint64_t ret[4] = {};
        if (!CallNative(Hash::GET_GAMEPLAY_CAM_COORD, nullptr, 0, ret)) return {};
        auto* r = reinterpret_cast<float*>(ret);
        return Vec3(r[0], r[1], r[2]);
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
        uint64_t ret3[4] = {};
        if (!CallNative(Hash::GET_BLIP_INFO_ID_COORD, a3, 1, ret3)) return {};
        auto* r = reinterpret_cast<float*>(ret3);
        return Vec3(r[0], r[1], r[2]);
    }
}
