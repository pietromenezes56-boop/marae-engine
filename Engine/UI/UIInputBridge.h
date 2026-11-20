#pragma once
// ---------------------------------------------------------------------------
// UIInputBridge — ponte entre a entrada do editor (ImGui IO) e o sistema de
// interação da UI 2D (UIInteraction).
//
// Converte o estado do mouse/teclado do ImGui em um UIInputState no espaço do
// canvas de referência (960x540) e executa MaraeUI::UpdateInteraction.
// O resultado deve ser lido pelo orquestrador (Application), que também
// despacha os eventos de script (click/changed) para o ScriptingRuntime.
// ---------------------------------------------------------------------------

#include "UIInteraction.h"

class Scene;

namespace MaraeUI
{
    struct UIInputBridgeParams
    {
        // Interação só roda em Play ou no modo jogo empacotado. No editor
        // (modo Edit) o overlay do viewport cuida de seleção/arraste.
        bool InteractionEnabled = false;
        bool RuntimeGame = false;

        // Retângulo (em pixels de tela) onde a imagem do jogo é exibida no
        // modo runtime; usado para mapear o ponteiro para o canvas da UI.
        float GameImageMinX = 0.0f;
        float GameImageMinY = 0.0f;
        float GameImageWidth = 0.0f;
        float GameImageHeight = 0.0f;
        bool GameImageValid = false;

        // Fallback: ponteiro sobre o viewport do editor.
        float ViewportPointerX = 0.0f;
        float ViewportPointerY = 0.0f;
        int ViewportWidth = 0;
        int ViewportHeight = 0;
        bool ViewportPointerValid = false;
    };

    // Retorna true quando a interação foi processada neste frame e o resultado
    // está preenchido. Retorna false (resultado zerado) quando desabilitada ou
    // sem contexto ImGui.
    bool UpdateInteractionFromImGui(Scene& scene,
                                    const UIInputBridgeParams& params,
                                    UIInteractionResult& result);
}
