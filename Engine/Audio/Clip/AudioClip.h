#pragma once
// ---------------------------------------------------------------------------
// AudioClip — representa um asset de áudio com metadados e ID estável.
// ---------------------------------------------------------------------------
#include "../AudioTypes.h"
#include <string>
#include <cstdint>

// Formato detectado automaticamente durante o import
enum class AudioFormat : uint8_t
{
    Unknown = 0,
    WAV,
    MP3,
    OGG,    // OGG Vorbis
    FLAC,
    M4A,    // suporte limitado (só PCM-in-M4A)
};

// Estratégia de carregamento
enum class AudioLoadMode : uint8_t
{
    Auto   = 0,  // engine decide: <256KB→Memory, >=256KB→Stream
    Memory = 1,  // carrega tudo na memória (SFX curtos)
    Stream = 2,  // streaming progressivo (música, ambience)
};

struct AudioClipMetadata
{
    float    DurationSeconds  = 0.f;
    uint32_t SampleRate       = 0;
    uint8_t  Channels         = 0;
    uint32_t BitRate          = 0;   // kbps; 0 se não disponível
    AudioFormat Format        = AudioFormat::Unknown;
    bool     Valid            = false;
    std::string ErrorMessage;
};

class AudioClip
{
public:
    // Cria um clip a partir de um caminho de arquivo.
    // ID é gerado automaticamente (hash do path relativo) ou pode ser fornecido.
    explicit AudioClip(const std::string& path, const std::string& id = "");

    // --- Identidade -------------------------------------------------------
    const std::string& GetID()   const { return m_ID;   }
    const std::string& GetPath() const { return m_Path; }
    const std::string& GetName() const { return m_Name; }

    // --- Metadados (preenchidos por AudioDecoder::Inspect) ----------------
    const AudioClipMetadata& GetMetadata() const { return m_Meta; }
    void SetMetadata(const AudioClipMetadata& meta) { m_Meta = meta; }

    // --- Configuração de playback -----------------------------------------
    AudioLoadMode         GetLoadMode()         const { return m_LoadMode; }
    void                  SetLoadMode(AudioLoadMode m) { m_LoadMode = m; }

    AudioAttenuationModel GetAttenuationModel() const { return m_AttenuationModel; }
    void SetAttenuationModel(AudioAttenuationModel m) { m_AttenuationModel = m; }

    float  GetDefaultVolume()      const { return m_DefaultVolume; }
    void   SetDefaultVolume(float v)     { m_DefaultVolume = v; }

    float  GetDefaultPitch()       const { return m_DefaultPitch; }
    void   SetDefaultPitch(float p)      { m_DefaultPitch = p; }

    bool   GetDefaultLoop()        const { return m_DefaultLoop; }
    void   SetDefaultLoop(bool l)        { m_DefaultLoop = l; }

    // Path relativo ao projeto (usado no manifest e para portabilidade)
    const std::string& GetRelativePath() const { return m_RelativePath; }
    void SetRelativePath(const std::string& rel) { m_RelativePath = rel; }

    // Verifica se o arquivo existe em disco
    bool ExistsOnDisk() const;

    // Retorna a string do formato legível
    static const char* FormatName(AudioFormat f);

private:
    std::string           m_ID;
    std::string           m_Path;           // caminho absoluto resolvido
    std::string           m_RelativePath;   // relativo à raiz do projeto
    std::string           m_Name;           // nome do arquivo sem extensão

    AudioClipMetadata     m_Meta;
    AudioLoadMode         m_LoadMode           = AudioLoadMode::Auto;
    AudioAttenuationModel m_AttenuationModel   = AudioAttenuationModel::Inverse;
    float                 m_DefaultVolume      = 1.0f;
    float                 m_DefaultPitch       = 1.0f;
    bool                  m_DefaultLoop        = false;

    std::string GenerateID(const std::string& path) const;
};
