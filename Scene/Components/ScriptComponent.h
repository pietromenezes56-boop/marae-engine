#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

struct ScriptComponent
{
    std::string ScriptPath;
    bool Enabled = true;
    bool Dirty = false;

    std::string ScriptAssemblyQualifiedTypeName;
    std::unordered_map<std::string, std::string> SerializedFields;
    uint64_t ScriptInstanceId = 0;

    void SetDirty(bool dirty) { Dirty = dirty; }
    bool IsDirty() const { return Dirty; }

    void SetField(const std::string& name, const std::string& jsonValue)
    {
        SerializedFields[name] = jsonValue;
    }

    std::optional<std::string> GetField(const std::string& name) const
    {
        auto it = SerializedFields.find(name);
        if (it != SerializedFields.end())
            return it->second;
        return std::nullopt;
    }
};
