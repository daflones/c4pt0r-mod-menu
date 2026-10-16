#pragma once
// ============================================================================
//  C4pt0r Cheats — Feature Logic
// ============================================================================

namespace C4pt0r::Features
{
    // Called every frame from the main cheat thread (has native access)
    void Tick();

    // Called every frame from the Present hook (ImGui draw list available)
    void RenderOverlay();
}
