#pragma once
// ---------------------------------------------------------------------------
// DemoGameContext — contexto compartilhado dos jogos demo embutidos
// (corrida e sobrevivência).
//
// Reúne apenas o que os jogos precisam da engine por frame, sem expor a
// classe Application. Os jogos demo são conteúdo de exemplo que roda sobre
// o runtime: detectam entidades convencionais na cena (PlayerCar, AI_Car_*,
// SurvivalPlayer, Toolbox) e tomam o controle da câmera durante o Play.
// ---------------------------------------------------------------------------

#include <string>

class Scene;
class Window;
class Camera;

struct DemoGameContext
{
    Scene& Scene;
    Window& Window;
    Camera& FlyCamera;                    // câmera fly do editor (usada como câmera do jogo)
    const std::string& ProjectPath;       // raiz do projeto para leitura/escrita de Save/
    bool EditMode = false;                // true quando o editor está em modo Edit (não Play)
};
