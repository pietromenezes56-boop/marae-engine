#include "AudioClip.h"
#include <filesystem>
#include <functional>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

AudioClip::AudioClip(const std::string& path, const std::string& id)
    : m_Path(path)
{
    // Nome = filename sem extensão
    m_Name = fs::path(path).stem().string();

    // ID: usa o fornecido ou gera um hash estável do path
    m_ID = id.empty() ? GenerateID(path) : id;
}

bool AudioClip::ExistsOnDisk() const
{
    return !m_Path.empty() && fs::exists(m_Path);
}

const char* AudioClip::FormatName(AudioFormat f)
{
    switch (f)
    {
        case AudioFormat::WAV:  return "WAV";
        case AudioFormat::MP3:  return "MP3";
        case AudioFormat::OGG:  return "OGG Vorbis";
        case AudioFormat::FLAC: return "FLAC";
        case AudioFormat::M4A:  return "M4A";
        default:                return "Unknown";
    }
}

std::string AudioClip::GenerateID(const std::string& path) const
{
    // FNV-1a 64-bit hash do path → string hex de 16 chars
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : path)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return "aclip_" + ss.str();
}
