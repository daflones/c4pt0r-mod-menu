// ============================================================================
//  C4pt0r Cheats — ImGui Menu (sidebar layout, cyan accent)
// ============================================================================

#include "menu.h"
#include "globals.h"
#include "imgui.h"

#include <shellapi.h>

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
        bool clicked = ImGui::Button(label, ImVec2(width, 36));
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

            ImGui::Spacing();
            ImGui::ColorEdit4("Cor caixa##esp",  g_ESP.boxColor,  ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit4("Cor nome##esp",   g_ESP.nameColor, ImGuiColorEditFlags_NoInputs);
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
    }

    static void TabTeleport()
    {
        SectionHeader("TELEPORTE");

        if (ImGui::Button("TP para Waypoint", ImVec2(-1, 32))) {
            g_Teleport.toWaypoint = true;
        }
        Tooltip("Marca no mapa e teleporta pra la.");

        ImGui::Spacing();

        if (ImGui::Button("Salvar posicao", ImVec2(-1, 32))) {
            g_Teleport.hasSaved = true;
        }

        if (g_Teleport.hasSaved) {
            ImGui::TextDisabled("Salvo: %.1f, %.1f, %.1f",
                g_Teleport.savedX, g_Teleport.savedY, g_Teleport.savedZ);

            if (ImGui::Button("TP para posicao salva", ImVec2(-1, 32))) {
                g_Teleport.toSaved = true;
            }
        }
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

        SectionHeader("COMUNIDADE");
        LinkButton("Discord  —  discord.gg/PhkbhxMBy",
                   "https://discord.gg/PhkbhxMBy", -1);
        LinkButton("YouTube  —  C4pt0r - Mods (em breve)", nullptr, -1);

        SectionHeader("INFO");
        ImGui::TextDisabled("C4pt0r Cheats v1.0");
        ImGui::TextDisabled("FiveM Internal");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.00f, 0.83f, 1.00f, 0.7f), "by Larpbase");
    }

    // ---- Main render ----------------------------------------------------
    void Render()
    {
        ImGui::SetNextWindowSize(ImVec2(600, 460), ImGuiCond_FirstUseEver);

        ImGui::Begin("C4pt0r Cheats##main", &g_Menu.isOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        // ---- Sidebar ----
        ImGui::BeginChild("##sidebar", ImVec2(140, 0),
                          ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.83f, 1.00f, 1.00f));
        ImGui::TextUnformatted("C4pt0r");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("v1.0");
        ImGui::Separator();
        ImGui::Spacing();

        float tabW = ImGui::GetContentRegionAvail().x;
        SideTab("Combat",    0, tabW);
        SideTab("Visuals",   1, tabW);
        SideTab("Player",    2, tabW);
        SideTab("Teleport",  3, tabW);
        SideTab("Ferramentas", 4, tabW);
        SideTab("Settings",  5, tabW);

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
            case 4: TabTools();    break;
            case 5: TabSettings(); break;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
