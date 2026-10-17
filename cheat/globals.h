#pragma once
// ============================================================================
//  C4pt0r Cheats — Global State & Configuration
//  FiveM internal cheat — DLL side
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include <atomic>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace C4pt0r
{
    // ------------------------------------------------------------------ Math
    struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x, float y) : x(x), y(y) {}
    };

    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

        float Distance(const Vec3& o) const {
            float dx = x - o.x, dy = y - o.y, dz = z - o.z;
            return sqrtf(dx * dx + dy * dy + dz * dz);
        }
        Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        float Length() const { return sqrtf(x * x + y * y + z * z); }
        Vec3 Normalized() const {
            float l = Length();
            return (l > 0.0001f) ? Vec3(x / l, y / l, z / l) : Vec3();
        }
    };

    // ------------------------------------------------------- Feature configs
    struct AimbotConfig {
        bool  enabled     = false;
        float fov         = 15.0f;   // degrees
        float smooth      = 5.0f;    // 1 = snap, higher = smoother
        bool  visibleOnly = true;
        int   targetBone  = 0;       // 0 = head, 1 = neck, 2 = chest
    };

    struct ESPConfig {
        bool  enabled     = false;
        bool  boxes       = true;
        bool  names       = true;
        bool  healthBars  = true;
        bool  snaplines   = false;
        bool  distance    = true;
        int   filter      = 0;      // 0=todos 1=so normais 2=so god/admin
        float maxDistance  = 500.0f;
        float boxColor[4]  = { 0.00f, 0.83f, 1.00f, 1.00f };  // cyan
        float nameColor[4] = { 1.00f, 1.00f, 1.00f, 1.00f };
        float godColor[4]  = { 1.00f, 0.70f, 0.10f, 1.00f };  // dourado
    };

    struct PlayerConfig {
        bool  godmode         = false;
        bool  invisible       = false;
        bool  noRecoil        = false;
        bool  infiniteAmmo    = false;
        bool  speedHack       = false;
        float speedMultiplier = 1.5f;
        bool  noclip          = false;
        float noclipSpeed     = 2.0f;
    };

    struct TeleportConfig {
        bool  toWaypoint = false;   // one-shot flag
        float savedX = 0, savedY = 0, savedZ = 0;
        bool  hasSaved   = false;
        bool  toSaved    = false;   // one-shot flag
        // one-shot targets consumed by TickTeleport
        bool  toCoords   = false;
        float tx = 0, ty = 0, tz = 0;
        int   toPlayerId    = -1;   // >=0 -> go to that player's position
        int   bringPlayerId = -1;   // >=0 -> move that player to us
    };

    struct VehicleConfig {
        bool  spawn    = false;     // one-shot flag
        bool  warpIn   = true;      // enter vehicle after spawn
        char  model[48] = {};
    };

    // ---------------------------------------------- Mundo / diversao
    struct FunConfig {
        // toggles — re-applied while on (all OFF at inject)
        bool  superJump      = false;
        bool  noWanted       = false;
        bool  policeIgnore   = false;
        bool  everyoneIgnore = false;
        bool  drunk          = false;
        bool  thermal        = false;
        bool  nightvision    = false;
        bool  slowmo         = false;
        float timeScale      = 0.30f;
        bool  pauseTime      = false;

        // one-shot world controls
        bool  setWeather     = false; int  weather  = 0;
        bool  clearWeather   = false;
        bool  setClock       = false; int  clockH   = 12; int clockM = 0;
        bool  setGravity     = false; int  gravity  = 0;
        bool  explode        = false;
        bool  fire           = false;
        bool  water          = false;

        // ped/animal/npc spawning
        bool  spawnPed       = false;
        char  pedModel[48]   = {};
        bool  pedBodyguard   = false;
        bool  pedHostile     = false;
        bool  pigRain        = false;
        bool  clone          = false;

        // vehicle fun
        bool  repairVeh      = false;
        bool  flipVeh        = false;
        bool  boostVeh       = false;
    };

    struct MenuConfig {
        int  toggleKey    = VK_F9;
        bool isOpen       = true;
        int  currentTab   = 0;
        bool showWatermark = true;
    };

    // ------------------------------------------------- Farm / macro config
    struct FarmConfig {
        bool  running      = false;   // master switch for the farm loop
        bool  hasPointA    = false;   // "coleta" marker position
        float ax = 0, ay = 0, az = 0;
        bool  hasPointB    = false;   // "venda" marker position
        float bx = 0, by = 0, bz = 0;

        int   keyControl   = 51;      // INPUT_CONTEXT (E) — RP jobs default
        int   holdFrames   = 2;       // frames the key stays pressed
        int   settleMs     = 1200;    // wait after teleport before pressing
        int   intervalMs   = 1500;    // wait after key before next step
        int   repeatTimes  = 0;       // 0 = forever
        int   cyclesDone   = 0;       // progress counter

        // live inspector state (written by Tick, read by menu)
        bool  inspEnabled  = false;   // OFF by default — user opts in
    };

    // Live inspector snapshot — refreshed every tick when enabled
    struct InspectorData {
        int   ped        = 0;
        int   player     = 0;
        Vec3  pos;
        float heading    = 0.f;
        int   health     = 0;
        int   armor      = 0;
        float speed      = 0.f;
        bool  inVehicle  = false;
        int   vehicle    = 0;
        uint32_t weapon  = 0;
        char  name[64]   = {};
        // last-pressed control ids this frame (for action capture)
        int   pressed[8] = {};
    };

    // ---------------------------------------- ESP shared data (thread-safe)
    struct PlayerESPData {
        bool  active   = false;
        int   playerId = -1;
        int   ped      = 0;
        Vec3  worldPos;
        Vec2  screenPos;
        bool  onScreen = false;
        float distance = 0.f;
        int   health   = 0;
        int   maxHealth = 0;
        int   armor    = 0;
        char  name[64] = {};
        bool  isDead   = false;
        bool  inVehicle = false;
        bool  god      = false;   // invencivel -> provavel admin
    };

    // Lightweight row for the player list (menu teleport/inspect)
    struct PlayerListEntry {
        int   id        = -1;
        int   ped       = 0;
        float distance  = 0.f;
        int   health    = 0;
        int   armor     = 0;
        bool  inVehicle = false;
        bool  dead      = false;
        char  name[64]  = {};
    };

    // ------------------------------------------------------- Global singletons
    inline AimbotConfig    g_Aimbot;
    inline ESPConfig       g_ESP;
    inline PlayerConfig    g_Player;
    inline TeleportConfig  g_Teleport;
    inline MenuConfig      g_Menu;
    inline FarmConfig      g_Farm;
    inline InspectorData   g_Inspector;

    inline std::atomic<bool> g_Running{ true };
    inline HMODULE           g_Module   = nullptr;
    inline HWND              g_GameWindow = nullptr;

    // D3D11
    inline ID3D11Device*            g_Device       = nullptr;
    inline ID3D11DeviceContext*     g_DeviceContext = nullptr;
    inline IDXGISwapChain*          g_SwapChain    = nullptr;
    inline ID3D11RenderTargetView*  g_RenderTarget = nullptr;

    // ESP shared buffer
    constexpr int MAX_PLAYERS = 256;
    inline PlayerESPData    g_PlayersData[MAX_PLAYERS] = {};
    inline PlayerListEntry  g_PlayerList[MAX_PLAYERS] = {};
    inline int              g_PlayerListCount = 0;
    // timed_mutex + try_lock: um zumbi segurando esse lock (morreu dentro
    // de um native) nao pode mais matar toda thread nova no lock_guard —
    // foi isso que criou o loop infinito de respawns mortos
    inline std::timed_mutex g_DataMutex;
    inline VehicleConfig    g_Vehicle;
    inline FunConfig        g_Fun;
    inline int           g_ScreenWidth  = 1920;
    inline int           g_ScreenHeight = 1080;
    // setado pelo MainThread quando o init de natives termina (ok ou nao) —
    // a feature thread espera isso antes de começar a tickar
    inline std::atomic<bool> g_InitDone{false};
}
