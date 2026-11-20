#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>

namespace MaraeInput
{
    enum class BindingType
    {
        Key,
        MouseButton,
        GamepadButton,
        GamepadAxis
    };

    struct Binding
    {
        BindingType Type = BindingType::Key;
        int Code = 0;
        float Scale = 1.0f;
    };

    class InputMap
    {
    public:
        static InputMap& Instance();

        void Load(const std::string& projectRoot);
        bool Save() const;
        void ResetDefaults();

        void Update(const std::array<bool, GLFW_KEY_LAST + 1>& keys,
                    const std::array<bool, GLFW_KEY_LAST + 1>& previousKeys,
                    const std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1>& mouseButtons,
                    const std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1>& previousMouseButtons,
                    float mouseDeltaX,
                    float mouseDeltaY);

        bool GetAction(const std::string& action) const;
        bool GetActionDown(const std::string& action) const;
        bool GetActionUp(const std::string& action) const;
        float GetAxis(const std::string& axis, bool raw = false) const;

        void BindKey(const std::string& action, int keyCode, float scale = 1.0f);
        void BindMouseButton(const std::string& action, int button, float scale = 1.0f);
        void ClearBindings(const std::string& action);
        void SetDeadZone(const std::string& axis, float deadZone);

        const std::string& GetProjectRoot() const { return m_ProjectRoot; }
        static int RunRegressionTests();

    private:
        InputMap();
        const std::vector<Binding>* FindBindings(const std::string& name) const;
        float EvaluateBinding(const Binding& binding, bool previous) const;
        float EvaluateBindings(const std::string& name, bool previous) const;
        void AddDefault(const std::string& name, BindingType type, int code, float scale = 1.0f);

        std::unordered_map<std::string, std::vector<Binding>> m_Bindings;
        std::unordered_map<std::string, float> m_DeadZones;
        std::array<bool, GLFW_KEY_LAST + 1> m_Keys{};
        std::array<bool, GLFW_KEY_LAST + 1> m_PreviousKeys{};
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_MouseButtons{};
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_PreviousMouseButtons{};
        std::array<float, 16> m_GamepadAxes{};
        std::array<float, 16> m_PreviousGamepadAxes{};
        std::array<bool, 32> m_GamepadButtons{};
        std::array<bool, 32> m_PreviousGamepadButtons{};
        float m_MouseDeltaX = 0.0f;
        float m_MouseDeltaY = 0.0f;
        std::string m_ProjectRoot;
    };
}
