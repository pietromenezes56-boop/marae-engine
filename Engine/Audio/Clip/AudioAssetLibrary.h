#pragma once
// ---------------------------------------------------------------------------
// AudioAssetLibrary — registra e resolve AudioClips por ID estável.
//
// Ao abrir um projeto a library é populada escaneando Assets/Sounds etc.
// Referências por ID continuam funcionando mesmo depois de renomear arquivos,
// desde que o asset seja reimportado com o mesmo ID.
// ---------------------------------------------------------------------------
#include "AudioClip.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class AudioAssetLibrary
{
public:
    static AudioAssetLibrary& Get();

    // --- Importação -------------------------------------------------------

    // Importa um arquivo, detecta formato, inspeciona metadados, registra.
    // Retorna nullptr se o formato não for suportado ou o arquivo inválido.
    std::shared_ptr<AudioClip> Import(const std::string& absolutePath,
                                      const std::string& projectRoot = "");

    // Reimporta todos os arquivos de áudio encontrados em `directory`.
    void ScanDirectory(const std::string& directory,
                       const std::string& projectRoot = "");

    // --- Lookup -----------------------------------------------------------

    // Por ID estável (preferencial)
    std::shared_ptr<AudioClip> FindByID(const std::string& id) const;

    // Por caminho absoluto ou relativo
    std::shared_ptr<AudioClip> FindByPath(const std::string& path) const;

    // Lista todos os clips registrados
    const std::vector<std::shared_ptr<AudioClip>>& GetAll() const;

    // --- Persistência -----------------------------------------------------

    // Salva o manifesto (ID → relativePath) em `projectRoot/Library/audio_assets.cfg`
    void Save(const std::string& projectRoot) const;

    // Carrega o manifesto e reconstrói a library
    void Load(const std::string& projectRoot);

    // Remove clip pelo ID
    void Remove(const std::string& id);

    // Limpa todos os registros
    void Clear();

    // Extensões suportadas
    static bool IsSupportedExtension(const std::string& ext);

private:
    AudioAssetLibrary() = default;

    std::vector<std::shared_ptr<AudioClip>>              m_Clips;
    std::unordered_map<std::string, std::shared_ptr<AudioClip>> m_ByID;
    std::unordered_map<std::string, std::shared_ptr<AudioClip>> m_ByPath;
};
