#include "DiagnosticLog.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

namespace MaraeDiagnostics
{
    namespace
    {
        std::mutex& LogMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        uint64_t NowMs()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        }

        LogLevel FromNativeLevel(int level)
        {
            switch (level)
            {
            case 0: return LogLevel::Debug;
            case 1: return LogLevel::Info;
            case 2: return LogLevel::Warning;
            case 3: return LogLevel::Error;
            default: return LogLevel::Info;
            }
        }

        const char* NativeCategory(const std::string& message)
        {
            if (message.find("AssemblyManager") != std::string::npos ||
                message.find("SourceFile(") != std::string::npos ||
                message.find("CS") != std::string::npos)
            {
                return "C#";
            }
            return "Runtime";
        }

        std::string SuggestionFor(LogLevel level, const std::string& message)
        {
            if (message.find("SourceFile(") != std::string::npos ||
                message.find("CS") != std::string::npos)
            {
                return "Abra o arquivo indicado, corrija o diagnóstico e recompile os scripts.";
            }
            if (message.find("HostFxr") != std::string::npos)
            {
                return "Verifique a instalação do .NET 8 e reinicie a Marae Engine.";
            }
            if (message.find("mesh") != std::string::npos ||
                message.find("Mesh") != std::string::npos)
            {
                return "Confira se o asset existe e se a cena aponta para um caminho válido.";
            }
            if (level == LogLevel::Error)
                return "Abra o contexto indicado no console e verifique a configuração relacionada.";
            if (level == LogLevel::Warning)
                return "Revise o contexto indicado; a execução pode continuar com comportamento limitado.";
            return {};
        }
    }

    DiagnosticLog& DiagnosticLog::Get()
    {
        static DiagnosticLog instance;
        return instance;
    }

    void DiagnosticLog::Add(
        LogLevel level,
        std::string message,
        std::string category,
        std::string file,
        int line,
        int column,
        std::string object,
        std::string scene,
        std::string suggestion)
    {
        std::lock_guard<std::mutex> lock(LogMutex());
        LogEntry entry;
        entry.Sequence = m_NextSequence++;
        entry.TimestampMs = NowMs();
        entry.Level = level;
        entry.Category = std::move(category);
        entry.Message = std::move(message);
        entry.File = std::move(file);
        entry.Line = line;
        entry.Column = column;
        entry.Object = std::move(object);
        entry.Scene = std::move(scene);
        entry.Suggestion = std::move(suggestion);
        m_Entries.push_back(std::move(entry));
        if (m_Entries.size() > MaxEntries)
            m_Entries.erase(m_Entries.begin(), m_Entries.begin() +
                static_cast<std::ptrdiff_t>(m_Entries.size() - MaxEntries));
    }

    void DiagnosticLog::AddNative(
        int nativeLevel,
        const std::string& message,
        std::string file,
        int line,
        std::string object,
        std::string scene)
    {
        const LogLevel level = FromNativeLevel(nativeLevel);
        Add(level, message, NativeCategory(message), std::move(file), line, 0,
            std::move(object), std::move(scene), SuggestionFor(level, message));
    }

    std::vector<LogEntry> DiagnosticLog::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(LogMutex());
        return m_Entries;
    }

    void DiagnosticLog::Clear()
    {
        std::lock_guard<std::mutex> lock(LogMutex());
        m_Entries.clear();
    }

    std::size_t DiagnosticLog::Size() const
    {
        std::lock_guard<std::mutex> lock(LogMutex());
        return m_Entries.size();
    }

    std::size_t DiagnosticLog::Count(LogLevel level) const
    {
        std::lock_guard<std::mutex> lock(LogMutex());
        return static_cast<std::size_t>(std::count_if(
            m_Entries.begin(), m_Entries.end(),
            [level](const LogEntry& entry) { return entry.Level == level; }));
    }

    int DiagnosticLog::RunRegressionTests()
    {
        DiagnosticLog& log = Get();
        log.Clear();
        int failures = 0;
        const auto check = [&failures](bool condition, const char* name)
        {
            std::cout << (condition ? "[DiagnosticTest] PASS: " : "[DiagnosticTest] FAIL: ")
                      << name << std::endl;
            if (!condition) ++failures;
        };

        log.Add(LogLevel::Error, "falha de teste", "C#", "Assets/Scripts/Test.cs", 12,
                4, "Cube", "Assets/Scenes/Main.scene", "corrija a linha indicada");
        log.AddNative(2, "aviso de teste");
        const std::vector<LogEntry> entries = log.Snapshot();
        check(entries.size() == 2, "armazena entradas");
        check(entries.front().File == "Assets/Scripts/Test.cs" &&
              entries.front().Line == 12, "preserva arquivo e linha");
        check(entries.front().Object == "Cube" &&
              entries.front().Scene == "Assets/Scenes/Main.scene", "preserva contexto");
        check(log.Count(LogLevel::Error) == 1 && log.Count(LogLevel::Warning) == 1,
              "contabiliza niveis");

        for (std::size_t i = 0; i < MaxEntries + 16; ++i)
            log.Add(LogLevel::Debug, "entrada limitada");
        check(log.Size() == MaxEntries, "limita crescimento do console");
        log.Clear();
        check(log.Size() == 0, "limpa o console");

        constexpr int threadCount = 4;
        constexpr int entriesPerThread = 256;
        std::vector<std::thread> writers;
        writers.reserve(threadCount);
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
        {
            writers.emplace_back([threadIndex]()
            {
                for (int entryIndex = 0; entryIndex < entriesPerThread; ++entryIndex)
                    Get().Add(LogLevel::Info,
                        "concorrente " + std::to_string(threadIndex) + ":" +
                        std::to_string(entryIndex));
            });
        }
        for (std::thread& writer : writers)
            writer.join();
        const std::vector<LogEntry> concurrentEntries = log.Snapshot();
        bool sequenceIsMonotonic = true;
        for (std::size_t i = 1; i < concurrentEntries.size(); ++i)
            sequenceIsMonotonic = sequenceIsMonotonic &&
                concurrentEntries[i - 1].Sequence < concurrentEntries[i].Sequence;
        check(concurrentEntries.size() == threadCount * entriesPerThread && sequenceIsMonotonic,
              "mantem entradas e sequencia consistentes com escritores concorrentes");
        log.Clear();
        std::cout << "[DiagnosticTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
                  << " (" << failures << " falha(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }
}
