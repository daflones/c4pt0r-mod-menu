// ============================================================================
//  C4pt0r Cheats — Feature Implementations
//  ESP, Aimbot, Godmode, Invisibility, Speed, No Recoil, Teleport, Noclip
// ============================================================================

#include "features.h"
#include "logger.h"

// Prevent Windows min/max macros from breaking std::max/std::clamp
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "globals.h"
#include "natives.h"
#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <chrono>

// Math helpers
constexpr float PI = 3.14159265358979f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float DEG2RAD = PI / 180.0f;

namespace C4pt0r::Features
{
    // ====================================================================
    //  Tick — called from main thread, safe to use natives
    // ====================================================================

    // ---- Godmode --------------------------------------------------------
    static void TickGodmode()
    {
        if (!g_Player.godmode) return;
        int ped = Natives::PlayerPedId();
        if (!ped) return;
        Natives::SetEntityInvincible(ped, true);
        // Also keep health topped up
        int maxHp = Natives::GetEntityMaxHealth(ped);
        if (Natives::GetEntityHealth(ped) < maxHp) {
            Natives::InvokeVoid(Natives::Hash::SET_ENTITY_HEALTH, ped, maxHp);
        }
    }

    // ---- Invisibility ---------------------------------------------------
    static void TickInvisible()
    {
        int ped = Natives::PlayerPedId();
        if (!ped) return;
        Natives::SetEntityVisible(ped, !g_Player.invisible, false);
    }

    // ---- Speed Hack -----------------------------------------------------
    static void TickSpeed()
    {
        if (!g_Player.speedHack) return;
        int player = Natives::PlayerId();
        Natives::SetRunSprintMultiplier(player, g_Player.speedMultiplier);
    }

    // ---- No Recoil (camera pitch restoration) ---------------------------
    static float s_SavedPitch = 0.0f;
    static bool  s_WasShooting = false;

    static void TickNoRecoil()
    {
        if (!g_Player.noRecoil) return;
        int ped = Natives::PlayerPedId();
        if (!ped) return;

        bool shooting = Natives::IsPedShooting(ped);
        if (shooting) {
            // Restore pitch to what it was before the shot
            Natives::SetGameplayCamRelativePitch(s_SavedPitch, 1.0f);
        } else {
            // Save current pitch when not shooting
            s_SavedPitch = Natives::GetGameplayCamRelativePitch();
        }
        s_WasShooting = shooting;
    }

    // ---- Infinite Ammo ---------------------------------------------------
    static void TickInfiniteAmmo()
    {
        if (!g_Player.infiniteAmmo) return;
        int ped = Natives::PlayerPedId();
        if (!ped) return;
        Natives::SetPedInfiniteAmmoClip(ped, true);
    }

    // ---- Noclip ---------------------------------------------------------
    static bool s_WasNoclip = false;

    static void TickNoclip()
    {
        int ped = Natives::PlayerPedId();
        if (!ped) return;

        if (g_Player.noclip) {
            s_WasNoclip = true;

            // Velocity-driven noclip: NO FreezeEntityPosition +
            // SetEntityCoordsNoOffset loop — teleporting a ped every tick
            // fights the physics engine and crashes the game process.
            // Collision off + gravity off + velocity = smooth fly-through.
            Natives::SetEntityCollision(ped, false, false);
            Natives::SetEntityHasGravity(ped, false);

            Vec3 camRot = Natives::GetGameplayCamRot();
            float pitch = camRot.x * DEG2RAD;
            float yaw   = camRot.z * DEG2RAD;

            Vec3 forward;
            forward.x = -sinf(yaw) * cosf(pitch);
            forward.y =  cosf(yaw) * cosf(pitch);
            forward.z =  sinf(pitch);

            Vec3 right;
            right.x = cosf(yaw);
            right.y = sinf(yaw);
            right.z = 0;

            float speed = g_Player.noclipSpeed;
            Vec3 vel;

            if (GetAsyncKeyState('W') & 0x8000)         vel = vel + forward * speed;
            if (GetAsyncKeyState('S') & 0x8000)         vel = vel - forward * speed;
            if (GetAsyncKeyState('A') & 0x8000)         vel = vel - right * speed;
            if (GetAsyncKeyState('D') & 0x8000)         vel = vel + right * speed;
            if (GetAsyncKeyState(VK_SPACE) & 0x8000)    vel.z += speed;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000)  vel.z -= speed;

            Natives::SetEntityVelocity(ped, vel.x, vel.y, vel.z);
        } else if (s_WasNoclip) {
            // Restore physics when noclip is turned off
            Natives::SetEntityCollision(ped, true, true);
            Natives::SetEntityHasGravity(ped, true);
            Natives::SetEntityVelocity(ped, 0.f, 0.f, 0.f);
            s_WasNoclip = false;
        }
    }

    // ---- Teleport -------------------------------------------------------
    // Pending waypoint state: TP requires a ground-Z probe which is async —
    // we retry a few ticks before giving up and using raw waypoint Z.
    static Vec3 s_PendingTp{};
    static bool s_HasPendingTp = false;
    static int  s_GroundTries  = 0;

    static void DoTeleport(int ped, float x, float y, float z) {
        // tp the vehicle too if we're inside one — otherwise we get ejected
        int veh = Natives::IsPedInAnyVehicle(ped) ? Natives::GetVehiclePedIsIn(ped) : 0;
        if (veh) Natives::SetEntityCoordsNoOffset(veh, x, y, z + 0.5f);
        else     Natives::SetEntityCoordsNoOffset(ped, x, y, z + 0.5f);
    }

    static void TickTeleport()
    {
        int ped = Natives::PlayerPedId();
        if (!ped) return;

        // Save current position when "hasSaved" first becomes true
        if (g_Teleport.hasSaved &&
            g_Teleport.savedX == 0 && g_Teleport.savedY == 0 && g_Teleport.savedZ == 0)
        {
            Vec3 pos = Natives::GetEntityCoords(ped);
            g_Teleport.savedX = pos.x;
            g_Teleport.savedY = pos.y;
            g_Teleport.savedZ = pos.z;
        }

        // --- pending waypoint TP: keep probing ground Z a few ticks ---
        if (s_HasPendingTp) {
            float gz = 0;
            if (Natives::GetGroundZ(s_PendingTp.x, s_PendingTp.y, s_PendingTp.z + 300.f, gz)) {
                DoTeleport(ped, s_PendingTp.x, s_PendingTp.y, gz);
                s_HasPendingTp = false;
            } else if (++s_GroundTries > 12) {
                // ground never resolved — land on the raw coords anyway
                DoTeleport(ped, s_PendingTp.x, s_PendingTp.y, s_PendingTp.z + 1.f);
                s_HasPendingTp = false;
                Log::Write("TP waypoint: ground Z nao resolveu, usando Z bruto.");
            }
        }

        // TP to waypoint — GET_WAYPOINT_COORDS direct first, blip walk fallback
        if (g_Teleport.toWaypoint) {
            g_Teleport.toWaypoint = false;
            Vec3 wp = Natives::GetWaypointCoordsDirect();
            if (wp.x == 0.f && wp.y == 0.f && Natives::IsWaypointActive())
                wp = Natives::GetWaypointCoords();
            if (wp.x != 0.f || wp.y != 0.f) {
                s_PendingTp   = wp;
                s_HasPendingTp = true;
                s_GroundTries = 0;
            } else {
                Log::Write("TP waypoint: nenhum waypoint marcado no mapa.");
            }
        }

        // TP to saved
        if (g_Teleport.toSaved) {
            g_Teleport.toSaved = false;
            if (g_Teleport.hasSaved) {
                DoTeleport(ped, g_Teleport.savedX, g_Teleport.savedY, g_Teleport.savedZ);
            }
        }

        // TP to explicit coords
        if (g_Teleport.toCoords) {
            g_Teleport.toCoords = false;
            float gz = 0;
            if (Natives::GetGroundZ(g_Teleport.tx, g_Teleport.ty, g_Teleport.tz + 300.f, gz))
                DoTeleport(ped, g_Teleport.tx, g_Teleport.ty, gz);
            else
                DoTeleport(ped, g_Teleport.tx, g_Teleport.ty, g_Teleport.tz);
        }

        // TP to player id
        if (g_Teleport.toPlayerId >= 0) {
            int target = g_Teleport.toPlayerId;
            g_Teleport.toPlayerId = -1;
            int tped = Natives::GetPlayerPed(target);
            if (tped) {
                Vec3 p = Natives::GetEntityCoords(tped);
                DoTeleport(ped, p.x + 1.5f, p.y, p.z);
            } else {
                Log::Write("TP player: id %d sem ped.", target);
            }
        }

        // Bring player to me (local-only effect on most servers)
        if (g_Teleport.bringPlayerId >= 0) {
            int target = g_Teleport.bringPlayerId;
            g_Teleport.bringPlayerId = -1;
            int tped = Natives::GetPlayerPed(target);
            if (tped) {
                Vec3 p = Natives::GetEntityCoords(ped);
                Natives::SetEntityCoordsNoOffset(tped, p.x + 1.5f, p.y, p.z);
            } else {
                Log::Write("Bring player: id %d sem ped.", target);
            }
        }
    }

    // ---- Vehicle spawn (request model -> wait loaded -> create) ----------
    enum class VStage { Idle, Loading };
    static VStage   s_VStage   = VStage::Idle;
    static uint32_t s_VModel   = 0;
    static int      s_VTries   = 0;

    static void TickVehicleSpawn()
    {
        if (g_Vehicle.spawn) {
            g_Vehicle.spawn = false;
            uint32_t m = Natives::GetHashKey(g_Vehicle.model);
            if (!m) {
                Log::Write("Veiculo: modelo '%s' invalido.", g_Vehicle.model);
                return;
            }
            s_VModel = m;
            s_VTries = 0;
            Natives::RequestModel(m);
            s_VStage = VStage::Loading;
            return;
        }

        if (s_VStage != VStage::Loading) return;

        if (++s_VTries > 120) {   // ~2s de espera
            s_VStage = VStage::Idle;
            Log::Write("Veiculo: modelo 0x%X nao carregou.", s_VModel);
            return;
        }
        if (!Natives::HasModelLoaded(s_VModel)) return;

        int ped = Natives::PlayerPedId();
        if (!ped) { s_VStage = VStage::Idle; return; }

        Vec3 pos    = Natives::GetEntityCoords(ped);
        float head  = Natives::GetEntityHeading(ped);
        float rad   = head * DEG2RAD;
        Vec3 spawnAt{ pos.x - sinf(rad) * 5.f, pos.y + cosf(rad) * 5.f, pos.z + 0.5f };

        int veh = Natives::CreateVehicle(s_VModel, spawnAt.x, spawnAt.y, spawnAt.z, head);
        if (veh) {
            Natives::SetVehicleOnGroundProperly(veh);
            if (g_Vehicle.warpIn)
                Natives::SetPedIntoVehicle(ped, veh, -1);
            Log::Write("Veiculo spawnado: %s (id %d)", g_Vehicle.model, veh);
        } else {
            Log::Write("Veiculo: CREATE_VEHICLE falhou (server pode bloquear).");
        }
        Natives::SetModelNoLongerNeeded(s_VModel);
        s_VStage = VStage::Idle;
    }

    // ---- RestoreAll: devolve o ped ao estado normal ----------------------
    //  Chamada no unload, quando o sistema de natives cai e pelo botao
    //  "Restaurar tudo" — evita ficar preso invisivel/sem colisao.
    void RestoreAll()
    {
        int ped = Natives::PlayerPedId();
        int player = Natives::PlayerId();
        if (ped) {
            __try {
                Natives::SetEntityVisible(ped, true, false);
                Natives::SetEntityCollision(ped, true, true);
                Natives::SetEntityHasGravity(ped, true);
                Natives::SetEntityVelocity(ped, 0.f, 0.f, 0.f);
                Natives::SetEntityInvincible(ped, false);
                Natives::FreezeEntityPosition(ped, false);
                Natives::SetPedInfiniteAmmoClip(ped, false);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (player) {
            __try { Natives::SetRunSprintMultiplier(player, 1.0f); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        // limpa flags que mantinham estado
        g_Player.godmode = g_Player.invisible = g_Player.noRecoil = false;
        g_Player.infiniteAmmo = g_Player.speedHack = g_Player.noclip = false;
        g_Farm.running = false;
        s_WasNoclip = false;
        Log::Write("RestoreAll: estado do ped restaurado.");
    }

    // ---- Player data collection (throttled — feeds ESP + player list) ----
    //  Always runs so the player list works even with ESP off. WorldToScreen
    //  is only paid for when ESP is actually enabled.
    static void TickPlayerData()
    {
        static auto last = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(150)) return;
        last = now;

        int localPed = Natives::PlayerPedId();
        int localPlayer = Natives::PlayerId();
        if (!localPed) return;

        Vec3 localPos = Natives::GetEntityCoords(localPed);
        bool espOn   = g_ESP.enabled;

        std::lock_guard<std::mutex> lock(g_DataMutex);
        int count = 0;

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            g_PlayersData[i].active = false;

            if (i == localPlayer) continue;
            if (!Natives::NetworkIsPlayerActive(i)) continue;

            int ped = Natives::GetPlayerPed(i);
            if (!ped) continue;
            bool dead = Natives::IsEntityDead(ped);

            Vec3 pos = Natives::GetEntityCoords(ped);
            float dist = localPos.Distance(pos);

            const char* name = Natives::GetPlayerName(i);
            char nameBuf[64];
            if (name) strncpy_s(nameBuf, name, sizeof(nameBuf) - 1);
            else      snprintf(nameBuf, sizeof(nameBuf), "Player %d", i);
            nameBuf[sizeof(nameBuf) - 1] = 0;

            int hp    = dead ? 0 : Natives::GetEntityHealth(ped);
            int maxHp = dead ? 100 : Natives::GetEntityMaxHealth(ped);
            int armor = dead ? 0 : Natives::GetPedArmour(ped);
            bool inVeh = Natives::IsPedInAnyVehicle(ped);

            // player list row (always collected)
            if (count < MAX_PLAYERS) {
                auto& e = g_PlayerList[count++];
                e.id = i; e.ped = ped; e.distance = dist;
                e.health = hp; e.armor = armor;
                e.inVehicle = inVeh; e.dead = dead;
                strncpy_s(e.name, nameBuf, sizeof(e.name) - 1);
            }

            // ESP data — only within range when ESP enabled
            if (!espOn || dead || dist > g_ESP.maxDistance) continue;

            Vec2 screen;
            bool onScreen = Natives::WorldToScreen(pos, screen);

            auto& data       = g_PlayersData[i];
            data.active      = true;
            data.playerId    = i;
            data.ped         = ped;
            data.worldPos    = pos;
            data.screenPos   = screen;
            data.onScreen    = onScreen;
            data.distance    = dist;
            data.health      = hp;
            data.maxHealth   = maxHp;
            data.armor       = armor;
            data.isDead      = false;
            data.inVehicle   = inVeh;
            strncpy_s(data.name, nameBuf, sizeof(data.name) - 1);
        }
        g_PlayerListCount = count;
    }

    // ---- Aimbot ---------------------------------------------------------
    static void TickAimbot()
    {
        if (!g_Aimbot.enabled) return;
        // Only active while right mouse button is held
        if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

        int localPed    = Natives::PlayerPedId();
        int localPlayer = Natives::PlayerId();
        if (!localPed) return;

        Vec3 camPos = Natives::GetGameplayCamCoord();
        Vec3 camRot = Natives::GetGameplayCamRot();

        float camYaw   = camRot.z;
        float camPitch = camRot.x;

        // Select bone
        int boneId = Natives::Bone::HEAD;
        switch (g_Aimbot.targetBone) {
            case 0: boneId = Natives::Bone::HEAD;  break;
            case 1: boneId = Natives::Bone::NECK;  break;
            case 2: boneId = Natives::Bone::CHEST; break;
        }

        float bestAngle = g_Aimbot.fov;
        Vec3  bestTarget;
        bool  found = false;

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (i == localPlayer) continue;
            if (!Natives::NetworkIsPlayerActive(i)) continue;

            int ped = Natives::GetPlayerPed(i);
            if (!ped || Natives::IsEntityDead(ped)) continue;

            // Visibility check
            if (g_Aimbot.visibleOnly) {
                if (!Natives::HasClearLOS(localPed, ped, 17))
                    continue;
            }

            Vec3 bonePos = Natives::GetPedBoneCoords(ped, boneId);
            Vec3 delta   = bonePos - camPos;
            float dist   = delta.Length();
            if (dist < 1.0f) continue;

            // Calculate angle to target
            float targetYaw   = atan2f(-delta.x, delta.y) * RAD2DEG;
            float groundDist  = sqrtf(delta.x * delta.x + delta.y * delta.y);
            float targetPitch = atan2f(delta.z, groundDist) * RAD2DEG;

            float dYaw   = targetYaw - camYaw;
            float dPitch = targetPitch - camPitch;

            // Normalize yaw delta to [-180, 180]
            while (dYaw > 180.0f)  dYaw -= 360.0f;
            while (dYaw < -180.0f) dYaw += 360.0f;

            float totalAngle = sqrtf(dYaw * dYaw + dPitch * dPitch);

            if (totalAngle < bestAngle) {
                bestAngle  = totalAngle;
                bestTarget = bonePos;
                found      = true;
            }
        }

        if (found) {
            Vec3 delta = bestTarget - camPos;
            float targetYaw   = atan2f(-delta.x, delta.y) * RAD2DEG;
            float groundDist  = sqrtf(delta.x * delta.x + delta.y * delta.y);
            float targetPitch = atan2f(delta.z, groundDist) * RAD2DEG;

            float currentHeading = Natives::GetGameplayCamRelativeHeading();
            float currentPitch   = Natives::GetGameplayCamRelativePitch();

            float dYaw   = targetYaw - camYaw;
            float dPitch = targetPitch - camPitch;

            while (dYaw > 180.0f)  dYaw -= 360.0f;
            while (dYaw < -180.0f) dYaw += 360.0f;

            // Smooth
            float smooth = (g_Aimbot.smooth > 1.0f) ? g_Aimbot.smooth : 1.0f;
            float newHeading = currentHeading + (dYaw / smooth);
            float newPitch   = currentPitch   + (dPitch / smooth);

            Natives::SetGameplayCamRelativeHeading(newHeading);
            Natives::SetGameplayCamRelativePitch(newPitch, 1.0f);
        }
    }

    // ---- Live inspector (writes g_Inspector for the Tools tab) ----------
    static void TickInspector()
    {
        if (!g_Farm.inspEnabled) return;

        int ped    = Natives::PlayerPedId();
        int player = Natives::PlayerId();

        g_Inspector.ped    = ped;
        g_Inspector.player = player;
        if (!ped) return;

        g_Inspector.pos     = Natives::GetEntityCoords(ped);
        g_Inspector.heading = Natives::GetEntityHeading(ped);
        g_Inspector.health  = Natives::GetEntityHealth(ped);
        g_Inspector.armor   = Natives::GetPedArmour(ped);
        g_Inspector.speed   = Natives::GetEntitySpeed(ped);
        g_Inspector.weapon  = Natives::GetSelectedPedWeapon(ped);
        g_Inspector.inVehicle = Natives::IsPedInAnyVehicle(ped);
        g_Inspector.vehicle   = g_Inspector.inVehicle
                                ? Natives::GetVehiclePedIsIn(ped) : 0;

        const char* nm = Natives::GetPlayerName(player);
        if (nm) strncpy_s(g_Inspector.name, nm, sizeof(g_Inspector.name) - 1);

        // Controls worth capturing for RP-job actions (E, pickup, F, G, R, M)
        static const int watch[] = { 51, 38, 75, 47, 45, 244, 23, 58 };
        int out = 0;
        for (int c : watch) {
            if (Natives::IsControlPressed(c))
                g_Inspector.pressed[out++] = c;
        }
        while (out < 8) g_Inspector.pressed[out++] = 0;
    }

    // ---- Farm macro (coleta -> vende -> repete) --------------------------
    //  Timeline per cycle:
    //    TP A -> settle -> press key x holdFrames -> interval ->
    //    TP B -> settle -> press key x holdFrames -> interval -> next cycle
    enum class FarmStage { Idle, ToA, ActA, WaitA, ToB, ActB, WaitB };
    static FarmStage s_Stage = FarmStage::Idle;
    static std::chrono::steady_clock::time_point s_StageSince;
    static int s_HoldsLeft = 0;

    static bool StageElapsed(int ms) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - s_StageSince).count() >= ms;
    }
    static void SetStage(FarmStage s) {
        s_Stage = s;
        s_StageSince = std::chrono::steady_clock::now();
    }

    static void TickFarm()
    {
        if (!g_Farm.running) {
            if (s_Stage != FarmStage::Idle) s_Stage = FarmStage::Idle;
            return;
        }

        if (!g_Farm.hasPointA || !g_Farm.hasPointB) {
            g_Farm.running = false;
            s_Stage = FarmStage::Idle;
            Log::Write("Farm: precisa gravar ponto A e B primeiro.");
            return;
        }

        int ped = Natives::PlayerPedId();
        if (!ped) return;

        switch (s_Stage) {
        case FarmStage::Idle:
            SetStage(FarmStage::ToA);
            break;

        case FarmStage::ToA:
            Natives::SetEntityCoordsNoOffset(ped, g_Farm.ax, g_Farm.ay, g_Farm.az + 0.5f);
            SetStage(FarmStage::ActA);
            s_HoldsLeft = g_Farm.holdFrames;
            break;
        case FarmStage::ActA:
            if (StageElapsed(g_Farm.settleMs)) {
                if (s_HoldsLeft > 0) {
                    Natives::PressControl(g_Farm.keyControl, 1.0f);
                    --s_HoldsLeft;
                } else {
                    SetStage(FarmStage::WaitA);
                }
            }
            break;
        case FarmStage::WaitA:
            if (StageElapsed(g_Farm.settleMs + g_Farm.intervalMs))
                SetStage(FarmStage::ToB);
            break;

        case FarmStage::ToB:
            Natives::SetEntityCoordsNoOffset(ped, g_Farm.bx, g_Farm.by, g_Farm.bz + 0.5f);
            SetStage(FarmStage::ActB);
            s_HoldsLeft = g_Farm.holdFrames;
            break;
        case FarmStage::ActB:
            if (StageElapsed(g_Farm.settleMs)) {
                if (s_HoldsLeft > 0) {
                    Natives::PressControl(g_Farm.keyControl, 1.0f);
                    --s_HoldsLeft;
                } else {
                    ++g_Farm.cyclesDone;
                    SetStage(FarmStage::WaitB);
                }
            }
            break;
        case FarmStage::WaitB:
            if (StageElapsed(g_Farm.settleMs + g_Farm.intervalMs)) {
                if (g_Farm.repeatTimes > 0 && g_Farm.cyclesDone >= g_Farm.repeatTimes) {
                    g_Farm.running = false;
                    s_Stage = FarmStage::Idle;
                    Log::Write("Farm: %d ciclos concluidos.", g_Farm.cyclesDone);
                } else {
                    SetStage(FarmStage::ToA);
                }
            }
            break;
        }
    }

    // ---- Main tick (called from cheat thread) ---------------------------
    void Tick()
    {
        if (!Natives::IsReady()) return;

        static bool s_FirstTick = true;
        if (s_FirstTick) {
            s_FirstTick = false;
            int ped  = Natives::PlayerPedId();
            int pid  = Natives::PlayerId();
            Vec3 pos = ped ? Natives::GetEntityCoords(ped) : Vec3{};
            Log::Write("FirstTick: ped=%d player=%d pos=(%.1f, %.1f, %.1f)",
                       ped, pid, pos.x, pos.y, pos.z);
        }

        TickGodmode();
        TickInvisible();
        TickSpeed();
        TickNoRecoil();
        TickInfiniteAmmo();
        TickNoclip();
        TickTeleport();
        TickVehicleSpawn();
        TickPlayerData();
        TickAimbot();
        TickInspector();
        TickFarm();
    }

    // ====================================================================
    //  RenderOverlay — called from Present hook, draws ESP with ImGui
    // ====================================================================
    void RenderOverlay()
    {
        if (!g_ESP.enabled) return;

        auto* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        float sw = static_cast<float>(g_ScreenWidth);
        float sh = static_cast<float>(g_ScreenHeight);

        ImU32 boxCol  = ImGui::ColorConvertFloat4ToU32(
            ImVec4(g_ESP.boxColor[0], g_ESP.boxColor[1], g_ESP.boxColor[2], g_ESP.boxColor[3]));
        ImU32 nameCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(g_ESP.nameColor[0], g_ESP.nameColor[1], g_ESP.nameColor[2], g_ESP.nameColor[3]));
        ImU32 hpGreen  = IM_COL32(0, 255, 80, 255);
        ImU32 hpRed    = IM_COL32(255, 40, 40, 255);
        ImU32 hpYellow = IM_COL32(255, 200, 0, 255);
        ImU32 snapCol  = IM_COL32(0, 212, 255, 100);

        std::lock_guard<std::mutex> lock(g_DataMutex);

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const auto& p = g_PlayersData[i];
            if (!p.active || !p.onScreen) continue;

            // Screen coords are 0..1, convert to pixels
            float sx = p.screenPos.x * sw;
            float sy = p.screenPos.y * sh;

            // Approximate box size based on distance
            float boxH = (sh / p.distance) * 1.8f;  // ~1.8m player height
            boxH = std::clamp(boxH, 20.0f, sh * 0.8f);
            float boxW = boxH * 0.4f;

            float left   = sx - boxW * 0.5f;
            float right  = sx + boxW * 0.5f;
            float top    = sy - boxH * 0.7f;
            float bottom = sy + boxH * 0.3f;

            // ---- Box ----
            if (g_ESP.boxes) {
                // Corner box style (looks cleaner than full rectangle)
                float cornerLen = boxW * 0.25f;
                auto drawCorner = [&](float cx, float cy, float dx, float dy) {
                    drawList->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * cornerLen, cy), boxCol, 2.0f);
                    drawList->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * cornerLen), boxCol, 2.0f);
                };
                drawCorner(left,  top,     1,  1);
                drawCorner(right, top,    -1,  1);
                drawCorner(left,  bottom,  1, -1);
                drawCorner(right, bottom, -1, -1);
            }

            // ---- Name ([id] nome) ----
            if (g_ESP.names) {
                char nameBuf[80];
                snprintf(nameBuf, sizeof(nameBuf), "[%d] %s", p.playerId, p.name);
                ImVec2 textSize = ImGui::CalcTextSize(nameBuf);
                drawList->AddText(
                    ImVec2(sx - textSize.x * 0.5f, top - textSize.y - 4),
                    nameCol, nameBuf);
            }

            // ---- Stats (hp + colete + veiculo) ----
            if (g_ESP.names) {
                char statBuf[48];
                if (p.inVehicle)
                    snprintf(statBuf, sizeof(statBuf), "%dHP %dA [car]", p.health, p.armor);
                else if (p.armor > 0)
                    snprintf(statBuf, sizeof(statBuf), "%dHP %dA", p.health, p.armor);
                else
                    snprintf(statBuf, sizeof(statBuf), "%dHP", p.health);
                ImVec2 st = ImGui::CalcTextSize(statBuf);
                drawList->AddText(
                    ImVec2(sx - st.x * 0.5f, bottom + (g_ESP.distance ? 18 : 4)),
                    IM_COL32(200, 210, 220, 220), statBuf);
            }

            // ---- Distance ----
            if (g_ESP.distance) {
                char distBuf[32];
                snprintf(distBuf, sizeof(distBuf), "%.0fm", p.distance);
                ImVec2 textSize = ImGui::CalcTextSize(distBuf);
                drawList->AddText(
                    ImVec2(sx - textSize.x * 0.5f, bottom + 4),
                    IM_COL32(180, 180, 180, 200), distBuf);
            }

            // ---- Health bar ----
            if (g_ESP.healthBars && p.maxHealth > 0) {
                float hpFrac = static_cast<float>(p.health) / static_cast<float>(p.maxHealth);
                hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);

                float barW = 3.0f;
                float barL = left - barW - 3;
                float barH = bottom - top;

                // Background
                drawList->AddRectFilled(
                    ImVec2(barL - 1, top - 1),
                    ImVec2(barL + barW + 1, bottom + 1),
                    IM_COL32(0, 0, 0, 160));

                // Health (bottom-up fill)
                ImU32 hpColor = hpFrac > 0.5f ? hpGreen : (hpFrac > 0.25f ? hpYellow : hpRed);
                float fillTop = bottom - barH * hpFrac;
                drawList->AddRectFilled(
                    ImVec2(barL, fillTop),
                    ImVec2(barL + barW, bottom),
                    hpColor);
            }

            // ---- Snaplines ----
            if (g_ESP.snaplines) {
                drawList->AddLine(
                    ImVec2(sw * 0.5f, sh),   // bottom center of screen
                    ImVec2(sx, bottom),
                    snapCol, 1.0f);
            }
        }
    }
}
