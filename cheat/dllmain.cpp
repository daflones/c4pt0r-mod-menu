// ============================================================================
//  C4pt0r Cheats — DLL Entry Point
//  Spawns main thread, initializes subsystems, runs feature loop
// ============================================================================

#include "globals.h"
#include "hooks.h"
#include "natives.h"
#include "features.h"

#include <thread>
#include <chrono>

using namespace C4pt0r;

// ---- Main cheat thread --------------------------------------------------
static void MainThread(HMODULE hModule)
{
    // Give the game time to fully load
    // Wait for the game window to appear
    while (!FindWindowW(L"grcWindow", nullptr) && g_Running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));

    if (!g_Running) return;

    // ---- Initialize native system ----
    bool nativesOk = false;
    for (int attempt = 0; attempt < 30 && g_Running; ++attempt) {
        if (Natives::Initialize()) {
            nativesOk = true;
            break;
        }
        // Natives might not be registered yet — retry
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (!nativesOk) {
        OutputDebugStringA("[C4pt0r] Native init failed after retries. Features disabled.\n");
    }

    // ---- Initialize D3D11 hooks (ImGui menu) ----
    if (!Hooks::Initialize()) {
        OutputDebugStringA("[C4pt0r] D3D11 hook failed. Aborting.\n");
        g_Running = false;
        FreeLibraryAndExitThread(hModule, 0);
        return;
    }

    OutputDebugStringA("[C4pt0r] All systems go. Entering main loop.\n");

    // ---- Main loop: tick features every frame ----
    while (g_Running) {
        Features::Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 ticks/s
    }

    // ---- Cleanup ----
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Hooks::Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    OutputDebugStringA("[C4pt0r] Unloaded cleanly.\n");
    FreeLibraryAndExitThread(hModule, 0);
}

// ---- DLL entry -----------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_Module = hModule;

        // Spawn main thread detached — FreeLibraryAndExitThread handles cleanup
        auto* t = new std::thread(MainThread, hModule);
        t->detach();
    }
    return TRUE;
}
