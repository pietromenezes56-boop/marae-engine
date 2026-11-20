#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

// Marae editor icon vocabulary. Keep these lightweight vector strokes in the
// established slate/blue UI; do not replace the editor's visual identity when
// adding controls. Extend this set instead of introducing a second icon style.
namespace MaraeUI
{
    enum class EditorIcon
    {
        Move, Rotate, Scale, Empty,
        Cube, Sphere, Cylinder, Cone, Torus, Capsule, Plane, Wedge,
        Camera, Light, SpotLight, DirectionalLight, AreaLight,
        Physics, Audio, Animation, Script, Folder, Model,
        ScreenGui, Frame, Text, Image, Button, ComponentPlus, Triangle,
        Circle, Diamond, Pentagon, Hexagon, Octagon, Star,
        Lock, Unlock, Inspector, Apply, Revert, Unlink, Reset
    };

    inline void DrawEditorIcon(ImDrawList* draw, EditorIcon icon, ImVec2 topLeft,
                               float size, ImU32 color = IM_COL32(190, 205, 224, 255))
    {
        if (!draw || size <= 0.0f)
            return;

        const float cx = topLeft.x + size * 0.5f;
        const float cy = topLeft.y + size * 0.5f;
        const float h = size * 0.37f;
        const float stroke = std::max(1.25f, size * 0.075f);
        const ImVec2 c(cx, cy);
        const auto line = [&](float x1, float y1, float x2, float y2, float weight = -1.0f)
        {
            draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color,
                          weight > 0.0f ? weight : stroke);
        };
        const auto loopAt = [&](float centerX, float centerY, float rx, float ry,
                                float startAngle = 0.0f,
                                float endAngle = 6.2831853f, int segments = 24)
        {
            ImVec2 points[32];
            segments = std::clamp(segments, 3, 32);
            const bool closed = (endAngle - startAngle) >= 6.28f;
            for (int i = 0; i < segments; ++i)
            {
                const float a = startAngle + (endAngle - startAngle) *
                    static_cast<float>(i) / static_cast<float>(segments - (closed ? 0 : 1));
                points[i] = ImVec2(centerX + std::cos(a) * rx, centerY + std::sin(a) * ry);
            }
            draw->AddPolyline(points, segments, color,
                closed ? ImDrawFlags_Closed : ImDrawFlags_None, stroke);
        };
        const auto loop = [&](float rx, float ry, float startAngle = 0.0f,
                              float endAngle = 6.2831853f, int segments = 24)
        {
            loopAt(cx, cy, rx, ry, startAngle, endAngle, segments);
        };
        const auto arrowHead = [&](ImVec2 tip, ImVec2 from, float radius)
        {
            const ImVec2 d(tip.x - from.x, tip.y - from.y);
            const float length = std::max(0.001f, std::sqrt(d.x * d.x + d.y * d.y));
            const ImVec2 n(d.x / length, d.y / length);
            const ImVec2 p(-n.y, n.x);
            draw->AddTriangleFilled(tip,
                ImVec2(tip.x - n.x * radius + p.x * radius * 0.65f,
                       tip.y - n.y * radius + p.y * radius * 0.65f),
                ImVec2(tip.x - n.x * radius - p.x * radius * 0.65f,
                       tip.y - n.y * radius - p.y * radius * 0.65f), color);
        };

        switch (icon)
        {
        case EditorIcon::Move:
            line(cx - h, cy, cx + h, cy);
            line(cx, cy - h, cx, cy + h);
            arrowHead(ImVec2(cx + h, cy), ImVec2(cx + h * 0.45f, cy), size * 0.13f);
            arrowHead(ImVec2(cx - h, cy), ImVec2(cx - h * 0.45f, cy), size * 0.13f);
            arrowHead(ImVec2(cx, cy - h), ImVec2(cx, cy - h * 0.45f), size * 0.13f);
            arrowHead(ImVec2(cx, cy + h), ImVec2(cx, cy + h * 0.45f), size * 0.13f);
            break;
        case EditorIcon::Rotate:
            loop(h, h, -0.45f, 4.85f, 22);
            arrowHead(ImVec2(cx + std::cos(4.85f) * h, cy + std::sin(4.85f) * h),
                      ImVec2(cx + std::cos(4.52f) * h, cy + std::sin(4.52f) * h), size * 0.14f);
            break;
        case EditorIcon::Scale:
            draw->AddRect(ImVec2(cx - h * 0.68f, cy - h * 0.68f),
                          ImVec2(cx + h * 0.68f, cy + h * 0.68f), color, 2.0f, 0, stroke);
            line(cx - h, cy, cx - h * 0.70f, cy);
            line(cx + h * 0.70f, cy, cx + h, cy);
            line(cx, cy - h, cx, cy - h * 0.70f);
            line(cx, cy + h * 0.70f, cx, cy + h);
            draw->AddCircleFilled(ImVec2(cx - h, cy), size * 0.075f, color);
            draw->AddCircleFilled(ImVec2(cx + h, cy), size * 0.075f, color);
            draw->AddCircleFilled(ImVec2(cx, cy - h), size * 0.075f, color);
            draw->AddCircleFilled(ImVec2(cx, cy + h), size * 0.075f, color);
            break;
        case EditorIcon::Empty:
            draw->AddCircle(c, h * 0.75f, color, 20, stroke);
            line(cx - h * 0.35f, cy, cx + h * 0.35f, cy);
            line(cx, cy - h * 0.35f, cx, cy + h * 0.35f);
            break;
        case EditorIcon::Cube:
        {
            const ImVec2 top(cx, cy - h), left(cx - h, cy - h * 0.48f);
            const ImVec2 right(cx + h, cy - h * 0.48f), front(cx, cy + h * 0.08f);
            const ImVec2 leftBottom(cx - h, cy + h * 0.58f);
            const ImVec2 rightBottom(cx + h, cy + h * 0.58f);
            const ImVec2 frontBottom(cx, cy + h);
            draw->AddLine(top, left, color, stroke); draw->AddLine(top, right, color, stroke);
            draw->AddLine(left, front, color, stroke); draw->AddLine(right, front, color, stroke);
            draw->AddLine(left, leftBottom, color, stroke); draw->AddLine(right, rightBottom, color, stroke);
            draw->AddLine(front, frontBottom, color, stroke);
            draw->AddLine(leftBottom, frontBottom, color, stroke);
            draw->AddLine(rightBottom, frontBottom, color, stroke);
            break;
        }
        case EditorIcon::Sphere:
            draw->AddCircle(c, h, color, 24, stroke);
            loop(h * 0.42f, h, -1.5707963f, 4.7123890f, 20);
            loop(h, h * 0.38f, 0.0f, 6.2831853f, 20);
            break;
        case EditorIcon::Cylinder:
            loopAt(cx, cy - h * 0.55f, h, h * 0.32f, 0.0f, 6.2831853f, 20);
            loopAt(cx, cy + h * 0.55f, h, h * 0.32f, 0.0f, 6.2831853f, 20);
            line(cx - h, cy - h * 0.55f, cx - h, cy + h * 0.55f);
            line(cx + h, cy - h * 0.55f, cx + h, cy + h * 0.55f);
            break;
        case EditorIcon::Cone:
            line(cx, cy - h, cx - h, cy + h * 0.55f);
            line(cx, cy - h, cx + h, cy + h * 0.55f);
            loopAt(cx, cy + h * 0.55f, h, h * 0.3f, 0.0f, 6.2831853f, 20);
            break;
        case EditorIcon::Torus:
            draw->AddCircle(c, h, color, 24, stroke);
            draw->AddCircle(c, h * 0.43f, color, 20, stroke);
            break;
        case EditorIcon::Capsule:
            loopAt(cx, cy - h * 0.32f, h * 0.62f, h * 0.62f,
                   3.1415926f, 6.2831853f, 14);
            loopAt(cx, cy + h * 0.32f, h * 0.62f, h * 0.62f,
                   0.0f, 3.1415926f, 14);
            line(cx - h * 0.62f, cy - h * 0.32f, cx - h * 0.62f, cy + h * 0.32f);
            line(cx + h * 0.62f, cy - h * 0.32f, cx + h * 0.62f, cy + h * 0.32f);
            break;
        case EditorIcon::Plane:
            draw->AddRect(ImVec2(cx - h, cy - h * 0.72f), ImVec2(cx + h, cy + h * 0.72f), color, 1.5f, 0, stroke);
            line(cx - h * 0.55f, cy, cx + h * 0.55f, cy);
            line(cx, cy - h * 0.48f, cx, cy + h * 0.48f);
            break;
        case EditorIcon::Wedge:
            line(cx - h, cy + h * 0.6f, cx - h, cy - h * 0.45f);
            line(cx - h, cy - h * 0.45f, cx + h, cy + h * 0.6f);
            line(cx + h, cy + h * 0.6f, cx - h, cy + h * 0.6f);
            line(cx - h * 0.5f, cy + h * 0.6f, cx + h * 0.55f, cy + h * 0.6f);
            line(cx + h * 0.55f, cy + h * 0.6f, cx + h, cy + h * 0.6f);
            line(cx - h, cy - h * 0.45f, cx - h * 0.5f, cy - h * 0.2f);
            line(cx - h * 0.5f, cy - h * 0.2f, cx + h, cy + h * 0.6f);
            break;
        case EditorIcon::Camera:
            draw->AddRect(ImVec2(cx - h, cy - h * 0.58f), ImVec2(cx + h, cy + h * 0.62f), color, 2.0f, 0, stroke);
            draw->AddCircle(ImVec2(cx, cy), h * 0.38f, color, 20, stroke);
            line(cx - h * 0.45f, cy - h * 0.58f, cx - h * 0.20f, cy - h * 0.82f);
            line(cx - h * 0.20f, cy - h * 0.82f, cx + h * 0.24f, cy - h * 0.82f);
            break;
        case EditorIcon::Light:
            draw->AddCircle(ImVec2(cx, cy - h * 0.2f), h * 0.55f, color, 20, stroke);
            line(cx - h * 0.25f, cy + h * 0.40f, cx + h * 0.25f, cy + h * 0.40f);
            line(cx - h * 0.20f, cy + h * 0.60f, cx + h * 0.20f, cy + h * 0.60f);
            line(cx, cy - h, cx, cy - h * 1.28f);
            line(cx - h * 0.75f, cy - h * 0.62f, cx - h, cy - h * 0.82f);
            line(cx + h * 0.75f, cy - h * 0.62f, cx + h, cy - h * 0.82f);
            break;
        case EditorIcon::SpotLight:
            draw->AddCircleFilled(ImVec2(cx, cy - h * 0.65f), size * 0.08f, color);
            line(cx - h * 0.15f, cy - h * 0.45f, cx - h * 0.85f, cy + h * 0.65f);
            line(cx + h * 0.15f, cy - h * 0.45f, cx + h * 0.85f, cy + h * 0.65f);
            line(cx - h * 0.85f, cy + h * 0.65f, cx + h * 0.85f, cy + h * 0.65f);
            break;
        case EditorIcon::DirectionalLight:
            for (int i = 0; i < 3; ++i)
            {
                const float y = cy - h * 0.55f + i * h * 0.55f;
                line(cx - h, y, cx + h * 0.55f, y);
                arrowHead(ImVec2(cx + h, y), ImVec2(cx + h * 0.45f, y), size * 0.12f);
            }
            break;
        case EditorIcon::AreaLight:
            draw->AddRect(ImVec2(cx - h * 0.72f, cy - h * 0.70f), ImVec2(cx + h * 0.72f, cy - h * 0.25f), color, 1.5f, 0, stroke);
            line(cx - h * 0.48f, cy - h * 0.12f, cx - h * 0.72f, cy + h * 0.65f);
            line(cx, cy - h * 0.12f, cx, cy + h * 0.65f);
            line(cx + h * 0.48f, cy - h * 0.12f, cx + h * 0.72f, cy + h * 0.65f);
            break;
        case EditorIcon::Physics:
            draw->AddCircle(c, h * 0.34f, color, 18, stroke);
            draw->AddCircle(ImVec2(cx - h * 0.58f, cy + h * 0.45f), h * 0.24f, color, 16, stroke);
            draw->AddCircle(ImVec2(cx + h * 0.58f, cy + h * 0.45f), h * 0.24f, color, 16, stroke);
            line(cx - h * 0.25f, cy + h * 0.15f, cx - h * 0.48f, cy + h * 0.30f);
            line(cx + h * 0.25f, cy + h * 0.15f, cx + h * 0.48f, cy + h * 0.30f);
            break;
        case EditorIcon::Audio:
        {
            const float heights[] = {0.2f, 0.55f, 0.9f, 0.48f, 0.72f, 0.25f};
            for (int i = 0; i < 6; ++i)
            {
                const float x = cx - h * 0.75f + i * h * 0.30f;
                line(x, cy - h * heights[i], x, cy + h * heights[i], stroke * 1.15f);
            }
            break;
        }
        case EditorIcon::Animation:
            draw->AddQuad(ImVec2(cx, cy - h), ImVec2(cx + h, cy),
                          ImVec2(cx, cy + h), ImVec2(cx - h, cy), color, stroke);
            draw->AddCircleFilled(ImVec2(cx, cy), size * 0.08f, color);
            break;
        case EditorIcon::Script:
            draw->AddRect(ImVec2(cx - h * 0.72f, cy - h), ImVec2(cx + h * 0.72f, cy + h), color, 1.5f, 0, stroke);
            line(cx - h * 0.40f, cy - h * 0.25f, cx - h * 0.12f, cy);
            line(cx - h * 0.12f, cy, cx - h * 0.40f, cy + h * 0.25f);
            line(cx + h * 0.40f, cy - h * 0.25f, cx + h * 0.12f, cy);
            line(cx + h * 0.12f, cy, cx + h * 0.40f, cy + h * 0.25f);
            break;
        case EditorIcon::Folder:
            line(cx - h, cy - h * 0.58f, cx - h * 0.15f, cy - h * 0.58f);
            line(cx - h * 0.15f, cy - h * 0.58f, cx + h * 0.05f, cy - h * 0.32f);
            line(cx + h * 0.05f, cy - h * 0.32f, cx + h, cy - h * 0.32f);
            line(cx + h, cy - h * 0.32f, cx + h * 0.82f, cy + h * 0.70f);
            line(cx + h * 0.82f, cy + h * 0.70f, cx - h, cy + h * 0.70f);
            line(cx - h, cy + h * 0.70f, cx - h, cy - h * 0.58f);
            break;
        case EditorIcon::Model:
            draw->AddLine(ImVec2(cx, cy - h), ImVec2(cx + h, cy - h * 0.45f), color, stroke);
            draw->AddLine(ImVec2(cx + h, cy - h * 0.45f), ImVec2(cx, cy + h * 0.1f), color, stroke);
            draw->AddLine(ImVec2(cx, cy + h * 0.1f), ImVec2(cx - h, cy - h * 0.45f), color, stroke);
            draw->AddLine(ImVec2(cx - h, cy - h * 0.45f), ImVec2(cx, cy - h), color, stroke);
            line(cx - h, cy + h * 0.15f, cx, cy + h * 0.72f);
            line(cx, cy + h * 0.72f, cx + h, cy + h * 0.15f);
            break;
        case EditorIcon::ScreenGui:
        case EditorIcon::Frame:
        case EditorIcon::Image:
        case EditorIcon::Button:
            draw->AddRect(ImVec2(cx - h, cy - h * 0.67f), ImVec2(cx + h, cy + h * 0.67f), color,
                          icon == EditorIcon::Button ? 3.0f : 1.5f, 0, stroke);
            if (icon == EditorIcon::ScreenGui)
                line(cx - h, cy - h * 0.25f, cx + h, cy - h * 0.25f);
            else if (icon == EditorIcon::Image)
            {
                line(cx - h * 0.72f, cy + h * 0.36f, cx - h * 0.12f, cy - h * 0.22f);
                line(cx - h * 0.12f, cy - h * 0.22f, cx + h * 0.12f, cy + h * 0.02f);
                line(cx + h * 0.12f, cy + h * 0.02f, cx + h * 0.44f, cy - h * 0.30f);
            }
            else if (icon == EditorIcon::Button)
                line(cx - h * 0.42f, cy, cx + h * 0.42f, cy);
            break;
        case EditorIcon::Text:
            line(cx - h * 0.78f, cy - h * 0.58f, cx + h * 0.78f, cy - h * 0.58f, stroke * 1.4f);
            line(cx, cy - h * 0.58f, cx, cy + h * 0.58f, stroke * 1.4f);
            line(cx - h * 0.52f, cy + h * 0.58f, cx + h * 0.52f, cy + h * 0.58f);
            break;
        case EditorIcon::ComponentPlus:
            draw->AddCircle(c, h * 0.9f, color, 20, stroke);
            line(cx - h * 0.45f, cy, cx + h * 0.45f, cy, stroke * 1.2f);
            line(cx, cy - h * 0.45f, cx, cy + h * 0.45f, stroke * 1.2f);
            break;
        case EditorIcon::Triangle:
            draw->AddTriangle(ImVec2(cx, cy - h), ImVec2(cx - h, cy + h * 0.75f),
                              ImVec2(cx + h, cy + h * 0.75f), color, stroke);
            break;
        case EditorIcon::Circle:
            draw->AddCircle(c, h * 0.82f, color, 28, stroke);
            break;
        case EditorIcon::Diamond:
        case EditorIcon::Pentagon:
        case EditorIcon::Hexagon:
        case EditorIcon::Octagon:
        case EditorIcon::Star:
        {
            const int points = icon == EditorIcon::Diamond ? 4
                : icon == EditorIcon::Pentagon ? 5
                : icon == EditorIcon::Hexagon ? 6
                : icon == EditorIcon::Octagon ? 8 : 10;
            ImVec2 polygon[10];
            for (int i = 0; i < points; ++i)
            {
                const float angle = -1.57079632679f + 6.28318530718f *
                    static_cast<float>(i) / static_cast<float>(points);
                const float radius = icon == EditorIcon::Star && (i % 2 != 0) ? h * 0.42f : h * 0.88f;
                polygon[i] = ImVec2(cx + std::cos(angle) * radius,
                                    cy + std::sin(angle) * radius);
            }
            draw->AddPolyline(polygon, points, color, ImDrawFlags_Closed, stroke);
            break;
        }
        case EditorIcon::Lock:
        {
            loopAt(cx, cy - h * 0.15f, h * 0.44f, h * 0.50f, 3.14159f, 6.2831853f, 16);
            draw->AddRectFilled(ImVec2(cx - h * 0.65f, cy - h * 0.15f),
                                ImVec2(cx + h * 0.65f, cy + h * 0.85f), color, 2.0f);
            const ImU32 holeColor = IM_COL32(20, 24, 30, 255);
            draw->AddCircleFilled(ImVec2(cx, cy + h * 0.20f), size * 0.08f, holeColor);
            draw->AddLine(ImVec2(cx, cy + h * 0.20f), ImVec2(cx, cy + h * 0.50f), holeColor, std::max(1.5f, stroke * 1.2f));
            break;
        }
        case EditorIcon::Unlock:
        {
            loopAt(cx - h * 0.12f, cy - h * 0.35f, h * 0.44f, h * 0.50f, 3.14159f, 5.8f, 14);
            draw->AddRectFilled(ImVec2(cx - h * 0.65f, cy - h * 0.15f),
                                ImVec2(cx + h * 0.65f, cy + h * 0.85f), color, 2.0f);
            const ImU32 holeColor = IM_COL32(20, 24, 30, 255);
            draw->AddCircleFilled(ImVec2(cx, cy + h * 0.20f), size * 0.08f, holeColor);
            draw->AddLine(ImVec2(cx, cy + h * 0.20f), ImVec2(cx, cy + h * 0.50f), holeColor, std::max(1.5f, stroke * 1.2f));
            break;
        }
        case EditorIcon::Inspector:
            draw->AddRect(ImVec2(cx - h, cy - h * 0.78f), ImVec2(cx + h, cy + h * 0.78f), color, 2.0f, 0, stroke);
            line(cx - h * 0.30f, cy - h * 0.72f, cx - h * 0.30f, cy + h * 0.72f);
            line(cx - h * 0.10f, cy - h * 0.38f, cx + h * 0.62f, cy - h * 0.38f);
            line(cx - h * 0.10f, cy, cx + h * 0.62f, cy);
            line(cx - h * 0.10f, cy + h * 0.38f, cx + h * 0.40f, cy + h * 0.38f);
            break;
        case EditorIcon::Apply:
            line(cx - h * 0.72f, cy, cx - h * 0.16f, cy + h * 0.55f, stroke * 1.25f);
            line(cx - h * 0.16f, cy + h * 0.55f, cx + h * 0.80f, cy - h * 0.58f, stroke * 1.25f);
            break;
        case EditorIcon::Revert:
            line(cx + h * 0.75f, cy - h * 0.46f, cx - h * 0.18f, cy - h * 0.46f, stroke * 1.2f);
            line(cx - h * 0.18f, cy - h * 0.46f, cx - h * 0.18f, cy + h * 0.55f, stroke * 1.2f);
            arrowHead(ImVec2(cx - h * 0.18f, cy + h * 0.76f), ImVec2(cx - h * 0.18f, cy + h * 0.28f), size * 0.14f);
            break;
        case EditorIcon::Unlink:
            loopAt(cx - h * 0.36f, cy, h * 0.42f, h * 0.27f, -0.82f, 2.28f, 12);
            loopAt(cx + h * 0.36f, cy, h * 0.42f, h * 0.27f, 2.30f, 5.42f, 12);
            line(cx - h * 0.18f, cy + h * 0.22f, cx + h * 0.18f, cy - h * 0.22f, stroke * 1.2f);
            break;
        case EditorIcon::Reset:
            loop(h * 0.72f, h * 0.72f, -2.55f, 2.15f, 20);
            arrowHead(ImVec2(cx - h * 0.62f, cy - h * 0.37f), ImVec2(cx - h * 0.18f, cy - h * 0.62f), size * 0.14f);
            break;
        }
    }

    inline void DrawIconLabel(EditorIcon icon, const char* label, ImVec4 textColor,
                              ImU32 iconColor = IM_COL32(190, 205, 224, 255))
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        DrawEditorIcon(ImGui::GetWindowDrawList(), icon,
                       ImVec2(pos.x + 2.0f, pos.y + 1.0f), 15.0f, iconColor);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f);
        ImGui::TextColored(textColor, "%s", label ? label : "");
    }

    inline bool IconMenuItem(EditorIcon icon, const char* label,
                             const char* shortcut = nullptr,
                             bool selected = false, bool enabled = true)
    {
        const std::string paddedLabel = std::string("     ") + (label ? label : "");
        ImGui::PushID(label ? label : "##icon_item");
        const bool clicked = ImGui::MenuItem(paddedLabel.c_str(), shortcut, selected, enabled);
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float iconSize = std::min(16.0f, std::max(12.0f, itemMax.y - itemMin.y - 4.0f));
        DrawEditorIcon(ImGui::GetWindowDrawList(), icon,
            ImVec2(itemMin.x + 4.0f, itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f),
            iconSize, enabled ? IM_COL32(190, 205, 224, 255) : IM_COL32(115, 126, 143, 180));
        ImGui::PopID();
        return clicked;
    }

    inline bool IconButton(EditorIcon icon, const char* label, ImVec2 size)
    {
        const std::string paddedLabel = std::string("   ") + (label ? label : "");
        const bool clicked = ImGui::Button(paddedLabel.c_str(), size);
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float iconSize = std::min(16.0f, std::max(12.0f, itemMax.y - itemMin.y - 4.0f));
        DrawEditorIcon(ImGui::GetWindowDrawList(), icon,
            ImVec2(itemMin.x + 5.0f, itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f),
            iconSize);
        return clicked;
    }
}
