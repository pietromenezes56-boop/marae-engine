#pragma once
// ---------------------------------------------------------------------------
// AudioEnvironmentManager — detecta qual zona o listener está e aplica
// os parâmetros acústicos com transição suave entre zonas.
// ---------------------------------------------------------------------------
#include "AudioEnvironmentComponent.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>

// Zona registrada no manager (componente + bounds da entidade)
struct AudioEnvironmentZone
{
    uint64_t EntityID = 0;
    glm::vec3 Center  = glm::vec3(0.f);
    glm::vec3 Size    = glm::vec3(10.f);  // half-extents do AABB
    AudioEnvironmentComponent* Component = nullptr;
};

// Estado acústico atual (blend de até 2 zonas)
struct AcousticState
{
    AudioReverbParams Reverb;
    float LPFCutoff       = 22000.f;
    float HPFCutoff       = 20.f;
    float VolumeScale     = 1.0f;
    float OcclusionAmount = 0.0f;
    float BlendAlpha      = 0.0f;   // 1 = totalmente na zona atual
};

class AudioEnvironmentManager
{
public:
    static AudioEnvironmentManager& Get();

    // Registra e remove zonas
    void RegisterZone(const AudioEnvironmentZone& zone);
    void UnregisterZone(uint64_t entityID);

    // Atualiza o estado acústico com base na posição do listener
    // Deve ser chamado por frame pelo AudioSystem
    void Update(const glm::vec3& listenerPos, float deltaTime);

    // Estado acústico atual (interpolado entre zonas)
    const AcousticState& GetCurrentState() const { return m_Current; }

    // Notifica occlusion externa (calculada pelo AudioSystem via raycasting)
    void SetOcclusion(float amount) { m_ExternalOcclusion = amount; }

private:
    AudioEnvironmentManager() = default;

    std::vector<AudioEnvironmentZone> m_Zones;
    AcousticState m_Current;
    AcousticState m_Target;
    float m_TransitionTime  = 1.5f;
    float m_TransitionTimer = 0.f;
    float m_ExternalOcclusion = 0.f;

    // Verifica se um ponto está dentro do AABB da zona
    bool IsInsideZone(const AudioEnvironmentZone& zone,
                      const glm::vec3& point) const;

    void BlendState(float alpha, float deltaTime);
};
