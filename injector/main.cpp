// ============================================================================
//  C4pt0r Loader v3 — ImGui/D3D11 UI
//  Login/Cadastro -> catalogo de produtos -> licenca -> download -> injecao.
//  PIX dentro do loader (QR textura + copia-e-cola).
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <wincrypt.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <Shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include "net.h"
#include "deps/qrcodegen.hpp"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

// ---- Config ----------------------------------------------------------------
static const char* SUPA_HOST = "bzthdklqvjtutkeiimpq.supabase.co";
static const char* ANON_KEY  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJ6dGhka2xxdmp0dXRrZWlpbXBxIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTIxNzU2MDYsImV4cCI6MjEwNzc1MTYwNn0.K1_us_hjL10yynZRDsrWZKwId-l5NuzrDSuHeFXtIdI";
static const wchar_t* DISCORD_URL = L"https://discord.gg/PhkbhxMBy";

// ---- D3D11 -----------------------------------------------------------------
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
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, nullptr, &g_pd3dCtx));
}

// ---- App state -------------------------------------------------------------
enum Screen { SCR_LOGIN, SCR_SIGNUP, SCR_MAIN, SCR_PAY };
enum StatKind { ST_NEUTRO, ST_OK, ST_ERR, ST_GOLD };

struct Variant {
    std::string id, label, bonus;
    int price_cents = 0, discount_cents = 0;
    int duration_minutes = -1;   // -1 = vitalicio
    int finalPrice() const {
        int f = price_cents - discount_cents;
        return f < 1 ? 1 : f;
    }
    std::string durLabel() const {
        if (duration_minutes < 0) return "vitalicio";
        if (duration_minutes < 60) return std::to_string(duration_minutes) + "min";
        if (duration_minutes % 1440 == 0)
            return duration_minutes == 1440 ? "24 horas"
                : std::to_string(duration_minutes / 1440) + " dias";
        return std::to_string(duration_minutes / 60) + " horas";
    }
};

struct Product {
    std::string id, slug, name, desc, dllPath;
    int price_cents = 0, duration_days = 0;   // 0 = vitalicio
    std::vector<std::string> features;
    std::vector<Variant> variants;
    bool isExe() const {
        return dllPath.size() > 4 &&
            dllPath.compare(dllPath.size() - 4, 4, ".exe") == 0;
    }
    // menor preco final entre variantes (0 se nao tem variante)
    int minPrice() const {
        if (variants.empty()) return price_cents;
        int m = variants[0].finalPrice();
        for (auto& v : variants) if (v.finalPrice() < m) m = v.finalPrice();
        return m;
    }
};
struct LicInfo { bool active = false; std::string expires, variant; };

static struct {
    std::atomic<int>     screen{SCR_LOGIN};
    std::atomic<bool>    busy{false};
    std::atomic<bool>    paid{false};
    std::atomic<bool>    injected{false};
    std::atomic<int>     statKind{ST_NEUTRO};
    std::string          accessToken, refreshToken, email;
    std::vector<Product> products;
    std::map<std::string, LicInfo> licenses;      // slug -> licenca do user
    std::string          planName, validity, hwid;
    std::string          status;
    std::string          orderId, qrEmv, qrB64, paySlug;
    std::mutex           mtx;
    std::string          logLine;
} g;

static ID3D11ShaderResourceView* g_QrTex = nullptr;
static int g_QrW = 0, g_QrH = 0;

static void SetStatus(const std::string& s, StatKind k = ST_NEUTRO) {
    std::lock_guard<std::mutex> l(g.mtx);
    g.status = s;
    g.statKind = k;
}
static void LogLine(const std::string& s) {
    std::lock_guard<std::mutex> l(g.mtx);
    g.logLine = s;
}

// ---- Helpers ----------------------------------------------------------------
static std::wstring ToWide(const std::string& s); // from net.h
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
static std::string ToUtf8(const std::wstring& w) { return Narrow(w.c_str()); }

// ---- Session (DPAPI) ---------------------------------------------------------
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

// ---- HWID --------------------------------------------------------------------
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

// ---- Supabase -----------------------------------------------------------------
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
static HttpRes RestGet(const std::string& path, const std::string& jwt = "") {
    auto h = ApiHdrs(jwt);
    if (jwt.empty()) h.push_back({ "Authorization", "Bearer " + std::string(ANON_KEY) });
    return Http(SUPA_HOST, "GET", path, h);
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

// 0 = logado | 1 = criou (confirmar email) | -1 = erro
static int DoSignup(const std::string& email, const std::string& pass, std::string& err) {
    std::string body = "{\"email\":\"" + JsonEsc(email) + "\",\"password\":\"" + JsonEsc(pass) + "\"}";
    auto r = Http(SUPA_HOST, "POST", "/auth/v1/signup", ApiHdrs(), body);
    auto j = JsonParse(r.body);
    if (!j["access_token"].str().empty()) {
        g.accessToken = j["access_token"].str();
        g.refreshToken = j["refresh_token"].str();
        SaveRefresh(g.refreshToken);
        return 0;
    }
    if (r.status == 200 || r.status == 201) return 1;
    err = j["msg"].str().empty() ? j["error"].str() : j["msg"].str();
    if (err.empty()) err = "Nao foi possivel criar a conta.";
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

static void FetchProducts() {
    auto r = RestGet("/rest/v1/products?select=id,slug,name,description,price_cents,duration_days,dll_path,features&active=eq.true&order=price_cents.asc");
    auto j = JsonParse(r.body);
    if (j.type != Json::Arr) return;
    std::vector<Product> out;
    for (auto& p : j.a) {
        Product pr;
        pr.id = p["id"].str();
        pr.slug = p["slug"].str();
        pr.name = p["name"].str();
        pr.desc = p["description"].str();
        pr.dllPath = p["dll_path"].str();
        pr.price_cents = (int)p["price_cents"].n;
        pr.duration_days = (int)p["duration_days"].n;
        for (auto& f : p["features"].a) pr.features.push_back(f.str());
        if (pr.slug.empty()) continue;
        out.push_back(pr);
    }
    // variantes (planos) agrupadas por produto
    auto rv = RestGet("/rest/v1/product_variants?select=id,product_id,label,price_cents,discount_cents,duration_minutes,bonus_label&active=eq.true&order=sort.asc,price_cents.asc");
    auto jv = JsonParse(rv.body);
    if (jv.type == Json::Arr) {
        for (auto& vv : jv.a) {
            Variant v;
            v.id = vv["id"].str();
            v.label = vv["label"].str();
            v.bonus = vv["bonus_label"].str();
            v.price_cents = (int)vv["price_cents"].n;
            v.discount_cents = (int)vv["discount_cents"].n;
            v.duration_minutes = vv["duration_minutes"].type == Json::Num
                ? (int)vv["duration_minutes"].n : -1;
            std::string pid = vv["product_id"].str();
            for (auto& p : out) if (p.id == pid) p.variants.push_back(v);
        }
    }
    std::lock_guard<std::mutex> l(g.mtx);
    g.products = out;
}

// license-check sem slug -> preenche mapa de licencas; com slug+hwid -> download_url
static bool CheckLicense(const std::string& slug, bool bindHwid, std::string* outUrl, std::string* outErr) {
    std::string body = "{\"action\":\"check\"";
    if (!slug.empty()) body += ",\"product_slug\":\"" + slug + "\"";
    if (bindHwid) body += ",\"hwid\":\"" + GetHwid() + "\"";
    body += "}";
    auto r = FnCall("license-check", body, g.accessToken);
    auto j = JsonParse(r.body);

    // mapa de todas as licencas (sempre presente)
    std::map<std::string, LicInfo> m;
    for (auto& l : j["licenses"].a) {
        LicInfo li;
        li.active = l["status"].str() == "active";
        li.expires = l["expires_at"].str();
        li.variant = l["variant_label"].str();
        m[l["slug"].str()] = li;
    }
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        g.licenses = m;
    }

    if (slug.empty()) return true;

    if (j["licensed"].truthy()) {
        if (outUrl) *outUrl = j["download_url"].str();
        auto lic = j["license"];
        g.planName = lic["product"].str();
        std::string exp = lic["expires_at"].str();
        if (exp.empty()) g.validity = "Vitalicia";
        else {
            // dias restantes
            int y, mo, d;
            if (sscanf_s(exp.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) {
                SYSTEMTIME st{}; st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
                FILETIME ft; SystemTimeToFileTime(&st, &ft);
                ULARGE_INTEGER a; a.u.LowPart = ft.dwLowDateTime; a.u.HighPart = ft.dwHighDateTime;
                FILETIME nf; GetSystemTimeAsFileTime(&nf);
                ULARGE_INTEGER b; b.u.LowPart = nf.dwLowDateTime; b.u.HighPart = nf.dwHighDateTime;
                long long days = (long long)(a.QuadPart - b.QuadPart) / 864000000000LL;
                char vb[128];
                snprintf(vb, 128, "ate %02d/%02d/%d (%lld dias)", d, mo, y, days < 0 ? 0 : days);
                g.validity = vb;
            } else g.validity = exp;
        }
        return true;
    }
    if (outErr) *outErr = j["error"].str();
    return false;
}

// ---- FiveM inject --------------------------------------------------------------
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

// ---- QR PIX -> textura D3D (gerado localmente, sem rede) --------------------------
static bool QrToTexture(const std::string& emv, ID3D11ShaderResourceView** srv, int* ow, int* oh) {
    using qrcodegen::QrCode;
    QrCode qr = QrCode::encodeText(emv.c_str(), QrCode::Ecc::LOW);
    const int border = 4, scale = 8;
    int dim = (qr.getSize() + border * 2) * scale;
    std::vector<BYTE> px((size_t)dim * dim * 4, 255);   // branco opaco
    for (int y = 0; y < qr.getSize(); y++)
        for (int x = 0; x < qr.getSize(); x++)
            if (qr.getModule(x, y))
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        size_t off = ((size_t)((y + border) * scale + dy) * dim
                            + (x + border) * scale + dx) * 4;
                        px[off] = px[off + 1] = px[off + 2] = 0;   // modulo preto
                    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = dim; td.Height = dim; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{ px.data(), (UINT)(dim * 4), 0 };
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&td, &sd, &tex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = td.Format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    bool ok = SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(tex, &sv, srv));
    if (ok) { *ow = *oh = dim; }
    tex->Release();
    return ok;
}

// ---- Worker threads ---------------------------------------------------------------
static void AfterAuth() {
    CheckLicense("", false, nullptr, nullptr);   // enche o mapa de licencas
    g.screen = SCR_MAIN;
    SetStatus("", ST_NEUTRO);
}

static void ThreadLogin(std::string email, std::string pass) {
    g.busy = true;
    std::string err;
    if (DoPasswordGrant(email, pass, err)) {
        g.email = email;
        g.hwid = GetHwid();
        AfterAuth();
        LogLine("[OK] Login efetuado.");
    } else {
        SetStatus(err, ST_ERR);
    }
    g.busy = false;
}

static void ThreadSignup(std::string email, std::string pass) {
    g.busy = true;
    std::string err;
    int rc = DoSignup(email, pass, err);
    if (rc == 0) {
        g.email = email;
        g.hwid = GetHwid();
        AfterAuth();
        LogLine("[OK] Conta criada e logada.");
    } else if (rc == 1) {
        g.screen = SCR_LOGIN;
        SetStatus("Conta criada! Confirme o email e entre.", ST_OK);
    } else {
        SetStatus(err, ST_ERR);
    }
    g.busy = false;
}

static void ThreadRefreshLicenses() {
    g.busy = true;
    CheckLicense("", false, nullptr, nullptr);
    SetStatus("Licencas atualizadas.", ST_OK);
    g.busy = false;
}

static void ThreadBuyPix(std::string slug, std::string variantId) {
    g.busy = true;
    std::string body = "{\"product_slug\":\"" + slug + "\",\"method\":\"pix\"";
    if (!variantId.empty()) body += ",\"variant_id\":\"" + variantId + "\"";
    body += "}";
    auto r = FnCall("checkout", body, g.accessToken);
    auto j = JsonParse(r.body);
    if (j["ok"].truthy() && !j["qr_code"].str().empty()) {
        g.orderId = j["order_id"].str();
        g.qrEmv = j["qr_code"].str();
        g.paySlug = slug;
        g.paid = false;
        // QR gerado localmente do codigo PIX — instantaneo, sem depender de rede
        {
            ID3D11ShaderResourceView* srv = nullptr; int w = 0, hgt = 0;
            if (QrToTexture(g.qrEmv, &srv, &w, &hgt)) {
                if (g_QrTex) g_QrTex->Release();
                g_QrTex = srv; g_QrW = w; g_QrH = hgt;
            } else LogLine("[..] Falha ao gerar QR — use copia-e-cola.");
        }
        g.screen = SCR_PAY;
        SetStatus("Escaneie o QR ou copie o codigo", ST_GOLD);
        // poll
        std::thread([] {
            for (int i = 0; i < 150 && g.screen == SCR_PAY && !g.paid; i++) {
                std::string b = "{\"order_id\":\"" + g.orderId + "\"}";
                auto rr = FnCall("order-status", b, g.accessToken);
                auto st = JsonParse(rr.body)["status"].str();
                if (st == "paid") {
                    g.paid = true;
                    CheckLicense("", false, nullptr, nullptr);
                    g.screen = SCR_MAIN;
                    SetStatus("Pagamento confirmado! Licenca ativa.", ST_OK);
                    LogLine("[OK] Pagamento confirmado.");
                    return;
                }
                if (st == "failed" || st == "expired") {
                    g.screen = SCR_MAIN;
                    SetStatus("Pagamento expirado ou falhou.", ST_ERR);
                    return;
                }
                Sleep(4000);
            }
        }).detach();
    } else {
        SetStatus(j["error"].str().empty() ? "Falha ao gerar PIX." : j["error"].str(), ST_ERR);
    }
    g.busy = false;
}

static void ThreadInject(std::string slug) {
    g.busy = true;
    SetStatus("Verificando licenca...", ST_NEUTRO);
    std::string url, err;
    if (!CheckLicense(slug, true, &url, &err) || url.empty()) {
        SetStatus(err.empty() ? "Sem licenca ativa para este produto." : err, ST_ERR);
        g.busy = false; return;
    }
    SetStatus("Baixando pacote...", ST_NEUTRO);
    auto r = HttpDownload(url);
    if (r.status != 200 || r.body.size() < 10000) {
        SetStatus("Download falhou.", ST_ERR);
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
        SetStatus("Abra o FiveM e entre num servidor.", ST_ERR);
        g.busy = false; return;
    }
    if (InjectDLL(pid, dllPath)) {
        g.injected = true;
        SetStatus("Injetado! Aperte F9 no jogo.", ST_OK);
        LogLine("[OK] DLL injetada com sucesso.");
    } else {
        SetStatus("Injecao falhou. Rode como administrador.", ST_ERR);
    }
    g.busy = false;
}

// produto tipo EXE (spoofer etc): baixa via licenca e executa
static void ThreadRunExe(std::string slug, std::string fname) {
    g.busy = true;
    SetStatus("Verificando licenca...", ST_NEUTRO);
    std::string url, err;
    if (!CheckLicense(slug, true, &url, &err) || url.empty()) {
        SetStatus(err.empty() ? "Sem licenca ativa para este produto." : err, ST_ERR);
        g.busy = false; return;
    }
    SetStatus("Baixando aplicativo...", ST_NEUTRO);
    auto r = HttpDownload(url);
    if (r.status != 200 || r.body.size() < 10000) {
        SetStatus("Download falhou.", ST_ERR);
        g.busy = false; return;
    }
    wchar_t* app = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &app);
    std::wstring dir = app; CoTaskMemFree(app);
    dir += L"\\C4pt0r\\products";
    CreateDirectoryW((std::wstring(app) + L"\\C4pt0r").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring exePath = dir + L"\\" + ToWide(fname);
    HANDLE f = CreateFileW(exePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(f, r.body.data(), (DWORD)r.body.size(), &w, nullptr);
    CloseHandle(f);

    HINSTANCE rc = ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOW);
    if ((INT_PTR)rc > 32) {
        SetStatus("Aplicativo iniciado.", ST_OK);
        LogLine("[OK] " + fname + " executado.");
    } else {
        SetStatus("Falha ao abrir o aplicativo.", ST_ERR);
    }
    g.busy = false;
}

// ============================================================================
//  ImGui style + widgets
// ============================================================================
static ImVec4 COL_ACCENT  = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
static ImVec4 COL_ACCENT2 = ImVec4(0.00f, 0.55f, 0.75f, 1.00f);
static ImVec4 COL_CARD    = ImVec4(0.075f, 0.085f, 0.16f, 1.00f);
static ImVec4 COL_CARD2   = ImVec4(0.10f, 0.11f, 0.20f, 1.00f);
static ImVec4 COL_TEXT    = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
static ImVec4 COL_DIM     = ImVec4(0.48f, 0.50f, 0.58f, 1.00f);
static ImVec4 COL_OK      = ImVec4(0.18f, 0.90f, 0.42f, 1.00f);
static ImVec4 COL_GOLD    = ImVec4(1.00f, 0.72f, 0.25f, 1.00f);
static ImVec4 COL_ERR     = ImVec4(1.00f, 0.30f, 0.42f, 1.00f);
static ImVec4 COL_INPUT   = ImVec4(0.05f, 0.055f, 0.11f, 1.00f);

static ImFont* F_Title = nullptr;
static ImFont* F_Big   = nullptr;
static ImFont* F_Med   = nullptr;
static ImFont* F_Sm    = nullptr;

static void SetupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]           = ImVec4(0.040f, 0.042f, 0.082f, 1.00f);
    c[ImGuiCol_ChildBg]            = COL_CARD;
    c[ImGuiCol_PopupBg]            = ImVec4(0.07f, 0.08f, 0.14f, 0.98f);
    c[ImGuiCol_Border]             = ImVec4(0.00f, 0.83f, 1.00f, 0.14f);
    c[ImGuiCol_FrameBg]            = COL_INPUT;
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.08f, 0.09f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.10f, 0.12f, 0.22f, 1.00f);
    c[ImGuiCol_Button]             = ImVec4(0.00f, 0.55f, 0.75f, 0.85f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.70f, 0.90f, 0.90f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.00f, 0.83f, 1.00f, 1.00f);
    c[ImGuiCol_Text]               = COL_TEXT;
    c[ImGuiCol_TextDisabled]       = COL_DIM;
    c[ImGuiCol_Separator]          = ImVec4(0.00f, 0.83f, 1.00f, 0.20f);
    c[ImGuiCol_CheckMark]          = COL_ACCENT;
    c[ImGuiCol_SliderGrab]         = COL_ACCENT;
    c[ImGuiCol_Header]             = ImVec4(0.00f, 0.45f, 0.60f, 0.40f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.00f, 0.60f, 0.78f, 0.55f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.00f, 0.83f, 1.00f, 0.60f);
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.03f, 0.03f, 0.06f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.00f, 0.45f, 0.60f, 0.55f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.60f, 0.78f, 0.70f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.04f, 0.04f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.05f, 0.05f, 0.09f, 1.00f);

    s.WindowRounding   = 12.0f;
    s.ChildRounding    = 10.0f;
    s.FrameRounding    = 8.0f;
    s.GrabRounding     = 8.0f;
    s.PopupRounding    = 8.0f;
    s.ScrollbarRounding = 10.0f;
    s.WindowPadding    = ImVec2(16, 16);
    s.FramePadding     = ImVec2(12, 8);
    s.ItemSpacing      = ImVec2(10, 10);
    s.WindowBorderSize = 0;
    s.ChildBorderSize  = 1.0f;
    s.FrameBorderSize  = 1.0f;
}

// botao primario grande (gradiente fake via cor + borda accent)
static bool PrimaryBtn(const char* label, float w = -1, float h = 44) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.62f, 0.80f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.78f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.00f, 0.90f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.02f, 0.10f, 0.14f, 1.00f));
    bool c = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(4);
    return c;
}
// botao secundario discreto
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

// ============================================================================
//  Screens
// ============================================================================
static void DrawHeader(const char* subtitle) {
    ImGui::PushFont(F_Title);
    ImGui::TextColored(COL_ACCENT, "C4PT0R");
    ImGui::PopFont();
    ImGui::PushFont(F_Sm);
    ImGui::TextColored(COL_DIM, "%s", subtitle);
    ImGui::PopFont();
    // accent underline
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 90, p.y + 2),
        ImGui::ColorConvertFloat4ToU32(COL_ACCENT));
    dl->AddRectFilled(ImVec2(p.x + 92, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x - 4, p.y + 2),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.16f, 0.18f, 0.32f, 1.00f)));
    ImGui::Dummy(ImVec2(0, 10));
}

static void ScreenLogin() {
    DrawHeader("Entre para continuar");

    ImGui::BeginChild("##logincard", ImVec2(0, 330), ImGuiChildFlags_Border);
    ImGui::Dummy(ImVec2(0, 8));

    static char email[128] = "";
    static char pass[128] = "";

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "EMAIL"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##email", "voce@email.com", email, sizeof(email));
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "SENHA"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pass", "********", pass, sizeof(pass),
        ImGuiInputTextFlags_Password);
    ImGui::Dummy(ImVec2(0, 10));

    bool enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (PrimaryBtn("ENTRAR", -1, 46) || enter) {
        if (!g.busy && email[0] && pass[0]) {
            SetStatus("Entrando...", ST_NEUTRO);
            std::thread(ThreadLogin, std::string(email), std::string(pass)).detach();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (GhostBtn("NAO TENHO CONTA — CADASTRAR", -1, 34)) {
        g.screen = SCR_SIGNUP;
        SetStatus("", ST_NEUTRO);
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 10));
    StatusArea();
    {
        std::string ll;
        { std::lock_guard<std::mutex> l(g.mtx); ll = g.logLine; }
        if (!ll.empty()) { ImGui::PushFont(F_Sm); ImGui::TextDisabled("%s", ll.c_str()); ImGui::PopFont(); }
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 56);
    GhostBtn("SUPORTE NO DISCORD", -1, 34);
    if (ImGui::IsItemClicked()) ShellExecuteW(nullptr, L"open", DISCORD_URL, nullptr, nullptr, SW_SHOW);
}

static void ScreenSignup() {
    DrawHeader("Crie sua conta");

    ImGui::BeginChild("##signupcard", ImVec2(0, 390), ImGuiChildFlags_Border);
    ImGui::Dummy(ImVec2(0, 8));

    static char email[128] = "", pass[128] = "", pass2[128] = "";

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "EMAIL"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##semail", "voce@email.com", email, sizeof(email));
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "SENHA (min. 6 caracteres)"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##spass", "********", pass, sizeof(pass), ImGuiInputTextFlags_Password);
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "CONFIRMAR SENHA"); ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##spass2", "********", pass2, sizeof(pass2), ImGuiInputTextFlags_Password);
    ImGui::Dummy(ImVec2(0, 10));

    if (PrimaryBtn("CRIAR MINHA CONTA", -1, 44)) {
        if (!g.busy) {
            if (!email[0] || strlen(pass) < 6)
                SetStatus("Email valido e senha com 6+ caracteres.", ST_ERR);
            else if (strcmp(pass, pass2))
                SetStatus("As senhas nao conferem.", ST_ERR);
            else {
                SetStatus("Criando conta...", ST_NEUTRO);
                std::thread(ThreadSignup, std::string(email), std::string(pass)).detach();
            }
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (GhostBtn("VOLTAR AO LOGIN", -1, 32)) {
        g.screen = SCR_LOGIN;
        SetStatus("", ST_NEUTRO);
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 10));
    StatusArea();
}

static std::string Brl(int cents) {
    char b[32];
    snprintf(b, 32, "R$ %.2f", cents / 100.0f);
    for (char* c = b; *c; c++) if (*c == '.') *c = ',';
    return b;
}

// card de produto no catalogo
static void ProductCard(const Product& p, const LicInfo& lic) {
    ImGui::PushID(p.slug.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_CARD2);
    ImGui::BeginChild("##pcard", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);

    // header: nome + preco
    ImGui::PushFont(F_Med);
    ImGui::TextColored(COL_TEXT, "%s", p.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
    ImGui::PushFont(F_Med);
    if (!p.variants.empty())
        ImGui::TextColored(COL_ACCENT, "a partir de %s", Brl(p.minPrice()).c_str());
    else
        ImGui::TextColored(COL_ACCENT, "%s", Brl(p.price_cents).c_str());
    ImGui::PopFont();

    ImGui::PushFont(F_Sm);
    if (!p.desc.empty()) ImGui::TextColored(COL_DIM, "%s", p.desc.c_str());
    ImGui::PopFont();

    // pill de status
    if (lic.active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.55f, 0.30f, 0.90f));
        ImGui::SmallButton(" LICENCIA ATIVA ");
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 2));

    if (lic.active) {
        if (p.isExe()) {
            if (PrimaryBtn("BAIXAR E ABRIR", -1, 40)) {
                if (!g.busy) {
                    std::string fn = p.dllPath.substr(p.dllPath.find_last_of('/') + 1);
                    std::thread(ThreadRunExe, p.slug, fn).detach();
                }
            }
        } else {
            if (PrimaryBtn(g.injected ? "INJETADO — APERTE F9" : "INJETAR NO FIVEM", -1, 40)) {
                if (!g.busy && !g.injected) {
                    if (!FindFiveMPid()) SetStatus("Abra o FiveM e entre num servidor.", ST_ERR);
                    else std::thread(ThreadInject, p.slug).detach();
                }
            }
        }
    } else {
        // seletor de plano (variante)
        static std::map<std::string, int> sel;
        int& cur = sel[p.slug];
        if (cur >= (int)p.variants.size()) cur = 0;

        if (!p.variants.empty()) {
            ImGui::PushFont(F_Sm);
            ImGui::TextColored(COL_DIM, "ESCOLHA O PLANO");
            ImGui::PopFont();

            // combo com preco final + desconto + brinde
            const Variant& cur_v = p.variants[cur];
            std::string curLbl = cur_v.label + "  —  " + Brl(cur_v.finalPrice());
            if (cur_v.discount_cents > 0)
                curLbl += "  (de " + Brl(cur_v.price_cents) + ")";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##variant", curLbl.c_str())) {
                for (int i = 0; i < (int)p.variants.size(); i++) {
                    const Variant& v = p.variants[i];
                    std::string lbl = v.label + "  —  " + Brl(v.finalPrice());
                    if (v.discount_cents > 0)
                        lbl += "  (de " + Brl(v.price_cents) + ")";
                    if (!v.bonus.empty()) lbl += "  +BRINDE";
                    if (ImGui::Selectable(lbl.c_str(), i == cur)) cur = i;
                    if (i == cur) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PushFont(F_Sm);
            ImGui::TextColored(COL_DIM, "%s de acesso", cur_v.durLabel().c_str());
            if (!cur_v.bonus.empty())
                ImGui::TextColored(COL_GOLD, "BRINDE: %s", cur_v.bonus.c_str());
            ImGui::PopFont();
        }

        const Variant* v = p.variants.empty() ? nullptr : &p.variants[cur];
        int price = v ? v->finalPrice() : p.price_cents;
        std::string lbl = "COMPRAR — " + Brl(price);
        if (PrimaryBtn(lbl.c_str(), -1, 40)) {
            if (!g.busy)
                std::thread(ThreadBuyPix, p.slug, v ? v->id : std::string()).detach();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4));
}

static void ScreenMain() {
    DrawHeader(g.email.empty() ? "Loader" : g.email.c_str());

    // ---- card da licenca / plano ----
    bool anyLic = false;
    std::string planSlug;
    {
        std::lock_guard<std::mutex> l(g.mtx);
        for (auto& [k, v] : g.licenses) if (v.active) { anyLic = true; if (planSlug.empty()) planSlug = k; }
    }

    ImGui::BeginChild("##plancard", ImVec2(0, 128), ImGuiChildFlags_Border);
    ImGui::PushFont(F_Sm); ImGui::TextColored(COL_DIM, "SUA CONTA"); ImGui::PopFont();

    if (anyLic) {
        std::string planShow = g.planName;
        {
            std::lock_guard<std::mutex> l(g.mtx);
            auto it = g.licenses.find(planSlug);
            if (it != g.licenses.end() && !it->second.variant.empty())
                planShow += " — " + it->second.variant;
        }
        ImGui::PushFont(F_Big);
        ImGui::TextColored(COL_TEXT, "%s", planShow.empty() ? "Licenca ativa" : planShow.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.55f, 0.30f, 0.90f));
        ImGui::SmallButton(" ATIVA ");
        ImGui::PopStyleColor();

        ImGui::PushFont(F_Sm);
        ImGui::TextColored(COL_DIM, "Validade: %s", g.validity.c_str());
        ImGui::TextColored(COL_DIM, "HWID: %s", g.hwid.c_str());
        ImGui::PopFont();
    } else {
        ImGui::PushFont(F_Big);
        ImGui::TextColored(COL_TEXT, "Nenhuma licenca");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.55f, 0.10f, 0.90f));
        ImGui::SmallButton(" SEM LICENCA ");
        ImGui::PopStyleColor();

        ImGui::PushFont(F_Sm);
        ImGui::TextColored(COL_DIM, "Escolha um produto abaixo para liberar o acesso.");
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 6));

    // ---- catalogo ----
    ImGui::PushFont(F_Med);
    ImGui::TextColored(COL_ACCENT, "CATALOGO");
    ImGui::PopFont();

    std::vector<Product> prods;
    std::map<std::string, LicInfo> lics;
    { std::lock_guard<std::mutex> l(g.mtx); prods = g.products; lics = g.licenses; }

    ImGui::BeginChild("##catalog", ImVec2(0, -118), ImGuiChildFlags_None);
    if (prods.empty()) {
        ImGui::TextDisabled("Carregando produtos...");
    }
    for (auto& p : prods) {
        LicInfo li = lics.count(p.slug) ? lics[p.slug] : LicInfo{};
        ProductCard(p, li);
    }
    ImGui::EndChild();

    // ---- rodape ----
    StatusArea();
    if (GhostBtn("SUPORTE NO DISCORD", ImGui::GetContentRegionAvail().x * 0.48f, 34))
        ShellExecuteW(nullptr, L"open", DISCORD_URL, nullptr, nullptr, SW_SHOW);
    ImGui::SameLine();
    if (GhostBtn("SAIR DA CONTA", -1, 34)) {
        ClearSession();
        g.accessToken.clear(); g.refreshToken.clear();
        g.injected = false;
        { std::lock_guard<std::mutex> l(g.mtx); g.licenses.clear(); }
        g.screen = SCR_LOGIN;
        SetStatus("Desconectado.", ST_NEUTRO);
    }
}

static void ScreenPay() {
    DrawHeader("Pagamento via PIX");

    ImGui::BeginChild("##paycard", ImVec2(0, 480), ImGuiChildFlags_Border);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(F_Sm);
    ImGui::TextColored(COL_DIM, "ESCANEIE COM O APP DO BANCO");
    ImGui::PopFont();

    // QR centralizado
    float qrSize = 240;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - qrSize) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    if (g_QrTex) {
        // fundo branco atras do QR
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(pos,
            ImVec2(pos.x + qrSize + 16, pos.y + qrSize + 16),
            IM_COL32(245, 245, 245, 255), 8.0f);
        ImGui::Image((ImTextureID)g_QrTex, ImVec2(qrSize, qrSize),
            ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
        ImGui::Dummy(ImVec2(0, 16));
    } else {
        ImGui::Dummy(ImVec2(qrSize, qrSize));
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - qrSize * 0.5f);
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 100) * 0.5f);
        ImGui::TextDisabled("Gerando QR...");
        ImGui::SetCursorPosY(pos.y + qrSize + 8);
    }

    ImGui::PushFont(F_Sm);
    ImGui::TextColored(COL_DIM, "PIX COPIA-E-COLA");
    ImGui::PopFont();
    static char emv[2048];
    strncpy_s(emv, g.qrEmv.c_str(), sizeof(emv) - 1);
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##emv", emv, sizeof(emv), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (GhostBtn("COPIAR", -1, 0)) {
        ImGui::SetClipboardText(g.qrEmv.c_str());
        SetStatus("Codigo PIX copiado!", ST_OK);
    }
    ImGui::Dummy(ImVec2(0, 6));

    if (PrimaryBtn("JA PAGUEI — VERIFICAR", -1, 42)) {
        if (!g.busy) {
            SetStatus("Verificando pagamento...", ST_NEUTRO);
            std::thread([] {
                std::string b = "{\"order_id\":\"" + g.orderId + "\"}";
                auto r = FnCall("order-status", b, g.accessToken);
                auto st = JsonParse(r.body)["status"].str();
                if (st == "paid") {
                    g.paid = true;
                    CheckLicense("", false, nullptr, nullptr);
                    g.screen = SCR_MAIN;
                    SetStatus("Pagamento confirmado! Licenca ativa.", ST_OK);
                } else {
                    SetStatus("Ainda nao confirmado. Aguarde alguns segundos.", ST_GOLD);
                }
            }).detach();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (GhostBtn("VOLTAR", -1, 32)) {
        g.screen = SCR_MAIN;
        SetStatus("", ST_NEUTRO);
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 8));
    StatusArea();
    {
        std::string ll;
        { std::lock_guard<std::mutex> l(g.mtx); ll = g.logLine; }
        if (!ll.empty()) { ImGui::PushFont(F_Sm); ImGui::TextDisabled("%s", ll.c_str()); ImGui::PopFont(); }
    }
}

// ============================================================================
//  Frame
// ============================================================================
static void RenderFrame() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    int s = g.screen.load();
    if (s == SCR_LOGIN)       ScreenLogin();
    else if (s == SCR_SIGNUP) ScreenSignup();
    else if (s == SCR_MAIN)   ScreenMain();
    else if (s == SCR_PAY)    ScreenPay();

    ImGui::End();
}

// ============================================================================
//  Win32
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
        return true;

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
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"C4pt0rLoader";
    RegisterClassExW(&wc);

    int winW = 480, winH = 780;
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"C4pt0r Loader",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - winW) / 2, (GetSystemMetrics(SM_CYSCREEN) - winH) / 2,
        winW, winH, nullptr, nullptr, hInst, nullptr);

    if (!CreateDeviceD3D(g_hWnd)) {
        MessageBoxW(g_hWnd, L"Falha ao iniciar DirectX 11.", L"C4pt0r", MB_ICONERROR);
        return 1;
    }
    CreateRTV();
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // dark title bar
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hWnd, 20, &dark, sizeof(dark));

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // sem imgui.ini sujando a pasta
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

    // produtos + auto-login
    g.hwid = GetHwid();
    std::thread(FetchProducts).detach();
    {
        std::string rt = LoadRefresh();
        if (!rt.empty()) {
            SetStatus("Restaurando sessao...", ST_NEUTRO);
            std::thread([rt] {
                g.busy = true;
                if (DoRefresh(rt)) { AfterAuth(); }
                else SetStatus("", ST_NEUTRO);
                g.busy = false;
            }).detach();
        }
    }

    // main loop
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

    // cleanup
    if (g_QrTex) g_QrTex->Release();
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
