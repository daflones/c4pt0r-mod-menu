// ============================================================================
//  C4pt0r Cheats — Native Invocation Implementation
//  Pattern scans for FiveM's GetNativeHandler, wraps all common natives
// ============================================================================

#include "natives.h"
#include "memory.h"
#include <cstdio>

namespace C4pt0r::Natives
{
    // ---- Internal state -------------------------------------------------
    static GetNativeHandlerFn s_GetNativeHandler = nullptr;
    static bool               s_Ready            = false;

    // ---- Initialization -------------------------------------------------
    bool Initialize()
    {
        // Pattern scan for rage::scrEngine::GetNativeHandler
        // This maps a 64-bit native hash → function pointer.
        // Patterns change between FiveM builds — update as needed.

        const char* patterns[] = {
            "48 8D 0D ?? ?? ?? ?? 48 8B 14 FA E8 ?? ?? ?? ??",
            "E8 ?? ?? ?? ?? 48 85 C0 75 ?? 48 8D 0D",
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8D 0D",
            nullptr
        };

        // First try the main game module
        uintptr_t addr = 0;
        for (int i = 0; patterns[i] && !addr; ++i)
            addr = Memory::PatternScanModule(nullptr, patterns[i]);

        // Fallback: scan FiveM-specific modules
        if (!addr) {
            const wchar_t* modules[] = {
                L"citizen-scripting-core.dll",
                L"citizen-game-main.dll",
                L"rage-scripting-five.dll",
                nullptr
            };
            for (int m = 0; modules[m] && !addr; ++m)
                for (int p = 0; patterns[p] && !addr; ++p)
                    addr = Memory::PatternScanModule(modules[m], patterns[p]);
        }

        if (!addr) {
            OutputDebugStringA("[C4pt0r] Native handler pattern not found.\n");
            return false;
        }

        // Resolve — if first byte is CALL (E8), follow the relative target
        uint8_t fb = *reinterpret_cast<uint8_t*>(addr);
        if (fb == 0xE8) {
            s_GetNativeHandler = reinterpret_cast<GetNativeHandlerFn>(
                Memory::ResolveRIP(addr, 1, 5));
        } else {
            // Search forward for a CALL within the matched region
            for (int i = 0; i < 48; ++i) {
                if (*reinterpret_cast<uint8_t*>(addr + i) == 0xE8) {
                    s_GetNativeHandler = reinterpret_cast<GetNativeHandlerFn>(
                        Memory::ResolveRIP(addr + i, 1, 5));
                    break;
                }
            }
        }

        if (s_GetNativeHandler) {
            s_Ready = true;
            OutputDebugStringA("[C4pt0r] Natives initialized.\n");
        } else {
            OutputDebugStringA("[C4pt0r] Failed to resolve GetNativeHandler.\n");
        }
        return s_Ready;
    }

    bool IsReady() { return s_Ready; }

    NativeHandler GetHandler(uint64_t hash) {
        if (!s_GetNativeHandler) return nullptr;
        return s_GetNativeHandler(hash);
    }

    // ---- Vec3-returning natives -----------------------------------------
    // GTA V returns Vector3 via the result buffer as three packed floats
    Vec3 InvokeVec3(uint64_t hash, uint64_t arg0, uint64_t arg1)
    {
        auto handler = GetHandler(hash);
        if (!handler) return {};
        NativeCallContext ctx;
        if (arg0) ctx.Push(arg0);
        if (arg1) ctx.Push(arg1);
        handler(&ctx);
        auto* r = reinterpret_cast<float*>(ctx.returnValue);
        return Vec3(r[0], r[1], r[2]);
    }

    // ====================================================================
    //  High-level wrappers
    // ====================================================================

    int PlayerPedId() {
        return Invoke<int>(Hash::PLAYER_PED_ID);
    }
    int PlayerId() {
        return Invoke<int>(Hash::PLAYER_ID);
    }
    int GetPlayerPed(int player) {
        return Invoke<int>(Hash::GET_PLAYER_PED, player);
    }
    bool IsPlayerPlaying(int player) {
        return Invoke<bool>(Hash::IS_PLAYER_PLAYING, player);
    }
    bool NetworkIsPlayerActive(int player) {
        return Invoke<bool>(Hash::NETWORK_IS_PLAYER_ACTIVE, player);
    }
    const char* GetPlayerName(int player) {
        return Invoke<const char*>(Hash::GET_PLAYER_NAME, player);
    }

    // Entity ---
    Vec3 GetEntityCoords(int entity, bool alive) {
        return InvokeVec3(Hash::GET_ENTITY_COORDS,
                          static_cast<uint64_t>(entity),
                          static_cast<uint64_t>(alive));
    }
    void SetEntityCoordsNoOffset(int entity, float x, float y, float z) {
        InvokeVoid(Hash::SET_ENTITY_COORDS_NO_OFFSET,
                   entity, x, y, z, (int)false, (int)false, (int)false);
    }
    void SetEntityInvincible(int entity, bool toggle) {
        InvokeVoid(Hash::SET_ENTITY_INVINCIBLE, entity, (int)toggle);
    }
    void SetEntityVisible(int entity, bool toggle, bool p2) {
        InvokeVoid(Hash::SET_ENTITY_VISIBLE, entity, (int)toggle, (int)p2);
    }
    int GetEntityHealth(int entity) {
        return Invoke<int>(Hash::GET_ENTITY_HEALTH, entity);
    }
    int GetEntityMaxHealth(int entity) {
        return Invoke<int>(Hash::GET_ENTITY_MAX_HEALTH, entity);
    }
    bool IsEntityDead(int entity) {
        return Invoke<bool>(Hash::IS_ENTITY_DEAD, entity, (int)false);
    }
    void FreezeEntityPosition(int entity, bool toggle) {
        InvokeVoid(Hash::FREEZE_ENTITY_POSITION, entity, (int)toggle);
    }
    void SetEntityCollision(int entity, bool toggle, bool keepPhysics) {
        InvokeVoid(Hash::SET_ENTITY_COLLISION, entity, (int)toggle, (int)keepPhysics);
    }
    void SetEntityHasGravity(int entity, bool toggle) {
        InvokeVoid(Hash::SET_ENTITY_HAS_GRAVITY, entity, (int)toggle);
    }

    // Ped ---
    bool IsPedShooting(int ped) {
        return Invoke<bool>(Hash::IS_PED_SHOOTING, ped);
    }
    Vec3 GetPedBoneCoords(int ped, int boneId) {
        auto handler = GetHandler(Hash::GET_PED_BONE_COORDS);
        if (!handler) return {};
        NativeCallContext ctx;
        ctx.Push(ped);
        ctx.Push(boneId);
        ctx.Push(0.0f);  // offsetX
        ctx.Push(0.0f);  // offsetY
        ctx.Push(0.0f);  // offsetZ
        handler(&ctx);
        auto* r = reinterpret_cast<float*>(ctx.returnValue);
        return Vec3(r[0], r[1], r[2]);
    }
    int GetPedArmour(int ped) {
        return Invoke<int>(Hash::GET_PED_ARMOUR, ped);
    }
    bool HasClearLOS(int e1, int e2, int traceType) {
        return Invoke<bool>(Hash::HAS_ENTITY_CLEAR_LOS_TO_ENTITY, e1, e2, traceType);
    }

    // Camera ---
    Vec3 GetGameplayCamRot(int order) {
        return InvokeVec3(Hash::GET_GAMEPLAY_CAM_ROT, static_cast<uint64_t>(order), 0);
    }
    Vec3 GetGameplayCamCoord() {
        return InvokeVec3(Hash::GET_GAMEPLAY_CAM_COORD, 0, 0);
    }
    float GetGameplayCamRelativePitch() {
        return Invoke<float>(Hash::GET_GAMEPLAY_CAM_RELATIVE_PITCH);
    }
    float GetGameplayCamRelativeHeading() {
        return Invoke<float>(Hash::GET_GAMEPLAY_CAM_RELATIVE_HEADING);
    }
    void SetGameplayCamRelativeHeading(float h) {
        InvokeVoid(Hash::SET_GAMEPLAY_CAM_RELATIVE_HEADING, h);
    }
    void SetGameplayCamRelativePitch(float p, float smoothSpeed) {
        InvokeVoid(Hash::SET_GAMEPLAY_CAM_RELATIVE_PITCH, p, smoothSpeed);
    }

    // Player misc ---
    void SetRunSprintMultiplier(int player, float mult) {
        InvokeVoid(Hash::SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, player, mult);
    }

    // World → screen ---
    bool WorldToScreen(Vec3 world, Vec2& screen) {
        auto handler = GetHandler(Hash::GET_SCREEN_COORD_FROM_WORLD_COORD);
        if (!handler) return false;
        NativeCallContext ctx;
        ctx.Push(world.x);
        ctx.Push(world.y);
        ctx.Push(world.z);
        float sx = 0, sy = 0;
        ctx.Push(reinterpret_cast<uint64_t>(&sx));
        ctx.Push(reinterpret_cast<uint64_t>(&sy));
        handler(&ctx);
        bool result = ctx.GetResult<bool>();
        if (result) {
            screen.x = sx;
            screen.y = sy;
        }
        return result;
    }

    // Waypoint ---
    bool IsWaypointActive() {
        // Blip sprite 8 = waypoint
        int blip = Invoke<int>(Hash::GET_FIRST_BLIP_INFO_ID, 8);
        return Invoke<bool>(Hash::DOES_BLIP_EXIST, blip);
    }

    Vec3 GetWaypointCoords() {
        int blip = Invoke<int>(Hash::GET_FIRST_BLIP_INFO_ID, 8);
        if (!Invoke<bool>(Hash::DOES_BLIP_EXIST, blip)) return {};
        return InvokeVec3(Hash::GET_BLIP_INFO_ID_COORD, static_cast<uint64_t>(blip), 0);
    }
}
