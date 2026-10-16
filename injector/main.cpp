// ============================================================================
//  C4pt0r Loader — login + licenca (Supabase) + injecao no FiveM
//  Login/Cadastro -> license-check -> baixa DLL assinada -> injeta.
//  Sem licenca: compra via PIX dentro do proprio loader (QR + copia-e-cola).
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
#include "net.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")

// ---- Config ----------------------------------------------------------------
static const char* SUPA_HOST = "bzthdklqvjtutkeiimpq.supabase.co";
static const char* ANON_KEY  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJ6dGhka2xxdmp0dXRrZWlpbXBxIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTIxNzU2MDYsImV4cCI6MjEwNzc1MTYwNn0.K1_us_hjL10yynZRDsrWZKwId-l5NuzrDSuHeFXtIdI";
static const char* PRODUCT   = "c4pt0r-mod-menu";
static const wchar_t* DISCORD_URL = L"https://discord.gg/PhkbhxMBy";

// ---- Colors ----------------------------------------------------------------
constexpr COLORREF CLR_BG      = RGB(10, 11, 20);
constexpr COLORREF CLR_CARD    = RGB(19, 21, 38);
constexpr COLORREF CLR_CARD2   = RGB(24, 27, 48);
constexpr COLORREF CLR_BORDER  = RGB(42, 48, 82);
constexpr COLORREF CLR_ACCENT  = RGB(0, 212, 255);
constexpr COLORREF CLR_ACCENT2 = RGB(0, 150, 200);
constexpr COLORREF CLR_TEXT    = RGB(228, 232, 244);
constexpr COLORREF CLR_DIM     = RGB(120, 126, 148);
constexpr COLORREF CLR_OK      = RGB(46, 230, 109);
constexpr COLORREF CLR_ERR     = RGB(255, 77, 106);
constexpr COLORREF CLR_GOLD    = RGB(255, 184, 64);
constexpr COLORREF CLR_EDIT_BG = RGB(12, 13, 26);
constexpr COLORREF CLR_GHOST   = RGB(28, 31, 56);
constexpr COLORREF CLR_GHOST_H = RGB(40, 45, 78);

// ---- App state -------------------------------------------------------------
enum Screen { SCR_LOGIN, SCR_SIGNUP, SCR_MAIN, SCR_PAY };
enum StatKind { ST_NEUTRO, ST_OK, ST_ERR, ST_GOLD };

struct Shared {
    std::atomic<int>     screen{SCR_LOGIN};
    std::atomic<bool>    busy{false};
    std::atomic<bool>    licensed{false};
    std::atomic<bool>    paid{false};
    std::atomic<bool>    injected{false};
    std::atomic<int>     statKind{ST_NEUTRO};
    std::string          accessToken, refreshToken, email;
    std::wstring         planName;        // nome do produto licenciado
    std::wstring         validity;        // "Vitalicia" / "ate dd/mm/aaaa (N dias)"
    std::wstring         statusMsg;
    std::string          downloadUrl;
    std::string          orderId, qrEmv, qrB64;
} g;

// ---- Controls --------------------------------------------------------------
static HWND g_hWnd;
// login
static HWND g_hEmail, g_hPass, g_hLblEmail, g_hLblPass, g_hBtnLogin, g_hBtnToSignup;
// signup
static HWND g_hSEmail, g_hSPass, g_hSPass2, g_hLblSEmail, g_hLblSPass, g_hLblSPass2,
            g_hBtnDoSignup, g_hBtnToLogin;
// main
static HWND g_hBtnMain, g_hBtnRefresh, g_hBtnLogout;
// pay
static HWND g_hQrBox, g_hEmv, g_hBtnCopy, g_hBtnCheck, g_hBtnBack;
// shared
static HWND g_hStatus, g_hLog, g_hBtnDiscord;
static HBRUSH g_BgBrush, g_CardBrush, g_EditBrush, g_WhiteBrush;
static HFONT  g_Font, g_FontSm, g_FontBold, g_FontTitle, g_FontBig, g_FontCard;
static HBITMAP g_QrBitmap = nullptr;

enum {
    ID_EMAIL = 201, ID_PASS, ID_SEMAIL, ID_SPASS, ID_SPASS2,
    ID_BTN_LOGIN, ID_BTN_TOSIGNUP, ID_BTN_DOSIGNUP, ID_BTN_TOLOGIN,
    ID_BTN_MAIN, ID_BTN_REFRESH, ID_BTN_LOGOUT,
    ID_BTN_CHECK, ID_BTN_COPY, ID_BTN_BACK, ID_BTN_DISCORD,
    ID_QRBOX, ID_EMV, ID_STATUS, ID_LOG,
};

// ---- Helpers ----------------------------------------------------------------
static void SetStatus(const wchar_t* msg, StatKind k = ST_NEUTRO) {
    g.statusMsg = msg;
    g.statKind = k;
    if (g_hStatus) SetWindowTextW(g_hStatus, msg);
    InvalidateRect(g_hWnd, nullptr, FALSE);
}

static void Log(const wchar_t* msg) {
    if (!g_hLog) return;
    int len = GetWindowTextLengthW(g_hLog);
    std::wstring cur(len + 1, L'\0');
    GetWindowTextW(g_hLog, cur.data(), len + 1);
    cur.resize(len);
    // mantem so as ultimas 2 linhas
    size_t cut = cur.rfind(L"\r\n", cur.size() > 1 ? cur.size() - 1 : 0);
    if (cut != std::wstring::npos) cur = cur.substr(cut + 2);
    if (!cur.empty()) cur += L"\r\n";
    cur += msg;
    SetWindowTextW(g_hLog, cur.c_str());
}

static std::string Narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}

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
    if (!sz || sz > 4096) { CloseHandle(f); return ""; }
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
    unsigned h = 2166136261u;
    for (auto c : comp) { h ^= (unsigned)c; h *= 16777619u; }
    char buf[64];
    snprintf(buf, 64, "%08X-%08X", serial, h);
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
    std::string body = "{\"email\":\"" + JsonEsc(email) + "\",\"password\":\"" + JsonEsc(pass) + "\"}";
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

// retorna: 0 ok+logado | 1 criou, precisa confirmar email | -1 erro
static int DoSignup(const std::string& email, const std::string& pass, std::wstring& err) {
    std::string body = "{\"email\":\"" + JsonEsc(email) + "\",\"password\":\"" + JsonEsc(pass) + "\"}";
    auto r = Http(SUPA_HOST, "POST", "/auth/v1/signup", ApiHdrs(), body);
    auto j = JsonParse(r.body);
    if (!j["access_token"].str().empty()) {
        g.accessToken = j["access_token"].str();
        g.refreshToken = j["refresh_token"].str();
        SaveRefresh(g.refreshToken);
        return 0;
    }
    if (r.status == 200 || r.status == 201) return 1; // sem sessao = confirmar email
    err = ToWide(j["msg"].str().empty() ? (j["error_description"].str().empty() ?
        j["error"].str() : j["error_description"].str()) : j["msg"].str());
    if (err.empty()) err = L"Nao foi possivel criar a conta.";
    return -1;
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

// calcula "N dias" a partir de ISO date
static std::wstring FmtValidity(const std::string& iso) {
    if (iso.empty()) return L"Vitalicia";
    SYSTEMTIME st{};
    swscanf_s(ToWide(iso).c_str(), L"%4hu-%2hu-%2huT%2hu:%2hu:%2hu",
        &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond);
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER a{}; a.u.LowPart = ft.dwLowDateTime; a.u.HighPart = ft.dwHighDateTime;
    FILETIME nf; GetSystemTimeAsFileTime(&nf);
    ULARGE_INTEGER b{}; b.u.LowPart = nf.dwLowDateTime; b.u.HighPart = nf.dwHighDateTime;
    long long days = (long long)(a.QuadPart - b.QuadPart) / 864000000000LL;
    wchar_t buf[128];
    if (days < 0) wcscpy_s(buf, L"Expirada");
    else if (days == 0) wcscpy_s(buf, L"Expira hoje");
    else swprintf_s(buf, L"ate %02d/%02d/%d (%lld dias)", st.wDay, st.wMonth, st.wYear, days);
    return buf;
}

static bool CheckLicense(std::wstring& err) {
    std::string body = "{\"action\":\"check\",\"product_slug\":\"" + std::string(PRODUCT) +
        "\",\"hwid\":\"" + GetHwid() + "\"}";
    auto r = FnCall("license-check", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["licensed"].truthy()) {
        g.downloadUrl = j["download_url"].str();
        auto lic = j["license"];
        g.planName = ToWide(lic["product"].str());
        if (g.planName.empty()) g.planName = L"C4pt0r Mod Menu";
        g.validity = FmtValidity(lic["expires_at"].str());
        return true;
    }
    auto e = j["error"].str();
    err = e.empty() ? L"" : ToWide(e);
    return false;
}

// ---- FiveM process + inject -------------------------------------------------
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

static HBITMAP QrFromDataUrl(const std::string& dataUrl) {
    size_t p = dataUrl.find("base64,");
    if (p == std::string::npos) return nullptr;
    std::string b64 = dataUrl.substr(p + 7);
    DWORD need = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
        nullptr, &need, nullptr, nullptr) || !need) return nullptr;
    std::vector<BYTE> bin(need);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
        bin.data(), &need, nullptr, nullptr)) return nullptr;
    return PngToBitmap(std::string((char*)bin.data(), need));
}

// ---- Worker threads ----------------------------------------------------------
static void AfterAuth(HWND hWnd) {
    std::wstring err;
    if (CheckLicense(err)) g.licensed = true;
    else { g.licensed = false; if (!err.empty()) g.statusMsg = err; }
    g.screen = SCR_MAIN;
    PostMessageW(hWnd, WM_APP + 1, 0, 0);
}

static void ThreadLogin(HWND hWnd, std::string email, std::string pass) {
    g.busy = true;
    std::wstring err;
    if (DoPasswordGrant(email, pass, err)) {
        g.email = email;
        AfterAuth(hWnd);
    } else {
        g.statusMsg = err;
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

static void ThreadSignup(HWND hWnd, std::string email, std::string pass) {
    g.busy = true;
    std::wstring err;
    int rc = DoSignup(email, pass, err);
    if (rc == 0) {
        g.email = email;
        AfterAuth(hWnd);
    } else if (rc == 1) {
        g.statusMsg = L"Conta criada! Confirme o email e entre.";
        PostMessageW(hWnd, WM_APP + 8, 0, 0);
    } else {
        g.statusMsg = err;
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

static void ThreadBuyPix(HWND hWnd) {
    g.busy = true;
    std::string body = "{\"product_slug\":\"" + std::string(PRODUCT) + "\",\"method\":\"pix\"}";
    auto r = FnCall("checkout", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["ok"].truthy() && !j["qr_code"].str().empty()) {
        g.orderId = j["order_id"].str();
        g.qrEmv = j["qr_code"].str();
        g.qrB64 = j["qr_image"].str();
        g.screen = SCR_PAY;
        g.paid = false;
        PostMessageW(hWnd, WM_APP + 4, 0, 0);
    } else {
        g.statusMsg = ToWide(j["error"].str().empty() ? "Falha ao gerar PIX." : j["error"].str());
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

static void ThreadPollOrder(HWND hWnd) {
    for (int i = 0; i < 120 && g.screen == SCR_PAY && !g.paid; i++) {
        std::string body = "{\"order_id\":\"" + g.orderId + "\"}";
        auto r = FnCall("order-status", body, g.accessToken);
        auto j = JsonParse(r.body);
        auto st = j["status"].str();
        if (st == "paid") {
            g.paid = true;
            std::wstring err;
            if (CheckLicense(err)) g.licensed = true;
            PostMessageW(hWnd, WM_APP + 5, 0, 0);
            return;
        }
        if (st == "failed" || st == "expired") {
            g.statusMsg = L"Pagamento expirado ou falhou.";
            PostMessageW(hWnd, WM_APP + 2, 0, 0);
            return;
        }
        Sleep(4000);
    }
}

static void ThreadDownloadInject(HWND hWnd) {
    g.busy = true;
    if (g.downloadUrl.empty()) {
        std::wstring err;
        if (!CheckLicense(err) || g.downloadUrl.empty()) {
            g.statusMsg = L"Nao consegui obter o download.";
            PostMessageW(hWnd, WM_APP + 2, 0, 0);
            g.busy = false; return;
        }
    }
    auto r = HttpDownload(g.downloadUrl);
    if (r.status != 200 || r.body.size() < 10000) {
        g.statusMsg = L"Download da DLL falhou.";
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
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
        g.statusMsg = L"FiveM nao encontrado. Entre num servidor.";
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
        g.busy = false; return;
    }
    if (InjectDLL(pid, dllPath)) {
        g.injected = true;
        PostMessageW(hWnd, WM_APP + 6, 0, 0);
    } else {
        g.statusMsg = L"Injecao falhou. Rode como administrador.";
        PostMessageW(hWnd, WM_APP + 2, 0, 0);
    }
    g.busy = false;
}

// ---- Painting helpers --------------------------------------------------------
static void PaintCard(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    SelectObject(hdc, b); SelectObject(hdc, p);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    DeleteObject(b); DeleteObject(p);
}

static void PaintPill(HDC hdc, const RECT& rc, COLORREF fill, const wchar_t* txt, HFONT f) {
    HBRUSH b = CreateSolidBrush(fill);
    SelectObject(hdc, b);
    SelectObject(hdc, GetStockObject(NULL_PEN));
    int d = rc.bottom - rc.top;
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, d, d);
    DeleteObject(b);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(6, 14, 10));
    SelectObject(hdc, f);
    RECT t = rc;
    DrawTextW(hdc, txt, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---- Owner-drawn button ------------------------------------------------------
static void DrawButton(LPDRAWITEMSTRUCT dis) {
    bool hot = (dis->itemState & ODS_HOTLIGHT) || (dis->itemState & ODS_SELECTED);
    bool primary = dis->CtlID == ID_BTN_LOGIN || dis->CtlID == ID_BTN_DOSIGNUP ||
        dis->CtlID == ID_BTN_MAIN || dis->CtlID == ID_BTN_CHECK;
    bool disabled = !IsWindowEnabled(dis->hwndItem);

    RECT rc = dis->rcItem;
    COLORREF top, bot, border, txt;
    if (disabled) {
        top = RGB(30, 32, 50); bot = RGB(26, 28, 44); border = CLR_BORDER; txt = CLR_DIM;
    } else if (primary) {
        top = hot ? RGB(20, 225, 255) : RGB(0, 212, 255);
        bot = hot ? RGB(0, 175, 215) : RGB(0, 150, 200);
        border = RGB(0, 240, 255); txt = RGB(3, 22, 30);
    } else {
        top = hot ? CLR_GHOST_H : CLR_GHOST;
        bot = hot ? RGB(34, 38, 66) : RGB(24, 27, 48);
        border = hot ? CLR_ACCENT2 : CLR_BORDER;
        txt = dis->CtlID == ID_BTN_DISCORD ? CLR_ACCENT : CLR_TEXT;
    }

    TRIVERTEX v[2] = {
        { rc.left, rc.top, (COLOR16)(GetRValue(top) << 8), (COLOR16)(GetGValue(top) << 8), (COLOR16)(GetBValue(top) << 8), 0 },
        { rc.left, rc.bottom, (COLOR16)(GetRValue(bot) << 8), (COLOR16)(GetGValue(bot) << 8), (COLOR16)(GetBValue(bot) << 8), 0 },
    };
    GRADIENT_RECT gr{ 0, 1 };
    // clip arredondado
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, 10, 10);
    SelectClipRgn(dis->hDC, rgn);
    GradientFill(dis->hDC, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    SelectClipRgn(dis->hDC, nullptr);
    DeleteObject(rgn);

    HPEN pen = CreatePen(PS_SOLID, 1, border);
    SelectObject(dis->hDC, pen);
    SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
    DeleteObject(pen);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, txt);
    SelectObject(dis->hDC, g_FontBold);
    wchar_t text[128]; GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextW(dis->hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---- Control factories -------------------------------------------------------
static HWND MkBtn(HWND parent, const wchar_t* t, int x, int y, int w, int h, int id) {
    HWND b = CreateWindowW(L"BUTTON", t, WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_FontBold, TRUE);
    return b;
}
static HWND MkEdit(HWND parent, int x, int y, int w, int h, int id, bool pass = false) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP | (pass ? ES_PASSWORD : 0),
        x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_Font, TRUE);
    return e;
}
static HWND MkLbl(HWND parent, const wchar_t* t, int x, int y, int w, int id = 0) {
    HWND s = CreateWindowW(L"STATIC", t, WS_CHILD | SS_LEFT,
        x, y, w, 16, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)g_FontSm, TRUE);
    return s;
}

// ---- Layout ------------------------------------------------------------------
// Janela 460x680. Card de conteudo: x=28 w=404. Campos dentro: x=52 w=356.
static void ApplyScreen() {
    int s = g.screen.load();
    auto sw = [&](HWND h, bool v) { if (h) ShowWindow(h, v ? SW_SHOW : SW_HIDE); };
    bool login = s == SCR_LOGIN, signup = s == SCR_SIGNUP, main = s == SCR_MAIN, pay = s == SCR_PAY;

    sw(g_hLblEmail, login);   sw(g_hEmail, login);
    sw(g_hLblPass, login);    sw(g_hPass, login);
    sw(g_hBtnLogin, login);   sw(g_hBtnToSignup, login);

    sw(g_hLblSEmail, signup); sw(g_hSEmail, signup);
    sw(g_hLblSPass, signup);  sw(g_hSPass, signup);
    sw(g_hLblSPass2, signup); sw(g_hSPass2, signup);
    sw(g_hBtnDoSignup, signup); sw(g_hBtnToLogin, signup);

    sw(g_hBtnMain, main); sw(g_hBtnRefresh, main); sw(g_hBtnLogout, main);

    sw(g_hQrBox, pay); sw(g_hEmv, pay); sw(g_hBtnCopy, pay);
    sw(g_hBtnCheck, pay); sw(g_hBtnBack, pay);

    sw(g_hStatus, true); sw(g_hLog, true); sw(g_hBtnDiscord, true);

    if (main)
        SetWindowTextW(g_hBtnMain, g.injected ? L"INJETADO — APERTE F9" :
            (g.licensed ? L"INJETAR NO FIVEM" : L"COMPRAR AGORA"));
    if (pay) SetWindowTextW(g_hBtnCheck, L"JA PAGUEI — VERIFICAR");

    InvalidateRect(g_hWnd, nullptr, TRUE);
}

// ---- WM_CREATE ----------------------------------------------------------------
static void BuildUI(HWND hWnd) {
    int x = 52, w = 356;

    // LOGIN (card: 28,116 .. 432,470)
    g_hLblEmail = MkLbl(hWnd, L"EMAIL", x, 148, w);
    g_hEmail = MkEdit(hWnd, x, 168, w, 40, ID_EMAIL);
    g_hLblPass = MkLbl(hWnd, L"SENHA", x, 220, w);
    g_hPass = MkEdit(hWnd, x, 240, w, 40, ID_PASS, true);
    g_hBtnLogin = MkBtn(hWnd, L"ENTRAR", x, 302, w, 46, ID_BTN_LOGIN);
    g_hBtnToSignup = MkBtn(hWnd, L"NAO TENHO CONTA — CADASTRAR", x, 358, w, 34, ID_BTN_TOSIGNUP);

    // SIGNUP
    g_hLblSEmail = MkLbl(hWnd, L"EMAIL", x, 148, w);
    g_hSEmail = MkEdit(hWnd, x, 168, w, 38, ID_SEMAIL);
    g_hLblSPass = MkLbl(hWnd, L"SENHA (min. 6)", x, 216, w);
    g_hSPass = MkEdit(hWnd, x, 236, w, 38, ID_SPASS, true);
    g_hLblSPass2 = MkLbl(hWnd, L"CONFIRMAR SENHA", x, 284, w);
    g_hSPass2 = MkEdit(hWnd, x, 304, w, 38, ID_SPASS2, true);
    g_hBtnDoSignup = MkBtn(hWnd, L"CRIAR MINHA CONTA", x, 356, w, 44, ID_BTN_DOSIGNUP);
    g_hBtnToLogin = MkBtn(hWnd, L"VOLTAR AO LOGIN", x, 410, w, 32, ID_BTN_TOLOGIN);

    // MAIN
    g_hBtnMain = MkBtn(hWnd, L"INJETAR NO FIVEM", x, 400, w, 50, ID_BTN_MAIN);
    g_hBtnRefresh = MkBtn(hWnd, L"ATUALIZAR STATUS", x, 462, w, 32, ID_BTN_REFRESH);
    g_hBtnLogout = MkBtn(hWnd, L"SAIR DA CONTA", x, 502, w, 32, ID_BTN_LOGOUT);

    // PAY
    g_hQrBox = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_BITMAP | SS_CENTERIMAGE,
        115, 152, 230, 230, hWnd, (HMENU)(INT_PTR)ID_QRBOX, nullptr, nullptr);
    g_hEmv = MkEdit(hWnd, x, 400, w - 72, 34, ID_EMV);
    SendMessageW(g_hEmv, EM_SETREADONLY, TRUE, 0);
    g_hBtnCopy = MkBtn(hWnd, L"COPIAR", x + w - 64, 400, 64, 34, ID_BTN_COPY);
    g_hBtnCheck = MkBtn(hWnd, L"JA PAGUEI — VERIFICAR", x, 448, w, 40, ID_BTN_CHECK);
    g_hBtnBack = MkBtn(hWnd, L"VOLTAR", x, 498, w, 32, ID_BTN_BACK);

    // shared
    g_hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT,
        32, 552, 396, 22, hWnd, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_FontBig, TRUE);
    g_hLog = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT,
        32, 576, 396, 34, hWnd, (HMENU)(INT_PTR)ID_LOG, nullptr, nullptr);
    SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_FontSm, TRUE);
    g_hBtnDiscord = MkBtn(hWnd, L"SUPORTE NO DISCORD", x, 620, w, 34, ID_BTN_DISCORD);

    ApplyScreen();
}

// ---- WndProc ------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE:
        g_hWnd = hWnd;
        g_Font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontSm = CreateFontW(-11, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontCard = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontTitle = CreateFontW(-34, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontBig = CreateFontW(-17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_BgBrush = CreateSolidBrush(CLR_BG);
        g_CardBrush = CreateSolidBrush(CLR_CARD);
        g_EditBrush = CreateSolidBrush(CLR_EDIT_BG);
        g_WhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        BuildUI(hWnd);

        {
            std::string rt = LoadRefresh();
            if (!rt.empty()) {
                SetStatus(L"Restaurando sessao...", ST_NEUTRO);
                std::thread([rt, hWnd] {
                    g.busy = true;
                    if (DoRefresh(rt)) {
                        std::wstring e;
                        if (CheckLicense(e)) g.licensed = true;
                        g.screen = SCR_MAIN;
                        PostMessageW(hWnd, WM_APP + 1, 0, 0);
                    }
                    g.busy = false;
                }).detach();
            }
        }
        return 0;

    case WM_APP + 1: // auth ok -> main
        ApplyScreen();
        SetStatus(g.licensed ? L"Licenca ativa — pronto para injetar" :
            (g.statusMsg.empty() ? L"Sem licenca — compre para liberar" : g.statusMsg.c_str()),
            g.licensed ? ST_OK : ST_GOLD);
        Log(g.licensed ? L"[OK] Licenca validada." : L"[..] Sem licenca ativa.");
        return 0;

    case WM_APP + 2: // erro
        SetStatus(g.statusMsg.c_str(), ST_ERR);
        if (g.screen == SCR_PAY) { g.screen = SCR_MAIN; ApplyScreen(); }
        return 0;

    case WM_APP + 4: // pix gerado
    {
        ApplyScreen();
        SetStatus(L"Escaneie o QR ou copie o codigo", ST_GOLD);
        SetWindowTextW(g_hEmv, ToWide(g.qrEmv).c_str());
        Log(L"[OK] PIX gerado. Aguardando pagamento...");
        // QR do edge function (data URL base64); fallback qrserver
        std::thread([] {
            HBITMAP b = g.qrB64.empty() ? nullptr : QrFromDataUrl(g.qrB64);
            if (!b) {
                std::string url = "https://api.qrserver.com/v1/create-qr-code/?size=230x230&data=";
                std::string enc;
                for (char c : g.qrEmv) {
                    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') enc += c;
                    else { char bb[4]; snprintf(bb, 4, "%%%02X", (unsigned char)c); enc += bb; }
                }
                auto r = HttpDownload(url + enc);
                if (r.status == 200 && r.body.size() > 100) b = PngToBitmap(r.body);
            }
            if (b) {
                if (g_QrBitmap) DeleteObject(g_QrBitmap);
                g_QrBitmap = b;
                PostMessageW(g_hWnd, WM_APP + 7, 0, 0);
            } else {
                Log(L"[..] QR nao carregou — use o copia-e-cola.");
            }
        }).detach();
        std::thread([] { ThreadPollOrder(g_hWnd); }).detach();
        return 0;
    }

    case WM_APP + 5: // pago
        g.screen = SCR_MAIN;
        ApplyScreen();
        SetStatus(L"Pagamento confirmado! Licenca liberada.", ST_OK);
        Log(L"[OK] Pagamento confirmado.");
        return 0;

    case WM_APP + 6: // injetado
        SetStatus(L"Injetado! Aperte F9 no jogo.", ST_OK);
        SetWindowTextW(g_hBtnMain, L"INJETADO — APERTE F9");
        Log(L"[OK] DLL injetada com sucesso.");
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_APP + 7: // QR bitmap pronto
        if (g_QrBitmap) {
            SendMessageW(g_hQrBox, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_QrBitmap);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;

    case WM_APP + 8: // signup: criou, precisa confirmar email
        g.screen = SCR_LOGIN;
        ApplyScreen();
        SetStatus(L"Conta criada! Confirme o email e entre.", ST_OK);
        return 0;

    case WM_SETCURSOR:
        return DefWindowProcW(hWnd, msg, wp, lp);

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        FillRect(hdc, &ps.rcPaint, g_BgBrush);
        SetBkMode(hdc, TRANSPARENT);

        // logo
        SetTextColor(hdc, CLR_ACCENT);
        SelectObject(hdc, g_FontTitle);
        TextOutW(hdc, 30, 18, L"C4PT0R", 6);
        SetTextColor(hdc, CLR_DIM);
        SelectObject(hdc, g_FontCard);
        int s = g.screen.load();
        const wchar_t* sub = s == SCR_LOGIN ? L"Entre para continuar" :
            s == SCR_SIGNUP ? L"Crie sua conta" :
            s == SCR_PAY ? L"Pagamento via PIX" : L"Loader";
        TextOutW(hdc, 32, 62, sub, (int)wcslen(sub));
        // linha accent
        HPEN pen = CreatePen(PS_SOLID, 2, CLR_ACCENT);
        SelectObject(hdc, pen);
        MoveToEx(hdc, 32, 88, nullptr); LineTo(hdc, 130, 88);
        HPEN pen2 = CreatePen(PS_SOLID, 2, CLR_BORDER);
        SelectObject(hdc, pen2);
        LineTo(hdc, 428, 88);
        DeleteObject(pen); DeleteObject(pen2);
        SelectObject(hdc, GetStockObject(NULL_PEN));

        // card container
        RECT card{ 28, 106, 432, 540 };
        PaintCard(hdc, card, CLR_CARD, CLR_BORDER);

        if (s == SCR_MAIN) {
            // card do plano
            RECT pc{ 44, 122, 416, 384 };
            PaintCard(hdc, pc, CLR_CARD2, CLR_BORDER);

            SetTextColor(hdc, CLR_DIM);
            SelectObject(hdc, g_FontSm);
            TextOutW(hdc, 64, 142, L"SEU PLANO", 9);

            SetTextColor(hdc, CLR_TEXT);
            SelectObject(hdc, g_FontBig);
            std::wstring pn = g.planName.empty() ? L"C4pt0r Mod Menu" : g.planName;
            TextOutW(hdc, 64, 160, pn.c_str(), (int)pn.size());

            // status pill
            RECT pill{ 64, 196, 170, 220 };
            if (g.injected) PaintPill(hdc, pill, CLR_ACCENT, L"INJETADO", g_FontSm);
            else if (g.licensed) PaintPill(hdc, pill, CLR_OK, L"ATIVA", g_FontSm);
            else PaintPill(hdc, pill, CLR_GOLD, L"SEM LICENCA", g_FontSm);

            SelectObject(hdc, g_FontCard);
            SetTextColor(hdc, CLR_DIM);
            TextOutW(hdc, 64, 238, L"Validade", 8);
            SetTextColor(hdc, CLR_TEXT);
            std::wstring v = g.licensed ? g.validity : L"—";
            if (v.empty()) v = L"—";
            TextOutW(hdc, 64, 256, v.c_str(), (int)v.size());

            SetTextColor(hdc, CLR_DIM);
            TextOutW(hdc, 64, 288, L"Conta", 5);
            SetTextColor(hdc, CLR_TEXT);
            std::wstring em = ToWide(g.email);
            TextOutW(hdc, 64, 306, em.c_str(), (int)em.size());

            SetTextColor(hdc, CLR_DIM);
            TextOutW(hdc, 64, 338, L"Maquina (HWID)", 14);
            SetTextColor(hdc, CLR_ACCENT2);
            std::wstring hw = ToWide(GetHwid());
            TextOutW(hdc, 64, 356, hw.c_str(), (int)hw.size());
        }

        if (s == SCR_PAY) {
            SetTextColor(hdc, CLR_DIM);
            SelectObject(hdc, g_FontSm);
            TextOutW(hdc, 52, 124, L"ESCANEIE COM O APP DO BANCO", 27);
            // painel branco atras do QR
            RECT qr{ 103, 148, 357, 394 };
            PaintCard(hdc, qr, RGB(245, 245, 245), CLR_BORDER);
            if (!g_QrBitmap) {
                SetTextColor(hdc, CLR_DIM);
                SelectObject(hdc, g_FontCard);
                RECT t{ 103, 255, 357, 285 };
                DrawTextW(hdc, L"Gerando QR...", -1, &t, DT_CENTER);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        HWND c = (HWND)lp;
        if (c == g_hStatus) {
            SetBkColor(hdc, CLR_BG);
            SetTextColor(hdc, g.statKind == ST_OK ? CLR_OK :
                g.statKind == ST_ERR ? CLR_ERR : CLR_GOLD);
            return (LRESULT)g_BgBrush;
        }
        if (c == g_hLog) {
            SetBkColor(hdc, CLR_BG);
            SetTextColor(hdc, CLR_DIM);
            return (LRESULT)g_BgBrush;
        }
        // labels dentro do card
        SetBkColor(hdc, CLR_CARD);
        SetTextColor(hdc, CLR_DIM);
        return (LRESULT)g_CardBrush;
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
        if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_UPDATE) break;
        if (g.busy && id != ID_BTN_BACK) return 0;

        if (id == ID_BTN_DISCORD) ShellExecuteW(nullptr, L"open", DISCORD_URL, nullptr, nullptr, SW_SHOW);

        if (id == ID_BTN_TOSIGNUP) { g.screen = SCR_SIGNUP; ApplyScreen(); SetStatus(L"", ST_NEUTRO); }
        if (id == ID_BTN_TOLOGIN || id == ID_BTN_BACK) {
            g.screen = id == ID_BTN_BACK ? SCR_MAIN : SCR_LOGIN;
            ApplyScreen(); SetStatus(L"", ST_NEUTRO);
        }

        if (id == ID_BTN_LOGIN) {
            wchar_t e[256], p[256];
            GetWindowTextW(g_hEmail, e, 256); GetWindowTextW(g_hPass, p, 256);
            if (!*e || !*p) { SetStatus(L"Preencha email e senha.", ST_ERR); return 0; }
            SetStatus(L"Entrando...", ST_NEUTRO);
            std::thread(ThreadLogin, hWnd, Narrow(e), Narrow(p)).detach();
        }

        if (id == ID_BTN_DOSIGNUP) {
            wchar_t e[256], p[256], p2[256];
            GetWindowTextW(g_hSEmail, e, 256);
            GetWindowTextW(g_hSPass, p, 256); GetWindowTextW(g_hSPass2, p2, 256);
            if (!*e || wcslen(p) < 6) { SetStatus(L"Email valido e senha com 6+ caracteres.", ST_ERR); return 0; }
            if (wcscmp(p, p2)) { SetStatus(L"As senhas nao conferem.", ST_ERR); return 0; }
            SetStatus(L"Criando conta...", ST_NEUTRO);
            std::thread(ThreadSignup, hWnd, Narrow(e), Narrow(p)).detach();
        }

        if (id == ID_BTN_LOGOUT) {
            ClearSession();
            g.accessToken.clear(); g.refreshToken.clear();
            g.licensed = false; g.injected = false;
            g.planName.clear(); g.validity.clear();
            g.screen = SCR_LOGIN;
            ApplyScreen();
            SetStatus(L"Desconectado.", ST_NEUTRO);
        }

        if (id == ID_BTN_REFRESH) {
            SetStatus(L"Verificando licenca...", ST_NEUTRO);
            std::thread([hWnd] {
                g.busy = true;
                std::wstring e;
                if (CheckLicense(e)) g.licensed = true;
                else { g.licensed = false; if (!e.empty()) g.statusMsg = e; }
                PostMessageW(hWnd, WM_APP + 1, 0, 0);
                g.busy = false;
            }).detach();
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
                SetStatus(L"Codigo PIX copiado!", ST_OK);
            }
        }

        if (id == ID_BTN_CHECK) {
            SetStatus(L"Verificando pagamento...", ST_NEUTRO);
            std::thread([hWnd] {
                std::string body = "{\"order_id\":\"" + g.orderId + "\"}";
                auto r = FnCall("order-status", body, g.accessToken);
                auto j = JsonParse(r.body);
                if (j["status"].str() == "paid") {
                    g.paid = true;
                    std::wstring e;
                    if (CheckLicense(e)) g.licensed = true;
                    PostMessageW(hWnd, WM_APP + 5, 0, 0);
                } else {
                    g.statusMsg = L"Ainda nao confirmado. Aguarde alguns segundos.";
                    PostMessageW(hWnd, WM_APP + 9, 0, 0);
                }
            }).detach();
        }

        if (id == ID_BTN_MAIN) {
            if (g.injected) return 0;
            if (g.licensed) {
                DWORD pid = FindFiveMPid();
                if (!pid) {
                    SetStatus(L"Abra o FiveM e entre num servidor.", ST_ERR);
                } else {
                    SetStatus(L"Baixando e injetando...", ST_NEUTRO);
                    std::thread(ThreadDownloadInject, hWnd).detach();
                }
            } else {
                SetStatus(L"Gerando PIX...", ST_NEUTRO);
                std::thread(ThreadBuyPix, hWnd).detach();
            }
        }
        return 0;
    }

    case WM_APP + 9: // check manual: ainda pendente
        SetStatus(g.statusMsg.c_str(), ST_GOLD);
        return 0;

    case WM_ERASEBKGND:
        return 1; // fundo pintado no WM_PAINT

    case WM_DESTROY:
        if (g_QrBitmap) DeleteObject(g_QrBitmap);
        DeleteObject(g_BgBrush); DeleteObject(g_CardBrush);
        DeleteObject(g_EditBrush); DeleteObject(g_WhiteBrush);
        DeleteObject(g_Font); DeleteObject(g_FontSm); DeleteObject(g_FontBold);
        DeleteObject(g_FontCard); DeleteObject(g_FontTitle); DeleteObject(g_FontBig);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ---- Entry -------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDPIAware();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"C4pt0rLoader";
    RegisterClassExW(&wc);

    int winW = 460, winH = 700;
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
