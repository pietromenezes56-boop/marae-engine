#include "../../../Engine/Localization/Localization.h"
#include "ToolbarPanel.h"
#include "../../../Core/Application.h"
#include "../../../Engine/Localization/Localization.h"
#include "../../UI/EditorIcons.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

bool ToolbarPanel::s_IsRecording = false;

void ToolbarPanel::Draw(Application& app)
{
    // Mantido por compatibilidade com chamadas antigas. A Toolbar agora é
    // desenhada como barra própria entre o MenuBar e o DockSpace.
    (void)app;
}

void ToolbarPanel::DrawContents(Application& app)
{
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    const ImVec2 btnSize = ImVec2(28, 22);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto tooltip = [](const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(UI("%s"), text);
    };

    // ── GRUPO 1: Ferramentas de Transformação (ESQUERDA) ─────────────────
    // Em Unity os gizmos ficam na ESQUERDA, antes do Play
    const auto pushGizmoActive = [&](Application::GizmoMode mode)
    {
        if (app.GetGizmoMode() == mode)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.32f, 1.0f));
    };

    // Mover
    const bool moveActive = app.GetGizmoMode() == Application::GizmoMode::Translate;
    pushGizmoActive(Application::GizmoMode::Translate);
    if (ImGui::Button(UI("##move"), btnSize)) app.SetGizmoMode(Application::GizmoMode::Translate);
    if (moveActive) ImGui::PopStyleColor();
    tooltip("Mover (W)");
    {
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        const float size = std::min(rmax.x - rmin.x, rmax.y - rmin.y) * 0.72f;
        MaraeUI::DrawEditorIcon(draw, MaraeUI::EditorIcon::Move,
            ImVec2((rmin.x + rmax.x - size) * 0.5f,
                   (rmin.y + rmax.y - size) * 0.5f), size);
    }

    ImGui::SameLine();

    // Rotacionar
    const bool rotateActive = app.GetGizmoMode() == Application::GizmoMode::Rotate;
    pushGizmoActive(Application::GizmoMode::Rotate);
    if (ImGui::Button(UI("##rotate"), btnSize)) app.SetGizmoMode(Application::GizmoMode::Rotate);
    if (rotateActive) ImGui::PopStyleColor();
    tooltip("Rotacionar (E)");
    {
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        const float size = std::min(rmax.x - rmin.x, rmax.y - rmin.y) * 0.72f;
        MaraeUI::DrawEditorIcon(draw, MaraeUI::EditorIcon::Rotate,
            ImVec2((rmin.x + rmax.x - size) * 0.5f,
                   (rmin.y + rmax.y - size) * 0.5f), size);
    }

    ImGui::SameLine();

    // Escalar
    const bool scaleActive = app.GetGizmoMode() == Application::GizmoMode::Scale;
    pushGizmoActive(Application::GizmoMode::Scale);
    if (ImGui::Button(UI("##scale"), btnSize)) app.SetGizmoMode(Application::GizmoMode::Scale);
    if (scaleActive) ImGui::PopStyleColor();
    tooltip("Escalar (R)");
    {
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        const float size = std::min(rmax.x - rmin.x, rmax.y - rmin.y) * 0.72f;
        MaraeUI::DrawEditorIcon(draw, MaraeUI::EditorIcon::Scale,
            ImVec2((rmin.x + rmax.x - size) * 0.5f,
                   (rmin.y + rmax.y - size) * 0.5f), size);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ── GRUPO 2: Play / Pause / Stop (CENTRO) ────────────────────────────
    // Em Unity estes botões ficam ao centro da toolbar

    // Play
    if (ImGui::Button(UI("##play"), btnSize)) app.SetEditorState(Application::EditorState::Play);
    tooltip("Executar (Ctrl+P)");
    {
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        ImVec2 c = ImVec2((rmin.x+rmax.x)*0.5f,(rmin.y+rmax.y)*0.5f);
        float s = std::min(rmax.x-rmin.x, rmax.y-rmin.y)*0.35f;
        const bool isPlaying = app.GetEditorState() == Application::EditorState::Play;
        const ImU32 col = isPlaying ? IM_COL32(235,235,240,255) : IM_COL32(255,255,255,255);
        draw->AddTriangleFilled({c.x-s*0.6f,c.y-s},{c.x-s*0.6f,c.y+s},{c.x+s*0.8f,c.y},col);
    }

    ImGui::SameLine();

    const bool pauseStopDisabled = app.GetEditorState() == Application::EditorState::Edit;
    if (pauseStopDisabled) { ImGui::PushStyleVar(ImGuiStyleVar_Alpha,0.42f); ImGui::BeginDisabled(); }

    // Pause
    if (ImGui::Button(UI("##pause"), btnSize)) app.SetEditorState(Application::EditorState::Pause);
    tooltip("Pausar (Ctrl+Shift+P)");
    {
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        ImVec2 c = ImVec2((rmin.x+rmax.x)*0.5f,(rmin.y+rmax.y)*0.5f);
        float s = std::min(rmax.x-rmin.x, rmax.y-rmin.y);
        float bw = s*0.14f;
        const bool isPaused = app.GetEditorState() == Application::EditorState::Pause;
        const ImU32 col = isPaused ? IM_COL32(235,235,240,255) : IM_COL32(255,255,255,255);
        draw->AddRectFilled({c.x-bw*1.6f,c.y-s*0.4f},{c.x-bw*0.2f,c.y+s*0.4f},col);
        draw->AddRectFilled({c.x+bw*0.2f,c.y-s*0.4f},{c.x+bw*1.6f,c.y+s*0.4f},col);
    }

    ImGui::SameLine();

    // Stop
    if (ImGui::Button(UI("##stop"), btnSize)) app.SetEditorState(Application::EditorState::Edit);
    tooltip("Parar");
    {
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        draw->AddRectFilled({rmin.x+6,rmin.y+4},{rmax.x-6,rmax.y-4},IM_COL32(255,255,255,255));
    }

    if (pauseStopDisabled) { ImGui::EndDisabled(); ImGui::PopStyleVar(); }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
}
