#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace MaraeScripting
{
    struct ScriptDebugSnapshot
    {
        bool Paused = false;
        bool BreakOnExceptions = false;
        uint64_t EntityId = 0;
        std::string TypeName;
        std::string Method;
        std::string File;
        std::string Message;
        std::string StackTrace;
        std::string VariablesJson;
        std::vector<std::string> CallStack;
    };

    // Debugger leve do runtime C#: não interrompe a engine por padrão.
    // Os breakpoints atuais são por tipo/método de ciclo de vida; o suporte
    // a linha de código depende de um protocolo de debugger futuro.
    class ScriptDebugger
    {
    public:
        static ScriptDebugger& Instance();

        void SetBreakOnExceptions(bool enabled);
        bool GetBreakOnExceptions() const;

        void SetBreakpoint(const std::string& typeName, const std::string& methodName);
        void RemoveBreakpoint(const std::string& typeName, const std::string& methodName);
        void ClearBreakpoints();
        std::vector<std::string> GetBreakpoints() const;

        bool ShouldBreak(uint64_t entityId, const std::string& typeName,
                         const std::string& methodName);
        void ReportException(uint64_t entityId, const std::string& typeName,
                             const std::string& methodName, const std::string& message,
                             const std::string& stackTrace,
                             const std::string& variablesJson);
        void ReportBreakpoint(uint64_t entityId, const std::string& typeName,
                              const std::string& methodName,
                              const std::string& variablesJson);

        void Continue();
        void ClearSession();
        bool IsPaused() const;
        ScriptDebugSnapshot GetSnapshot() const;

        // Exercita o estado do debugger sem depender do host .NET ou de uma cena.
        static int RunRegressionTests();

    private:
        ScriptDebugger() = default;

        static std::string MakeKey(const std::string& typeName, const std::string& methodName);
        static std::string Trim(std::string value);

        mutable std::mutex m_Mutex;
        bool m_BreakOnExceptions = false;
        bool m_Paused = false;
        std::string m_SkipOnceKey;
        std::string m_CurrentBreakpointKey;
        std::vector<std::string> m_BreakpointKeys;
        ScriptDebugSnapshot m_Snapshot;
    };
}
