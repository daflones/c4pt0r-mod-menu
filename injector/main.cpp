// ============================================================================
//  C4pt0r Cheats — Injector
//  Win32 GUI (dark themed) — finds FiveM process, injects DLL
//  Injection: CreateRemoteThread + LoadLibraryW
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <filesystem>
#include <ShlObj.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// ---- Colors -------------------------------------------------------------
constexpr COLORREF CLR_BG        = RGB(15, 15, 25);
constexpr COLORREF CLR_PANEL     = RGB(22, 22, 38);
constexpr COLORREF CLR_ACCENT    = RGB(0, 212, 255);
constexpr COLORREF CLR_TEXT      = RGB(220, 225, 235);
constexpr COLORREF CLR_DIM       = RGB(100, 105, 115);
constexpr COLORREF CLR_SUCCESS   = RGB(0, 255, 120);
constexpr COLORREF CLR_ERROR     = RGB(255, 60, 60);
constexpr COLORREF CLR_BTN       = RGB(0, 140, 180);
constexpr COLORREF CLR_BTN_HOT   = RGB(0, 180, 220);
constexpr COLORREF CLR_EDIT_BG   = RGB(30, 30, 50);

// ---- Globals ------------------------------------------------------------
static HWND   g_hWnd       = nullptr;
static HWND   g_hStatus    = nullptr;
static HWND   g_hDllPath   = nullptr;
static HWND   g_hLog       = nullptr;
static HWND   g_hBtnInject = nullptr;
static HWND   g_hBtnBrowse = nullptr;
static HBRUSH g_BgBrush    = nullptr;
static HBRUSH g_PanelBrush = nullptr;
static HBRUSH g_EditBrush  = nullptr;
static HFONT  g_Font       = nullptr;
static HFONT  g_FontBold   = nullptr;
static HFONT  g_FontTitle  = nullptr;

static std::wstring g_DllPathStr;
static bool         g_Injected = false;

// ---- Control IDs --------------------------------------------------------
enum {
    ID_BTN_INJECT = 101,
    ID_BTN_BROWSE = 102,
    ID_EDIT_DLL   = 103,
    ID_STATIC_LOG = 104,
    ID_STATIC_STATUS = 105,
};

// ---- Log helper ---------------------------------------------------------
static void Log(const wchar_t* msg, COLORREF /*color*/ = CLR_TEXT) {
    // Append to the log static
    int len = GetWindowTextLengthW(g_hLog);
    std::wstring current(len + 1, L'\0');
    GetWindowTextW(g_hLog, current.data(), len + 1);
    current.resize(len);
    if (!current.empty()) current += L"\r\n";
    current += msg;
    SetWindowTextW(g_hLog, current.c_str());
}

// ---- Process finding ----------------------------------------------------
struct ProcessInfo {
    DWORD pid;
    std::wstring name;
};

static std::vector<ProcessInfo> FindFiveMProcesses() {
    std::vector<ProcessInfo> results;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return results;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = pe.szExeFile;
            // FiveM game process: FiveM_b????_GTAProcess.exe or FiveM_GTAProcess.exe
            // Also check for plain GTA5.exe in case of custom launcher
            if (name.find(L"FiveM") != std::wstring::npos &&
                name.find(L"GTAProcess") != std::wstring::npos)
            {
                results.push_back({ pe.th32ProcessID, name });
            }
            // Also accept fivem_b*_GTAProcess (lowercase variants)
            else if (name.find(L"fivem") != std::wstring::npos &&
                     name.find(L"GTAProcess") != std::wstring::npos)
            {
                results.push_back({ pe.th32ProcessID, name });
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return results;
}

// ---- DLL Injection ------------------------------------------------------
static bool InjectDLL(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);

    if (!hProcess) {
        Log(L"[ERRO] OpenProcess falhou. Rode como administrador.");
        return false;
    }

    // Allocate memory in target for DLL path
    size_t pathSize = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProcess, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        Log(L"[ERRO] VirtualAllocEx falhou.");
        CloseHandle(hProcess);
        return false;
    }

    // Write DLL path
    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), pathSize, nullptr)) {
        Log(L"[ERRO] WriteProcessMemory falhou.");
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get LoadLibraryW address
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    auto loadLibAddr = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(hKernel, "LoadLibraryW"));

    // Create remote thread
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, loadLibAddr, remoteMem, 0, nullptr);
    if (!hThread) {
        Log(L"[ERRO] CreateRemoteThread falhou.");
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for injection to complete
    WaitForSingleObject(hThread, 10000);

    // Check if LoadLibrary succeeded
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return exitCode != 0;
}

// ---- File browser -------------------------------------------------------
static std::wstring BrowseForDLL(HWND parent) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = parent;
    ofn.lpstrFilter  = L"DLL Files\0*.dll\0All Files\0*.*\0";
    ofn.lpstrFile    = file;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle   = L"Selecionar C4pt0r DLL";

    if (GetOpenFileNameW(&ofn))
        return file;
    return {};
}

// ---- Auto-find DLL next to injector -------------------------------------
static std::wstring FindDLLNextToExe() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();

    // Look for any .dll that matches our naming
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().wstring();
        auto stem = entry.path().stem().wstring();
        if (ext == L".dll" &&
            (stem.find(L"c4pt0r") != std::wstring::npos ||
             stem.find(L"C4pt0r") != std::wstring::npos ||
             stem.find(L"cheat") != std::wstring::npos))
        {
            return entry.path().wstring();
        }
    }
    return {};
}

// ---- Owner-drawn button -------------------------------------------------
static void DrawButton(LPDRAWITEMSTRUCT dis) {
    bool hot = (dis->itemState & ODS_SELECTED) || (dis->itemState & ODS_HOTLIGHT);
    COLORREF bg = hot ? CLR_BTN_HOT : CLR_BTN;

    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_ACCENT);
    SelectObject(dis->hDC, brush);
    SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, 8, 8);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, CLR_TEXT);
    SelectObject(dis->hDC, g_FontBold);

    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, 64);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DeleteObject(brush);
    DeleteObject(pen);
}

// ---- WndProc ------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE:
    {
        // ---- Create fonts ----
        g_Font      = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontBold  = CreateFontW(-14, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_FontTitle = CreateFontW(-24, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        // ---- Brushes ----
        g_BgBrush    = CreateSolidBrush(CLR_BG);
        g_PanelBrush = CreateSolidBrush(CLR_PANEL);
        g_EditBrush  = CreateSolidBrush(CLR_EDIT_BG);

        int y = 70;
        int pad = 20;
        int w = 545;

        // DLL path edit
        CreateWindowW(L"STATIC", L"DLL Path:", WS_CHILD | WS_VISIBLE,
            pad, y, 60, 20, hWnd, nullptr, nullptr, nullptr);
        y += 22;

        g_hDllPath = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
            pad, y, w - 90, 28, hWnd, (HMENU)ID_EDIT_DLL, nullptr, nullptr);

        g_hBtnBrowse = CreateWindowW(L"BUTTON", L"...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + w - 85, y, 70, 28, hWnd, (HMENU)ID_BTN_BROWSE, nullptr, nullptr);
        y += 40;

        // Status
        g_hStatus = CreateWindowW(L"STATIC", L"Aguardando...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, y, w, 20, hWnd, (HMENU)ID_STATIC_STATUS, nullptr, nullptr);
        y += 28;

        // Inject button
        g_hBtnInject = CreateWindowW(L"BUTTON", L"INJETAR",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, w, 40, hWnd, (HMENU)ID_BTN_INJECT, nullptr, nullptr);
        y += 52;

        // Log area
        g_hLog = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, y, w, 220, hWnd, (HMENU)ID_STATIC_LOG, nullptr, nullptr);

        // Set fonts
        SendMessageW(g_hDllPath,   WM_SETFONT, (WPARAM)g_Font, TRUE);
        SendMessageW(g_hStatus,    WM_SETFONT, (WPARAM)g_Font, TRUE);
        SendMessageW(g_hLog,       WM_SETFONT, (WPARAM)g_Font, TRUE);
        SendMessageW(g_hBtnInject, WM_SETFONT, (WPARAM)g_FontBold, TRUE);
        SendMessageW(g_hBtnBrowse, WM_SETFONT, (WPARAM)g_FontBold, TRUE);

        // Auto-find DLL
        auto found = FindDLLNextToExe();
        if (!found.empty()) {
            SetWindowTextW(g_hDllPath, found.c_str());
            g_DllPathStr = found;
            Log(L"[INFO] DLL encontrada automaticamente.");
        }

        // Dark title bar (Win10/11)
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));

        Log(L"[C4pt0r] Injector pronto.");
        Log(L"[INFO] Abra o FiveM e entre num servidor.");
        Log(L"[INFO] Depois clique INJETAR.");
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // Background
        FillRect(hdc, &ps.rcPaint, g_BgBrush);

        // Title
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_ACCENT);
        SelectObject(hdc, g_FontTitle);
        TextOutW(hdc, 16, 16, L"C4pt0r Cheats", 13);

        // Subtitle
        SetTextColor(hdc, CLR_DIM);
        SelectObject(hdc, g_Font);
        TextOutW(hdc, 16, 46, L"FiveM Injector v1.0", 19);

        // Accent line under title
        HPEN pen = CreatePen(PS_SOLID, 2, CLR_ACCENT);
        SelectObject(hdc, pen);
        MoveToEx(hdc, 16, 65, nullptr);
        LineTo(hdc, 200, 65);
        DeleteObject(pen);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        HWND ctrl = (HWND)lParam;
        if (ctrl == g_hStatus) {
            SetTextColor(hdc, g_Injected ? CLR_SUCCESS : CLR_ACCENT);
        } else {
            SetTextColor(hdc, CLR_TEXT);
        }
        return (LRESULT)g_BgBrush;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, CLR_EDIT_BG);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_EditBrush;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        DrawButton(dis);
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == ID_BTN_BROWSE) {
            auto path = BrowseForDLL(hWnd);
            if (!path.empty()) {
                SetWindowTextW(g_hDllPath, path.c_str());
                g_DllPathStr = path;
                Log(L"[INFO] DLL selecionada.");
            }
        }

        if (id == ID_BTN_INJECT) {
            if (g_Injected) {
                Log(L"[AVISO] Ja foi injetado nesta sessao.");
                return 0;
            }

            // Get DLL path
            wchar_t pathBuf[MAX_PATH];
            GetWindowTextW(g_hDllPath, pathBuf, MAX_PATH);
            g_DllPathStr = pathBuf;

            if (g_DllPathStr.empty()) {
                Log(L"[ERRO] Selecione a DLL primeiro.");
                return 0;
            }

            if (!std::filesystem::exists(g_DllPathStr)) {
                Log(L"[ERRO] Arquivo DLL nao encontrado.");
                return 0;
            }

            // Find FiveM
            SetWindowTextW(g_hStatus, L"Procurando FiveM...");
            Log(L"[...] Procurando processo FiveM...");
            InvalidateRect(hWnd, nullptr, TRUE);
            UpdateWindow(hWnd);

            auto procs = FindFiveMProcesses();
            if (procs.empty()) {
                SetWindowTextW(g_hStatus, L"FiveM nao encontrado!");
                Log(L"[ERRO] FiveM_GTAProcess nao encontrado.");
                Log(L"[INFO] Abra o FiveM e entre num servidor primeiro.");
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }

            auto& target = procs[0];
            wchar_t logBuf[256];
            swprintf_s(logBuf, L"[OK] Processo encontrado: %s (PID %lu)",
                       target.name.c_str(), target.pid);
            Log(logBuf);

            // Inject
            SetWindowTextW(g_hStatus, L"Injetando...");
            Log(L"[...] Injetando DLL...");
            InvalidateRect(hWnd, nullptr, TRUE);
            UpdateWindow(hWnd);

            bool success = InjectDLL(target.pid, g_DllPathStr);

            if (success) {
                SetWindowTextW(g_hStatus, L"Injetado com sucesso!");
                Log(L"[OK] DLL injetada! Aperte F9 no jogo.");
                g_Injected = true;
            } else {
                SetWindowTextW(g_hStatus, L"Falha na injecao!");
                Log(L"[ERRO] Injecao falhou. Tente como admin.");
            }
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_DESTROY:
        DeleteObject(g_BgBrush);
        DeleteObject(g_PanelBrush);
        DeleteObject(g_EditBrush);
        DeleteObject(g_Font);
        DeleteObject(g_FontBold);
        DeleteObject(g_FontTitle);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- Entry point --------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"C4pt0rInjector";
    wc.hbrBackground = nullptr;  // we paint our own bg
    RegisterClassExW(&wc);

    int winW = 600, winH = 580;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExW(
        0, wc.lpszClassName, L"C4pt0r Cheats — Injector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screenW - winW) / 2, (screenH - winH) / 2,
        winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
