#pragma once

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

    struct AimbotConfig {
        bool  enabled     = false;
        float fov         = 15.0f;
        float smooth      = 5.0f;
        bool  visibleOnly = true;
        int   targetBone  = 0;
    };

    struct ESPConfig {
        bool  enabled     = false;
        bool  boxes       = true;
        bool  names       = true;
        bool  healthBars  = true;
        bool  snaplines   = false;
        bool  distance    = true;
        int   filter      = 0;
        float maxDistance  = 500.0f;
        float boxColor[4]  = { 0.00f, 0.83f, 1.00f, 1.00f };
        float nameColor[4] = { 1.00f, 1.00f, 1.00f, 1.00f };
        float godColor[4]  = { 1.00f, 0.70f, 0.10f, 1.00f };
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
        bool  toWaypoint = false;
        float savedX = 0, savedY = 0, savedZ = 0;
        bool  hasSaved   = false;
        bool  toSaved    = false;

        bool  toCoords   = false;
        float tx = 0, ty = 0, tz = 0;
        int   toPlayerId    = -1;
        int   bringPlayerId = -1;
    };

    struct VehicleConfig {
        bool  spawn    = false;
        bool  warpIn   = true;
        char  model[48] = {};
        uint32_t modelHash = 0;

        int   postPrim = -1, postSec = -1;
        bool  postCustom = false;
        float postPrimRgb[3] = {};
        float postSecRgb[3]  = {};
    };

    struct GarageEntry {
        char     name[32] = {};
        uint32_t model    = 0;
        int      prim = -1, sec = -1;
        bool     custom = false;
        float    primRgb[3] = {};
        float    secRgb[3]  = {};
    };

    struct FunConfig {

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

        bool  setWeather     = false; int  weather  = 0;
        bool  clearWeather   = false;
        bool  setClock       = false; int  clockH   = 12; int clockM = 0;
        bool  setGravity     = false; int  gravity  = 0;
        bool  explode        = false;
        bool  fire           = false;
        bool  water          = false;

        bool  spawnPed       = false;
        char  pedModel[48]   = {};
        bool  pedBodyguard   = false;
        bool  pedHostile     = false;
        bool  pigRain        = false;
        bool  clone          = false;

        bool  repairVeh      = false;
        bool  flipVeh        = false;
        bool  boostVeh       = false;
    };

    struct MechanicConfig {
        bool  useNearest   = false;

        bool  applyMods    = false;
        bool  applyColors  = false;
        bool  applyExtras  = false;
        bool  applyPower   = false;
        bool  plate        = false;
        bool  saveGarage   = false;
        char  plateText[16] = {};
        char  status[96]   = {};

        int   engine = -1;
        int   brakes = -1;
        int   trans  = -1;
        int   susp   = -1;
        int   armor  = -1;
        int   horn   = -1;
        int   wheels = -1;
        int   wheelType = -1;
        int   livery = -1;
        bool  turbo   = false;
        bool  xenon   = false;
        bool  bpTires = false;

        int   colPrim  = -1;
        int   colSec   = -1;
        int   pearl    = -1;
        int   wheelCol = -1;
        int   winTint  = -1;
        bool  useCustomCol = false;
        float primRgb[3] = { 1.00f, 0.23f, 0.23f };
        float secRgb[3]  = { 0.12f, 0.12f, 0.12f };

        bool  neonOn   = false;
        float neonRgb[3]  = { 0.00f, 0.83f, 1.00f };
        float smokeRgb[3] = { 0.00f, 0.83f, 1.00f };

        float engineMult = 0.f;
        float torqueMult = 0.f;
    };

    struct PlayerActionsConfig {
        int   giveGodId    = -1;
        int   reviveId     = -1;
        int   grabId       = -1;
        bool  releaseGrab  = false;
        bool  grabActive   = false;
    };

    struct LuaConfig {
        char  eventName[96] = "";
        int   eventArg      = -1;
        bool  run        = false;
        int   preset     = 0;
        int   targetId   = -1;
    };

    struct WeaponConfig {
        bool  give        = false;
        char  model[48]   = "weapon_carbinerifle";
        int   ammo        = 250;
        bool  attachments = false;
        bool  giveAll     = false;
        bool  stripAll    = false;
        bool  refillAll   = false;
    };

    struct MenuConfig {
        int  toggleKey    = VK_F9;
        bool isOpen       = true;
        int  currentTab   = 0;
        bool showWatermark = true;
    };

    struct FarmConfig {
        bool  running      = false;
        bool  hasPointA    = false;
        float ax = 0, ay = 0, az = 0;
        bool  hasPointB    = false;
        float bx = 0, by = 0, bz = 0;

        int   keyControl   = 51;
        int   holdFrames   = 2;
        int   settleMs     = 1200;
        int   intervalMs   = 1500;
        int   repeatTimes  = 0;
        int   cyclesDone   = 0;

        bool  inspEnabled  = false;
    };

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

        int   pressed[8] = {};
    };

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
        bool  god      = false;
    };

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

    inline ID3D11Device*            g_Device       = nullptr;
    inline ID3D11DeviceContext*     g_DeviceContext = nullptr;
    inline IDXGISwapChain*          g_SwapChain    = nullptr;
    inline ID3D11RenderTargetView*  g_RenderTarget = nullptr;

    constexpr int MAX_PLAYERS = 256;
    inline PlayerESPData    g_PlayersData[MAX_PLAYERS] = {};
    inline PlayerListEntry  g_PlayerList[MAX_PLAYERS] = {};
    inline int              g_PlayerListCount = 0;

    inline std::timed_mutex g_DataMutex;
    inline VehicleConfig    g_Vehicle;
    inline FunConfig        g_Fun;
    inline MechanicConfig   g_Mech;
    inline WeaponConfig     g_Weapon;
    inline PlayerActionsConfig g_PActions;
    inline LuaConfig           g_Lua;
    inline std::vector<GarageEntry> g_Garage;
    inline std::mutex               g_GarageMtx;
    inline int           g_ScreenWidth  = 1920;
    inline int           g_ScreenHeight = 1080;

    inline std::atomic<bool> g_InitDone{false};
}
