#pragma once
// ============================================================================
//  C4pt0r Cheats — FiveM / GTA V Native Invocation
//  Finds GetNativeHandler via pattern scan, provides typed wrappers
// ============================================================================

#include "globals.h"
#include <cstdint>

namespace C4pt0r::Natives
{
    // ---- Native call context (matches RAGE scrNativeCallContext) ---------
    struct NativeCallContext {
        uint64_t* returnValue;
        uint32_t  argCount;
        uint32_t  pad0;
        uint64_t* args;

        // Internal storage
        uint64_t resultBuffer[4];
        uint64_t argBuffer[32];

        NativeCallContext() {
            returnValue = resultBuffer;
            args        = argBuffer;
            argCount    = 0;
            pad0        = 0;
            memset(argBuffer, 0, sizeof(argBuffer));
            memset(resultBuffer, 0, sizeof(resultBuffer));
        }

        void Reset() {
            argCount    = 0;
            returnValue = resultBuffer;
            args        = argBuffer;
            memset(argBuffer, 0, sizeof(argBuffer));
            memset(resultBuffer, 0, sizeof(resultBuffer));
        }

        template <typename T>
        void Push(T value) {
            static_assert(sizeof(T) <= 8);
            *reinterpret_cast<T*>(&argBuffer[argCount]) = value;
            ++argCount;
        }

        template <typename T>
        T GetResult() const {
            return *reinterpret_cast<const T*>(returnValue);
        }
    };

    // ---- Handler types --------------------------------------------------
    using NativeHandler       = void(__cdecl*)(NativeCallContext*);
    // rage::scrEngine::GetNativeHandler(registrationTable*, hash)
    // verified on FiveM b3258: resolver takes the table as arg1 — the table
    // is a static 256-entry bucket array, not retrievable from the hash alone
    using GetNativeHandlerFn  = NativeHandler(*)(uintptr_t registrationTable, uint64_t nativeHash);

    // ---- Core API -------------------------------------------------------
    bool           Initialize();
    bool           IsReady();
    NativeHandler  GetHandler(uint64_t hash);
    void           LogMissingHash(uint64_t hash);   // reports each dead hash once

    // Unified call: tries scripting-gta CallNativeHandler first, then
    // rage::scrEngine::GetNativeHandler, then the raw table walk.
    // args = qword-packed arguments (each arg in its own slot), ret = 4-qword
    // result buffer. Returns false when no invocation path succeeded.
    bool           CallNative(uint64_t hash, const uint64_t* args, int nargs,
                              uint64_t* ret);

    // ---- Typed invoke ---------------------------------------------------
    namespace detail {
        inline void PackArgs(uint64_t* dst) { (void)dst; }
        template <typename T, typename... Rest>
        inline void PackArgs(uint64_t* dst, T value, Rest... rest) {
            static_assert(sizeof(T) <= 8);
            *reinterpret_cast<T*>(dst) = value;
            PackArgs(dst + 1, rest...);
        }
    }

    template <typename R, typename... Args>
    inline R Invoke(uint64_t hash, Args... args) {
        uint64_t packed[sizeof...(Args) > 0 ? sizeof...(Args) : 1] = {};
        detail::PackArgs(packed, args...);
        uint64_t ret[4] = {};
        if (!CallNative(hash, packed, static_cast<int>(sizeof...(Args)), ret)) {
            LogMissingHash(hash);
            return R{};
        }
        return *reinterpret_cast<R*>(ret);
    }

    // void specialization
    template <typename... Args>
    inline void InvokeVoid(uint64_t hash, Args... args) {
        uint64_t packed[sizeof...(Args) > 0 ? sizeof...(Args) : 1] = {};
        detail::PackArgs(packed, args...);
        if (!CallNative(hash, packed, static_cast<int>(sizeof...(Args)), nullptr))
            LogMissingHash(hash);
    }

    // Vec3-returning natives (special handling)
    // explicitN < 0 = legacy behavior (skip zero args); otherwise push
    // exactly explicitN args — needed when a legit arg can be 0/false.
    Vec3 InvokeVec3(uint64_t hash, uint64_t arg0 = 0, uint64_t arg1 = 0,
                    int explicitN = -1);

    // ---- Common native hashes -------------------------------------------
    namespace Hash
    {
        // Player
        constexpr uint64_t PLAYER_PED_ID                        = 0xD80958FC74E988A6;
        constexpr uint64_t PLAYER_ID                            = 0x4F8644AF03DED0E0;
        constexpr uint64_t GET_PLAYER_PED                       = 0x43A66C31C68491C0;
        constexpr uint64_t IS_PLAYER_PLAYING                    = 0x5E9564D8246B909A;
        constexpr uint64_t GET_PLAYER_NAME                      = 0x6D0DE6A7B5DA71F8;
        constexpr uint64_t SET_PLAYER_INVINCIBLE                = 0x239528EACDC3E7DE;
        constexpr uint64_t SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER = 0x6DB47AA77FD94E09;
        constexpr uint64_t NETWORK_IS_PLAYER_ACTIVE             = 0xB8DFD30D6973E135;

        // Entity
        constexpr uint64_t GET_ENTITY_COORDS                    = 0x3FEF770D40960D5A;
        constexpr uint64_t SET_ENTITY_COORDS_NO_OFFSET          = 0x239A3351AC1DA385;
        constexpr uint64_t SET_ENTITY_VISIBLE                   = 0xEA1C610A04DB6BBB;
        constexpr uint64_t SET_ENTITY_INVINCIBLE                = 0x3882114BDE571AD4;
        constexpr uint64_t GET_ENTITY_HEALTH                    = 0xEEF059FAD016D209;
        constexpr uint64_t GET_ENTITY_MAX_HEALTH                = 0x15D757606D170C3C;
        constexpr uint64_t SET_ENTITY_HEALTH                    = 0x6B76DC1F3AE6E6A3;
        constexpr uint64_t GET_ENTITY_HEADING                   = 0xE83D4F9BA2A38914;
        constexpr uint64_t IS_ENTITY_DEAD                       = 0x5F9532F3B5CC2551;
        constexpr uint64_t FREEZE_ENTITY_POSITION               = 0x428CA6DBD1094446;
        constexpr uint64_t SET_ENTITY_COLLISION                 = 0x1A9205C1B9EE827F;
        constexpr uint64_t SET_ENTITY_HAS_GRAVITY               = 0x4A4722448F18EEF5;
        constexpr uint64_t SET_ENTITY_VELOCITY                  = 0x1C99BB7B6E96D16F;
        constexpr uint64_t GET_ENTITY_VELOCITY                  = 0x4805D2E5118A2474;

        // Ped
        constexpr uint64_t IS_PED_SHOOTING                      = 0x34616828CD07F1A1;
        constexpr uint64_t GET_PED_BONE_COORDS                  = 0x17C07FC640E86B4E;
        constexpr uint64_t IS_PED_A_PLAYER                      = 0x12534C348C6CB68B;
        constexpr uint64_t GET_PED_ARMOUR                       = 0x9483AF821C9B9B4E;
        constexpr uint64_t HAS_ENTITY_CLEAR_LOS_TO_ENTITY       = 0xFCDFF7B72D23A1AC;
        constexpr uint64_t SET_PED_CAN_RAGDOLL                  = 0xB128377056A54E2A;

        // Camera
        constexpr uint64_t GET_GAMEPLAY_CAM_ROT                 = 0x837765A25378F0BB;
        constexpr uint64_t GET_GAMEPLAY_CAM_COORD               = 0x14D6F5678D8F1B37;
        constexpr uint64_t SET_GAMEPLAY_CAM_RELATIVE_HEADING    = 0xB4EC2312F4E5B1F1;
        constexpr uint64_t SET_GAMEPLAY_CAM_RELATIVE_PITCH      = 0x6D0858B8EDFD2B7D;
        constexpr uint64_t GET_GAMEPLAY_CAM_RELATIVE_HEADING    = 0x743607648ADD4587;
        constexpr uint64_t GET_GAMEPLAY_CAM_RELATIVE_PITCH      = 0x3A6867B4845BEDA2;
        constexpr uint64_t GET_GAMEPLAY_CAM_FOV                 = 0x65019750A0324133;

        // Blip / waypoint
        constexpr uint64_t GET_FIRST_BLIP_INFO_ID               = 0x1BEDE233E6CD2A1F;
        constexpr uint64_t GET_NEXT_BLIP_INFO_ID                = 0x14F96AA50D6FBEA7;
        constexpr uint64_t GET_BLIP_INFO_ID_COORD               = 0xFA7C7F0AADF25D09;
        constexpr uint64_t DOES_BLIP_EXIST                      = 0xA6DB27D19ECBB7DA;
        constexpr uint64_t GET_BLIP_INFO_ID_TYPE                = 0xBE9B0959FFD0779B;
        constexpr uint64_t GET_WAYPOINT_COORDS                  = 0x29B30D07C3F7873B;

        // Ground / physics
        constexpr uint64_t GET_GROUND_Z_FOR_3D_COORD            = 0xC906A7F04091EDA8;

        // Vehicle / model
        constexpr uint64_t GET_HASH_KEY                         = 0xD24D37CC275948CC;
        constexpr uint64_t REQUEST_MODEL                        = 0x963D27A58DF860AC;
        constexpr uint64_t HAS_MODEL_LOADED                     = 0x98A4EB5D89A0C952;
        constexpr uint64_t SET_MODEL_AS_NO_LONGER_NEEDED        = 0xE532F5D78798DAAB;
        constexpr uint64_t CREATE_VEHICLE                       = 0xAF35D0F2583551B0;
        constexpr uint64_t SET_PED_INTO_VEHICLE                 = 0xF75B0D629E1C063D;
        constexpr uint64_t SET_VEHICLE_ON_GROUND_PROPERLY       = 0x49733E92263139D1;

        // HUD / screen
        constexpr uint64_t GET_SCREEN_COORD_FROM_WORLD_COORD    = 0x34E82F05DF2974F5;

        // Weapon
        constexpr uint64_t GET_SELECTED_PED_WEAPON              = 0x0A6DB4965674D243;
        constexpr uint64_t SET_PED_INFINITE_AMMO_CLIP           = 0x183DADC6AA953186;

        // Input / control injection
        constexpr uint64_t IS_CONTROL_PRESSED                   = 0xF3A21BCD95725A4A;
        constexpr uint64_t SET_CONTROL_VALUE_NEXT_FRAME         = 0xE8A25867FBA3B05E;
        constexpr uint64_t GET_ENTITY_SPEED                     = 0xD5037BA82E12416F;

        // Vehicle
        constexpr uint64_t IS_PED_IN_ANY_VEHICLE                = 0x997ABD671D25CA0B;
        constexpr uint64_t GET_VEHICLE_PED_IS_IN                = 0x9A9112A0FE9A4713;

        // Network
        constexpr uint64_t NETWORK_IS_SESSION_STARTED           = 0x9DE624D2FC4B603F;
    }

    // ---- Bone IDs (SKEL_) -----------------------------------------------
    namespace Bone {
        constexpr int HEAD  = 31086;  // SKEL_Head
        constexpr int NECK  = 39317;  // SKEL_Neck_1
        constexpr int CHEST = 24818;  // SKEL_Spine3
        constexpr int SPINE = 24816;  // SKEL_Spine2
    }

    // ---- High-level wrappers (defined in natives.cpp) -------------------
    int          PlayerPedId();
    int          PlayerId();
    int          GetPlayerPed(int player);
    bool         IsPlayerPlaying(int player);
    bool         NetworkIsPlayerActive(int player);
    const char*  GetPlayerName(int player);
    Vec3         GetEntityCoords(int entity, bool alive = true);
    void         SetEntityCoordsNoOffset(int entity, float x, float y, float z);
    void         SetEntityInvincible(int entity, bool toggle);
    void         SetEntityVisible(int entity, bool toggle, bool p2 = false);
    int          GetEntityHealth(int entity);
    int          GetEntityMaxHealth(int entity);
    bool         IsEntityDead(int entity);
    bool         IsPedShooting(int ped);
    Vec3         GetPedBoneCoords(int ped, int boneId);
    int          GetPedArmour(int ped);
    bool         HasClearLOS(int e1, int e2, int traceType = 17);
    Vec3         GetGameplayCamRot(int order = 2);
    Vec3         GetGameplayCamCoord();
    float        GetGameplayCamRelativePitch();
    float        GetGameplayCamRelativeHeading();
    void         SetGameplayCamRelativeHeading(float h);
    void         SetGameplayCamRelativePitch(float p, float smoothSpeed = 1.0f);
    void         SetRunSprintMultiplier(int player, float mult);
    void         SetPedInfiniteAmmoClip(int ped, bool toggle);
    float        GetEntityHeading(int entity);
    float        GetEntitySpeed(int entity);
    uint32_t     GetSelectedPedWeapon(int ped);
    bool         IsControlPressed(int controlId);
    void         PressControl(int controlId, float value = 1.0f);
    bool         IsPedInAnyVehicle(int ped);
    int          GetVehiclePedIsIn(int ped);
    void         FreezeEntityPosition(int entity, bool toggle);
    void         SetEntityCollision(int entity, bool toggle, bool keepPhysics = true);
    void         SetEntityHasGravity(int entity, bool toggle);
    void         SetEntityVelocity(int entity, float x, float y, float z);
    bool         WorldToScreen(Vec3 world, Vec2& screen);

    // Waypoint
    Vec3         GetWaypointCoords();
    bool         IsWaypointActive();
    Vec3         GetWaypointCoordsDirect();   // GET_WAYPOINT_COORDS native

    // Ground
    bool         GetGroundZ(float x, float y, float z, float& outZ);

    // Vehicles
    uint32_t     GetHashKey(const char* str);
    void         RequestModel(uint32_t model);
    bool         HasModelLoaded(uint32_t model);
    void         SetModelNoLongerNeeded(uint32_t model);
    int          CreateVehicle(uint32_t model, float x, float y, float z, float heading);
    void         SetPedIntoVehicle(int ped, int vehicle, int seat);
    void         SetVehicleOnGroundProperly(int vehicle);

    // Generic by-name invocation — hash computed at runtime (joaat).
    // Returns false if the native doesn't exist on this build (safe to probe).
    uint32_t     JoaatHash(const char* name);
    bool         CallByName(const char* name, const uint64_t* args = nullptr,
                            int argCount = 0, uint64_t* ret = nullptr);
    bool         HasNative(const char* name);

    // Recovery
    bool         ReInitialize();   // re-resolve exports after path failure
}
