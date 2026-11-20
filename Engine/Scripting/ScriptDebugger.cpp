#include "ScriptDebugger.h"

#include <algorithm>
#include <iostream>

namespace MaraeScripting
{
    ScriptDebugger& ScriptDebugger::Instance()
    {
        static ScriptDebugger instance;
        return instance;
    }

    std::string ScriptDebugger::Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string ScriptDebugger::MakeKey(const std::string& typeName, const std::string& methodName)
    {
        return Trim(typeName) + "::" + Trim(methodName);
    }

    void ScriptDebugger::SetBreakOnExceptions(bool enabled)
    {
        std::scoped_lock lock(m_Mutex);
        m_BreakOnExceptions = enabled;
        m_Snapshot.BreakOnExceptions = enabled;
    }

    bool ScriptDebugger::GetBreakOnExceptions() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_BreakOnExceptions;
    }

    void ScriptDebugger::SetBreakpoint(const std::string& typeName, const std::string& methodName)
    {
        const std::string key = MakeKey(typeName, methodName);
        if (key == "::") return;
        std::scoped_lock lock(m_Mutex);
        if (std::find(m_BreakpointKeys.begin(), m_BreakpointKeys.end(), key) == m_BreakpointKeys.end())
            m_BreakpointKeys.push_back(key);
    }

    void ScriptDebugger::RemoveBreakpoint(const std::string& typeName, const std::string& methodName)
    {
        const std::string key = MakeKey(typeName, methodName);
        std::scoped_lock lock(m_Mutex);
        m_BreakpointKeys.erase(std::remove(m_BreakpointKeys.begin(), m_BreakpointKeys.end(), key),
                               m_BreakpointKeys.end());
    }

    void ScriptDebugger::ClearBreakpoints()
    {
        std::scoped_lock lock(m_Mutex);
        m_BreakpointKeys.clear();
        m_SkipOnceKey.clear();
        m_CurrentBreakpointKey.clear();
    }

    std::vector<std::string> ScriptDebugger::GetBreakpoints() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_BreakpointKeys;
    }

    bool ScriptDebugger::ShouldBreak(uint64_t entityId, const std::string& typeName,
                                     const std::string& methodName)
    {
        std::string matchingKey;
        std::scoped_lock lock(m_Mutex);
        for (const std::string& breakpoint : m_BreakpointKeys)
        {
            const size_t separator = breakpoint.rfind("::");
            if (separator == std::string::npos) continue;
            const std::string breakpointType = breakpoint.substr(0, separator);
            const std::string breakpointMethod = breakpoint.substr(separator + 2);
            const bool typeMatches = breakpointType == typeName ||
                (typeName.size() > breakpointType.size() &&
                 typeName.compare(typeName.size() - breakpointType.size(), breakpointType.size(), breakpointType) == 0 &&
                 typeName[typeName.size() - breakpointType.size() - 1] == '.');
            if (typeMatches && breakpointMethod == methodName)
            {
                matchingKey = breakpoint;
                break;
            }
        }
        if (m_Paused || matchingKey.empty())
            return m_Paused;
        if (m_SkipOnceKey == matchingKey)
        {
            m_SkipOnceKey.clear();
            return false;
        }

        m_Paused = true;
        m_Snapshot.Paused = true;
        m_Snapshot.BreakOnExceptions = m_BreakOnExceptions;
        m_Snapshot.EntityId = entityId;
        m_Snapshot.TypeName = typeName;
        m_Snapshot.Method = methodName;
        m_Snapshot.File.clear();
        m_Snapshot.Message = "Breakpoint atingido: " + matchingKey;
        m_CurrentBreakpointKey = matchingKey;
        m_Snapshot.StackTrace.clear();
        m_Snapshot.VariablesJson = "{}";
        m_Snapshot.CallStack = { typeName + "." + methodName };
        return true;
    }

    void ScriptDebugger::ReportException(uint64_t entityId, const std::string& typeName,
                                         const std::string& methodName, const std::string& message,
                                         const std::string& stackTrace,
                                         const std::string& variablesJson)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot.EntityId = entityId;
        m_Snapshot.TypeName = typeName;
        m_Snapshot.Method = methodName;
        m_Snapshot.File.clear();
        m_Snapshot.Message = message;
        m_Snapshot.StackTrace = stackTrace;
        m_Snapshot.VariablesJson = variablesJson.empty() ? "{}" : variablesJson;
        m_Snapshot.CallStack.clear();
        m_Snapshot.CallStack.push_back(typeName + "." + methodName);
        m_Snapshot.BreakOnExceptions = m_BreakOnExceptions;
        if (m_BreakOnExceptions)
        {
            m_Paused = true;
            m_Snapshot.Paused = true;
        }
    }

    void ScriptDebugger::ReportBreakpoint(uint64_t entityId, const std::string& typeName,
                                          const std::string& methodName,
                                          const std::string& variablesJson)
    {
        std::scoped_lock lock(m_Mutex);
        if (!m_Paused) return;
        m_Snapshot.EntityId = entityId;
        m_Snapshot.TypeName = typeName;
        m_Snapshot.Method = methodName;
        m_Snapshot.VariablesJson = variablesJson.empty() ? "{}" : variablesJson;
        m_Snapshot.CallStack = { typeName + "." + methodName };
    }

    void ScriptDebugger::Continue()
    {
        std::scoped_lock lock(m_Mutex);
        if (!m_CurrentBreakpointKey.empty())
            m_SkipOnceKey = m_CurrentBreakpointKey;
        m_CurrentBreakpointKey.clear();
        m_Paused = false;
        m_Snapshot.Paused = false;
    }

    void ScriptDebugger::ClearSession()
    {
        std::scoped_lock lock(m_Mutex);
        m_Paused = false;
        m_SkipOnceKey.clear();
        m_CurrentBreakpointKey.clear();
        m_Snapshot = {};
        m_Snapshot.BreakOnExceptions = m_BreakOnExceptions;
    }

    bool ScriptDebugger::IsPaused() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Paused;
    }

    ScriptDebugSnapshot ScriptDebugger::GetSnapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    int ScriptDebugger::RunRegressionTests()
    {
        ScriptDebugger& debugger = Instance();
        const bool oldBreakOnExceptions = debugger.GetBreakOnExceptions();
        debugger.ClearSession();
        debugger.ClearBreakpoints();
        debugger.SetBreakOnExceptions(false);

        int failures = 0;
        auto check = [&](bool condition, const char* name)
        {
            if (condition) std::cout << "[ScriptDebugger-Test] OK: " << name << "\n";
            else { std::cerr << "[ScriptDebugger-Test] FALHA: " << name << "\n"; ++failures; }
        };

        debugger.SetBreakpoint("TestController", "OnUpdate");
        check(debugger.ShouldBreak(7, "TestController", "OnUpdate"), "breakpoint pausa no método");
        check(debugger.IsPaused(), "estado pausado visível");
        debugger.Continue();
        check(!debugger.IsPaused(), "continue libera o runtime");
        check(!debugger.ShouldBreak(7, "TestController", "OnUpdate"), "continue ignora a repetição imediata");
        check(debugger.ShouldBreak(7, "TestController", "OnUpdate"), "breakpoint volta no próximo ciclo");

        debugger.ClearSession();
        debugger.ClearBreakpoints();
        debugger.SetBreakpoint("Game.Scripts.TestController", "OnUpdate");
        check(debugger.ShouldBreak(7, "Game.Scripts.TestController", "OnUpdate"),
              "breakpoint com nome totalmente qualificado pausa");
        debugger.Continue();
        check(!debugger.ShouldBreak(7, "Game.Scripts.TestController", "OnUpdate"),
              "continue ignora breakpoint qualificado na chamada atual");
        check(debugger.ShouldBreak(7, "Game.Scripts.TestController", "OnUpdate"),
              "breakpoint qualificado volta no ciclo seguinte");

        debugger.ClearSession();
        debugger.ClearBreakpoints();
        debugger.SetBreakpoint("TestController", "OnUpdate");
        check(debugger.ShouldBreak(7, "Game.Scripts.TestController", "OnUpdate"),
              "breakpoint curto corresponde ao tipo com namespace");
        debugger.Continue();
        check(!debugger.ShouldBreak(7, "Game.Scripts.TestController", "OnUpdate"),
              "continue ignora chave curta correspondente ao tipo qualificado");

        debugger.SetBreakOnExceptions(true);
        debugger.ReportException(9, "BrokenController", "Update", "erro de teste",
                                 "BrokenController.Update()", "{\"Speed\":5}");
        const ScriptDebugSnapshot snapshot = debugger.GetSnapshot();
        check(debugger.IsPaused() && snapshot.Message == "erro de teste", "exceção pausa com diagnóstico");
        check(snapshot.VariablesJson.find("Speed") != std::string::npos, "variáveis capturadas");

        debugger.ClearSession();
        debugger.ClearBreakpoints();
        debugger.SetBreakOnExceptions(oldBreakOnExceptions);
        return failures == 0 ? 0 : 1;
    }
}
