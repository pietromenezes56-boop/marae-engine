#pragma once
// ---------------------------------------------------------------------------
// AudioEvent — representa uma ação de áudio acionada por nome.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "../AudioTypes.h"
#include "../Clip/AudioClip.h"
#include "../Mixer/AudioMixer.h"

// Seleção de clip dentro do evento
enum class AudioEventClipSelection : uint8_t
{
    Random      = 0,   // escolhe aleatoriamente entre os clips
    Sequential  = 1,   // toca em ordem circular
    RandomNoRepeat = 2 // aleatório sem repetir o último
};

// Uma entrada de clip no evento (com peso para controlar probabilidade)
struct AudioEventClipEntry
{
    std::string ClipID;       // ID na AudioAssetLibrary
    std::string ClipPath;     // fallback
    float       Weight = 1.f; // peso para seleção aleatória ponderada
};

struct AudioEvent
{
    std::string    Name;                // chave única ("Footstep", "Explosion"...)
    std::string    Description;

    // ---- Clips -----------------------------------------------------------
    std::vector<AudioEventClipEntry>  Clips;
    AudioEventClipSelection           Selection = AudioEventClipSelection::Random;

    // ---- Parâmetros de playback ------------------------------------------
    float  VolumeMin   = 1.0f;
    float  VolumeMax   = 1.0f;
    float  PitchMin    = 1.0f;
    float  PitchMax    = 1.0f;
    bool   Loop        = false;
    float  SpatialBlend= 1.0f;  // 0=2D, 1=3D
    float  MinDistance = 1.0f;
    float  MaxDistance = 25.0f;
    std::string MixerGroupID = ::MixerGroupID::SFX;

    // ---- Cooldown --------------------------------------------------------
    float  CooldownSeconds = 0.f;  // tempo mínimo entre triggers

    // ---- Estado de runtime (não serializado) ----------------------------
    mutable float  LastTriggerTime  = -9999.f;
    mutable int    LastClipIndex    = -1;
    mutable uint32_t RandState      = 54321;

    // Resolve o clip path para este trigger
    std::string ResolveClip() const;
    float ResolveVolume()     const;
    float ResolvePitch()      const;
    bool  IsOnCooldown(float currentTime) const;

private:
    float Rand01() const;
};
