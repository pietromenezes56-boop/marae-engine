#include "UIInputBridge.h"

#include "../../Scene/Scene/Scene.h"

#include <imgui.h>
#include <algorithm>

namespace MaraeUI
{
    bool UpdateInteractionFromImGui(Scene& scene,
                                    const UIInputBridgeParams& params,
                                    UIInteractionResult& result)
    {
        result = UIInteractionResult{};

        if (!params.InteractionEnabled)
            return false;

        if (ImGui::GetCurrentContext() == nullptr)
            return false;

        ImGuiIO& io = ImGui::GetIO();

        UIInputState input;
        input.PointerDown = io.MouseDown[0];
        input.PointerPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input.PointerReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        input.WheelDelta = io.MouseWheel;
        input.KeyBackspace = ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
        input.KeyDelete = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
        input.KeyEnter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        input.KeyLeft = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);
        input.KeyRight = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
        input.KeyHome = ImGui::IsKeyPressed(ImGuiKey_Home, false);
        input.KeyEnd = ImGui::IsKeyPressed(ImGuiKey_End, false);

        // Caracteres digitados -> UTF-8 (usado pelo TextBox).
        input.TextInput.reserve(static_cast<size_t>(std::max(0, io.InputQueueCharacters.Size)));
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
        {
            const unsigned int cp = static_cast<unsigned int>(io.InputQueueCharacters[i]);
            if (cp == 0)
                continue;
            if (cp < 0x80)
                input.TextInput.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                input.TextInput.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                input.TextInput.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                input.TextInput.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                input.TextInput.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                input.TextInput.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                input.TextInput.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                input.TextInput.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                input.TextInput.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                input.TextInput.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        // Ponteiro -> pixels do canvas de referência (960x540).
        if (params.RuntimeGame && params.GameImageValid &&
            params.GameImageWidth > 1.0f && params.GameImageHeight > 1.0f)
        {
            const float localX = io.MousePos.x - params.GameImageMinX;
            const float localY = io.MousePos.y - params.GameImageMinY;
            if (localX >= 0.0f && localX <= params.GameImageWidth &&
                localY >= 0.0f && localY <= params.GameImageHeight)
            {
                input.PointerX = localX / params.GameImageWidth * kInteractionRefWidth;
                input.PointerY = localY / params.GameImageHeight * kInteractionRefHeight;
                input.PointerValid = true;
            }
        }
        else if (params.ViewportPointerValid && params.ViewportWidth > 0 && params.ViewportHeight > 0)
        {
            input.PointerX = params.ViewportPointerX / static_cast<float>(params.ViewportWidth) *
                kInteractionRefWidth;
            input.PointerY = params.ViewportPointerY / static_cast<float>(params.ViewportHeight) *
                kInteractionRefHeight;
            input.PointerValid = true;
        }

        result = UpdateInteraction(scene.GetEntities(), input);
        return true;
    }
}
