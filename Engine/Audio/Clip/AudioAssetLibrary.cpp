#include "AudioAssetLibrary.h"
#include "AudioDecoder.h"
#include "../../../Assets/AssetDatabase/AssetDatabase.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

static bool IsSafeProjectRelativePath(const fs::path& path)
{
    if (path.empty() || path.is_absolute()) return false;
    const fs::path normalized = path.lexically_normal();
    for (const auto& part : normalized)
        if (part == "..") return false;
    return true;
}

static const std::vector<std::string> k_AudioExtensions = {
    ".wav", ".mp3", ".ogg", ".flac"
    // .m4a listado mas será rejeitado no Inspect
};

AudioAssetLibrary& AudioAssetLibrary::Get()
{
    static AudioAssetLibrary instance;
    return instance;
}

bool AudioAssetLibrary::IsSupportedExtension(const std::string& ext)
{
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& e : k_AudioExtensions)
        if (lower == e) return true;
    return false;
}

std::shared_ptr<AudioClip> AudioAssetLibrary::Import(const std::string& absolutePath,
                                                      const std::string& projectRoot)
{
    // Já registrado por path?
    auto existing = FindByPath(absolutePath);
    if (existing) return existing;

    auto clip = std::make_shared<AudioClip>(absolutePath);

    // Calcula path relativo para portabilidade
    if (!projectRoot.empty())
    {
        std::error_code ec;
        fs::path rel = fs::relative(absolutePath, projectRoot, ec);
        if (!ec)
        {
            clip->SetRelativePath(rel.generic_string());
            // O AssetDatabase é a fonte única de identidade. Assim o áudio
            // importado usa o mesmo GUID que aparece no editor e nos scripts.
            const std::string assetGUID = AssetDatabase::GetGUIDForPath(
                clip->GetRelativePath());
            if (!assetGUID.empty())
                clip = std::make_shared<AudioClip>(absolutePath, assetGUID);
            clip->SetRelativePath(rel.generic_string());
        }
    }

    // Inspeciona metadados — rejeita se inválido
    if (!AudioDecoder::Inspect(*clip))
    {
        std::cerr << "[AudioAssetLibrary] Ignorando arquivo inválido: "
                  << absolutePath << std::endl;
        return nullptr;
    }

    // Registra
    m_Clips.push_back(clip);
    m_ByID[clip->GetID()]     = clip;
    m_ByPath[absolutePath]    = clip;
    if (!clip->GetRelativePath().empty())
        m_ByPath[clip->GetRelativePath()] = clip;

    std::cout << "[AudioAssetLibrary] Importado: " << clip->GetName()
              << " (" << AudioClip::FormatName(clip->GetMetadata().Format) << ", "
              << clip->GetMetadata().DurationSeconds << "s)"
              << std::endl;

    return clip;
}

void AudioAssetLibrary::ScanDirectory(const std::string& directory,
                                      const std::string& projectRoot)
{
    if (!fs::exists(directory)) return;

    for (const auto& entry : fs::recursive_directory_iterator(
             directory, fs::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (!IsSupportedExtension(ext)) continue;
        Import(entry.path().string(), projectRoot);
    }
}

std::shared_ptr<AudioClip> AudioAssetLibrary::FindByID(const std::string& id) const
{
    auto it = m_ByID.find(id);
    return (it != m_ByID.end()) ? it->second : nullptr;
}

std::shared_ptr<AudioClip> AudioAssetLibrary::FindByPath(const std::string& path) const
{
    auto it = m_ByPath.find(path);
    return (it != m_ByPath.end()) ? it->second : nullptr;
}

const std::vector<std::shared_ptr<AudioClip>>& AudioAssetLibrary::GetAll() const
{
    return m_Clips;
}

void AudioAssetLibrary::Remove(const std::string& id)
{
    auto it = m_ByID.find(id);
    if (it == m_ByID.end()) return;

    auto clip = it->second;
    m_ByPath.erase(clip->GetPath());
    m_ByPath.erase(clip->GetRelativePath());
    m_ByID.erase(it);
    m_Clips.erase(std::remove_if(m_Clips.begin(), m_Clips.end(),
        [&](const std::shared_ptr<AudioClip>& c){ return c->GetID() == id; }),
        m_Clips.end());
}

void AudioAssetLibrary::Clear()
{
    m_Clips.clear();
    m_ByID.clear();
    m_ByPath.clear();
}

// ---------------------------------------------------------------------------
// Persistência: formato simples key=value por clip
// ---------------------------------------------------------------------------
void AudioAssetLibrary::Save(const std::string& projectRoot) const
{
    if (projectRoot.empty()) return;

    fs::path libDir = fs::path(projectRoot) / "Library";
    fs::create_directories(libDir);
    fs::path cfgPath = libDir / "audio_assets.cfg";

    std::ofstream f(cfgPath);
    if (!f.is_open()) return;

    for (const auto& clip : m_Clips)
    {
        f << "[clip]\n";
        f << "id="       << clip->GetID()           << "\n";
        f << "rel="      << clip->GetRelativePath()  << "\n";
        f << "loadmode=" << static_cast<int>(clip->GetLoadMode()) << "\n";
        f << "volume="   << clip->GetDefaultVolume() << "\n";
        f << "pitch="    << clip->GetDefaultPitch()  << "\n";
        f << "loop="     << (clip->GetDefaultLoop() ? 1 : 0) << "\n";
        f << "\n";
    }
}

void AudioAssetLibrary::Load(const std::string& projectRoot)
{
    if (projectRoot.empty()) return;

    fs::path cfgPath = fs::path(projectRoot) / "Library" / "audio_assets.cfg";
    if (!fs::exists(cfgPath)) return;

    std::ifstream f(cfgPath);
    if (!f.is_open()) return;

    std::string line;
    std::string pendingID, pendingRel;
    int pendingLoadMode = 0;
    float pendingVolume = 1.f, pendingPitch = 1.f;
    bool pendingLoop = false;
    bool inClip = false;

    auto FlushClip = [&]()
    {
        if (!inClip || pendingID.empty() || pendingRel.empty()) return;

        const fs::path relative = fs::path(pendingRel);
        if (!IsSafeProjectRelativePath(relative)) return;
        fs::path abs = fs::weakly_canonical(fs::path(projectRoot) / relative);
        if (!fs::is_regular_file(abs)) return;

        std::string stableID = pendingID;
        const std::string assetGUID = AssetDatabase::GetGUIDForPath(
            relative.generic_string());
        if (!assetGUID.empty())
            stableID = assetGUID;
        auto clip = std::make_shared<AudioClip>(abs.string(), stableID);
        clip->SetRelativePath(relative.generic_string());
        clip->SetLoadMode(pendingLoadMode >= static_cast<int>(AudioLoadMode::Memory) &&
                          pendingLoadMode <= static_cast<int>(AudioLoadMode::Stream)
                              ? static_cast<AudioLoadMode>(pendingLoadMode) : AudioLoadMode::Auto);
        clip->SetDefaultVolume(std::isfinite(pendingVolume) ? std::clamp(pendingVolume, 0.0f, 1.0f) : 1.0f);
        clip->SetDefaultPitch(std::isfinite(pendingPitch) ? std::clamp(pendingPitch, 0.01f, 4.0f) : 1.0f);
        clip->SetDefaultLoop(pendingLoop);

        // Inspeciona metadados frescos
        if (!AudioDecoder::Inspect(*clip)) return;

        m_Clips.push_back(clip);
        m_ByID[clip->GetID()]           = clip;
        m_ByPath[clip->GetPath()]       = clip;
        m_ByPath[clip->GetRelativePath()] = clip;
    };

    while (std::getline(f, line))
    {
        if (line == "[clip]")
        {
            FlushClip();
            pendingID.clear(); pendingRel.clear();
            pendingLoadMode = 0; pendingVolume = 1.f;
            pendingPitch = 1.f; pendingLoop = false;
            inClip = true;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "id")       pendingID  = val;
        else if (key == "rel")      pendingRel = val;
        else
        {
            try
            {
                if      (key == "loadmode") pendingLoadMode = std::stoi(val);
                else if (key == "volume")   pendingVolume   = std::stof(val);
                else if (key == "pitch")    pendingPitch    = std::stof(val);
                else if (key == "loop")     pendingLoop     = (val == "1");
            }
            catch (const std::exception&) {}
        }
    }
    FlushClip();
}
