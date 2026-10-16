// ============================================================================
//  C4pt0r Spoofer — app standalone (ImGui/D3D11)
//  Login -> licenca 'c4pt0r-spoofer' -> painel de spoof.
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <dwmapi.h>
#include <wincrypt.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <Shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#include "..\injector\net.h"
#include "spoof.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// ---- Config (mesma infra do loader) ------------------------------------------
static const char* SUPA_HOST = "bzthdklqvjtutkeiimpq.supabase.co";
static const char* ANON_KEY  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJ6dGhka2xxdmp0dXRrZWlpbXBxIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTIxNzU2MDYsImV4cCI6MjEwNzc1MTYwNn0.K1_us_hjL10yynZRDsrWZKwId-l5NuzrDSuHeFXtIdI";
static const char* PRODUCT_SLUG = "c4pt0r-spoofer";
static const wchar_t* STORE_URL = L"https://c4pt0r-store.eletrify.me/checkout/c4pt0r-spoofer";
static const wchar_t* DISCORD_URL = L"https://discord.gg/PhkbhxMBy";

// ---- D3D11 -------------------------------------------------------------------
static ID3D11Device*           g_pd3dDevice  = nullptr;
static ID3D11DeviceContext*    g_pd3dCtx     = nullptr;
static IDXGISwapChain*         g_pSwapChain  = nullptr;
static ID3D11RenderTargetView* g_mainRTV     = nullptr;
static HWND                    g_hWnd        = nullptr;

static void CreateRTV() {
    ID3D11Texture2D* bb = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRTV);
    bb->Release();
}
static void CleanupRTV() { if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; } }
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, nullptr, &g_pd3dCtx));
}

// ---- State ---------------------------------------------------------------------
enum Screen { SCR_LOGIN, SCR_MAIN };
enum StatKind { ST_NEUTRO, ST_OK, ST_ERR, ST_GOLD };

static struct {
    std::atomic<int>  screen{SCR_LOGIN};
    std::atomic<bool> busy{false};
    std::atomic<bool> licensed{false};
    std::atomic<int>  statKind{ST_NEUTRO};
    std::string       accessToken, refreshToken, email;
    std::string       status;
    std::string       validity;
    std::vector<SpoofResult> results;
    std::mutex        mtx;
    bool              optReg = true, optMac = true, optVol = true,
                      optHost = false, optClean = true;
    bool              spoofDone = false;
} g;

static void SetStatus(const std::string& s, StatKind k = ST_NEUTRO) {
    std::lock_guard<std::mutex> l(g.mtx);
    g.status = s;
    g.statKind = k;
}

// ---- Session (mesma do loader — login unificado) ---------------------------------
static std::wstring SessPath() {
    wchar_t* app = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &app);
    std::wstring dir = app;
    CoTaskMemFree(app);
    dir += L"\\C4pt0r";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\session.bin";
}
static void SaveRefresh(const std::string& token) {
    DATA_BLOB in{ (DWORD)token.size(), (BYTE*)token.data() }, out{};
    if (CryptProtectData(&in, L"c4", nullptr, nullptr, nullptr, 0, &out)) {
        HANDLE f = CreateFileW(SessPath().c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w; WriteFile(f, out.pbData, out.cbData, &w, nullptr); CloseHandle(f);
        }
        LocalFree(out.pbData);
    }
}
static std::string LoadRefresh() {
    HANDLE f = CreateFileW(SessPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    DWORD sz = GetFileSize(f, nullptr);
    if (!sz || sz > 8192) { CloseHandle(f); return ""; }
    std::vector<BYTE> buf(sz);
    DWORD r; ReadFile(f, buf.data(), sz, &r, nullptr); CloseHandle(f);
    DATA_BLOB in{ sz, buf.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return "";
    std::string t((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return t;
}
static void ClearSession() { DeleteFileW(SessPath().c_str()); }

// ---- Supabase ----------------------------------------------------------------------
static std::vector<std::pair<std::string, std::string>> ApiHdrs(const std::string& jwt = "") {
    std::vector<std::pair<std::string, std::string>> h = {
        {"apikey", ANON_KEY},
        {"Content-Type", "application/json"},
    };
    if (!jwt.empty()) h.push_back({ "Authorization", "Bearer " + jwt });
    return h;
}
static HttpRes FnCall(const char* name, const std::string& body, const std::string& jwt) {
    return Http(SUPA_HOST, "POST", std::string("/functions/v1/") + name, ApiHdrs(jwt), body);
}
static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}

static bool DoPasswordGrant(const std::string& email, const std::string& pass, std::string& err) {
    std::string body = "{\"email\":\"" + JsonEsc(email) + "\",\"password\":\"" + JsonEsc(pass) + "\"}";
    auto r = Http(SUPA_HOST, "POST", "/auth/v1/token?grant_type=password", ApiHdrs(), body);
    auto j = JsonParse(r.body);
    if (r.status == 200 && !j["access_token"].str().empty()) {
        g.accessToken = j["access_token"].str();
        g.refreshToken = j["refresh_token"].str();
        SaveRefresh(g.refreshToken);
        return true;
    }
    err = j["error_description"].str().empty() ? j["msg"].str() : j["error_description"].str();
    if (err.empty()) err = "Email ou senha incorretos.";
    return false;
}
static bool DoRefresh(const std::string& rt) {
    std::string body = "{\"refresh_token\":\"" + JsonEsc(rt) + "\"}";
    auto r = Http(SUPA_HOST, "POST", "/auth/v1/token?grant_type=refresh_token", ApiHdrs(), body);
    auto j = JsonParse(r.body);
    if (r.status == 200 && !j["access_token"].str().empty()) {
        g.accessToken = j["access_token"].str();
        g.refreshToken = j["refresh_token"].str();
        SaveRefresh(g.refreshToken);
        return true;
    }
    return false;
}

static bool CheckLicense() {
    std::string body = "{\"action\":\"check\",\"product_slug\":\"" + std::string(PRODUCT_SLUG) + "\"}";
    auto r = FnCall("license-check", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["licensed"].truthy()) {
        auto lic = j["license"];
        std::string exp = lic["expires_at"].str();
        g.validity = exp.empty() ? "Vitalicia" : "ate " + exp.substr(0, 10);
        return true;
    }
    return false;
}

// ---- Workers --------------------------------------------------------------------------
static void ThreadLogin(std::string email, std::string pass) {
    g.busy = true;
    std::string err;
    if (DoPasswordGrant(email, pass, err)) {
        g.email = email;
        if (CheckLicense()) {
            g.licensed = true;
            g.screen = SCR_MAIN;
            SetStatus("", ST_NEUTRO);
        } else {
            g.licensed = false;
            g.screen = SCR_MAIN;
            SetStatus("Sem licenca do C4pt0r Spoofer.", ST_GOLD);
        }
    } else SetStatus(err, ST_ERR);
    g.busy = false;
}

static void ThreadSpoof() {
    g.busy = true;
    g.spoofDone = false;
    { std::lock_guard<std::mutex> l(g.mtx); g.results.clear(); }
    SetStatus("Executando spoof...", ST_NEUTRO);
    std::vector<SpoofResult> all;
    std::vector<std::wstring> backup;

    if (g.optReg) {
        bool rb; auto r = SpoofRegistry(rb, &backup);
        all.insert(all.end(), r.begin(), r.end());
    }
    if (g.optMac) {
        auto r = SpoofMac();
        all.insert(all.end(), r.begin(), r.end());
    }
    if (g.optVol) all.push_back(SpoofVolumeSerial(L"C:"));
    if (g.optHost) all.push_back(SpoofHostname(&backup));
    if (g.optClean) all.push_back(CleanFiveMTraces());

    WriteBackup(backup);
    {
        std::lock_guard<std::mutex> l(g.mtx);
        g.results = all;
    }
    g.spoofDone = true;
    int ok = 0; for (auto& r : all) if (r.ok) ok++;
    char d[128];
    snprintf(d, 128, "Spoof concluido: %d/%d operacoes. Reinicie o PC!", ok, (int)all.size());
    SetStatus(d, ok == (int)all.size() ? ST_OK : ST_GOLD);
    g.busy = false;
}

static void ThreadRestore() {
    g.busy = true;
    auto r = RestoreBackup();
    { std::lock_guard<std::mutex> l(g.mtx); g.results = r; }
    SetStatus(r[0].detail, r[0].ok ? ST_OK : ST_ERR);
    g.busy = false;
}

// ============================================================================
//  UI
// ============================================================================
static ImVec4 COL_ACCENT  = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
static ImVec4 COL_CARD    = ImVec4(0.075f, 0.085f, 0.16f, 1.00f);
static ImVec4 COL_CARD2   = ImVec4(0.10f, 0.11f, 0.20f, 1.00f);
static ImVec4 COL_TEXT    = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
static ImVec4 COL_DIM     = ImVec4(0.48f, 0.50f, 0.58f, 1.00f);
static ImVec4 COL_OK      = ImVec4(0.18f, 0.90f, 0.42f, 1.00f);
static ImVec4 COL_GOLD    = ImVec4(1.00f, 0.72f, 0.25f, 1.00f);
static ImVec4 COL_ERR     = ImVec4(1.00f, 0.30f, 0.42f, 1.00f);
static ImVec4 COL_INPUT   = ImVec4(0.05f, 0.055f, 0.11f, 1.00f);
static ImFont *F_Title = nullptr, *F_Big = nullptr, *F_Med = nullptr, *F_Sm = nullptr;

static void SetupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.040f, 0.042f, 0.082f, 1.00f);
    c[ImGuiCol_ChildBg]         = COL_CARD;
    c[ImGuiCol_Border]          = ImVec4(0.00f, 0.83f, 1.00f, 0.14f);
    c[ImGuiCol_FrameBg]         = COL_INPUT;
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.08f, 0.09f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.10f, 0.12f, 0.22f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.00f, 0.55f, 0.75f, 0.85f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.00f, 0.70f, 0.90f, 0.90f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    c[ImGuiCol_Text]            = COL_TEXT;
    c[ImGuiCol_TextDisabled]    = COL_DIM;
    c[ImGuiCol_Separator]       = ImVec4(0.00f, 0.83f, 1.00f, 0.20f);
    c[ImGuiCol_CheckMark]       = COL_ACCENT;
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.03f, 0.03f, 0.06f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.00f, 0.45f, 0.60f, 0.55f);
    s.WindowRounding = 12.0f; s.ChildRounding = 10.0f; s.FrameRounding = 8.0f;
    s.ScrollbarRounding = 10.0f;
    s.WindowPadding = ImVec2(16, 16); s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.WindowBorderSize = 0; s.ChildBorderSize = 1.0f; s.FrameBorderSize = 1.0f;
}
static bool PrimaryBtn(const char* label, float w = -1, float h = 44) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.62f, 0.80f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.78f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.90f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.02f, 0.10f, 0.14f, 1.00f));
    bool c = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(4);
    return c;
}
static bool GhostBtn(const char* label, float w = -1, float h = 34) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.11f, 0.12f, 0.21f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.18f, 0.30f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.55f, 0.75f, 0.80f));
    bool c = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    return c;
}
static void StatusArea() {
    ImVec4 col = g.statKind == ST_OK ? COL_OK : g.statKind == ST_ERR ? COL_ERR : COL_GOLD;
    std::string st;
    { std::lock_guard<std::mutex> l(g.mtx); st = g.status; }
    if (!st.empty()) {
        ImGui::PushFont(F_Med);
        ImGui::TextColored(col, "%s", st.c_str());
        ImGui::PopFont();
    }
}
static void DrawHeader(const char* subtitle) {
    ImGui::PushFont(F_Title);
    ImGui::TextColored(COL_ACCENT, "C4PT0R ");
    ImGui::SameLine(0, 0);
    ImGui::TextColored(COL_GOLD, "SPOOFER");
    ImGui::PopFont();
    ImGui::PushFont(F_Sm);
    ImGui::TextColored(COL_DIM, "%s", subtitle);
    ImGui::PopFont();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 90, p.y + 2),
        ImGui::ColorConvertFloat4ToU32(COL_ACCENT));
    dl->AddRectFilled(ImVec2(p.x + 92, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x - 4, p.y + 2),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.16f, 0.18f, 0.32f, 1.00f)));
    ImGui::Dummy(ImVec2(0, 10));
}

static void ScreenLogin() {
    DrawHeader("Entre para usar o spoofer");

    ImGui::BeginChild("##logincard", ImVec2(0, 320), ImGuiChildFlags_Border);
    ImGui::Dummy(ImVec2(0, 8));
    static char email[128] = "", pass[128] = "";

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "EMAIL"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##email", "voce@email.com", email, sizeof(email));
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "SENHA"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pass", "********", pass, sizeof(pass),
        ImGuiInputTextFlags_Password);
    ImGui::Dummy(ImVec2(0, 10));

    if (PrimaryBtn("ENTRAR", -1, 46)) {
        if (!g.busy && email[0] && pass[0]) {
            SetStatus("Entrando...", ST_NEUTRO);
            std::thread(ThreadLogin, std::string(email), std::string(pass)).detach();
        }
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 10));
    StatusArea();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 56);
    if (GhostBtn("COMPRAR O SPOOFER NA LOJA", -1, 34))
        ShellExecuteW(nullptr, L"open", STORE_URL, nullptr, nullptr, SW_SHOW);
}

static void ScreenMain() {
    DrawHeader(g.email.c_str());

    // banner de licenca
    if (!g.licensed) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.22f, 0.14f, 0.05f, 1.00f));
        ImGui::BeginChild("##nolicense", ImVec2(0, 96), ImGuiChildFlags_Border);
        ImGui::PushFont(F_Med);
        ImGui::TextColored(COL_GOLD, "LICENCA NECESSARIA");
        ImGui::PopFont();
        ImGui::PushFont(F_Sm);
        ImGui::TextColored(COL_DIM, "Voce precisa de uma licenca do C4pt0r Spoofer.");
        ImGui::PopFont();
        if (PrimaryBtn("COMPRAR AGORA", -1, 36))
            ShellExecuteW(nullptr, L"open", STORE_URL, nullptr, nullptr, SW_SHOW);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        // IDs atuais
        static CurrentIds ids; static bool idsLoaded = false;
        if (!idsLoaded) { ids = ReadCurrentIds(); idsLoaded = true; }

        ImGui::BeginChild("##ids", ImVec2(0, 128), ImGuiChildFlags_Border);
        ImGui::PushFont(F_Sm);
        ImGui::TextColored(COL_DIM, "IDS ATUAIS");
        ImGui::PopFont();
        ImGui::TextColored(COL_TEXT, "GUID: %s", NarrowS(ids.machineGuid).c_str());
        ImGui::TextColored(COL_TEXT, "PC:   %s", NarrowS(ids.computerName).c_str());
        for (auto& m : ids.macs)
            ImGui::TextColored(COL_TEXT, "MAC:  %s  (%s)", NarrowS(m.second).c_str(),
                NarrowS(m.first).c_str());
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, 4));

        // opcoes
        ImGui::BeginChild("##opts", ImVec2(0, 118), ImGuiChildFlags_Border);
        ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "O QUE SPOOFAR"); ImGui::PopFont();
        ImGui::Checkbox("IDs do registro (MachineGuid, SQM, ProductId)", &g.optReg);
        ImGui::Checkbox("MAC address + Serial do volume", &g.optMac);
        ImGui::SameLine(0, 4); ImGui::Checkbox("##vol", &g.optVol); ImGui::SameLine(0, 0);
        ImGui::TextDisabled("(volume)");
        ImGui::Checkbox("Nome do PC", &g.optHost);
        ImGui::SameLine();
        ImGui::Checkbox("Limpar rastros do FiveM", &g.optClean);
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, 4));

        // acoes
        if (PrimaryBtn(g.busy ? "TRABALHANDO..." : "SPOOFAR AGORA", -1, 46)) {
            if (!g.busy) std::thread(ThreadSpoof).detach();
        }
        ImGui::Dummy(ImVec2(0, 2));
        if (GhostBtn(HasBackup() ? "RESTAURAR IDS ORIGINAIS" : "RESTORE (sem backup)", -1, 34)) {
            if (!g.busy && HasBackup()) std::thread(ThreadRestore).detach();
        }
        ImGui::Dummy(ImVec2(0, 4));

        // resultados
        std::vector<SpoofResult> rs;
        { std::lock_guard<std::mutex> l(g.mtx); rs = g.results; }
        if (!rs.empty()) {
            ImGui::BeginChild("##results", ImVec2(0, -110), ImGuiChildFlags_Border);
            ImGui::PushFont(F_Sm);
            for (auto& r : rs) {
                ImGui::TextColored(r.ok ? COL_OK : COL_ERR, "%s", r.ok ? "[OK]" : "[X]");
                ImGui::SameLine(0, 6);
                ImGui::TextColored(COL_TEXT, "%s", r.name.c_str());
                if (!r.detail.empty()) {
                    ImGui::SameLine(0, 6);
                    ImGui::TextColored(COL_DIM, "— %s", r.detail.c_str());
                }
            }
            if (g.spoofDone) {
                ImGui::TextColored(COL_GOLD, "Reinicie o PC para aplicar tudo.");
            }
            ImGui::PopFont();
            ImGui::EndChild();
        } else {
            ImGui::Dummy(ImVec2(0, -110 + 40));
        }
    }

    StatusArea();
    if (GhostBtn("DISCORD", ImGui::GetContentRegionAvail().x * 0.48f, 32))
        ShellExecuteW(nullptr, L"open", DISCORD_URL, nullptr, nullptr, SW_SHOW);
    ImGui::SameLine();
    if (GhostBtn("SAIR", -1, 32)) {
        ClearSession();
        g.accessToken.clear();
        g.screen = SCR_LOGIN;
    }
}

// ============================================================================
//  Win32
// ============================================================================
static void RenderFrame() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    if (g.screen == SCR_LOGIN) ScreenLogin(); else ScreenMain();
    ImGui::End();
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wp != SIZE_MINIMIZED) {
            CleanupRTV();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"C4pt0rSpoofer";
    RegisterClassExW(&wc);

    int winW = 520, winH = 780;
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"C4pt0r Spoofer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - winW) / 2, (GetSystemMetrics(SM_CYSCREEN) - winH) / 2,
        winW, winH, nullptr, nullptr, hInst, nullptr);

    if (!CreateDeviceD3D(g_hWnd)) {
        MessageBoxW(g_hWnd, L"Falha ao iniciar DirectX 11.", L"C4pt0r Spoofer", MB_ICONERROR);
        return 1;
    }
    CreateRTV();
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hWnd, 20, &dark, sizeof(dark));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImFontConfig fc;
    fc.SizePixels = 30.0f; F_Title = io.Fonts->AddFontDefault(&fc);
    fc.SizePixels = 20.0f; F_Big   = io.Fonts->AddFontDefault(&fc);
    fc.SizePixels = 16.0f; F_Med   = io.Fonts->AddFontDefault(&fc);
    fc.SizePixels = 13.0f; F_Sm    = io.Fonts->AddFontDefault(&fc);
    io.Fonts->Build();
    io.FontDefault = F_Med;
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dCtx);
    SetupStyle();

    // auto-login com a sessao compartilhada do loader
    {
        std::string rt = LoadRefresh();
        if (!rt.empty()) {
            SetStatus("Restaurando sessao...", ST_NEUTRO);
            std::thread([rt] {
                g.busy = true;
                if (DoRefresh(rt)) {
                    if (CheckLicense()) {
                        g.licensed = true;
                        g.screen = SCR_MAIN;
                        SetStatus("", ST_NEUTRO);
                    } else {
                        g.screen = SCR_MAIN;
                        SetStatus("Sem licenca do C4pt0r Spoofer.", ST_GOLD);
                    }
                } else SetStatus("", ST_NEUTRO);
                g.busy = false;
            }).detach();
        }
    }

    ImVec4 clearCol = ImVec4(0.040f, 0.042f, 0.082f, 1.00f);
    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RenderFrame();
        ImGui::Render();
        const float cc[4] = { clearCol.x, clearCol.y, clearCol.z, clearCol.w };
        g_pd3dCtx->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_pd3dCtx->ClearRenderTargetView(g_mainRTV, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupRTV();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dCtx) g_pd3dCtx->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
    DestroyWindow(g_hWnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
