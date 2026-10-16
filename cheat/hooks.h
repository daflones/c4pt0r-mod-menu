#pragma once
// ============================================================================
//  C4pt0r Cheats — D3D11 & Input Hooks
//  VTable hook on IDXGISwapChain::Present, WndProc subclass for input
// ============================================================================

#include "globals.h"

namespace C4pt0r::Hooks
{
    bool Initialize();
    void Shutdown();
}
