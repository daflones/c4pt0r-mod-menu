// ============================================================================
//  C4pt0r Cheats — Feature Implementations
//  ESP, Aimbot, Godmode, Invisibility, Speed, No Recoil, Teleport, Noclip
// ============================================================================

#include "features.h"
#include "globals.h"
#include "natives.h"
#include "imgui.h"

#include <cmath>
#include <algorithm>

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

    // ---- Noclip ---------------------------------------------------------
    static void TickNoclip()
    {
        int ped = Natives::PlayerPedId();
        if (!ped) return;

        if (g_Player.noclip) {
            Natives::FreezeEntityPosition(ped, true);
            Natives::SetEntityCollision(ped, false, false);
            Natives::SetEntityHasGravity(ped, false);

            // Get camera forward direction
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

            Vec3 pos = Natives::GetEntityCoords(ped);
            float speed = g_Player.noclipSpeed;

            // W/S
            if (GetAsyncKeyState('W') & 0x8000) {
                pos = pos + forward * speed;
            }
            if (GetAsyncKeyState('S') & 0x8000) {
                pos = pos - forward * speed;
            }
            // A/D
            if (GetAsyncKeyState('A') & 0x8000) {
                pos = pos - right * speed;
            }
            if (GetAsyncKeyState('D') & 0x8000) {
                pos = pos + right * speed;
            }
            // Space / Ctrl (up / down)
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
                pos.z += speed;
            }
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                pos.z -= speed;
            }

            Natives::SetEntityCoordsNoOffset(ped, pos.x, pos.y, pos.z);
        } else {
            // Restore physics when noclip is turned off
            static bool wasNoclip = false;
            if (wasNoclip) {
                Natives::FreezeEntityPosition(ped, false);
                Natives::SetEntityCollision(ped, true, true);
                Natives::SetEntityHasGravity(ped, true);
                wasNoclip = false;
            }
            if (g_Player.noclip) wasNoclip = true;
        }
    }

    // ---- Teleport -------------------------------------------------------
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

        // TP to waypoint
        if (g_Teleport.toWaypoint) {
            g_Teleport.toWaypoint = false;
            if (Natives::IsWaypointActive()) {
                Vec3 wp = Natives::GetWaypointCoords();
                // Waypoint Z is 0 on the map, so place player a bit above ground
                Natives::SetEntityCoordsNoOffset(ped, wp.x, wp.y, wp.z + 1.0f);
            }
        }

        // TP to saved
        if (g_Teleport.toSaved) {
            g_Teleport.toSaved = false;
            if (g_Teleport.hasSaved) {
                Natives::SetEntityCoordsNoOffset(ped,
                    g_Teleport.savedX, g_Teleport.savedY, g_Teleport.savedZ);
            }
        }
    }

    // ---- ESP data collection (runs in game thread, writes shared buffer) -
    static void TickESPData()
    {
        if (!g_ESP.enabled) return;

        int localPed = Natives::PlayerPedId();
        int localPlayer = Natives::PlayerId();
        if (!localPed) return;

        Vec3 localPos = Natives::GetEntityCoords(localPed);

        std::lock_guard<std::mutex> lock(g_DataMutex);

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            g_PlayersData[i].active = false;

            if (i == localPlayer) continue;
            if (!Natives::NetworkIsPlayerActive(i)) continue;

            int ped = Natives::GetPlayerPed(i);
            if (!ped || Natives::IsEntityDead(ped)) continue;

            Vec3 pos = Natives::GetEntityCoords(ped);
            float dist = localPos.Distance(pos);
            if (dist > g_ESP.maxDistance) continue;

            Vec2 screen;
            bool onScreen = Natives::WorldToScreen(pos, screen);

            auto& data       = g_PlayersData[i];
            data.active      = true;
            data.worldPos    = pos;
            data.screenPos   = screen;
            data.onScreen    = onScreen;
            data.distance    = dist;
            data.health      = Natives::GetEntityHealth(ped);
            data.maxHealth   = Natives::GetEntityMaxHealth(ped);
            data.armor       = Natives::GetPedArmour(ped);
            data.isDead      = false;

            const char* name = Natives::GetPlayerName(i);
            if (name) {
                strncpy_s(data.name, name, sizeof(data.name) - 1);
            } else {
                snprintf(data.name, sizeof(data.name), "Player %d", i);
            }
        }
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
            float smooth = std::max(g_Aimbot.smooth, 1.0f);
            float newHeading = currentHeading + (dYaw / smooth);
            float newPitch   = currentPitch   + (dPitch / smooth);

            Natives::SetGameplayCamRelativeHeading(newHeading);
            Natives::SetGameplayCamRelativePitch(newPitch, 1.0f);
        }
    }

    // ---- Main tick (called from cheat thread) ---------------------------
    void Tick()
    {
        if (!Natives::IsReady()) return;

        TickGodmode();
        TickInvisible();
        TickSpeed();
        TickNoRecoil();
        TickNoclip();
        TickTeleport();
        TickESPData();
        TickAimbot();
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

            // ---- Name ----
            if (g_ESP.names) {
                const char* name = p.name;
                ImVec2 textSize = ImGui::CalcTextSize(name);
                drawList->AddText(
                    ImVec2(sx - textSize.x * 0.5f, top - textSize.y - 4),
                    nameCol, name);
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
