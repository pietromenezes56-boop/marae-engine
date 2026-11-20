#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MaraeDiagnostics
{
    enum class LogLevel : uint8_t
    {
        Debug = 0,
        Info,
        Warning,
        Error,
        Success
    };

    struct LogEntry
    {
        uint64_t Sequence = 0;
        uint64_t TimestampMs = 0;
        LogLevel Level = LogLevel::Info;
        std::string Category;
        std::string Message;
        std::string File;
        int Line = 0;
        int Column = 0;
        std::string Object;
        std::string Scene;
        std::string Suggestion;
    };

    class DiagnosticLog final
    {
    public:
        static DiagnosticLog& Get();

        void Add(
            LogLevel level,
            std::string message,
            std::string category = {},
            std::string file = {},
            int line = 0,
            int column = 0,
            std::string object = {},
            std::string scene = {},
            std::string suggestion = {}
        );

        void AddNative(
            int nativeLevel,
            const std::string& message,
            std::string file = {},
            int line = 0,
            std::string object = {},
            std::string scene = {}
        );

        std::vector<LogEntry> Snapshot() const;
        void Clear();
        std::size_t Size() const;
        std::size_t Count(LogLevel level) const;

        // Mantém o console útil em sessões longas sem crescer indefinidamente.
        static constexpr std::size_t MaxEntries = 2048;

        // Teste determinístico que não precisa criar janela nem iniciar o editor.
        static int RunRegressionTests();

    private:
        DiagnosticLog() = default;

        std::vector<LogEntry> m_Entries;
        uint64_t m_NextSequence = 1;
    };
}
