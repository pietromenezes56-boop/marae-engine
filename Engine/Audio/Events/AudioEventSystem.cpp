#include "AudioEventSystem.h"
#include "../AudioSystem.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

AudioEventSystem& AudioEventSystem::Get()
{
    static AudioEventSystem instance;
    return instance;
}

void AudioEventSystem::Register(const AudioEvent& event)
{
    Unregister(event.Name);
    auto e = std::make_shared<AudioEvent>(event);
    m_Events.push_back(e);
    m_ByName[event.Name] = e.get();
}

void AudioEventSystem::Unregister(const std::string& name)
{
    m_ByName.erase(name);
    m_Events.erase(
        std::remove_if(m_Events.begin(), m_Events.end(),
            [&](const std::shared_ptr<AudioEvent>& e){ return e->Name == name; }),
        m_Events.end());
}

AudioEvent* AudioEventSystem::GetEvent(const std::string& name)
{
    auto it = m_ByName.find(name);
    return (it != m_ByName.end()) ? it->second : nullptr;
}

const std::vector<std::shared_ptr<AudioEvent>>& AudioEventSystem::GetAll() const
{
    return m_Events;
}

// ---------------------------------------------------------------------------
// Trigger
// ---------------------------------------------------------------------------
AudioVoiceID AudioEventSystem::Trigger(const std::string& name,
                                        const glm::vec3& position,
                                        float currentTime)
{
    AudioEvent* ev = GetEvent(name);
    if (!ev) return AUDIO_INVALID_VOICE;
    if (ev->IsOnCooldown(currentTime)) return AUDIO_INVALID_VOICE;

    std::string clipPath = ev->ResolveClip();
    if (clipPath.empty()) return AUDIO_INVALID_VOICE;

    AudioSourceParams params;
    params.ClipPath     = clipPath;
    params.Position     = position;
    params.Volume       = ev->ResolveVolume();
    params.Pitch        = ev->ResolvePitch();
    params.Loop         = ev->Loop;
    params.SpatialBlend = ev->SpatialBlend;
    params.MinDistance  = ev->MinDistance;
    params.MaxDistance  = ev->MaxDistance;
    params.GroupID      = ev->MixerGroupID;

    ev->LastTriggerTime = currentTime;
    return AudioSystem::Get().Play(params);
}

AudioVoiceID AudioEventSystem::TriggerGlobal(const std::string& name,
                                              float currentTime)
{
    AudioEvent* ev = GetEvent(name);
    if (!ev) return AUDIO_INVALID_VOICE;
    if (ev->IsOnCooldown(currentTime)) return AUDIO_INVALID_VOICE;

    std::string clipPath = ev->ResolveClip();
    if (clipPath.empty()) return AUDIO_INVALID_VOICE;

    AudioSourceParams params;
    params.ClipPath     = clipPath;
    params.Volume       = ev->ResolveVolume();
    params.Pitch        = ev->ResolvePitch();
    params.Loop         = ev->Loop;
    params.SpatialBlend = 0.f;   // 2D puro
    params.GroupID      = ev->MixerGroupID;

    ev->LastTriggerTime = currentTime;
    return AudioSystem::Get().Play(params);
}

// ---------------------------------------------------------------------------
// Persistência — formato simples por evento
// ---------------------------------------------------------------------------
void AudioEventSystem::Save(const std::string& projectRoot) const
{
    if (projectRoot.empty()) return;
    fs::path path = fs::path(projectRoot) / "Library" / "audio_events.cfg";
    fs::create_directories(path.parent_path());

    std::ofstream f(path);
    if (!f.is_open()) return;

    for (const auto& ev : m_Events)
    {
        f << "[event]\n";
        f << "name="        << ev->Name          << "\n";
        f << "desc="        << ev->Description   << "\n";
        f << "selection="   << static_cast<int>(ev->Selection) << "\n";
        f << "vol_min="     << ev->VolumeMin      << "\n";
        f << "vol_max="     << ev->VolumeMax      << "\n";
        f << "pitch_min="   << ev->PitchMin       << "\n";
        f << "pitch_max="   << ev->PitchMax       << "\n";
        f << "loop="        << (ev->Loop ? 1 : 0) << "\n";
        f << "spatial="     << ev->SpatialBlend   << "\n";
        f << "min_dist="    << ev->MinDistance    << "\n";
        f << "max_dist="    << ev->MaxDistance    << "\n";
        f << "group="       << ev->MixerGroupID   << "\n";
        f << "cooldown="    << ev->CooldownSeconds<< "\n";
        for (const auto& c : ev->Clips)
        {
            f << "clip=" << c.ClipID << "|" << c.ClipPath << "|" << c.Weight << "\n";
        }
        f << "\n";
    }
}

void AudioEventSystem::Load(const std::string& projectRoot)
{
    fs::path path = fs::path(projectRoot) / "Library" / "audio_events.cfg";
    if (!fs::exists(path)) return;

    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    AudioEvent pending;
    bool inEvent = false;

    auto FlushEvent = [&]()
    {
        if (inEvent && !pending.Name.empty())
            Register(pending);
        pending = AudioEvent{};
    };

    while (std::getline(f, line))
    {
        if (line == "[event]")     { FlushEvent(); inEvent = true; continue; }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "name")      pending.Name       = val;
        else if (key == "desc")      pending.Description= val;
        else if (key == "selection") pending.Selection  = static_cast<AudioEventClipSelection>(std::stoi(val));
        else if (key == "vol_min")   pending.VolumeMin  = std::stof(val);
        else if (key == "vol_max")   pending.VolumeMax  = std::stof(val);
        else if (key == "pitch_min") pending.PitchMin   = std::stof(val);
        else if (key == "pitch_max") pending.PitchMax   = std::stof(val);
        else if (key == "loop")      pending.Loop       = (val == "1");
        else if (key == "spatial")   pending.SpatialBlend = std::stof(val);
        else if (key == "min_dist")  pending.MinDistance= std::stof(val);
        else if (key == "max_dist")  pending.MaxDistance= std::stof(val);
        else if (key == "group")     pending.MixerGroupID = val;
        else if (key == "cooldown")  pending.CooldownSeconds = std::stof(val);
        else if (key == "clip")
        {
            // clip=id|path|weight
            AudioEventClipEntry entry;
            size_t p1 = val.find('|');
            size_t p2 = val.rfind('|');
            entry.ClipID   = val.substr(0, p1);
            entry.ClipPath = (p1 != p2) ? val.substr(p1+1, p2-p1-1) : "";
            try { entry.Weight = std::stof(val.substr(p2+1)); } catch(...) {}
            pending.Clips.push_back(entry);
        }
    }
    FlushEvent();
}

void AudioEventSystem::Clear() { m_Events.clear(); m_ByName.clear(); }
