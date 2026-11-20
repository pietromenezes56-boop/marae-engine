#pragma once
// ---------------------------------------------------------------------------
// RaceGame — modo corrida embutido (Marae Speedway / Apex Cup).
//
// Ativado automaticamente quando a cena contém uma entidade "PlayerCar"
// (carros "AI_Car_*" opcionais viram tráfego de IA). Durante o Play o jogo
// assume o controle do veículo do jogador, das IA, da câmera de perseguição
// e desenha o HUD de corrida.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

struct DemoGameContext;

class RaceGame
{
public:
    bool IsActive() const { return m_Active; }

    void Reset();

    // Varre a cena atrás de PlayerCar/AI_Car_* e ativa o modo se houver player.
    void ActivateFromScene(class Scene& scene);

    void Update(const DemoGameContext& ctx, float deltaTime);
    void DrawHUD(const DemoGameContext& ctx);

private:
    bool m_Active = false;
    bool m_Finished = false;
    uint64_t m_PlayerID = 0;
    std::vector<uint64_t> m_AICarIDs;
    float m_Elapsed = 0.0f;
    int m_Lap = 1;
    int m_TotalLaps = 3;
    int m_NextCheckpoint = 0;
};
