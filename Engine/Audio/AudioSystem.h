#pragma once
// ---------------------------------------------------------------------------
// AudioSystem — núcleo do motor de áudio da Marae Engine
//
// Backend: miniaudio (WAV/MP3/OGG/FLAC, 3D nativo, LPF/HPF, delay)
//
// Responsabilidades:
//   • Inicializar/destruir a engine miniaudio
//   • Gerenciar ma_sound_group por MixerGroup
//   • Pool de voices com roubo por prioridade
//   • Atualizar listener 3D nativo (ma_engine_listener)
//   • Processar fades, delays, playback 3D, Doppler
//   • Coordenar AudioMixer, AudioEnvironmentManager
//   • Expor API para AudioEventSystem e código do jogo
// ---------------------------------------------------------------------------

#include "AudioTypes.h"
#include "Source/AudioSourceComponent.h"
#include "Listener/AudioListenerComponent.h"
#include "Mixer/AudioMixer.h"
#include "Environment/AudioEnvironmentManager.h"
#include "Debug/AudioDebugStats.h"

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <glm/glm.hpp>

// Forward-declare miniaudio sem contaminar headers
struct ma_engine;
struct ma_sound;
// struct ma_hpf_node;
// struct ma_delay_node;

// ---------------------------------------------------------------------------
// Parâmetros de Play — API pública simplificada
// ---------------------------------------------------------------------------
struct AudioSourceParams
{
    std::string  ClipPath;
    glm::vec3    Position      = glm::vec3(0.f);
    glm::vec3    Velocity      = glm::vec3(0.f);
    float        Volume        = 1.0f;
    float        Pitch         = 1.0f;
    float        SpatialBlend  = 1.0f;
    float        MinDistance   = 1.0f;
    float        MaxDistance   = 25.0f;
    float        DopplerFactor = 1.0f;
    float        ConeInnerDeg  = 360.f;
    float        ConeOuterDeg  = 360.f;
    float        ConeOuterVol  = 0.f;
    bool         Loop          = false;
    bool         Stream        = false;   // forçar streaming
    uint8_t      Priority      = 128;
    std::string  GroupID       = MixerGroupID::SFX;
    AudioAttenuationModel AttenuationModel = AudioAttenuationModel::Inverse;
};

// ---------------------------------------------------------------------------
// AudioSystem
// ---------------------------------------------------------------------------
class AudioSystem
{
public:
    static AudioSystem& Get();

    // --- Ciclo de vida ----------------------------------------------------
    bool Init();
    void Shutdown();

    // Chamado uma vez por frame — atualiza 3D, fades, environment, mixer
    void Update(float deltaTime);

    // --- Playback ---------------------------------------------------------
    AudioVoiceID Play(const AudioSourceParams& params);
    void         Stop(AudioVoiceID id);
    void         Stop();                      // para tudo
    void         Pause(AudioVoiceID id);
    void         Resume(AudioVoiceID id);
    bool         IsPlaying(AudioVoiceID id) const;

    // Atualiza posição/velocidade de uma voice em movimento
    void SetVoicePosition(AudioVoiceID id, const glm::vec3& pos);
    void SetVoiceTransform(AudioVoiceID id,
                           const glm::vec3& pos,
                           const glm::vec3& vel);
    void SetVoiceAcousticMix(AudioVoiceID id, const AudioAcousticMix& mix);

    // Fade para uma voice específica
    void FadeVolume(AudioVoiceID id, float targetVol, float durationSec);

    // --- Listener ---------------------------------------------------------
    void SetListener(const AudioListenerComponent& listener);
    void SetListenerFromCamera(const glm::vec3& pos,
                               const glm::vec3& forward,
                               const glm::vec3& up,
                               const glm::vec3& velocity = glm::vec3(0.f));

    // --- Mixer (conveniência — delega ao AudioMixer::Get()) ---------------
    void SetGroupVolume(const std::string& groupID, float volume);
    void SetMasterVolume(float volume);
    float GetMasterVolume() const;

    // --- Environment ------------------------------------------------------
    void RegisterEnvironmentZone(const AudioEnvironmentZone& zone);
    void UnregisterEnvironmentZone(uint64_t entityID);

    // --- Sons de jogo (survival — mantidos por compatibilidade) ----------
    void PlayToolSwing();
    void PlayPickup();
    void PlayCraft();
    void PlayEat();

    // --- Preview do editor ------------------------------------------------
    AudioVoiceID PlayPreview(const AudioSourceComponent& src);
    AudioVoiceID PlayPreviewClip(const std::string& clipPath, float volume = 1.f, float pitch = 1.f);

    // --- Debug ------------------------------------------------------------
    const AudioDebugStats& GetDebugStats() const { return m_Stats; }

    bool IsInitialized() const { return m_Initialized; }

private:
    AudioSystem() = default;
    ~AudioSystem() { Shutdown(); }
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // --- Pool de voices ---------------------------------------------------
    struct Voice
    {
        AudioVoiceID      ID           = AUDIO_INVALID_VOICE;
        ma_sound*         Sound        = nullptr;
        AudioSourceParams Params;
        bool              Active       = false;
        bool              Paused       = false;

        // Fade interno
        bool              FadeActive   = false;
        float             FadeStart    = 1.f;
        float             FadeTarget   = 0.f;
        float             FadeDuration = 0.f;
        float             FadeElapsed  = 0.f;
        bool              FadeStop     = false;  // para ao final

        // Delay de início
        float             DelayTimer   = 0.f;
        bool              DelayPending = false;
        float             AcousticGain = 1.0f;
        void*             AcousticFilter = nullptr; // ma_lpf_node, opaco no header
        float             AcousticCutoffHz = 22000.0f;
    };

    bool  InitMixerGroups();
    void  UpdateListenerNative();
    void  UpdateVoices(float dt);
    void  UpdateEnvironment(float dt);
    void  UpdateMixerGroups();
    void  ApplyEnvironmentToNewVoice(Voice& v);
    float GetGroupEffectiveVolume(const std::string& groupID) const;

    Voice* AllocVoice(uint8_t priority);
    void   FreeVoice(Voice& v);
    Voice* FindVoice(AudioVoiceID id);
    const Voice* FindVoice(AudioVoiceID id) const;

    ma_sound* GetOrCreateGroup(const std::string& groupID);

    void PlayToneInternal(float frequency, float duration, float volume);

    // --- Estado -----------------------------------------------------------
    bool        m_Initialized    = false;
    ma_engine*  m_Engine         = nullptr;

    // ma_sound_group por MixerGroup ID
    struct GroupEntry {
        std::string  GroupID;
        ma_sound*    Group = nullptr;
    };
    std::vector<GroupEntry> m_Groups;

    std::vector<Voice>  m_Voices;
    AudioVoiceID        m_NextID = 1;

    // Listener atual
    glm::vec3  m_ListenerPos     = glm::vec3(0.f);
    glm::vec3  m_ListenerForward = glm::vec3(0.f, 0.f, -1.f);
    glm::vec3  m_ListenerUp      = glm::vec3(0.f, 1.f,  0.f);
    glm::vec3  m_ListenerVel     = glm::vec3(0.f);

    // Efeitos globais de ambiente
    // ma_lpf_node*   m_GlobalLPF   = nullptr;
    // ma_hpf_node*   m_GlobalHPF   = nullptr;
    // ma_delay_node* m_GlobalDelay = nullptr;
    // float          m_CurrentLPFCutoff  = 22000.f;
    // float          m_CurrentHPFCutoff  = 20.f;

    float          m_MasterVolume = 1.f;
    float          m_CurrentTime  = 0.f;

    AudioDebugStats m_Stats;
};
