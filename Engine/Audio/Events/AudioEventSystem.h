#pragma once
// ---------------------------------------------------------------------------
// AudioEventSystem — registra e dispara AudioEvents pelo nome.
//
// Uso:
//   AudioEventSystem::Get().Trigger("Explosion", position);
//   AudioEventSystem::Get().TriggerGlobal("UIClick");  // 2D
// ---------------------------------------------------------------------------
#include "AudioEvent.h"
#include "../AudioTypes.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>

class AudioEventSystem
{
public:
    static AudioEventSystem& Get();

    // --- Registro ---------------------------------------------------------
    // Registra ou substitui um evento
    void Register(const AudioEvent& event);
    void Unregister(const std::string& name);
    AudioEvent* GetEvent(const std::string& name);
    const std::vector<std::shared_ptr<AudioEvent>>& GetAll() const;

    // --- Trigger ----------------------------------------------------------
    // Dispara um evento em posição 3D
    AudioVoiceID Trigger(const std::string& name,
                         const glm::vec3& position = glm::vec3(0.f),
                         float currentTime = 0.f);

    AudioVoiceID TriggerGlobal(const std::string& name,
                               float currentTime = 0.f);

    // --- Persistência -----------------------------------------------------
    void Save(const std::string& projectRoot) const;
    void Load(const std::string& projectRoot);

    void Clear();

private:
    AudioEventSystem() = default;

    std::vector<std::shared_ptr<AudioEvent>>              m_Events;
    std::unordered_map<std::string, AudioEvent*>          m_ByName;
};
