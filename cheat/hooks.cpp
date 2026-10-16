// ============================================================================
//  C4pt0r Cheats — D3D11 Present Hook + WndProc + ImGui Bootstrap
//  Uses dummy SwapChain to grab the VTable, then patches Present in-place
// ============================================================================

#include "hooks.h"
#include "globals.h"
#include "menu.h"
#include "features.h"

// ---- ImGui (user must place these in deps/imgui/) -----------------------
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
    static PresentFn       s_OriginalPresent       = nullptr;
    static ResizeBuffersFn s_OriginalResizeBuffers  = nullptr;
    static WNDPROC         s_OriginalWndProc        = nullptr;
    static bool            s_ImGuiInited            = false;
    static void**          s_SwapChainVTable        = nullptr;

    // ---- VTable helpers -------------------------------------------------
    static void PatchVTable(void** vtable, int index, void* hook, void** original) {
        *original = vtable[index];
        DWORD old;
        VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old);
        vtable[index] = hook;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
    }
    static void RestoreVTable(void** vtable, int index, void* original) {
        DWORD old;
        VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old);
        vtable[index] = original;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
    }

    // ---- WndProc hook ---------------------------------------------------
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // Menu toggle
        if (msg == WM_KEYDOWN && wParam == static_cast<WPARAM>(g_Menu.toggleKey)) {
            g_Menu.isOpen = !g_Menu.isOpen;
            return 0;
        }

        // Panic key: END → unload
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
        }

        return CallWindowProcW(s_OriginalWndProc, hWnd, msg, wParam, lParam);
    }

    // ---- ResizeBuffers hook (recreate render target on window resize) ----
    static HRESULT __stdcall HookedResizeBuffers(
        IDXGISwapChain* sc, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
    {
        // Release old render target
        if (g_RenderTarget) { g_RenderTarget->Release(); g_RenderTarget = nullptr; }

        // Call original
        HRESULT hr = s_OriginalResizeBuffers(sc, bufferCount, width, height, format, flags);

        // Recreate render target
        ID3D11Texture2D* backBuf = nullptr;
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuf));
        if (backBuf) {
            g_Device->CreateRenderTargetView(backBuf, nullptr, &g_RenderTarget);
            backBuf->Release();
        }

        // Update screen size
        g_ScreenWidth  = static_cast<int>(width);
        g_ScreenHeight = static_cast<int>(height);

        return hr;
    }

    // ---- Present hook (main render entry) -------------------------------
    static HRESULT __stdcall HookedPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags) {
        // First-time ImGui init
        if (!s_ImGuiInited) {
            sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_Device));
            g_Device->GetImmediateContext(&g_DeviceContext);
            g_SwapChain = sc;

            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            g_GameWindow  = desc.OutputWindow;
            g_ScreenWidth  = static_cast<int>(desc.BufferDesc.Width);
            g_ScreenHeight = static_cast<int>(desc.BufferDesc.Height);

            // Back-buffer render target
            ID3D11Texture2D* backBuf = nullptr;
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuf));
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
            s_OriginalWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(HookedWndProc)));

            s_ImGuiInited = true;
        }

        // ---- New frame ----
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Watermark
        if (g_Menu.showWatermark) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("##watermark", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
            ImGui::TextColored(ImVec4(0.0f, 0.83f, 1.0f, 1.0f), "C4pt0r Cheats");
            ImGui::SameLine();
            ImGui::TextDisabled("| F9 Menu | END Unload");
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

        return s_OriginalPresent(sc, syncInterval, flags);
    }

    // ---- Public: Initialize ---------------------------------------------
    bool Initialize()
    {
        // ---- Create dummy swap chain to grab VTable ----
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
            DestroyWindow(hDummy);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            OutputDebugStringA("[C4pt0r] Failed to create dummy D3D11 device.\n");
            return false;
        }

        // Grab VTable
        s_SwapChainVTable = *reinterpret_cast<void***>(tmpSwap);

        // Clean up dummy
        tmpSwap->Release();
        tmpDevice->Release();
        tmpCtx->Release();
        DestroyWindow(hDummy);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);

        // ---- Hook Present (index 8) and ResizeBuffers (index 13) --------
        PatchVTable(s_SwapChainVTable, 8,
                    reinterpret_cast<void*>(HookedPresent),
                    reinterpret_cast<void**>(&s_OriginalPresent));
        PatchVTable(s_SwapChainVTable, 13,
                    reinterpret_cast<void*>(HookedResizeBuffers),
                    reinterpret_cast<void**>(&s_OriginalResizeBuffers));

        OutputDebugStringA("[C4pt0r] D3D11 hooks installed.\n");
        return true;
    }

    // ---- Public: Shutdown -----------------------------------------------
    void Shutdown()
    {
        // Restore VTable
        if (s_SwapChainVTable) {
            if (s_OriginalPresent)
                RestoreVTable(s_SwapChainVTable, 8, reinterpret_cast<void*>(s_OriginalPresent));
            if (s_OriginalResizeBuffers)
                RestoreVTable(s_SwapChainVTable, 13, reinterpret_cast<void*>(s_OriginalResizeBuffers));
        }

        // Restore WndProc
        if (s_OriginalWndProc && g_GameWindow) {
            SetWindowLongPtrW(g_GameWindow, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(s_OriginalWndProc));
        }

        // Tear down ImGui
        if (s_ImGuiInited) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        // Release render target
        if (g_RenderTarget) { g_RenderTarget->Release(); g_RenderTarget = nullptr; }

        OutputDebugStringA("[C4pt0r] Hooks cleaned up.\n");
    }
}
