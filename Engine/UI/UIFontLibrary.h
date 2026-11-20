#pragma once

#include "../../Scene/Components/UIComponent.h"
#include <imgui.h>
#include <array>
#include <string>

namespace MaraeUI
{
    inline constexpr std::array<const char*, 7> UIFontNames = {
        "Default", "Segoe UI", "Arial", "Calibri", "Consolas", "Tahoma", "Verdana"
    };

    inline int GetUIFontIndex(const std::string& name)
    {
        for (int i = 0; i < static_cast<int>(UIFontNames.size()); ++i)
            if (name == UIFontNames[static_cast<size_t>(i)]) return i;
        return 0;
    }

    inline ImFont* GetUIFont(const UIComponent& ui)
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return nullptr;
        ImGuiIO& io = ImGui::GetIO();
        // O slot 0 é a fonte padrão; os demais seguem UIFontNames.
        const int atlasIndex = GetUIFontIndex(ui.Font);
        if (io.Fonts && atlasIndex >= 0 && atlasIndex < io.Fonts->Fonts.Size)
            return io.Fonts->Fonts[atlasIndex];
        return io.FontDefault ? io.FontDefault : ImGui::GetFont();
    }
}
