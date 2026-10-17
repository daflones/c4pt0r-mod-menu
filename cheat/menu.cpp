// ============================================================================
//  C4pt0r Cheats — ImGui Menu (sidebar layout, cyan accent)
// ============================================================================

#include "menu.h"
#include "globals.h"
#include "features.h"
#include "natives.h"
#include "imgui.h"

#include <shellapi.h>
#include <chrono>

namespace C4pt0r::Menu
{
    // ---- Custom style: deep dark with cyan accent -----------------------
    void SetupStyle()
    {
        ImGuiStyle& s  = ImGui::GetStyle();
        ImVec4* c       = s.Colors;

        // Window / child / popup
        c[ImGuiCol_WindowBg]           = ImVec4(0.05f, 0.05f, 0.08f, 0.97f);
        c[ImGuiCol_ChildBg]            = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
        c[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.13f, 0.97f);
        c[ImGuiCol_Border]             = ImVec4(0.00f, 0.83f, 1.00f, 0.15f);

        // Headers / tabs
        c[ImGuiCol_Header]             = ImVec4(0.00f, 0.50f, 0.65f, 0.40f);
        c[ImGuiCol_HeaderHovered]      = ImVec4(0.00f, 0.65f, 0.80f, 0.55f);
        c[ImGuiCol_HeaderActive]       = ImVec4(0.00f, 0.83f, 1.00f, 0.60f);

        // Buttons
        c[ImGuiCol_Button]             = ImVec4(0.00f, 0.45f, 0.58f, 0.55f);
        c[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.65f, 0.80f, 0.75f);
        c[ImGuiCol_ButtonActive]       = ImVec4(0.00f, 0.83f, 1.00f, 0.90f);

        // Frame (sliders, inputs)
        c[ImGuiCol_FrameBg]            = ImVec4(0.10f, 0.10f, 0.16f, 0.80f);
        c[ImGuiCol_FrameBgHovered]     = ImVec4(0.00f, 0.50f, 0.65f, 0.45f);
        c[ImGuiCol_FrameBgActive]      = ImVec4(0.00f, 0.65f, 0.80f, 0.55f);

        // Tabs
        c[ImGuiCol_Tab]                = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
        c[ImGuiCol_TabHovered]         = ImVec4(0.00f, 0.65f, 0.80f, 0.65f);
        c[ImGuiCol_TabActive]          = ImVec4(0.00f, 0.50f, 0.65f, 0.80f);
        c[ImGuiCol_TabUnfocused]       = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.00f, 0.40f, 0.55f, 0.60f);

        // Slider grab
        c[ImGuiCol_SliderGrab]         = ImVec4(0.00f, 0.83f, 1.00f, 0.80f);
        c[ImGuiCol_SliderGrabActive]   = ImVec4(0.00f, 0.93f, 1.00f, 1.00f);

        // Checkbox
        c[ImGuiCol_CheckMark]          = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);

        // Title bar
        c[ImGuiCol_TitleBg]            = ImVec4(0.04f, 0.04f, 0.07f, 1.00f);
        c[ImGuiCol_TitleBgActive]      = ImVec4(0.05f, 0.05f, 0.09f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.03f, 0.03f, 0.06f, 0.80f);

        // Scrollbar
        c[ImGuiCol_ScrollbarBg]        = ImVec4(0.04f, 0.04f, 0.08f, 0.50f);
        c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.00f, 0.50f, 0.65f, 0.50f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.65f, 0.80f, 0.70f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 0.83f, 1.00f, 0.90f);

        // Text
        c[ImGuiCol_Text]               = ImVec4(0.90f, 0.93f, 0.96f, 1.00f);
        c[ImGuiCol_TextDisabled]       = ImVec4(0.40f, 0.43f, 0.46f, 1.00f);

        // Separator
        c[ImGuiCol_Separator]          = ImVec4(0.00f, 0.83f, 1.00f, 0.20f);
        c[ImGuiCol_SeparatorHovered]   = ImVec4(0.00f, 0.83f, 1.00f, 0.40f);
        c[ImGuiCol_SeparatorActive]    = ImVec4(0.00f, 0.83f, 1.00f, 0.60f);

        // Resize grip
        c[ImGuiCol_ResizeGrip]         = ImVec4(0.00f, 0.83f, 1.00f, 0.20f);
        c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.00f, 0.83f, 1.00f, 0.50f);
        c[ImGuiCol_ResizeGripActive]   = ImVec4(0.00f, 0.83f, 1.00f, 0.80f);

        // Rounding & spacing
        s.WindowRounding    = 12.0f;
        s.ChildRounding     = 8.0f;
        s.FrameRounding     = 6.0f;
        s.GrabRounding      = 6.0f;
        s.TabRounding       = 6.0f;
        s.ScrollbarRounding = 8.0f;
        s.PopupRounding     = 8.0f;

        s.WindowPadding     = ImVec2(14, 14);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(10, 8);
        s.ItemInnerSpacing  = ImVec2(8, 5);
        s.WindowBorderSize  = 1.0f;
        s.FrameBorderSize   = 0.0f;
        s.TabBorderSize     = 0.0f;
        s.ScrollbarSize     = 12.0f;
        s.GrabMinSize       = 9.0f;
    }

    // ---- Helpers --------------------------------------------------------
    static void SectionHeader(const char* label) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.83f, 1.00f, 1.00f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    static void Tooltip(const char* tip) {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(280.0f);
            ImGui::TextUnformatted(tip);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // Sidebar tab button — cyan-filled when active
    static bool SideTab(const char* label, int idx, float width)
    {
        bool active = (g_Menu.currentTab == idx);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.55f, 0.70f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.80f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f, 0.09f, 0.13f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.45f, 0.58f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.70f, 0.75f, 0.80f, 1.00f));
        }
        bool clicked = ImGui::Button(label, ImVec2(width, 40));
        ImGui::PopStyleColor(3);
        if (clicked) g_Menu.currentTab = idx;
        return clicked;
    }

    static void LinkButton(const char* label, const char* url, float width)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.18f, 0.80f));
        bool clicked = ImGui::Button(label, ImVec2(width, 30));
        ImGui::PopStyleColor();
        if (clicked && url && url[0])
            ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::IsItemHovered() && !url)
            ImGui::SetTooltip("Em breve");
    }

    // ---- Tab renderers --------------------------------------------------

    static void TabCombat()
    {
        SectionHeader("AIMBOT");

        ImGui::Checkbox("Ativar Aimbot", &g_Aimbot.enabled);
        Tooltip("Mira automatica nos jogadores proximos.");

        if (g_Aimbot.enabled) {
            ImGui::SliderFloat("FOV##aim", &g_Aimbot.fov, 1.0f, 90.0f, "%.1f");
            Tooltip("Angulo do cone de deteccao (graus).");

            ImGui::SliderFloat("Smooth##aim", &g_Aimbot.smooth, 1.0f, 20.0f, "%.1f");
            Tooltip("1 = snap instantaneo, maior = mais suave e humano.");

            ImGui::Checkbox("Apenas visiveis##aim", &g_Aimbot.visibleOnly);

            const char* bones[] = { "Cabeca", "Pescoco", "Peito" };
            ImGui::Combo("Osso alvo##aim", &g_Aimbot.targetBone, bones, IM_ARRAYSIZE(bones));
        }

        SectionHeader("NO RECOIL");
        ImGui::Checkbox("Ativar No Recoil", &g_Player.noRecoil);
        Tooltip("Cancela o recuo da arma salvando/restaurando o pitch da camera.");

        SectionHeader("ARMA");
        ImGui::Checkbox("Municao infinita", &g_Player.infiniteAmmo);
        Tooltip("O pente nunca esvazia — nao precisa recarregar.");
    }

    static void TabVisuals()
    {
        SectionHeader("ESP");

        ImGui::Checkbox("Ativar ESP", &g_ESP.enabled);
        Tooltip("Mostra jogadores atraves das paredes.");

        if (g_ESP.enabled) {
            ImGui::Checkbox("Caixas##esp",         &g_ESP.boxes);
            ImGui::Checkbox("Nomes##esp",          &g_ESP.names);
            ImGui::Checkbox("Barras de vida##esp",  &g_ESP.healthBars);
            ImGui::Checkbox("Snaplines##esp",      &g_ESP.snaplines);
            ImGui::Checkbox("Distancia##esp",      &g_ESP.distance);

            ImGui::SliderFloat("Dist. maxima##esp", &g_ESP.maxDistance, 50.0f, 2000.0f, "%.0f m");

            const char* filtros[] = { "Todos", "So normais", "So admins (god)" };
            ImGui::Combo("Filtro##esp", &g_ESP.filter, filtros, IM_ARRAYSIZE(filtros));
            Tooltip("Filtra quem aparece no ESP. 'God' = invencivel — quase sempre admin.");

            ImGui::Spacing();
            ImGui::ColorEdit4("Cor caixa##esp",  g_ESP.boxColor,  ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit4("Cor nome##esp",   g_ESP.nameColor, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit4("Cor admin##esp",  g_ESP.godColor,  ImGuiColorEditFlags_NoInputs);
            Tooltip("Caixa dourada + tag [GOD] em jogador invencivel.");
        }
    }

    static void TabPlayer()
    {
        SectionHeader("PROTECAO");

        ImGui::Checkbox("Godmode", &g_Player.godmode);
        Tooltip("Invencivel — vida nao abaixa.");

        ImGui::Checkbox("Invisivel", &g_Player.invisible);
        Tooltip("Outros jogadores nao te veem.");

        SectionHeader("MOVIMENTO");

        ImGui::Checkbox("Speed Hack", &g_Player.speedHack);
        if (g_Player.speedHack) {
            ImGui::SliderFloat("Multiplicador##spd", &g_Player.speedMultiplier, 1.0f, 10.0f, "%.1fx");
        }

        ImGui::Checkbox("Noclip", &g_Player.noclip);
        Tooltip("Voar atraves de paredes. WASD + Space/Ctrl.");
        if (g_Player.noclip) {
            ImGui::SliderFloat("Velocidade##noclip", &g_Player.noclipSpeed, 0.5f, 20.0f, "%.1f");
        }

        SectionHeader("EMERGENCIA");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.16f, 0.80f));
        if (ImGui::Button("RESTAURAR TUDO", ImVec2(-1, 32))) {
            Features::RestoreAll();
        }
        ImGui::PopStyleColor();
        Tooltip("Desfaz invisivel, colisao, godmode, velocidade — se algo ficar 'preso'.");
    }

    // Presets de teleporte (aproximados — GTA V map)
    struct TpPreset { const char* name; float x, y, z; };
    static const TpPreset s_Presets[] = {
        { "Praca (Legion Square)",      215.8f,  -923.3f,  30.7f },
        { "Pier (Del Perro)",          -1849.4f, -1231.5f, 13.0f },
        { "Aeroporto (LSIA)",          -1337.0f, -3044.0f, 13.9f },
        { "Norte (Paleto Bay)",         -223.5f,  6328.9f, 31.5f },
        { "Sul (Porto de LS)",          -505.4f, -2323.7f,  6.4f },
    };

    static void TabTeleport()
    {
        SectionHeader("TELEPORTE RAPIDO");

        if (ImGui::Button("TP para Waypoint (marca no mapa)", ImVec2(-1, 32))) {
            g_Teleport.toWaypoint = true;
        }
        Tooltip("Marca um ponto no mapa (esc) e teleporta pra la.");

        // ---- Presets ----
        static int s_Preset = 0;
        const char* names[IM_ARRAYSIZE(s_Presets)];
        for (int i = 0; i < IM_ARRAYSIZE(s_Presets); ++i) names[i] = s_Presets[i].name;
        ImGui::Combo("##preset", &s_Preset, names, IM_ARRAYSIZE(names));
        ImGui::SameLine();
        if (ImGui::Button("IR##presetbtn", ImVec2(60, 0))) {
            const auto& p = s_Presets[s_Preset];
            g_Teleport.tx = p.x; g_Teleport.ty = p.y; g_Teleport.tz = p.z;
            g_Teleport.toCoords = true;
        }

        // ---- Coords ----
        static float s_Coords[3] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat3("X, Y, Z##tp", s_Coords, "%.1f");
        if (ImGui::Button("TP por coordenada", ImVec2(-1, 30))) {
            g_Teleport.tx = s_Coords[0];
            g_Teleport.ty = s_Coords[1];
            g_Teleport.tz = s_Coords[2];
            g_Teleport.toCoords = true;
        }

        // ---- Saved position ----
        if (ImGui::Button("Salvar posicao atual", ImVec2(-1, 30))) {
            g_Teleport.hasSaved = true;
            g_Teleport.savedX = g_Teleport.savedY = g_Teleport.savedZ = 0; // re-captura no tick
        }

        if (g_Teleport.hasSaved) {
            ImGui::TextDisabled("Salvo: %.1f, %.1f, %.1f",
                g_Teleport.savedX, g_Teleport.savedY, g_Teleport.savedZ);
            if (ImGui::Button("TP para posicao salva", ImVec2(-1, 30))) {
                g_Teleport.toSaved = true;
            }
        }

        // ---- Players ----
        SectionHeader("JOGADORES");

        static int s_SelPlayer = -1;
        // try_lock: nunca deixar a thread de render travar no mutex
        if (g_DataMutex.try_lock_for(std::chrono::milliseconds(10))) {
            ImGui::BeginChild("##plist", ImVec2(0, 120), ImGuiChildFlags_Border);
            if (g_PlayerListCount == 0) {
                ImGui::TextDisabled("  Nenhum jogador online visivel.");
            }
            for (int i = 0; i < g_PlayerListCount; ++i) {
                const auto& p = g_PlayerList[i];
                char label[128];
                snprintf(label, sizeof(label), "[%2d] %s  %.0fm%s%s",
                    p.id, p.name, p.distance,
                    p.inVehicle ? "  carro" : "",
                    p.dead ? "  morto" : "");
                if (ImGui::Selectable(label, s_SelPlayer == p.id))
                    s_SelPlayer = p.id;
            }
            ImGui::EndChild();
            g_DataMutex.unlock();
        }

        if (s_SelPlayer >= 0) {
            ImGui::TextDisabled("Selecionado: ID %d", s_SelPlayer);
            if (ImGui::Button("IR ATE ELE", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 30)))
                g_Teleport.toPlayerId = s_SelPlayer;
            ImGui::SameLine();
            if (ImGui::Button("TRAZER PRA MIM", ImVec2(-1, 30)))
                g_Teleport.bringPlayerId = s_SelPlayer;
            Tooltip("Trazer o jogador pra perto de voce (efeito local).");
        }

        // ---- Vehicles ----
        SectionHeader("SPAWN DE VEICULO");

        static const char* vehNames[] = {
            "Adder", "T20", "Oppressor MK2", "Kuruma blindado",
            "Insurgent", "Sultan RS", "Vigilante", "Sparrow (heli)", "Rhino (tanque)"
        };
        static const char* vehModels[] = {
            "adder", "t20", "oppressor2", "kuruma2",
            "insurgent2", "sultanrs", "vigilante", "sparrow", "rhino"
        };
        static int s_VehIdx = 0;
        ImGui::Combo("##vehpreset", &s_VehIdx, vehNames, IM_ARRAYSIZE(vehNames));

        static char s_CustomModel[48] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##vehmodel", "ou digite o modelo (ex: zentorno)",
                                 s_CustomModel, sizeof(s_CustomModel));

        ImGui::Checkbox("Entrar no veiculo ao spawnar##veh", &g_Vehicle.warpIn);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.55f, 0.30f, 0.80f));
        if (ImGui::Button("SPAWNAR VEICULO", ImVec2(-1, 34))) {
            const char* m = s_CustomModel[0] ? s_CustomModel : vehModels[s_VehIdx];
            strncpy_s(g_Vehicle.model, m, sizeof(g_Vehicle.model) - 1);
            g_Vehicle.spawn = true;
        }
        ImGui::PopStyleColor();
        Tooltip("Spawn so funciona se o servidor permitir criacao de entidades.");
    }

    // ---- MUNDO: clima, tempo, gravidade, caos, spawns, veiculo fun -------
    static void TabMundo()
    {
        SectionHeader("CLIMA");

        static const char* wNames[] = {
            "Limpo","Ensolarado","Nublado","Encoberto","Chuva",
            "Tempestade","Pos-chuva","Nevoeiro","Neblina",
            "Natal (neve)","Neve leve","Nevasca"
        };
        ImGui::Combo("##weather", &g_Fun.weather, wNames, IM_ARRAYSIZE(wNames));
        ImGui::SameLine();
        if (ImGui::Button("APLICAR##w", ImVec2(90,0))) g_Fun.setWeather = true;
        ImGui::SameLine();
        if (ImGui::Button("LIMPAR##w", ImVec2(80,0))) g_Fun.clearWeather = true;
        Tooltip("Forca o clima local (Tempestade = trovoes e chuva forte).");

        SectionHeader("TEMPO & FISICA");

        ImGui::SliderInt("Hora##clk", &g_Fun.clockH, 0, 23);
        ImGui::SliderInt("Minuto##clk", &g_Fun.clockM, 0, 59);
        if (ImGui::Button("Aplicar hora", ImVec2(150,0))) g_Fun.setClock = true;
        ImGui::SameLine();
        ImGui::Checkbox("Congelar tempo", &g_Fun.pauseTime);

        static const char* gravNames[] = { "Terra (normal)", "Lua", "Muito baixa" };
        ImGui::Combo("Gravidade##g", &g_Fun.gravity, gravNames, IM_ARRAYSIZE(gravNames));
        ImGui::SameLine();
        if (ImGui::Button("Aplicar##g", ImVec2(90,0))) g_Fun.setGravity = true;

        ImGui::Checkbox("Camera lenta", &g_Fun.slowmo);
        if (g_Fun.slowmo)
            ImGui::SliderFloat("Escala##ts", &g_Fun.timeScale, 0.05f, 1.0f, "%.2fx");

        SectionHeader("EFEITOS NO JOGADOR");
        ImGui::Checkbox("Super pulo", &g_Fun.superJump);
        ImGui::SameLine();
        ImGui::Checkbox("Nunca procurado", &g_Fun.noWanted);
        ImGui::Checkbox("Policia me ignora", &g_Fun.policeIgnore);
        ImGui::SameLine();
        ImGui::Checkbox("Todos me ignoram", &g_Fun.everyoneIgnore);
        ImGui::Checkbox("Bebado", &g_Fun.drunk);
        ImGui::SameLine();
        ImGui::Checkbox("Visao termica", &g_Fun.thermal);
        ImGui::SameLine();
        ImGui::Checkbox("Visao noturna", &g_Fun.nightvision);

        SectionHeader("CAOS");
        if (ImGui::Button("EXPLODIR A FRENTE", ImVec2(-1, 30))) g_Fun.explode = true;
        if (ImGui::Button("INCENDIO AQUI", ImVec2(-1, 30)))     g_Fun.fire = true;
        if (ImGui::Button("AGUA DO MAR AQUI", ImVec2(-1, 30)))  g_Fun.water = true;
        Tooltip("Cria uma lamina d'agua onde voce esta (experimental).");
        if (ImGui::Button("CLONAR-ME", ImVec2(-1, 30)))         g_Fun.clone = true;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.32f, 0.00f, 0.80f));
        if (ImGui::Button("CHUVA DE PORCOS", ImVec2(-1, 34)))   g_Fun.pigRain = true;
        ImGui::PopStyleColor();
        Tooltip("12 porcos despencando do ceu.");

        SectionHeader("SPAWN DE PED / ANIMAL / NPC");
        static const char* pedNames[] = {
            "Gato","Husky","Veado","Vaca","Javali","Coelho","Galinha","Pombo",
            "Chimpanze","Macaco Rhesus","Onca","Coiote",
            "Tubarao","Golfinho","Zumbi","Policial","Guarda armado","Hipster"
        };
        static const char* pedModels[] = {
            "a_c_cat_01","a_c_husky","a_c_deer","a_c_cow","a_c_boar",
            "a_c_rabbit_01","a_c_hen","a_c_pigeon","a_c_chimp","a_c_rhesus",
            "a_c_mtlion","a_c_coyote","a_c_sharktiger","a_c_dolphin",
            "u_m_y_zombie_01","s_m_y_cop_01","s_m_m_armoured_02","a_m_y_hipster_01"
        };
        static int s_PedIdx = 0;
        ImGui::Combo("##pedpreset", &s_PedIdx, pedNames, IM_ARRAYSIZE(pedNames));

        static char s_PedModel[48] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pedmodel", "ou digite o modelo (ex: a_c_seagull)",
                                 s_PedModel, sizeof(s_PedModel));
        ImGui::Checkbox("Guarda-costas (te segue)", &g_Fun.pedBodyguard);
        ImGui::SameLine();
        ImGui::Checkbox("Hostil", &g_Fun.pedHostile);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.55f, 0.30f, 0.80f));
        if (ImGui::Button("SPAWNAR PED", ImVec2(-1, 34))) {
            const char* m = s_PedModel[0] ? s_PedModel : pedModels[s_PedIdx];
            strncpy_s(g_Fun.pedModel, m, sizeof(g_Fun.pedModel) - 1);
            g_Fun.spawnPed = true;
        }
        ImGui::PopStyleColor();
        Tooltip("Spawn local — em RP serio outros jogadores podem nao ver.");

        SectionHeader("VEICULO ATUAL");
        if (ImGui::Button("Consertar / limpar", ImVec2(-1, 30))) g_Fun.repairVeh = true;
        if (ImGui::Button("Virar pra cima", ImVec2(-1, 30)))     g_Fun.flipVeh = true;
        if (ImGui::Button("TURBO +35 m/s", ImVec2(-1, 30)))      g_Fun.boostVeh = true;
    }

    static void TabTools()
    {
        SectionHeader("INSPECTOR AO VIVO");

        ImGui::Checkbox("Atualizar inspector", &g_Farm.inspEnabled);
        if (g_Farm.inspEnabled) {
            const auto& d = g_Inspector;
            ImGui::Text("Nome:    %s", d.name[0] ? d.name : "-");
            ImGui::Text("Player:  %d    Ped: %d", d.player, d.ped);
            ImGui::Text("Pos:     %.2f, %.2f, %.2f", d.pos.x, d.pos.y, d.pos.z);
            ImGui::Text("Heading: %.1f   Speed: %.1f m/s", d.heading, d.speed);
            ImGui::Text("Vida:    %d   Colete: %d   Arma: 0x%08X",
                        d.health, d.armor, d.weapon);
            ImGui::Text("Veiculo: %s", d.inVehicle ? "sim" : "nao");

            ImGui::Spacing();
            ImGui::TextDisabled("Controles pressionados agora:");
            bool any = false;
            for (int i = 0; i < 8; ++i) {
                if (d.pressed[i]) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0, 0.83f, 1, 1), "[%d]", d.pressed[i]);
                    any = true;
                }
            }
            if (!any) ImGui::TextDisabled("  (nenhum — aperte E/F/G/R...)");

            // control id cheatsheet
            ImGui::Spacing();
            ImGui::TextDisabled("IDs: 51=E(contexto) 38=E(pickup) 75=F 47=G 45=R 244=M");
        }

        SectionHeader("MACRO DE FARM (coleta -> venda)");

        if (ImGui::Button(g_Farm.hasPointA ? "Regravar ponto A (coleta)" : "Gravar ponto A (coleta)",
                          ImVec2(-1, 30))) {
            g_Farm.ax = g_Inspector.pos.x;
            g_Farm.ay = g_Inspector.pos.y;
            g_Farm.az = g_Inspector.pos.z;
            g_Farm.hasPointA = true;
        }
        if (g_Farm.hasPointA)
            ImGui::TextDisabled("  A: %.1f, %.1f, %.1f", g_Farm.ax, g_Farm.ay, g_Farm.az);

        if (ImGui::Button(g_Farm.hasPointB ? "Regravar ponto B (venda)" : "Gravar ponto B (venda)",
                          ImVec2(-1, 30))) {
            g_Farm.bx = g_Inspector.pos.x;
            g_Farm.by = g_Inspector.pos.y;
            g_Farm.bz = g_Inspector.pos.z;
            g_Farm.hasPointB = true;
        }
        if (g_Farm.hasPointB)
            ImGui::TextDisabled("  B: %.1f, %.1f, %.1f", g_Farm.bx, g_Farm.by, g_Farm.bz);

        ImGui::Spacing();
        const char* keys[] = { "E (contexto 51)", "E (pickup 38)", "F (75)", "G (47)" };
        static const int keyIds[] = { 51, 38, 75, 47 };
        int cur = 0;
        for (int i = 0; i < 4; ++i) if (keyIds[i] == g_Farm.keyControl) cur = i;
        if (ImGui::Combo("Tecla da acao##farm", &cur, keys, IM_ARRAYSIZE(keys)))
            g_Farm.keyControl = keyIds[cur];

        ImGui::SliderInt("Espera apos TP (ms)##farm", &g_Farm.settleMs, 200, 5000);
        ImGui::SliderInt("Intervalo da acao (ms)##farm", &g_Farm.intervalMs, 200, 10000);
        ImGui::InputInt("Repeticoes (0 = infinito)##farm", &g_Farm.repeatTimes);
        if (g_Farm.repeatTimes < 0) g_Farm.repeatTimes = 0;

        ImGui::Spacing();
        if (!g_Farm.running) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.60f, 0.30f, 0.80f));
            if (ImGui::Button("INICIAR FARM", ImVec2(-1, 34)))
                g_Farm.running = true;
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.10f, 0.10f, 0.80f));
            if (ImGui::Button("PARAR FARM", ImVec2(-1, 34)))
                g_Farm.running = false;
            ImGui::PopStyleColor();
        }
        if (g_Farm.cyclesDone > 0)
            ImGui::TextDisabled("  Ciclos feitos: %d", g_Farm.cyclesDone);
    }

    static void TabSettings()
    {
        SectionHeader("MENU");

        ImGui::Checkbox("Watermark", &g_Menu.showWatermark);
        ImGui::TextDisabled("Tecla do menu: F9");
        ImGui::TextDisabled("Panic key (unload): END");

        SectionHeader("SISTEMA");

        char hb[160];
        Natives::HealthString(hb, sizeof(hb));
        bool ok = Natives::IsReady();
        if (ok) ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.40f, 1.0f), "Natives: OK");
        else    ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.40f, 1.0f), "Natives: RECONECTANDO...");
        ImGui::TextDisabled("%s", hb);

        if (ImGui::Button("FORCAR RECONEXAO", ImVec2(-1, 30))) {
            __try { Natives::ReInitialize(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        Tooltip("Re-resolve os exports e re-probe os natives (se algo travar).");

        SectionHeader("COMUNIDADE");
        LinkButton("Discord  —  discord.gg/PhkbhxMBy",
                   "https://discord.gg/PhkbhxMBy", -1);
        LinkButton("YouTube  —  C4pt0r - Mods (em breve)", nullptr, -1);

        SectionHeader("INFO");
        ImGui::TextDisabled("C4pt0r Cheats v1.7");
        ImGui::TextDisabled("FiveM Internal");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.00f, 0.83f, 1.00f, 0.7f), "by Larpbase");
    }

    // ---- Main render ----------------------------------------------------
    void Render()
    {
        ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);

        ImGui::Begin("C4pt0r Cheats##main", &g_Menu.isOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        // ---- Sidebar ----
        ImGui::BeginChild("##sidebar", ImVec2(160, 0),
                          ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.83f, 1.00f, 1.00f));
        ImGui::TextUnformatted("C4pt0r");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("v1.7");

        // indicador de saude dos natives — visivel de cara se cair
        bool nOk = Natives::IsReady();
        ImVec2 dp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(dp.x + 5, dp.y + 8), 4.0f,
            nOk ? IM_COL32(40, 220, 90, 255) : IM_COL32(255, 60, 70, 255));
        ImGui::Dummy(ImVec2(14, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", nOk ? "natives ok" : "reconectando");
        ImGui::Separator();
        ImGui::Spacing();

        float tabW = ImGui::GetContentRegionAvail().x;
        SideTab("Combat",    0, tabW);
        SideTab("Visuals",   1, tabW);
        SideTab("Player",    2, tabW);
        SideTab("Teleport",  3, tabW);
        SideTab("Mundo",     4, tabW);
        SideTab("Ferramentas", 5, tabW);
        SideTab("Settings",  6, tabW);

        // push links to the bottom of the sidebar
        float remain = ImGui::GetContentRegionAvail().y - 70;
        if (remain > 0) ImGui::Dummy(ImVec2(0, remain));
        ImGui::Separator();
        LinkButton("Discord", "https://discord.gg/PhkbhxMBy", tabW);
        LinkButton("YouTube (breve)", nullptr, tabW);

        ImGui::EndChild();
        ImGui::SameLine();

        // ---- Content ----
        ImGui::BeginChild("##content", ImVec2(0, 0));

        switch (g_Menu.currentTab) {
            case 0: TabCombat();   break;
            case 1: TabVisuals();  break;
            case 2: TabPlayer();   break;
            case 3: TabTeleport(); break;
            case 4: TabMundo();    break;
            case 5: TabTools();    break;
            case 6: TabSettings(); break;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
