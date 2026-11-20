#include "AudioSourceComponent.h"
#include "../Clip/AudioAssetLibrary.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Random interno (xorshift32 — sem dep de <random> para evitar overhead)
// ---------------------------------------------------------------------------
float AudioSourceComponent::RandFloat() const
{
    m_RandState ^= m_RandState << 13;
    m_RandState ^= m_RandState >> 17;
    m_RandState ^= m_RandState << 5;
    // Mapeia uint32 → [-1, +1]
    return (static_cast<float>(m_RandState) / 2147483648.f) - 1.f;
}

// ---------------------------------------------------------------------------
// Resolve o clip path efetivo
// ---------------------------------------------------------------------------
std::string AudioSourceComponent::ResolveClipPath() const
{
    // 1. Por ID na library
    if (!ClipID.empty())
    {
        auto clip = AudioAssetLibrary::Get().FindByID(ClipID);
        if (clip && clip->ExistsOnDisk())
            return clip->GetPath();
    }
    // 2. Fallback por path direto
    return ClipPath;
}

// ---------------------------------------------------------------------------
// Próximo clip da playlist
// ---------------------------------------------------------------------------
std::string AudioSourceComponent::NextPlaylistClip()
{
    if (PlaylistIDs.empty()) return ResolveClipPath();

    if (PlaylistMode == AudioPlaylistMode::Shuffle)
    {
        // Índice aleatório
        float r = (RandFloat() + 1.f) * 0.5f; // [0, 1]
        PlaylistIndex = static_cast<int>(r * static_cast<float>(PlaylistIDs.size()));
        PlaylistIndex = std::clamp(PlaylistIndex, 0,
                                   static_cast<int>(PlaylistIDs.size()) - 1);
    }
    else
    {
        // Sequencial
        PlaylistIndex = PlaylistIndex % static_cast<int>(PlaylistIDs.size());
    }

    const std::string& id = PlaylistIDs[PlaylistIndex];
    ++PlaylistIndex;

    auto clip = AudioAssetLibrary::Get().FindByID(id);
    if (clip && clip->ExistsOnDisk()) return clip->GetPath();
    return id;  // fallback: trata como path
}

// ---------------------------------------------------------------------------
// Volume e pitch efetivos
// ---------------------------------------------------------------------------
float AudioSourceComponent::GetEffectiveVolume() const
{
    if (Mute) return 0.f;
    float rand = (RandomVolume > 0.f) ? (RandFloat() * RandomVolume) : 0.f;
    return std::clamp(Volume + rand, 0.f, 1.f);
}

float AudioSourceComponent::GetEffectivePitch() const
{
    float rand = (RandomPitch > 0.f) ? (RandFloat() * RandomPitch) : 0.f;
    return std::clamp(Pitch + rand, 0.1f, 4.f);
}
