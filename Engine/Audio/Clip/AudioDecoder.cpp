#include "AudioDecoder.h"

// miniaudio — apenas declarações
#include "../../../ThirdParty/miniaudio/miniaudio.h"

#include <filesystem>
#include <fstream>
#include <array>
#include <iostream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Detecção de formato por magic bytes
// ---------------------------------------------------------------------------
AudioFormat AudioDecoder::DetectFormat(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return AudioFormat::Unknown;

    std::array<uint8_t, 12> magic{};
    f.read(reinterpret_cast<char*>(magic.data()), 12);
    const size_t read = static_cast<size_t>(f.gcount());
    f.close();

    if (read < 4) return AudioFormat::Unknown;

    // RIFF....WAVE
    if (magic[0]=='R' && magic[1]=='I' && magic[2]=='F' && magic[3]=='F' &&
        read >= 12 &&
        magic[8]=='W' && magic[9]=='A' && magic[10]=='V' && magic[11]=='E')
        return AudioFormat::WAV;

    // ID3 tag (MP3) ou frame sync FF Fx
    if ((magic[0]=='I' && magic[1]=='D' && magic[2]=='3') ||
        (magic[0]==0xFF && (magic[1] & 0xE0)==0xE0))
        return AudioFormat::MP3;

    // OggS
    if (magic[0]=='O' && magic[1]=='g' && magic[2]=='g' && magic[3]=='S')
        return AudioFormat::OGG;

    // fLaC
    if (magic[0]=='f' && magic[1]=='L' && magic[2]=='a' && magic[3]=='C')
        return AudioFormat::FLAC;

    // ftyp / M4A
    if (read >= 8 && magic[4]=='f' && magic[5]=='t' && magic[6]=='y' && magic[7]=='p')
        return AudioFormat::M4A;

    // Fallback por extensão
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".wav")  return AudioFormat::WAV;
    if (ext == ".mp3")  return AudioFormat::MP3;
    if (ext == ".ogg")  return AudioFormat::OGG;
    if (ext == ".flac") return AudioFormat::FLAC;
    if (ext == ".m4a")  return AudioFormat::M4A;

    return AudioFormat::Unknown;
}

// ---------------------------------------------------------------------------
// Inspeção de metadados via miniaudio decoder
// ---------------------------------------------------------------------------
bool AudioDecoder::Inspect(AudioClip& clip)
{
    AudioClipMetadata meta;
    meta.Format = DetectFormat(clip.GetPath());

    if (meta.Format == AudioFormat::Unknown)
    {
        meta.Valid = false;
        meta.ErrorMessage = "Formato nao reconhecido: " + clip.GetPath();
        clip.SetMetadata(meta);
        std::cerr << "[AudioDecoder] " << meta.ErrorMessage << std::endl;
        return false;
    }

    if (meta.Format == AudioFormat::M4A)
    {
        // M4A/AAC não tem suporte nativo no miniaudio open-source
        meta.Valid = false;
        meta.ErrorMessage = "M4A nao suportado pelo backend atual (miniaudio). "
                            "Converta para MP3, OGG ou FLAC.";
        clip.SetMetadata(meta);
        std::cerr << "[AudioDecoder] " << meta.ErrorMessage << std::endl;
        return false;
    }

    // Usa miniaudio decoder para ler sample rate, canais e duração
    ma_decoder_config cfg = ma_decoder_config_init_default();
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(clip.GetPath().c_str(), &cfg, &decoder);

    if (result != MA_SUCCESS)
    {
        meta.Valid = false;
        meta.ErrorMessage = std::string("Falha ao decodificar: ") +
                            ma_result_description(result);
        clip.SetMetadata(meta);
        std::cerr << "[AudioDecoder] " << meta.ErrorMessage << std::endl;
        return false;
    }

    meta.SampleRate = decoder.outputSampleRate;
    meta.Channels   = static_cast<uint8_t>(decoder.outputChannels);

    // Duração: total de frames / sample rate
    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) == MA_SUCCESS
        && meta.SampleRate > 0)
    {
        meta.DurationSeconds = static_cast<float>(frameCount) /
                               static_cast<float>(meta.SampleRate);
    }

    // Bitrate: estimativa pelo tamanho do arquivo
    if (meta.DurationSeconds > 0.001f)
    {
        uintmax_t fileSize = fs::file_size(clip.GetPath());
        // kbps = (bytes * 8) / (duration * 1000)
        meta.BitRate = static_cast<uint32_t>(
            (static_cast<double>(fileSize) * 8.0) /
            (static_cast<double>(meta.DurationSeconds) * 1000.0));
    }

    ma_decoder_uninit(&decoder);

    meta.Valid = true;
    clip.SetMetadata(meta);

    // Seta o LoadMode recomendado se ainda for Auto
    if (clip.GetLoadMode() == AudioLoadMode::Auto)
        clip.SetLoadMode(RecommendLoadMode(clip.GetPath()));

    return true;
}

AudioLoadMode AudioDecoder::RecommendLoadMode(const std::string& path)
{
    constexpr uintmax_t k_StreamThreshold = 256 * 1024; // 256 KB
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec || size == static_cast<uintmax_t>(-1)) return AudioLoadMode::Memory;
    return (size >= k_StreamThreshold) ? AudioLoadMode::Stream : AudioLoadMode::Memory;
}
