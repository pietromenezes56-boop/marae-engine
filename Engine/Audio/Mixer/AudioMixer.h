#pragma once
// ---------------------------------------------------------------------------
// AudioMixer — gerencia a árvore de MixerGroups e calcula volumes efetivos.
//
// Hierarquia padrão:
//   Master
//   ├─ Music
//   ├─ SFX
//   │   ├─ Weapons
//   │   └─ Vehicles
//   ├─ Voice
//   ├─ Ambience
//   └─ UI
// ---------------------------------------------------------------------------
#include "AudioMixerGroup.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

struct AudioMixerSnapshotState
{
    std::string GroupID;
    float Volume = 1.0f;
    bool Muted = false;
    bool Solo = false;
};

struct AudioMixerSnapshot
{
    std::string Name;
    std::vector<AudioMixerSnapshotState> Groups;
};

class AudioMixer
{
public:
    static AudioMixer& Get();

    // Cria os grupos padrão (chamado por AudioSystem::Init)
    void Init();

    // Atualiza fades e ducking em todos os grupos
    void Update(float deltaTime);

    // --- Grupos -----------------------------------------------------------

    // Retorna grupo por ID (nullptr se não encontrado)
    AudioMixerGroup* GetGroup(const std::string& id);
    const AudioMixerGroup* GetGroup(const std::string& id) const;

    // Cria um grupo customizado
    AudioMixerGroup* CreateGroup(const std::string& id,
                                 const std::string& name,
                                 const std::string& parentID = MixerGroupID::Master);

    // Lista todos os grupos em ordem de profundidade (pai antes do filho)
    const std::vector<std::shared_ptr<AudioMixerGroup>>& GetAllGroups() const;

    // --- Volume efetivo ---------------------------------------------------

    // Recalcula m_EffectiveVolume de todos os grupos (produto hierárquico)
    void RecalculateEffectiveVolumes();

    // Volume efetivo de um grupo pelo ID
    float GetEffectiveVolume(const std::string& groupID) const;

    bool CaptureSnapshot(const std::string& name);
    bool ApplySnapshot(const std::string& name, float transitionSeconds = 0.25f);
    bool RemoveSnapshot(const std::string& name);
    const std::vector<AudioMixerSnapshot>& GetSnapshots() const { return m_Snapshots; }

    // --- Ducking rápido ---------------------------------------------------

    // Ativa o ducking de `targetGroupID` quando `triggerGroupID` tiver sons
    void SetupDucking(const std::string& targetGroupID,
                      const std::string& triggerGroupID,
                      float duckTo,
                      float attackTime,
                      float releaseTime);

    // Notifica que um grupo tem voices ativas (chamado pelo AudioSystem)
    void NotifyGroupActive(const std::string& groupID, bool hasVoices);

    // --- Serialização -----------------------------------------------------
    void Save(const std::string& projectRoot) const;
    void Load(const std::string& projectRoot);

    // Reseta para os valores padrão
    void Reset();

private:
    AudioMixer() = default;

    void CreateDefaultGroups();
    void RecalcGroup(AudioMixerGroup& group, float parentEffective);

    std::vector<std::shared_ptr<AudioMixerGroup>>              m_Groups;
    std::unordered_map<std::string, AudioMixerGroup*>          m_ByID;
    std::vector<AudioMixerSnapshot> m_Snapshots;
    bool m_Initialized = false;
};
