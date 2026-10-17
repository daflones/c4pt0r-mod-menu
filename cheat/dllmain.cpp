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

    Log::Write("Entering main loop. F9 = menu, END = unload.");

    // ---- Main loop: tick features every frame ----
    int consecutiveErrors = 0;
    int recoveryWait = 0;
    int watchTicks = 0;
    int deadPings  = 0;
    while (g_Running) {
        if (nativesOk) {
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
                    __try { Features::RestoreAll(); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // Watchdog: natives tambem morrem SEM jogar excecao (CallNative
            // retorna false, todos os paths falham). Nesse caso o tick roda
            // mas nada acontece — era o "menu fantasma". Pinga um nativo
            // seguro a cada ~2s; 2 falhas = declara morto e re-inicializa.
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
                        __try { Features::RestoreAll(); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    deadPings = 0;
                }
            }
        } else {
            // auto-recovery: re-resolve + re-probe periodically
            if (++recoveryWait >= 312) {   // ~5s em passos de 16ms
                recoveryWait = 0;
                Log::Write("Tentando re-inicializar natives...");
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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // ---- Cleanup ----
    Log::Write("Shutting down...");
    // devolve o ped ao normal antes de sair — nunca deixar invisivel/
    // sem colisao preso depois do unload
    if (nativesOk || Natives::IsReady()) {
        __try { Features::RestoreAll(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Hooks::Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
