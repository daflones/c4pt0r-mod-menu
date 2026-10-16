// ============================================================================
//  C4pt0r Loader — injetor com login + licenca (Supabase)
//  Login -> license-check -> baixa DLL assinada -> injeta no FiveM
//  Sem licenca: compra via PIX dentro do proprio loader.
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <Shellapi.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <filesystem>
#include "net.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

// ---- Config ----------------------------------------------------------------
static const char* SUPA_HOST = "bzthdklqvjtutkeiimpq.supabase.co";
static const char* ANON_KEY  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJ6dGhka2xxdmp0dXRrZWlpbXBxIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTIxNzU2MDYsImV4cCI6MjEwNzc1MTYwNn0.K1_us_hjL10yynZRDsrWZKwId-l5NuzrDSuHeFXtIdI";
static const char* PRODUCT   = "c4pt0r-mod-menu";
static const wchar_t* DISCORD_URL = L"https://discord.gg/PhkbhxMBy";
static const wchar_t* STORE_URL   = L"https://c4pt0r-store.eletrify.me/cadastro";

// ---- Colors ----------------------------------------------------------------
constexpr COLORREF CLR_BG      = RGB(13, 13, 22);
constexpr COLORREF CLR_PANEL   = RGB(20, 20, 42);
constexpr COLORREF CLR_ACCENT  = RGB(0, 212, 255);
constexpr COLORREF CLR_TEXT    = RGB(220, 225, 235);
constexpr COLORREF CLR_DIM     = RGB(100, 105, 115);
constexpr COLORREF CLR_OK      = RGB(46, 230, 109);
constexpr COLORREF CLR_ERR     = RGB(255, 77, 106);
constexpr COLORREF CLR_GOLD    = RGB(255, 184, 64);
constexpr COLORREF CLR_BTN     = RGB(0, 140, 180);
constexpr COLORREF CLR_BTN_HOT = RGB(0, 180, 220);
constexpr COLORREF CLR_EDIT_BG = RGB(14, 14, 32);

// ---- App state -------------------------------------------------------------
enum Screen { SCR_LOGIN, SCR_MAIN, SCR_PAY };

struct Shared {
    std::atomic<int>     screen{SCR_LOGIN};
    std::atomic<bool>    busy{false};
    std::atomic<bool>    licensed{false};
    std::atomic<bool>    paid{false};
    std::atomic<bool>    injected{false};
    std::string          accessToken, refreshToken, email;
    std::wstring         licLabel = L"Verificando licenca...";
    std::string          downloadUrl;
    std::string          orderId, qrEmv;
    std::wstring         lastErr;
} g;

// ---- Controls --------------------------------------------------------------
static HWND g_hWnd, g_hEmail, g_hPass, g_hStatus, g_hLog, g_hQrBox, g_hEmv;
static HWND g_hLblEmail, g_hLblPass;
static HWND g_hBtnLogin, g_hBtnDiscord, g_hBtnSignup, g_hBtnMain, g_hBtnCopy,
            g_hBtnLogout, g_hBtnBack;
static HBRUSH g_BgBrush, g_PanelBrush, g_EditBrush;
static HFONT  g_Font, g_FontBold, g_FontTitle, g_FontBig;
static HBITMAP g_QrBitmap = nullptr;

enum {
    ID_EMAIL = 201, ID_PASS, ID_BTN_LOGIN, ID_BTN_DISCORD, ID_BTN_SIGNUP,
    ID_BTN_MAIN, ID_BTN_COPY, ID_BTN_LOGOUT, ID_BTN_BACK, ID_QRBOX, ID_EMV,
    ID_STATUS, ID_LOG, ID_TIMER_POLL = 900,
};

// ---- Log helper -------------------------------------------------------------
static void Log(const wchar_t* msg) {
    int len = GetWindowTextLengthW(g_hLog);
    std::wstring cur(len + 1, L'\0');
    GetWindowTextW(g_hLog, cur.data(), len + 1);
    cur.resize(len);
    if (!cur.empty()) cur += L"\r\n";
    cur += msg;
    SetWindowTextW(g_hLog, cur.c_str());
}
static void Log(const std::string& s) { Log(ToWide(s).c_str()); }

// ---- Session persistence (DPAPI) -------------------------------------------
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
    std::vector<BYTE> buf(sz);
    DWORD r; ReadFile(f, buf.data(), sz, &r, nullptr); CloseHandle(f);
    DATA_BLOB in{ sz, buf.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return "";
    std::string t((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return t;
}
static void ClearSession() { DeleteFileW(SessPath().c_str()); }

// ---- HWID -------------------------------------------------------------------
static std::string GetHwid() {
    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD clen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(comp, &clen);
    char buf[64];
    snprintf(buf, 64, "%08X-%08X", serial, (unsigned)(serial ^ clen ^ comp[0]));
    return buf;
}

// ---- Supabase helpers -------------------------------------------------------
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

static bool DoPasswordGrant(const std::string& email, const std::string& pass, std::wstring& err) {
    std::string body = "{\"email\":\"" + email + "\",\"password\":\"" + pass + "\"}";
    auto r = Http(SUPA_HOST, "POST", "/auth/v1/token?grant_type=password", ApiHdrs(), body);
    auto j = JsonParse(r.body);
    if (r.status == 200 && !j["access_token"].str().empty()) {
        g.accessToken = j["access_token"].str();
        g.refreshToken = j["refresh_token"].str();
        SaveRefresh(g.refreshToken);
        return true;
    }
    err = ToWide(j["error_description"].str().empty() ? j["msg"].str() : j["error_description"].str());
    if (err.empty()) err = L"Email ou senha incorretos.";
    return false;
}

static bool DoRefresh(const std::string& rt) {
    std::string body = "{\"refresh_token\":\"" + rt + "\"}";
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

static bool CheckLicense(std::wstring& label) {
    std::string body = "{\"action\":\"check\",\"product_slug\":\"" + std::string(PRODUCT) +
        "\",\"hwid\":\"" + GetHwid() + "\"}";
    auto r = FnCall("license-check", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["licensed"].truthy()) {
        g.downloadUrl = j["download_url"].str();
        auto lic = j["license"];
        std::string exp = lic["expires_at"].str();
        label = exp.empty()
            ? L"Licenca: VITALICIA"
            : L"Licenca: ATIVA ate " + ToWide(exp.substr(0, 10));
        return true;
    }
    auto e = j["error"].str();
    label = e.empty() ? L"Sem licenca ativa" : ToWide(e);
    return false;
}

// ---- FiveM process + inject (mesmo codigo de antes) -------------------------
static DWORD FindFiveMPid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring n = pe.szExeFile;
            if (n.find(L"GTAProcess") != std::wstring::npos) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool InjectDLL(DWORD pid, const std::wstring& dllPath) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!h) return false;
    size_t sz = (dllPath.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { CloseHandle(h); return false; }
    WriteProcessMemory(h, mem, dllPath.c_str(), sz, nullptr);
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE t = CreateRemoteThread(h, nullptr, 0, load, mem, 0, nullptr);
    if (!t) { VirtualFreeEx(h, mem, 0, MEM_RELEASE); CloseHandle(h); return false; }
    WaitForSingleObject(t, 10000);
    DWORD code = 0; GetExitCodeThread(t, &code);
    CloseHandle(t); VirtualFreeEx(h, mem, 0, MEM_RELEASE); CloseHandle(h);
    return code != 0;
}

// ---- WIC: PNG bytes -> HBITMAP ----------------------------------------------
static HBITMAP PngToBitmap(const std::string& bytes) {
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, (void**)&fac))) return nullptr;
    IStream* stm = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size());
    IWICBitmapDecoder* dec = nullptr;
    HBITMAP bmp = nullptr;
    if (stm && SUCCEEDED(fac->CreateDecoderFromStream(stm, nullptr, WICDecodeMetadataCacheOnLoad, &dec))) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(dec->GetFrame(0, &frame))) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                UINT w = 0, hgt = 0; conv->GetSize(&w, &hgt);
                BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
                bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -(int)hgt;
                bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
                void* bits = nullptr;
                bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (bmp && bits) conv->CopyPixels(nullptr, w * 4, w * hgt * 4, (BYTE*)bits);
            }
            if (conv) conv->Release();
            frame->Release();
        }
        dec->Release();
    }
    if (stm) stm->Release();
    fac->Release();
    return bmp;
}

// ---- UI visibility per screen -----------------------------------------------
static void ApplyScreen() {
    int s = g.screen.load();
    auto sw = [&](HWND h, bool v) { ShowWindow(h, v ? SW_SHOW : SW_HIDE); };
    auto mv = [&](HWND h, int x, int y, int w, int ht) { MoveWindow(h, x, y, w, ht, TRUE); };
    bool login = s == SCR_LOGIN, main = s == SCR_MAIN, pay = s == SCR_PAY;

    // shared
    mv(g_hStatus, 40, 92, 380, 22);
    mv(g_hLog, 40, 400, 380, 66);
    mv(g_hBtnDiscord, 40, 478, 380, 30);
    sw(g_hStatus, true); sw(g_hLog, !pay); sw(g_hBtnDiscord, true);

    // login
    sw(g_hLblEmail, login); sw(g_hEmail, login);
    sw(g_hLblPass, login); sw(g_hPass, login);
    sw(g_hBtnLogin, login); sw(g_hBtnSignup, login);

    // main
    sw(g_hBtnMain, main || pay); sw(g_hBtnLogout, main);
    if (main) { mv(g_hBtnMain, 40, 130, 380, 46); mv(g_hBtnLogout, 40, 188, 380, 32); }

    // pay
    sw(g_hQrBox, pay); sw(g_hEmv, pay); sw(g_hBtnCopy, pay); sw(g_hBtnBack, pay);
    if (pay) {
        mv(g_hQrBox, 125, 98, 210, 210);
        mv(g_hEmv, 40, 316, 292, 30);
        mv(g_hBtnCopy, 340, 316, 80, 30);
        mv(g_hBtnMain, 40, 356, 380, 36);
        mv(g_hBtnBack, 40, 426, 380, 28);
    }

    SetWindowTextW(g_hBtnMain, pay ? L"VERIFICAR PAGAMENTO" :
        (g.licensed ? L"INJETAR" : L"COMPRAR AGORA"));
    InvalidateRect(g_hWnd, nullptr, TRUE);
}

// ---- Worker threads ----------------------------------------------------------
static void ThreadLogin(const std::string& email, const std::string& pass) {
    g.busy = true;
    std::wstring err;
    if (DoPasswordGrant(email, pass, err)) {
        g.email = email;
        std::wstring label;
        if (CheckLicense(label)) { g.licensed = true; g.licLabel = label; }
        else g.licLabel = label;
        g.screen = SCR_MAIN;
        PostMessageW(g_hWnd, WM_APP + 1, 0, 0);
    } else {
        g.lastErr = err;
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

static void ThreadCheckLicense() {
    g.busy = true;
    std::wstring label;
    if (CheckLicense(label)) { g.licensed = true; g.licLabel = label; }
    else { g.licensed = false; g.licLabel = label; }
    PostMessageW(g_hWnd, WM_APP + 3, 0, 0);
    g.busy = false;
}

static void ThreadBuyPix() {
    g.busy = true;
    std::string body = "{\"product_slug\":\"" + std::string(PRODUCT) + "\",\"method\":\"pix\"}";
    auto r = FnCall("checkout", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["ok"].truthy() && !j["qr_code"].str().empty()) {
        g.orderId = j["order_id"].str();
        g.qrEmv = j["qr_code"].str();
        g.screen = SCR_PAY;
        g.paid = false;
        PostMessageW(g_hWnd, WM_APP + 4, 0, 0);
    } else {
        g.lastErr = ToWide(j["error"].str().empty() ? "Falha ao gerar PIX." : j["error"].str());
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

static void ThreadPollOrder() {
    while (g.screen == SCR_PAY && !g.paid) {
        std::string body = "{\"order_id\":\"" + g.orderId + "\"}";
        auto r = FnCall("order-status", body, g.accessToken);
        auto j = JsonParse(r.body);
        if (j["status"].str() == "paid") {
            g.paid = true;
            std::wstring label;
            if (CheckLicense(label)) { g.licensed = true; g.licLabel = label; }
            PostMessageW(g_hWnd, WM_APP + 5, 0, 0);
            return;
        }
        if (j["status"].str() == "failed" || j["status"].str() == "expired") {
            g.lastErr = L"Pagamento expirado ou falhou.";
            PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
            return;
        }
        Sleep(4000);
    }
}

static void ThreadDownloadInject() {
    g.busy = true;
    if (g.downloadUrl.empty()) {
        std::wstring label;
        if (!CheckLicense(label) || g.downloadUrl.empty()) {
            g.lastErr = L"Nao consegui obter o download.";
            PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
            g.busy = false; return;
        }
    }
    auto r = HttpDownload(g.downloadUrl);
    if (r.status != 200 || r.body.size() < 10000) {
        g.lastErr = L"Download da DLL falhou.";
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
        g.busy = false; return;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dllPath = std::wstring(tmp) + L"c4rt.dll";
    HANDLE f = CreateFileW(dllPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(f, r.body.data(), (DWORD)r.body.size(), &w, nullptr);
    CloseHandle(f);

    DWORD pid = FindFiveMPid();
    if (!pid) {
        g.lastErr = L"FiveM nao encontrado. Entre num servidor primeiro.";
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
        g.busy = false; return;
    }
    if (InjectDLL(pid, dllPath)) {
        g.injected = true;
        PostMessageW(g_hWnd, WM_APP + 6, 0, 0);
    } else {
        g.lastErr = L"Injecao falhou. Rode como administrador.";
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

// ---- Owner-drawn button ------------------------------------------------------
static void DrawButton(LPDRAWITEMSTRUCT dis) {
    bool hot = (dis->itemState & ODS_SELECTED) || (dis->itemState & ODS_HOTLIGHT);
    COLORREF bg = hot ? CLR_BTN_HOT : CLR_BTN;
    if (dis->CtlID == ID_BTN_DISCORD || dis->CtlID == ID_BTN_SIGNUP ||
        dis->CtlID == ID_BTN_BACK || dis->CtlID == ID_BTN_LOGOUT || dis->CtlID == ID_BTN_COPY)
        bg = hot ? RGB(45, 45, 80) : RGB(35, 35, 65);

    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_ACCENT);
    SelectObject(dis->hDC, brush); SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
        dis->rcItem.right, dis->rcItem.bottom, 8, 8);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, dis->CtlID == ID_BTN_MAIN || dis->CtlID == ID_BTN_LOGIN ? RGB(4, 18, 26) : CLR_TEXT);
    SelectObject(dis->hDC, g_FontBold);
    wchar_t text[128]; GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(brush); DeleteObject(pen);
}

static HWND MkBtn(const wchar_t* t, int x, int y, int w, int h, int id) {
    return CreateWindowW(L"BUTTON", t, WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, g_hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
}
static HWND MkEdit(int x, int y, int w, int id, bool pass = false) {
    return CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL | WS_BORDER | (pass ? ES_PASSWORD : 0),
        x, y, w, 32, g_hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
}
static HWND MkStatic(const wchar_t* t, int x, int y, int w, int h, int id = 0) {
    return CreateWindowW(L"STATIC", t, WS_CHILD | SS_LEFT,
        x, y, w, h, g_hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
}

// ---- WM_CREATE: build all controls hidden, ApplyScreen manages ---------------
static void BuildUI(HWND hWnd) {
    int cx = 40, w = 380;

    // LOGIN
    g_hLblEmail = MkStatic(L"Email", cx, 100, w, 18);
    g_hEmail = MkEdit(cx, 118, w, ID_EMAIL);
    g_hLblPass = MkStatic(L"Senha", cx, 158, w, 18);
    g_hPass = MkEdit(cx, 176, w, ID_PASS, true);
    g_hBtnLogin = MkBtn(L"ENTRAR", cx, 222, w, 42, ID_BTN_LOGIN);
    g_hBtnSignup = MkBtn(L"CRIAR CONTA", cx, 272, w, 32, ID_BTN_SIGNUP);

    // MAIN / shared
    g_hStatus = MkStatic(L"", cx, 92, w, 22, ID_STATUS);
    g_hBtnMain = MkBtn(L"INJETAR", cx, 130, w, 46, ID_BTN_MAIN);
    g_hBtnLogout = MkBtn(L"SAIR", cx, 188, w, 32, ID_BTN_LOGOUT);

    // PAY
    g_hQrBox = MkStatic(L"", 125, 98, 210, 210, ID_QRBOX);
    g_hEmv = MkEdit(cx, 316, w - 88, ID_EMV);
    SendMessageW(g_hEmv, EM_SETREADONLY, TRUE, 0);
    g_hBtnCopy = MkBtn(L"COPIAR", 340, 316, 80, 30, ID_BTN_COPY);
    g_hBtnBack = MkBtn(L"VOLTAR", cx, 426, w, 28, ID_BTN_BACK);

    // shared
    g_hBtnDiscord = MkBtn(L"DISCORD - SUPORTE", cx, 478, w, 30, ID_BTN_DISCORD);
    g_hLog = MkStatic(L"", cx, 400, w, 66, ID_LOG);

    HWND all[] = { g_hEmail, g_hPass, g_hStatus, g_hLog, g_hEmv, g_hLblEmail, g_hLblPass };
    for (auto c : all) SendMessageW(c, WM_SETFONT, (WPARAM)g_Font, TRUE);
    HWND btns[] = { g_hBtnLogin, g_hBtnDiscord, g_hBtnSignup, g_hBtnMain, g_hBtnCopy, g_hBtnLogout, g_hBtnBack };
    for (auto b : btns) SendMessageW(b, WM_SETFONT, (WPARAM)g_FontBold, TRUE);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));
    ApplyScreen();
}

// ---- WndProc -----------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE:
        g_hWnd = hWnd; // precisa antes de criar os filhos (CreateWindowExW ainda nao retornou)
        g_Font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontTitle = CreateFontW(-30, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontBig = CreateFontW(-17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_BgBrush = CreateSolidBrush(CLR_BG);
        g_PanelBrush = CreateSolidBrush(CLR_PANEL);
        g_EditBrush = CreateSolidBrush(CLR_EDIT_BG);
        BuildUI(hWnd);

        // Auto-login via refresh token salvo
        {
            std::string rt = LoadRefresh();
            if (!rt.empty()) {
                Log(L"[..] Sessao salva encontrada, entrando...");
                std::thread([rt, hWnd] {
                    g.busy = true;
                    if (DoRefresh(rt)) {
                        std::wstring label;
                        if (CheckLicense(label)) { g.licensed = true; g.licLabel = label; }
                        else g.licLabel = label;
                        g.screen = SCR_MAIN;
                        PostMessageW(hWnd, WM_APP + 1, 0, 0);
                    }
                    g.busy = false;
                }).detach();
            }
        }
        return 0;

    case WM_APP + 1: // login ok -> main
        ApplyScreen();
        SetWindowTextW(g_hStatus, g.licLabel.c_str());
        Log(g.licensed ? L"[OK] Licenca ativa." : L"[..] Sem licenca — compre para liberar.");
        return 0;

    case WM_APP + 2: // erro
        SetWindowTextW(g_hStatus, g.lastErr.c_str());
        Log((std::wstring(L"[ERRO] ") + g.lastErr).c_str());
        if (g.screen == SCR_PAY) { g.screen = SCR_MAIN; ApplyScreen(); }
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_APP + 3: // re-check done
        ApplyScreen();
        SetWindowTextW(g_hStatus, g.licLabel.c_str());
        return 0;

    case WM_APP + 4: // pix gerado -> tela de pagamento
    {
        ApplyScreen();
        SetWindowTextW(g_hStatus, L"Escaneie o QR ou use copia-e-cola");
        SetWindowTextW(g_hEmv, ToWide(g.qrEmv).c_str());
        Log(L"[OK] PIX gerado. Aguardando pagamento...");
        // QR via servico publico -> PNG -> WIC
        std::thread([] {
            std::string url = "https://api.qrserver.com/v1/create-qr-code/?size=220x220&data=";
            // urlencode do EMV
            std::string enc;
            for (char c : g.qrEmv) {
                if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') enc += c;
                else { char b[4]; snprintf(b, 4, "%%%02X", (unsigned char)c); enc += b; }
            }
            auto r = HttpDownload(url + enc);
            if (r.status == 200 && r.body.size() > 100) {
                HBITMAP b = PngToBitmap(r.body);
                if (b) {
                    if (g_QrBitmap) DeleteObject(g_QrBitmap);
                    g_QrBitmap = b;
                    PostMessageW(g_hWnd, WM_APP + 7, 0, 0);
                }
            }
        }).detach();
        // poll do pagamento
        std::thread(ThreadPollOrder).detach();
        return 0;
    }

    case WM_APP + 5: // pago!
        SetWindowTextW(g_hStatus, L"PAGO! Licenca liberada.");
        Log(L"[OK] Pagamento confirmado. Pode injetar!");
        g.screen = SCR_MAIN;
        ApplyScreen();
        SetWindowTextW(g_hStatus, g.licLabel.c_str());
        return 0;

    case WM_APP + 6: // injetado
        SetWindowTextW(g_hStatus, L"Injetado! Aperte F9 no jogo.");
        Log(L"[OK] DLL injetada com sucesso. F9 abre o menu.");
        return 0;

    case WM_APP + 7: // QR pronto
        InvalidateRect(g_hQrBox, nullptr, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        FillRect(hdc, &ps.rcPaint, g_BgBrush);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_ACCENT);
        SelectObject(hdc, g_FontTitle);
        TextOutW(hdc, 28, 20, L"C4PT0R", 6);
        SetTextColor(hdc, CLR_DIM);
        SelectObject(hdc, g_Font);
        int s = g.screen.load();
        const wchar_t* sub = s == SCR_LOGIN ? L"Entre com sua conta" :
            s == SCR_PAY ? L"Pagamento via PIX" : L"Loader v2.0";
        TextOutW(hdc, 30, 56, sub, (int)wcslen(sub));
        HPEN pen = CreatePen(PS_SOLID, 2, CLR_ACCENT);
        SelectObject(hdc, pen);
        MoveToEx(hdc, 30, 80, nullptr); LineTo(hdc, 160, 80);
        DeleteObject(pen);

        // QR bitmap
        if (s == SCR_PAY && g_QrBitmap) {
            HDC mem = CreateCompatibleDC(hdc);
            SelectObject(mem, g_QrBitmap);
            RECT rc; GetClientRect(g_hQrBox, &rc);
            POINT pt{ rc.left, rc.top };
            ClientToScreen(g_hQrBox, &pt); ScreenToClient(hWnd, &pt);
            BitBlt(hdc, pt.x, pt.y, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
            DeleteDC(mem);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        HWND c = (HWND)lp;
        if (c == g_hStatus) {
            SetTextColor(hdc, g.licensed || g.injected ? CLR_OK : CLR_GOLD);
            SelectObject(hdc, g_FontBig);
        } else SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_BgBrush;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, CLR_EDIT_BG); SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_EditBrush;
    }
    case WM_DRAWITEM:
        DrawButton((LPDRAWITEMSTRUCT)lp);
        return TRUE;

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        if (g.busy) return 0;

        if (id == ID_BTN_SIGNUP) ShellExecuteW(nullptr, L"open", STORE_URL, nullptr, nullptr, SW_SHOW);
        if (id == ID_BTN_DISCORD) ShellExecuteW(nullptr, L"open", DISCORD_URL, nullptr, nullptr, SW_SHOW);

        if (id == ID_BTN_LOGIN) {
            wchar_t e[256], p[256];
            GetWindowTextW(g_hEmail, e, 256); GetWindowTextW(g_hPass, p, 256);
            auto narrow = [](const wchar_t* w) {
                char buf[512]; WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, 512, nullptr, nullptr);
                return std::string(buf);
            };
            if (!*e || !*p) { SetWindowTextW(g_hStatus, L"Preencha email e senha."); return 0; }
            SetWindowTextW(g_hStatus, L"Entrando...");
            Log(L"[..] Autenticando...");
            std::thread(ThreadLogin, narrow(e), narrow(p)).detach();
        }

        if (id == ID_BTN_LOGOUT) {
            ClearSession();
            g.accessToken.clear(); g.licensed = false;
            g.screen = SCR_LOGIN;
            ApplyScreen();
            Log(L"[..] Desconectado.");
        }

        if (id == ID_BTN_BACK) {
            g.screen = SCR_MAIN;
            ApplyScreen();
        }

        if (id == ID_BTN_COPY) {
            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                std::wstring w = ToWide(g.qrEmv);
                size_t sz = (w.size() + 1) * sizeof(wchar_t);
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sz);
                memcpy(GlobalLock(h), w.c_str(), sz); GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
                CloseClipboard();
                Log(L"[OK] Codigo PIX copiado.");
            }
        }

        if (id == ID_BTN_MAIN) {
            if (g.screen == SCR_PAY) {
                // re-check manual
                std::thread(ThreadPollOrder).detach();
                Log(L"[..] Verificando pagamento...");
            } else if (g.licensed) {
                DWORD pid = FindFiveMPid();
                if (!pid) {
                    SetWindowTextW(g_hStatus, L"Abra o FiveM e entre num servidor.");
                    Log(L"[ERRO] FiveM_GTAProcess nao encontrado.");
                } else {
                    SetWindowTextW(g_hStatus, L"Baixando e injetando...");
                    Log(L"[..] Baixando DLL assinada...");
                    std::thread(ThreadDownloadInject).detach();
                }
            } else {
                SetWindowTextW(g_hStatus, L"Gerando PIX...");
                std::thread(ThreadBuyPix).detach();
            }
        }
        return 0;
    }

    case WM_DESTROY:
        if (g_QrBitmap) DeleteObject(g_QrBitmap);
        DeleteObject(g_BgBrush); DeleteObject(g_PanelBrush); DeleteObject(g_EditBrush);
        DeleteObject(g_Font); DeleteObject(g_FontBold); DeleteObject(g_FontTitle); DeleteObject(g_FontBig);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ---- Entry -------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"C4pt0rLoader";
    RegisterClassExW(&wc);

    int winW = 460, winH = 560;
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"C4pt0r Loader",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - winW) / 2, (GetSystemMetrics(SM_CYSCREEN) - winH) / 2,
        winW, winH, nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    CoUninitialize();
    return (int)m.wParam;
}
