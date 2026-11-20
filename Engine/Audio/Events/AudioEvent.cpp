#include "AudioEvent.h"
#include "../Clip/AudioAssetLibrary.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// ---------------------------------------------------------------------------
// Rand interno xorshift32
// ---------------------------------------------------------------------------
float AudioEvent::Rand01() const
{
    RandState ^= RandState << 13;
    RandState ^= RandState >> 17;
    RandState ^= RandState << 5;
    return static_cast<float>(RandState) / 4294967296.f;
}

// ---------------------------------------------------------------------------
// Cooldown
// ---------------------------------------------------------------------------
bool AudioEvent::IsOnCooldown(float currentTime) const
{
    return (CooldownSeconds > 0.f) &&
           ((currentTime - LastTriggerTime) < CooldownSeconds);
}

// ---------------------------------------------------------------------------
// Resolução de clip
// ---------------------------------------------------------------------------
std::string AudioEvent::ResolveClip() const
{
    if (Clips.empty()) return "";

    int chosen = 0;

    if (Selection == AudioEventClipSelection::Sequential)
    {
        LastClipIndex = (LastClipIndex + 1) % static_cast<int>(Clips.size());
        chosen = LastClipIndex;
    }
    else if (Selection == AudioEventClipSelection::RandomNoRepeat)
    {
        // Aleatório mas evita o índice anterior
        if (Clips.size() == 1)
        {
            chosen = 0;
        }
        else
        {
            do {
                float r = Rand01();
                chosen = std::clamp(static_cast<int>(r * Clips.size()),
                                    0, (int)Clips.size() - 1);
            } while (chosen == LastClipIndex);
        }
        LastClipIndex = chosen;
    }
    else
    {
        // Aleatório ponderado
        float totalWeight = 0.f;
        for (const auto& c : Clips) totalWeight += std::max(c.Weight, 0.001f);

        float pick = Rand01() * totalWeight;
        float acc  = 0.f;
        for (int i = 0; i < (int)Clips.size(); ++i)
        {
            acc += std::max(Clips[i].Weight, 0.001f);
            if (pick <= acc) { chosen = i; break; }
        }
        LastClipIndex = chosen;
    }

    const AudioEventClipEntry& entry = Clips[chosen];

    // Tenta resolver pelo ID
    if (!entry.ClipID.empty())
    {
        auto clip = AudioAssetLibrary::Get().FindByID(entry.ClipID);
        if (clip && clip->ExistsOnDisk()) return clip->GetPath();
    }
    return entry.ClipPath;
}

float AudioEvent::ResolveVolume() const
{
    if (VolumeMin >= VolumeMax) return VolumeMin;
    return VolumeMin + Rand01() * (VolumeMax - VolumeMin);
}

float AudioEvent::ResolvePitch() const
{
    if (PitchMin >= PitchMax) return PitchMin;
    return PitchMin + Rand01() * (PitchMax - PitchMin);
}
