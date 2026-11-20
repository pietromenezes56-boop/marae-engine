#include "InputMap.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace MaraeInput
{
    namespace
    {
        constexpr float kDefaultDeadZone = 0.19f;

        bool IsValidToken(const std::string& value)
        {
            return !value.empty() && value.find_first_of(" \t\r\n") == std::string::npos;
        }

        bool ParseType(const std::string& text, BindingType& type)
        {
            if (text == "key") type = BindingType::Key;
            else if (text == "mouse") type = BindingType::MouseButton;
            else if (text == "gamepad_button") type = BindingType::GamepadButton;
            else if (text == "gamepad_axis") type = BindingType::GamepadAxis;
            else return false;
            return true;
        }

        bool IsValidBinding(BindingType type, int code, float scale)
        {
            if (!std::isfinite(scale)) return false;
            switch (type)
            {
            case BindingType::Key: return code >= 0 && code <= GLFW_KEY_LAST;
            case BindingType::MouseButton: return code >= 0 && code <= GLFW_MOUSE_BUTTON_LAST;
            case BindingType::GamepadButton: return code >= 0 && code <= GLFW_GAMEPAD_BUTTON_LAST;
            case BindingType::GamepadAxis: return code >= 0 && code <= GLFW_GAMEPAD_AXIS_LAST;
            }
            return false;
        }

        std::string Trim(const std::string& value)
        {
            const size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        const char* TypeName(BindingType type)
        {
            switch (type)
            {
            case BindingType::MouseButton: return "mouse";
            case BindingType::GamepadButton: return "gamepad_button";
            case BindingType::GamepadAxis: return "gamepad_axis";
            default: return "key";
            }
        }
    }

    InputMap& InputMap::Instance()
    {
        static InputMap map;
        return map;
    }

    InputMap::InputMap()
    {
        ResetDefaults();
    }

    void InputMap::AddDefault(const std::string& name, BindingType type, int code, float scale)
    {
        m_Bindings[name].push_back({ type, code, scale });
    }

    void InputMap::ResetDefaults()
    {
        m_Bindings.clear();
        m_DeadZones.clear();

        AddDefault("Jump", BindingType::Key, GLFW_KEY_SPACE);
        AddDefault("Jump", BindingType::Key, GLFW_KEY_UP);
        AddDefault("Jump", BindingType::MouseButton, GLFW_MOUSE_BUTTON_LEFT);
        AddDefault("Submit", BindingType::Key, GLFW_KEY_ENTER);
        AddDefault("Cancel", BindingType::Key, GLFW_KEY_ESCAPE);

        AddDefault("Horizontal", BindingType::Key, GLFW_KEY_D, 1.0f);
        AddDefault("Horizontal", BindingType::Key, GLFW_KEY_RIGHT, 1.0f);
        AddDefault("Horizontal", BindingType::Key, GLFW_KEY_A, -1.0f);
        AddDefault("Horizontal", BindingType::Key, GLFW_KEY_LEFT, -1.0f);
        AddDefault("Vertical", BindingType::Key, GLFW_KEY_W, 1.0f);
        AddDefault("Vertical", BindingType::Key, GLFW_KEY_UP, 1.0f);
        AddDefault("Vertical", BindingType::Key, GLFW_KEY_S, -1.0f);
        AddDefault("Vertical", BindingType::Key, GLFW_KEY_DOWN, -1.0f);
        AddDefault("Horizontal", BindingType::GamepadAxis, GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f);
        AddDefault("Vertical", BindingType::GamepadAxis, GLFW_GAMEPAD_AXIS_LEFT_Y, -1.0f);
        AddDefault("Jump", BindingType::GamepadButton, GLFW_GAMEPAD_BUTTON_A);
        AddDefault("Submit", BindingType::GamepadButton, GLFW_GAMEPAD_BUTTON_A);
        AddDefault("Cancel", BindingType::GamepadButton, GLFW_GAMEPAD_BUTTON_B);

        m_DeadZones["Horizontal"] = kDefaultDeadZone;
        m_DeadZones["Vertical"] = kDefaultDeadZone;
    }

    void InputMap::Load(const std::string& projectRoot)
    {
        m_ProjectRoot = projectRoot;
        ResetDefaults();
        if (projectRoot.empty()) return;

        const std::filesystem::path path = std::filesystem::path(projectRoot) /
            "Library" / "input_map.cfg";
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::stringstream stream(line);
            std::string command;
            stream >> command;
            if (command == "deadzone")
            {
                std::string axis;
                float value = kDefaultDeadZone;
                if (stream >> axis >> value && IsValidToken(axis) && std::isfinite(value))
                    m_DeadZones[axis] = std::clamp(value, 0.0f, 0.99f);
            }
            else if (command == "clear")
            {
                std::string action;
                if (stream >> action && IsValidToken(action)) m_Bindings[action].clear();
            }
            else if (command == "bind")
            {
                std::string action, type;
                int code = 0;
                float scale = 1.0f;
                BindingType parsedType{};
                if (stream >> action >> type >> code >> scale && IsValidToken(action) &&
                    ParseType(type, parsedType) && IsValidBinding(parsedType, code, scale))
                    m_Bindings[action].push_back({ parsedType, code, scale });
            }
        }
    }

    bool InputMap::Save() const
    {
        if (m_ProjectRoot.empty()) return false;
        const std::filesystem::path path = std::filesystem::path(m_ProjectRoot) /
            "Library" / "input_map.cfg";
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        std::filesystem::path temporaryPath = path;
        temporaryPath += ".tmp";
        std::filesystem::path backupPath = path;
        backupPath += ".bak";

        std::ofstream file(temporaryPath, std::ios::trunc);
        if (!file.is_open()) return false;
        file << "# Marae Engine Input Map v1\n";
        for (const auto& [axis, deadZone] : m_DeadZones)
            file << "deadzone " << axis << " " << deadZone << "\n";
        for (const auto& [action, bindings] : m_Bindings)
        {
            file << "clear " << action << "\n";
            for (const Binding& binding : bindings)
                file << "bind " << action << " " << TypeName(binding.Type) << " "
                     << binding.Code << " " << binding.Scale << "\n";
        }
        file.flush();
        const bool writeSucceeded = file.good();
        file.close();
        if (!writeSucceeded || !file)
        {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        const bool hadOriginal = std::filesystem::exists(path, error) && !error;
        if (hadOriginal)
        {
            std::filesystem::remove(backupPath, error);
            error.clear();
            std::filesystem::rename(path, backupPath, error);
            if (error)
            {
                std::filesystem::remove(temporaryPath, error);
                return false;
            }
        }

        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
        if (error)
        {
            if (hadOriginal)
            {
                std::error_code restoreError;
                std::filesystem::rename(backupPath, path, restoreError);
            }
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        if (hadOriginal)
        {
            error.clear();
            std::filesystem::remove(backupPath, error);
        }
        return true;
    }

    void InputMap::Update(const std::array<bool, GLFW_KEY_LAST + 1>& keys,
                          const std::array<bool, GLFW_KEY_LAST + 1>& previousKeys,
                          const std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1>& mouseButtons,
                          const std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1>& previousMouseButtons,
                          float mouseDeltaX,
                          float mouseDeltaY)
    {
        m_PreviousKeys = previousKeys;
        m_Keys = keys;
        m_PreviousMouseButtons = previousMouseButtons;
        m_MouseButtons = mouseButtons;
        m_MouseDeltaX = mouseDeltaX;
        m_MouseDeltaY = mouseDeltaY;
        m_PreviousGamepadAxes = m_GamepadAxes;
        m_PreviousGamepadButtons = m_GamepadButtons;

        m_GamepadAxes.fill(0.0f);
        m_GamepadButtons.fill(false);
        for (int jid = 0; jid <= GLFW_JOYSTICK_LAST; ++jid)
        {
            GLFWgamepadstate state{};
            if (!glfwJoystickIsGamepad(jid) || glfwGetGamepadState(jid, &state) != GLFW_TRUE)
                continue;
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis)
                if (axis < static_cast<int>(m_GamepadAxes.size()))
                    m_GamepadAxes[static_cast<size_t>(axis)] = state.axes[axis];
            for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button)
                if (button < static_cast<int>(m_GamepadButtons.size()))
                    m_GamepadButtons[static_cast<size_t>(button)] =
                        state.buttons[button] == GLFW_PRESS;
            break;
        }
    }

    const std::vector<Binding>* InputMap::FindBindings(const std::string& name) const
    {
        const auto it = m_Bindings.find(name);
        return it == m_Bindings.end() ? nullptr : &it->second;
    }

    float InputMap::EvaluateBinding(const Binding& binding, bool previous) const
    {
        switch (binding.Type)
        {
        case BindingType::Key:
            if (binding.Code < 0 || binding.Code > GLFW_KEY_LAST) return 0.0f;
            return (previous ? m_PreviousKeys : m_Keys)[static_cast<size_t>(binding.Code)]
                ? binding.Scale : 0.0f;
        case BindingType::MouseButton:
            if (binding.Code < 0 || binding.Code > GLFW_MOUSE_BUTTON_LAST) return 0.0f;
            return (previous ? m_PreviousMouseButtons : m_MouseButtons)[static_cast<size_t>(binding.Code)]
                ? binding.Scale : 0.0f;
        case BindingType::GamepadButton:
            if (binding.Code < 0 || binding.Code >= static_cast<int>(m_GamepadButtons.size())) return 0.0f;
            return (previous ? m_PreviousGamepadButtons : m_GamepadButtons)[static_cast<size_t>(binding.Code)]
                ? binding.Scale : 0.0f;
        case BindingType::GamepadAxis:
            if (binding.Code < 0 || binding.Code >= static_cast<int>(m_GamepadAxes.size())) return 0.0f;
            return (previous ? m_PreviousGamepadAxes : m_GamepadAxes)[static_cast<size_t>(binding.Code)] * binding.Scale;
        }
        return 0.0f;
    }

    float InputMap::EvaluateBindings(const std::string& name, bool previous) const
    {
        const std::vector<Binding>* bindings = FindBindings(name);
        if (!bindings) return 0.0f;
        float result = 0.0f;
        for (const Binding& binding : *bindings)
        {
            const float value = EvaluateBinding(binding, previous);
            if (std::abs(value) > std::abs(result)) result = value;
        }
        return result;
    }

    bool InputMap::GetAction(const std::string& action) const
    {
        return std::abs(EvaluateBindings(action, false)) > 0.5f;
    }

    bool InputMap::GetActionDown(const std::string& action) const
    {
        return GetAction(action) && std::abs(EvaluateBindings(action, true)) <= 0.5f;
    }

    bool InputMap::GetActionUp(const std::string& action) const
    {
        return !GetAction(action) && std::abs(EvaluateBindings(action, true)) > 0.5f;
    }

    float InputMap::GetAxis(const std::string& axis, bool raw) const
    {
        if (axis == "Mouse X" || axis == "mouse x") return m_MouseDeltaX;
        if (axis == "Mouse Y" || axis == "mouse y") return m_MouseDeltaY;
        float value = std::clamp(EvaluateBindings(axis, false), -1.0f, 1.0f);
        if (raw) return value;
        const float deadZone = std::clamp(m_DeadZones.contains(axis) ? m_DeadZones.at(axis) : kDefaultDeadZone,
                                          0.0f, 0.99f);
        if (std::abs(value) <= deadZone) return 0.0f;
        return std::copysign((std::abs(value) - deadZone) / (1.0f - deadZone), value);
    }

    void InputMap::BindKey(const std::string& action, int keyCode, float scale)
    {
        if (!IsValidToken(action) || !IsValidBinding(BindingType::Key, keyCode, scale)) return;
        m_Bindings[action].push_back({ BindingType::Key, keyCode, scale });
        Save();
    }

    void InputMap::BindMouseButton(const std::string& action, int button, float scale)
    {
        if (!IsValidToken(action) || !IsValidBinding(BindingType::MouseButton, button, scale)) return;
        m_Bindings[action].push_back({ BindingType::MouseButton, button, scale });
        Save();
    }

    void InputMap::ClearBindings(const std::string& action)
    {
        if (!IsValidToken(action)) return;
        m_Bindings[action].clear();
        Save();
    }

    void InputMap::SetDeadZone(const std::string& axis, float deadZone)
    {
        if (!IsValidToken(axis) || !std::isfinite(deadZone)) return;
        m_DeadZones[axis] = std::clamp(deadZone, 0.0f, 0.99f);
        Save();
    }

    int InputMap::RunRegressionTests()
    {
        int failures = 0;
        const auto check = [&failures](bool value, const char* message)
        {
            std::cout << (value ? "[InputTest] PASS: " : "[InputTest] FAIL: ") << message << std::endl;
            if (!value) ++failures;
        };

        InputMap test;
        std::array<bool, GLFW_KEY_LAST + 1> keys{};
        std::array<bool, GLFW_KEY_LAST + 1> previous{};
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouse{};
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> previousMouse{};
        keys[GLFW_KEY_SPACE] = true;
        keys[GLFW_KEY_D] = true;
        test.Update(keys, previous, mouse, previousMouse, 0.0f, 0.0f);
        check(test.GetAction("Jump") && test.GetActionDown("Jump"), "acao e borda de pressionamento");
        check(test.GetAxis("Horizontal", true) > 0.9f, "eixo positivo do teclado");
        previous = keys;
        test.Update(keys, previous, mouse, previousMouse, 0.0f, 0.0f);
        check(test.GetAction("Jump") && !test.GetActionDown("Jump"), "acao mantida nao repete GetActionDown");
        test.ClearBindings("Jump");
        test.BindKey("Jump", GLFW_KEY_F);
        keys.fill(false);
        keys[GLFW_KEY_F] = true;
        test.Update(keys, previous, mouse, previousMouse, 0.0f, 0.0f);
        check(test.GetActionDown("Jump"), "rebind de tecla funciona");
        test.SetDeadZone("Horizontal", 0.5f);
        check(test.GetAxis("Horizontal", false) == 0.0f, "dead zone e aplicada");

        const std::filesystem::path projectRoot =
            std::filesystem::temp_directory_path() / "marae_input_m007_regression";
        std::error_code error;
        std::filesystem::remove_all(projectRoot, error);
        test.Load(projectRoot.string());
        test.BindKey("PersistedAction", GLFW_KEY_G);
        test.BindMouseButton("PersistedMouse", GLFW_MOUSE_BUTTON_RIGHT);
        test.SetDeadZone("PersistedAxis", 0.37f);
        test.ClearBindings("PersistedMouse");
        InputMap reloaded;
        reloaded.Load(projectRoot.string());
        const auto* persistedAction = reloaded.FindBindings("PersistedAction");
        const auto* clearedMouse = reloaded.FindBindings("PersistedMouse");
        check(persistedAction && persistedAction->size() == 1 &&
              persistedAction->front().Code == GLFW_KEY_G,
              "bindings persistem depois de recarregar o projeto");
        check(clearedMouse && clearedMouse->empty() &&
              reloaded.m_DeadZones.contains("PersistedAxis") &&
              std::abs(reloaded.m_DeadZones.at("PersistedAxis") - 0.37f) < 0.001f,
              "limpar binding e dead zone persistem");
        check(reloaded.GetProjectRoot() == projectRoot.string(), "mapa reabre no projeto selecionado");
        std::filesystem::remove_all(projectRoot, error);
        std::cout << "[InputTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
                  << " (" << failures << " falha(s))" << std::endl;
        return failures == 0 ? 0 : 1;
    }
}
