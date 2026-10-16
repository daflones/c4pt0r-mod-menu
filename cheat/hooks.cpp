// ============================================================================
//  C4pt0r Cheats — D3D11 Present Hook + WndProc + ImGui Bootstrap
//  Uses MinHook for inline detour (works even when FiveM wraps DXGI)
// ============================================================================

#include "hooks.h"
#include "globals.h"
#include "menu.h"
#include "features.h"
#include "logger.h"

// MinHook
#include "MinHook.h"

// ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi.h>

// Forward-decl from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace C4pt0r::Hooks
{
    // ---- Types ----------------------------------------------------------
    using PresentFn       = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    // ---- State ----------------------------------------------------------
    static PresentFn       oPresent        = nullptr;
    static ResizeBuffersFn oResizeBuffers  = nullptr;
    static WNDPROC         oWndProc        = nullptr;
    static bool            s_ImGuiInited   = false;
    static void*           s_PresentTarget = nullptr;
    static void*           s_ResizeTarget  = nullptr;

    // ---- WndProc hook ---------------------------------------------------
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Menu toggle
        if (msg == WM_KEYDOWN && wParam == static_cast<WPARAM>(g_Menu.toggleKey)) {
            g_Menu.isOpen = !g_Menu.isOpen;
            Log::Write("Menu toggled: %s", g_Menu.isOpen ? "OPEN" : "CLOSED");
            return 0;
        }

        // Panic key: END -> unload
        if (msg == WM_KEYDOWN && wParam == VK_END) {
            g_Running = false;
            return 0;
        }

        // Forward input to ImGui when menu is open
        if (g_Menu.isOpen && s_ImGuiInited) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
                return 0;
            // Block mouse from reaching the game while menu is open
            if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
                return 0;
            // Block keyboard (except our toggle/panic keys already handled above)
            if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR ||
                msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
                return 0;
            // Block raw input
            if (msg == WM_INPUT)
                return 0;
        }

        return CallWindowProcW(oWndProc, hWnd, msg, wParam, lParam);
    }

    // ---- ResizeBuffers hook ---------------------------------------------
    static HRESULT __stdcall HookedResizeBuffers(
        IDXGISwapChain* sc, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
    {
        Log::Write("ResizeBuffers called: %ux%u", width, height);

        // Only touch RTV if Present already bound this swapchain —
        // ResizeBuffers can fire before the first Present, when
        // g_Device/g_SwapChain are still null → was a guaranteed AV
        bool ours = s_ImGuiInited && sc == g_SwapChain && g_Device;
        if (ours && g_RenderTarget) {
            g_RenderTarget->Release();
            g_RenderTarget = nullptr;
        }

        HRESULT hr = oResizeBuffers(sc, bufferCount, width, height, format, flags);

        if (ours) {
            ID3D11Texture2D* backBuf = nullptr;
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuf));
            if (backBuf) {
                g_Device->CreateRenderTargetView(backBuf, nullptr, &g_RenderTarget);
                backBuf->Release();
            }
        }

        g_ScreenWidth  = static_cast<int>(width);
        g_ScreenHeight = static_cast<int>(height);

        return hr;
    }

    // ---- Present hook (main render entry) -------------------------------
    static HRESULT __stdcall HookedPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags)
    {
        // First-time ImGui init
        if (!s_ImGuiInited) {
            Log::Write("Present hooked! First call - initializing ImGui...");

            if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_Device)))) {
                Log::Write("ERROR: GetDevice failed");
                return oPresent(sc, syncInterval, flags);
            }
            g_Device->GetImmediateContext(&g_DeviceContext);
            g_SwapChain = sc;

            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            g_GameWindow   = desc.OutputWindow;
            g_ScreenWidth  = static_cast<int>(desc.BufferDesc.Width);
            g_ScreenHeight = static_cast<int>(desc.BufferDesc.Height);

            Log::Write("SwapChain: %ux%u, HWND=0x%p", g_ScreenWidth, g_ScreenHeight, g_GameWindow);

            // Back-buffer render target
            ID3D11Texture2D* backBuf = nullptr;
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuf));
            if (!backBuf) {
                Log::Write("ERROR: GetBuffer failed");
                return oPresent(sc, syncInterval, flags);
            }
            g_Device->CreateRenderTargetView(backBuf, nullptr, &g_RenderTarget);
            backBuf->Release();

            // ImGui
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            ImGui_ImplWin32_Init(g_GameWindow);
            ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

            Menu::SetupStyle();

            // Subclass WndProc
            oWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(HookedWndProc)));

            s_ImGuiInited = true;
            Log::Write("ImGui initialized. Menu ready (F9 to toggle).");
        }

        // Only render on the swapchain we initialized against — FiveM runs
        // extra swapchains (CEF overlays etc.) and binding our RTV with a
        // foreign device context corrupts the pipeline state
        if (sc != g_SwapChain || !g_DeviceContext || !g_RenderTarget)
            return oPresent(sc, syncInterval, flags);

        // ---- Render frame with SEH protection ----
        __try {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Cursor control: show/hide based on menu state
            ImGuiIO& io = ImGui::GetIO();
            if (g_Menu.isOpen) {
                io.MouseDrawCursor = true;
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            } else {
                io.MouseDrawCursor = false;
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            }

            // Watermark
            if (g_Menu.showWatermark && !g_Menu.isOpen) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.35f);
                ImGui::Begin("##watermark", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
                ImGui::TextColored(ImVec4(0.0f, 0.83f, 1.0f, 1.0f), "C4pt0r Cheats");
                ImGui::SameLine();
                ImGui::TextDisabled("| F9 Menu");
                ImGui::End();
            }

            // Menu
            if (g_Menu.isOpen)
                Menu::Render();

            // ESP overlay (always, even when menu closed)
            Features::RenderOverlay();

            // ---- Finish frame ----
            ImGui::EndFrame();
            ImGui::Render();
            g_DeviceContext->OMSetRenderTargets(1, &g_RenderTarget, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("EXCEPTION in render frame! Code: 0x%08X", GetExceptionCode());
        }

        return oPresent(sc, syncInterval, flags);
    }

    // ---- Public: Initialize ---------------------------------------------
    bool Initialize()
    {
        Log::Write("Hooks::Initialize() starting...");

        // ---- Create dummy swap chain to grab VTable addresses ----
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance      = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"C4PT0R_DUMMY";
        RegisterClassExW(&wc);

        HWND hDummy = CreateWindowExW(
            0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount       = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow      = hDummy;
        sd.SampleDesc.Count  = 1;
        sd.Windowed          = TRUE;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device*        tmpDevice  = nullptr;
        IDXGISwapChain*      tmpSwap    = nullptr;
        ID3D11DeviceContext* tmpCtx     = nullptr;
        D3D_FEATURE_LEVEL    featureLevel;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &tmpSwap, &tmpDevice, &featureLevel, &tmpCtx);

        if (FAILED(hr)) {
            Log::Write("ERROR: D3D11CreateDeviceAndSwapChain failed (0x%08X)", hr);
            DestroyWindow(hDummy);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        // Get function addresses from VTable
        void** vtable = *reinterpret_cast<void***>(tmpSwap);
        s_PresentTarget = vtable[8];   // IDXGISwapChain::Present
        s_ResizeTarget  = vtable[13];  // IDXGISwapChain::ResizeBuffers

        Log::Write("Present address: 0x%p", s_PresentTarget);
        Log::Write("ResizeBuffers address: 0x%p", s_ResizeTarget);

        // Release dummy
        tmpSwap->Release();
        tmpDevice->Release();
        tmpCtx->Release();
        DestroyWindow(hDummy);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);

        // ---- MinHook: detour the actual function code ----
        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_Initialize failed (%d)", mhStatus);
            return false;
        }
        Log::Write("MinHook initialized");

        // Hook Present
        mhStatus = MH_CreateHook(s_PresentTarget,
                                 reinterpret_cast<void*>(&HookedPresent),
                                 reinterpret_cast<void**>(&oPresent));
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_CreateHook(Present) failed (%d)", mhStatus);
            return false;
        }
        Log::Write("Present hook created");

        // Hook ResizeBuffers
        mhStatus = MH_CreateHook(s_ResizeTarget,
                                 reinterpret_cast<void*>(&HookedResizeBuffers),
                                 reinterpret_cast<void**>(&oResizeBuffers));
        if (mhStatus != MH_OK) {
            Log::Write("WARNING: MH_CreateHook(ResizeBuffers) failed (%d) - non-critical", mhStatus);
            // Non-critical, continue
        } else {
            Log::Write("ResizeBuffers hook created");
        }

        // Enable all hooks
        mhStatus = MH_EnableHook(MH_ALL_HOOKS);
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_EnableHook failed (%d)", mhStatus);
            return false;
        }

        Log::Write("All hooks enabled. Waiting for first Present call...");
        return true;
    }

    // ---- Public: Shutdown -----------------------------------------------
    void Shutdown()
    {
        Log::Write("Hooks::Shutdown() starting...");

        // Disable and remove all MinHook hooks
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        // Restore WndProc
        if (oWndProc && g_GameWindow) {
            SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(oWndProc));
        }

        // Tear down ImGui
        if (s_ImGuiInited) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        // Release render target
        if (g_RenderTarget) { g_RenderTarget->Release(); g_RenderTarget = nullptr; }

        Log::Write("Hooks cleaned up.");
    }
}
