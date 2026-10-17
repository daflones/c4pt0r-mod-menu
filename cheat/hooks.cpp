

#include "hooks.h"
#include "globals.h"
#include "menu.h"
#include "features.h"
#include "logger.h"

#include "MinHook.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace C4pt0r::Hooks
{

    using PresentFn       = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    static PresentFn       oPresent        = nullptr;
    static ResizeBuffersFn oResizeBuffers  = nullptr;
    static bool            s_ImGuiInited   = false;
    static void*           s_PresentTarget = nullptr;
    static void*           s_ResizeTarget  = nullptr;

    static bool s_KeyDown[256] = {};

    static bool KeyPressed(int vk)
    {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = down && !s_KeyDown[vk & 0xFF];
        s_KeyDown[vk & 0xFF] = down;
        return pressed;
    }

    static void FeedImGuiInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        POINT pt{};
        GetCursorPos(&pt);
        if (g_GameWindow) ScreenToClient(g_GameWindow, &pt);
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(static_cast<float>(pt.x), static_cast<float>(pt.y));
        io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
    }

    static HRESULT __stdcall HookedResizeBuffers(
        IDXGISwapChain* sc, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
    {
        Log::Write("ResizeBuffers called: %ux%u", width, height);

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

    static HRESULT __stdcall HookedPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags)
    {

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

            ID3D11Texture2D* backBuf = nullptr;
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuf));
            if (!backBuf) {
                Log::Write("ERROR: GetBuffer failed");
                return oPresent(sc, syncInterval, flags);
            }
            g_Device->CreateRenderTargetView(backBuf, nullptr, &g_RenderTarget);
            backBuf->Release();

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            ImGui_ImplWin32_Init(g_GameWindow);
            ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

            Menu::SetupStyle();

            s_ImGuiInited = true;
            Log::Write("ImGui initialized. Menu ready (F9 to toggle).");
        }

        if (sc != g_SwapChain || !g_DeviceContext || !g_RenderTarget)
            return oPresent(sc, syncInterval, flags);

        if (KeyPressed(g_Menu.toggleKey)) {
            g_Menu.isOpen = !g_Menu.isOpen;
            Log::Write("Menu toggled: %s", g_Menu.isOpen ? "OPEN" : "CLOSED");
        }
        if (KeyPressed(VK_END)) {
            Log::Write("Panic key pressed — unloading");
            g_Running = false;
            return oPresent(sc, syncInterval, flags);
        }

        __try {

            Features::FrameTick();
            FeedImGuiInput();

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& io = ImGui::GetIO();
            if (g_Menu.isOpen) {
                io.MouseDrawCursor = true;
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            } else {
                io.MouseDrawCursor = false;
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            }

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

            if (g_Menu.isOpen)
                Menu::Render();

            Features::RenderOverlay();

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

    bool Initialize()
    {
        Log::Write("Hooks::Initialize() starting...");

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

        void** vtable = *reinterpret_cast<void***>(tmpSwap);
        s_PresentTarget = vtable[8];
        s_ResizeTarget  = vtable[13];

        Log::Write("Present address: 0x%p", s_PresentTarget);
        Log::Write("ResizeBuffers address: 0x%p", s_ResizeTarget);

        tmpSwap->Release();
        tmpDevice->Release();
        tmpCtx->Release();
        DestroyWindow(hDummy);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);

        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_Initialize failed (%d)", mhStatus);
            return false;
        }
        Log::Write("MinHook initialized");

        mhStatus = MH_CreateHook(s_PresentTarget,
                                 reinterpret_cast<void*>(&HookedPresent),
                                 reinterpret_cast<void**>(&oPresent));
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_CreateHook(Present) failed (%d)", mhStatus);
            return false;
        }
        Log::Write("Present hook created");

        mhStatus = MH_CreateHook(s_ResizeTarget,
                                 reinterpret_cast<void*>(&HookedResizeBuffers),
                                 reinterpret_cast<void**>(&oResizeBuffers));
        if (mhStatus != MH_OK) {
            Log::Write("WARNING: MH_CreateHook(ResizeBuffers) failed (%d) - non-critical", mhStatus);

        } else {
            Log::Write("ResizeBuffers hook created");
        }

        mhStatus = MH_EnableHook(MH_ALL_HOOKS);
        if (mhStatus != MH_OK) {
            Log::Write("ERROR: MH_EnableHook failed (%d)", mhStatus);
            return false;
        }

        Log::Write("All hooks enabled. Waiting for first Present call...");
        return true;
    }

    void Shutdown()
    {
        Log::Write("Hooks::Shutdown() starting...");

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        if (s_ImGuiInited) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        if (g_RenderTarget) { g_RenderTarget->Release(); g_RenderTarget = nullptr; }

        Log::Write("Hooks cleaned up.");
    }
}
