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

using namespace C4pt0r;

// ---- Main cheat thread --------------------------------------------------
static void MainThread(HMODULE hModule)
{
    Log::Init();
    Log::Write("C4pt0r Cheats starting...");
    Log::Write("DLL loaded at 0x%p", hModule);

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
    while (g_Running) {
        if (nativesOk) {
            __try {
                Features::Tick();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Write("EXCEPTION in Features::Tick(), code 0x%08X", GetExceptionCode());
                // Don't spam: disable features after crash
                nativesOk = false;
                Log::Write("Features disabled to prevent further crashes.");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // ---- Cleanup ----
    Log::Write("Shutting down...");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Hooks::Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
