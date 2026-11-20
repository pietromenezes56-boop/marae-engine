#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <utility>

enum class EditorLanguage
{
    Portuguese = 0,
    English    = 1,
    Spanish    = 2,
    Auto       = 3   // detecta o idioma do sistema operacional
};

class Localization
{
public:
    static Localization& Get();
    ~Localization();

    // Retorna a string traduzida para a chave. Fallback para PT ou para a própria chave.
    const char* T(const char* key) const;

    // Traduz um texto de UI em background (para strings não catalogadas).
    const char* TranslateUI(const char* sourceText) const;

    bool IsAutoTranslationEnabled() const { return m_AutoTranslationEnabled; }
    void SetAutoTranslationEnabled(bool enabled);
    std::string GetAutoTranslationStatus() const;
    void RetryAutoTranslation();

    EditorLanguage GetLanguage()         const { return m_Language; }
    EditorLanguage GetResolvedLanguage() const; // Auto -> idioma real detectado

    void SetLanguage(EditorLanguage lang);

    void Load();        // Lê Library/settings.json
    void Save() const;  // Salva Library/settings.json

    // Detecta idioma do SO sem internet (Windows GetUserDefaultUILanguage)
    static EditorLanguage DetectSystemLanguage();

    // Retorna true se o arquivo lang/xx.json existe
    bool HasLangFile(const std::string& langCode) const;

    // Inicia geração assíncrona de en.json ou es.json traduzindo a partir de pt.json
    void RequestLangGeneration(const std::string& langCode);

    // Status da geração de arquivo de idioma
    std::string GetLangGenerationStatus() const;

private:
    Localization();

    bool  LoadLangFile(const std::string& langCode);
    void  GenerateLangFileAsync(const std::string& langCode);
    void  GenerateLangFileWorker(const std::string& langCode);
    std::string LangFilePath(const std::string& langCode) const;

    // Worker de tradução de UI em tempo real (TranslateUI)
    void TranslationWorker();
    void QueueTranslation(const std::string& source, const std::string& target) const;
    bool TranslateBatch(const std::vector<std::string>& source,
        const std::string& target, std::vector<std::string>& translated,
        std::string& error) const;
    void LoadAutoTranslationCache();
    void SaveAutoTranslationCache() const;

    // Tabelas de strings: langCode -> (key -> value)
    // "pt" sempre carregado; outros quando selecionados
    mutable std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_Tables;

    EditorLanguage m_Language = EditorLanguage::Auto;
    bool m_AutoTranslationEnabled = true;

    // Geração assíncrona de arquivo de idioma
    mutable std::mutex m_GenMutex;
    std::string m_GenStatus;         // status visível para o usuário
    std::thread m_GenThread;
    std::atomic<bool> m_GenRunning{ false };

    // Worker de tradução de UI
    mutable std::mutex m_TranslationMutex;
    mutable std::condition_variable m_TranslationWake;
    mutable std::deque<std::pair<std::string, std::string>> m_TranslationQueue;
    mutable std::unordered_set<std::string> m_PendingTranslations;
    mutable std::unordered_set<std::string> m_KnownUiStrings;
    mutable std::unordered_map<std::string, std::string> m_AutoTranslations;
    mutable std::string m_TranslationError;
    std::thread m_TranslationThread;
    std::atomic<bool> m_StopTranslationWorker{ false };
};

// Atalho global -- use L("chave") em qualquer lugar do editor.
inline const char* L(const char* key)
{
    return Localization::Get().T(key);
}

inline const char* UI(const char* sourceText)
{
    return Localization::Get().TranslateUI(sourceText);
}
