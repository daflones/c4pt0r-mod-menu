

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

static PLIST_ENTRY s_SavedLinks[3]  = {};
static PLIST_ENTRY s_SavedBlink[3]  = {};
static bool        s_Unlinked       = false;

static void HideModule(HMODULE hMod)
{
    PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
    if (!peb || !peb->Ldr) return;

    PLIST_ENTRY heads[3] = {
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x10),
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x20),
        reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<uintptr_t>(peb->Ldr) + 0x30),
    };
    const int linkOff[3] = { 0x00, 0x10, 0x20 };

    for (int k = 0; k < 3; ++k) {
        for (PLIST_ENTRY e = heads[k]->Flink; e != heads[k]; e = e->Flink) {
            uintptr_t entry = reinterpret_cast<uintptr_t>(e) - linkOff[k];
            PVOID base = *reinterpret_cast<PVOID*>(entry + 0x30);
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

static std::atomic<uint64_t> s_FeatureGen{0};
static std::atomic<int>      s_FeatureThreads{0};

static void FeatureLoop()
{

    Natives::g_TickStage = 89;
    int waited = 0;
    while (g_Running && !g_InitDone.load() && waited < 400) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++waited;
    }
    ++s_FeatureThreads;
    const uint64_t myGen = s_FeatureGen.load();
    Log::Write("Feature thread iniciada (gen %llu, threads=%d)",
               (unsigned long long)myGen, s_FeatureThreads.load());

    while (g_Running && s_FeatureGen.load() == myGen) {
        Features::FrameTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    Natives::g_TickStage = 99;
    --s_FeatureThreads;
    Log::Write("Feature thread saindo (gen %llu, threads=%d)",
               (unsigned long long)myGen, s_FeatureThreads.load());
}

static DWORD WINAPI FeatureThreadProc(LPVOID) { FeatureLoop(); return 0; }
static void SpawnFeatureLoop()
{
    HANDLE h = CreateThread(nullptr, 0, FeatureThreadProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

static void MainThread(HMODULE hModule)
{
    Log::Init();
    Log::Write("C4pt0r Cheats starting...");
    Log::Write("DLL loaded at 0x%p", hModule);

    HideModule(hModule);
    Log::Write("Module unlinked from PEB lists.");

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

    Log::Write("Initializing D3D11 hooks...");
    if (!Hooks::Initialize()) {
        Log::Write("FATAL: D3D11 hook failed. Aborting.");
        Log::Close();
        FreeLibraryAndExitThread(hModule, 0);
        return;
    }
    Log::Write("D3D11 hooks OK.");

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
    g_InitDone = true;

    Log::Write("Entering supervisor loop. F9 = menu, END = unload.");

    Natives::g_TickBeat = NowMs();

    while (g_Running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!g_Running) break;

        int64_t beat = Natives::g_TickBeat.load();
        int64_t staleMs = beat ? NowMs() - beat : 0;
        if (staleMs > 4000) {

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
                       "(native 0x%016llX path %d stage %d threads=%d) — "
                       "aposentando path e respawnando o loop.",
                       (long long)staleMs,
                       (unsigned long long)hungHash, hungPath,
                       Natives::g_TickStage.load(),
                       s_FeatureThreads.load());
            if (hungPath) Natives::RetirePath(hungPath);
            ++s_FeatureGen;
            Natives::g_TickBeat = NowMs();

            static int deadSpawns = 0;
            if (deadSpawns < 3) {
                SpawnFeatureLoop();
                if (s_FeatureThreads.load() == 0) ++deadSpawns;
                else deadSpawns = 0;
                if (deadSpawns >= 3)
                    Log::Write("SUPERVISOR: novas threads nao iniciam "
                               "(bloqueio pos-injecao) — respawn desligado.");
            }
            __try { Features::RestoreEntity(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

    Log::Write("Shutting down...");

    if (nativesOk || Natives::IsReady()) {
        __try { Features::RestoreAll(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

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

    RelinkModule();

    Log::Write("Unloaded cleanly. Bye!");
    Log::Close();
    FreeLibraryAndExitThread(hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID )
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_Module = hModule;

        auto* t = new std::thread(MainThread, hModule);
        t->detach();
        auto* f = new std::thread(FeatureThreadProc, nullptr);
        f->detach();
    }
    return TRUE;
}
