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
        float maxDistance  = 500.0f;
        float boxColor[4]  = { 0.00f, 0.83f, 1.00f, 1.00f };  // cyan
        float nameColor[4] = { 1.00f, 1.00f, 1.00f, 1.00f };
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
    };

    struct MenuConfig {
        int  toggleKey    = VK_F9;
        bool isOpen       = true;
        int  currentTab   = 0;
        bool showWatermark = true;
    };

    // ---------------------------------------- ESP shared data (thread-safe)
    struct PlayerESPData {
        bool  active   = false;
        Vec3  worldPos;
        Vec2  screenPos;
        bool  onScreen = false;
        float distance = 0.f;
        int   health   = 0;
        int   maxHealth = 0;
        int   armor    = 0;
        char  name[64] = {};
        bool  isDead   = false;
    };

    // ------------------------------------------------------- Global singletons
    inline AimbotConfig    g_Aimbot;
    inline ESPConfig       g_ESP;
    inline PlayerConfig    g_Player;
    inline TeleportConfig  g_Teleport;
    inline MenuConfig      g_Menu;

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
    inline PlayerESPData g_PlayersData[MAX_PLAYERS] = {};
    inline std::mutex    g_DataMutex;
    inline int           g_ScreenWidth  = 1920;
    inline int           g_ScreenHeight = 1080;
}
