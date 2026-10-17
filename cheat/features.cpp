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
#include "memory.h"
#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdlib>

// Math helpers
constexpr float PI = 3.14159265358979f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float DEG2RAD = PI / 180.0f;

namespace C4pt0r::Features
{
    static uint64_t FA(float f);   // fwd — definida junto do CN() mais abaixo
    static bool CN(const char* n, const uint64_t* a = nullptr, int c = 0,
                   uint64_t* r = nullptr);

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

    // ---- Safe staged teleport --------------------------------------------
    //  Teleporting far away used to crash: the destination's collision wasn't
    //  streamed, GET_GROUND_Z failed, we landed on raw Z and fell into the
    //  void. Now:
    //    1) freeze ped/vehicle, move it 400m above the target, start loading
    //       the scene there (streams collision for the ground probe)
    //    2) poll GET_GROUND_Z each tick
    //    3) land on real ground (or raw Z after ~5s), unfreeze, stop scene load
    static bool  s_TpBusy   = false;
    static int   s_TpEnt    = 0;
    static Vec3  s_TpTarget{};
    static Vec3  s_TpStart{};
    static int   s_TpTries  = 0;
    static float s_TpLastZ  = 0.f;
    static int   s_TpStill  = 0;

    static void SceneLoad(float x, float y, float z) {
        uint64_t a[5];
        *reinterpret_cast<float*>(&a[0]) = x;
        *reinterpret_cast<float*>(&a[1]) = y;
        *reinterpret_cast<float*>(&a[2]) = z;
        *reinterpret_cast<float*>(&a[3]) = 350.f;
        a[4] = 0;
        Natives::CallByName("NEW_LOAD_SCENE_START_SPHERE", a, 5, nullptr);
    }
    static void SceneStop() {
        Natives::CallByName("NEW_LOAD_SCENE_STOP", nullptr, 0, nullptr);
    }

    static void BeginTeleport(int ped, float x, float y, float z) {
        if (s_TpBusy) return;
        int veh = Natives::IsPedInAnyVehicle(ped) ? Natives::GetVehiclePedIsIn(ped) : 0;
        s_TpEnt = veh ? veh : ped;
        s_TpStart = Natives::GetEntityCoords(s_TpEnt);   // ponto de retorno seguro
        Natives::FreezeEntityPosition(s_TpEnt, true);
        // park 400m above the destination — collision starts streaming below us
        Natives::SetEntityCoordsNoOffset(s_TpEnt, x, y, z + 400.f);
        SceneLoad(x, y, z);
        s_TpTarget = { x, y, z };
        s_TpTries  = 0;
        s_TpBusy   = true;
        Log::Write("TP: voando para %.1f, %.1f (carregando area...)", x, y);
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

        // --- staged TP in flight ---
        //  GET_GROUND_Z e os shape-tests nao existem nesse build — entao o
        //  pouso e por QUEDA LIVRE: stream a area, solta, e observa o Z
        //  estabilizar (= encostou em algo solido). void/timeout aborta
        //  pro ponto de partida.
        if (s_TpBusy) {
            ++s_TpTries;

            if (s_TpTries == 45) {          // ~700ms de stream primeiro
                Natives::FreezeEntityPosition(s_TpEnt, false);
                s_TpLastZ = 1e9f;
                s_TpStill = 0;
                Log::Write("TP: soltando — pousando no chao real.");
            }
            else if (s_TpTries > 45) {
                Vec3 p = Natives::GetEntityCoords(s_TpEnt);
                bool stopped = (p.z >= s_TpLastZ - 0.05f);   // parou de cair
                s_TpLastZ = p.z;
                if (stopped) ++s_TpStill; else s_TpStill = 0;

                if (s_TpStill >= 3) {       // ~3 ticks estavel = pousou
                    SceneStop();
                    s_TpBusy = false;
                    Log::Write("TP: pousou em %.1f, %.1f, %.1f",
                               p.x, p.y, p.z);
                } else if (p.z < -60.f || s_TpTries > 1200) {
                    // void/timeout — volta pro inicio, NUNCA pousa a ceu aberto
                    Natives::FreezeEntityPosition(s_TpEnt, true);
                    Natives::SetEntityCoordsNoOffset(s_TpEnt, s_TpStart.x,
                        s_TpStart.y, s_TpStart.z + 0.5f);
                    Natives::FreezeEntityPosition(s_TpEnt, false);
                    SceneStop();
                    s_TpBusy = false;
                    Log::Write("TP: falhou — voltou pro ponto de partida.");
                }
            }
        }

        // TP to waypoint — blip PRIMEIRO (e o marcador que o jogador
        // realmente pos no mapa); GET_WAYPOINT_COORDS direto so fallback
        if (g_Teleport.toWaypoint) {
            g_Teleport.toWaypoint = false;
            Vec3 wp = Natives::GetWaypointCoords();
            if (wp.x == 0.f && wp.y == 0.f)
                wp = Natives::GetWaypointCoordsDirect();
            if (wp.x != 0.f || wp.y != 0.f) {
                BeginTeleport(ped, wp.x, wp.y, wp.z);
            } else {
                Log::Write("TP waypoint: nenhum waypoint marcado no mapa.");
            }
        }

        // TP to saved
        if (g_Teleport.toSaved) {
            g_Teleport.toSaved = false;
            if (g_Teleport.hasSaved)
                BeginTeleport(ped, g_Teleport.savedX, g_Teleport.savedY, g_Teleport.savedZ);
        }

        // TP to explicit coords / presets
        if (g_Teleport.toCoords) {
            g_Teleport.toCoords = false;
            BeginTeleport(ped, g_Teleport.tx, g_Teleport.ty, g_Teleport.tz);
        }

        // TP to player id
        if (g_Teleport.toPlayerId >= 0) {
            int target = g_Teleport.toPlayerId;
            g_Teleport.toPlayerId = -1;
            int tped = Natives::GetPlayerPed(target);
            if (tped) {
                Vec3 p = Natives::GetEntityCoords(tped);
                BeginTeleport(ped, p.x + 1.5f, p.y, p.z);
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
            uint32_t m = g_Vehicle.modelHash
                         ? g_Vehicle.modelHash
                         : Natives::GetHashKey(g_Vehicle.model);
            g_Vehicle.modelHash = 0;
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
            // cor guardada na garagem — aplica logo depois do spawn
            if (g_Vehicle.postPrim >= 0 || g_Vehicle.postSec >= 0) {
                uint64_t a[3] = { (uint64_t)veh,
                    (uint64_t)(g_Vehicle.postPrim < 0 ? 0 : g_Vehicle.postPrim),
                    (uint64_t)(g_Vehicle.postSec  < 0 ? 0 : g_Vehicle.postSec) };
                CN("SET_VEHICLE_COLOURS", a, 3);
            }
            if (g_Vehicle.postCustom) {
                auto c255 = [](float v) { return (uint64_t)(v * 255.f); };
                uint64_t a[4] = { (uint64_t)veh, c255(g_Vehicle.postPrimRgb[0]),
                                  c255(g_Vehicle.postPrimRgb[1]),
                                  c255(g_Vehicle.postPrimRgb[2]) };
                CN("SET_VEHICLE_CUSTOM_PRIMARY_COLOUR", a, 4);
                a[1] = c255(g_Vehicle.postSecRgb[0]);
                a[2] = c255(g_Vehicle.postSecRgb[1]);
                a[3] = c255(g_Vehicle.postSecRgb[2]);
                CN("SET_VEHICLE_CUSTOM_SECONDARY_COLOUR", a, 4);
            }
            g_Vehicle.postPrim = g_Vehicle.postSec = -1;
            g_Vehicle.postCustom = false;
            if (g_Vehicle.warpIn)
                Natives::SetPedIntoVehicle(ped, veh, -1);
            Log::Write("Veiculo spawnado: %s (id %d)", g_Vehicle.model, veh);
        } else {
            Log::Write("Veiculo: CREATE_VEHICLE falhou (server pode bloquear).");
        }
        Natives::SetModelNoLongerNeeded(s_VModel);
        s_VStage = VStage::Idle;
    }

    // ---- Fun / world features ---------------------------------------------
    //  All natives invoked by name (joaat at runtime) — a native that doesn't
    //  exist on this build just returns false, nothing crashes.
    static bool CN(const char* n, const uint64_t* a, int c,
                   uint64_t* r) {
        return Natives::CallByName(n, a, c, r);
    }
    static uint64_t FA(float f) {          // float arg packed into u64 slot
        uint64_t u = 0;
        *reinterpret_cast<float*>(&u) = f;
        return u;
    }

    // ---- Ped/animal spawn (request model -> wait -> create) ---------------
    enum class PStage { Idle, Loading };
    static PStage   s_PStage  = PStage::Idle;
    static uint32_t s_PModel  = 0;
    static int      s_PTries  = 0;
    static int      s_PigLeft = 0;      // pigs left to drop from the sky
    static bool     s_PigMode = false;  // current spawn is a falling pig

    static void TickFun()
    {
        int ped    = Natives::PlayerPedId();
        int player = Natives::PlayerId();
        if (!ped) return;
        Vec3 pos = Natives::GetEntityCoords(ped);

        // ---------------- toggles ----------------
        if (g_Fun.superJump) {
            uint64_t a[] = { static_cast<uint64_t>(player) };
            CN("SET_SUPER_JUMP_THIS_FRAME", a, 1);
        }
        if (g_Fun.noWanted) {
            uint64_t a[] = { static_cast<uint64_t>(player) };
            CN("CLEAR_PLAYER_WANTED_LEVEL", a, 1);
        }
        if (g_Fun.thermal) {
            uint64_t a[] = { 1 };
            CN("SET_SEETHROUGH", a, 1);
        }
        if (g_Fun.nightvision) {
            uint64_t a[] = { 1 };
            CN("SET_NIGHTVISION", a, 1);
        }

        // edge-applied persistent toggles
        static bool s_Pol = false, s_Ev = false, s_Pause = false, s_Drunk = false;
        if (g_Fun.policeIgnore != s_Pol) {
            s_Pol = g_Fun.policeIgnore;
            uint64_t a[] = { static_cast<uint64_t>(player), s_Pol ? 1ull : 0ull };
            CN("SET_POLICE_IGNORE_PLAYER", a, 2);
        }
        if (g_Fun.everyoneIgnore != s_Ev) {
            s_Ev = g_Fun.everyoneIgnore;
            uint64_t a[] = { static_cast<uint64_t>(player), s_Ev ? 1ull : 0ull };
            CN("SET_EVERYONE_IGNORE_PLAYER", a, 2);
        }
        if (g_Fun.pauseTime != s_Pause) {
            s_Pause = g_Fun.pauseTime;
            uint64_t a[] = { s_Pause ? 1ull : 0ull };
            CN("PAUSE_CLOCK", a, 1);
        }
        if (g_Fun.drunk != s_Drunk) {
            s_Drunk = g_Fun.drunk;
            uint64_t a[] = { static_cast<uint64_t>(ped), s_Drunk ? 1ull : 0ull };
            CN("SET_PED_IS_DRUNK", a, 2);
        }
        if (g_Fun.drunk) {   // keep the cam wobbling while drunk
            static auto lastShake = std::chrono::steady_clock::time_point{};
            auto now = std::chrono::steady_clock::now();
            if (now - lastShake > std::chrono::milliseconds(600)) {
                lastShake = now;
                const char* shake = "DRUNK_SHAKE";
                uint64_t a[] = { reinterpret_cast<uint64_t>(shake), FA(1.2f) };
                CN("SHAKE_GAMEPLAY_CAM", a, 2);
            }
        }

        static float s_LastScale = -1.f;
        float want = g_Fun.slowmo ? g_Fun.timeScale : 1.f;
        if (want != s_LastScale) {
            uint64_t a[] = { FA(want) };
            if (CN("SET_TIME_SCALE", a, 1)) s_LastScale = want;
        }

        // ---------------- one-shot world ----------------
        if (g_Fun.setWeather) {
            g_Fun.setWeather = false;
            static const char* wnames[] = {
                "CLEAR","EXTRASUNNY","CLOUDS","OVERCAST","RAIN","THUNDER",
                "CLEARING","FOGGY","SMOG","XMAS","SNOWLIGHT","BLIZZARD"
            };
            const char* w = wnames[g_Fun.weather % 12];
            uint64_t a[] = { reinterpret_cast<uint64_t>(w) };
            if (CN("SET_OVERRIDE_WEATHER", a, 1))
                Log::Write("Clima: %s", w);
        }
        if (g_Fun.clearWeather) {
            g_Fun.clearWeather = false;
            CN("CLEAR_OVERRIDE_WEATHER");
        }
        if (g_Fun.setClock) {
            g_Fun.setClock = false;
            uint64_t a[] = { static_cast<uint64_t>(g_Fun.clockH),
                             static_cast<uint64_t>(g_Fun.clockM), 0 };
            CN("SET_CLOCK_TIME", a, 3);
        }
        if (g_Fun.setGravity) {
            g_Fun.setGravity = false;
            uint64_t a[] = { static_cast<uint64_t>(g_Fun.gravity) };
            CN("SET_GRAVITY_LEVEL", a, 1);
        }

        // ---------------- chaos ----------------
        if (g_Fun.explode) {
            g_Fun.explode = false;
            float rad = Natives::GetEntityHeading(ped) * DEG2RAD;
            uint64_t a[8];
            a[0] = FA(pos.x - sinf(rad) * 6.f);
            a[1] = FA(pos.y + cosf(rad) * 6.f);
            a[2] = FA(pos.z);
            a[3] = 0;               // grenade
            a[4] = FA(1.f);         // damage scale
            a[5] = 1;               // audible
            a[6] = 0;               // visible
            a[7] = FA(0.8f);        // cam shake
            CN("ADD_EXPLOSION", a, 8);
        }
        if (g_Fun.fire) {
            g_Fun.fire = false;
            float rad = Natives::GetEntityHeading(ped) * DEG2RAD;
            uint64_t a[5];
            a[0] = FA(pos.x - sinf(rad) * 3.f);
            a[1] = FA(pos.y + cosf(rad) * 3.f);
            a[2] = FA(pos.z);
            a[3] = 25;              // max children
            a[4] = 0;               // not a gas fire
            CN("START_SCRIPT_FIRE", a, 5);
        }
        if (g_Fun.water) {
            g_Fun.water = false;
            uint64_t a[4];
            a[0] = FA(pos.x); a[1] = FA(pos.y);
            a[2] = FA(pos.z + 3.f); a[3] = FA(150.f);
            if (CN("MODIFY_WATER", a, 4))
                Log::Write("Agua do mar spawnada (raio 150m, z+3).");
            else
                Log::Write("Agua: MODIFY_WATER indisponivel neste build.");
        }
        if (g_Fun.clone) {
            g_Fun.clone = false;
            uint64_t a[] = { static_cast<uint64_t>(ped) };
            uint64_t r[4] = {};
            CN("CLONE_PED", a, 1, r);
            Log::Write(r[0] ? "Clone criado." : "Clone falhou.");
        }
        if (g_Fun.pigRain) {
            g_Fun.pigRain = false;
            s_PigLeft = 12;
            Log::Write("Chuva de porcos chegando...");
        }
        if (s_PigLeft > 0 && s_PStage == PStage::Idle) {
            --s_PigLeft;
            s_PModel  = Natives::GetHashKey("a_c_pig");
            s_PigMode = true;
            s_PTries  = 0;
            Natives::RequestModel(s_PModel);
            s_PStage  = PStage::Loading;
        }

        // ---------------- ped / animal spawning ----------------
        if (g_Fun.spawnPed) {
            g_Fun.spawnPed = false;
            const char* mdl = g_Fun.pedModel[0] ? g_Fun.pedModel : "a_m_y_hipster_01";
            uint32_t m = Natives::GetHashKey(mdl);
            if (!m) {
                Log::Write("Ped: modelo '%s' invalido.", mdl);
            } else {
                s_PModel  = m;
                s_PigMode = false;
                s_PTries  = 0;
                Natives::RequestModel(m);
                s_PStage  = PStage::Loading;
            }
        }
        if (s_PStage == PStage::Loading) {
            if (++s_PTries > 120) {
                s_PStage = PStage::Idle;
                s_PigLeft = 0;
                Log::Write("Ped: modelo 0x%X nao carregou.", s_PModel);
            } else if (Natives::HasModelLoaded(s_PModel)) {
                Vec3 at;
                if (s_PigMode) {   // drop it from the sky
                    at = { pos.x + static_cast<float>(rand() % 13 - 6),
                           pos.y + static_cast<float>(rand() % 13 - 6),
                           pos.z + 18.f };
                } else {
                    float rad = Natives::GetEntityHeading(ped) * DEG2RAD;
                    at = { pos.x - sinf(rad) * 4.f,
                           pos.y + cosf(rad) * 4.f,
                           pos.z + 0.3f };
                }
                float head = Natives::GetEntityHeading(ped);
                uint64_t a[8];
                a[0] = 4;               // pedType civ
                a[1] = s_PModel;
                a[2] = FA(at.x); a[3] = FA(at.y); a[4] = FA(at.z);
                a[5] = FA(head);
                a[6] = 0;               // not networked
                a[7] = 0;
                uint64_t r[4] = {};
                if (CN("CREATE_PED", a, 8, r) && r[0]) {
                    int p = static_cast<int>(r[0]);
                    if (!s_PigMode) {
                        if (g_Fun.pedBodyguard) {
                            uint64_t ga[] = { static_cast<uint64_t>(player) };
                            uint64_t gr[4] = {};
                            if (CN("GET_PLAYER_GROUP", ga, 1, gr) && gr[0]) {
                                uint64_t ba[] = { static_cast<uint64_t>(p), gr[0] };
                                CN("SET_PED_AS_GROUP_MEMBER", ba, 2);
                                Log::Write("Guarda-costas no seu grupo.");
                            }
                        }
                        if (g_Fun.pedHostile) {
                            uint64_t ta[] = { static_cast<uint64_t>(p),
                                              static_cast<uint64_t>(ped), 0, 16 };
                            CN("TASK_COMBAT_PED", ta, 4);
                            Log::Write("Ped hostil contra voce!");
                        }
                    }
                    Log::Write("Ped spawnado (id %d).", p);
                } else {
                    Log::Write("Ped: CREATE_PED falhou (server pode bloquear).");
                    if (s_PigMode) s_PigLeft = 0;
                }
                Natives::SetModelNoLongerNeeded(s_PModel);
                s_PStage = PStage::Idle;
            }
        }

        // ---------------- vehicle fun ----------------
        int veh = Natives::IsPedInAnyVehicle(ped) ? Natives::GetVehiclePedIsIn(ped) : 0;
        if (g_Fun.repairVeh) {
            g_Fun.repairVeh = false;
            if (veh) {
                uint64_t a[] = { static_cast<uint64_t>(veh) };
                CN("SET_VEHICLE_FIXED", a, 1);
                uint64_t b[] = { static_cast<uint64_t>(veh), FA(0.f) };
                CN("SET_VEHICLE_DIRT_LEVEL", b, 2);
                Log::Write("Veiculo consertado.");
            } else Log::Write("Sem veiculo para consertar.");
        }
        if (g_Fun.flipVeh) {
            g_Fun.flipVeh = false;
            if (veh) Natives::SetVehicleOnGroundProperly(veh);
            else Log::Write("Sem veiculo para virar.");
        }
        if (g_Fun.boostVeh) {
            g_Fun.boostVeh = false;
            if (veh) {
                float sp = Natives::GetEntitySpeed(veh) + 35.f;
                uint64_t a[] = { static_cast<uint64_t>(veh), FA(sp) };
                CN("SET_VEHICLE_FORWARD_SPEED", a, 2);
                Log::Write("Turbo!");
            } else Log::Write("Entre num veiculo pro turbo.");
        }
    }

    // ---- RestoreEntity: devolve o ped ao normal SEM limpar os toggles ----
    //  Usada pelo watchdog/recovery: se os natives voltarem, as features
    //  que o usuario ligou re-aplicam sozinhas no proximo tick.
    void RestoreEntity()
    {
        int ped = Natives::PlayerPedId();
        int player = Natives::PlayerId();
        __try {
            if (ped) {
                Natives::SetEntityVisible(ped, true, false);
                Natives::SetEntityCollision(ped, true, true);
                Natives::SetEntityHasGravity(ped, true);
                Natives::SetEntityVelocity(ped, 0.f, 0.f, 0.f);
                Natives::SetEntityInvincible(ped, false);
                Natives::FreezeEntityPosition(ped, false);
            }
            if (player) Natives::SetRunSprintMultiplier(player, 1.0f);
            if (s_TpBusy && s_TpEnt) { Natives::FreezeEntityPosition(s_TpEnt, false); s_TpBusy = false; SceneStop(); }
            uint64_t one[] = { FA(1.f) };
            CN("SET_TIME_SCALE", one, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---- RestoreAll: devolve o ped ao estado normal ----------------------
    //  Chamada no unload e pelo botao "Restaurar tudo" — zera tudo.
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
        // aborta TP em voo (descongela a entidade que estava no ar)
        if (s_TpBusy && s_TpEnt) {
            __try { Natives::FreezeEntityPosition(s_TpEnt, false); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            s_TpBusy = false;
            SceneStop();
        }
        // mundo/diversao de volta ao normal
        __try {
            uint64_t zero[] = { 0 };
            uint64_t one[]  = { FA(1.f) };
            CN("SET_SEETHROUGH", zero, 1);
            CN("SET_NIGHTVISION", zero, 1);
            CN("SET_TIME_SCALE", one, 1);
            CN("PAUSE_CLOCK", zero, 1);
            CN("CLEAR_OVERRIDE_WEATHER");
            CN("SET_GRAVITY_LEVEL", zero, 1);
            if (ped)    { uint64_t a[] = { static_cast<uint64_t>(ped), 0 };
                          CN("SET_PED_IS_DRUNK", a, 2); }
            if (player) { uint64_t a[] = { static_cast<uint64_t>(player), 0 };
                          CN("SET_POLICE_IGNORE_PLAYER", a, 2);
                          CN("SET_EVERYONE_IGNORE_PLAYER", a, 2); }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        // limpa flags que mantinham estado
        g_Player.godmode = g_Player.invisible = g_Player.noRecoil = false;
        g_Player.infiniteAmmo = g_Player.speedHack = g_Player.noclip = false;
        g_Farm.running = false;
        g_Fun = FunConfig{};
        s_WasNoclip = false;
        s_PStage = PStage::Idle;
        s_PigLeft = 0;
        Log::Write("RestoreAll: estado do ped restaurado.");
    }

    // ---- Mecanico (tunagem em qualquer lugar) ------------------------------
    static int MechTargetVeh(int ped)
    {
        int veh = 0;
        if (Natives::IsPedInAnyVehicle(ped))
            veh = Natives::GetVehiclePedIsIn(ped);
        if (!veh && g_Mech.useNearest) {
            Vec3 p = Natives::GetEntityCoords(ped);
            uint64_t a[6] = { FA(p.x), FA(p.y), FA(p.z), FA(25.f), 0, 70 };
            uint64_t r[4] = {};
            if (CN("GET_CLOSEST_VEHICLE", a, 6, r)) veh = (int)r[0];
        }
        return veh;
    }

    //  Mexer em veiculo REMOTO (de outro player) crashava o cliente —
    //  so aplica quando a gente tem controle da entidade. Se nao tem,
    //  pede e tenta de novo no proximo tick.
    static int s_MechCtrlTries = 0;
    static bool MechHasControl(int veh)
    {
        uint64_t a[1] = { (uint64_t)veh };
        uint64_t r[4] = {};
        if (CN("DOES_ENTITY_EXIST", a, 1, r) && !r[0]) return false;
        memset(r, 0, sizeof(r));
        if (CN("NETWORK_HAS_CONTROL_OF_ENTITY", a, 1, r) && r[0]) return true;
        CN("NETWORK_REQUEST_CONTROL_OF_ENTITY", a, 1, nullptr);
        return false;
    }

    static void MechSaveToGarage(int veh)
    {
        GarageEntry e{};
        uint64_t a[1] = { (uint64_t)veh };
        uint64_t r[4] = {};
        if (CN("GET_ENTITY_MODEL", a, 1, r)) e.model = (uint32_t)r[0];
        int pr = -1, se = -1;
        uint64_t ca[3] = { (uint64_t)veh, (uint64_t)&pr, (uint64_t)&se };
        if (CN("GET_VEHICLE_COLOURS", ca, 3)) { e.prim = pr; e.sec = se; }
        int cr = -1, cg = -1, cb = -1;
        uint64_t cc[4] = { (uint64_t)veh, (uint64_t)&cr,
                           (uint64_t)&cg, (uint64_t)&cb };
        if (CN("GET_VEHICLE_CUSTOM_PRIMARY_COLOUR", cc, 4) && cr >= 0) {
            e.custom = true;
            e.primRgb[0] = cr / 255.f; e.primRgb[1] = cg / 255.f;
            e.primRgb[2] = cb / 255.f;
            cr = cg = cb = -1;
            uint64_t cd[4] = { (uint64_t)veh, (uint64_t)&cr,
                               (uint64_t)&cg, (uint64_t)&cb };
            CN("GET_VEHICLE_CUSTOM_SECONDARY_COLOUR", cd, 4);
            e.secRgb[0] = (cr >= 0 ? cr : 0) / 255.f;
            e.secRgb[1] = (cg >= 0 ? cg : 0) / 255.f;
            e.secRgb[2] = (cb >= 0 ? cb : 0) / 255.f;
        }
        snprintf(e.name, sizeof(e.name), "Veiculo 0x%08X", e.model);
        {
            std::lock_guard<std::mutex> lk(g_GarageMtx);
            g_Garage.push_back(e);
        }
        Log::Write("Garagem: %s guardado.", e.name);
    }

    static void MechApplyMods(int veh)
    {
        uint64_t kit[2] = { (uint64_t)veh, 0 };
        CN("SET_VEHICLE_MOD_KIT", kit, 2);          // obrigatorio antes de mods
        if (g_Mech.wheelType >= 0) {                // wheel type vem antes das rodas
            uint64_t a[2] = { (uint64_t)veh, (uint64_t)g_Mech.wheelType };
            CN("SET_VEHICLE_WHEEL_TYPE", a, 2);
        }
        static const int kSlots[] = { 11, 12, 13, 15, 16, 14, 23, 24 };
        const int vals[] = { g_Mech.engine, g_Mech.brakes, g_Mech.trans,
                             g_Mech.susp,   g_Mech.armor,  g_Mech.horn,
                             g_Mech.wheels, g_Mech.wheels };
        for (int i = 0; i < 8; ++i) {
            if (vals[i] < 0) continue;
            uint64_t a[4] = { (uint64_t)veh, (uint64_t)kSlots[i], (uint64_t)vals[i], 0 };
            CN("SET_VEHICLE_MOD", a, 4);
        }
        if (g_Mech.livery >= 0) {
            uint64_t a[4] = { (uint64_t)veh, 48, (uint64_t)g_Mech.livery, 0 };
            CN("SET_VEHICLE_MOD", a, 4);
            uint64_t b[3] = { (uint64_t)veh, (uint64_t)g_Mech.livery, 0 };
            CN("SET_VEHICLE_LIVERY", b, 3);         // builds antigos
        }
        { uint64_t a[3] = { (uint64_t)veh, 18, g_Mech.turbo ? 1ull : 0ull };
          CN("TOGGLE_VEHICLE_MOD", a, 3); }
        { uint64_t a[3] = { (uint64_t)veh, 22, g_Mech.xenon ? 1ull : 0ull };
          CN("TOGGLE_VEHICLE_MOD", a, 3); }
        { uint64_t a[2] = { (uint64_t)veh, g_Mech.bpTires ? 0ull : 1ull };
          CN("SET_VEHICLE_TYRES_CAN_BURST", a, 2); }
    }

    static void MechApplyColors(int veh)
    {
        if (g_Mech.useCustomCol) {
            auto c255 = [](float v) { return (uint64_t)(v * 255.f); };
            uint64_t a[4] = { (uint64_t)veh, c255(g_Mech.primRgb[0]),
                              c255(g_Mech.primRgb[1]), c255(g_Mech.primRgb[2]) };
            CN("SET_VEHICLE_CUSTOM_PRIMARY_COLOUR", a, 4);
            a[1] = c255(g_Mech.secRgb[0]); a[2] = c255(g_Mech.secRgb[1]);
            a[3] = c255(g_Mech.secRgb[2]);
            CN("SET_VEHICLE_CUSTOM_SECONDARY_COLOUR", a, 4);
        }
        else if (g_Mech.colPrim >= 0 || g_Mech.colSec >= 0) {
            uint64_t a[3] = { (uint64_t)veh,
                (uint64_t)(g_Mech.colPrim < 0 ? 0 : g_Mech.colPrim),
                (uint64_t)(g_Mech.colSec  < 0 ? 0 : g_Mech.colSec) };
            CN("SET_VEHICLE_COLOURS", a, 3);
        }
        if (g_Mech.pearl >= 0 || g_Mech.wheelCol >= 0) {
            uint64_t a[3] = { (uint64_t)veh,
                (uint64_t)(g_Mech.pearl    < 0 ? 0 : g_Mech.pearl),
                (uint64_t)(g_Mech.wheelCol < 0 ? 0 : g_Mech.wheelCol) };
            CN("SET_VEHICLE_EXTRA_COLOURS", a, 3);
        }
    }

    static void MechApplyExtras(int veh)
    {
        auto c255 = [](float v) { return (uint64_t)(v * 255.f); };
        for (int i = 0; i < 4; ++i) {
            uint64_t a[3] = { (uint64_t)veh, (uint64_t)i,
                              g_Mech.neonOn ? 1ull : 0ull };
            CN("SET_VEHICLE_NEON_ENABLED", a, 3);
        }
        { uint64_t a[4] = { (uint64_t)veh, c255(g_Mech.neonRgb[0]),
                            c255(g_Mech.neonRgb[1]), c255(g_Mech.neonRgb[2]) };
          CN("SET_VEHICLE_NEON_COLOUR", a, 4); }
        { uint64_t a[4] = { (uint64_t)veh, c255(g_Mech.smokeRgb[0]),
                            c255(g_Mech.smokeRgb[1]), c255(g_Mech.smokeRgb[2]) };
          CN("SET_VEHICLE_TYRE_SMOKE_COLOR", a, 4); }
        if (g_Mech.winTint >= 0) {
            uint64_t a[2] = { (uint64_t)veh, (uint64_t)g_Mech.winTint };
            CN("SET_VEHICLE_WINDOW_TINT", a, 2);
        }
    }

    static void TickMechanic()
    {
        bool any = g_Mech.applyMods | g_Mech.applyColors | g_Mech.applyExtras |
                   g_Mech.applyPower | g_Mech.plate | g_Mech.saveGarage;
        if (!any) return;
        int ped = Natives::PlayerPedId();
        int veh = ped ? MechTargetVeh(ped) : 0;
        if (veh && !MechHasControl(veh)) {
            if (++s_MechCtrlTries < 60) return;   // ~1s pedindo controle
            s_MechCtrlTries = 0;
            veh = 0;
            Log::Write("Mecanico: sem controle do veiculo (de outro player?) — cancelado.");
        } else {
            s_MechCtrlTries = 0;
        }
        if (veh) {
            if (g_Mech.saveGarage) MechSaveToGarage(veh);
            if (g_Mech.applyMods)   MechApplyMods(veh);
            if (g_Mech.applyColors) MechApplyColors(veh);
            if (g_Mech.applyExtras) MechApplyExtras(veh);
            if (g_Mech.applyPower) {
                if (g_Mech.engineMult > 0.f) {
                    uint64_t a[2] = { (uint64_t)veh, FA(g_Mech.engineMult) };
                    CN("SET_VEHICLE_ENGINE_POWER_MULTIPLIER", a, 2);
                }
                if (g_Mech.torqueMult > 0.f) {
                    uint64_t a[2] = { (uint64_t)veh, FA(g_Mech.torqueMult) };
                    CN("SET_VEHICLE_ENGINE_TORQUE_MULTIPLIER", a, 2);
                }
            }
            if (g_Mech.plate && g_Mech.plateText[0]) {
                uint64_t a[2] = { (uint64_t)veh, (uint64_t)g_Mech.plateText };
                CN("SET_VEHICLE_NUMBER_PLATE_TEXT", a, 2);
            }
        }
        g_Mech.applyMods = g_Mech.applyColors = g_Mech.applyExtras = false;
        g_Mech.applyPower = g_Mech.plate = g_Mech.saveGarage = false;
    }

    // ---- Armas -------------------------------------------------------------
    static const char* kWeaponList[] = {
        "weapon_pistol","weapon_combatpistol","weapon_appistol","weapon_pistol50",
        "weapon_snspistol","weapon_heavypistol","weapon_vintagepistol",
        "weapon_microsmg","weapon_smg","weapon_assaultsmg","weapon_machinepistol",
        "weapon_minismg","weapon_combatpdw","weapon_mumpshotgun",
        "weapon_assaultrifle","weapon_carbinerifle","weapon_advancedrifle",
        "weapon_specialcarbine","weapon_bullpuprifle","weapon_compactrifle",
        "weapon_mg","weapon_combatmg","weapon_gusenberg",
        "weapon_pumpshotgun","weapon_sawnoffshotgun","weapon_assaultshotgun",
        "weapon_bullpupshotgun","weapon_musket","weapon_heavyshotgun",
        "weapon_dbshotgun","weapon_autoshotgun",
        "weapon_sniperrifle","weapon_heavysniper","weapon_marksmanrifle",
        "weapon_grenadelauncher","weapon_rpg","weapon_hominglauncher",
        "weapon_minigun","weapon_railgun","weapon_firework",
        "weapon_grenade","weapon_stickybomb","weapon_molotov","weapon_proxmine",
        "weapon_knife","weapon_bat","weapon_crowbar","weapon_nightstick",
        "weapon_machete","weapon_battleaxe","weapon_switchblade",
        "weapon_petrolcan","weapon_parachute","weapon_flare","weapon_flashlight",
        "weapon_stungun","weapon_fireextinguisher"
    };

    static const char* kWeaponComps[] = {
        "component_at_scope_small","component_at_scope_medium",
        "component_at_scope_large","component_at_scope_macro",
        "component_at_scope_max","component_at_scope_nightvision",
        "component_at_ar_supp","component_at_ar_supp_02",
        "component_at_pi_supp","component_at_pi_supp_02",
        "component_at_sr_supp","component_at_sr_supp_03",
        "component_at_ar_afgrip","component_at_ar_afgrip_02",
        "component_at_pi_flsh","component_at_ar_flsh",
        "component_extended_clip","component_luxury_finish"
    };

    static void GiveOneWeapon(int ped, const char* model, int ammo)
    {
        uint64_t a[5] = { (uint64_t)ped, (uint64_t)Natives::JoaatHash(model),
                          (uint64_t)ammo, 0, 1 };
        if (CN("GIVE_WEAPON_TO_PED", a, 5))
            Log::Write("arma: %s (muni %d)", model, ammo);
    }

    static void TickWeapons()
    {
        if (!(g_Weapon.give | g_Weapon.giveAll | g_Weapon.stripAll |
              g_Weapon.refillAll)) return;
        int ped = Natives::PlayerPedId();
        if (ped) {
            if (g_Weapon.stripAll) {
                uint64_t a[2] = { (uint64_t)ped, 1 };
                CN("REMOVE_ALL_PED_WEAPONS", a, 2);
            }
            if (g_Weapon.give && g_Weapon.model[0]) {
                GiveOneWeapon(ped, g_Weapon.model, g_Weapon.ammo);
                if (g_Weapon.attachments) {
                    uint32_t wh = Natives::JoaatHash(g_Weapon.model);
                    for (const char* comp : kWeaponComps) {
                        uint64_t a[3] = { (uint64_t)ped, wh,
                                          (uint64_t)Natives::JoaatHash(comp) };
                        CN("GIVE_WEAPON_COMPONENT_TO_PED", a, 3);
                    }
                }
            }
            if (g_Weapon.giveAll) {
                for (const char* w : kWeaponList) GiveOneWeapon(ped, w, 9999);
            }
            if (g_Weapon.refillAll) {
                for (const char* w : kWeaponList) {
                    uint64_t a[3] = { (uint64_t)ped,
                        (uint64_t)Natives::JoaatHash(w), 9999 };
                    CN("SET_PED_AMMO", a, 3);
                }
            }
        }
        g_Weapon.give = g_Weapon.giveAll = false;
        g_Weapon.stripAll = g_Weapon.refillAll = false;
    }

    // ---- Player data collection (throttled — feeds ESP + player list) ----
    //  Always runs so the player list works even with ESP off. WorldToScreen
    //  is only paid for when ESP is actually enabled.
    //
    //  CRITICO: natives NUNCA rodam segurando g_DataMutex — se uma chamada
    //  trava no meio, o mutex ficava preso pra sempre e TODA thread nova
    //  morria no lock_guard (o ciclo infinito de respawns do log). Coleta
    //  em arrays locais; o lock so serve pra copiar o resultado.
    static void TickPlayerData()
    {
        static auto last = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(250)) return;
        last = now;

        int localPed = Natives::PlayerPedId();
        int localPlayer = Natives::PlayerId();
        if (!localPed) return;

        Vec3 localPos = Natives::GetEntityCoords(localPed);
        bool espOn   = g_ESP.enabled;

        // coleta local (sem lock)
        static PlayerESPData   tmpData[MAX_PLAYERS];
        static PlayerListEntry tmpList[MAX_PLAYERS];
        int count = 0;

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            tmpData[i].active = false;

            if (i == localPlayer) continue;
            if (!Natives::NetworkIsPlayerActive(i)) continue;

            int ped = Natives::GetPlayerPed(i);
            if (!ped) continue;
            bool dead = Natives::IsEntityDead(ped);

            Vec3 pos = Natives::GetEntityCoords(ped);
            float dist = localPos.Distance(pos);

            const char* name = Natives::GetPlayerName(i);
            char nameBuf[64] = {};
            bool gotName = false;
            if (name && Memory::IsReadable(reinterpret_cast<uintptr_t>(name))) {
                // a string pode cruzar pra uma pagina guard no meio do copy
                __try {
                    strncpy_s(nameBuf, name, sizeof(nameBuf) - 1);
                    gotName = true;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (!gotName) snprintf(nameBuf, sizeof(nameBuf), "Player %d", i);
            nameBuf[sizeof(nameBuf) - 1] = 0;

            int hp    = dead ? 0 : Natives::GetEntityHealth(ped);
            int maxHp = dead ? 100 : Natives::GetEntityMaxHealth(ped);
            int armor = dead ? 0 : Natives::GetPedArmour(ped);
            bool inVeh = Natives::IsPedInAnyVehicle(ped);

            // godmode detection — provavel admin/dono do server (so quando
            // ESP ligado pra nao gastar natives)
            bool god = false;
            if (espOn) {
                uint64_t ga[] = { static_cast<uint64_t>(i) };
                uint64_t gr[4] = {};
                if (CN("GET_PLAYER_INVINCIBLE", ga, 1, gr)) god = gr[0] != 0;
            }

            // player list row (always collected)
            if (count < MAX_PLAYERS) {
                auto& e = tmpList[count++];
                e.id = i; e.ped = ped; e.distance = dist;
                e.health = hp; e.armor = armor;
                e.inVehicle = inVeh; e.dead = dead;
                strncpy_s(e.name, nameBuf, sizeof(e.name) - 1);
            }

            // ESP data — only within range when ESP enabled
            if (!espOn || dead || dist > g_ESP.maxDistance) continue;
            // filtro admin(god)/normal
            if (g_ESP.filter == 1 && god)  continue;
            if (g_ESP.filter == 2 && !god) continue;

            Vec2 screen;
            bool onScreen = Natives::WorldToScreen(pos, screen);

            auto& data       = tmpData[i];
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
            data.god         = god;
            strncpy_s(data.name, nameBuf, sizeof(data.name) - 1);
        }

        // publica o resultado — lock curto; se um zumbi segurar o mutex,
        // try_lock_for sai em 50ms em vez de matar a thread
        if (g_DataMutex.try_lock_for(std::chrono::milliseconds(50))) {
            memcpy(g_PlayersData, tmpData, sizeof(tmpData));
            memcpy(g_PlayerList,  tmpList, sizeof(tmpList));
            g_PlayerListCount = count;
            g_DataMutex.unlock();
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

        // g_TickStage marca o sub-tick em execucao — se a thread travar, o
        // supervisor loga exatamente onde (stage no HealthString/log)
        Natives::g_TickStage = 1;  TickGodmode();
        Natives::g_TickStage = 2;  TickInvisible();
        Natives::g_TickStage = 3;  TickSpeed();
        Natives::g_TickStage = 4;  TickNoRecoil();
        Natives::g_TickStage = 5;  TickInfiniteAmmo();
        Natives::g_TickStage = 6;  TickNoclip();
        Natives::g_TickStage = 7;  TickTeleport();
        Natives::g_TickStage = 8;  TickVehicleSpawn();
        Natives::g_TickStage = 9;  TickFun();
        Natives::g_TickStage = 10; TickPlayerData();
        Natives::g_TickStage = 11; TickAimbot();
        Natives::g_TickStage = 12; TickInspector();
        Natives::g_TickStage = 13; TickFarm();
        Natives::g_TickStage = 14; TickMechanic();
        Natives::g_TickStage = 15; TickWeapons();
        Natives::g_TickStage = 0;
    }

    // ====================================================================
    //  FrameTick — o driver do cheat, chamado pelo hook do Present.
    //
    //  O FiveM/adhesive SUSPENDE threads estrangeiras — a feature thread
    //  congelava no meio de um sleep (stage 94 com beat velho) e nada
    //  revivia, porque nem o supervisor podia rodar. A thread de render
    //  do jogo NUNCA e suspensa (senao o jogo congela junto) — entao o
    //  tick roda nela. A feature thread continua existindo como backup:
    //  se o Present parar, ela assume (o gate atomico garante um driver
    //  so). Contem o watchdog de natives + recovery tambem.
    // ====================================================================
    void FrameTick()
    {
        static std::atomic<int64_t> s_LastTick{0};
        static std::atomic<bool>    s_InTick{false};
        static bool nativesOk = true;
        static int  consecutiveErrors = 0;
        static int  recoveryWait = 0;
        static int  watchTicks = 0;
        static int  deadPings  = 0;

        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t prev = s_LastTick.load();
        if (now - prev < 30) return;   // ~33Hz — cheat nao precisa 80fps
        if (!s_LastTick.compare_exchange_strong(prev, now)) return;
        if (s_InTick.exchange(true)) return;      // outro driver no tick

        Natives::g_TickStage = 91;
        Natives::g_TickBeat  = now;

        if (nativesOk) {
            Natives::g_TickStage = 92;
            __try {
                Tick();
                consecutiveErrors = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                ++consecutiveErrors;
                Log::Write("EXCEPTION in Features::Tick(), code 0x%08X (#%d)",
                           GetExceptionCode(), consecutiveErrors);
                if (consecutiveErrors >= 25) {
                    nativesOk = false;
                    recoveryWait = 0;
                    consecutiveErrors = 0;
                    Natives::MarkUnhealthy();
                    Log::Write("Features pausadas por excecoes — restaurando ped e agendando re-init.");
                    Natives::g_TickStage = 97;
                    __try { RestoreEntity(); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // watchdog: natives morrem SEM excecao tambem — pinga ~2s;
            // 2 falhas = re-init (o proprio driver resolve agora)
            Natives::g_TickStage = 93;
            if (++watchTicks >= 120) {
                watchTicks = 0;
                bool alive = false;
                __try { alive = Natives::Ping(); }
                __except (EXCEPTION_EXECUTE_HANDLER) { alive = false; }
                if (!alive) {
                    if (++deadPings >= 2) {
                        deadPings = 0;
                        nativesOk = false;
                        recoveryWait = 0;
                        Natives::MarkUnhealthy();
                        Log::Write("Watchdog: natives pararam de responder — re-init agendado.");
                        Natives::g_TickStage = 97;
                        __try { RestoreEntity(); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else {
                    deadPings = 0;
                }
            }
        } else {
            Natives::g_TickStage = 95;
            if (++recoveryWait >= 300) {   // ~5s
                recoveryWait = 0;
                Log::Write("Tentando re-inicializar natives...");
                Natives::g_TickStage = 96;
                __try {
                    if (Natives::ReInitialize()) {
                        nativesOk = true;
                        Log::Write("Natives recuperados — features reativadas.");
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log::Write("Re-init threw (0x%08X), tentando de novo depois.",
                               GetExceptionCode());
                }
            }
        }

        s_InTick = false;
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

        // try_lock: se o mutex estiver preso por um zumbi, pulamos esse
        // frame em vez de travar a thread de render pra sempre
        if (!g_DataMutex.try_lock_for(std::chrono::milliseconds(10))) return;

        ImU32 godCol = ImGui::ColorConvertFloat4ToU32(
            ImVec4(g_ESP.godColor[0], g_ESP.godColor[1], g_ESP.godColor[2], g_ESP.godColor[3]));

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const auto& p = g_PlayersData[i];
            if (!p.active || !p.onScreen) continue;

            // admins/godmode ganham caixa dourada
            const ImU32 curBox = p.god ? godCol : boxCol;

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
                    drawList->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * cornerLen, cy), curBox, 2.0f);
                    drawList->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * cornerLen), curBox, 2.0f);
                };
                drawCorner(left,  top,     1,  1);
                drawCorner(right, top,    -1,  1);
                drawCorner(left,  bottom,  1, -1);
                drawCorner(right, bottom, -1, -1);
            }

            // ---- Name ([id] nome, [GOD] se admin) ----
            if (g_ESP.names) {
                char nameBuf[80];
                snprintf(nameBuf, sizeof(nameBuf), "[%d] %s%s",
                         p.playerId, p.name, p.god ? " [GOD]" : "");
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
        g_DataMutex.unlock();
    }
}
