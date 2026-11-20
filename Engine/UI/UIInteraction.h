#pragma once

#include "UILayout.h"
#include "../../Scene/Entity/Entity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Sistema de interação nativo da UI 2D.
//
// Enquanto UIRenderer desenha os elementos, este módulo traduz o ponteiro e o
// teclado em estados de widget (hover/press/foco/valor), da mesma forma que o
// ScreenGui/GuiObject do Roblox Studio ou o Canvas do Unity. Os estados de
// runtime ficam em UIComponent (IsHovered/IsPressed/IsFocused/Checked/Value/...)
// e são lidos tanto pelo renderizador quanto pelos scripts.
//
// O módulo é independente de ImGui/GLFW: recebe um snapshot (UIInputState) já em
// pixels do canvas de referência (960x540), de forma que editor, runtime e um
// jogador standalone compartilhem exatamente a mesma lógica.
namespace MaraeUI
{
    constexpr float kInteractionRefWidth = 960.0f;
    constexpr float kInteractionRefHeight = 540.0f;

    // Snapshot de entrada de um frame. Coordenadas em pixels do canvas (960x540).
    struct UIInputState
    {
        float PointerX = 0.0f;
        float PointerY = 0.0f;
        bool PointerValid = false;     // ponteiro está sobre a área da UI/jogo
        bool PointerDown = false;      // botão esquerdo mantido
        bool PointerPressed = false;   // pressionado neste frame (edge)
        bool PointerReleased = false;  // solto neste frame (edge)
        float WheelDelta = 0.0f;       // rolagem vertical acumulada no frame
        std::string TextInput;         // caracteres digitados neste frame (UTF-8)
        bool KeyBackspace = false;
        bool KeyDelete = false;
        bool KeyEnter = false;
        bool KeyLeft = false;
        bool KeyRight = false;
        bool KeyHome = false;
        bool KeyEnd = false;
    };

    struct UIInteractionResult
    {
        bool PointerOverUI = false;     // ponteiro sobre elemento interativo (bloqueia o mundo)
        bool TextInputCaptured = false; // teclado direcionado a um TextBox
        uint64_t HoveredEntity = 0;
        uint64_t PressedEntity = 0;     // pressionado neste frame
        uint64_t ClickedEntity = 0;     // clique concluído (soltou sobre o elemento)
        uint64_t ChangedEntity = 0;     // valor/pressed alterado (slider/checkbox/dropdown/textbox)
        uint64_t FocusedTextBox = 0;    // TextBox com foco de teclado
    };

    namespace Detail
    {
        inline size_t NextCodepoint(const std::string& text, size_t index)
        {
            if (index >= text.size())
                return text.size();
            ++index;
            while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
                ++index;
            return index;
        }

        inline size_t PrevCodepoint(const std::string& text, size_t index)
        {
            if (index == 0)
                return 0;
            --index;
            while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
                --index;
            return index;
        }
    }

    inline UIInteractionResult UpdateInteraction(std::vector<Entity>& entities,
                                                 const UIInputState& input)
    {
        UIInteractionResult result;

        std::vector<Entity*> drawOrder;
        std::unordered_map<uint64_t, Entity*> byId;
        for (Entity& entity : entities)
            byId.emplace(entity.GetID(), &entity);

        const auto depths = GetUIHierarchyDepths(entities);
        for (Entity& entity : entities)
        {
            if (!entity.HasUI() || !IsUIHierarchyEnabled(entity, byId))
                continue;
            const UIComponent& ui = entity.GetUIComponent();
            if (!ui.Enabled || ui.Type == UIElementType::ScreenGui)
                continue;
            drawOrder.push_back(&entity);
        }

        // Reset do estado transitório de hover (inclusive em elementos pulados).
        for (Entity& entity : entities)
        {
            if (entity.HasUI())
                entity.GetUIComponent().IsHovered = false;
        }
        if (drawOrder.empty())
            return result;

        // A interação segue o desenho: pai primeiro, filho por cima em empate.
        std::stable_sort(drawOrder.begin(), drawOrder.end(), [&depths](const Entity* a, const Entity* b)
        {
            if (a->GetUIComponent().ZIndex != b->GetUIComponent().ZIndex)
                return a->GetUIComponent().ZIndex < b->GetUIComponent().ZIndex;
            return depths.at(a->GetID()) < depths.at(b->GetID());
        });

        const ResolvedUIMap resolved =
            ResolveLayouts(entities, kInteractionRefWidth, kInteractionRefHeight);

        const auto rectOf = [&resolved](Entity* entity) -> UIComponent::ResolvedRect
        {
            const auto it = resolved.find(entity->GetID());
            if (it != resolved.end())
                return it->second;
            return entity->GetUIComponent().ResolveRect(kInteractionRefWidth, kInteractionRefHeight);
        };

        // Ponto dentro do elemento e de todos os ancestrais que recortam.
        const auto pointInside = [&](Entity* entity, float px, float py)
        {
            const auto rect = rectOf(entity);
            if (px < rect.X || px > rect.X + rect.Width || py < rect.Y || py > rect.Y + rect.Height)
                return false;
            uint64_t parentId = entity->GetParentID();
            int guard = 0;
            while (parentId != 0 && guard++ < 64)
            {
                const auto it = byId.find(parentId);
                if (it == byId.end())
                    break;
                Entity* parent = it->second;
                if (parent->HasUI())
                {
                    const UIComponent& parentUI = parent->GetUIComponent();
                    if (parentUI.ClipsContent || parentUI.ScrollViewEnabled)
                    {
                        const auto parentRect = rectOf(parent);
                        if (px < parentRect.X || px > parentRect.X + parentRect.Width ||
                            py < parentRect.Y || py > parentRect.Y + parentRect.Height)
                            return false;
                    }
                }
                parentId = parent->GetParentID();
            }
            return true;
        };

        // ── Hit test: elemento mais ao topo sob o ponteiro ───────────────────
        Entity* hover = nullptr;
        bool hoverIsOption = false;
        int hoverOptionIndex = -1;

        if (input.PointerValid)
        {
            // Dropdowns abertos desenham as opções abaixo do cabeçalho e capturam
            // o ponteiro sobre elas (prioridade sobre os demais elementos).
            for (auto it = drawOrder.rbegin(); it != drawOrder.rend(); ++it)
            {
                Entity* entity = *it;
                UIComponent& ui = entity->GetUIComponent();
                if (ui.Type != UIElementType::Dropdown || !ui.DropdownOpen)
                    continue;
                const auto rect = rectOf(entity);
                for (size_t i = 0; i < ui.Options.size(); ++i)
                {
                    const float rowY = rect.Y + rect.Height * static_cast<float>(i + 1);
                    if (input.PointerX >= rect.X && input.PointerX <= rect.X + rect.Width &&
                        input.PointerY >= rowY && input.PointerY <= rowY + rect.Height)
                    {
                        hover = entity;
                        hoverIsOption = true;
                        hoverOptionIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (hover != nullptr)
                    break;
            }

            if (hover == nullptr)
            {
                for (auto it = drawOrder.rbegin(); it != drawOrder.rend(); ++it)
                {
                    Entity* entity = *it;
                    if (!UIElementIsInteractive(entity->GetUIComponent().Type))
                        continue;
                    if (pointInside(entity, input.PointerX, input.PointerY))
                    {
                        hover = entity;
                        break;
                    }
                }
            }
        }

        if (hover != nullptr)
        {
            result.PointerOverUI = true;
            if (!hoverIsOption)
                result.HoveredEntity = hover->GetID();
        }

        // ── Estado por widget ────────────────────────────────────────────────
        for (Entity* entity : drawOrder)
        {
            UIComponent& ui = entity->GetUIComponent();
            const uint64_t id = entity->GetID();
            const bool isHover = (hover == entity) && !hoverIsOption;
            const bool isOptionHover = (hover == entity) && hoverIsOption;
            const bool wasPressed = ui.IsPressed;

            switch (ui.Type)
            {
            case UIElementType::TextButton:
            case UIElementType::ImageButton:
            {
                ui.IsHovered = isHover;
                ui.IsPressed = isHover && input.PointerDown;
                if (isHover && input.PointerPressed)
                    result.PressedEntity = id;
                if (isHover && input.PointerReleased && wasPressed)
                {
                    result.ClickedEntity = id;
                    result.ChangedEntity = id;
                }
                break;
            }
            case UIElementType::Frame:
                ui.IsHovered = isHover;
                break;
            case UIElementType::Slider:
            {
                ui.IsHovered = isHover;
                if (isHover && input.PointerPressed)
                    ui.IsPressed = true;
                if (ui.IsPressed)
                {
                    if (input.PointerDown)
                    {
                        const auto rect = rectOf(entity);
                        float t = rect.Width > 0.0001f
                            ? (input.PointerX - rect.X) / rect.Width : 0.0f;
                        t = std::clamp(t, 0.0f, 1.0f);
                        const float newValue = ui.MinValue + t * (ui.MaxValue - ui.MinValue);
                        if (newValue != ui.Value)
                        {
                            ui.Value = newValue;
                            result.ChangedEntity = id;
                        }
                    }
                    else
                    {
                        ui.IsPressed = false;
                    }
                }
                break;
            }
            case UIElementType::Checkbox:
            {
                ui.IsHovered = isHover;
                ui.IsPressed = isHover && input.PointerDown;
                if (isHover && input.PointerReleased && wasPressed)
                {
                    ui.Checked = !ui.Checked;
                    result.ClickedEntity = id;
                    result.ChangedEntity = id;
                }
                break;
            }
            case UIElementType::Dropdown:
            {
                ui.IsHovered = isHover;
                ui.HoverOptionIndex = isOptionHover ? hoverOptionIndex : -1;
                ui.IsPressed = isHover && input.PointerDown;
                if (isHover && input.PointerPressed)
                    result.PressedEntity = id;
                if (isHover && input.PointerReleased && wasPressed)
                {
                    ui.DropdownOpen = !ui.DropdownOpen;
                    ui.HoverOptionIndex = -1;
                    result.ChangedEntity = id;
                }
                if (isOptionHover && input.PointerReleased)
                {
                    ui.SelectedIndex = hoverOptionIndex;
                    ui.DropdownOpen = false;
                    ui.HoverOptionIndex = -1;
                    result.ClickedEntity = id;
                    result.ChangedEntity = id;
                }
                if (ui.DropdownOpen && input.PointerPressed && !isHover && !isOptionHover)
                {
                    ui.DropdownOpen = false;
                    ui.HoverOptionIndex = -1;
                    result.ChangedEntity = id;
                }
                break;
            }
            case UIElementType::ScrollingFrame:
            {
                ui.IsHovered = isHover;
                const float maxScroll = GetMaxScrollOffset(
                    *entity, entities, kInteractionRefWidth, kInteractionRefHeight, &resolved);
                if (isHover && input.WheelDelta != 0.0f)
                {
                    const float next = std::clamp(
                        ui.ScrollOffsetY - input.WheelDelta * 48.0f, 0.0f, maxScroll);
                    if (next != ui.ScrollOffsetY)
                    {
                        ui.ScrollOffsetY = next;
                        result.ChangedEntity = id;
                    }
                }
                else if (ui.ScrollOffsetY > maxScroll)
                {
                    ui.ScrollOffsetY = maxScroll;
                }
                break;
            }
            default:
                break;
            }
        }

        // ── Foco de teclado (TextBox) ────────────────────────────────────────
        // Um clique foca um TextBox; um clique em qualquer outro lugar remove o foco.
        if (input.PointerPressed)
        {
            Entity* pressed = hover != nullptr && !hoverIsOption ? hover : nullptr;
            const bool pressedIsTextBox =
                pressed != nullptr && pressed->GetUIComponent().Type == UIElementType::TextBox;
            if (pressedIsTextBox)
            {
                for (Entity* entity : drawOrder)
                {
                    UIComponent& ui = entity->GetUIComponent();
                    ui.IsFocused = (entity == pressed);
                }
            }
            else
            {
                for (Entity* entity : drawOrder)
                    entity->GetUIComponent().IsFocused = false;
            }
        }

        Entity* focusedTextBox = nullptr;
        for (Entity* entity : drawOrder)
        {
            UIComponent& ui = entity->GetUIComponent();
            if (ui.Type == UIElementType::TextBox && ui.IsFocused)
            {
                focusedTextBox = entity;
                result.FocusedTextBox = entity->GetID();
                break;
            }
        }

        if (focusedTextBox != nullptr)
        {
            UIComponent& ui = focusedTextBox->GetUIComponent();
            result.TextInputCaptured = true;
            const auto rect = rectOf(focusedTextBox);

            // Posiciona o cursor ao clicar (estimativa por avanço médio de glifo).
            if (input.PointerPressed && input.PointerValid &&
                pointInside(focusedTextBox, input.PointerX, input.PointerY))
            {
                const float advance = std::max(1.0f, ui.FontSize * 0.5f);
                const float localX = input.PointerX - (rect.X + 8.0f) + advance * 0.5f;
                size_t caret = 0;
                if (localX > 0.0f)
                {
                    const size_t approx = static_cast<size_t>(localX / advance);
                    caret = std::min(approx, ui.Text.size());
                    while (caret < ui.Text.size() &&
                           (static_cast<unsigned char>(ui.Text[caret]) & 0xC0) == 0x80)
                        ++caret;
                }
                ui.CaretIndex = static_cast<int>(caret);
            }

            size_t caret = static_cast<size_t>(
                std::clamp(ui.CaretIndex, 0, static_cast<int>(ui.Text.size())));
            caret = std::min(caret, ui.Text.size());
            bool textDirty = false;

            if (input.KeyLeft)
            {
                caret = Detail::PrevCodepoint(ui.Text, caret);
                ui.CaretIndex = static_cast<int>(caret);
            }
            else if (input.KeyRight)
            {
                caret = Detail::NextCodepoint(ui.Text, caret);
                ui.CaretIndex = static_cast<int>(caret);
            }
            else if (input.KeyHome)
            {
                caret = 0;
                ui.CaretIndex = 0;
            }
            else if (input.KeyEnd)
            {
                caret = ui.Text.size();
                ui.CaretIndex = static_cast<int>(caret);
            }

            if (input.KeyBackspace && caret > 0)
            {
                const size_t prev = Detail::PrevCodepoint(ui.Text, caret);
                ui.Text.erase(prev, caret - prev);
                caret = prev;
                ui.CaretIndex = static_cast<int>(caret);
                textDirty = true;
            }
            if (input.KeyDelete && caret < ui.Text.size())
            {
                const size_t next = Detail::NextCodepoint(ui.Text, caret);
                ui.Text.erase(caret, next - caret);
                textDirty = true;
            }
            if (!input.TextInput.empty())
            {
                ui.Text.insert(caret, input.TextInput);
                caret += input.TextInput.size();
                ui.CaretIndex = static_cast<int>(caret);
                textDirty = true;
            }
            if (input.KeyEnter)
                ui.IsFocused = false;

            if (textDirty)
                result.ChangedEntity = focusedTextBox->GetID();
        }

        return result;
    }
}
