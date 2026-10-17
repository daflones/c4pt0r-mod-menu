#pragma once
// ============================================================================
//  C4pt0r Cheats — Feature Logic
// ============================================================================

namespace C4pt0r::Features
{
    // Chamada pelo hook do Present (thread de render do JOGO — o anticheat
    // nao pode suspender sem congelar tudo) E pela feature thread como
    // backup. Throttle interno + gate atomico: so um driver por vez.
    // Contem Tick + watchdog de natives + recovery.
    void FrameTick();

    // Called every frame from the main cheat thread (has native access)
    void Tick();

    // Called every frame from the Present hook (ImGui draw list available)
    void RenderOverlay();

    // Restaura a entidade ao normal MANTENDO os toggles — usado pelo
    // watchdog de natives (recovery invisivel, features voltam sozinhas).
    void RestoreEntity();

    // Restaura o ped ao estado normal (visivel, colisao, gravidade,
    // invencivel off, velocidade zerada) e ZERA todos os toggles —
    // unload e botao "Restaurar tudo".
    void RestoreAll();
}
