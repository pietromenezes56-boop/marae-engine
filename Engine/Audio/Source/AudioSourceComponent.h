#pragma once
// ---------------------------------------------------------------------------
// AudioSourceComponent — componente rico de fonte de áudio para entidades.
//
// Substitui o AudioComponent antigo. Suporta:
//   • AudioClip por ID ou path
//   • Play/Pause/Resume/Stop com fade
//   • Volume, Pitch, Pan, Mute
//   • Loop, PlayOnStart, PlayOnEnable
//   • Delay antes de tocar
//   • Prioridade (0=mais alta, 255=mais baixa)
//   • 2D puro / 3D espacial / blend
//   • Distância min/max, atenuação, cone
//   • Doppler
//   • Random pitch / random volume / random clip
//   • Playlist (lista de clips para tocar em sequência ou shuffle)
//   • Fade in/out automático
//   • Mixer group target
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../Clip/AudioClip.h"
#include "../Mixer/AudioMixer.h"

// Handle de voice — vem de AudioTypes.h (incluído via AudioClip.h)
// AudioVoiceID e kInvalidVoiceID usam AUDIO_INVALID_VOICE de AudioTypes.h
static constexpr AudioVoiceID kInvalidVoiceID = AUDIO_INVALID_VOICE;

// Modo de reprodução da playlist
enum class AudioPlaylistMode : uint8_t
{
    Single     = 0,   // toca apenas ClipID
    Sequential = 1,   // toca Playlist em ordem
    Shuffle    = 2,   // toca Playlist aleatoriamente
};

// Estado de playback
enum class AudioPlayState : uint8_t
{
    Stopped = 0,
    Playing,
    Paused,
    Fading,
};

// Fade em andamento
struct AudioSourceFade
{
    bool  Active       = false;
    bool  StopOnEnd    = false;   // para a source ao final do fade out
    float StartVolume  = 1.f;
    float TargetVolume = 0.f;
    float Duration     = 0.f;
    float Elapsed      = 0.f;
};

struct AudioSourceComponent
{
    // ---- Identidade e asset ---------------------------------------------
    // ID público da fonte: scripts podem tocar este áudio sem depender do
    // nome da entidade ou do caminho físico do arquivo.
    std::string SourceID;
    std::string ClipID;       // ID estável do AudioClip (preferencial)
    std::string ClipPath;     // fallback: path direto se ClipID vazio

    // Playlist de clips (modo Sequential/Shuffle)
    std::vector<std::string>  PlaylistIDs;
    AudioPlaylistMode         PlaylistMode   = AudioPlaylistMode::Single;

    // ---- Controle básico -------------------------------------------------
    bool  Enabled       = true;
    bool  Loop          = false;
    bool  PlayOnStart   = false;
    bool  PlayOnEnable  = false;
    bool  Mute          = false;
    uint8_t Priority    = 128;        // 0=maior prioridade, 255=menor

    // ---- Volume e pitch --------------------------------------------------
    float Volume        = 1.0f;       // 0..1
    float Pitch         = 1.0f;       // razão de frequência
    float Pan           = 0.0f;       // -1=esquerda, 0=centro, +1=direita (2D)

    // Randomização
    float RandomVolume  = 0.0f;       // ±randVol aplicado ao Volume
    float RandomPitch   = 0.0f;       // ±randPitch aplicado ao Pitch

    // Delay antes de tocar (segundos)
    float PlayDelay     = 0.0f;

    // ---- Fade automático -------------------------------------------------
    float FadeInDuration  = 0.0f;     // 0 = sem fade in
    float FadeOutDuration = 0.0f;     // 0 = sem fade out (ao chamar Stop)

    // ---- Espacialização 3D -----------------------------------------------
    float SpatialBlend    = 1.0f;     // 0=2D puro, 1=3D puro
    float MinDistance     = 1.0f;
    float MaxDistance     = 25.0f;
    AudioAttenuationModel AttenuationModel = AudioAttenuationModel::Inverse;
    float Rolloff         = 1.0f;     // usado com Inverse e Exponential

    // Cone de direção (valores em graus; 0=omni-direcional)
    float ConeInnerAngle  = 360.0f;   // graus — área de volume pleno
    float ConeOuterAngle  = 360.0f;   // graus — limite do cone
    float ConeOuterVolume = 0.0f;     // volume fora do cone externo

    // Doppler
    float DopplerFactor   = 1.0f;     // 0=desligado, 1=físico real

    // ---- Mixer -----------------------------------------------------------
    std::string MixerGroupID = MixerGroupID::SFX;  // grupo de roteamento

    // ---- Estado de runtime (não serializado) ----------------------------
    AudioPlayState    PlayState     = AudioPlayState::Stopped;
    AudioVoiceID      RuntimeVoiceID = kInvalidVoiceID;
    AudioSourceFade   Fade;
    float             DelayTimer    = 0.f;    // contador do delay
    bool              DelayPending  = false;
    int               PlaylistIndex = 0;
    float             PlaybackPos   = 0.f;    // posição em segundos (estimada)

    // ---- Acústica geométrica -------------------------------------------
    // Cada opção pode ser desligada para fontes simples ou para economizar
    // custo em jogos que não precisam de propagação espacial avançada.
    bool AcousticPropagationEnabled = true;
    bool AcousticRayTracingEnabled = true;
    bool GeometricAcousticsEnabled = true;
    bool DynamicPropagationEnabled = true;
    bool OcclusionEnabled = true;
    bool ObstructionEnabled = true;
    bool DiffractionEnabled = true;
    bool AcousticPathfindingEnabled = true;
    float AcousticTransmission = 0.35f;
    float AcousticUpdateInterval = 0.05f;

    // ---- Helpers em runtime ----------------------------------------------

    // Resolve o clip path efetivo (ClipID → library → path, ou ClipPath direto)
    std::string ResolveClipPath() const;

    // Seleciona o próximo clip da playlist
    std::string NextPlaylistClip();

    // Volume efetivo aplicado (considera Mute + Random)
    float GetEffectiveVolume() const;

    // Pitch efetivo (considera Random)
    float GetEffectivePitch() const;

private:
    mutable uint32_t m_RandState = 12345;
    float RandFloat() const;  // [-1, +1] determinístico por source
};
