#pragma once
// ---------------------------------------------------------------------------
// AudioMixerGroup — um bus do mixer com volume, mute, solo, fade, efeitos.
//
// A hierarquia de roteamento é:
//   AudioSource → MixerGroup → MixerGroup (pai) → ... → Master
//
// O volume efetivo de um grupo = volume_proprio * volume_pai (recursivo).
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <algorithm>

// IDs pré-definidos para os grupos padrão
namespace MixerGroupID
{
    constexpr const char* Master   = "master";
    constexpr const char* Music    = "music";
    constexpr const char* SFX      = "sfx";
    constexpr const char* Voice    = "voice";
    constexpr const char* Ambience = "ambience";
    constexpr const char* UI       = "ui";
    constexpr const char* Weapons  = "weapons";
    constexpr const char* Vehicles = "vehicles";
}

// Estado de um fade em andamento
struct MixerFade
{
    float StartVolume  = 1.f;
    float TargetVolume = 1.f;
    float Duration     = 0.f;   // segundos
    float Elapsed      = 0.f;
    bool  Active       = false;
};

// Configuração de ducking automático
struct DuckingConfig
{
    bool  Enabled      = false;
    float DuckTo       = 0.3f;   // volume alvo ao fazer duck
    float AttackTime   = 0.1f;   // segundos para descer
    float ReleaseTime  = 0.5f;   // segundos para subir
    std::string TriggerGroupID;  // grupo que dispara o ducking neste grupo
};

class AudioMixerGroup
{
public:
    explicit AudioMixerGroup(const std::string& id,
                             const std::string& name,
                             const std::string& parentID = "");

    // --- Identidade -------------------------------------------------------
    const std::string& GetID()       const { return m_ID; }
    const std::string& GetName()     const { return m_Name; }
    const std::string& GetParentID() const { return m_ParentID; }
    void SetParentID(const std::string& id) { m_ParentID = id; }

    // --- Volume -----------------------------------------------------------
    float GetVolume()      const { return m_Volume; }
    void  SetVolume(float v)     { m_Volume = std::clamp(v, 0.f, 1.f); }

    // Volume em dB (conversão bidirecional)
    float GetVolumeDB()    const;
    void  SetVolumeDB(float db);

    // Volume efetivo = produto de todos os grupos na hierarquia (calculado
    // externamente pelo AudioMixer que tem visão global da árvore)
    float GetEffectiveVolume() const { return m_EffectiveVolume; }
    void  SetEffectiveVolume(float v) { m_EffectiveVolume = v; }

    // --- Controles --------------------------------------------------------
    bool IsMuted()   const { return m_Muted; }
    void SetMuted(bool m)  { m_Muted = m; }
    void ToggleMute()      { m_Muted = !m_Muted; }

    bool IsSolo()    const { return m_Solo; }
    void SetSolo(bool s)   { m_Solo = s; }

    // --- Fade -------------------------------------------------------------
    void FadeTo(float targetVolume, float durationSeconds);
    void FadeIn(float durationSeconds)  { FadeTo(1.f, durationSeconds); }
    void FadeOut(float durationSeconds) { FadeTo(0.f, durationSeconds); }
    bool IsFading() const { return m_Fade.Active; }
    const MixerFade& GetFade() const { return m_Fade; }

    // Avança o fade (chamado pelo AudioMixer::Update)
    void UpdateFade(float deltaTime);

    // --- Ducking ----------------------------------------------------------
    DuckingConfig& GetDuckingConfig()             { return m_Ducking; }
    const DuckingConfig& GetDuckingConfig() const { return m_Ducking; }
    void  SetDucked(bool ducked);
    bool  IsDucked() const { return m_Ducked; }
    void  UpdateDucking(float deltaTime);
    float GetDuckVolume() const { return m_DuckVolume; }

    // Volume final aplicado às sources: considera mute + fade + duck
    float GetAppliedVolume() const;

    // --- Contagem de voices ativa (atualizada pelo AudioSystem) -----------
    int  GetActiveVoiceCount()   const { return m_ActiveVoices; }
    void SetActiveVoiceCount(int n)    { m_ActiveVoices = n; }

private:
    std::string   m_ID;
    std::string   m_Name;
    std::string   m_ParentID;

    float         m_Volume          = 1.f;
    float         m_EffectiveVolume = 1.f;
    bool          m_Muted           = false;
    bool          m_Solo            = false;

    MixerFade     m_Fade;

    DuckingConfig m_Ducking;
    bool          m_Ducked      = false;
    float         m_DuckVolume  = 1.f;   // interpolado entre 1 e DuckTo

    int           m_ActiveVoices = 0;
};
