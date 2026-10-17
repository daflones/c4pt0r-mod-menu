// ============================================================================
//  C4pt0r Cheats — DLL Entry Point
//  Spawns main thread, initializes subsystems, runs feature loop
// ============================================================================

#include "globals.h"
#include "hooks.h"
#include "natives.h"
#include "features.h"
#include "logger.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <winternl.h>

using namespace C4pt0r;

// ---- Module hiding ------------------------------------------------------
//  LoadLibrary injection leaves us in the PEB module lists — the first
//  thing any integrity check enumerates. Unlink our LDR_DATA_TABLE_ENTRY
//  from all three lists; keep the neighbors so we can relink before unload.
//
//  Newer SDKs strip _PEB_LDR_DATA down to InMemoryOrderModuleList only —
//  use raw offsets into the real layout instead.
static PLIST_ENTRY s_SavedLinks[3]  = {};
static PLIST_ENTRY s_SavedBlink[3]  = {};
static bool        s_Unlinked       = false;

static void HideModule(HMODULE hMod)
{
    PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
    if (!peb || !peb->Ldr) return;

    // PEB_LDR_DATA (real): Length(4) Initialized(1) pad(3) SsHandle(8)
    //   InLoadOrderModuleList        +0x10
    //   InMemoryOrderModuleList      +0x20
    //   InInitializationOrderModule  +0x30
    PLIST_ENTRY heads[3] = {
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x10),
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x20),
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x30),
    };
    const int linkOff[3] = { 0x00, 0x10, 0x20 };   // offsets in LDR entry

    for (int k = 0; k < 3; ++k) {
        for (PLIST_ENTRY e = heads[k]->Flink; e != heads[k]; e = e->Flink) {
            uintptr_t entry = reinterpret_cast<uintptr_t>(e) - linkOff[k];
            PVOID base = *reinterpret_cast<PVOID*>(entry + 0x30); // DllBase
            if (base == hMod) {
                s_SavedLinks[k] = e;
                s_SavedBlink[k] = e->Blink;
                e->Blink->Flink = e->Flink;
                e->Flink->Blink = e->Blink;
                break;
            }
        }
    }
    s_Unlinked = true;
}

static void RelinkModule()
{
    if (!s_Unlinked) return;
    for (int k = 0; k < 3; ++k) {
        PLIST_ENTRY e = s_SavedLinks[k];
        if (e && e->Blink) {
            e->Blink->Flink = e;
            e->Flink->Blink = e;
        }
    }
    s_Unlinked = false;
}

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- Feature loop (runs on its own thread, watched by the supervisor) ---
//  Generation check: when the supervisor detects the loop hung inside a
//  blocked native it bumps s_FeatureGen — the zombie loop exits as soon as
//  its blocked call returns, while a fresh loop takes over immediately.
static std::atomic<uint64_t> s_FeatureGen{0};
static std::atomic<int>      s_FeatureThreads{0};

//  g_TickStage values outside Features::Tick (1-13 sao os sub-ticks):
//    90=entry  91=loop-top  92=in-Tick call  93=watchdog  94=sleep
//    95=recovery-wait  96=ReInitialize  97=RestoreEntity  99=exiting
static void FeatureLoop()
{
    ++s_FeatureThreads;
    const uint64_t myGen = s_FeatureGen.load();
    Log::Write("Feature thread iniciada (gen %llu, threads=%d)",
               (unsigned long long)myGen, s_FeatureThreads.load());
    bool nativesOk = Natives::IsReady();
    int consecutiveErrors = 0;
    int recoveryWait = 0;
    int watchTicks = 0;
    int deadPings  = 0;
    Natives::g_TickStage = 90;

    while (g_Running && s_FeatureGen.load() == myGen) {
        Natives::g_TickStage = 91;
        Natives::g_TickBeat = NowMs();
        if (nativesOk) {
            Natives::g_TickStage = 92;
            __try {
                Features::Tick();
                consecutiveErrors = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                ++consecutiveErrors;
                Log::Write("EXCEPTION in Features::Tick(), code 0x%08X (#%d)",
                           GetExceptionCode(), consecutiveErrors);
                if (consecutiveErrors >= 25) {
                    // NAO mata as features pra sempre — restaura o ped
                    // (evita ficar invisivel/sem colisao) e tenta re-
                    // inicializar os natives sozinho a cada ~5s.
                    nativesOk = false;
                    recoveryWait = 0;
                    consecutiveErrors = 0;
                    Natives::MarkUnhealthy();
                    Log::Write("Features pausadas por excecoes — restaurando ped e agendando re-init.");
                    Natives::g_TickStage = 97;
                    __try { Features::RestoreEntity(); }   // mantem toggles do user
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // Watchdog: natives tambem morrem SEM jogar excecao (CallNative
            // retorna false, todos os paths falham). Nesse caso o tick roda
            // mas nada acontece — era o "menu fantasma". Pinga um nativo
            // seguro a cada ~2s; 2 falhas = declara morto e re-inicializa.
            Natives::g_TickStage = 93;
            if (++watchTicks >= 120) {   // 120 x 16ms ~= 2s
                watchTicks = 0;
                bool alive = false;
                __try { alive = Natives::Ping(); }
                __except (EXCEPTION_EXECUTE_HANDLER) { alive = false; }
                if (!alive) {
                    if (++deadPings >= 2) {
                        deadPings = 0;
                        nativesOk = false;
                        recoveryWait = 0;
                        Natives::MarkUnhealthy();
                        Log::Write("Watchdog: natives pararam de responder "
                                   "sem excecao — re-init agendado.");
                        Natives::g_TickStage = 97;
                        __try { Features::RestoreEntity(); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    deadPings = 0;
                }
            }
        } else {
            // auto-recovery: re-resolve + re-probe periodically
            Natives::g_TickStage = 95;
            if (++recoveryWait >= 312) {   // ~5s em passos de 16ms
                recoveryWait = 0;
                Log::Write("Tentando re-inicializar natives...");
                Natives::g_TickStage = 96;
                __try {
                    if (Natives::ReInitialize()) {
                        nativesOk = true;
                        Log::Write("Natives recuperados — features reativadas.");
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log::Write("Re-init threw (0x%08X), tentando de novo depois.",
                               GetExceptionCode());
                }
            }
        }
        Natives::g_TickStage = 94;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    Natives::g_TickStage = 99;
    --s_FeatureThreads;
    Log::Write("Feature thread saindo (gen %llu, threads=%d)",
               (unsigned long long)myGen, s_FeatureThreads.load());
}

//  CreateThread puro: std::thread teria destrutor e impediria __try no
//  MainThread (C2712)
static DWORD WINAPI FeatureThreadProc(LPVOID) { FeatureLoop(); return 0; }
static void SpawnFeatureLoop()
{
    HANDLE h = CreateThread(nullptr, 0, FeatureThreadProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// ---- Main cheat thread --------------------------------------------------
static void MainThread(HMODULE hModule)
{
    Log::Init();
    Log::Write("C4pt0r Cheats starting...");
    Log::Write("DLL loaded at 0x%p", hModule);

    // Drop off the PEB module lists before doing anything else
    HideModule(hModule);
    Log::Write("Module unlinked from PEB lists.");

    // Wait for game window
    Log::Write("Waiting for game window (grcWindow)...");
    int waitCount = 0;
    while (!FindWindowW(L"grcWindow", nullptr) && g_Running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        waitCount++;
        if (waitCount % 10 == 0) {
            Log::Write("Still waiting for grcWindow... (%d seconds)", waitCount / 2);
        }
    }

    if (!g_Running) {
        Log::Write("Aborted during window wait.");
        Log::Close();
        FreeLibraryAndExitThread(hModule, 0);
        return;
    }

    Log::Write("Game window found! Waiting 3s for full load...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ---- Initialize D3D11 hooks (ImGui menu) ----
    Log::Write("Initializing D3D11 hooks...");
    if (!Hooks::Initialize()) {
        Log::Write("FATAL: D3D11 hook failed. Aborting.");
        Log::Close();
        FreeLibraryAndExitThread(hModule, 0);
        return;
    }
    Log::Write("D3D11 hooks OK.");

    // ---- Initialize native system (with crash protection) ----
    Log::Write("Initializing native system...");
    bool nativesOk = false;
    for (int attempt = 0; attempt < 10 && g_Running; ++attempt) {
        __try {
            if (Natives::Initialize()) {
                nativesOk = true;
                break;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("EXCEPTION in Natives::Initialize() attempt %d, code 0x%08X",
                       attempt + 1, GetExceptionCode());
        }
        Log::Write("Native init attempt %d failed, retrying in 3s...", attempt + 1);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    if (!nativesOk) {
        Log::Write("WARNING: Native init failed. Game features disabled, menu still works.");
    } else {
        Log::Write("Native system OK.");
    }

    Log::Write("Entering supervisor loop. F9 = menu, END = unload.");

    // ---- Supervisor ----------------------------------------------------
    //  The feature loop lives on its own thread. If a native call blocks
    //  inside the engine (the "menu fantasma"), the heartbeat goes stale —
    //  we retire the in-flight path and respawn the loop so the cheat can
    //  NEVER stay dead. The zombie thread exits by itself via s_FeatureGen
    //  as soon as its blocked call returns.
    Natives::g_TickBeat = NowMs();
    SpawnFeatureLoop();

    while (g_Running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!g_Running) break;

        int64_t beat = Natives::g_TickBeat.load();
        int64_t staleMs = beat ? NowMs() - beat : 0;
        if (staleMs > 4000) {
            // muitos zumbis = o hang e sistemico (mutex preso, lock do
            // loader...). nao adianta spawnar mais — loga uma vez e para
            if (s_FeatureThreads.load() >= 8) {
                static bool warnedCap = false;
                if (!warnedCap) {
                    warnedCap = true;
                    Log::Write("SUPERVISOR: %d feature threads travadas — "
                               "hang sistemico, parando de respawnar.",
                               s_FeatureThreads.load());
                }
                continue;
            }
            uint64_t hungHash = Natives::g_InvokeHash.load();
            int      hungPath = Natives::g_InvokePath.load();
            Log::Write("SUPERVISOR: feature loop travado ha %lldms "
                       "(native 0x%016llX path %d stage %d) — aposentando "
                       "path e respawnando o loop.",
                       (long long)staleMs,
                       (unsigned long long)hungHash, hungPath,
                       Natives::g_TickStage.load());
            if (hungPath) Natives::RetirePath(hungPath);
            ++s_FeatureGen;                        // mata o loop velho quando destravar
            Natives::g_TickBeat = NowMs();
            SpawnFeatureLoop();                    // loop novo assume
            __try { Features::RestoreEntity(); }   // devolve o ped por seguranca
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

    // ---- Cleanup ----
    Log::Write("Shutting down...");
    // devolve o ped ao normal antes de sair — nunca deixar invisivel/
    // sem colisao preso depois do unload
    if (nativesOk || Natives::IsReady()) {
        __try { Features::RestoreAll(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Espera as feature threads sairem — se alguma esta travada dentro de
    // um native ela nunca volta, e liberar a DLL com codigo nosso em
    // execucao = crash. Nesse caso vazamos o modulo em vez de crashar.
    for (int i = 0; i < 40 && s_FeatureThreads.load() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool zombieLeft = s_FeatureThreads.load() > 0;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Hooks::Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (zombieLeft) {
        Log::Write("Feature thread travada no unload — saindo SEM FreeLibrary (modulo fica residente, sem crash).");
        Log::Close();
        return;
    }

    // Relink so FreeLibrary can find our LDR entry again
    RelinkModule();

    Log::Write("Unloaded cleanly. Bye!");
    Log::Close();
    FreeLibraryAndExitThread(hModule, 0);
}

// ---- DLL entry -----------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_Module = hModule;

        auto* t = new std::thread(MainThread, hModule);
        t->detach();
    }
    return TRUE;
}
