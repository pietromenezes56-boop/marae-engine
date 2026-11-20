#include "AudioMixerGroup.h"
#include <cmath>
#include <algorithm>

AudioMixerGroup::AudioMixerGroup(const std::string& id,
                                 const std::string& name,
                                 const std::string& parentID)
    : m_ID(id), m_Name(name), m_ParentID(parentID)
{}

// ---------------------------------------------------------------------------
// Volume em dB
// ---------------------------------------------------------------------------
float AudioMixerGroup::GetVolumeDB() const
{
    if (m_Volume <= 0.f) return -96.f;   // silêncio = -96 dB
    return 20.f * std::log10(m_Volume);
}

void AudioMixerGroup::SetVolumeDB(float db)
{
    m_Volume = std::clamp(std::pow(10.f, db / 20.f), 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Fade
// ---------------------------------------------------------------------------
void AudioMixerGroup::FadeTo(float targetVolume, float durationSeconds)
{
    m_Fade.StartVolume  = m_Volume;
    m_Fade.TargetVolume = std::clamp(targetVolume, 0.f, 1.f);
    m_Fade.Duration     = std::max(durationSeconds, 0.001f);
    m_Fade.Elapsed      = 0.f;
    m_Fade.Active       = true;
}

void AudioMixerGroup::UpdateFade(float deltaTime)
{
    if (!m_Fade.Active) return;

    m_Fade.Elapsed += deltaTime;
    float t = std::clamp(m_Fade.Elapsed / m_Fade.Duration, 0.f, 1.f);

    // Interpolação suave com ease-in-out
    float smooth = t * t * (3.f - 2.f * t);
    m_Volume = m_Fade.StartVolume + (m_Fade.TargetVolume - m_Fade.StartVolume) * smooth;

    if (t >= 1.f)
    {
        m_Volume       = m_Fade.TargetVolume;
        m_Fade.Active  = false;
    }
}

// ---------------------------------------------------------------------------
// Ducking
// ---------------------------------------------------------------------------
void AudioMixerGroup::SetDucked(bool ducked)
{
    m_Ducked = ducked;
}

void AudioMixerGroup::UpdateDucking(float deltaTime)
{
    if (!m_Ducking.Enabled) { m_DuckVolume = 1.f; return; }

    float target = m_Ducked ? m_Ducking.DuckTo : 1.f;
    float speed  = m_Ducked
                     ? (1.f / std::max(m_Ducking.AttackTime, 0.001f))
                     : (1.f / std::max(m_Ducking.ReleaseTime, 0.001f));

    float diff = target - m_DuckVolume;
    if (std::abs(diff) < 0.0001f)
        m_DuckVolume = target;
    else
        m_DuckVolume += diff * speed * deltaTime;

    m_DuckVolume = std::clamp(m_DuckVolume, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Volume final aplicado
// ---------------------------------------------------------------------------
float AudioMixerGroup::GetAppliedVolume() const
{
    if (m_Muted) return 0.f;
    return m_Volume * m_DuckVolume;
}
