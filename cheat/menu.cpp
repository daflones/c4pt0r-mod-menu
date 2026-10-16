// ============================================================================
//  C4pt0r Cheats — ImGui Menu (dark + cyan, tabbed layout)
// ============================================================================

#include "menu.h"
#include "globals.h"
#include "imgui.h"

namespace C4pt0r::Menu
{
    // ---- Custom style: deep dark with cyan accent -----------------------
    void SetupStyle()
    {
        ImGuiStyle& s  = ImGui::GetStyle();
        ImVec4* c       = s.Colors;

        // Window / child / popup
        c[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.10f, 0.96f);
        c[ImGuiCol_ChildBg]            = ImVec4(0.07f, 0.07f, 0.12f, 1.00f);
        c[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.13f, 0.96f);
        c[ImGuiCol_Border]             = ImVec4(0.00f, 0.83f, 1.00f, 0.15f);

        // Headers / tabs
        c[ImGuiCol_Header]             = ImVec4(0.00f, 0.50f, 0.65f, 0.40f);
        c[ImGuiCol_HeaderHovered]      = ImVec4(0.00f, 0.65f, 0.80f, 0.55f);
        c[ImGuiCol_HeaderActive]       = ImVec4(0.00f, 0.83f, 1.00f, 0.60f);

        // Buttons
        c[ImGuiCol_Button]             = ImVec4(0.00f, 0.50f, 0.65f, 0.55f);
        c[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.65f, 0.80f, 0.70f);
        c[ImGuiCol_ButtonActive]       = ImVec4(0.00f, 0.83f, 1.00f, 0.85f);

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
        s.WindowRounding    = 10.0f;
        s.ChildRounding     = 6.0f;
        s.FrameRounding     = 5.0f;
        s.GrabRounding      = 5.0f;
        s.TabRounding       = 5.0f;
        s.ScrollbarRounding = 5.0f;
        s.PopupRounding     = 6.0f;

        s.WindowPadding     = ImVec2(14, 14);
        s.FramePadding      = ImVec2(10, 5);
        s.ItemSpacing       = ImVec2(10, 7);
        s.ItemInnerSpacing  = ImVec2(8, 5);
        s.WindowBorderSize  = 1.0f;
        s.FrameBorderSize   = 0.0f;
        s.TabBorderSize     = 0.0f;
        s.ScrollbarSize     = 12.0f;
        s.GrabMinSize       = 8.0f;
    }

    // ---- Helpers --------------------------------------------------------
    static void SectionHeader(const char* label) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.00f, 0.83f, 1.00f, 1.00f), "%s", label);
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
            // Feature tick saves the coords
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

    static void TabSettings()
    {
        SectionHeader("MENU");

        ImGui::Checkbox("Watermark", &g_Menu.showWatermark);
        ImGui::TextDisabled("Tecla do menu: F9");
        ImGui::TextDisabled("Panic key (unload): END");

        SectionHeader("INFO");
        ImGui::TextDisabled("C4pt0r Cheats v1.0");
        ImGui::TextDisabled("FiveM Internal");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.00f, 0.83f, 1.00f, 0.7f), "by Larpbase");
    }

    // ---- Main render ----------------------------------------------------
    void Render()
    {
        ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);

        ImGui::Begin("C4pt0r Cheats##main", &g_Menu.isOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        // ---- Tabs ----
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {

            if (ImGui::BeginTabItem("Combat"))   { TabCombat();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Visuals"))   { TabVisuals();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Player"))    { TabPlayer();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Teleport"))  { TabTeleport(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Settings"))  { TabSettings(); ImGui::EndTabItem(); }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
