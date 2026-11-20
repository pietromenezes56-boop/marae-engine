#include "AudioEnvironmentManager.h"
#include <algorithm>
#include <cmath>

AudioEnvironmentManager& AudioEnvironmentManager::Get()
{
    static AudioEnvironmentManager instance;
    return instance;
}

void AudioEnvironmentManager::RegisterZone(const AudioEnvironmentZone& zone)
{
    // Remove duplicata pelo EntityID
    UnregisterZone(zone.EntityID);
    m_Zones.push_back(zone);
}

void AudioEnvironmentManager::UnregisterZone(uint64_t entityID)
{
    m_Zones.erase(
        std::remove_if(m_Zones.begin(), m_Zones.end(),
            [&](const AudioEnvironmentZone& z){ return z.EntityID == entityID; }),
        m_Zones.end());
}

bool AudioEnvironmentManager::IsInsideZone(const AudioEnvironmentZone& zone,
                                            const glm::vec3& point) const
{
    return std::abs(point.x - zone.Center.x) <= zone.Size.x &&
           std::abs(point.y - zone.Center.y) <= zone.Size.y &&
           std::abs(point.z - zone.Center.z) <= zone.Size.z;
}

void AudioEnvironmentManager::Update(const glm::vec3& listenerPos, float deltaTime)
{
    // Encontra a zona de maior prioridade que contém o listener
    const AudioEnvironmentZone* best = nullptr;
    int bestPriority = -999999;

    for (const auto& zone : m_Zones)
    {
        if (!zone.Component || !zone.Component->Enabled) continue;
        if (!IsInsideZone(zone, listenerPos)) continue;
        if (zone.Component->Priority > bestPriority)
        {
            best = &zone;
            bestPriority = zone.Component->Priority;
        }
    }

    // Monta o estado alvo
    if (best && best->Component->ReverbEnabled)
    {
        const AudioEnvironmentComponent* env = best->Component;
        m_Target.Reverb         = env->Reverb;
        m_Target.LPFCutoff      = env->LPFEnabled  ? env->LPFCutoffHz : 22000.f;
        m_Target.HPFCutoff      = env->HPFEnabled  ? env->HPFCutoffHz : 20.f;
        m_Target.VolumeScale    = env->VolumeScale;
        m_Target.OcclusionAmount= m_ExternalOcclusion;
        m_TransitionTime        = env->TransitionTime;
    }
    else
    {
        // Sem zona → valores neutros
        m_Target.Reverb         = AudioReverbParams();
        m_Target.Reverb.WetLevel= 0.f;
        m_Target.LPFCutoff      = 22000.f;
        m_Target.HPFCutoff      = 20.f;
        m_Target.VolumeScale    = 1.f;
        m_Target.OcclusionAmount= m_ExternalOcclusion;
        m_TransitionTime        = 1.0f;
    }

    // Interpola o estado atual em direção ao alvo
    BlendState(deltaTime / std::max(m_TransitionTime, 0.001f), deltaTime);
}

void AudioEnvironmentManager::BlendState(float alpha, float /*deltaTime*/)
{
    float t = std::clamp(alpha, 0.f, 1.f);
    // Smooth step
    float s = t * t * (3.f - 2.f * t);

    auto Lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    m_Current.Reverb.WetLevel    = Lerp(m_Current.Reverb.WetLevel,    m_Target.Reverb.WetLevel,    s);
    m_Current.Reverb.DryLevel    = Lerp(m_Current.Reverb.DryLevel,    m_Target.Reverb.DryLevel,    s);
    m_Current.Reverb.RoomSize    = Lerp(m_Current.Reverb.RoomSize,    m_Target.Reverb.RoomSize,    s);
    m_Current.Reverb.DecayTime   = Lerp(m_Current.Reverb.DecayTime,   m_Target.Reverb.DecayTime,   s);
    m_Current.Reverb.Density     = Lerp(m_Current.Reverb.Density,     m_Target.Reverb.Density,     s);
    m_Current.Reverb.Diffusion   = Lerp(m_Current.Reverb.Diffusion,   m_Target.Reverb.Diffusion,   s);
    m_Current.Reverb.Damping     = Lerp(m_Current.Reverb.Damping,     m_Target.Reverb.Damping,     s);
    m_Current.Reverb.PreDelayMs  = Lerp(m_Current.Reverb.PreDelayMs,  m_Target.Reverb.PreDelayMs,  s);
    m_Current.Reverb.LPFCutoff   = Lerp(m_Current.Reverb.LPFCutoff,  m_Target.Reverb.LPFCutoff,   s);
    m_Current.LPFCutoff          = Lerp(m_Current.LPFCutoff,          m_Target.LPFCutoff,           s);
    m_Current.HPFCutoff          = Lerp(m_Current.HPFCutoff,          m_Target.HPFCutoff,           s);
    m_Current.VolumeScale        = Lerp(m_Current.VolumeScale,        m_Target.VolumeScale,         s);
    m_Current.OcclusionAmount    = Lerp(m_Current.OcclusionAmount,     m_Target.OcclusionAmount,     s);
}
