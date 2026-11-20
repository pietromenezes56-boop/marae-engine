#include "Localization.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <regex>
#include "../../ThirdParty/json/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace fs = std::filesystem;

// ─── Caminho dos arquivos de idioma ─────────────────────────────────────────
// Os arquivos ficam em Engine/Localization/langs/ relativo ao diretório do exe.
// Em tempo de execução, usamos a localização do executável como base.
static std::string GetLangsDir()
{
    // 1. Relativo ao arquivo fonte (em tempo de desenvolvimento)
    try {
        fs::path srcDir = fs::path(__FILE__).parent_path() / "langs";
        if (fs::exists(srcDir)) return srcDir.string();
    } catch (...) {}

#ifdef _WIN32
    try {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path exeDir = fs::path(buf).parent_path();
        const std::vector<fs::path> candidates = {
            exeDir / "Engine" / "Localization" / "langs",
            exeDir / "MaraeEngine" / "Engine" / "Localization" / "langs",
            exeDir / "langs",
            exeDir.parent_path() / "Engine" / "Localization" / "langs",
            exeDir.parent_path() / "MaraeEngine" / "Engine" / "Localization" / "langs",
            exeDir.parent_path().parent_path() / "Engine" / "Localization" / "langs",
            exeDir.parent_path().parent_path() / "MaraeEngine" / "Engine" / "Localization" / "langs",
            exeDir.parent_path().parent_path().parent_path() / "MaraeEngine" / "Engine" / "Localization" / "langs"
        };
        for (const auto& c : candidates)
        {
            if (fs::exists(c)) return c.string();
        }
    } catch (...) {}
#endif

    try {
        const std::vector<fs::path> cwdCandidates = {
            fs::current_path() / "Engine" / "Localization" / "langs",
            fs::current_path() / "MaraeEngine" / "Engine" / "Localization" / "langs",
            fs::current_path() / "langs"
        };
        for (const auto& c : cwdCandidates)
        {
            if (fs::exists(c)) return c.string();
        }
    } catch (...) {}

    return "Engine/Localization/langs";
}

static std::string LangCodeFromEnum(EditorLanguage lang)
{
    switch (lang)
    {
        case EditorLanguage::English:    return "en";
        case EditorLanguage::Spanish:    return "es";
        case EditorLanguage::Portuguese: return "pt";
        default:                         return "pt";
    }
}

// ─── Singleton ───────────────────────────────────────────────────────────────
Localization& Localization::Get()
{
    static Localization instance;
    return instance;
}

Localization::Localization()
{
    // 1) Carrega PT (sempre disponível – é o arquivo base incluído no projeto)
    LoadLangFile("pt");

    // 2) Lê configuração salva (idioma escolhido pelo usuário)
    Load();

    // 3) Carrega arquivo do idioma ativo (se não for PT)
    const EditorLanguage resolved = GetResolvedLanguage();
    const std::string code = LangCodeFromEnum(resolved);
    if (code != "pt")
    {
        if (!LoadLangFile(code))
            GenerateLangFileAsync(code); // Primeira vez: gera e salva
    }

    // 4) Worker de tradução de UI em tempo real (TranslateUI para strings avulsas)
    LoadAutoTranslationCache();
    m_TranslationThread = std::thread(&Localization::TranslationWorker, this);
}

Localization::~Localization()
{
    m_StopTranslationWorker.store(true);
    m_TranslationWake.notify_all();
    if (m_TranslationThread.joinable())
        m_TranslationThread.join();
    if (m_GenThread.joinable())
        m_GenThread.join();
    SaveAutoTranslationCache();
}

// ─── Idioma ──────────────────────────────────────────────────────────────────
EditorLanguage Localization::GetResolvedLanguage() const
{
    if (m_Language == EditorLanguage::Auto)
        return DetectSystemLanguage();
    return m_Language;
}

EditorLanguage Localization::DetectSystemLanguage()
{
#ifdef _WIN32
    LANGID lid = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(lid);
    if (primary == LANG_PORTUGUESE) return EditorLanguage::Portuguese;
    if (primary == LANG_SPANISH)    return EditorLanguage::Spanish;
    return EditorLanguage::English;
#else
    return EditorLanguage::Portuguese;
#endif
}

void Localization::SetLanguage(EditorLanguage lang)
{
    {
        std::lock_guard<std::mutex> lock(m_TranslationMutex);
        m_Language = lang;
        m_TranslationError.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_GenMutex);
        m_GenStatus.clear();
    }

    const EditorLanguage resolved = GetResolvedLanguage();
    const std::string code = LangCodeFromEnum(resolved);

    // Carrega o arquivo do idioma escolhido (se ainda não tiver em memória)
    if (m_Tables.find(code) == m_Tables.end() || m_Tables[code].empty())
    {
        if (!LoadLangFile(code))
            GenerateLangFileAsync(code);
    }

    // Re-enfileira traduções de UI em background (para strings avulsas)
    if (m_AutoTranslationEnabled && code != "pt")
    {
        std::lock_guard<std::mutex> lock(m_TranslationMutex);
        for (const std::string& source : m_KnownUiStrings)
            QueueTranslation(source, code);
    }

    Save();
}

// ─── Lookup de chave ─────────────────────────────────────────────────────────
const char* Localization::T(const char* key) const
{
    const EditorLanguage resolved = GetResolvedLanguage();
    const std::string code = LangCodeFromEnum(resolved);

    // Tabela do idioma ativo
    auto tableIt = m_Tables.find(code);
    if (tableIt != m_Tables.end())
    {
        auto it = tableIt->second.find(key);
        if (it != tableIt->second.end())
            return it->second.c_str();
    }

    // Fallback para PT
    auto ptIt = m_Tables.find("pt");
    if (ptIt != m_Tables.end())
    {
        auto it = ptIt->second.find(key);
        if (it != ptIt->second.end())
            return it->second.c_str();
    }

    if (key && strcmp(key, "material.mirror") == 0)
    {
        if (code == "en") return "Mirror";
        if (code == "es") return "Espejo";
        return "Espelho";
    }

    return key; // Última saída: retorna a própria chave
}

// ─── Carregamento de arquivo JSON de idioma ──────────────────────────────────
std::string Localization::LangFilePath(const std::string& langCode) const
{
    return GetLangsDir() + "/" + langCode + ".json";
}

bool Localization::HasLangFile(const std::string& langCode) const
{
    return fs::exists(LangFilePath(langCode));
}

bool Localization::LoadLangFile(const std::string& langCode)
{
    const std::string path = LangFilePath(langCode);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    try
    {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return false;

        auto& table = m_Tables[langCode];
        table.clear();
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.value().is_string())
                table[it.key()] = it.value().get<std::string>();
        }
        if (!table.empty())
        {
            std::lock_guard<std::mutex> lk(m_GenMutex);
            m_GenStatus.clear();
            return true;
        }
        return false;
    }
    catch (...) { return false; }
}

// ─── Geração assíncrona de arquivo de idioma ─────────────────────────────────
void Localization::RequestLangGeneration(const std::string& langCode)
{
    GenerateLangFileAsync(langCode);
}

std::string Localization::GetLangGenerationStatus() const
{
    std::lock_guard<std::mutex> lk(m_GenMutex);
    return m_GenStatus;
}

void Localization::GenerateLangFileAsync(const std::string& langCode)
{
    if (m_GenRunning.exchange(true)) return; // já rodando

    {
        std::lock_guard<std::mutex> lk(m_GenMutex);
        m_GenStatus = "Gerando traducao para " + langCode + "...";
    }

    if (m_GenThread.joinable()) m_GenThread.join();
    m_GenThread = std::thread([this, langCode]() {
        GenerateLangFileWorker(langCode);
        m_GenRunning.store(false);
    });
}

void Localization::GenerateLangFileWorker(const std::string& langCode)
{
    // Pega todas as strings PT como source
    auto ptIt = m_Tables.find("pt");
    if (ptIt == m_Tables.end())
    {
        std::lock_guard<std::mutex> lk(m_GenMutex);
        m_GenStatus = "Erro: pt.json nao carregado.";
        return;
    }

    const auto& ptTable = ptIt->second;
    std::vector<std::string> keys, sources;
    keys.reserve(ptTable.size());
    sources.reserve(ptTable.size());
    for (const auto& kv : ptTable)
    {
        keys.push_back(kv.first);
        sources.push_back(kv.second);
    }

    // Traduz em lotes de 24
    std::unordered_map<std::string, std::string> result;
    const size_t BATCH = 24;
    for (size_t i = 0; i < sources.size(); i += BATCH)
    {
        const size_t end = (std::min)(i + BATCH, sources.size());
        std::vector<std::string> srcBatch(sources.begin() + i, sources.begin() + end);
        std::vector<std::string> translated;
        std::string error;

        {
            std::lock_guard<std::mutex> lk(m_GenMutex);
            m_GenStatus = "Traduzindo " + langCode + ": " +
                          std::to_string(i) + "/" + std::to_string(sources.size()) + " strings...";
        }

        bool ok = TranslateBatch(srcBatch, langCode, translated, error);
        if (!ok || translated.size() != srcBatch.size())
        {
            std::lock_guard<std::mutex> lk(m_GenMutex);
            m_GenStatus = "Erro na traducao para " + langCode + ": " + error;
            return;
        }

        for (size_t j = 0; j < srcBatch.size(); ++j)
            result[keys[i + j]] = translated[j];

        // Pausa gentil para não sobrecarregar o servidor
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Salva o arquivo JSON
    const std::string outPath = LangFilePath(langCode);
    try
    {
        fs::create_directories(fs::path(outPath).parent_path());
        nlohmann::json j = result;
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            std::lock_guard<std::mutex> lk(m_GenMutex);
            m_GenStatus = "Erro: nao foi possivel salvar " + outPath;
            return;
        }
        out << j.dump(2) << '\n';
    }
    catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lk(m_GenMutex);
        m_GenStatus = std::string("Erro ao salvar: ") + e.what();
        return;
    }

    // Carrega em memória
    LoadLangFile(langCode);

    {
        std::lock_guard<std::mutex> lk(m_GenMutex);
        m_GenStatus = "Traducao para " + langCode + " concluida!";
    }
}

// ─── TranslateUI (strings avulsas de UI em tempo real) ───────────────────────
const char* Localization::TranslateUI(const char* sourceText) const
{
    if (!sourceText || !*sourceText)
        return sourceText ? sourceText : "";

    const std::string fullText(sourceText);
    const size_t idStart = fullText.find("##");
    const std::string visible = fullText.substr(0, idStart);
    const std::string suffix = (idStart == std::string::npos) ? "" : fullText.substr(idStart);
    if (visible.empty())
        return sourceText;

    const EditorLanguage resolved = GetResolvedLanguage();
    const std::string target = LangCodeFromEnum(resolved);
    if (target == "pt") return sourceText; // PT é o idioma base, não traduz

    const std::string cacheKey = target + "\n" + visible;
    {
        std::lock_guard<std::mutex> lock(m_TranslationMutex);
        if (!m_AutoTranslationEnabled)
            return sourceText;
        auto cached = m_AutoTranslations.find(cacheKey);
        if (cached != m_AutoTranslations.end())
            return cached->second.c_str();
        m_KnownUiStrings.insert(visible);
        QueueTranslation(visible, target);
    }

    return sourceText;
}

void Localization::SetAutoTranslationEnabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(m_TranslationMutex);
        if (m_AutoTranslationEnabled == enabled) return;
        m_AutoTranslationEnabled = enabled;
        m_TranslationError.clear();
        if (enabled)
        {
            const std::string target = LangCodeFromEnum(GetResolvedLanguage());
            if (target != "pt")
                for (const std::string& source : m_KnownUiStrings)
                    QueueTranslation(source, target);
        }
    }
    Save();
}

std::string Localization::GetAutoTranslationStatus() const
{
    std::lock_guard<std::mutex> lock(m_TranslationMutex);
    if (!m_AutoTranslationEnabled)
        return "A traducao automatica da interface esta desativada.";
    if (!m_TranslationError.empty())
        return "Servico de traducao indisponivel; exibindo texto original.";
    if (!m_TranslationQueue.empty() || !m_PendingTranslations.empty())
        return "Traduzindo textos da interface em segundo plano...";
    return "Apenas textos fixos da interface sao enviados ao tradutor.";
}

void Localization::RetryAutoTranslation()
{
    std::lock_guard<std::mutex> lock(m_TranslationMutex);
    m_TranslationError.clear();
    if (!m_AutoTranslationEnabled) return;
    const std::string target = LangCodeFromEnum(GetResolvedLanguage());
    if (target != "pt")
        for (const std::string& source : m_KnownUiStrings)
            QueueTranslation(source, target);
}

void Localization::QueueTranslation(const std::string& source, const std::string& target) const
{
    const std::string key = target + "\n" + source;
    if (m_AutoTranslations.find(key) != m_AutoTranslations.end() ||
        m_PendingTranslations.find(key) != m_PendingTranslations.end())
        return;
    m_PendingTranslations.insert(key);
    m_TranslationQueue.emplace_back(source, target);
    m_TranslationWake.notify_one();
}

// ─── Load / Save ────────────────────────────────────────────────────────────
void Localization::Load()
{
    try
    {
        std::ifstream f("Library/settings.json");
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.find("\"language\"") != std::string::npos)
            {
                if (line.find("\"en\"") != std::string::npos ||
                    line.find("\"EN\"") != std::string::npos)
                    m_Language = EditorLanguage::English;
                else if (line.find("\"es\"") != std::string::npos ||
                         line.find("\"ES\"") != std::string::npos)
                    m_Language = EditorLanguage::Spanish;
                else if (line.find("\"auto\"") != std::string::npos ||
                         line.find("\"AUTO\"") != std::string::npos)
                    m_Language = EditorLanguage::Auto;
                else
                    m_Language = EditorLanguage::Portuguese;
            }
            if (line.find("\"autoTranslateUI\"") != std::string::npos)
            {
                m_AutoTranslationEnabled = line.find("true") != std::string::npos;
            }
        }
    }
    catch (...) {}
}

void Localization::Save() const
{
    try
    {
        std::string content;
        {
            std::ifstream f("Library/settings.json");
            if (f.is_open())
                content = std::string(std::istreambuf_iterator<char>(f), {});
        }

        nlohmann::json settings = nlohmann::json::object();
        try
        {
            if (!content.empty())
                settings = nlohmann::json::parse(content);
            if (!settings.is_object())
                return;
        }
        catch (...) { if (!content.empty()) return; }

        std::string langCode;
        switch (m_Language)
        {
            case EditorLanguage::English:    langCode = "en";   break;
            case EditorLanguage::Spanish:    langCode = "es";   break;
            case EditorLanguage::Auto:       langCode = "auto"; break;
            default:                         langCode = "pt";   break;
        }
        settings["language"]        = langCode;
        settings["autoTranslateUI"] = m_AutoTranslationEnabled;

        std::ofstream out("Library/settings.json");
        if (out.is_open())
            out << settings.dump(2) << '\n';
    }
    catch (...) {}
}


void Localization::LoadAutoTranslationCache()
{
    try
    {
        std::ifstream input("Library/auto_translation_cache.json", std::ios::binary);
        if (!input) return;
        nlohmann::json cache;
        input >> cache;
        if (!cache.is_object()) return;
        std::lock_guard<std::mutex> lock(m_TranslationMutex);
        for (auto it = cache.begin(); it != cache.end(); ++it)
        {
            if (it.value().is_string())
                m_AutoTranslations.emplace(it.key(), it.value().get<std::string>());
        }
    }
    catch (...) {}
}

void Localization::SaveAutoTranslationCache() const
{
    try
    {
        nlohmann::json cache = nlohmann::json::object();
        {
            std::lock_guard<std::mutex> lock(m_TranslationMutex);
            for (const auto& item : m_AutoTranslations)
                cache[item.first] = item.second;
        }
        if (cache.empty()) return;
        fs::create_directories("Library");
        const fs::path temporaryPath = "Library/auto_translation_cache.json.tmp";
        {
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output) return;
            output << cache.dump(2) << '\n';
        }
        std::error_code ec;
        fs::rename(temporaryPath, "Library/auto_translation_cache.json", ec);
        if (ec)
        {
            fs::remove("Library/auto_translation_cache.json", ec);
            ec.clear();
            fs::rename(temporaryPath, "Library/auto_translation_cache.json", ec);
        }
    }
    catch (...) {}
}

void Localization::TranslationWorker()
{
    while (!m_StopTranslationWorker.load())
    {
        std::vector<std::pair<std::string, std::string>> batch;
        {
            std::unique_lock<std::mutex> lock(m_TranslationMutex);
            m_TranslationWake.wait(lock, [this] {
                return m_StopTranslationWorker.load() || !m_TranslationQueue.empty();
            });
            if (m_StopTranslationWorker.load()) break;

            const std::string target = m_TranslationQueue.front().second;
            while (!m_TranslationQueue.empty() && batch.size() < 24 &&
                m_TranslationQueue.front().second == target)
            {
                batch.emplace_back(std::move(m_TranslationQueue.front()));
                m_TranslationQueue.pop_front();
            }
        }

        std::vector<std::string> source;
        source.reserve(batch.size());
        for (const auto& item : batch) source.push_back(item.first);

        std::vector<std::string> translated;
        std::string error;
        const std::string target = batch.empty() ? "en" : batch.front().second;
        const bool succeeded = TranslateBatch(source, target, translated, error);
        {
            std::lock_guard<std::mutex> lock(m_TranslationMutex);
            if (succeeded && translated.size() == batch.size())
            {
                for (size_t i = 0; i < batch.size(); ++i)
                {
                    const std::string cacheKey = batch[i].second + "\n" + batch[i].first;
                    m_AutoTranslations.emplace(cacheKey, std::move(translated[i]));
                    m_PendingTranslations.erase(cacheKey);
                }
                m_TranslationError.clear();
            }
            else
            {
                m_TranslationError = error.empty() ? "The translation service returned an invalid response." : error;
                for (const auto& item : batch)
                    m_PendingTranslations.erase(item.second + "\n" + item.first);
                // Don't hammer a community translation server if it's unavailable.
                m_TranslationQueue.clear();
            }
        }
        if (succeeded)
            SaveAutoTranslationCache();
        if (!m_StopTranslationWorker.load() && succeeded)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

bool Localization::TranslateBatch(const std::vector<std::string>& source,
    const std::string& target, std::vector<std::string>& translated,
    std::string& error) const
{
    translated.clear();
    error.clear();
    if (source.empty()) return true;

    translated.reserve(source.size());
    auto targetIt = m_Tables.find(target);
    const auto* targetTable = (targetIt != m_Tables.end()) ? &targetIt->second : nullptr;

    auto ptIt = m_Tables.find("pt");
    const auto* ptTable = (ptIt != m_Tables.end()) ? &ptIt->second : nullptr;

    for (const std::string& text : source)
    {
        std::string res;
        // 1. Se o proprio texto e chave na tabela alvo
        if (targetTable)
        {
            auto it = targetTable->find(text);
            if (it != targetTable->end())
                res = it->second;
        }

        // 2. Se o texto e valor em PT, busca a chave correspondente e pega o valor no alvo
        if (res.empty() && ptTable && targetTable)
        {
            for (const auto& kv : *ptTable)
            {
                if (kv.second == text)
                {
                    auto it = targetTable->find(kv.first);
                    if (it != targetTable->end())
                    {
                        res = it->second;
                        break;
                    }
                }
            }
        }

        // 3. Fallback: mantem o texto original (offline, instantaneo e seguro)
        if (res.empty())
            res = text;

        translated.push_back(std::move(res));
    }

    return true;
}
