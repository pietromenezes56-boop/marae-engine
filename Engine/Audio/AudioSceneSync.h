#pragma once
// ---------------------------------------------------------------------------
// AudioSceneSync — sincroniza as fontes de áudio da cena com as voices do
// AudioSystem, uma vez por frame.
//
// Responsabilidades:
//   • Derivar o listener 3D a partir da câmera do viewport (posição,
//     orientação e velocidade para Doppler).
//   • Iniciar uma única vez as fontes com PlayOnStart durante o Play.
//   • Parar voices de fontes desativadas/removidas e atualizar posição,
//     velocidade e mix acústico das voices ativas.
//   • Dirigir AudioSystem::Update(deltaTime).
//
// O estado de "fontes já iniciadas" é interno: Reset() deve ser chamado nas
// transições de Play para que PlayOnStart toque novamente na próxima sessão.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_set>

class Scene;
class Camera;

class AudioSceneSync
{
public:
    // gameplayActive deve ser verdadeiro apenas em Play/runtime do jogo.
    // projectRoot é a raiz do projeto para resolver ClipPath relativos.
    void Update(Scene& scene,
                const Camera& listenerCamera,
                float deltaTime,
                bool gameplayActive,
                const std::string& projectRoot);

    void Reset();

private:
    std::unordered_set<uint64_t> m_StartedEntities;
    glm::vec3 m_PrevListenerPosition = glm::vec3(0.0f);
    bool m_HasListenerPosition = false;
};
