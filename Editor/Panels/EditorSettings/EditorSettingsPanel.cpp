#include "../../../Engine/Localization/Localization.h"
#include "EditorSettingsPanel.h"
#include "../../../Core/Application.h"
#include "../../../Engine/Localization/Localization.h"
#include <imgui.h>
#include <algorithm>
#include "../../../Core/Diagnostics/PerformanceMonitor.h"

void EditorSettingsPanel::Draw(Application& app)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float width = viewport->WorkSize.x;
    float height = viewport->WorkSize.y;

    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + width - app.GetInspectorWidth(), 20),
        ImGuiCond_FirstUseEver
    );

    const float panelHeight = std::max(180.0f, height - 20.0f);
    const float minPanelWidth = std::min(200.0f, std::max(160.0f, width));
    const float maxPanelWidth = std::max(minPanelWidth, std::min(600.0f, width));
    ImGui::SetNextWindowSize(
        ImVec2(std::clamp(app.GetInspectorWidth(), minPanelWidth, maxPanelWidth), panelHeight),
        ImGuiCond_FirstUseEver
    );

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(minPanelWidth, panelHeight),
        ImVec2(maxPanelWidth, panelHeight)
    );

    ImGui::Begin(L("panel.settings"), nullptr, ImGuiWindowFlags_NoCollapse);
    app.SetInspectorWidth(ImGui::GetWindowWidth());

    // == VIEW SETTINGS =========================================================
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
    bool viewOpen = ImGui::CollapsingHeader(
        (std::string("  ") + L("settings.view")).c_str(),
        ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();

    if (viewOpen)
    {
        const char* qualityNames[] = {
            L("hub.quality_ultra_performance_name"),
            L("hub.quality_performance_name"),
            L("hub.quality_balanced_name"),
            L("hub.quality_high_name"),
            L("hub.quality_ultra_name")
        };
        const GraphicsQuality qualityValues[] = {
            GraphicsQuality::UltraPerformance, GraphicsQuality::Performance,
            GraphicsQuality::Balanced, GraphicsQuality::HighFidelity, GraphicsQuality::Ultra };
        int qualityIndex = 0;
        for (int i = 0; i < 5; ++i)
            if (qualityValues[i] == app.GetGraphicsQuality()) qualityIndex = i;
        if (ImGui::Combo(L("settings.graphics"), &qualityIndex, qualityNames, 5))
            app.SetGraphicsQuality(qualityValues[std::clamp(qualityIndex, 0, 4)]);
        ImGui::TextWrapped(UI("%s"), L("settings.quality_hint"));
        if (app.IsGraphicsSafetyFallbackActive())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s",
                L("settings.graphics_safety_fallback"));
        }
        ImGui::Checkbox(L("settings.show_grid"), &app.GetShowGrid());
        ImGui::Checkbox(L("settings.show_axis"), &app.GetShowAxis());
        ImGui::SliderFloat(L("settings.grid_dist"), &app.GetGridRenderDistance(), 500.0f, 10000.0f, "%.0f m");
        ImGui::Spacing();
    }

    // == PLAY / LIVE EDITING ===================================================
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
    const bool playOpen = ImGui::CollapsingHeader(
        (std::string("  ") + L("settings.play")).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (playOpen)
    {
        bool editDuringPlay = app.GetEditDuringPlay();
        if (ImGui::Checkbox(L("settings.edit_during_play"), &editDuringPlay))
            app.SetEditDuringPlay(editDuringPlay);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", L("settings.edit_during_play_help"));
        ImGui::TextWrapped("%s", L("settings.edit_during_play_hint"));
        ImGui::Spacing();
    }

    // == PROFILER =============================================================
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
    const bool profilerOpen = ImGui::CollapsingHeader(
        (std::string("  ") + L("settings.profiler")).c_str());
    ImGui::PopStyleColor();
    if (profilerOpen)
    {
        bool profileScripts = PerformanceMonitor::IsScriptProfilingEnabled();
        if (ImGui::Checkbox(UI("Medir tempo dos scripts (adiciona custo de medição)"), &profileScripts))
            PerformanceMonitor::SetScriptProfilingEnabled(profileScripts);
        const SceneRenderer::RenderStats& stats = app.GetRenderer().GetSceneRenderer().GetLastStats();
        ImGui::Text(UI("%s: %.1f"), "FPS", PerformanceMonitor::GetFPS());
        ImGui::Text(UI("%s: %u"), L("settings.draw_calls"), stats.DrawCalls);
        ImGui::Text(UI("%s: %u"), L("settings.triangles"), stats.Triangles);
        ImGui::Text(UI("%s: %u"), L("settings.visible"), stats.VisibleEntities);
        ImGui::Text(UI("%s: %u"), L("settings.frustum_culled"), stats.FrustumCulledEntities);
        ImGui::Text(UI("%s: %u"), L("settings.shadow_calls"), stats.ShadowDrawCalls);
        ImGui::Text(UI("%s: %.2f ms"), L("settings.render_time"), stats.RenderTimeMs);
        ImGui::Text(UI("%s: %.2f ms"), L("settings.shadow_time"), stats.ShadowTimeMs);
        if (stats.GpuTimeAvailable)
            ImGui::Text(UI("GPU: %.2f ms"), stats.GpuTimeMs);
        else
            ImGui::TextDisabled("GPU: aguardando medição...");
        ImGui::Text(UI("CPU: %.1f%% | RAM: %.1f MB"),
            PerformanceMonitor::GetCPUUsage(), PerformanceMonitor::GetWorkingSetMB());
        ImGui::Text(UI("Frame time: média %.2f ms | pico %.2f ms"),
            PerformanceMonitor::GetAverageFrameTimeMs(), PerformanceMonitor::GetWorstFrameTimeMs());
        static float frameTimes[120]{};
        PerformanceMonitor::GetFrameTimeHistory(frameTimes, 120);
        ImGui::PlotLines(UI("Frame time (ms) — últimos 4 s"), frameTimes, 120, 0,
            nullptr, 0.0f, 50.0f, ImVec2(-1.0f, 70.0f));
        ImGui::TextDisabled(UI("Meta: %.1f ms a 60 FPS. Pico usa janela móvel de até 240 frames."), 1000.0f / 60.0f);
        if (profileScripts)
        {
            ImGui::Separator();
            ImGui::Text(UI("Scripts mais lentos neste frame"));
            const auto& timings = PerformanceMonitor::GetScriptTimings();
            const size_t shown = std::min<size_t>(timings.size(), 10);
            for (size_t i = 0; i < shown; ++i)
                ImGui::Text("%.3f ms | %u chamada(s) | %s",
                    timings[i].FrameTimeMs, timings[i].Calls, timings[i].Name.c_str());
            if (timings.empty())
                ImGui::TextDisabled(UI("Inicie o Play para coletar os tempos."));
        }

        ImGui::Separator();
        static int selectedCaptureFrame = 0;
        bool captureFrames = PerformanceMonitor::IsFrameCaptureEnabled();
        if (ImGui::Checkbox(UI("Capturar quadros para inspeção"), &captureFrames))
        {
            PerformanceMonitor::SetFrameCaptureEnabled(captureFrames);
            selectedCaptureFrame = 0;
        }
        ImGui::TextDisabled(UI("Armazena até 120 quadros; desligado não aloca histórico."));
        const auto& capturedFrames = PerformanceMonitor::GetCapturedFrames();
        if (!capturedFrames.empty())
        {
            const int last = static_cast<int>(capturedFrames.size()) - 1;
            selectedCaptureFrame = std::clamp(selectedCaptureFrame, 0, last);
            ImGui::SliderInt(UI("Quadro capturado"), &selectedCaptureFrame, 0, last);
            const auto& frame = capturedFrames[static_cast<size_t>(selectedCaptureFrame)];
            ImGui::Text(UI("Quadro #%llu | render %.2f ms | sombras %.2f ms"),
                frame.Frame, frame.RenderTimeMs, frame.ShadowTimeMs);
            ImGui::Text(UI("Draw calls %u | triângulos %u | sombras %u / %u triângulos"),
                frame.DrawCalls, frame.Triangles, frame.ShadowDrawCalls, frame.ShadowTriangles);
            ImGui::Text(UI("Visíveis %u | removidos pelo frustum %u"),
                frame.VisibleEntities, frame.FrustumCulledEntities);
            if (frame.GpuTimeAvailable)
                ImGui::Text(UI("GPU do quadro: %.2f ms"), frame.GpuTimeMs);
            else
                ImGui::TextDisabled(UI("GPU do quadro: consulta ainda indisponível."));
            if (ImGui::Button(UI("Limpar quadros capturados")))
            {
                PerformanceMonitor::ClearCapturedFrames();
                selectedCaptureFrame = 0;
            }
        }
        else if (captureFrames)
        {
            ImGui::TextDisabled(UI("Aguardando o próximo desenho do viewport..."));
        }
        ImGui::Spacing();
    }

    // == CAMERA ================================================================
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
    bool camOpen = ImGui::CollapsingHeader(
        (std::string("  ") + L("settings.camera")).c_str());
    ImGui::PopStyleColor();

    if (camOpen)
    {
        ImGui::SliderFloat(L("settings.camera_speed"), &app.GetCamera().MoveSpeed, 0.001f, 1.0f);
        ImGui::Spacing();
    }

    // == GIZMO =================================================================
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
    bool gizmoOpen = ImGui::CollapsingHeader(
        (std::string("  ") + L("settings.gizmo")).c_str());
    ImGui::PopStyleColor();

    if (gizmoOpen)
    {
        ImGui::Checkbox(L("settings.pos_snap"),   &app.GetSnapSettings().PositionEnabled);
        ImGui::DragFloat(L("settings.pos_step"),  &app.GetSnapSettings().PositionStep, 0.1f, 0.01f, 100.0f);
        ImGui::Checkbox(L("settings.rot_snap"),   &app.GetSnapSettings().RotationEnabled);
        ImGui::DragFloat(L("settings.rot_step"),  &app.GetSnapSettings().RotationStep, 1.0f, 1.0f, 90.0f);
        ImGui::Checkbox(L("settings.scale_snap"), &app.GetSnapSettings().ScaleEnabled);
        ImGui::DragFloat(L("settings.scale_step"), &app.GetSnapSettings().ScaleStep, 0.01f, 0.01f, 10.0f);
        ImGui::Separator();
        bool faceScaleHandles = app.GetFaceScaleHandlesEnabled();
        if (ImGui::Checkbox(UI("Alças de escala pelas faces"), &faceScaleHandles))
            app.SetFaceScaleHandlesEnabled(faceScaleHandles);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("No modo Escala, permite puxar uma face mantendo a face oposta parada."));
        SnapSettings& docking = app.GetSnapSettings();
        ImGui::Checkbox(UI("Encaixe geométrico (Docking)"), &docking.FaceSnapEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Alinha peças entre si durante o arraste. Independente dos snaps numéricos de posição, rotação e escala."));
        if (docking.FaceSnapEnabled)
        {
            ImGui::Indent();
            ImGui::Checkbox(UI("Encaixar faces"), &docking.FaceAlignmentEnabled);
            ImGui::Checkbox(UI("Alinhar arestas"), &docking.EdgeSnapEnabled);
            ImGui::Checkbox(UI("Grudar vértices"), &docking.VertexSnapEnabled);
            ImGui::Checkbox(UI("Alinhar centros nos eixos X/Z"), &docking.AxisAlignmentEnabled);
            if (docking.FaceAlignmentEnabled)
            {
                ImGui::SliderFloat(UI("Raio de encaixe das faces"), &docking.FaceSnapThreshold,
                    0.01f, 2.0f, "%.2f m");
                ImGui::SliderFloat(UI("Ajuste lateral das faces"), &docking.FaceAlignTolerance,
                    0.01f, 2.0f, "%.2f m");
            }
            if (docking.EdgeSnapEnabled)
                ImGui::SliderFloat(UI("Raio de encaixe das arestas"), &docking.EdgeSnapThreshold,
                    0.01f, 2.0f, "%.2f m");
            if (docking.VertexSnapEnabled)
                ImGui::SliderFloat(UI("Raio para grudar vértices"), &docking.VertexSnapThreshold,
                    0.01f, 2.0f, "%.2f m");
            ImGui::Unindent();
        }
        ImGui::TextDisabled(UI("Para construir, deixe o docking ligado e mantenha os snaps numéricos desligados até precisar de uma grade fixa."));
        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", L("settings.tip"));
    ImGui::End();
}
