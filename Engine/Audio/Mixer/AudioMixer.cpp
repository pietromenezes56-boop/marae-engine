#include "AudioMixer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
AudioMixer& AudioMixer::Get()
{
    static AudioMixer instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Init — cria hierarquia padrão
// ---------------------------------------------------------------------------
void AudioMixer::Init()
{
    if (m_Initialized) return;
    CreateDefaultGroups();
    m_Initialized = true;
}

void AudioMixer::CreateDefaultGroups()
{
    auto MakeGroup = [&](const char* id, const char* name, const char* parent)
    {
        auto g = std::make_shared<AudioMixerGroup>(id, name, parent ? parent : "");
        m_Groups.push_back(g);
        m_ByID[id] = g.get();
    };

    // Ordem: pai antes do filho (importante para RecalcGroup)
    MakeGroup(MixerGroupID::Master,   "Master",   nullptr);
    MakeGroup(MixerGroupID::Music,    "Music",    MixerGroupID::Master);
    MakeGroup(MixerGroupID::SFX,      "SFX",      MixerGroupID::Master);
    MakeGroup(MixerGroupID::Voice,    "Voice",    MixerGroupID::Master);
    MakeGroup(MixerGroupID::Ambience, "Ambience", MixerGroupID::Master);
    MakeGroup(MixerGroupID::UI,       "UI",       MixerGroupID::Master);
    MakeGroup(MixerGroupID::Weapons,  "Weapons",  MixerGroupID::SFX);
    MakeGroup(MixerGroupID::Vehicles, "Vehicles", MixerGroupID::SFX);

    RecalculateEffectiveVolumes();
}

// ---------------------------------------------------------------------------
// Update — avança fades e ducking
// ---------------------------------------------------------------------------
void AudioMixer::Update(float deltaTime)
{
    // Solo: se algum grupo tem solo ativado, muteia todos os outros no mesmo pai
    // (implementação simples: apenas grupos de nível 1 suportam solo)
    bool hasSolo = false;
    for (auto& g : m_Groups)
        if (g->IsSolo()) { hasSolo = true; break; }

    for (auto& g : m_Groups)
    {
        g->UpdateFade(deltaTime);
        g->UpdateDucking(deltaTime);

        // Aplica solo: silencia grupos no mesmo nível que não têm solo
        if (hasSolo && g->GetID() != MixerGroupID::Master)
        {
            // Não sobrescreve mute manual — apenas aplica lógica de solo
            // através do effectiveVolume na próxima recalculação
        }
    }

    RecalculateEffectiveVolumes();
}

// ---------------------------------------------------------------------------
// Grupos
// ---------------------------------------------------------------------------
AudioMixerGroup* AudioMixer::GetGroup(const std::string& id)
{
    auto it = m_ByID.find(id);
    return (it != m_ByID.end()) ? it->second : nullptr;
}

const AudioMixerGroup* AudioMixer::GetGroup(const std::string& id) const
{
    auto it = m_ByID.find(id);
    return (it != m_ByID.end()) ? it->second : nullptr;
}

AudioMixerGroup* AudioMixer::CreateGroup(const std::string& id,
                                          const std::string& name,
                                          const std::string& parentID)
{
    if (m_ByID.count(id)) return m_ByID[id]; // já existe

    auto g = std::make_shared<AudioMixerGroup>(id, name, parentID);
    m_Groups.push_back(g);
    m_ByID[id] = g.get();
    RecalculateEffectiveVolumes();
    return g.get();
}

const std::vector<std::shared_ptr<AudioMixerGroup>>& AudioMixer::GetAllGroups() const
{
    return m_Groups;
}

// ---------------------------------------------------------------------------
// Volumes efetivos
// ---------------------------------------------------------------------------
void AudioMixer::RecalculateEffectiveVolumes()
{
    // Processa em ordem de inserção (pais sempre antes dos filhos)
    for (auto& g : m_Groups)
    {
        float parentEffective = 1.f;
        if (!g->GetParentID().empty())
        {
            const AudioMixerGroup* parent = GetGroup(g->GetParentID());
            if (parent) parentEffective = parent->GetEffectiveVolume();
        }
        RecalcGroup(*g, parentEffective);
    }
}

void AudioMixer::RecalcGroup(AudioMixerGroup& group, float parentEffective)
{
    float effective = group.GetAppliedVolume() * parentEffective;

    // Solo: se algum irmão tem solo e este não tem, zera
    if (!group.IsSolo())
    {
        const std::string& parentID = group.GetParentID();
        bool siblingHasSolo = false;
        for (auto& g : m_Groups)
        {
            if (g.get() == &group) continue;
            if (g->GetParentID() == parentID && g->IsSolo())
            {
                siblingHasSolo = true;
                break;
            }
        }
        if (siblingHasSolo) effective = 0.f;
    }

    group.SetEffectiveVolume(effective);
}

float AudioMixer::GetEffectiveVolume(const std::string& groupID) const
{
    const AudioMixerGroup* g = GetGroup(groupID);
    return g ? g->GetEffectiveVolume() : 1.f;
}

bool AudioMixer::CaptureSnapshot(const std::string& requestedName)
{
    std::string name;
    name.reserve(std::min<size_t>(requestedName.size(), 48));
    for (unsigned char ch : requestedName)
    {
        if (name.size() >= 48) break;
        if (std::isalnum(ch)) name.push_back(static_cast<char>(ch));
        else if ((ch == '_' || ch == '-') && !name.empty() && name.back() != '_') name.push_back(static_cast<char>(ch));
    }
    if (name.empty() || m_Groups.empty()) return false;

    AudioMixerSnapshot snapshot;
    snapshot.Name = name;
    snapshot.Groups.reserve(m_Groups.size());
    for (const auto& group : m_Groups)
        if (group)
            snapshot.Groups.push_back({ group->GetID(), group->GetVolume(), group->IsMuted(), group->IsSolo() });

    const auto existing = std::find_if(m_Snapshots.begin(), m_Snapshots.end(),
        [&name](const AudioMixerSnapshot& item) { return item.Name == name; });
    if (existing != m_Snapshots.end()) *existing = std::move(snapshot);
    else m_Snapshots.push_back(std::move(snapshot));
    return true;
}

bool AudioMixer::ApplySnapshot(const std::string& name, float transitionSeconds)
{
    const auto snapshot = std::find_if(m_Snapshots.begin(), m_Snapshots.end(),
        [&name](const AudioMixerSnapshot& item) { return item.Name == name; });
    if (snapshot == m_Snapshots.end()) return false;
    const float duration = std::clamp(std::isfinite(transitionSeconds) ? transitionSeconds : 0.0f, 0.0f, 60.0f);
    bool applied = false;
    for (const auto& state : snapshot->Groups)
    {
        AudioMixerGroup* group = GetGroup(state.GroupID);
        if (!group) continue;
        if (duration > 0.0f) group->FadeTo(state.Volume, duration);
        else group->SetVolume(state.Volume);
        group->SetMuted(state.Muted);
        group->SetSolo(state.Solo);
        applied = true;
    }
    RecalculateEffectiveVolumes();
    return applied;
}

bool AudioMixer::RemoveSnapshot(const std::string& name)
{
    const auto oldSize = m_Snapshots.size();
    m_Snapshots.erase(std::remove_if(m_Snapshots.begin(), m_Snapshots.end(),
        [&name](const AudioMixerSnapshot& snapshot) { return snapshot.Name == name; }), m_Snapshots.end());
    return m_Snapshots.size() != oldSize;
}

// ---------------------------------------------------------------------------
// Ducking
// ---------------------------------------------------------------------------
void AudioMixer::SetupDucking(const std::string& targetGroupID,
                               const std::string& triggerGroupID,
                               float duckTo,
                               float attackTime,
                               float releaseTime)
{
    AudioMixerGroup* target = GetGroup(targetGroupID);
    if (!target) return;

    DuckingConfig& cfg  = target->GetDuckingConfig();
    cfg.Enabled         = true;
    cfg.DuckTo          = duckTo;
    cfg.AttackTime      = attackTime;
    cfg.ReleaseTime     = releaseTime;
    cfg.TriggerGroupID  = triggerGroupID;
}

void AudioMixer::NotifyGroupActive(const std::string& groupID, bool hasVoices)
{
    // Verifica se algum grupo com ducking tem este groupID como trigger
    for (auto& g : m_Groups)
    {
        const DuckingConfig& cfg = g->GetDuckingConfig();
        if (cfg.Enabled && cfg.TriggerGroupID == groupID)
            g->SetDucked(hasVoices);
    }
}

// ---------------------------------------------------------------------------
// Serialização
// ---------------------------------------------------------------------------
void AudioMixer::Save(const std::string& projectRoot) const
{
    if (projectRoot.empty()) return;
    fs::path path = fs::path(projectRoot) / "Library" / "audio_mixer.cfg";
    fs::create_directories(path.parent_path());

    std::ofstream f(path);
    if (!f.is_open()) return;

    for (const auto& g : m_Groups)
    {
        f << "[group]\n";
        f << "id="     << g->GetID()       << "\n";
        f << "name="   << g->GetName()     << "\n";
        f << "parent=" << g->GetParentID() << "\n";
        f << "volume=" << g->GetVolume()   << "\n";
        f << "muted="  << (g->IsMuted() ? 1 : 0) << "\n";
        f << "solo="   << (g->IsSolo() ? 1 : 0) << "\n";

        const DuckingConfig& dc = g->GetDuckingConfig();
        if (dc.Enabled)
        {
            f << "duck_enabled=1\n";
            f << "duck_to="       << dc.DuckTo      << "\n";
            f << "duck_attack="   << dc.AttackTime   << "\n";
            f << "duck_release="  << dc.ReleaseTime  << "\n";
            f << "duck_trigger="  << dc.TriggerGroupID << "\n";
        }
        f << "\n";
    }

    for (const auto& snapshot : m_Snapshots)
    {
        f << "[snapshot]\nname=" << snapshot.Name << "\n";
        for (const auto& state : snapshot.Groups)
            f << "state=" << state.GroupID << "," << state.Volume << ","
              << (state.Muted ? 1 : 0) << "," << (state.Solo ? 1 : 0) << "\n";
        f << "\n";
    }
}

void AudioMixer::Load(const std::string& projectRoot)
{
    fs::path path = fs::path(projectRoot) / "Library" / "audio_mixer.cfg";
    if (!fs::exists(path)) return;

    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    std::string pendingID, pendingName, pendingParent;
    float pendingVol = 1.f;
    bool pendingMuted = false;
    bool pendingSolo = false;
    bool pendingDuckEnabled = false;
    float pendingDuckTo = 0.3f, pendingAttack = 0.1f, pendingRelease = 0.5f;
    std::string pendingDuckTrigger;
    bool inGroup = false;
    bool inSnapshot = false;
    AudioMixerSnapshot pendingSnapshot;

    auto FlushSnapshot = [&]()
    {
        if (!inSnapshot || pendingSnapshot.Name.empty()) return;
        const auto found = std::find_if(m_Snapshots.begin(), m_Snapshots.end(),
            [&](const AudioMixerSnapshot& item) { return item.Name == pendingSnapshot.Name; });
        if (found == m_Snapshots.end()) m_Snapshots.push_back(std::move(pendingSnapshot));
        else *found = std::move(pendingSnapshot);
        pendingSnapshot = {};
        inSnapshot = false;
    };

    auto FlushGroup = [&]()
    {
        if (!inGroup || pendingID.empty()) return;

        AudioMixerGroup* g = GetGroup(pendingID);
        if (!g)
        {
            // Grupo customizado não-padrão
            g = CreateGroup(pendingID, pendingName, pendingParent);
        }
        if (g)
        {
            g->SetVolume(pendingVol);
            g->SetMuted(pendingMuted);
            g->SetSolo(pendingSolo);
            if (pendingDuckEnabled)
                SetupDucking(pendingID, pendingDuckTrigger,
                             pendingDuckTo, pendingAttack, pendingRelease);
        }
    };

    while (std::getline(f, line))
    {
        if (line == "[group]" || line == "[snapshot]")
        {
            FlushGroup();
            FlushSnapshot();
            if (line == "[snapshot]")
            {
                pendingSnapshot = {};
                inSnapshot = true;
                inGroup = false;
                continue;
            }
            pendingID.clear(); pendingName.clear(); pendingParent.clear();
            pendingVol = 1.f; pendingMuted = false; pendingSolo = false;
            pendingDuckEnabled = false; pendingDuckTo = 0.3f;
            pendingAttack = 0.1f; pendingRelease = 0.5f;
            pendingDuckTrigger.clear();
            inGroup = true;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (inSnapshot)
        {
            if (key == "name") pendingSnapshot.Name = val;
            else if (key == "state")
            {
                std::stringstream stateStream(val);
                std::string id, volume, muted, solo;
                if (std::getline(stateStream, id, ',') && std::getline(stateStream, volume, ',') &&
                    std::getline(stateStream, muted, ',') && std::getline(stateStream, solo, ','))
                {
                    try
                    {
                        const float parsedVolume = std::stof(volume);
                        if (!id.empty() && std::isfinite(parsedVolume))
                            pendingSnapshot.Groups.push_back({ id, std::clamp(parsedVolume, 0.0f, 1.0f), muted == "1", solo == "1" });
                    }
                    catch (...) {}
                }
            }
            continue;
        }

        if      (key == "id")           pendingID     = val;
        else if (key == "name")         pendingName   = val;
        else if (key == "parent")       pendingParent = val;
        else if (key == "volume")       pendingVol    = std::stof(val);
        else if (key == "muted")        pendingMuted  = (val == "1");
        else if (key == "solo")         pendingSolo   = (val == "1");
        else if (key == "duck_enabled") pendingDuckEnabled = (val == "1");
        else if (key == "duck_to")      pendingDuckTo      = std::stof(val);
        else if (key == "duck_attack")  pendingAttack      = std::stof(val);
        else if (key == "duck_release") pendingRelease     = std::stof(val);
        else if (key == "duck_trigger") pendingDuckTrigger = val;
    }
    FlushGroup();
    FlushSnapshot();
    RecalculateEffectiveVolumes();
}

void AudioMixer::Reset()
{
    for (auto& g : m_Groups)
    {
        g->SetVolume(1.f);
        g->SetMuted(false);
        g->SetSolo(false);
    }
    m_Snapshots.clear();
    RecalculateEffectiveVolumes();
}
