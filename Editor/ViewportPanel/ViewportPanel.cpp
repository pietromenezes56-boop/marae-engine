#include "../../Engine/Localization/Localization.h"
#include "ViewportPanel.h"
#include "../Panels/Toolbar/ToolbarPanel.h"
#include "../Picking/Picking.h"
#include "../InspectorPanel/InspectorPanel.h"
#include "../HierarchyPanel/HierarchyPanel.h"
#include "../../Core/Application.h"
#include "../../Core/Diagnostics/PerformanceMonitor.h"
#include "../../Core/Diagnostics/DiagnosticLog.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Renderer/CameraRenderer/CameraRenderer.h"
#include "../../Scene/Camera/Camera.h"
#include "../../Math/Vector3.h"
#include "../../ThirdParty/ImGuizmo/ImGuizmo/src/ImGuizmo.h"
#include "../Modeling/MeshTopology.h"
#include "../Modeling/MeshPainter.h"
#include "../Modeling/ModelingOperations.h"
#include <unordered_set>

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <limits>
#include <map>
#include <set>
#include "../../Scene/Components/LightComponent.h"
#include "../../Scene/Components/UIComponent.h"
#include "../../Engine/UI/UILayout.h"
#include "../../Engine/UI/UITextMarkup.h"
#include "../../Engine/UI/UIFontLibrary.h"
#include "../../Engine/Scripting/ScriptDebugger.h"
#include "../UI/EditorIcons.h"

namespace
{
    bool s_ModelingBoxSelecting = false;
    ImVec2 s_ModelingBoxStart(0.0f, 0.0f);
    bool s_ViewportBoxSelecting = false;
    ImVec2 s_ViewportBoxStart(0.0f, 0.0f);
    ImVec2 s_ViewportBoxCurrent(0.0f, 0.0f);
    struct ActiveDockVisual
    {
        int snapKind = 0; // 0 face, 1 axis/center, 2 edge
        int alignedAxis = -1;
        glm::vec3 targetCenter{0.0f};
        glm::vec3 faceCenter{0.0f};
        glm::vec3 faceNormal{0.0f};
        glm::vec3 faceAxisU{1.0f, 0.0f, 0.0f};
        glm::vec3 faceAxisV{0.0f, 1.0f, 0.0f};
        glm::vec2 faceExtents{1.0f};
    };
    inline std::vector<ActiveDockVisual> s_ActiveDocks;
    bool s_FaceDockingActive = false;
    bool s_DockAxisAlignment = false;
    int s_DockSnapKind = 0; // 0 face, 1 axis, 2 edge, 3 vertex
    int s_DockAlignedAxis = -1;
    glm::vec3 s_DockTargetCenter(0.0f);
    glm::vec3 s_DockFaceCenter(0.0f);
    glm::vec3 s_DockFaceNormal(0.0f);
    glm::vec3 s_DockFaceAxisU(1.0f, 0.0f, 0.0f);
    glm::vec3 s_DockFaceAxisV(0.0f, 1.0f, 0.0f);
    glm::vec2 s_DockFaceExtents(1.0f);
    // UI gizmo state
    bool s_UIGizmoDragging = false;
    int s_UIGizmoHandle = -1;
    uint64_t s_UIGizmoEntityID = 0;
    ImVec2 s_UIGizmoStartMouse(0.0f, 0.0f);
    float s_UIGizmoOrigX = 0.0f, s_UIGizmoOrigY = 0.0f, s_UIGizmoOrigW = 0.0f, s_UIGizmoOrigH = 0.0f, s_UIGizmoOrigFontSize = 14.0f;
    bool s_UIPositionDragging = false;
    ImVec2 s_UIPositionStartMouse(0.0f, 0.0f);
    float s_UIPositionOrigX = 0.0f, s_UIPositionOrigY = 0.0f;
    float s_UIPositionOrigOffsets[4] = {};
    bool s_FlySpeedDragging = false;
    double s_FlySpeedLastScrollTime = -1000.0;
    bool s_ShowSceneProfiler = false;
    glm::mat4 BuildModel(const Transform& transform);
    glm::mat4 GetEntityWorldMatrix(const Entity& entity);
    bool ProjectPoint(const glm::mat4& view, const glm::mat4& projection,
                      const glm::mat4& model, const glm::vec3& local,
                      const ImVec2& imageMin, const ImVec2& imageMax, ImVec2& screen);

    size_t EstimateEntityMeshBytes(const Entity& entity)
    {
        const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
        if (!mesh) return 0;
        // CPU vertex copy + approximate GPU VBO copy; excludes driver allocator overhead.
        return mesh->GetVertices().size() * sizeof(Vertex) * 2;
    }

    void DrawSceneProfiler(Application& app, const std::vector<Entity>& entities,
                           const glm::mat4& view, const glm::mat4& projection,
                           const ImVec2& imageMin, const ImVec2& imageMax)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        size_t meshBytes = 0;
        size_t vertexCount = 0;
        size_t physicsCount = 0;
        size_t lightCount = 0;
        size_t shadowLightCount = 0;
        size_t particleCount = 0;
        size_t terrainCount = 0;
        size_t cameraCount = 0;
        size_t globalLightingCount = 0;
        for (const Entity& entity : entities)
        {
            if (entity.IsFolder || entity.HasUI() || entity.IsGlobalLight() || entity.IsGlobalScript()) continue;
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh)
            {
                meshBytes += EstimateEntityMeshBytes(entity);
                vertexCount += mesh->GetVertices().size();

                // Box the mesh's local bounds in world space and draw all 12 edges.
                const glm::vec3 center = mesh->GetBoundingCenter();
                const glm::vec3 half = mesh->GetBoundingHalfExtents();
                const glm::mat4 model = GetEntityWorldMatrix(entity);
                ImVec2 screen[8]{};
                bool visible[8]{};
                for (int corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local = center + half * glm::vec3(
                        (corner & 1) ? 1.0f : -1.0f,
                        (corner & 2) ? 1.0f : -1.0f,
                        (corner & 4) ? 1.0f : -1.0f);
                    visible[corner] = ProjectPoint(view, projection, model, local,
                        imageMin, imageMax, screen[corner]);
                }
                const ImU32 color = IM_COL32(70, 190, 255, 205);
                for (int corner = 0; corner < 8; ++corner)
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const int other = corner ^ (1 << axis);
                        if (corner < other && visible[corner] && visible[other])
                            draw->AddLine(screen[corner], screen[other], color, 1.5f);
                    }
                ImVec2 label{};
                const glm::vec3 top = center + glm::vec3(0.0f, half.y, 0.0f);
                if (ProjectPoint(view, projection, model, top, imageMin, imageMax, label))
                {
                    char text[160];
                    std::snprintf(text, sizeof(text), "%s | mesh ~%.1f KB | script %.3f ms",
                        entity.GetName().c_str(),
                        static_cast<double>(EstimateEntityMeshBytes(entity)) / 1024.0,
                        PerformanceMonitor::GetScriptTimeForEntity(entity.GetID()));
                    const ImVec2 textSize = ImGui::CalcTextSize(text);
                    const ImVec2 pad(6.0f, 4.0f);
                    draw->AddRectFilled(ImVec2(label.x - textSize.x * 0.5f - pad.x, label.y - 22.0f),
                        ImVec2(label.x + textSize.x * 0.5f + pad.x, label.y - 22.0f + textSize.y + pad.y * 2.0f),
                        IM_COL32(12, 18, 28, 150), 5.0f);
                    draw->AddText(ImVec2(label.x - textSize.x * 0.5f, label.y - 18.0f),
                        IM_COL32(225, 240, 255, 240), text);
                }
            }
            if (entity.PhysicsEnabled && entity.ColliderEnabled) ++physicsCount;
            const Entity& readonlyEntity = entity;
            if (entity.HasLight() && const_cast<Entity&>(readonlyEntity).GetLightComponent().Enabled)
            {
                ++lightCount;
                if (const_cast<Entity&>(readonlyEntity).GetLightComponent().CastShadows) ++shadowLightCount;
            }
            if (entity.HasParticleSystem()) ++particleCount;
            if (entity.HasTerrain()) ++terrainCount;
            if (entity.HasCamera()) ++cameraCount;
            if (entity.IsGlobalLight() && entity.GetGlobalLightComponent().Lighting.Enabled)
                ++globalLightingCount;
        }

        ImGui::SetNextWindowPos(ImVec2(imageMax.x - 360.0f, imageMin.y + 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 120.0f), ImVec2(480.0f, 620.0f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("Profiler da Cena##SceneProfiler", &s_ShowSceneProfiler, flags))
        {
            ImGui::Text("PROCESSO DA ENGINE");
            ImGui::Text("CPU: %.1f%%  |  RAM: %.1f MB (working set)",
                PerformanceMonitor::GetCPUUsage(), PerformanceMonitor::GetWorkingSetMB());
            ImGui::Text("FPS: %.0f  |  Frame: %.2f ms",
                PerformanceMonitor::GetFPS(), PerformanceMonitor::GetAverageFrameTimeMs());
            ImGui::Text("VRAM estimada (texturas + meshes): %.1f MB",
                PerformanceMonitor::GetEstimatedVRAMMB());
            {
                double gc0 = 0.0, gc1 = 0.0, gc2 = 0.0, gcHeap = 0.0;
                PerformanceMonitor::GetGCStats(&gc0, &gc1, &gc2, &gcHeap);
                ImGui::Text("GC .NET: heap %.1f MB | coletas/s G0 %.1f G1 %.1f G2 %.1f",
                    gcHeap, gc0, gc1, gc2);
            }
            {
                constexpr size_t kSectionCapacity =
                    static_cast<size_t>(PerformanceMonitor::FrameSection::Count);
                std::array<double, kSectionCapacity> lastMs{};
                std::array<double, kSectionCapacity> avgMs{};
                PerformanceMonitor::GetFrameSectionTimes(lastMs.data(), avgMs.data(), kSectionCapacity);
                ImGui::TextDisabled("Seções do frame (último / média ms):");
                for (size_t i = 0; i < kSectionCapacity; ++i)
                {
                    const char* name = PerformanceMonitor::GetFrameSectionName(
                        static_cast<PerformanceMonitor::FrameSection>(i));
                    ImGui::BulletText("%s: %.2f / %.2f", name, lastMs[i], avgMs[i]);
                }
            }
            bool scriptProfile = PerformanceMonitor::IsScriptProfilingEnabled();
            if (ImGui::Checkbox("Medir scripts por objeto", &scriptProfile))
                PerformanceMonitor::SetScriptProfilingEnabled(scriptProfile);
            ImGui::Separator();
            ImGui::Text("CENA — %zu objetos", entities.size());
            ImGui::Text("Meshes: %zu vértices | RAM mesh estimada: %.2f MB",
                vertexCount, static_cast<double>(meshBytes) / (1024.0 * 1024.0));
            ImGui::Text("Física/colisores: %zu | Luzes: %zu | Sombras habilitadas: %zu",
                physicsCount, lightCount, shadowLightCount);
            ImGui::Text("Luz global/sol: %zu | Partículas: %zu | Terrenos: %zu | Câmeras: %zu",
                globalLightingCount, particleCount, terrainCount, cameraCount);
            const auto& renderStats = app.GetRenderer().GetSceneRenderer().GetLastStats();
            ImGui::Text("Cena %.2f ms | Sombras %.2f ms | Draw calls %u | Triângulos %u",
                renderStats.RenderTimeMs, renderStats.ShadowTimeMs,
                renderStats.DrawCalls, renderStats.Triangles);
            if (renderStats.GpuTimeAvailable)
                ImGui::Text("GPU cena: %.2f ms", renderStats.GpuTimeMs);
            else
                ImGui::TextDisabled("GPU cena: timer não disponível neste backend");
            const auto& sky = app.GetRenderer().GetSkyRenderer();
            ImGui::Text("Céu procedural: %.2f ms | Nuvens %s | qualidade %d/5 | cobertura %.0f%%",
                app.GetRenderer().GetLastSkyRenderMs(), sky.AreCloudsEnabled() ? "ativas" : "inativas",
                sky.GetCloudQuality(), sky.GetCloudAmount() * 100.0f);
            ImGui::Separator();
            ImGui::TextWrapped("CPU por objeto mede chamadas de script (ms/frame); física e render são custos agregados. RAM por mesh estima cópia CPU + VBO e não inclui overhead do driver ou recursos compartilhados.");
            if (ImGui::TreeNode("Detalhes por objeto##ProfilerObjects"))
            {
                std::unordered_map<uint64_t, std::vector<const Entity*>> children;
                for (const Entity& entity : entities)
                {
                    children[entity.GetParentID()].push_back(&entity);
                }
                std::function<size_t(const Entity*, size_t)> subtreeBytes =
                    [&](const Entity* entity, size_t depth) -> size_t
                {
                    if (!entity || depth > entities.size()) return 0;
                    size_t total = EstimateEntityMeshBytes(*entity);
                    const auto it = children.find(entity->GetID());
                    if (it != children.end())
                        for (const Entity* child : it->second)
                            total += subtreeBytes(child, depth + 1);
                    return total;
                };
                std::function<double(const Entity*, size_t)> subtreeScriptMs =
                    [&](const Entity* entity, size_t depth) -> double
                {
                    if (!entity || depth > entities.size()) return 0.0;
                    double total = PerformanceMonitor::GetScriptTimeForEntity(entity->GetID());
                    const auto it = children.find(entity->GetID());
                    if (it != children.end())
                        for (const Entity* child : it->second)
                            total += subtreeScriptMs(child, depth + 1);
                    return total;
                };
                std::function<void(const Entity*, size_t)> drawEntity =
                    [&](const Entity* entity, size_t depth)
                {
                    if (!entity || depth > entities.size() || entity->HasUI()) return;
                    const auto& mesh = const_cast<Entity*>(entity)->GetMeshComponent().MeshData;
                    const size_t bytes = subtreeBytes(entity, 0);
                    ImGui::PushID(static_cast<int>(entity->GetID() & 0x7fffffff));
                    const auto childIt = children.find(entity->GetID());
                    const bool hasChildren = childIt != children.end() && !childIt->second.empty();
                    ImGui::Text("%s  |  total + descendentes: %.1f KB",
                        entity->GetName().c_str(), static_cast<double>(bytes) / 1024.0);
                    ImGui::SameLine();
                    ImGui::TextDisabled("scripts + filhos: %.3f ms/frame", subtreeScriptMs(entity, 0));
                    ImGui::Indent();
                    ImGui::Text("RAM mesh própria: %.1f KB", static_cast<double>(EstimateEntityMeshBytes(*entity)) / 1024.0);
                    ImGui::Text("CPU scripts: %.3f ms/frame%s",
                        PerformanceMonitor::GetScriptTimeForEntity(entity->GetID()),
                        scriptProfile ? "" : " (ative a medição)");
                    ImGui::Text("Mesh: %s | vértices: %zu", mesh ? "sim" : "não",
                        mesh ? mesh->GetVertices().size() : 0);
                    ImGui::Text("Física: %s | Luz: %s | Partículas: %s",
                        entity->PhysicsEnabled && entity->ColliderEnabled ? "sim" : "não",
                        entity->HasLight() ? "sim" : "não",
                        entity->HasParticleSystem() ? "sim" : "não");
                    if (hasChildren && ImGui::TreeNode("Filhos##Children"))
                    {
                        for (const Entity* child : childIt->second)
                            drawEntity(child, depth + 1);
                        ImGui::TreePop();
                    }
                    ImGui::Unindent();
                    ImGui::PopID();
                };
                for (const Entity& entity : entities)
                    if (entity.GetParentID() == 0)
                        drawEntity(&entity, 0);
                ImGui::TreePop();
            }
            ImGui::TextDisabled("Nuvens são calculadas no shader; não possuem buffer de RAM por objeto. Tempo de céu é CPU de submissão, não tempo GPU.");
        }
        const bool panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::End();
        if (panelHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            app.SetViewportImageHovered(false);
    }

    struct FaceScaleDragState
    {
        bool active = false;
        uint64_t entityId = 0;
        int axis = -1;
        float sign = 1.0f;
        glm::vec3 worldAxis{ 1.0f, 0.0f, 0.0f };
        float worldScalePerLocalUnit = 1.0f;
        ImVec2 startMouse{ 0.0f, 0.0f };
        ImVec2 screenAxis{ 0.0f, 0.0f };
        float pixelsPerUnit = 1.0f;
        Transform initialTransform;
        glm::vec3 boundsCenter{ 0.0f };
        glm::vec3 boundsHalfExtents{ 0.5f };
    };
    FaceScaleDragState s_FaceScaleDrag;

    struct AxisGizmoDragState
    {
        bool active = false;
        uint64_t entityId = 0;
        bool group = false;
        std::vector<int> groupIndices;
        std::vector<Transform> groupInitialTransforms;
        std::vector<glm::mat4> groupInitialModels;
        int axis = -1;
        int axis2 = -1;
        bool plane = false;
        glm::vec3 worldAxis2{ 0.0f, 1.0f, 0.0f };
        ImVec2 planeScreenU{ 1.0f, 0.0f };
        ImVec2 planeScreenV{ 0.0f, 1.0f };
        Application::GizmoMode mode = Application::GizmoMode::Translate;
        ImVec2 startMouse{ 0.0f, 0.0f };
        ImVec2 centerScreen{ 0.0f, 0.0f };
        ImVec2 screenAxis{ 0.0f, 0.0f };
        float pixelsPerUnit = 1.0f;
        float startAngle = 0.0f;
        float lastAngle = 0.0f;
        float accumulatedRotation = 0.0f;
        float rotationDirection = 1.0f;
        glm::vec3 worldAxis{ 1.0f, 0.0f, 0.0f };
        Transform initialTransform;
        Transform initialLocalTransform;
        bool hasModelChildren = false;
        glm::mat4 initialParentModel{ 1.0f };
    };
    AxisGizmoDragState s_AxisGizmoDrag;

    float PointSegmentDistanceSquared(const ImVec2& point, const ImVec2& a, const ImVec2& b)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float lengthSquared = dx * dx + dy * dy;
        const float t = lengthSquared > 0.0001f
            ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        const float px = a.x + t * dx - point.x;
        const float py = a.y + t * dy - point.y;
        return px * px + py * py;
    }

    void DrawGizmoReadout(ImDrawList* drawList, const char* text, const ImVec2& mouse,
                          const ImVec2& viewportMin, const ImVec2& viewportMax)
    {
        if (!drawList || !text || text[0] == '\0')
            return;

        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 padding(7.0f, 5.0f);
        const ImVec2 boxSize(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        ImVec2 boxMin(mouse.x + 15.0f, mouse.y + 15.0f);
        if (boxMin.x + boxSize.x > viewportMax.x)
            boxMin.x = mouse.x - boxSize.x - 15.0f;
        if (boxMin.y + boxSize.y > viewportMax.y)
            boxMin.y = mouse.y - boxSize.y - 15.0f;
        boxMin.x = std::clamp(boxMin.x, viewportMin.x, std::max(viewportMin.x, viewportMax.x - boxSize.x));
        boxMin.y = std::clamp(boxMin.y, viewportMin.y, std::max(viewportMin.y, viewportMax.y - boxSize.y));
        const ImVec2 boxMax(boxMin.x + boxSize.x, boxMin.y + boxSize.y);
        drawList->AddRectFilled(boxMin, boxMax, IM_COL32(22, 27, 36, 238), 4.0f);
        drawList->AddRect(boxMin, boxMax, IM_COL32(57, 130, 210, 245), 4.0f, 0, 1.0f);
        drawList->AddText(ImVec2(boxMin.x + padding.x, boxMin.y + padding.y),
                          IM_COL32(238, 243, 250, 255), text);
    }

    static bool IsInsideViewport(const ImVec2& point, const ImVec2& min, const ImVec2& max)
    {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }

    glm::mat4 BuildModel(const Transform& transform)
    {
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
        model *= glm::mat4_cast(transform.Rotation);
        return glm::scale(model, glm::vec3(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z));
    }

    glm::mat4 GetEntityWorldMatrix(const Entity& entity)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        if (entity.GetParentID() != 0)
            return transform.WorldMatrix;
        return BuildModel(transform);
    }

    void ApplyGizmoMatrix(Entity& entity, const glm::mat4& model)
    {
        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(
            &model[0][0], translation, rotation, scale);

        Transform& transform = entity.GetTransform();
        transform.Position = Vector3(translation[0], translation[1], translation[2]);
        transform.EulerRotation = Vector3(rotation[0], rotation[1], rotation[2]);
        transform.Rotation = glm::quat(glm::radians(glm::vec3(
            rotation[0], rotation[1], rotation[2])));
        transform.Scale = Vector3(
            std::max(0.001f, scale[0]),
            std::max(0.001f, scale[1]),
            std::max(0.001f, scale[2]));
    }

    Transform DecomposeGizmoTransform(const glm::mat4& matrix, Transform result)
    {
        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(&matrix[0][0], translation, rotation, scale);
        result.Position = Vector3(translation[0], translation[1], translation[2]);
        result.EulerRotation = Vector3(rotation[0], rotation[1], rotation[2]);
        result.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
        result.Scale = Vector3(std::max(0.001f, scale[0]), std::max(0.001f, scale[1]),
                               std::max(0.001f, scale[2]));
        return result;
    }

    void ApplyWorldGizmoMatrix(Scene& scene, Entity& entity, const glm::mat4& worldMatrix)
    {
        glm::mat4 localMatrix = worldMatrix;
        if (entity.GetParentID() != 0)
        {
            Entity* parent = scene.FindEntityByID(entity.GetParentID());
            if (parent)
            {
                const glm::mat4& parentWorld = parent->GetTransform().WorldMatrix;
                if (std::abs(glm::determinant(parentWorld)) < 1e-8f)
                    return;
                localMatrix = glm::inverse(parentWorld) * worldMatrix;
            }
        }
        entity.GetTransform() = DecomposeGizmoTransform(localMatrix, entity.GetTransform());
        entity.GetTransform().UpdateLocalMatrix();
    }

    glm::mat4 BuildViewportProjection(const Camera& camera,
                                      float width,
                                      float height)
    {
        CameraRenderer renderer;
        const float aspect = width / std::max(1.0f, height);
        if (camera.Orthographic)
        {
            const float halfHeight = std::max(0.01f, camera.OrthographicSize) * 0.5f;
            const float halfWidth = halfHeight * aspect;
            return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                              std::max(0.001f, camera.NearClip),
                              std::max(camera.NearClip + 0.01f, camera.FarClip));
        }

        return renderer.GetProjectionMatrix(
            std::clamp(camera.FieldOfView, 1.0f, 179.0f),
            aspect,
            std::max(0.001f, camera.NearClip),
            std::max(camera.NearClip + 0.01f, camera.FarClip));
    }

    ImGuizmo::OPERATION GetGizmoOperation(Application::GizmoMode mode)
    {
        switch (mode)
        {
        case Application::GizmoMode::Rotate: return ImGuizmo::ROTATE;
        case Application::GizmoMode::Scale: return ImGuizmo::SCALE;
        case Application::GizmoMode::Translate:
        default: return ImGuizmo::TRANSLATE;
        }
    }

    bool ProjectPoint(const glm::mat4& view, const glm::mat4& projection,
                      const glm::mat4& model, const glm::vec3& local,
                      const ImVec2& imageMin, const ImVec2& imageMax,
                      ImVec2& screen)
    {
        const glm::vec4 clip = projection * view * model * glm::vec4(local, 1.0f);
        if (clip.w <= 0.0001f)
            return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.1f || ndc.z > 1.1f)
            return false;
        screen.x = imageMin.x + (ndc.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x);
        screen.y = imageMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y);
        return true;
    }

    Picking::Hit PickEntity2DAtScreenPoint(
        Application& app, const glm::mat4& view, const glm::mat4& projection,
        const ImVec2& imageMin, const ImVec2& imageMax, const ImVec2& mouse)
    {
        app.GetScene().UpdateTransforms();
        Picking::Hit best;
        float nearestDepth = std::numeric_limits<float>::max();
        const auto& entities = app.GetScene().GetEntities();
        const glm::vec2 point(mouse.x, mouse.y);

        for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
        {
            const Entity& entity = entities[entityIndex];
            if (entity.SelectionLocked || entity.IsEditorOnly() || entity.IsFolder || entity.IsModel ||
                entity.HasUI() || entity.HasCamera() || entity.IsGlobalLight() || entity.IsGlobalScript() ||
                !entity.Active || entity.Opacity <= 0.001f || !entity.RaycastTarget)
                continue;

            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (!mesh || !mesh->IsValid() || mesh->GetVertices().size() < 3 ||
                mesh->GetPrimitiveType() != GL_TRIANGLES)
                continue;

            const glm::mat4 model = GetEntityWorldMatrix(entity);
            const auto& vertices = mesh->GetVertices();
            for (size_t vertexIndex = 0; vertexIndex + 2 < vertices.size(); vertexIndex += 3)
            {
                const Vertex& va = vertices[vertexIndex];
                const Vertex& vb = vertices[vertexIndex + 1];
                const Vertex& vc = vertices[vertexIndex + 2];
                const glm::vec3 la(va.X, va.Y, va.Z);
                const glm::vec3 lb(vb.X, vb.Y, vb.Z);
                const glm::vec3 lc(vc.X, vc.Y, vc.Z);
                ImVec2 sa{}, sb{}, sc{};
                if (!ProjectPoint(view, projection, model, la, imageMin, imageMax, sa) ||
                    !ProjectPoint(view, projection, model, lb, imageMin, imageMax, sb) ||
                    !ProjectPoint(view, projection, model, lc, imageMin, imageMax, sc))
                    continue;

                const glm::vec2 a(sa.x, sa.y), b(sb.x, sb.y), c(sc.x, sc.y);
                const float area = (b.x - a.x) * (c.y - a.y) -
                                   (b.y - a.y) * (c.x - a.x);
                if (std::abs(area) < 0.0001f)
                    continue;
                const float wa = ((b.x - point.x) * (c.y - point.y) -
                                  (b.y - point.y) * (c.x - point.x)) / area;
                const float wb = ((c.x - point.x) * (a.y - point.y) -
                                  (c.y - point.y) * (a.x - point.x)) / area;
                const float wc = 1.0f - wa - wb;
                if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
                    continue;

                const glm::vec3 world = glm::vec3(model * glm::vec4(
                    la * wa + lb * wb + lc * wc, 1.0f));
                const float depth = -(view * glm::vec4(world, 1.0f)).z;
                if (depth < 0.0f || depth >= nearestDepth)
                    continue;
                nearestDepth = depth;
                best.EntityIndex = static_cast<int>(entityIndex);
                best.TriangleIndex = static_cast<int>(vertexIndex / 3);
                best.WorldPosition = world;
                best.LocalPosition = la * wa + lb * wb + lc * wc;
            }
        }
        return best;
    }

    void GetLocalMeshBounds(Entity& entity, glm::vec3& minimum, glm::vec3& maximum)
    {
        minimum = glm::vec3(-0.5f);
        maximum = glm::vec3(0.5f);
        const auto& mesh = entity.GetMeshComponent().MeshData;
        if (!mesh || mesh->GetVertices().empty())
            return;

        const glm::vec3 center = mesh->GetBoundingCenter();
        const glm::vec3 halfExtents = mesh->GetBoundingHalfExtents();
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(halfExtents.x) || !std::isfinite(halfExtents.y) ||
            !std::isfinite(halfExtents.z))
        {
            minimum = glm::vec3(-0.5f);
            maximum = glm::vec3(0.5f);
            return;
        }
        minimum = center - halfExtents;
        maximum = center + halfExtents;
    }

    struct DockCell
    {
        int64_t x = 0, y = 0, z = 0;
        bool operator==(const DockCell& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct DockCellHash
    {
        size_t operator()(const DockCell& cell) const
        {
            size_t hash = std::hash<int64_t>{}(cell.x);
            hash ^= std::hash<int64_t>{}(cell.y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            hash ^= std::hash<int64_t>{}(cell.z) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    DockCell GetDockCell(const glm::vec3& point, float size)
    {
        return {
            static_cast<int64_t>(std::floor(point.x / size)),
            static_cast<int64_t>(std::floor(point.y / size)),
            static_cast<int64_t>(std::floor(point.z / size))
        };
    }

    struct DockEdge
    {
        glm::vec3 a{ 0.0f };
        glm::vec3 b{ 0.0f };
        glm::vec3 center{ 0.0f };
        glm::vec3 direction{ 0.0f };
        float length = 0.0f;
    };

    std::vector<DockCell> GetDockEdgeCells(const DockEdge& edge, float cellSize)
    {
        const float cellStep = std::max(cellSize * 0.5f, 0.001f);
        const int steps = std::clamp(
            static_cast<int>(std::ceil(edge.length / cellStep)), 1, 4096);
        std::vector<DockCell> cells;
        cells.reserve(static_cast<size_t>(steps) + 1);
        for (int step = 0; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            const DockCell cell = GetDockCell(glm::mix(edge.a, edge.b, t), cellSize);
            if (cells.empty() || !(cells.back() == cell))
                cells.push_back(cell);
        }
        return cells;
    }

    void ClosestPointsOnSegments(const DockEdge& first, const DockEdge& second,
                                 glm::vec3& firstPoint, glm::vec3& secondPoint)
    {
        const glm::vec3 u = first.b - first.a;
        const glm::vec3 v = second.b - second.a;
        const glm::vec3 w = first.a - second.a;
        const float a = glm::dot(u, u);
        const float b = glm::dot(u, v);
        const float c = glm::dot(v, v);
        const float d = glm::dot(u, w);
        const float e = glm::dot(v, w);
        const float denominator = a * c - b * b;
        float sNumerator = 0.0f;
        float sDenominator = denominator;
        float tNumerator = 0.0f;
        float tDenominator = denominator;

        if (denominator < 0.000001f)
        {
            sNumerator = 0.0f;
            sDenominator = 1.0f;
            tNumerator = e;
            tDenominator = c;
        }
        else
        {
            sNumerator = b * e - c * d;
            tNumerator = a * e - b * d;
            if (sNumerator < 0.0f)
            {
                sNumerator = 0.0f;
                tNumerator = e;
                tDenominator = c;
            }
            else if (sNumerator > sDenominator)
            {
                sNumerator = sDenominator;
                tNumerator = e + b;
                tDenominator = c;
            }
        }

        if (tNumerator < 0.0f)
        {
            tNumerator = 0.0f;
            if (-d < 0.0f)
                sNumerator = 0.0f;
            else if (-d > a)
                sNumerator = sDenominator;
            else
            {
                sNumerator = -d;
                sDenominator = a;
            }
        }
        else if (tNumerator > tDenominator)
        {
            tNumerator = tDenominator;
            if (-d + b < 0.0f)
                sNumerator = 0.0f;
            else if (-d + b > a)
                sNumerator = sDenominator;
            else
            {
                sNumerator = -d + b;
                sDenominator = a;
            }
        }

        const float s = std::abs(sNumerator) < 0.000001f ? 0.0f : sNumerator / sDenominator;
        const float t = std::abs(tNumerator) < 0.000001f ? 0.0f : tNumerator / tDenominator;
        firstPoint = first.a + u * s;
        secondPoint = second.a + v * t;
    }

    struct LocalSnapTopology
    {
        std::weak_ptr<Mesh> owner;
        uint64_t revision = 0;
        std::vector<glm::vec3> points;
        std::vector<std::pair<glm::vec3, glm::vec3>> edges;
    };

    void BuildWorldSnapFeatures(Entity& entity, const glm::mat4& world,
                                std::vector<glm::vec3>& points,
                                std::vector<DockEdge>& edges)
    {
        points.clear();
        edges.clear();
        const auto& mesh = entity.GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid())
            return;

        // Topology is immutable between Mesh::Create calls. Cache its expensive
        // vertex welding/edge classification; each frame only transforms the
        // small set of actual corners and crease edges into world space.
        static std::unordered_map<const Mesh*, LocalSnapTopology> topologyCache;
        LocalSnapTopology& topology = topologyCache[mesh.get()];
        const auto owner = topology.owner.lock();
        if (owner.get() != mesh.get() || topology.revision != mesh->GetRevision())
        {
            topology = LocalSnapTopology{};
            topology.owner = mesh;
            topology.revision = mesh->GetRevision();
            const auto& vertices = mesh->GetVertices();
            using PositionKey = std::array<int64_t, 3>;
            std::map<PositionKey, uint32_t> unique;
            std::vector<glm::vec3> localPoints;
            std::vector<uint32_t> vertexIds(vertices.size());
            constexpr double quantization = 100000.0;
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const Vertex& vertex = vertices[i];
                const PositionKey key = {
                    static_cast<int64_t>(std::llround(vertex.X * quantization)),
                    static_cast<int64_t>(std::llround(vertex.Y * quantization)),
                    static_cast<int64_t>(std::llround(vertex.Z * quantization))
                };
                auto found = unique.find(key);
                if (found == unique.end())
                {
                    const uint32_t id = static_cast<uint32_t>(localPoints.size());
                    localPoints.emplace_back(vertex.X, vertex.Y, vertex.Z);
                    unique.emplace(key, id);
                    vertexIds[i] = id;
                }
                else
                    vertexIds[i] = found->second;
            }

            struct EdgeFeature
            {
                glm::vec3 firstNormal{ 0.0f };
                uint32_t adjacentTriangles = 0;
                bool isCrease = false;
            };
            std::map<std::pair<uint32_t, uint32_t>, EdgeFeature> edgeFeatures;
            for (size_t i = 0; i + 2 < vertexIds.size(); i += 3)
            {
                const uint32_t triangle[3] = { vertexIds[i], vertexIds[i + 1], vertexIds[i + 2] };
                const glm::vec3 crossProduct = glm::cross(
                    localPoints[triangle[1]] - localPoints[triangle[0]],
                    localPoints[triangle[2]] - localPoints[triangle[0]]);
                const float normalLength = glm::length(crossProduct);
                if (normalLength <= 0.000001f)
                    continue;
                const glm::vec3 triangleNormal = crossProduct / normalLength;
                for (int edge = 0; edge < 3; ++edge)
                {
                    uint32_t a = triangle[edge];
                    uint32_t b = triangle[(edge + 1) % 3];
                    if (a == b) continue;
                    if (a > b) std::swap(a, b);
                    EdgeFeature& feature = edgeFeatures[{ a, b }];
                    if (feature.adjacentTriangles == 0)
                        feature.firstNormal = triangleNormal;
                    else if (glm::dot(feature.firstNormal, triangleNormal) < 0.9995f)
                        feature.isCrease = true;
                    ++feature.adjacentTriangles;
                }
            }

            std::vector<bool> isFeatureVertex(localPoints.size(), false);
            for (const auto& [edge, feature] : edgeFeatures)
            {
                if (feature.adjacentTriangles != 1 && !feature.isCrease)
                    continue;
                isFeatureVertex[edge.first] = true;
                isFeatureVertex[edge.second] = true;
                topology.edges.emplace_back(localPoints[edge.first], localPoints[edge.second]);
            }
            for (size_t i = 0; i < localPoints.size(); ++i)
                if (isFeatureVertex[i]) topology.points.push_back(localPoints[i]);

            if (topologyCache.size() > 512)
                for (auto it = topologyCache.begin(); it != topologyCache.end(); )
                    if (it->second.owner.expired()) it = topologyCache.erase(it);
                    else ++it;
        }

        points.reserve(topology.points.size());
        edges.reserve(topology.edges.size());
        for (const glm::vec3& localPoint : topology.points)
            points.push_back(glm::vec3(world * glm::vec4(localPoint, 1.0f)));
        for (const auto& localEdge : topology.edges)
        {
            const glm::vec3 worldA = glm::vec3(world * glm::vec4(localEdge.first, 1.0f));
            const glm::vec3 worldB = glm::vec3(world * glm::vec4(localEdge.second, 1.0f));
            const glm::vec3 delta = worldB - worldA;
            const float length = glm::length(delta);
            if (length > 0.00001f)
                edges.push_back({ worldA, worldB, (worldA + worldB) * 0.5f,
                                  delta / length, length });
        }
    }

    int DockSnapPriority(int kind)
    {
        // A face connection is the most useful result for modular building;
        // edge and vertex snaps remain available when no face pair is suitable.
        switch (kind)
        {
        case 0: return 0; // face
        case 2: return 1; // edge
        case 3: return 2; // vertex
        case 1: return 3; // center alignment
        default: return 4;
        }
    }

    bool IsBetterDockCandidate(int kind, float distance,
                               int currentKind, float currentDistance)
    {
        if (currentKind < 0)
            return true;
        const int candidatePriority = DockSnapPriority(kind);
        const int currentPriority = DockSnapPriority(currentKind);
        return candidatePriority < currentPriority ||
            (candidatePriority == currentPriority && distance < currentDistance);
    }

    bool ApplyFaceDocking(Application& app, Entity& moving,
                          const glm::vec3* constrainedAxis = nullptr)
    {
        s_FaceDockingActive = false;
        s_DockAxisAlignment = false;
        s_DockAlignedAxis = -1;
        s_ActiveDocks.clear();
        const SnapSettings& settings = app.GetSnapSettings();
        if (!settings.FaceSnapEnabled || moving.IsFolder || moving.IsModel ||
            moving.HasUI() || !moving.Active || moving.Opacity <= 0.001f)
            return false;

        auto& scene = app.GetScene();

        auto& movingMesh = moving.GetMeshComponent().MeshData;
        if (!movingMesh || !movingMesh->IsValid())
            return false;

        glm::vec3 movingMin, movingMax;
        GetLocalMeshBounds(moving, movingMin, movingMax);
        const glm::vec3 movingCenter = (movingMin + movingMax) * 0.5f;
        const glm::vec3 movingHalf = glm::max((movingMax - movingMin) * 0.5f, glm::vec3(0.0001f));

        glm::mat4 parentWorld(1.0f);
        Entity* spatialParent = moving.GetParentID() != 0
            ? scene.FindEntityByID(moving.GetParentID()) : nullptr;
        std::unordered_set<uint64_t> visitedParents;
        while (spatialParent && spatialParent->IsFolder &&
               visitedParents.insert(spatialParent->GetID()).second)
            spatialParent = spatialParent->GetParentID() != 0
                ? scene.FindEntityByID(spatialParent->GetParentID()) : nullptr;
        if (spatialParent)
            parentWorld = spatialParent->GetTransform().WorldMatrix;
        const glm::mat4 movingWorld = parentWorld * BuildModel(moving.GetTransform());
        const glm::mat3 movingLinear(movingWorld);
        const float movingDeterminant = glm::determinant(movingLinear);
        if (!std::isfinite(movingDeterminant) || std::abs(movingDeterminant) < 1e-8f)
            return false;
        const glm::mat3 movingNormalMatrix = glm::transpose(glm::inverse(movingLinear));
        const glm::vec3 movingWorldCenter = glm::vec3(
            movingWorld * glm::vec4(movingCenter, 1.0f));
        glm::vec3 movingWorldHalf(0.0f);
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                movingWorldHalf[row] += std::abs(movingLinear[column][row]) * movingHalf[column];

        const float faceThreshold = std::max(0.0f, settings.FaceSnapThreshold);
        const float threshold = std::max({
            settings.FaceAlignmentEnabled ? faceThreshold : 0.0f,
            settings.AxisAlignmentEnabled ? faceThreshold : 0.0f,
            faceThreshold
        });
        if (threshold <= 0.0f)
            return false;
        const float lateralTolerance = std::max(0.0f, settings.FaceAlignTolerance);

        auto isAxisAligned = [](const glm::mat3& linear)
        {
            bool usedAxes[3] = { false, false, false };
            for (int column = 0; column < 3; ++column)
            {
                const float length = glm::length(linear[column]);
                if (length <= 1e-6f)
                    return false;
                const glm::vec3 axis = linear[column] / length;
                int dominant = 0;
                if (std::abs(axis.y) > std::abs(axis.x)) dominant = 1;
                if (std::abs(axis.z) > std::abs(axis[dominant])) dominant = 2;
                if (std::abs(axis[dominant]) < 0.999f || usedAxes[dominant])
                    return false;
                usedAxes[dominant] = true;
            }
            return true;
        };
        const bool movingAxisAligned = isAxisAligned(movingLinear);

        // Suporte a multi-docking simultaneo em ate 3 eixos ortogonais (ex: chao em Y + parede lateral em X + parede de fundo em Z)
        struct DockSlotCandidate
        {
            bool valid = false;
            int priority = 999; // 0 face-to-face contact, 1 edge flush, 2 center alignment
            float distance = std::numeric_limits<float>::max();
            glm::vec3 correction{ 0.0f };
            int snapKind = 0; // 0 face, 1 center, 2 edge
            int alignedAxis = -1;
            glm::vec3 targetCenter{ 0.0f };
            glm::vec3 faceCenter{ 0.0f };
            glm::vec3 faceNormal{ 0.0f };
            glm::vec3 faceAxisU{ 1.0f, 0.0f, 0.0f };
            glm::vec3 faceAxisV{ 0.0f, 1.0f, 0.0f };
            glm::vec2 faceExtents{ 0.5f };
        };
        std::array<DockSlotCandidate, 3> bestSlotCandidates;

        auto considerCandidate = [&](int slot, int priority, float distance,
                                    const glm::vec3& correction, int snapKind, int alignedAxis,
                                    const glm::vec3& faceCenter, const glm::vec3& targetCenter,
                                    const glm::vec3& faceNormal, const glm::vec3& axisU,
                                    const glm::vec3& axisV, const glm::vec2& extents)
        {
            if (slot < 0 || slot >= 3) return;
            DockSlotCandidate& cur = bestSlotCandidates[static_cast<size_t>(slot)];
            if (!cur.valid || priority < cur.priority ||
                (priority == cur.priority && distance < cur.distance))
            {
                cur.valid = true;
                cur.priority = priority;
                cur.distance = distance;
                cur.correction = correction;
                cur.snapKind = snapKind;
                cur.alignedAxis = alignedAxis;
                cur.faceCenter = faceCenter;
                cur.targetCenter = targetCenter;
                cur.faceNormal = faceNormal;
                cur.faceAxisU = axisU;
                cur.faceAxisV = axisV;
                cur.faceExtents = extents;
            }
        };

        const auto& entities = scene.GetEntities();
        for (const Entity& target : entities)
        {
            if (target.GetID() == moving.GetID() || target.IsEditorOnly() ||
                target.IsFolder || target.IsModel || target.HasUI() ||
                !target.Active || target.Opacity <= 0.001f)
                continue;

            Entity* targetEntity = const_cast<Entity*>(&target);

            // Um objeto nao deve encaixar em um filho que se move junto com ele.
            bool isMovingDescendant = false;
            uint64_t ancestorID = target.GetParentID();
            size_t ancestorGuard = 0;
            while (ancestorID != 0 && ancestorGuard++ <= entities.size())
            {
                if (ancestorID == moving.GetID())
                {
                    isMovingDescendant = true;
                    break;
                }
                const Entity* ancestor = scene.FindEntityByID(ancestorID);
                ancestorID = ancestor ? ancestor->GetParentID() : 0;
            }
            if (isMovingDescendant)
                continue;

            auto& targetMesh = targetEntity->GetMeshComponent().MeshData;
            if (!targetMesh || !targetMesh->IsValid())
                continue;

            glm::vec3 targetMin, targetMax;
            GetLocalMeshBounds(*targetEntity, targetMin, targetMax);
            const glm::vec3 targetCenter = (targetMin + targetMax) * 0.5f;
            const glm::vec3 targetHalf = glm::max((targetMax - targetMin) * 0.5f, glm::vec3(0.0001f));
            const glm::mat4 targetWorld = targetEntity->GetTransform().WorldMatrix;
            const glm::mat3 targetLinear(targetWorld);
            const float targetDeterminant = glm::determinant(targetLinear);
            if (!std::isfinite(targetDeterminant) || std::abs(targetDeterminant) < 1e-8f)
                continue;
            const bool targetAxisAligned = isAxisAligned(targetLinear);
            const glm::mat3 targetNormalMatrix = glm::transpose(glm::inverse(targetLinear));
            const glm::vec3 targetWorldCenter = glm::vec3(
                targetWorld * glm::vec4(targetCenter, 1.0f));
            glm::vec3 targetWorldHalf(0.0f);
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    targetWorldHalf[row] += std::abs(targetLinear[column][row]) * targetHalf[column];

            // Bounding box rejection
            bool boundsNear = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(movingWorldCenter[axis] - targetWorldCenter[axis]) >
                    movingWorldHalf[axis] + targetWorldHalf[axis] + threshold + lateralTolerance)
                {
                    boundsNear = false;
                    break;
                }
            }
            if (!boundsNear)
                continue;

            // 1. DOCKING ALINHADO AOS EIXOS (Axis-aligned contact - pisos, paredes, tetos e plataformas)
            if (movingAxisAligned && targetAxisAligned)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    const int tangent0 = (axis + 1) % 3;
                    const int tangent1 = (axis + 2) % 3;

                    // Sobreposicao nas direcoes tangentes a face
                    const float tangentOverlap0 = std::abs(movingWorldCenter[tangent0] - targetWorldCenter[tangent0]) -
                                                 (movingWorldHalf[tangent0] + targetWorldHalf[tangent0]);
                    const float tangentOverlap1 = std::abs(movingWorldCenter[tangent1] - targetWorldCenter[tangent1]) -
                                                 (movingWorldHalf[tangent1] + targetWorldHalf[tangent1]);
                    const bool overlapsTangentially = (tangentOverlap0 <= lateralTolerance && tangentOverlap1 <= lateralTolerance);

                    if (overlapsTangentially)
                    {
                        // Lado 1: Face inferior/negativa do moving encosta na face superior/positiva do target
                        // Ex: fundo da peca repousando sobre o chao ou topo do bloco
                        const float gapMin = (movingWorldCenter[axis] - movingWorldHalf[axis]) -
                                             (targetWorldCenter[axis] + targetWorldHalf[axis]);
                        if (std::abs(gapMin) <= faceThreshold)
                        {
                            glm::vec3 corr(0.0f);
                            corr[axis] = -gapMin;
                            glm::vec3 norm(0.0f);
                            norm[axis] = 1.0f;
                            glm::vec3 u(0.0f), v(0.0f);
                            u[tangent0] = 1.0f;
                            v[tangent1] = 1.0f;
                            glm::vec2 ext(std::min(movingWorldHalf[tangent0], targetWorldHalf[tangent0]),
                                          std::min(movingWorldHalf[tangent1], targetWorldHalf[tangent1]));
                            glm::vec3 faceCtr = movingWorldCenter;
                            faceCtr[axis] = targetWorldCenter[axis] + targetWorldHalf[axis];
                            glm::vec3 tgtCtr = targetWorldCenter;
                            tgtCtr[axis] = targetWorldCenter[axis] + targetWorldHalf[axis];
                            considerCandidate(axis, 0, std::abs(gapMin), corr, 0, -1,
                                              faceCtr, tgtCtr, norm, u, v, ext);
                        }

                        // Lado 2: Face superior/positiva do moving encosta na face inferior/negativa do target
                        // Ex: topo da peca encostando na face inferior ou lateral oposta
                        const float gapMax = (targetWorldCenter[axis] - targetWorldHalf[axis]) -
                                             (movingWorldCenter[axis] + movingWorldHalf[axis]);
                        if (std::abs(gapMax) <= faceThreshold)
                        {
                            glm::vec3 corr(0.0f);
                            corr[axis] = gapMax;
                            glm::vec3 norm(0.0f);
                            norm[axis] = -1.0f;
                            glm::vec3 u(0.0f), v(0.0f);
                            u[tangent0] = 1.0f;
                            v[tangent1] = 1.0f;
                            glm::vec2 ext(std::min(movingWorldHalf[tangent0], targetWorldHalf[tangent0]),
                                          std::min(movingWorldHalf[tangent1], targetWorldHalf[tangent1]));
                            glm::vec3 faceCtr = movingWorldCenter;
                            faceCtr[axis] = targetWorldCenter[axis] - targetWorldHalf[axis];
                            glm::vec3 tgtCtr = targetWorldCenter;
                            tgtCtr[axis] = targetWorldCenter[axis] - targetWorldHalf[axis];
                            considerCandidate(axis, 0, std::abs(gapMax), corr, 0, -1,
                                              faceCtr, tgtCtr, norm, u, v, ext);
                        }
                    }

                    // Alinhamento de Arestas Flush (FaceAlignmentEnabled) e Centro (AxisAlignmentEnabled)
                    // quando objetos ja estao encostados ou muito proximos em outro eixo
                    for (int otherAxis = 0; otherAxis < 3; ++otherAxis)
                    {
                        if (otherAxis == axis) continue;
                        const float contactGap = std::abs(std::abs(movingWorldCenter[otherAxis] - targetWorldCenter[otherAxis]) -
                                                 (movingWorldHalf[otherAxis] + targetWorldHalf[otherAxis]));
                        if (contactGap <= faceThreshold + 0.05f)
                        {
                            // Alinhamento de arestas flush
                            if (settings.FaceAlignmentEnabled)
                            {
                                // Arestas maximas coincidentes
                                const float edgeMaxGap = (targetWorldCenter[axis] + targetWorldHalf[axis]) -
                                                         (movingWorldCenter[axis] + movingWorldHalf[axis]);
                                if (std::abs(edgeMaxGap) <= lateralTolerance && std::abs(edgeMaxGap) > 0.001f)
                                {
                                    glm::vec3 corr(0.0f);
                                    corr[axis] = edgeMaxGap;
                                    considerCandidate(axis, 1, std::abs(edgeMaxGap), corr, 2, axis,
                                                      movingWorldCenter + corr, targetWorldCenter,
                                                      glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                                                      glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.5f));
                                }

                                // Arestas minimas coincidentes
                                const float edgeMinGap = (targetWorldCenter[axis] - targetWorldHalf[axis]) -
                                                         (movingWorldCenter[axis] - movingWorldHalf[axis]);
                                if (std::abs(edgeMinGap) <= lateralTolerance && std::abs(edgeMinGap) > 0.001f)
                                {
                                    glm::vec3 corr(0.0f);
                                    corr[axis] = edgeMinGap;
                                    considerCandidate(axis, 1, std::abs(edgeMinGap), corr, 2, axis,
                                                      movingWorldCenter + corr, targetWorldCenter,
                                                      glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                                                      glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.5f));
                                }
                            }

                            // Alinhamento de Centros
                            if (settings.AxisAlignmentEnabled)
                            {
                                const float centerGap = targetWorldCenter[axis] - movingWorldCenter[axis];
                                if (std::abs(centerGap) <= lateralTolerance && std::abs(centerGap) > 0.001f)
                                {
                                    glm::vec3 corr(0.0f);
                                    corr[axis] = centerGap;
                                    considerCandidate(axis, 2, std::abs(centerGap), corr, 1, axis,
                                                      movingWorldCenter + corr, targetWorldCenter,
                                                      glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                                                      glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.5f));
                                }
                            }
                        }
                    }
                }
            }
            else if (settings.FaceAlignmentEnabled)
            {
                // 2. DOCKING GERAL PARA MESHES ROTACIONADOS / ARBITRARIOS
                for (int movingAxis = 0; movingAxis < 3; ++movingAxis)
                {
                    int movingTangent[2]{};
                    int tangentIndex = 0;
                    for (int axis = 0; axis < 3; ++axis)
                        if (axis != movingAxis) movingTangent[tangentIndex++] = axis;

                    for (int movingSign : { -1, 1 })
                    {
                        glm::vec3 movingLocalFace = movingCenter;
                        movingLocalFace[movingAxis] = movingSign > 0
                            ? movingMax[movingAxis] : movingMin[movingAxis];
                        const glm::vec3 movingFaceCenter = glm::vec3(
                            movingWorld * glm::vec4(movingLocalFace, 1.0f));
                        const glm::vec3 movingNormal = glm::normalize(movingNormalMatrix *
                            (glm::vec3(movingAxis == 0, movingAxis == 1, movingAxis == 2) *
                             static_cast<float>(movingSign)));

                        for (int targetAxis = 0; targetAxis < 3; ++targetAxis)
                        {
                            int targetTangent[2]{};
                            tangentIndex = 0;
                            for (int axis = 0; axis < 3; ++axis)
                                if (axis != targetAxis) targetTangent[tangentIndex++] = axis;

                            for (int targetSign : { -1, 1 })
                            {
                                glm::vec3 targetLocalFace = targetCenter;
                                targetLocalFace[targetAxis] = targetSign > 0
                                    ? targetMax[targetAxis] : targetMin[targetAxis];
                                const glm::vec3 targetFaceCenter = glm::vec3(
                                    targetWorld * glm::vec4(targetLocalFace, 1.0f));
                                const glm::vec3 targetNormal = glm::normalize(targetNormalMatrix *
                                    (glm::vec3(targetAxis == 0, targetAxis == 1, targetAxis == 2) *
                                     static_cast<float>(targetSign)));

                                if (std::abs(glm::dot(movingNormal, targetNormal)) < 0.995f)
                                    continue;

                                const glm::vec3 targetU = glm::normalize(targetLinear *
                                    glm::vec3(targetTangent[0] == 0, targetTangent[0] == 1,
                                              targetTangent[0] == 2));
                                const glm::vec3 targetV = glm::normalize(targetLinear *
                                    glm::vec3(targetTangent[1] == 0, targetTangent[1] == 1,
                                              targetTangent[1] == 2));
                                const float targetHalfU = glm::length(targetLinear *
                                    (glm::vec3(targetTangent[0] == 0, targetTangent[0] == 1,
                                               targetTangent[0] == 2) * targetHalf[targetTangent[0]]));
                                const float targetHalfV = glm::length(targetLinear *
                                    (glm::vec3(targetTangent[1] == 0, targetTangent[1] == 1,
                                               targetTangent[1] == 2) * targetHalf[targetTangent[1]]));
                                const glm::vec3 movingEdgeA = movingLinear *
                                    (glm::vec3(movingTangent[0] == 0, movingTangent[0] == 1,
                                               movingTangent[0] == 2) * movingHalf[movingTangent[0]]);
                                const glm::vec3 movingEdgeB = movingLinear *
                                    (glm::vec3(movingTangent[1] == 0, movingTangent[1] == 1,
                                               movingTangent[1] == 2) * movingHalf[movingTangent[1]]);
                                const float movingRadiusU = std::abs(glm::dot(movingEdgeA, targetU)) +
                                    std::abs(glm::dot(movingEdgeB, targetU));
                                const float movingRadiusV = std::abs(glm::dot(movingEdgeA, targetV)) +
                                    std::abs(glm::dot(movingEdgeB, targetV));

                                const glm::vec3 between = targetFaceCenter - movingFaceCenter;
                                const float normalGap = glm::dot(between, movingNormal);
                                if (std::abs(normalGap) > faceThreshold)
                                    continue;
                                const float lateralU = glm::dot(between, targetU);
                                const float lateralV = glm::dot(between, targetV);
                                const float excessU = std::max(0.0f,
                                    std::abs(lateralU) - targetHalfU - movingRadiusU);
                                const float excessV = std::max(0.0f,
                                    std::abs(lateralV) - targetHalfV - movingRadiusV);
                                if (excessU > lateralTolerance || excessV > lateralTolerance)
                                    continue;

                                const glm::vec3 correction = movingNormal * normalGap;
                                const float distance = std::abs(normalGap);
                                const glm::vec2 extents(std::min(targetHalfU, movingRadiusU),
                                                        std::min(targetHalfV, movingRadiusV));

                                considerCandidate(movingAxis, 0, distance, correction, 0, -1,
                                                  movingFaceCenter + correction, targetFaceCenter,
                                                  movingNormal, targetU, targetV, extents);
                            }
                        }
                    }
                }
            }
        }

        // Combina as correcoes dos eixos ativos (multi-docking simultaneo)
        glm::vec3 bestCorrection(0.0f);
        bool hasAnyDock = false;

        for (int slot = 0; slot < 3; ++slot)
        {
            if (!bestSlotCandidates[static_cast<size_t>(slot)].valid)
                continue;

            const auto& cand = bestSlotCandidates[static_cast<size_t>(slot)];

            if (constrainedAxis)
            {
                const glm::vec3 axisConstraint = glm::normalize(*constrainedAxis);
                const float corrLen = glm::length(cand.correction);
                if (corrLen > 0.0001f)
                {
                    const float axisAlignment = std::abs(glm::dot(cand.correction / corrLen, axisConstraint));
                    // Se o usuario esta arrastando em um unico eixo restrito, so aplica outro eixo
                    // se ja estiver em contato previo (tolerancia minima para manter o objeto ancorado)
                    if (axisAlignment < 0.7f && cand.distance > lateralTolerance)
                        continue;
                }
            }

            bestCorrection += cand.correction;
            hasAnyDock = true;

            ActiveDockVisual visual;
            visual.snapKind = cand.snapKind;
            visual.alignedAxis = cand.alignedAxis;
            visual.faceCenter = cand.faceCenter + cand.correction;
            visual.targetCenter = cand.targetCenter;
            visual.faceNormal = cand.faceNormal;
            visual.faceAxisU = cand.faceAxisU;
            visual.faceAxisV = cand.faceAxisV;
            visual.faceExtents = cand.faceExtents;
            s_ActiveDocks.push_back(visual);
        }

        if (!hasAnyDock || s_ActiveDocks.empty())
            return false;

        glm::vec3 localCorrection = bestCorrection;
        const uint64_t parentID = moving.GetParentID();
        Entity* parent = parentID != 0 ? scene.FindEntityByID(parentID) : nullptr;
        size_t parentGuard = 0;
        while (parent && parent->IsFolder && parentGuard++ <= entities.size())
            parent = parent->GetParentID() != 0
                ? scene.FindEntityByID(parent->GetParentID()) : nullptr;
        if (parent)
        {
            const glm::mat3 parentLinear(parent->GetTransform().WorldMatrix);
            const float determinant = glm::determinant(parentLinear);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
                return false;
            localCorrection = glm::inverse(parentLinear) * bestCorrection;
        }

        Transform& transform = moving.GetTransform();
        transform.Position.X += localCorrection.x;
        transform.Position.Y += localCorrection.y;
        transform.Position.Z += localCorrection.z;
        s_FaceDockingActive = true;

        // Mantem compatibilidade com variaveis legadas usando o dock primario
        const auto& primary = s_ActiveDocks.front();
        s_DockAxisAlignment = primary.snapKind == 1;
        s_DockSnapKind = primary.snapKind;
        s_DockAlignedAxis = primary.alignedAxis;
        s_DockTargetCenter = primary.targetCenter;
        s_DockFaceCenter = primary.faceCenter;
        s_DockFaceNormal = primary.faceNormal;
        s_DockFaceAxisU = primary.faceAxisU;
        s_DockFaceAxisV = primary.faceAxisV;
        s_DockFaceExtents = glm::max(primary.faceExtents, glm::vec2(0.05f));
        return true;
    }

    float PointSegmentDistanceSquared(const glm::vec3& point,
                                      const glm::vec3& start,
                                      const glm::vec3& end)
    {
        const glm::vec3 direction = end - start;
        const float lengthSquared = glm::dot(direction, direction);
        const float factor = lengthSquared > 0.000001f
            ? glm::clamp(glm::dot(point - start, direction) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        const glm::vec3 closest = start + direction * factor;
        return glm::dot(point - closest, point - closest);
    }

    int FindModelingComponent(const MeshTopology& topology,
                              int mode, int triangleIndex,
                              const glm::vec3& localPosition)
    {
        if (triangleIndex < 0 || triangleIndex >= static_cast<int>(topology.Faces.size()))
            return -1;
        const auto& face = topology.Faces[static_cast<size_t>(triangleIndex)];
        if (mode == 2)
            return triangleIndex;

        if (mode == 0)
        {
            int result = static_cast<int>(face.Vertices[0]);
            float best = std::numeric_limits<float>::max();
            for (uint32_t vertex : face.Vertices)
            {
                const glm::vec3 position(
                    topology.Vertices[vertex].Value.X,
                    topology.Vertices[vertex].Value.Y,
                    topology.Vertices[vertex].Value.Z);
                const float distance = glm::dot(position - localPosition,
                                                position - localPosition);
                if (distance < best)
                {
                    best = distance;
                    result = static_cast<int>(vertex);
                }
            }
            return result;
        }

        const auto edges = topology.GetEdges();
        int result = -1;
        float best = std::numeric_limits<float>::max();
        for (size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
        {
            const auto& edge = edges[edgeIndex];
            if (std::find(edge.Faces.begin(), edge.Faces.end(),
                          static_cast<uint32_t>(triangleIndex)) == edge.Faces.end())
                continue;

            if (InspectorPanel::IsHideDiagonalLines() &&
                InspectorPanel::GetMiniFaceShape() != InspectorPanel::MiniFaceShape::Triangles &&
                edge.Faces.size() == 2)
            {
                uint32_t f0 = edge.Faces[0];
                uint32_t f1 = edge.Faces[1];
                const auto& fa = topology.Faces[f0];
                const auto& fb = topology.Faces[f1];
                const auto& va0 = topology.Vertices[fa.Vertices[0]].Value;
                const auto& va1 = topology.Vertices[fa.Vertices[1]].Value;
                const auto& va2 = topology.Vertices[fa.Vertices[2]].Value;
                const auto& vb0 = topology.Vertices[fb.Vertices[0]].Value;
                const auto& vb1 = topology.Vertices[fb.Vertices[1]].Value;
                const auto& vb2 = topology.Vertices[fb.Vertices[2]].Value;
                glm::vec3 nA = glm::cross(
                    glm::vec3(va1.X - va0.X, va1.Y - va0.Y, va1.Z - va0.Z),
                    glm::vec3(va2.X - va0.X, va2.Y - va0.Y, va2.Z - va0.Z));
                glm::vec3 nB = glm::cross(
                    glm::vec3(vb1.X - vb0.X, vb1.Y - vb0.Y, vb1.Z - vb0.Z),
                    glm::vec3(vb2.X - vb0.X, vb2.Y - vb0.Y, vb2.Z - vb0.Z));
                float lA = glm::length(nA);
                float lB = glm::length(nB);
                if (lA > 1e-6f && lB > 1e-6f && glm::dot(nA / lA, nB / lB) > 0.999f)
                    continue;
            }
            const glm::vec3 start(
                topology.Vertices[edge.A].Value.X,
                topology.Vertices[edge.A].Value.Y,
                topology.Vertices[edge.A].Value.Z);
            const glm::vec3 end(
                topology.Vertices[edge.B].Value.X,
                topology.Vertices[edge.B].Value.Y,
                topology.Vertices[edge.B].Value.Z);
            const float distance = PointSegmentDistanceSquared(localPosition, start, end);
            if (distance < best)
            {
                best = distance;
                result = static_cast<int>(edgeIndex);
            }
        }
        return result;
    }

    struct CachedTopologyEntry
    {
        uint64_t EntityID = 0;
        size_t VertexCount = 0;
        MeshTopology Topology;
        std::vector<MeshTopology::Edge> Edges;
    };
    static CachedTopologyEntry s_CachedModelingTopology;

    const CachedTopologyEntry& GetOrUpdateModelingTopology(const Entity& entity)
    {
        const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid())
        {
            s_CachedModelingTopology.EntityID = 0;
            return s_CachedModelingTopology;
        }

        const auto& vertices = mesh->GetVertices();
        const size_t vCount = vertices.size();
        glm::vec3 firstV(0.0f);
        glm::vec3 midV(0.0f);
        if (vCount > 0) firstV = glm::vec3(vertices[0].X, vertices[0].Y, vertices[0].Z);
        if (vCount > 1) midV = glm::vec3(vertices[vCount / 2].X, vertices[vCount / 2].Y, vertices[vCount / 2].Z);

        static glm::vec3 s_LastFirstV(0.0f);
        static glm::vec3 s_LastMidV(0.0f);

        if (s_CachedModelingTopology.EntityID == entity.GetID() &&
            s_CachedModelingTopology.VertexCount == vCount &&
            firstV == s_LastFirstV &&
            midV == s_LastMidV)
        {
            return s_CachedModelingTopology;
        }

        s_CachedModelingTopology.EntityID = entity.GetID();
        s_CachedModelingTopology.VertexCount = vCount;
        s_LastFirstV = firstV;
        s_LastMidV = midV;
        s_CachedModelingTopology.Topology = MeshTopology::FromVertices(vertices);
        s_CachedModelingTopology.Edges = s_CachedModelingTopology.Topology.GetEdges();
        return s_CachedModelingTopology;
    }

    void DrawModelingOverlay(const Entity& entity,
                             const std::vector<int>& selection,
                             int selectionMode,
                             const glm::mat4& view,
                             const glm::mat4& projection,
                             const ImVec2& imageMin,
                             const ImVec2& imageMax,
                             ImDrawList* drawList)
    {
        const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid()) return;

        const auto& cached = GetOrUpdateModelingTopology(entity);
        const auto& topology = cached.Topology;
        const auto& edges = cached.Edges;
        if (topology.Faces.empty()) return;

        const glm::mat4 model = GetEntityWorldMatrix(entity);
        const glm::mat4 mvp = projection * view * model;
        const glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);
        const float screenW = imageMax.x - imageMin.x;
        const float screenH = imageMax.y - imageMin.y;

        // O(1) set lookup instead of linear scans
        std::unordered_set<int> selectedSet(selection.begin(), selection.end());

        auto projectFast = [&](const glm::vec3& local, ImVec2& outScreen) -> bool
        {
            const glm::vec4 clip = mvp * glm::vec4(local, 1.0f);
            if (clip.w <= 0.001f) return false;
            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            const float ndcZ = clip.z * invW;
            if (ndcZ < -1.1f || ndcZ > 1.1f) return false;
            if (ndcX < -1.15f || ndcX > 1.15f || ndcY < -1.15f || ndcY > 1.15f) return false;
            outScreen.x = imageMin.x + (ndcX * 0.5f + 0.5f) * screenW;
            outScreen.y = imageMin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * screenH;
            return true;
        };

                const bool hideDiags = InspectorPanel::IsHideDiagonalLines();
        const auto miniFaceShape = InspectorPanel::GetMiniFaceShape();
        const auto miniFaceOrient = InspectorPanel::GetMiniFaceOrientation();
        int vLines = InspectorPanel::GetMiniFaceVerticalLines();
        int hLines = InspectorPanel::GetMiniFaceHorizontalLines();
        if (miniFaceOrient == InspectorPanel::MiniFaceOrientation::Horizontal) vLines = 0;
        if (miniFaceOrient == InspectorPanel::MiniFaceOrientation::Vertical) hLines = 0;
        if (miniFaceShape == InspectorPanel::MiniFaceShape::Squares && miniFaceOrient == InspectorPanel::MiniFaceOrientation::Grid)
        {
            vLines = std::max(vLines, hLines);
            hLines = vLines;
        }

        // Pré-calcular normais das faces
        const size_t faceCount = topology.Faces.size();
        std::vector<glm::vec3> faceNormals(faceCount, glm::vec3(0.0f, 1.0f, 0.0f));
        for (size_t fIdx = 0; fIdx < faceCount; ++fIdx)
        {
            const auto& f = topology.Faces[fIdx];
            const auto& va = topology.Vertices[f.Vertices[0]].Value;
            const auto& vb = topology.Vertices[f.Vertices[1]].Value;
            const auto& vc = topology.Vertices[f.Vertices[2]].Value;
            glm::vec3 crossVal = glm::cross(
                glm::vec3(vb.X - va.X, vb.Y - va.Y, vb.Z - va.Z),
                glm::vec3(vc.X - va.X, vc.Y - va.Y, vc.Z - va.Z));
            float len = glm::length(crossVal);
            if (len > 1e-6f) faceNormals[fIdx] = crossVal / len;
        }

        // 1. MODO FACES (selectionMode == 2)
        if (selectionMode == 2)
        {
            struct QuadMesh
            {
                uint32_t Face0 = 0;
                uint32_t Face1 = 0;
                uint32_t V00 = 0;
                uint32_t V10 = 0;
                uint32_t V11 = 0;
                uint32_t V01 = 0;
                glm::vec3 Normal{0.0f, 0.0f, 1.0f};
            };

            std::vector<QuadMesh> quads;
            std::vector<bool> facePaired(faceCount, false);
            quads.reserve(faceCount / 2);

            for (const auto& edge : edges)
            {
                if (edge.Faces.size() == 2)
                {
                    uint32_t f0 = edge.Faces[0];
                    uint32_t f1 = edge.Faces[1];
                    if (!facePaired[f0] && !facePaired[f1])
                    {
                        if (glm::dot(faceNormals[f0], faceNormals[f1]) > 0.999f)
                        {
                            uint32_t ea = edge.A;
                            uint32_t eb = edge.B;
                            const auto& face0 = topology.Faces[f0];
                            const auto& face1 = topology.Faces[f1];
                            uint32_t opp0 = 0;
                            for (uint32_t v : face0.Vertices) { if (v != ea && v != eb) { opp0 = v; break; } }
                            uint32_t opp1 = 0;
                            for (uint32_t v : face1.Vertices) { if (v != ea && v != eb) { opp1 = v; break; } }
                            bool uThenVIn0 = false;
                            for (int k = 0; k < 3; ++k)
                            {
                                if (face0.Vertices[k] == ea && face0.Vertices[(k + 1) % 3] == eb)
                                {
                                    uThenVIn0 = true;
                                    break;
                                }
                            }

                            QuadMesh qm;
                            qm.Face0 = f0;
                            qm.Face1 = f1;
                            if (uThenVIn0)
                            {
                                qm.V00 = ea;
                                qm.V10 = opp1;
                                qm.V11 = eb;
                                qm.V01 = opp0;
                            }
                            else
                            {
                                qm.V00 = eb;
                                qm.V10 = opp0;
                                qm.V11 = ea;
                                qm.V01 = opp1;
                            }
                            qm.Normal = faceNormals[f0];
                            quads.push_back(qm);
                            facePaired[f0] = true;
                            facePaired[f1] = true;
                        }
                    }
                }
            }

            for (const auto& quad : quads)
            {
                const auto& v00 = topology.Vertices[quad.V00].Value;
                const auto& v10 = topology.Vertices[quad.V10].Value;
                const auto& v11 = topology.Vertices[quad.V11].Value;
                const auto& v01 = topology.Vertices[quad.V01].Value;
                glm::vec3 p00(v00.X, v00.Y, v00.Z);
                glm::vec3 p10(v10.X, v10.Y, v10.Z);
                glm::vec3 p11(v11.X, v11.Y, v11.Z);
                glm::vec3 p01(v01.X, v01.Y, v01.Z);
                glm::vec3 worldCenter = glm::vec3(model * glm::vec4((p00 + p10 + p11 + p01) * 0.25f, 1.0f));
                glm::vec3 worldNormal = glm::mat3(model) * quad.Normal;
                const bool isFrontFacing = glm::dot(worldNormal, cameraPos - worldCenter) > 0.0f;
                const bool selected = (selectedSet.find(static_cast<int>(quad.Face0)) != selectedSet.end()) ||
                                      (selectedSet.find(static_cast<int>(quad.Face1)) != selectedSet.end());
                if (!isFrontFacing && !selected) continue;
                ImVec2 s00, s10, s11, s01;
                if (!projectFast(p00, s00) || !projectFast(p10, s10) ||
                    !projectFast(p11, s11) || !projectFast(p01, s01)) continue;
                if (selected)
                {
                    drawList->AddTriangleFilled(s00, s10, s11, IM_COL32(255, 180, 40, 75));
                    drawList->AddTriangleFilled(s00, s11, s01, IM_COL32(255, 180, 40, 75));
                    drawList->AddLine(s00, s10, IM_COL32(255, 195, 45, 255), 2.0f);
                    drawList->AddLine(s10, s11, IM_COL32(255, 195, 45, 255), 2.0f);
                    drawList->AddLine(s11, s01, IM_COL32(255, 195, 45, 255), 2.0f);
                    drawList->AddLine(s01, s00, IM_COL32(255, 195, 45, 255), 2.0f);
                    if (vLines > 0 || hLines > 0)
                    {
                        const int cols = vLines + 1;
                        const int rows = hLines + 1;
                        for (int i = 1; i <= vLines; ++i)
                        {
                            float u = static_cast<float>(i) / static_cast<float>(cols);
                            glm::vec3 bot = glm::mix(p00, p10, u);
                            glm::vec3 top = glm::mix(p01, p11, u);
                            ImVec2 sBot, sTop;
                            if (projectFast(bot, sBot) && projectFast(top, sTop))
                                drawList->AddLine(sBot, sTop, IM_COL32(255, 195, 45, 255), 1.5f);
                        }
                        for (int j = 1; j <= hLines; ++j)
                        {
                            float v = static_cast<float>(j) / static_cast<float>(rows);
                            glm::vec3 left = glm::mix(p00, p01, v);
                            glm::vec3 right = glm::mix(p10, p11, v);
                            ImVec2 sLeft, sRight;
                            if (projectFast(left, sLeft) && projectFast(right, sRight))
                                drawList->AddLine(sLeft, sRight, IM_COL32(255, 195, 45, 255), 1.5f);
                        }
                        if (miniFaceShape == InspectorPanel::MiniFaceShape::Triangles)
                        {
                            for (int j = 0; j < rows; ++j)
                            {
                                float vA = static_cast<float>(j) / static_cast<float>(rows);
                                float vB = static_cast<float>(j + 1) / static_cast<float>(rows);
                                for (int i = 0; i < cols; ++i)
                                {
                                    float uA = static_cast<float>(i) / static_cast<float>(cols);
                                    float uB = static_cast<float>(i + 1) / static_cast<float>(cols);
                                    auto bilerp = [&](float u, float v) {
                                        return (1.0f - u) * (1.0f - v) * p00 + u * (1.0f - v) * p10 + u * v * p11 + (1.0f - u) * v * p01;
                                    };
                                    ImVec2 sc0, sc1;
                                    if (projectFast(bilerp(uA, vA), sc0) && projectFast(bilerp(uB, vB), sc1))
                                        drawList->AddLine(sc0, sc1, IM_COL32(255, 195, 45, 255), 1.2f);
                                }
                            }
                        }
                    }
                    else
                    {
                        if (miniFaceShape == InspectorPanel::MiniFaceShape::Triangles || !hideDiags)
                            drawList->AddLine(s00, s11, IM_COL32(255, 195, 45, 255), 2.0f);
                    }
                }
                else
                {
                    drawList->AddLine(s00, s10, IM_COL32(135, 185, 235, 80), 1.0f);
                    drawList->AddLine(s10, s11, IM_COL32(135, 185, 235, 80), 1.0f);
                    drawList->AddLine(s11, s01, IM_COL32(135, 185, 235, 80), 1.0f);
                    drawList->AddLine(s01, s00, IM_COL32(135, 185, 235, 80), 1.0f);
                    if (miniFaceShape == InspectorPanel::MiniFaceShape::Triangles || !hideDiags)
                        drawList->AddLine(s00, s11, IM_COL32(135, 185, 235, 80), 1.0f);
                }
            }

            for (size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
            {
                if (facePaired[faceIndex]) continue;
                const auto& face = topology.Faces[faceIndex];
                const auto& v0 = topology.Vertices[face.Vertices[0]].Value;
                const auto& v1 = topology.Vertices[face.Vertices[1]].Value;
                const auto& v2 = topology.Vertices[face.Vertices[2]].Value;
                glm::vec3 p0(v0.X, v0.Y, v0.Z);
                glm::vec3 p1(v1.X, v1.Y, v1.Z);
                glm::vec3 p2(v2.X, v2.Y, v2.Z);
                glm::vec3 normal = faceNormals[faceIndex];
                glm::vec3 worldCenter = glm::vec3(model * glm::vec4((p0 + p1 + p2) * 0.333333f, 1.0f));
                glm::vec3 worldNormal = glm::mat3(model) * normal;
                const bool isFrontFacing = glm::dot(worldNormal, cameraPos - worldCenter) > 0.0f;
                const bool selected = selectedSet.find(static_cast<int>(faceIndex)) != selectedSet.end();
                if (!isFrontFacing && !selected) continue;
                ImVec2 pts[3];
                if (!projectFast(p0, pts[0]) || !projectFast(p1, pts[1]) || !projectFast(p2, pts[2])) continue;
                if (selected)
                {
                    drawList->AddTriangleFilled(pts[0], pts[1], pts[2], IM_COL32(255, 180, 40, 75));
                    drawList->AddTriangle(pts[0], pts[1], pts[2], IM_COL32(255, 195, 45, 255), 2.0f);
                }
                else
                {
                    drawList->AddTriangle(pts[0], pts[1], pts[2], IM_COL32(135, 185, 235, 80), 1.0f);
                }
            }
        }
        // 2. MODO ARESTAS (selectionMode == 1)
        else if (selectionMode == 1)
        {
            std::unordered_set<size_t> internalDiagonals;
            if (hideDiags && miniFaceShape != InspectorPanel::MiniFaceShape::Triangles)
            {
                for (size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
                {
                    const auto& edge = edges[edgeIndex];
                    if (edge.Faces.size() == 2)
                    {
                        uint32_t f0 = edge.Faces[0];
                        uint32_t f1 = edge.Faces[1];
                        if (glm::dot(faceNormals[f0], faceNormals[f1]) > 0.999f)
                            internalDiagonals.insert(edgeIndex);
                    }
                }
            }

            const size_t edgeCount = edges.size();
            for (size_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex)
            {
                const bool selected = selectedSet.find(static_cast<int>(edgeIndex)) != selectedSet.end();
                if (!selected && internalDiagonals.find(edgeIndex) != internalDiagonals.end()) continue;
                const auto& edge = edges[edgeIndex];
                const auto& a = topology.Vertices[edge.A].Value;
                const auto& b = topology.Vertices[edge.B].Value;
                ImVec2 start, end;
                if (!projectFast(glm::vec3(a.X, a.Y, a.Z), start) || !projectFast(glm::vec3(b.X, b.Y, b.Z), end)) continue;
                if (selected)
                    drawList->AddLine(start, end, IM_COL32(255, 195, 45, 255), 3.0f);
                else
                    drawList->AddLine(start, end, IM_COL32(110, 185, 255, 120), 1.0f);
            }
        }
        // 3. MODO VÉRTICES (selectionMode == 0)
        else if (selectionMode == 0)
        {
            std::unordered_set<size_t> internalDiagonals;
            if (hideDiags && miniFaceShape != InspectorPanel::MiniFaceShape::Triangles)
            {
                for (size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
                {
                    const auto& edge = edges[edgeIndex];
                    if (edge.Faces.size() == 2 && glm::dot(faceNormals[edge.Faces[0]], faceNormals[edge.Faces[1]]) > 0.999f)
                    {
                        internalDiagonals.insert(edgeIndex);
                    }
                }
            }

            const size_t edgeCount = edges.size();
            for (size_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex)
            {
                if (internalDiagonals.find(edgeIndex) != internalDiagonals.end()) continue;
                const auto& edge = edges[edgeIndex];
                const auto& a = topology.Vertices[edge.A].Value;
                const auto& b = topology.Vertices[edge.B].Value;
                ImVec2 start, end;
                if (projectFast(glm::vec3(a.X, a.Y, a.Z), start) && projectFast(glm::vec3(b.X, b.Y, b.Z), end))
                {
                    drawList->AddLine(start, end, IM_COL32(100, 160, 230, 70), 1.0f);
                }
            }

            const size_t vertCount = topology.Vertices.size();
            for (size_t vertIndex = 0; vertIndex < vertCount; ++vertIndex)
            {
                const auto& v = topology.Vertices[vertIndex].Value;
                ImVec2 pt;
                if (!projectFast(glm::vec3(v.X, v.Y, v.Z), pt))
                    continue;

                const bool selected = selectedSet.find(static_cast<int>(vertIndex)) != selectedSet.end();
                if (selected)
                {
                    drawList->AddCircleFilled(pt, 5.0f, IM_COL32(255, 195, 45, 255), 8);
                    drawList->AddCircle(pt, 7.5f, IM_COL32(255, 230, 100, 200), 8, 1.5f);
                }
                else
                {
                    drawList->AddRectFilled(ImVec2(pt.x - 1.5f, pt.y - 1.5f), ImVec2(pt.x + 1.5f, pt.y + 1.5f), IM_COL32(220, 235, 255, 180));
                }
            }
        }
    }

    void DrawLightRangeOverlay(const Entity& entity,
                                const glm::mat4& view,
                                const glm::mat4& projection,
                                const ImVec2& imageMin,
                                const ImVec2& imageMax,
                                ImDrawList* drawList)
    {
        if (!entity.HasLight())
            return;

        const LightComponent& light = const_cast<Entity&>(entity).GetLightComponent();
        if (!light.Enabled)
            return;

        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::mat4 model = transform.WorldMatrix;
        const glm::vec3 position = glm::vec3(model[3]);
        const glm::mat4 identity(1.0f);
        const ImU32 color = IM_COL32(255, 190, 55, 225);
        const ImU32 faintColor = IM_COL32(255, 190, 55, 110);
        constexpr int segments = 32;

        auto projectWorld = [&](const glm::vec3& world, ImVec2& screen) -> bool
        {
            return ProjectPoint(view, projection, identity, world,
                                imageMin, imageMax, screen);
        };

        auto drawRing = [&](const glm::vec3& center, float radius, const glm::vec3& axisA, const glm::vec3& axisB)
        {
            ImVec2 previous{};
            bool hasPrevious = false;
            for (int i = 0; i <= segments; ++i)
            {
                const float angle = glm::two_pi<float>() *
                    static_cast<float>(i) / static_cast<float>(segments);
                const glm::vec3 world = center + radius *
                    (std::cos(angle) * axisA + std::sin(angle) * axisB);
                ImVec2 current{};
                const bool visible = projectWorld(world, current);
                if (visible && hasPrevious)
                    drawList->AddLine(previous, current, faintColor, 1.5f);
                if (visible)
                {
                    previous = current;
                    hasPrevious = true;
                }
                else
                {
                    hasPrevious = false;
                }
            }
        };

        if (light.Type == LightType::Point || light.Type == LightType::Area)
        {
            drawRing(position, light.Range, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            drawRing(position, light.Range, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            drawRing(position, light.Range, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

            if (light.Type == LightType::Area && light.SourceRadius > 0.01f)
            {
                drawRing(position, light.SourceRadius, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                drawRing(position, light.SourceRadius, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            }
        }
        else if (light.Type == LightType::Spot)
        {
            glm::vec3 localDirection(
                light.DirectionLocal[0], light.DirectionLocal[1], light.DirectionLocal[2]);
            if (glm::length(localDirection) < 0.001f)
                localDirection = glm::vec3(0.0f, -1.0f, 0.0f);

            glm::vec3 direction = glm::vec3(model * glm::vec4(localDirection, 0.0f));
            direction = glm::length(direction) > 0.001f
                ? glm::normalize(direction)
                : glm::vec3(0.0f, -1.0f, 0.0f);

            const float outerHalfAngle = glm::radians(
                std::clamp(light.OuterAngleDeg * 0.5f, 0.5f, 44.5f));
            const float baseRadius = std::tan(outerHalfAngle) * light.Range;
            const glm::vec3 endPosition = position + direction * light.Range;
            const glm::vec3 referenceUp = std::abs(direction.y) < 0.95f
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 axisA = glm::normalize(glm::cross(direction, referenceUp));
            const glm::vec3 axisB = glm::normalize(glm::cross(direction, axisA));

            ImVec2 apexScreen{};
            projectWorld(position, apexScreen);
            ImVec2 previous{};
            bool hasPrevious = false;
            for (int i = 0; i <= segments; ++i)
            {
                const float angle = glm::two_pi<float>() *
                    static_cast<float>(i) / static_cast<float>(segments);
                const glm::vec3 world = endPosition + baseRadius *
                    (std::cos(angle) * axisA + std::sin(angle) * axisB);
                ImVec2 current{};
                const bool visible = projectWorld(world, current);
                if (visible && hasPrevious)
                    drawList->AddLine(previous, current, faintColor, 1.5f);
                if (visible && (i % (segments / 8) == 0) && projectWorld(position, apexScreen))
                    drawList->AddLine(apexScreen, current, faintColor, 1.0f);
                if (visible)
                {
                    previous = current;
                    hasPrevious = true;
                }
                else
                {
                    hasPrevious = false;
                }
            }
        }
        else if (light.Type == LightType::Directional)
        {
            glm::vec3 localDirection(
                light.DirectionLocal[0], light.DirectionLocal[1], light.DirectionLocal[2]);
            if (glm::length(localDirection) < 0.001f)
                localDirection = glm::vec3(0.0f, -1.0f, 0.0f);
            glm::vec3 direction = glm::vec3(model * glm::vec4(localDirection, 0.0f));
            direction = glm::length(direction) > 0.001f
                ? glm::normalize(direction)
                : glm::vec3(0.0f, -1.0f, 0.0f);
            ImVec2 start{}, end{};
            if (projectWorld(position, start) &&
                projectWorld(position + direction * 3.0f, end))
            {
                drawList->AddLine(start, end, color, 2.0f);
                drawList->AddCircleFilled(end, 4.0f, color, 8);
            }
        }

        ImVec2 centerScreen{};
        if (projectWorld(position, centerScreen))
        {
            drawList->AddCircleFilled(centerScreen, 4.0f, color, 10);
            const std::string label = light.Type == LightType::Directional
                ? "Direcional"
                : "Fim: " + std::to_string(static_cast<int>(std::round(light.Range))) + " m";
            drawList->AddText(ImVec2(centerScreen.x + 8.0f, centerScreen.y - 8.0f), color, label.c_str());
        }
    }

    void DrawCameraOverlay(const Entity& entity,
                           bool selected,
                           const glm::mat4& view,
                           const glm::mat4& projection,
                           const ImVec2& imageMin,
                           const ImVec2& imageMax,
                           ImDrawList* drawList)
    {
        if (!entity.HasCamera() || !entity.Active)
            return;

        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        glm::mat4 model = transform.WorldMatrix;
        if (entity.GetParentID() == 0)
            model = BuildModel(transform);

        const float scale = std::max({
            std::abs(transform.Scale.X),
            std::abs(transform.Scale.Y),
            std::abs(transform.Scale.Z),
            0.001f
        });
        const float iconScale = std::max(0.45f, scale * 0.70f);
        const ImU32 outline = selected
            ? IM_COL32(255, 210, 55, 255)
            : IM_COL32(95, 200, 255, 235);
        const ImU32 secondary = selected
            ? IM_COL32(255, 175, 35, 170)
            : IM_COL32(80, 175, 245, 145);
        const ImU32 fill = selected
            ? IM_COL32(255, 190, 45, 55)
            : IM_COL32(60, 165, 235, 38);

        auto worldPoint = [&](const glm::vec3& local)
        {
            return glm::vec3(model * glm::vec4(local * iconScale, 1.0f));
        };
        auto project = [&](const glm::vec3& world, ImVec2& screen) -> bool
        {
            return ProjectPoint(view, projection, glm::mat4(1.0f), world,
                                imageMin, imageMax, screen);
        };
        auto line = [&](const glm::vec3& a, const glm::vec3& b,
                        ImU32 color, float thickness = 2.0f)
        {
            ImVec2 screenA{}, screenB{};
            if (project(a, screenA) && project(b, screenB))
                drawList->AddLine(screenA, screenB, color, thickness);
        };

        // Corpo da câmera: um retângulo 2D com pequena saliência superior,
        // orientado pelo Transform da entidade e sem volume sólido.
        const glm::vec3 front[4] = {
            worldPoint({ -0.48f, -0.28f, -0.18f }),
            worldPoint({  0.48f, -0.28f, -0.18f }),
            worldPoint({  0.48f,  0.28f, -0.18f }),
            worldPoint({ -0.48f,  0.28f, -0.18f })
        };
        ImVec2 body[4]{};
        bool bodyVisible = true;
        for (int i = 0; i < 4; ++i)
            bodyVisible = project(front[i], body[i]) && bodyVisible;
        if (bodyVisible)
        {
            drawList->AddConvexPolyFilled(body, 4, fill);
            for (int i = 0; i < 4; ++i)
                drawList->AddLine(body[i], body[(i + 1) % 4], outline, 2.5f);
        }

        const glm::vec3 rear[4] = {
            worldPoint({ -0.48f, -0.28f,  0.18f }),
            worldPoint({  0.48f, -0.28f,  0.18f }),
            worldPoint({  0.48f,  0.28f,  0.18f }),
            worldPoint({ -0.48f,  0.28f,  0.18f })
        };
        for (int i = 0; i < 4; ++i)
        {
            line(front[i], rear[i], secondary, 1.5f);
            line(rear[i], rear[(i + 1) % 4], secondary, 1.5f);
        }

        const glm::vec3 top[4] = {
            worldPoint({ -0.22f, 0.28f, -0.18f }),
            worldPoint({  0.22f, 0.28f, -0.18f }),
            worldPoint({  0.16f, 0.43f, -0.18f }),
            worldPoint({ -0.16f, 0.43f, -0.18f })
        };
        for (int i = 0; i < 4; ++i)
            line(top[i], top[(i + 1) % 4], outline, 2.0f);

        // Lente circular na frente da câmera.
        ImVec2 previous{};
        bool hasPrevious = false;
        for (int i = 0; i <= 16; ++i)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / 16.0f;
            const glm::vec3 lens = worldPoint({
                std::cos(angle) * 0.16f,
                std::sin(angle) * 0.16f,
                -0.22f
            });
            ImVec2 current{};
            if (project(lens, current) && hasPrevious)
                drawList->AddLine(previous, current, outline, 2.0f);
            if (project(lens, current))
            {
                previous = current;
                hasPrevious = true;
            }
        }

        // Linhas discretas do enquadramento mostram para onde a câmera aponta.
        // A janela do frustum usa a proporção e a projeção configuradas na
        // própria câmera, em vez de uma caixa fixa que podia sugerir outro
        // enquadramento.
        const CameraComponent& cameraSettings = entity.GetCameraComponent();
        const float aspect = std::max(0.1f,
            (imageMax.x - imageMin.x) / std::max(1.0f, imageMax.y - imageMin.y));
        constexpr float frustumDepth = 1.55f;
        const float halfHeight = cameraSettings.Orthographic
            ? std::max(0.05f, cameraSettings.OrthographicSize * 0.5f)
            : std::tan(glm::radians(std::clamp(cameraSettings.FieldOfView, 1.0f, 179.0f)) * 0.5f)
                * frustumDepth;
        const float halfWidth = cameraSettings.Orthographic
            ? halfHeight * aspect
            : halfHeight * aspect;
        const glm::vec3 viewPlane[4] = {
            worldPoint({ -halfWidth, -halfHeight, -frustumDepth }),
            worldPoint({  halfWidth, -halfHeight, -frustumDepth }),
            worldPoint({  halfWidth,  halfHeight, -frustumDepth }),
            worldPoint({ -halfWidth,  halfHeight, -frustumDepth })
        };
        for (int i = 0; i < 4; ++i)
        {
            line(front[i], viewPlane[i], secondary, 1.0f);
            line(viewPlane[i], viewPlane[(i + 1) % 4], secondary, 1.0f);
        }

        ImVec2 labelPoint{};
        if (project(worldPoint({ 0.0f, 0.60f, 0.0f }), labelPoint))
            drawList->AddText(ImVec2(labelPoint.x + 6.0f, labelPoint.y - 8.0f),
                              outline, entity.GetName().c_str());
    }

    void DrawHoverOutline(const Entity& entity,
                          const glm::mat4& view,
                          const glm::mat4& projection,
                          const ImVec2& imageMin,
                          const ImVec2& imageMax,
                          ImDrawList* drawList)
    {
        const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid() || entity.HasUI() || entity.IsFolder ||
            entity.IsGlobalLight() || entity.IsGlobalScript())
            return;

        const glm::vec3 center = mesh->GetBoundingCenter();
        const glm::vec3 half = mesh->GetBoundingHalfExtents();
        const glm::mat4 model = GetEntityWorldMatrix(entity);
        const glm::vec3 local[8] = {
            center + glm::vec3(-half.x, -half.y, -half.z),
            center + glm::vec3( half.x, -half.y, -half.z),
            center + glm::vec3( half.x, -half.y,  half.z),
            center + glm::vec3(-half.x, -half.y,  half.z),
            center + glm::vec3(-half.x,  half.y, -half.z),
            center + glm::vec3( half.x,  half.y, -half.z),
            center + glm::vec3( half.x,  half.y,  half.z),
            center + glm::vec3(-half.x,  half.y,  half.z)
        };
        const int edges[24] = {
            0, 1, 1, 2, 2, 3, 3, 0,
            4, 5, 5, 6, 6, 7, 7, 4,
            0, 4, 1, 5, 2, 6, 3, 7
        };
        ImVec2 screen[8]{};
        bool visible[8]{};
        for (int i = 0; i < 8; ++i)
            visible[i] = ProjectPoint(view, projection, model, local[i],
                                      imageMin, imageMax, screen[i]);

        constexpr ImU32 hoverColor = IM_COL32(255, 255, 255, 245);
        for (int i = 0; i < 24; i += 2)
        {
            const int a = edges[i];
            const int b = edges[i + 1];
            if (visible[a] && visible[b])
                drawList->AddLine(screen[a], screen[b], hoverColor, 2.0f);
        }
    }

    void Draw2DWorldGrid(const Camera& camera,
                         const glm::mat4& view,
                         const glm::mat4& projection,
                         const ImVec2& imageMin,
                         const ImVec2& imageMax,
                         ImDrawList* drawList)
    {
        const float aspect = (imageMax.x - imageMin.x) /
            std::max(1.0f, imageMax.y - imageMin.y);
        const float halfHeight = std::max(0.1f, camera.OrthographicSize) * 0.5f;
        const float halfWidth = halfHeight * aspect;
        const float centerX = camera.Position.X;
        const float centerY = camera.Position.Y;
        // Mantém aproximadamente 12-24 linhas menores visíveis. Sem isso, um
        // zoom afastado de um mundo grande tenta desenhar milhares de linhas.
        const float targetLines = 18.0f;
        const float rawStep = std::max(0.0001f, std::max(halfWidth, halfHeight) * 2.0f / targetLines);
        const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
        const float normalized = rawStep / magnitude;
        const float gridStep = (normalized < 2.0f ? 1.0f : normalized < 5.0f ? 2.0f : 5.0f) * magnitude;
        const int minX = static_cast<int>(std::floor(centerX / gridStep)) - 1;
        const int maxX = static_cast<int>(std::ceil(centerX / gridStep)) + 1;
        const int minY = static_cast<int>(std::floor(centerY / gridStep)) - 1;
        const int maxY = static_cast<int>(std::ceil(centerY / gridStep)) + 1;
        const glm::mat4 identity(1.0f);

        auto projectWorld = [&](const glm::vec3& world, ImVec2& screen) -> bool
        {
            return ProjectPoint(view, projection, identity, world,
                                imageMin, imageMax, screen);
        };
        auto drawSegment = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float width)
        {
            ImVec2 screenA{}, screenB{};
            if (projectWorld(a, screenA) && projectWorld(b, screenB))
                drawList->AddLine(screenA, screenB, color, width);
        };

        for (int x = minX; x <= maxX; ++x)
        {
            const bool major = x % 5 == 0;
            const float worldX = static_cast<float>(x) * gridStep;
            drawSegment(
                glm::vec3(worldX, centerY - halfHeight, 0.0f),
                glm::vec3(worldX, centerY + halfHeight, 0.0f),
                major ? IM_COL32(85, 125, 165, 90) : IM_COL32(70, 95, 120, 48),
                major ? 1.0f : 1.0f);
        }
        for (int y = minY; y <= maxY; ++y)
        {
            const bool major = y % 5 == 0;
            const float worldY = static_cast<float>(y) * gridStep;
            drawSegment(
                glm::vec3(centerX - halfWidth, worldY, 0.0f),
                glm::vec3(centerX + halfWidth, worldY, 0.0f),
                major ? IM_COL32(85, 125, 165, 90) : IM_COL32(70, 95, 120, 48),
                major ? 1.0f : 1.0f);
        }

        drawSegment(
            glm::vec3(centerX - halfWidth, 0.0f, 0.001f),
            glm::vec3(centerX + halfWidth, 0.0f, 0.001f),
            IM_COL32(230, 80, 80, 210), 2.0f);
        drawSegment(
            glm::vec3(0.0f, centerY - halfHeight, 0.001f),
            glm::vec3(0.0f, centerY + halfHeight, 0.001f),
            IM_COL32(85, 220, 125, 210), 2.0f);

        char gridLabel[64]{};
        std::snprintf(gridLabel, sizeof(gridLabel), "MUNDO 2D | XY | grade %.3g", gridStep);
        drawList->AddText(ImVec2(imageMin.x + 12.0f, imageMin.y + 12.0f),
                          IM_COL32(160, 215, 255, 230), gridLabel);
    }

    static void DrawColliderOverlay(
        const std::vector<Entity>& entities,
        int selectedIndex,
        bool is2DMode,
        const glm::mat4& view,
        const glm::mat4& projection,
        const ImVec2& imageMin,
        const ImVec2& imageMax,
        ImDrawList* drawList)
    {
        const glm::mat4 identity(1.0f);
        auto projectWorld = [&](const glm::vec3& world, ImVec2& screen) -> bool
        {
            return ProjectPoint(view, projection, identity, world, imageMin, imageMax, screen);
        };

        for (size_t i = 0; i < entities.size(); ++i)
        {
            const Entity& entity = entities[i];
            if (entity.IsEditorOnly() || !entity.Active || !entity.ColliderEnabled)
                continue;

            const bool isSelected = (static_cast<int>(i) == selectedIndex);
            if (!is2DMode && !isSelected)
                continue;

            const ImU32 colColor = entity.IsTrigger
                ? IM_COL32(55, 210, 255, 245)
                : (isSelected ? IM_COL32(255, 225, 45, 240) : IM_COL32(50, 225, 95, 185));
            const float lineThickness = isSelected ? 2.0f : 1.2f;

            const glm::mat4 worldMatrix = GetEntityWorldMatrix(entity);
            glm::vec3 localHalf(0.5f);
            glm::vec3 localCenter(0.0f);
            if (entity.Collider == Entity::ColliderShape::Capsule)
                localHalf = glm::vec3(0.5f, 1.0f, 0.5f);
            if (entity.Collider == Entity::ColliderShape::Mesh ||
                entity.Collider == Entity::ColliderShape::ConvexMesh)
            {
                const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
                if (mesh && mesh->IsValid())
                {
                    localHalf = mesh->GetBoundingHalfExtents();
                    if (entity.Collider == Entity::ColliderShape::Mesh)
                        localCenter = mesh->GetBoundingCenter();
                }
            }
            auto worldPoint = [&](const glm::vec3& local)
            {
                return glm::vec3(worldMatrix * glm::vec4(localCenter + local, 1.0f));
            };
            auto drawSegment = [&](const glm::vec3& a, const glm::vec3& b)
            {
                ImVec2 screenA{}, screenB{};
                if (projectWorld(worldPoint(a), screenA) &&
                    projectWorld(worldPoint(b), screenB))
                    drawList->AddLine(screenA, screenB, colColor, lineThickness);
            };
            auto drawRing = [&](const glm::vec3& center, const glm::vec3& axisA,
                                const glm::vec3& axisB, float radiusA, float radiusB)
            {
                constexpr int segments = 24;
                for (int s = 0; s < segments; ++s)
                {
                    const float angleA = 6.28318530718f * static_cast<float>(s) / segments;
                    const float angleB = 6.28318530718f * static_cast<float>(s + 1) / segments;
                    const glm::vec3 pointA = center + axisA * (std::cos(angleA) * radiusA) +
                                             axisB * (std::sin(angleA) * radiusB);
                    const glm::vec3 pointB = center + axisA * (std::cos(angleB) * radiusA) +
                                             axisB * (std::sin(angleB) * radiusB);
                    drawSegment(pointA, pointB);
                }
            };

            if (is2DMode)
            {
                if (entity.Collider == Entity::ColliderShape::Sphere)
                {
                    drawRing(glm::vec3(0.0f), glm::vec3(1, 0, 0),
                             glm::vec3(0, 1, 0), localHalf.x, localHalf.y);
                }
                else
                {
                    const glm::vec3 corners[4] = {
                        glm::vec3(-localHalf.x, -localHalf.y, 0.0f),
                        glm::vec3( localHalf.x, -localHalf.y, 0.0f),
                        glm::vec3( localHalf.x,  localHalf.y, 0.0f),
                        glm::vec3(-localHalf.x,  localHalf.y, 0.0f)
                    };
                    for (int k = 0; k < 4; ++k)
                        drawSegment(corners[k], corners[(k + 1) % 4]);
                }
            }
            else if (entity.Collider == Entity::ColliderShape::Sphere)
            {
                drawRing(glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), localHalf.x, localHalf.y);
                drawRing(glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), localHalf.x, localHalf.z);
                drawRing(glm::vec3(0.0f), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), localHalf.y, localHalf.z);
            }
            else if (entity.Collider == Entity::ColliderShape::Cylinder)
            {
                drawRing(glm::vec3(0.0f, -localHalf.y, 0.0f), glm::vec3(1, 0, 0),
                         glm::vec3(0, 0, 1), localHalf.x, localHalf.z);
                drawRing(glm::vec3(0.0f, localHalf.y, 0.0f), glm::vec3(1, 0, 0),
                         glm::vec3(0, 0, 1), localHalf.x, localHalf.z);
                drawSegment(glm::vec3(localHalf.x, -localHalf.y, 0), glm::vec3(localHalf.x, localHalf.y, 0));
                drawSegment(glm::vec3(-localHalf.x, -localHalf.y, 0), glm::vec3(-localHalf.x, localHalf.y, 0));
                drawSegment(glm::vec3(0, -localHalf.y, localHalf.z), glm::vec3(0, localHalf.y, localHalf.z));
                drawSegment(glm::vec3(0, -localHalf.y, -localHalf.z), glm::vec3(0, localHalf.y, -localHalf.z));
            }
            else if (entity.Collider == Entity::ColliderShape::Cone)
            {
                const glm::vec3 apex(0.0f, localHalf.y, 0.0f);
                constexpr int segments = 16;
                for (int s = 0; s < segments; ++s)
                {
                    const float angle = 6.28318530718f * static_cast<float>(s) / segments;
                    const float nextAngle = 6.28318530718f * static_cast<float>(s + 1) / segments;
                    const glm::vec3 baseA(std::cos(angle) * localHalf.x, -localHalf.y,
                                          std::sin(angle) * localHalf.z);
                    const glm::vec3 baseB(std::cos(nextAngle) * localHalf.x, -localHalf.y,
                                          std::sin(nextAngle) * localHalf.z);
                    drawSegment(baseA, baseB);
                    if (s % 4 == 0)
                        drawSegment(apex, baseA);
                }
            }
            else if (entity.Collider == Entity::ColliderShape::Capsule)
            {
                const float capRadius = std::min(localHalf.x, localHalf.z);
                const float segmentHalf = std::max(0.0f, localHalf.y - capRadius);
                drawRing(glm::vec3(0, -segmentHalf, 0), glm::vec3(1, 0, 0),
                         glm::vec3(0, 0, 1), localHalf.x, localHalf.z);
                drawRing(glm::vec3(0, segmentHalf, 0), glm::vec3(1, 0, 0),
                         glm::vec3(0, 0, 1), localHalf.x, localHalf.z);
                drawSegment(glm::vec3(localHalf.x, -segmentHalf, 0), glm::vec3(localHalf.x, segmentHalf, 0));
                drawSegment(glm::vec3(-localHalf.x, -segmentHalf, 0), glm::vec3(-localHalf.x, segmentHalf, 0));
                drawSegment(glm::vec3(0, -segmentHalf, localHalf.z), glm::vec3(0, segmentHalf, localHalf.z));
                drawSegment(glm::vec3(0, -segmentHalf, -localHalf.z), glm::vec3(0, segmentHalf, -localHalf.z));
            }
            else if (entity.Collider == Entity::ColliderShape::ConvexMesh)
            {
                const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
                if (mesh && mesh->IsValid())
                {
                    const auto& vertices = mesh->GetVertices();
                    for (size_t vertexIndex = 0; vertexIndex + 2 < vertices.size(); vertexIndex += 3)
                    {
                        const glm::vec3 a(vertices[vertexIndex].X, vertices[vertexIndex].Y, vertices[vertexIndex].Z);
                        const glm::vec3 b(vertices[vertexIndex + 1].X, vertices[vertexIndex + 1].Y, vertices[vertexIndex + 1].Z);
                        const glm::vec3 c(vertices[vertexIndex + 2].X, vertices[vertexIndex + 2].Y, vertices[vertexIndex + 2].Z);
                        drawSegment(a, b);
                        drawSegment(b, c);
                        drawSegment(c, a);
                    }
                }
            }
            else
            {
                const glm::vec3 corners[8] = {
                    glm::vec3(-localHalf.x, -localHalf.y, -localHalf.z),
                    glm::vec3( localHalf.x, -localHalf.y, -localHalf.z),
                    glm::vec3( localHalf.x,  localHalf.y, -localHalf.z),
                    glm::vec3(-localHalf.x,  localHalf.y, -localHalf.z),
                    glm::vec3(-localHalf.x, -localHalf.y,  localHalf.z),
                    glm::vec3( localHalf.x, -localHalf.y,  localHalf.z),
                    glm::vec3( localHalf.x,  localHalf.y,  localHalf.z),
                    glm::vec3(-localHalf.x,  localHalf.y,  localHalf.z)
                };
                const int edges[12][2] = {
                    {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6},
                    {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7}
                };
                for (const auto& edge : edges)
                    drawSegment(corners[edge[0]], corners[edge[1]]);
            }

            if (entity.IsTrigger)
            {
                ImVec2 labelPosition{};
                if (projectWorld(worldPoint(glm::vec3(0.0f)), labelPosition))
                    drawList->AddText(labelPosition, IM_COL32(55, 210, 255, 245), "TRIGGER");
            }
        }
    }

    bool DrawUIOverlay(Application& app, const ImVec2& imageMin, const ImVec2& imageMax,
                       bool viewportHovered)
    {
        struct UIEntry { Entity* EntityPtr; int Z; };
        auto& sceneEntities = app.GetScene().GetEntities();
        std::unordered_map<uint64_t, Entity*> entitiesById;
        entitiesById.reserve(sceneEntities.size());
        for (Entity& entity : sceneEntities)
            entitiesById.emplace(entity.GetID(), &entity);
        const auto hierarchyDepths = MaraeUI::GetUIHierarchyDepths(sceneEntities);

        std::vector<UIEntry> entries;
        for (Entity& entity : sceneEntities)
        {
            if (entity.HasUI() && entity.GetUIComponent().Enabled &&
                MaraeUI::IsUIHierarchyEnabled(entity, entitiesById) &&
                entity.GetUIComponent().Type != UIElementType::ScreenGui)
                entries.push_back({ &entity, entity.GetUIComponent().ZIndex });
        }
        std::stable_sort(entries.begin(), entries.end(),
            [&hierarchyDepths](const UIEntry& a, const UIEntry& b)
            {
                if (a.Z != b.Z)
                    return a.Z < b.Z;
                return hierarchyDepths.at(a.EntityPtr->GetID()) <
                       hierarchyDepths.at(b.EntityPtr->GetID());
            });

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const MaraeUI::ResolvedUIMap scrollLayoutRects =
            MaraeUI::ResolveLayouts(sceneEntities, 960.0f, 540.0f);
        if (viewportHovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            Entity* hoveredScrollView = nullptr;
            size_t deepestScrollDepth = 0;
            for (Entity& candidate : sceneEntities)
            {
                if (!candidate.HasUI() || !candidate.GetUIComponent().Enabled ||
                    !MaraeUI::IsUIHierarchyEnabled(candidate, entitiesById) ||
                    !candidate.GetUIComponent().ScrollViewEnabled) continue;
                auto r = candidate.GetUIComponent().ResolveRect(960.0f, 540.0f);
                if (const auto it = scrollLayoutRects.find(candidate.GetID()); it != scrollLayoutRects.end())
                    r = it->second;
                const float sx = (imageMax.x - imageMin.x) / 960.0f;
                const float sy = (imageMax.y - imageMin.y) / 540.0f;
                const ImVec2 a(imageMin.x + r.X * sx, imageMin.y + r.Y * sy);
                const ImVec2 b(a.x + r.Width * sx, a.y + r.Height * sy);
                if (mouse.x < a.x || mouse.x > b.x || mouse.y < a.y || mouse.y > b.y) continue;
                size_t depth = 0;
                uint64_t parentId = candidate.GetParentID();
                while (parentId != 0 && depth <= sceneEntities.size())
                {
                    const auto parent = std::find_if(sceneEntities.begin(), sceneEntities.end(),
                        [parentId](const Entity& e) { return e.GetID() == parentId; });
                    if (parent == sceneEntities.end()) break;
                    parentId = parent->GetParentID();
                    ++depth;
                }
                if (!hoveredScrollView || depth >= deepestScrollDepth)
                {
                    hoveredScrollView = &candidate;
                    deepestScrollDepth = depth;
                }
            }
            if (hoveredScrollView)
            {
                auto& scrollUI = hoveredScrollView->GetUIComponent();
                scrollUI.ScrollOffsetY = std::clamp(scrollUI.ScrollOffsetY - ImGui::GetIO().MouseWheel * 40.0f,
                    0.0f, MaraeUI::GetMaxScrollOffset(*hoveredScrollView, sceneEntities,
                        960.0f, 540.0f, &scrollLayoutRects));
            }
        }
        const MaraeUI::ResolvedUIMap resolvedRects = MaraeUI::ResolveLayouts(sceneEntities, 960.0f, 540.0f);
        Entity* hoveredEntity = nullptr;
        int hoveredZ = -2147483647;
        for (const UIEntry& entry : entries)
        {
            Entity& entity = *entry.EntityPtr;
            if (entity.SelectionLocked)
                continue;
            UIComponent& ui = entity.GetUIComponent();
            const auto rectIt = resolvedRects.find(entity.GetID());
            const auto rect = rectIt != resolvedRects.end() ? rectIt->second : ui.ResolveRect(960.0f, 540.0f);
            // O renderizador nativo escala o canvas de referência (960x540) para
            // o tamanho real do alvo. As affordances de edição usam a mesma
            // transformação para que a seleção coincida com o que é desenhado.
            const float layoutScaleX = (imageMax.x - imageMin.x) / 960.0f;
            const float layoutScaleY = (imageMax.y - imageMin.y) / 540.0f;
            const ImVec2 min(imageMin.x + rect.X * layoutScaleX, imageMin.y + rect.Y * layoutScaleY);
            const ImVec2 max(min.x + std::max(1.0f, rect.Width * layoutScaleX),
                             min.y + std::max(1.0f, rect.Height * layoutScaleY));
            if (max.x < imageMin.x || min.x > imageMax.x || max.y < imageMin.y || min.y > imageMax.y)
                continue;

            // A UI only captures input while the pointer is actually over the
            // viewport image. This prevents dragging another editor panel from
            // selecting widgets using stale screen-space coordinates.
            bool insideClippingAncestors = true;
            uint64_t clipParentID = entity.GetParentID();
            size_t clipGuard = 0;
            while (clipParentID != 0 && clipGuard++ <= sceneEntities.size())
            {
                const auto parentIt = entitiesById.find(clipParentID);
                if (parentIt == entitiesById.end()) break;
                const Entity& parent = *parentIt->second;
                const UIComponent& parentUI = parent.GetUIComponent();
                if (parent.HasUI() && (parentUI.ClipsContent || parentUI.ScrollViewEnabled))
                {
                    const auto parentRectIt = resolvedRects.find(clipParentID);
                    const auto parentRect = parentRectIt != resolvedRects.end()
                        ? parentRectIt->second : parentUI.ResolveRect(960.0f, 540.0f);
                    const float parentScaleX = (imageMax.x - imageMin.x) / 960.0f;
                    const float parentScaleY = (imageMax.y - imageMin.y) / 540.0f;
                    const float left = imageMin.x + parentRect.X * parentScaleX;
                    const float top = imageMin.y + parentRect.Y * parentScaleY;
                    if (mouse.x < left || mouse.x > left + parentRect.Width * parentScaleX ||
                        mouse.y < top || mouse.y > top + parentRect.Height * parentScaleY)
                        insideClippingAncestors = false;
                }
                clipParentID = parent.GetParentID();
            }
            const bool hovered = viewportHovered && insideClippingAncestors &&
                mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
            if (hovered && (hoveredEntity == nullptr || entry.Z >= hoveredZ))
            {
                hoveredEntity = entry.EntityPtr;
                hoveredZ = entry.Z;
            }
            // O overlay do editor não desenha mais o visual da UI — quem faz isso
            // agora é o renderizador nativo, idêntico ao que roda no jogo. Aqui
            // ficam apenas affordances de edição: destaque de hover e seleção.
            const bool isSelected = app.GetSelectedID() == entity.GetID();
            if (hovered || isSelected)
            {
                const ImU32 highlight = isSelected ? IM_COL32(255, 190, 70, 230)
                                                   : IM_COL32(120, 190, 255, 170);
                drawList->AddRect(min, max, highlight,
                                  std::max(0.0f, ui.CornerRadius),
                                  0, isSelected ? 2.0f : 1.0f);
            }

            if (s_UIPositionDragging && app.GetSelectedID() == entity.GetID() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 current = ImGui::GetIO().MousePos;
                const float dx = (current.x - s_UIPositionStartMouse.x) / layoutScaleX;
                const float dy = (current.y - s_UIPositionStartMouse.y) / layoutScaleY;
                if (ui.UseResponsiveAnchors)
                {
                    ui.OffsetMinX = s_UIPositionOrigOffsets[0] + dx;
                    ui.OffsetMinY = s_UIPositionOrigOffsets[1] + dy;
                    ui.OffsetMaxX = s_UIPositionOrigOffsets[2] + dx;
                    ui.OffsetMaxY = s_UIPositionOrigOffsets[3] + dy;
                }
                else
                {
                    ui.PositionX = s_UIPositionOrigX + dx;
                    ui.PositionY = s_UIPositionOrigY + dy;
                }
                app.MarkDirty();
            }
        }

        if (s_UIPositionDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            s_UIPositionDragging = false;

        // Only the topmost widget under the pointer receives the selection and
        // drag start. Previously every overlapped widget could select itself in
        // the same frame and the 3D picker would run afterward.
        if (viewportHovered && hoveredEntity && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto& entities = app.GetScene().GetEntities();
            for (size_t i = 0; i < entities.size(); ++i)
            {
                if (entities[i].GetID() != hoveredEntity->GetID())
                    continue;
                app.SetSelectedEntity(static_cast<int>(i));
                app.SetMultiSelectedEntities({ static_cast<int>(i) });
                s_UIPositionDragging = true;
                s_UIPositionStartMouse = mouse;
                s_UIPositionOrigX = hoveredEntity->GetUIComponent().PositionX;
                s_UIPositionOrigY = hoveredEntity->GetUIComponent().PositionY;
                const UIComponent& selectedUI = hoveredEntity->GetUIComponent();
                s_UIPositionOrigOffsets[0] = selectedUI.OffsetMinX;
                s_UIPositionOrigOffsets[1] = selectedUI.OffsetMinY;
                s_UIPositionOrigOffsets[2] = selectedUI.OffsetMaxX;
                s_UIPositionOrigOffsets[3] = selectedUI.OffsetMaxY;
                app.MarkDirty();
                break;
            }
        }

        // --- Gizmo de redimensionamento para elemento UI selecionado ---
        const int selectedIndex = app.GetSelectedEntityIndex();
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(app.GetScene().GetEntities().size()))
        {
            Entity& sel = app.GetScene().GetEntities()[static_cast<size_t>(selectedIndex)];
            if (sel.HasUI() && app.GetSelectedID() == sel.GetID())
            {
                UIComponent& ui = sel.GetUIComponent();
                const auto rectIt = resolvedRects.find(sel.GetID());
                const auto rect = rectIt != resolvedRects.end()
                    ? rectIt->second : ui.ResolveRect(960.0f, 540.0f);
                UIComponent::ResolvedRect parentRect{ 0.0f, 0.0f, 960.0f, 540.0f };
                const auto parentIt = entitiesById.find(sel.GetParentID());
                if (parentIt != entitiesById.end() && parentIt->second->HasUI())
                {
                    const auto parentResolved = resolvedRects.find(sel.GetParentID());
                    if (parentResolved != resolvedRects.end())
                        parentRect = parentResolved->second;
                }
                const float layoutScaleX = (imageMax.x - imageMin.x) / 960.0f;
                const float layoutScaleY = (imageMax.y - imageMin.y) / 540.0f;
                const ImVec2 min(imageMin.x + rect.X * layoutScaleX, imageMin.y + rect.Y * layoutScaleY);
                const ImVec2 max(min.x + std::max(1.0f, rect.Width * layoutScaleX),
                                 min.y + std::max(1.0f, rect.Height * layoutScaleY));
                const ImU32 handleCol = IM_COL32(255, 200, 80, 220);
                const float handleSize = 8.0f;
                ImVec2 handles[8] = {
                    ImVec2(min.x, min.y), // TL
                    ImVec2((min.x+max.x)*0.5f, min.y), // T
                    ImVec2(max.x, min.y), // TR
                    ImVec2(max.x, (min.y+max.y)*0.5f), // R
                    ImVec2(max.x, max.y), // BR
                    ImVec2((min.x+max.x)*0.5f, max.y), // B
                    ImVec2(min.x, max.y), // BL
                    ImVec2(min.x, (min.y+max.y)*0.5f) // L
                };

                // draw handles
                for (int h = 0; h < 8; ++h)
                {
                    drawList->AddRectFilled(ImVec2(handles[h].x - handleSize*0.5f, handles[h].y - handleSize*0.5f),
                                            ImVec2(handles[h].x + handleSize*0.5f, handles[h].y + handleSize*0.5f), handleCol);
                }

                const ImVec2 mousePos = ImGui::GetIO().MousePos;
                // start drag
                if (!s_UIGizmoDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    for (int h = 0; h < 8; ++h)
                    {
                        if (mousePos.x >= handles[h].x - handleSize*0.5f && mousePos.x <= handles[h].x + handleSize*0.5f &&
                            mousePos.y >= handles[h].y - handleSize*0.5f && mousePos.y <= handles[h].y + handleSize*0.5f)
                        {
                            s_UIGizmoDragging = true;
                            s_UIGizmoHandle = h;
                            s_UIGizmoEntityID = sel.GetID();
                            s_UIGizmoStartMouse = mousePos;
                            s_UIGizmoOrigX = rect.X;
                            s_UIGizmoOrigY = rect.Y;
                            s_UIGizmoOrigW = rect.Width;
                            s_UIGizmoOrigH = rect.Height;
                            s_UIGizmoOrigFontSize = ui.FontSize;
                            break;
                        }
                    }
                }

                // dragging
                if (s_UIGizmoDragging && s_UIGizmoEntityID == sel.GetID() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const ImVec2 cur = ImGui::GetIO().MousePos;
                    const float dx = (cur.x - s_UIGizmoStartMouse.x) / layoutScaleX;
                    const float dy = (cur.y - s_UIGizmoStartMouse.y) / layoutScaleY;
                    // Redimensionamos o retângulo visual, não o ponto de
                    // âncora; ao final reconstruímos Position a partir dele.
                    float newX = s_UIGizmoOrigX;
                    float newY = s_UIGizmoOrigY;
                    float newW = s_UIGizmoOrigW;
                    float newH = s_UIGizmoOrigH;
                    switch (s_UIGizmoHandle)
                    {
                    case 0: // TL
                        newX = s_UIGizmoOrigX + dx;
                        newY = s_UIGizmoOrigY + dy;
                        newW = s_UIGizmoOrigW - dx;
                        newH = s_UIGizmoOrigH - dy;
                        break;
                    case 1: // T
                        newY = s_UIGizmoOrigY + dy;
                        newH = s_UIGizmoOrigH - dy;
                        break;
                    case 2: // TR
                        newY = s_UIGizmoOrigY + dy;
                        newW = s_UIGizmoOrigW + dx;
                        newH = s_UIGizmoOrigH - dy;
                        break;
                    case 3: // R
                        newW = s_UIGizmoOrigW + dx;
                        break;
                    case 4: // BR
                        newW = s_UIGizmoOrigW + dx;
                        newH = s_UIGizmoOrigH + dy;
                        break;
                    case 5: // B
                        newH = s_UIGizmoOrigH + dy;
                        break;
                    case 6: // BL
                        newX = s_UIGizmoOrigX + dx;
                        newW = s_UIGizmoOrigW - dx;
                        newH = s_UIGizmoOrigH + dy;
                        break;
                    case 7: // L
                        newX = s_UIGizmoOrigX + dx;
                        newW = s_UIGizmoOrigW - dx;
                        break;
                    }
                    // clamp
                    newW = std::max(1.0f, newW);
                    newH = std::max(1.0f, newH);
                    ui.Width = newW;
                    ui.Height = newH;
                    const float localX = newX - parentRect.X;
                    const float localY = newY - parentRect.Y;
                    if (ui.UseResponsiveAnchors)
                    {
                        ui.OffsetMinX = localX - ui.AnchorMinX * parentRect.Width;
                        ui.OffsetMinY = localY - ui.AnchorMinY * parentRect.Height;
                        ui.OffsetMaxX = localX + newW - ui.AnchorMaxX * parentRect.Width;
                        ui.OffsetMaxY = localY + newH - ui.AnchorMaxY * parentRect.Height;
                    }
                    else
                    {
                        ui.PositionX = localX + ui.AnchorX * newW;
                        ui.PositionY = localY + ui.AnchorY * newH;
                    }

                    // text heuristic: combine font-size change and residual scale
                    if (ui.Type == UIElementType::TextLabel || ui.Type == UIElementType::TextButton || ui.Type == UIElementType::TextBox)
                    {
                        const float heightScale = newH / std::max(1.0f, s_UIGizmoOrigH);
                        ui.FontSize = std::max(6.0f, s_UIGizmoOrigFontSize * heightScale);
                    }

                    app.MarkDirty();
                }

                if (s_UIGizmoDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    s_UIGizmoDragging = false;
                    s_UIGizmoHandle = -1;
                    s_UIGizmoEntityID = 0;
                }
            }
        }

        return hoveredEntity != nullptr || s_UIPositionDragging || s_UIGizmoDragging;
    }
}

void ViewportPanel::Draw(Application& app)
{
    // O viewport nunca passa à frente de painéis flutuantes ao receber foco;
    // Hierarquia, Inspector e demais painéis permanecem interativos por cima.
    ImGui::Begin(UI("Viewport"), nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus);
    // A toolbar agora vive como barra própria fora do Viewport (ver Application.cpp)
    if (ImGui::SmallButton(s_ShowSceneProfiler ? "Fechar Profiler" : "Profiler da Cena"))
        s_ShowSceneProfiler = !s_ShowSceneProfiler;

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = std::max(1, static_cast<int>(available.x));
    const int height = std::max(1, static_cast<int>(available.y));
    app.ResizeViewport(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    // Não considere o viewport sob painéis, janelas flutuantes ou itens ativos;
    // isso evita que arrastar/rolar um painel também controle a câmera.
    app.SetViewportHovered(ImGui::IsWindowHovered());
    app.SetViewportFocused(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

    if (app.GetWindow().GetGraphicsAPI() == GraphicsAPI::Vulkan)
    {
        app.SetViewportImageHovered(false);
        app.SetViewportPointer(0.0f, 0.0f, false);
        ImGui::BeginChild("##VulkanSceneViewportStatus", available, true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 region = ImGui::GetContentRegionAvail();
        const float textWidth = std::min(520.0f, std::max(250.0f, region.x - 40.0f));
        ImGui::SetCursorPos(ImVec2(std::max(16.0f, (region.x - textWidth) * 0.5f),
                                   std::max(24.0f, region.y * 0.36f)));
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.35f, 0.68f, 1.0f, 1.0f), "Vulkan ativo — interface do editor");
        ImGui::Separator();
        ImGui::TextWrapped("A viewport 3D/2D da cena ainda é renderizada pelo OpenGL e não é exibida neste backend. Hierarquia, Inspector, assets e demais painéis já estão sendo apresentados por Vulkan.");
        ImGui::Spacing();
        ImGui::SetCursorPosX(std::max(16.0f, (region.x - 190.0f) * 0.5f));
        if (ImGui::Button(UI("Voltar para OpenGL"), ImVec2(190.0f, 36.0f)))
            app.RequestGraphicsAPISwitch(GraphicsAPI::OpenGL);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    const uint32_t texture = app.GetViewportTexture();
    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImVec2 imageMax(imageMin.x + static_cast<float>(width), imageMin.y + static_cast<float>(height));
    bool imageHovered = false;
    bool imageLeftClicked = false;
    if (texture != 0 && available.x > 1.0f && available.y > 1.0f)
    {
        ImGui::Image(static_cast<ImTextureID>(texture),
            ImVec2(static_cast<float>(width), static_cast<float>(height)),
            ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        imageMin = ImGui::GetItemRectMin();
        imageMax = ImGui::GetItemRectMax();
        imageHovered = ImGui::IsItemHovered();
        imageLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    }
    else
    {
        ImGui::TextDisabled("Viewport indispon�vel");
    }
    app.SetViewportImageHovered(imageHovered);
    const ImVec2 pointer = ImGui::GetIO().MousePos;
    app.SetViewportPointer(pointer.x - imageMin.x, pointer.y - imageMin.y, imageHovered);

    const bool hasImage = texture != 0 && available.x > 1.0f && available.y > 1.0f;
    int hoveredEntityIndex = -1;
    bool uiInteractionCaptured = false;
    if (hasImage)
    {
        Entity* terrainEntity = nullptr;
        if (app.GetEditorState() != Application::EditorState::Pause ||
            app.GetEditorState() == Application::EditorState::Pause)
        {
            const int selectedIndex = app.GetSelectedEntityIndex();
            const auto& sceneEntities = app.GetScene().GetEntities();
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(sceneEntities.size()) &&
                sceneEntities[static_cast<size_t>(selectedIndex)].HasTerrain())
            {
                terrainEntity = const_cast<Entity*>(&sceneEntities[static_cast<size_t>(selectedIndex)]);
            }
        }

        if (terrainEntity && imageHovered && !InspectorPanel::IsModelingMode())
        {
            Camera terrainCamera = app.GetViewportCamera();
            const float brushWidth = std::max(1.0f, imageMax.x - imageMin.x);
            const float brushHeight = std::max(1.0f, imageMax.y - imageMin.y);
            const glm::mat4 brushView = app.GetRenderer().GetCameraRenderer().GetViewMatrix(terrainCamera);
            const glm::mat4 brushProjection = BuildViewportProjection(
                terrainCamera, brushWidth, brushHeight);
            const ImGuiIO& io = ImGui::GetIO();
            const float mouseX = io.MousePos.x - imageMin.x;
            const float mouseY = io.MousePos.y - imageMin.y;
            const Ray ray = Picking::ScreenPointToRay(
                terrainCamera, mouseX, mouseY,
                std::max(1, static_cast<int>(imageMax.x - imageMin.x)),
                std::max(1, static_cast<int>(imageMax.y - imageMin.y)));
            const float terrainY = terrainEntity->GetTransform().Position.Y;
            const float denominator = ray.Direction.y;
            bool hasBrushPoint = std::abs(denominator) > 0.0001f;
            float rayDistance = hasBrushPoint ? (terrainY - ray.Origin.y) / denominator : -1.0f;
            hasBrushPoint = hasBrushPoint && rayDistance > 0.0f;
            glm::vec3 brushPoint = ray.Origin + ray.Direction * rayDistance;

            if (hasBrushPoint && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
            {
                Transform& transform = terrainEntity->GetTransform();
                const glm::mat4 model = BuildModel(transform);
                const glm::vec3 local = glm::vec3(glm::inverse(model) * glm::vec4(brushPoint, 1.0f));
                TerrainComponent& terrain = terrainEntity->GetTerrainComponent();
                if (terrain.SculptAt(local.x, local.z, std::max(0.0f, io.DeltaTime)))
                {
                    terrainEntity->GetMeshComponent().MeshData->UpdateVertices(
                        terrain.BuildVertices(), "Terrain::Sculpt");
                    app.MarkDirty();
                }
            }

            if (hasBrushPoint)
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const Transform& transform = terrainEntity->GetTransform();
                const float radius = terrainEntity->GetTerrainComponent().BrushRadius;
                ImVec2 previous{};
                bool hasPrevious = false;
                for (int i = 0; i <= 32; ++i)
                {
                    const float angle = glm::two_pi<float>() * static_cast<float>(i) / 32.0f;
                    ImVec2 current{};
                    const glm::vec3 point = brushPoint + glm::vec3(
                        std::cos(angle) * radius, 0.03f, std::sin(angle) * radius);
                    if (ProjectPoint(brushView, brushProjection, glm::mat4(1.0f), point,
                                      imageMin, imageMax, current) && hasPrevious)
                        drawList->AddLine(previous, current, IM_COL32(255, 205, 70, 230), 2.0f);
                    previous = current;
                    hasPrevious = true;
                }
                (void)transform;
            }
        }

        // Navegação do mundo 2D: botão direito arrastado move a visão livremente;
        // roda controla o zoom e F enquadra a seleção.
        if (app.Is2DWorldMode() && imageHovered)
        {
            Camera& editorCamera = app.GetCamera();
            ImGuiIO& io = ImGui::GetIO();
            if (std::abs(io.MouseWheel) > 0.001f && !ImGuizmo::IsOver())
            {
                const float zoomFactor = std::pow(0.86f, io.MouseWheel);
                editorCamera.OrthographicSize = std::clamp(
                    editorCamera.OrthographicSize * zoomFactor, 0.1f, 10000.0f);
            }

            if (ImGui::IsMouseDown(ImGuiMouseButton_Right
            ) && !ImGuizmo::IsOver())
            {
                const float worldPerPixel = editorCamera.OrthographicSize /
                    std::max(1.0f, imageMax.y - imageMin.y);
                editorCamera.Position.X -= io.MouseDelta.x * worldPerPixel;
                editorCamera.Position.Y += io.MouseDelta.y * worldPerPixel;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_F) && !ImGui::GetIO().WantTextInput)
            {
                const int selectedIndex = app.GetSelectedEntityIndex();
                const auto& sceneEntities = app.GetScene().GetEntities();
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(sceneEntities.size()))
                {
                    const Entity& selected = sceneEntities[static_cast<size_t>(selectedIndex)];
                    const Transform& transform = const_cast<Entity&>(selected).GetTransform();
                    editorCamera.Position.X = transform.Position.X;
                    editorCamera.Position.Y = transform.Position.Y;
                    const auto& mesh = const_cast<Entity&>(selected).GetMeshComponent().MeshData;
                    const float radius = mesh && mesh->IsValid()
                        ? mesh->GetBoundingRadius() * std::max({ std::abs(transform.Scale.X),
                            std::abs(transform.Scale.Y), 0.1f }) : 1.0f;
                    editorCamera.OrthographicSize = std::clamp(radius * 3.0f, 0.5f, 10000.0f);
                }
            }
        }

        if (app.Is2DWorldMode())
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 helpMin(imageMin.x + 12.0f, imageMax.y - 30.0f);
            drawList->AddText(helpMin, IM_COL32(190, 205, 220, 210),
                "Arraste com o botao direito: mover visao  |  Roda: zoom  |  F: enquadrar selecao");
        }

        // UI 2D fica por cima do framebuffer, como ScreenGui/GuiObject:
        // os widgets aparecem mesmo quando não possuem mesh 3D.
        // Quando a UI esta oculta no modo editor (botao "UI 2D" da toolbar),
        // ela tambem deixa de capturar o mouse para nao bloquear a edicao.
        if (app.IsRenderUIInEditorActive())
            uiInteractionCaptured = DrawUIOverlay(app, imageMin, imageMax, imageHovered);
        else
            uiInteractionCaptured = false;

        // Escala vertical de velocidade do fly, ancorada à esquerda do viewport.
        // A escala logarítmica oferece uma faixa ampla sem perder precisão perto de 1x.
        if (!app.Is2DWorldMode())
        {
            const ImGuiIO& io = ImGui::GetIO();
            const double currentTime = ImGui::GetTime();
            if (imageHovered && std::abs(io.MouseWheel) > 0.0f)
                s_FlySpeedLastScrollTime = currentTime;
            const bool showFlySpeed = s_FlySpeedDragging ||
                (currentTime - s_FlySpeedLastScrollTime < 2.0);

            if (showFlySpeed)
            {
            constexpr float minFlySpeed = 0.01f;
            constexpr float maxFlySpeed = 1000.0f;
            constexpr float railHeight = 156.0f;
            const float railX = imageMin.x + 23.0f;
            const float railTop = std::max(imageMin.y + 54.0f,
                (imageMin.y + imageMax.y - railHeight) * 0.5f);
            const float railBottom = std::min(imageMax.y - 46.0f, railTop + railHeight);
            const float actualRailHeight = std::max(40.0f, railBottom - railTop);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool overRail = imageHovered &&
                mouse.x >= railX - 13.0f && mouse.x <= railX + 13.0f &&
                mouse.y >= railTop - 10.0f && mouse.y <= railBottom + 10.0f;

            if (showFlySpeed && overRail && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                s_FlySpeedDragging = true;
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                s_FlySpeedDragging = false;

            if (s_FlySpeedDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float normalizedY = std::clamp(
                    (railBottom - mouse.y) / actualRailHeight, 0.0f, 1.0f);
                const float multiplier = minFlySpeed * std::pow(
                    maxFlySpeed / minFlySpeed, normalizedY);
                app.SetCameraFlySpeedMultiplier(multiplier);
                uiInteractionCaptured = true;
            }
            uiInteractionCaptured = uiInteractionCaptured ||
                (showFlySpeed && overRail) || s_FlySpeedDragging;

            const float multiplier = std::clamp(
                app.GetCameraFlySpeedMultiplier(), minFlySpeed, maxFlySpeed);
            const float normalizedSpeed = std::log(multiplier / minFlySpeed) /
                std::log(maxFlySpeed / minFlySpeed);
            const float knobY = railBottom - normalizedSpeed * actualRailHeight;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddLine(ImVec2(railX, railTop), ImVec2(railX, railBottom),
                IM_COL32(100, 110, 125, 170), 2.0f);
            drawList->AddLine(ImVec2(railX, knobY), ImVec2(railX, railBottom),
                IM_COL32(35, 145, 245, 220), 2.0f);
            for (int decade = 0; decade <= 5; ++decade)
            {
                const float value = minFlySpeed * std::pow(10.0f, static_cast<float>(decade));
                const float t = std::log(value / minFlySpeed) /
                    std::log(maxFlySpeed / minFlySpeed);
                const float y = railBottom - t * actualRailHeight;
                const float tickWidth = (decade == 2 || decade == 4) ? 7.0f : 4.0f;
                drawList->AddLine(ImVec2(railX - tickWidth, y), ImVec2(railX + tickWidth, y),
                    IM_COL32(190, 200, 215, 220), 1.0f);
            }
            drawList->AddCircleFilled(ImVec2(railX, knobY), 7.0f,
                IM_COL32(235, 242, 250, 255));
            drawList->AddCircle(ImVec2(railX, knobY), 8.0f,
                IM_COL32(35, 145, 245, 255), 0, 2.0f);

            char speedLabel[32];
            std::snprintf(speedLabel, sizeof(speedLabel), "%.2gx", multiplier);
            const ImVec2 labelSize = ImGui::CalcTextSize(speedLabel);
            const ImVec2 labelPosition(railX - labelSize.x * 0.5f, railTop - labelSize.y - 7.0f);
            drawList->AddText(labelPosition,
                IM_COL32(230, 238, 248, 220), speedLabel);

            if (overRail || s_FlySpeedDragging)
                ImGui::SetTooltip(UI("Velocidade da câmera: scroll + botão direito ou arraste a escala (0,01x–1000x)"));
            }
        }
    }
    ImGuizmo::BeginFrame();
    if (hasImage)
    {
        const int owningSelectionIndex = app.GetSelectedEntityIndex();
        const auto& entities = app.GetScene().GetEntities();
        int selectedIndex = owningSelectionIndex;
        bool selectedHasEditableGeometry = false;
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(entities.size()))
        {
            const Entity& selectedEntity = entities[static_cast<size_t>(selectedIndex)];
            constexpr uint64_t componentMask = 0xFF0000000ULL;
            const bool componentNodeSelected = (app.GetSelectedID() & componentMask) != 0 &&
                (app.GetSelectedID() & ~componentMask) == selectedEntity.GetID();
            const auto& selectedMesh = const_cast<Entity&>(selectedEntity).GetMeshComponent().MeshData;
            selectedHasEditableGeometry = selectedMesh && selectedMesh->IsValid() &&
                const_cast<Entity&>(selectedEntity).GetMeshComponent().PrimitiveType != "None" &&
                const_cast<Entity&>(selectedEntity).GetMeshComponent().PrimitiveType != "UI";
            if (componentNodeSelected || selectedEntity.HasUI() || selectedEntity.IsFolder ||
                selectedEntity.IsGlobalLight() || selectedEntity.IsGlobalScript())
                selectedIndex = -1;
        }
        // A edição durante Play é uma preferência do projeto. Desligada,
        // mantém o jogo simulando, mas bloqueia seleção e ferramentas do editor.
        const bool editing = app.GetEditorState() != Application::EditorState::Play ||
            app.GetEditDuringPlay();
        const bool modeling = InspectorPanel::IsModelingMode();
        const float viewportWidth = std::max(1.0f, imageMax.x - imageMin.x);
        const float viewportHeight = std::max(1.0f, imageMax.y - imageMin.y);
        Camera viewportCamera = app.GetViewportCamera();
        const glm::mat4 view = app.GetRenderer().GetCameraRenderer().GetViewMatrix(viewportCamera);
        const glm::mat4 projection = BuildViewportProjection(
            viewportCamera, viewportWidth, viewportHeight);

        if (s_ShowSceneProfiler && !modeling)
            DrawSceneProfiler(app, entities, view, projection, imageMin, imageMax);

        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imageMin.x, imageMin.y, viewportWidth, viewportHeight);
        ImGuizmo::SetOrthographic(viewportCamera.Orthographic);

        if (modeling && selectedHasEditableGeometry && selectedIndex >= 0 &&
            selectedIndex < static_cast<int>(entities.size()) &&
            InspectorPanel::GetModelingEntityID() ==
                entities[static_cast<size_t>(selectedIndex)].GetID())
        {
            DrawModelingOverlay(
                entities[static_cast<size_t>(selectedIndex)],
                InspectorPanel::GetModelingSelection(),
                InspectorPanel::GetModelingSelectionMode(),
                view, projection, imageMin, imageMax, ImGui::GetWindowDrawList());
        }

        if (editing && !modeling && !app.Is2DWorldMode() && selectedIndex >= 0 &&
            selectedIndex < static_cast<int>(entities.size()))
        {
            DrawLightRangeOverlay(
                entities[static_cast<size_t>(selectedIndex)],
                view, projection, imageMin, imageMax, ImGui::GetWindowDrawList());
        }

        if (editing && !modeling && app.Is2DWorldMode())
        {
            Draw2DWorldGrid(
                viewportCamera, view, projection, imageMin, imageMax,
                ImGui::GetWindowDrawList());
        }

        if (editing && !modeling)
        {
            DrawColliderOverlay(
                entities, selectedIndex, app.Is2DWorldMode(),
                view, projection, imageMin, imageMax,
                ImGui::GetWindowDrawList());
        }

        const Entity* activeSceneCamera = app.GetActiveSceneCamera();
        if (editing && !modeling && !app.Is2DWorldMode())
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
            {
                const Entity& camEntity = entities[entityIndex];
                if (!camEntity.HasCamera()) continue;

                // Se a viewport atual estiver renderizando a visao DESTA camera
                // (ou seja, ela for a camera ativa em Play ou se as linhas foram desativadas),
                // nao desenha o frustum/gizmo para nao poluir a tela.
                const bool isViewCamera = (activeSceneCamera && activeSceneCamera->GetID() == camEntity.GetID());
                if (isViewCamera) continue;
                if (!camEntity.GetCameraComponent().ShowFrustumGizmo) continue;

                DrawCameraOverlay(
                    camEntity,
                    static_cast<int>(entityIndex) == selectedIndex,
                    view, projection, imageMin, imageMax, drawList);
            }
        }

        // O hover usa o mesmo picking do clique, mas não altera a seleção.
        // O contorno branco só é desenhado quando o objeto sob o cursor é
        // diferente do selecionado; assim a linha amarela sempre prevalece.
        if (editing && !modeling && imageHovered && !uiInteractionCaptured)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const Picking::Hit hit = app.Is2DWorldMode()
                ? PickEntity2DAtScreenPoint(app, view, projection, imageMin, imageMax, mouse)
                : Picking::PickEntityDetailed(
                    app.GetScene(), viewportCamera,
                    mouse.x - imageMin.x, mouse.y - imageMin.y,
                    static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));
            hoveredEntityIndex = hit.EntityIndex;
            if (hoveredEntityIndex >= 0 &&
                hoveredEntityIndex < static_cast<int>(entities.size()) &&
                hoveredEntityIndex != selectedIndex)
            {
                DrawHoverOutline(
                    entities[static_cast<size_t>(hoveredEntityIndex)],
                    view, projection, imageMin, imageMax,
                    ImGui::GetWindowDrawList());
            }
        }

        bool gizmoUsing = false;
        const bool canUseGizmo = selectedIndex >= 0 && selectedIndex < static_cast<int>(entities.size()) &&
            !entities[static_cast<size_t>(selectedIndex)].HasUI() &&
            !entities[static_cast<size_t>(selectedIndex)].IsFolder &&
            !entities[static_cast<size_t>(selectedIndex)].IsGlobalLight() &&
            !entities[static_cast<size_t>(selectedIndex)].IsGlobalScript();
        if (editing && !modeling && !MeshPainter::IsBrushActive() && canUseGizmo)
        {
            Entity& selected = const_cast<Entity&>(entities[static_cast<size_t>(selectedIndex)]);
            const bool is2DWorld = app.Is2DWorldMode();
            const SnapSettings& snapSettings = app.GetSnapSettings();
            const Application::GizmoMode gizmoMode = app.GetGizmoMode();
            const ImGuizmo::MODE gizmoSpace = app.GetGizmoSpace() == Application::GizmoSpace::Local
                ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            float snap[3] = { 0.0f, 0.0f, 0.0f };
            if (gizmoMode == Application::GizmoMode::Translate && snapSettings.PositionEnabled)
                snap[0] = snap[1] = snap[2] = snapSettings.PositionStep;
            else if (gizmoMode == Application::GizmoMode::Rotate && snapSettings.RotationEnabled)
                snap[0] = snap[1] = snap[2] = snapSettings.RotationStep;
            else if (gizmoMode == Application::GizmoMode::Scale && snapSettings.ScaleEnabled)
                snap[0] = snap[1] = snap[2] = snapSettings.ScaleStep;

            std::vector<int> groupIndices;
            for (int index : app.GetMultiSelectedEntities())
                if (index >= 0 && index < static_cast<int>(entities.size()) &&
                    !entities[static_cast<size_t>(index)].IsGlobalLight() &&
                    !entities[static_cast<size_t>(index)].IsGlobalScript() &&
                    !entities[static_cast<size_t>(index)].HasUI() &&
                    !entities[static_cast<size_t>(index)].IsFolder &&
                    std::find(groupIndices.begin(), groupIndices.end(), index) == groupIndices.end())
                    groupIndices.push_back(index);
            if (std::find(groupIndices.begin(), groupIndices.end(), selectedIndex) == groupIndices.end())
                groupIndices.push_back(selectedIndex);

            {
                std::vector<int> childIndices;
                for (size_t i = 0; i < entities.size(); ++i)
                {
                    if (entities[i].GetParentID() == selected.GetID() && static_cast<int>(i) != selectedIndex)
                        childIndices.push_back(static_cast<int>(i));
                }
                for (uint64_t cid : selected.GetChildrenIDs())
                {
                    for (size_t i = 0; i < entities.size(); ++i)
                    {
                        if (entities[i].GetID() == cid && std::find(childIndices.begin(), childIndices.end(), static_cast<int>(i)) == childIndices.end())
                            childIndices.push_back(static_cast<int>(i));
                    }
                }
                const bool hasChildren = !childIndices.empty();
                const bool isGroupSelection = groupIndices.size() > 1;

                Transform gizmoTransform = DecomposeGizmoTransform(
                    selected.GetTransform().WorldMatrix, selected.GetTransform());
                if (isGroupSelection)
                {
                    glm::vec3 bMin(1e9f), bMax(-1e9f), sumPos(0.0f);
                    int count = 0;
                    for (int index : groupIndices)
                    {
                        Entity& item = const_cast<Entity&>(entities[static_cast<size_t>(index)]);
                        const glm::mat4& wMat = item.GetTransform().WorldMatrix;
                        const auto& mData = item.GetMeshComponent().MeshData;
                        if (mData && mData->IsValid())
                        {
                            glm::vec3 lMin, lMax;
                            GetLocalMeshBounds(item, lMin, lMax);
                            for (int cx = 0; cx < 2; ++cx)
                                for (int cy = 0; cy < 2; ++cy)
                                    for (int cz = 0; cz < 2; ++cz)
                                    {
                                        glm::vec3 corner(cx ? lMax.x : lMin.x, cy ? lMax.y : lMin.y, cz ? lMax.z : lMin.z);
                                        glm::vec3 wCorner = glm::vec3(wMat * glm::vec4(corner, 1.0f));
                                        bMin = glm::min(bMin, wCorner);
                                        bMax = glm::max(bMax, wCorner);
                                    }
                        }
                        else
                        {
                            glm::vec3 wPos = glm::vec3(wMat[3]);
                            bMin = glm::min(bMin, wPos);
                            bMax = glm::max(bMax, wPos);
                        }
                        sumPos += glm::vec3(wMat[3]);
                        count++;
                    }
                    glm::vec3 pivot = (bMin.x <= bMax.x) ? (bMin + bMax) * 0.5f : (sumPos / static_cast<float>(std::max(1, count)));
                    gizmoTransform.Position = Vector3(pivot.x, pivot.y, pivot.z);
                    if (gizmoSpace != ImGuizmo::LOCAL)
                        gizmoTransform.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    gizmoTransform.Scale = Vector3(1.0f, 1.0f, 1.0f);
                }
                else if (hasChildren)
                {
                    glm::vec3 bMin(1e9f), bMax(-1e9f), sumPos(0.0f);
                    int count = 0;
                    if (selectedHasEditableGeometry)
                    {
                        glm::vec3 lMin, lMax;
                        GetLocalMeshBounds(selected, lMin, lMax);
                        const glm::mat4& wMat = selected.GetTransform().WorldMatrix;
                        for (int cx = 0; cx < 2; ++cx)
                            for (int cy = 0; cy < 2; ++cy)
                                for (int cz = 0; cz < 2; ++cz)
                                {
                                    glm::vec3 corner(cx ? lMax.x : lMin.x, cy ? lMax.y : lMin.y, cz ? lMax.z : lMin.z);
                                    glm::vec3 wCorner = glm::vec3(wMat * glm::vec4(corner, 1.0f));
                                    bMin = glm::min(bMin, wCorner);
                                    bMax = glm::max(bMax, wCorner);
                                }
                        sumPos += glm::vec3(wMat[3]);
                        count++;
                    }
                    for (int index : childIndices)
                    {
                        Entity& item = const_cast<Entity&>(entities[static_cast<size_t>(index)]);
                        const glm::mat4& wMat = item.GetTransform().WorldMatrix;
                        const auto& mData = item.GetMeshComponent().MeshData;
                        if (mData && mData->IsValid())
                        {
                            glm::vec3 lMin, lMax;
                            GetLocalMeshBounds(item, lMin, lMax);
                            for (int cx = 0; cx < 2; ++cx)
                                for (int cy = 0; cy < 2; ++cy)
                                    for (int cz = 0; cz < 2; ++cz)
                                    {
                                        glm::vec3 corner(cx ? lMax.x : lMin.x, cy ? lMax.y : lMin.y, cz ? lMax.z : lMin.z);
                                        glm::vec3 wCorner = glm::vec3(wMat * glm::vec4(corner, 1.0f));
                                        bMin = glm::min(bMin, wCorner);
                                        bMax = glm::max(bMax, wCorner);
                                    }
                        }
                        else
                        {
                            glm::vec3 wPos = glm::vec3(wMat[3]);
                            bMin = glm::min(bMin, wPos);
                            bMax = glm::max(bMax, wPos);
                        }
                        sumPos += glm::vec3(wMat[3]);
                        count++;
                    }
                    glm::vec3 pivot = (bMin.x <= bMax.x) ? (bMin + bMax) * 0.5f : (sumPos / static_cast<float>(std::max(1, count)));
                    gizmoTransform.Position = Vector3(pivot.x, pivot.y, pivot.z);
                    if (gizmoSpace != ImGuizmo::LOCAL)
                        gizmoTransform.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    gizmoTransform.Scale = Vector3(1.0f, 1.0f, 1.0f);
                }

                const bool useFaceScaleHandles = !isGroupSelection && !hasChildren && selectedHasEditableGeometry &&
                    gizmoMode == Application::GizmoMode::Scale && app.GetFaceScaleHandlesEnabled();
                if (useFaceScaleHandles)
                {
                    if (!s_FaceScaleDrag.active)
                        s_FaceDockingActive = false;
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const Transform& transform = selected.GetTransform();
                    const glm::mat4 worldMat = GetEntityWorldMatrix(selected);
                    const glm::vec3 position = glm::vec3(worldMat[3]);
                    const glm::mat4 identity(1.0f);
                    glm::vec3 boundsMin, boundsMax;
                    GetLocalMeshBounds(selected, boundsMin, boundsMax);
                    const glm::vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
                    const glm::vec3 boundsHalfExtents = glm::max(
                        (boundsMax - boundsMin) * 0.5f, glm::vec3(0.001f));
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    const float axisColors[3][4] = {
                        { 0.95f, 0.20f, 0.18f, 1.0f },
                        { 0.20f, 0.90f, 0.30f, 1.0f },
                        { 0.20f, 0.48f, 1.00f, 1.0f }
                    };
                    bool hoveredHandle = false;
                    int hoveredAxis = -1;
                    float hoveredSign = 1.0f;
                    glm::vec3 hoveredWorldAxis(1.0f, 0.0f, 0.0f);
                    ImVec2 hoveredScreenAxis(0.0f, 0.0f);
                    float hoveredPixelsPerUnit = 1.0f;
                    float hoveredWorldScalePerLocalUnit = 1.0f;

                    for (int axis = 0; axis < 3; ++axis)
                    {
                        // Em 2D, escala só altera as dimensões do plano XY.
                        if (is2DWorld && axis == 2)
                            continue;
                        glm::vec3 localAxis(0.0f);
                        localAxis[axis] = 1.0f;
                        // Usa a direção real da aresta transformada, sem converter uma
                        // matriz com escala não uniforme em quaternion (isso entortava
                        // os eixos e fazia o arraste divergir do lado visível).
                        const glm::vec3 transformedAxis = glm::vec3(worldMat * glm::vec4(localAxis, 0.0f));
                        const float worldScalePerLocalUnit = glm::length(transformedAxis);
                        if (worldScalePerLocalUnit < 0.00001f)
                            continue;
                        const glm::vec3 worldAxis = transformedAxis / worldScalePerLocalUnit;
                        const glm::vec3 worldBoundsCenter = glm::vec3(worldMat * glm::vec4(boundsCenter, 1.0f));
                        ImVec2 centerScreen, axisScreen;
                        if (!ProjectPoint(view, projection, identity, worldBoundsCenter, imageMin, imageMax, centerScreen) ||
                            !ProjectPoint(view, projection, identity, worldBoundsCenter + worldAxis, imageMin, imageMax, axisScreen))
                            continue;
                        const ImVec2 axisVector(axisScreen.x - centerScreen.x, axisScreen.y - centerScreen.y);
                        const float pixelsPerUnit = std::sqrt(axisVector.x * axisVector.x + axisVector.y * axisVector.y);
                        if (pixelsPerUnit < 0.5f)
                        {
                            // Um eixo quase alinhado à direção da câmera não tem uma
                            // direção de arraste estável, mas continua identificado.
                            drawList->AddCircle(centerScreen, 9.0f,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 0.95f)), 0, 2.0f);
                            drawList->AddText(ImVec2(centerScreen.x + 10.0f + axis * 10.0f,
                                centerScreen.y - 7.0f),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 1.0f)),
                                axis == 0 ? "X" : (axis == 1 ? "Y" : "Z"));
                            continue;
                        }
                        const ImVec2 screenDirection(axisVector.x / pixelsPerUnit, axisVector.y / pixelsPerUnit);

                        for (int side = -1; side <= 1; side += 2)
                        {
                            const float sign = static_cast<float>(side);
                            glm::vec3 faceLocal = boundsCenter;
                            faceLocal[axis] += boundsHalfExtents[axis] * sign;
                            ImVec2 handle;
                            if (!ProjectPoint(view, projection, worldMat, faceLocal,
                                              imageMin, imageMax, handle))
                                continue;

                            const float centerToHandle = std::sqrt(
                                (handle.x - centerScreen.x) * (handle.x - centerScreen.x) +
                                (handle.y - centerScreen.y) * (handle.y - centerScreen.y));
                            const float lineInset = std::min(12.0f, centerToHandle * 0.35f);
                            const ImVec2 lineStart(centerScreen.x + screenDirection.x * sign * lineInset,
                                                   centerScreen.y + screenDirection.y * sign * lineInset);
                            drawList->AddLine(lineStart, handle,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 0.85f)), 2.0f);
                            const float dx = mouse.x - handle.x;
                            const float dy = mouse.y - handle.y;
                            const bool hit = imageHovered && dx * dx + dy * dy <= 13.0f * 13.0f;
                            const ImU32 color = hit
                                ? IM_COL32(255, 255, 255, 255)
                                : ImGui::ColorConvertFloat4ToU32(ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 1.0f));
                            drawList->AddCircleFilled(handle, hit ? 8.0f : 6.5f, color);
                            drawList->AddCircle(handle, hit ? 8.0f : 6.5f, IM_COL32(20, 22, 26, 255), 0, 1.5f);
                            if (hit)
                            {
                                hoveredHandle = true;
                                hoveredAxis = axis;
                                hoveredSign = sign;
                                hoveredWorldAxis = worldAxis;
                                hoveredScreenAxis = screenDirection;
                                hoveredPixelsPerUnit = pixelsPerUnit;
                                hoveredWorldScalePerLocalUnit = worldScalePerLocalUnit;
                            }
                        }
                    }

                    if (!s_FaceScaleDrag.active && hoveredHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        s_FaceScaleDrag.active = true;
                        s_FaceScaleDrag.entityId = selected.GetID();
                        s_FaceScaleDrag.axis = hoveredAxis;
                        s_FaceScaleDrag.sign = hoveredSign;
                        s_FaceScaleDrag.worldAxis = hoveredWorldAxis;
                        s_FaceScaleDrag.worldScalePerLocalUnit = hoveredWorldScalePerLocalUnit;
                        s_FaceScaleDrag.startMouse = mouse;
                        s_FaceScaleDrag.screenAxis = hoveredScreenAxis;
                        s_FaceScaleDrag.pixelsPerUnit = hoveredPixelsPerUnit;
                        s_FaceScaleDrag.initialTransform = transform;
                        s_FaceScaleDrag.boundsCenter = boundsCenter;
                        s_FaceScaleDrag.boundsHalfExtents = boundsHalfExtents;
                        app.m_BeforeTransform = transform;
                        app.SetWasUsingGizmo(true);
                    }

                    if (s_FaceScaleDrag.active && s_FaceScaleDrag.entityId == selected.GetID())
                    {
                        gizmoUsing = true;
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
                            const float mouseDeltaX = mouse.x - s_FaceScaleDrag.startMouse.x;
                            const float mouseDeltaY = mouse.y - s_FaceScaleDrag.startMouse.y;
                            float worldDelta = (mouseDeltaX * s_FaceScaleDrag.screenAxis.x +
                                                mouseDeltaY * s_FaceScaleDrag.screenAxis.y) /
                                               std::max(0.5f, s_FaceScaleDrag.pixelsPerUnit);
                            if (snapSettings.ScaleEnabled && snapSettings.ScaleStep > 0.0f)
                                worldDelta = std::round(worldDelta / snapSettings.ScaleStep) * snapSettings.ScaleStep;

                            Transform changed = s_FaceScaleDrag.initialTransform;
                            float* scaleValues[3] = { &changed.Scale.X, &changed.Scale.Y, &changed.Scale.Z };
                            const float* initialScaleValues[3] = {
                                &s_FaceScaleDrag.initialTransform.Scale.X,
                                &s_FaceScaleDrag.initialTransform.Scale.Y,
                                &s_FaceScaleDrag.initialTransform.Scale.Z
                            };
                            const int axis = s_FaceScaleDrag.axis;
                            const float initialAxisScale = std::abs(*initialScaleValues[axis]);
                            const float scaleDelta = s_FaceScaleDrag.sign * worldDelta * initialAxisScale /
                                (2.0f * s_FaceScaleDrag.boundsHalfExtents[axis] *
                                 std::max(0.00001f, s_FaceScaleDrag.worldScalePerLocalUnit));
                            const float newScale = std::max(0.01f,
                                *initialScaleValues[axis] + scaleDelta);
                            const float appliedScaleDelta = newScale - *initialScaleValues[axis];
                            *scaleValues[axis] = newScale;
                            const glm::quat initialRotation = glm::normalize(s_FaceScaleDrag.initialTransform.Rotation);
                            glm::vec3 localAxis(0.0f);
                            localAxis[axis] = 1.0f;
                            const glm::vec3 positionAxis = initialRotation * localAxis;
                            const float pivotShift = -(s_FaceScaleDrag.boundsCenter[axis] -
                                s_FaceScaleDrag.sign * s_FaceScaleDrag.boundsHalfExtents[axis]) * appliedScaleDelta;
                            changed.Position.X += positionAxis.x * pivotShift;
                            changed.Position.Y += positionAxis.y * pivotShift;
                            changed.Position.Z += positionAxis.z * pivotShift;
                            selected.GetTransform() = changed;
                            if (snapSettings.FaceSnapEnabled)
                            {
                                if (ApplyFaceDocking(app, selected))
                                {
                                    // Converte o pequeno ajuste do docking em escala da face
                                    // arrastada, em vez de deslocar a peça inteira. Assim a
                                    // face oposta continua ancorada durante o encaixe.
                                    Transform docked = selected.GetTransform();
                                    glm::vec3 dockShift(
                                        docked.Position.X - changed.Position.X,
                                        docked.Position.Y - changed.Position.Y,
                                        docked.Position.Z - changed.Position.Z);
                                    const glm::vec3 axisVector = s_FaceScaleDrag.worldAxis;
                                    const float axialShift = glm::dot(dockShift, axisVector);
                                    const float dockScaleDelta = axialShift /
                                        (2.0f * s_FaceScaleDrag.boundsHalfExtents[axis] *
                                         std::max(0.00001f, s_FaceScaleDrag.worldScalePerLocalUnit) *
                                         s_FaceScaleDrag.sign) *
                                        std::abs(*initialScaleValues[axis]);
                                    if (std::isfinite(dockScaleDelta) && std::abs(dockScaleDelta) > 0.000001f)
                                    {
                                        float* dockScaleValues[3] = {
                                            &docked.Scale.X, &docked.Scale.Y, &docked.Scale.Z
                                        };
                                        const float oldScale = *dockScaleValues[axis];
                                        const float newScale = std::max(0.01f, oldScale + dockScaleDelta);
                                        const float appliedDockScale = newScale - oldScale;
                                        *dockScaleValues[axis] = newScale;
                                        const float anchorCorrection =
                                            -(s_FaceScaleDrag.boundsCenter[axis] -
                                              s_FaceScaleDrag.sign * s_FaceScaleDrag.boundsHalfExtents[axis]) *
                                            appliedDockScale;
                                        const glm::vec3 lateralShift = dockShift - axisVector * axialShift;
                                        docked.Position.X = changed.Position.X + lateralShift.x + axisVector.x * anchorCorrection;
                                        docked.Position.Y = changed.Position.Y + lateralShift.y + axisVector.y * anchorCorrection;
                                        docked.Position.Z = changed.Position.Z + lateralShift.z + axisVector.z * anchorCorrection;
                                        selected.GetTransform() = docked;
                                        app.GetScene().UpdateTransforms();
                                    }
                                }
                            }
                            else
                                s_FaceDockingActive = false;
                            app.MarkDirty();
                        }
                        else
                        {
                            TransformCommand command;
                            command.Type = CommandType::Transform;
                            command.EntityID = s_FaceScaleDrag.entityId;
                            command.Before = s_FaceScaleDrag.initialTransform;
                            command.After = selected.GetTransform();
                            const bool changedTransform =
                                std::abs(command.Before.Position.X - command.After.Position.X) > 0.00001f ||
                                std::abs(command.Before.Position.Y - command.After.Position.Y) > 0.00001f ||
                                std::abs(command.Before.Position.Z - command.After.Position.Z) > 0.00001f ||
                                std::abs(command.Before.Scale.X - command.After.Scale.X) > 0.00001f ||
                                std::abs(command.Before.Scale.Y - command.After.Scale.Y) > 0.00001f ||
                                std::abs(command.Before.Scale.Z - command.After.Scale.Z) > 0.00001f;
                            if (changedTransform)
                                app.GetUndoRedoManager().Push(command);
                            s_FaceScaleDrag.active = false;
                            app.SetWasUsingGizmo(false);
                            s_FaceDockingActive = false;
                        }
                    }
                    else if (s_FaceScaleDrag.active)
                    {
                        // A seleção mudou no meio do arraste: encerra a captura
                        // para não deixar o estado do gizmo preso.
                        s_FaceScaleDrag.active = false;
                        app.SetWasUsingGizmo(false);
                    }
                    else if (app.WasUsingGizmo() && !ImGuizmo::IsUsing())
                    {
                        app.SetWasUsingGizmo(false);
                        s_FaceDockingActive = false;
                    }
                }
                else
                {
                    if (s_FaceScaleDrag.active)
                    {
                        s_FaceScaleDrag.active = false;
                        app.SetWasUsingGizmo(false);
                    }
                    if (gizmoMode == Application::GizmoMode::Translate ||
                        gizmoMode == Application::GizmoMode::Rotate ||
                        gizmoMode == Application::GizmoMode::Scale)
                    {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        const glm::vec3 position(gizmoTransform.Position.X, gizmoTransform.Position.Y, gizmoTransform.Position.Z);
                        const glm::quat rotation = glm::normalize(gizmoTransform.Rotation);
                        const glm::mat4 identity(1.0f);
                        glm::vec3 boundsMin, boundsMax;
                        GetLocalMeshBounds(selected, boundsMin, boundsMax);
                        // Mantém o gizmo com dimensões visuais consistentes, sem depender
                        // do tamanho da malha. A conversão muda com zoom/distância da câmera.
                        const float viewDepth = std::max(0.01f,
                            -glm::vec3(view * glm::vec4(position, 1.0f)).z);
                        const float worldUnitsPerPixel = viewportCamera.Orthographic
                            ? std::max(0.001f, viewportCamera.OrthographicSize) / viewportHeight
                            : (2.0f * viewDepth * std::tan(glm::radians(
                                std::clamp(viewportCamera.FieldOfView, 1.0f, 179.0f)) * 0.5f)) /
                                viewportHeight;
                        const float gizmoPixelSize = std::clamp(
                            std::min(viewportWidth, viewportHeight) * 0.11f, 72.0f, 108.0f);
                        const float gizmoUiScale = gizmoPixelSize / 92.0f;
                        const glm::vec3 cameraPosition(viewportCamera.Position.X,
                            viewportCamera.Position.Y, viewportCamera.Position.Z);
                        const glm::vec3 cameraToObject = glm::length(position - cameraPosition) > 0.0001f
                            ? glm::normalize(position - cameraPosition) : glm::vec3(0.0f, 0.0f, -1.0f);
                        ImVec2 centerScreen;
                        const ImVec2 mouse = ImGui::GetIO().MousePos;
                        const float axisColors[3][4] = {
                            { 0.95f, 0.20f, 0.18f, 1.0f },
                            { 0.20f, 0.90f, 0.30f, 1.0f },
                            { 0.20f, 0.48f, 1.00f, 1.0f }
                        };
                        const char* axisLabels[3] = { "X", "Y", "Z" };
                        const auto drawCollapsedAxisIndicator = [&](int axis, const ImVec2& center)
                        {
                            const ImVec2 offsets[3] = {
                                ImVec2(-13.0f, 0.0f), ImVec2(0.0f, -13.0f), ImVec2(13.0f, 0.0f)
                            };
                            const ImVec2 marker(center.x + offsets[axis].x,
                                                center.y + offsets[axis].y);
                            const ImU32 color = ImGui::ColorConvertFloat4ToU32(
                                ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 1.0f));
                            drawList->AddCircleFilled(marker, 8.0f, color);
                            drawList->AddCircle(marker, 8.0f, IM_COL32(20, 22, 26, 255), 0, 1.3f);
                            const ImVec2 labelSize = ImGui::CalcTextSize(axisLabels[axis]);
                            drawList->AddText(ImVec2(marker.x - labelSize.x * 0.5f,
                                marker.y - labelSize.y * 0.5f), IM_COL32(255, 255, 255, 255), axisLabels[axis]);
                        };
                        bool hovered = false;
                        int hoveredAxis = -1;
                        int hoveredAxis2 = -1;
                        bool hoveredPlane = false;
                        glm::vec3 hoveredWorldAxis(1.0f, 0.0f, 0.0f);
                        glm::vec3 hoveredWorldAxis2(0.0f, 1.0f, 0.0f);
                        ImVec2 hoveredScreenAxis(1.0f, 0.0f);
                        ImVec2 hoveredPlaneScreenU(1.0f, 0.0f);
                        ImVec2 hoveredPlaneScreenV(0.0f, 1.0f);
                        bool hoveredFreeMove = false;
                        float hoveredPixelsPerUnit = 1.0f;
                        float hoveredStartAngle = 0.0f;
                        float hoveredRotationDirection = 1.0f;
                        const bool centerVisible = ProjectPoint(view, projection, identity,
                            position, imageMin, imageMax, centerScreen);

                        if (centerVisible)
                        {
                            for (int axis = 0; axis < 3; ++axis)
                            {
                                // O mundo 2D opera no plano XY: movimento/escala
                                // em Z não fazem parte do gizmo desse modo.
                                if (is2DWorld && gizmoMode != Application::GizmoMode::Rotate && axis == 2)
                                    continue;
                                // Rotação em torno de Z mantém o objeto no plano 2D;
                                // rotações em X/Y o inclinariam para fora dele.
                                if (is2DWorld && gizmoMode == Application::GizmoMode::Rotate && axis != 2)
                                    continue;
                                glm::vec3 localAxis(0.0f);
                                localAxis[axis] = 1.0f;
                                const glm::vec3 worldAxis = gizmoSpace == ImGuizmo::LOCAL
                                    ? rotation * localAxis : localAxis;
                                const ImU32 axisColor = ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(axisColors[axis][0], axisColors[axis][1], axisColors[axis][2], 1.0f));

                                if (gizmoMode == Application::GizmoMode::Translate)
                                {
                                    const float handleLength = worldUnitsPerPixel * gizmoPixelSize;
                                    ImVec2 end;
                                    if (!ProjectPoint(view, projection, identity,
                                        position + worldAxis * handleLength, imageMin, imageMax, end))
                                        continue;
                                    const float dx = end.x - centerScreen.x;
                                    const float dy = end.y - centerScreen.y;
                                    const float pixels = std::sqrt(dx * dx + dy * dy);
                                    if (pixels < 8.0f)
                                    {
                                        drawCollapsedAxisIndicator(axis, centerScreen);
                                        continue;
                                    }
                                    const ImVec2 direction(dx / pixels, dy / pixels);
                                    const ImVec2 perpendicular(-direction.y, direction.x);
                                    const ImVec2 tip(end.x, end.y);
                                    const ImVec2 base(end.x - direction.x * 15.0f, end.y - direction.y * 15.0f);
                                    const float hitDistance = PointSegmentDistanceSquared(mouse, centerScreen, end);
                                    const float hitRadius = std::max(10.0f, 10.0f * gizmoUiScale);
                                    const bool hit = imageHovered && hitDistance < hitRadius * hitRadius;
                                    const ImU32 visibleColor = hit ? IM_COL32(255, 255, 255, 255) : axisColor;
                                    drawList->AddLine(centerScreen, base, axisColor, hit ? 4.5f : 3.0f);
                                    const float arrowHalfWidth = (hit ? 8.0f : 6.5f) * gizmoUiScale;
                                    const ImVec2 arrowLeft(base.x + perpendicular.x * arrowHalfWidth,
                                                          base.y + perpendicular.y * arrowHalfWidth);
                                    const ImVec2 arrowRight(base.x - perpendicular.x * arrowHalfWidth,
                                                           base.y - perpendicular.y * arrowHalfWidth);
                                    drawList->AddTriangleFilled(tip, arrowLeft, arrowRight, visibleColor);
                                    drawList->AddTriangle(tip, arrowLeft, arrowRight,
                                        IM_COL32(20, 22, 26, 255), 1.5f);
                                    drawList->AddText(ImVec2(end.x + 5.0f, end.y - 8.0f),
                                        hit ? IM_COL32(255, 255, 255, 255) : axisColor, axisLabels[axis]);
                                    if (hit)
                                    {
                                        hovered = true;
                                        hoveredAxis = axis;
                                        hoveredWorldAxis = worldAxis;
                                        hoveredScreenAxis = direction;
                                        hoveredPixelsPerUnit = pixels / handleLength;
                                    }
                                }
                                else if (gizmoMode == Application::GizmoMode::Rotate)
                                {
                                    // Anel de rotação: círculo no plano perpendicular ao eixo,
                                    // com o mesmo código de cores e destaque das alças de escala.
                                    glm::vec3 helper = std::abs(worldAxis.y) < 0.85f
                                        ? glm::vec3(0.0f, 1.0f, 0.0f)
                                        : glm::vec3(1.0f, 0.0f, 0.0f);
                                    const glm::vec3 basisU = glm::normalize(glm::cross(worldAxis, helper));
                                    const glm::vec3 basisV = glm::normalize(glm::cross(worldAxis, basisU));
                                    const float ringRadius = worldUnitsPerPixel * gizmoPixelSize * (68.0f / 92.0f);
                                    constexpr int segments = 64;
                                    ImVec2 previous{};
                                    bool havePrevious = false;
                                    const float ringHitRadius = std::max(9.0f, 8.0f * gizmoUiScale);
                                    float nearestDistance = ringHitRadius * ringHitRadius;
                                    ImVec2 nearestPoint{};
                                    for (int segment = 0; segment <= segments; ++segment)
                                    {
                                        const float angle = (2.0f * 3.1415926535f * segment) / segments;
                                        const glm::vec3 ringPoint = position + ringRadius *
                                            (basisU * std::cos(angle) + basisV * std::sin(angle));
                                        ImVec2 projected;
                                        if (!ProjectPoint(view, projection, identity, ringPoint,
                                            imageMin, imageMax, projected))
                                        {
                                            havePrevious = false;
                                            continue;
                                        }
                                        if (havePrevious)
                                        {
                                            drawList->AddLine(previous, projected, axisColor, 2.5f);
                                            const float distance = PointSegmentDistanceSquared(mouse, previous, projected);
                                            if (distance < nearestDistance)
                                            {
                                                nearestDistance = distance;
                                                nearestPoint = ImVec2((previous.x + projected.x) * 0.5f,
                                                                      (previous.y + projected.y) * 0.5f);
                                            }
                                        }
                                        previous = projected;
                                        havePrevious = true;
                                    }
                                    const bool hit = imageHovered && nearestDistance < ringHitRadius * ringHitRadius;
                                    ImVec2 axisLabelPoint;
                                    if (ProjectPoint(view, projection, identity,
                                            position + ringRadius * (basisU * 0.7071f + basisV * 0.7071f),
                                            imageMin, imageMax, axisLabelPoint))
                                        drawList->AddText(ImVec2(axisLabelPoint.x + 4.0f, axisLabelPoint.y - 8.0f),
                                            hit ? IM_COL32(255, 255, 255, 255) : axisColor, axisLabels[axis]);
                                    if (hit)
                                    {
                                        hovered = true;
                                        hoveredAxis = axis;
                                        hoveredWorldAxis = worldAxis;
                                        hoveredStartAngle = std::atan2(mouse.y - centerScreen.y,
                                                                      mouse.x - centerScreen.x);
                                        ImVec2 projectedU, projectedV;
                                        if (ProjectPoint(view, projection, identity,
                                                position + basisU * ringRadius, imageMin, imageMax, projectedU) &&
                                            ProjectPoint(view, projection, identity,
                                                position + basisV * ringRadius, imageMin, imageMax, projectedV))
                                        {
                                            const float ux = projectedU.x - centerScreen.x;
                                            const float uy = projectedU.y - centerScreen.y;
                                            const float vx = projectedV.x - centerScreen.x;
                                            const float vy = projectedV.y - centerScreen.y;
                                            const float projectedOrientation = ux * vy - uy * vx;
                                            if (std::abs(projectedOrientation) > 0.0001f)
                                                hoveredRotationDirection = projectedOrientation > 0.0f ? 1.0f : -1.0f;
                                        }
                                    }
                                    if (hit)
                                    {
                                        // Realça o anel inteiro enquanto o cursor está sobre ele.
                                        ImVec2 highlightPrevious{};
                                        bool highlightHasPrevious = false;
                                        for (int segment = 0; segment <= segments; ++segment)
                                        {
                                            const float angle = (2.0f * 3.1415926535f * segment) / segments;
                                            ImVec2 projected;
                                            if (!ProjectPoint(view, projection, identity,
                                                position + ringRadius * (basisU * std::cos(angle) +
                                                    basisV * std::sin(angle)), imageMin, imageMax, projected))
                                            {
                                                highlightHasPrevious = false;
                                                continue;
                                            }
                                            if (highlightHasPrevious)
                                                drawList->AddLine(highlightPrevious, projected,
                                                    IM_COL32(255, 255, 255, 235), 3.5f);
                                            highlightPrevious = projected;
                                            highlightHasPrevious = true;
                                        }
                                        drawList->AddCircleFilled(nearestPoint, 7.5f,
                                            IM_COL32(255, 255, 255, 255));
                                    }
                                }
                                else
                                {
                                    // Alça quadrada: mantém o código RGB por eixo do mover,
                                    // mas com uma silhueta própria para escala.
                                    const float handleLength = worldUnitsPerPixel * gizmoPixelSize;
                                    ImVec2 end;
                                    if (!ProjectPoint(view, projection, identity,
                                        position + worldAxis * handleLength, imageMin, imageMax, end))
                                        continue;
                                    const float dx = end.x - centerScreen.x;
                                    const float dy = end.y - centerScreen.y;
                                    const float pixels = std::sqrt(dx * dx + dy * dy);
                                    if (pixels < 8.0f)
                                    {
                                        drawCollapsedAxisIndicator(axis, centerScreen);
                                        continue;
                                    }
                                    const ImVec2 direction(dx / pixels, dy / pixels);
                                    const float handleHalfSize = 7.0f * gizmoUiScale;
                                    const float hitHalfSize = std::max(11.0f, 10.0f * gizmoUiScale);
                                    const bool hit = imageHovered &&
                                        std::abs(mouse.x - end.x) <= hitHalfSize &&
                                        std::abs(mouse.y - end.y) <= hitHalfSize;
                                    const ImU32 visibleColor = hit
                                        ? IM_COL32(255, 255, 255, 255) : axisColor;
                                    drawList->AddLine(centerScreen, end, axisColor, hit ? 4.0f : 2.8f);
                                    const ImVec2 squareMin(end.x - handleHalfSize, end.y - handleHalfSize);
                                    const ImVec2 squareMax(end.x + handleHalfSize, end.y + handleHalfSize);
                                    drawList->AddRectFilled(squareMin, squareMax, visibleColor, 2.0f);
                                    drawList->AddRect(squareMin, squareMax,
                                        IM_COL32(20, 22, 26, 255), 2.0f, 0, 1.5f);
                                    drawList->AddText(ImVec2(end.x + handleHalfSize + 3.0f,
                                        end.y - 8.0f), hit ? IM_COL32(255, 255, 255, 255) : axisColor,
                                        axisLabels[axis]);
                                    if (hit)
                                    {
                                        hovered = true;
                                        hoveredAxis = axis;
                                        hoveredWorldAxis = worldAxis;
                                        hoveredScreenAxis = direction;
                                        hoveredPixelsPerUnit = pixels / handleLength;
                                    }
                                }
                            }
                            if (gizmoMode == Application::GizmoMode::Rotate && !is2DWorld)
                            {
                                // Anel externo no plano da câmera para rotação livre, presente
                                // no gizmo clássico e útil quando os anéis dos eixos se cruzam.
                                const float viewRingRadius = gizmoPixelSize * (82.0f / 92.0f);
                                drawList->AddCircle(centerScreen, viewRingRadius,
                                    IM_COL32(205, 212, 224, 220), 72, 2.0f);
                                drawList->AddText(ImVec2(centerScreen.x - 14.0f,
                                    centerScreen.y - viewRingRadius - 17.0f),
                                    IM_COL32(205, 212, 224, 220), UI("Livre"));
                                const float radialDistance = std::sqrt(
                                    (mouse.x - centerScreen.x) * (mouse.x - centerScreen.x) +
                                    (mouse.y - centerScreen.y) * (mouse.y - centerScreen.y));
                                if (imageHovered && std::abs(radialDistance - viewRingRadius) <
                                    std::max(9.0f, 8.0f * gizmoUiScale))
                                {
                                    hovered = true;
                                    hoveredAxis = 3;
                                    hoveredWorldAxis = -cameraToObject;
                                    hoveredStartAngle = std::atan2(mouse.y - centerScreen.y,
                                        mouse.x - centerScreen.x);
                                    // Screen Y grows downward; the camera-facing axis
                                    // therefore rotates opposite to the screen angle.
                                    hoveredRotationDirection = -1.0f;
                                    drawList->AddCircle(centerScreen, viewRingRadius,
                                        IM_COL32(255, 255, 255, 255), 72, 3.5f);
                                }
                            }
                            if (gizmoMode == Application::GizmoMode::Translate)
                            {
                                // Alças planares (XY, XZ, YZ): áreas pequenas e visíveis
                                // entre as setas, com captura precisamente sob o quadrado.
                                for (int axisU = 0; axisU < 3; ++axisU)
                                {
                                    for (int axisV = axisU + 1; axisV < 3; ++axisV)
                                    {
                                        if (is2DWorld && axisV == 2)
                                            continue;
                                        glm::vec3 localU(0.0f), localV(0.0f);
                                        localU[axisU] = 1.0f;
                                        localV[axisV] = 1.0f;
                                        const glm::vec3 worldU = gizmoSpace == ImGuizmo::LOCAL ? rotation * localU : localU;
                                        const glm::vec3 worldV = gizmoSpace == ImGuizmo::LOCAL ? rotation * localV : localV;
                                        ImVec2 endU, endV;
                                        const float handleLength = worldUnitsPerPixel * gizmoPixelSize;
                                        if (!ProjectPoint(view, projection, identity, position + worldU * handleLength,
                                                imageMin, imageMax, endU) ||
                                            !ProjectPoint(view, projection, identity, position + worldV * handleLength,
                                                imageMin, imageMax, endV))
                                            continue;
                                        const ImVec2 dirU((endU.x - centerScreen.x) / handleLength,
                                                          (endU.y - centerScreen.y) / handleLength);
                                        const ImVec2 dirV((endV.x - centerScreen.x) / handleLength,
                                                          (endV.y - centerScreen.y) / handleLength);
                                        const float screenDeterminant = dirU.x * dirV.y - dirV.x * dirU.y;
                                        if (std::abs(screenDeterminant) < 0.5f)
                                            continue; // Não oferece uma alça instável vista quase de lado.
                                        const ImVec2 handle(centerScreen.x + dirU.x * handleLength * 0.29f + dirV.x * handleLength * 0.29f,
                                                            centerScreen.y + dirU.y * handleLength * 0.29f + dirV.y * handleLength * 0.29f);
                                        const ImVec2 halfU(dirU.x * handleLength * 0.10f, dirU.y * handleLength * 0.10f);
                                        const ImVec2 halfV(dirV.x * handleLength * 0.10f, dirV.y * handleLength * 0.10f);
                                        const ImVec2 corners[4] = {
                                            ImVec2(handle.x - halfU.x - halfV.x, handle.y - halfU.y - halfV.y),
                                            ImVec2(handle.x + halfU.x - halfV.x, handle.y + halfU.y - halfV.y),
                                            ImVec2(handle.x + halfU.x + halfV.x, handle.y + halfU.y + halfV.y),
                                            ImVec2(handle.x - halfU.x + halfV.x, handle.y - halfU.y + halfV.y)
                                        };
                                        const float planeHitRadius = std::max(9.0f, 8.0f * gizmoUiScale);
                                        const bool hit = imageHovered && std::abs(mouse.x - handle.x) <= planeHitRadius &&
                                            std::abs(mouse.y - handle.y) <= planeHitRadius;
                                        const ImU32 tint = hit ? IM_COL32(255, 255, 255, 235)
                                            : IM_COL32(160, 190, 225, 155);
                                        drawList->AddConvexPolyFilled(corners, 4, tint);
                                        drawList->AddPolyline(corners, 4, IM_COL32(25, 28, 34, 230), ImDrawFlags_Closed, 1.2f);
                                        if (hit)
                                        {
                                            hovered = true;
                                            hoveredPlane = true;
                                            hoveredAxis = axisU;
                                            hoveredAxis2 = axisV;
                                            hoveredWorldAxis = worldU;
                                            hoveredWorldAxis2 = worldV;
                                            hoveredPlaneScreenU = dirU;
                                            hoveredPlaneScreenV = dirV;
                                        }
                                    }
                                }
                            }
                            if (gizmoMode == Application::GizmoMode::Translate)
                            {
                                // Esfera central: arraste livre no plano da câmera,
                                // como a alça universal de movimento de outras engines.
                                const glm::mat4 cameraWorld = glm::inverse(view);
                                const glm::vec3 cameraRight = glm::normalize(glm::vec3(cameraWorld[0]));
                                const glm::vec3 cameraUp = glm::normalize(glm::vec3(cameraWorld[1]));
                                const float probeLength = worldUnitsPerPixel * gizmoPixelSize;
                                ImVec2 projectedRight, projectedUp;
                                if (probeLength > 0.0001f &&
                                    ProjectPoint(view, projection, identity,
                                        position + cameraRight * probeLength, imageMin, imageMax, projectedRight) &&
                                    ProjectPoint(view, projection, identity,
                                        position + cameraUp * probeLength, imageMin, imageMax, projectedUp))
                                {
                                    const ImVec2 screenU(
                                        (projectedRight.x - centerScreen.x) / probeLength,
                                        (projectedRight.y - centerScreen.y) / probeLength);
                                    const ImVec2 screenV(
                                        (projectedUp.x - centerScreen.x) / probeLength,
                                        (projectedUp.y - centerScreen.y) / probeLength);
                                    const float determinant = screenU.x * screenV.y - screenV.x * screenU.y;
                                    const float dx = mouse.x - centerScreen.x;
                                    const float dy = mouse.y - centerScreen.y;
                                    if (std::abs(determinant) > 0.0001f && imageHovered &&
                                        dx * dx + dy * dy <= 11.0f * 11.0f)
                                    {
                                        hovered = true;
                                        hoveredPlane = true;
                                        hoveredFreeMove = true;
                                        hoveredAxis = -1;
                                        hoveredAxis2 = -1;
                                        hoveredWorldAxis = cameraRight;
                                        hoveredWorldAxis2 = cameraUp;
                                        hoveredPlaneScreenU = screenU;
                                        hoveredPlaneScreenV = screenV;
                                    }
                                }
                            }
                            if (gizmoMode == Application::GizmoMode::Scale &&
                                imageHovered && std::abs(mouse.x - centerScreen.x) <= 8.0f &&
                                std::abs(mouse.y - centerScreen.y) <= 8.0f)
                            {
                                // O centro faz escala uniforme; as alças RGB continuam
                                // reservadas à escala de um eixo.
                                hovered = true;
                                hoveredPlane = false;
                                hoveredAxis = -2;
                                drawList->AddRectFilled(
                                    ImVec2(centerScreen.x - 7.0f, centerScreen.y - 7.0f),
                                    ImVec2(centerScreen.x + 7.0f, centerScreen.y + 7.0f),
                                    IM_COL32(255, 255, 255, 255), 2.0f);
                            }
                            if (gizmoMode == Application::GizmoMode::Translate)
                            {
                                // Volume, contorno e brilho deixam a pega central fácil de
                                // distinguir das alças planares e do pivô simples.
                                drawList->AddCircleFilled(centerScreen, hoveredFreeMove ? 13.0f : 11.0f,
                                    IM_COL32(60, 145, 235, hoveredFreeMove ? 100 : 55));
                                drawList->AddCircleFilled(centerScreen, hoveredFreeMove ? 9.0f : 8.0f,
                                    hoveredFreeMove ? IM_COL32(245, 250, 255, 255) : IM_COL32(105, 180, 250, 245));
                                drawList->AddCircle(centerScreen, hoveredFreeMove ? 9.0f : 8.0f,
                                    IM_COL32(25, 32, 44, 255), 0, 1.5f);
                                drawList->AddCircleFilled(
                                    ImVec2(centerScreen.x - 2.5f, centerScreen.y - 2.5f), 2.3f,
                                    IM_COL32(255, 255, 255, 235));
                            }
                            else
                            {
                                drawList->AddCircleFilled(centerScreen, 5.0f, IM_COL32(245, 248, 255, 245));
                                drawList->AddCircle(centerScreen, 5.0f, IM_COL32(25, 28, 34, 255), 0, 1.2f);
                            }
                            if (gizmoMode == Application::GizmoMode::Scale)
                            {
                                const ImVec2 centerMin(centerScreen.x - 6.5f, centerScreen.y - 6.5f);
                                const ImVec2 centerMax(centerScreen.x + 6.5f, centerScreen.y + 6.5f);
                                const bool centerHit = imageHovered && std::abs(mouse.x - centerScreen.x) <= 8.0f &&
                                    std::abs(mouse.y - centerScreen.y) <= 8.0f;
                                drawList->AddRectFilled(centerMin, centerMax,
                                    centerHit ? IM_COL32(255, 255, 255, 255) : IM_COL32(238, 242, 250, 245), 2.0f);
                                drawList->AddRect(centerMin, centerMax, IM_COL32(25, 28, 34, 255), 2.0f, 0, 1.2f);
                            }
                        }

                        if (hovered && imageHovered && !s_AxisGizmoDrag.active)
                        {
                            if (gizmoMode == Application::GizmoMode::Rotate && hoveredAxis == 3)
                                ImGui::SetTooltip(UI("Rotação livre pela câmera · arraste para girar"));
                            else if (gizmoMode == Application::GizmoMode::Scale && hoveredAxis == -2)
                                ImGui::SetTooltip(UI("Escala uniforme · arraste para aumentar ou reduzir"));
                            else if (hoveredFreeMove)
                                ImGui::SetTooltip(UI("Mover livre no plano da câmera · arraste"));
                            else if (hoveredPlane)
                                ImGui::SetTooltip(UI("Mover no plano %s%s · arraste"),
                                    axisLabels[hoveredAxis], axisLabels[hoveredAxis2]);
                            else if (hoveredAxis >= 0 && hoveredAxis < 3)
                            {
                                const char* action = gizmoMode == Application::GizmoMode::Translate
                                    ? UI("Mover")
                                    : gizmoMode == Application::GizmoMode::Rotate
                                        ? UI("Girar") : UI("Escalar");
                                ImGui::SetTooltip("%s %s · arraste", action, axisLabels[hoveredAxis]);
                            }
                        }

                        if (!s_AxisGizmoDrag.active && hovered &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            s_AxisGizmoDrag.active = true;
                            s_AxisGizmoDrag.entityId = selected.GetID();
                            s_AxisGizmoDrag.group = isGroupSelection;
                            s_AxisGizmoDrag.groupIndices = isGroupSelection ? groupIndices : std::vector<int>{};
                            s_AxisGizmoDrag.groupInitialTransforms.clear();
                            s_AxisGizmoDrag.groupInitialModels.clear();
                            if (isGroupSelection)
                            {
                                s_AxisGizmoDrag.groupInitialTransforms.reserve(groupIndices.size());
                                s_AxisGizmoDrag.groupInitialModels.reserve(groupIndices.size());
                                for (int index : groupIndices)
                                {
                                    Entity& item = const_cast<Entity&>(entities[static_cast<size_t>(index)]);
                                    s_AxisGizmoDrag.groupInitialTransforms.push_back(item.GetTransform());
                                    s_AxisGizmoDrag.groupInitialModels.push_back(GetEntityWorldMatrix(item));
                                }
                            }
                            s_AxisGizmoDrag.axis = hoveredAxis;
                            s_AxisGizmoDrag.axis2 = hoveredAxis2;
                            s_AxisGizmoDrag.plane = hoveredPlane;
                            s_AxisGizmoDrag.worldAxis2 = hoveredWorldAxis2;
                            s_AxisGizmoDrag.planeScreenU = hoveredPlaneScreenU;
                            s_AxisGizmoDrag.planeScreenV = hoveredPlaneScreenV;
                            s_AxisGizmoDrag.mode = gizmoMode;
                            s_AxisGizmoDrag.startMouse = mouse;
                            s_AxisGizmoDrag.centerScreen = centerScreen;
                            s_AxisGizmoDrag.screenAxis = hoveredScreenAxis;
                            s_AxisGizmoDrag.pixelsPerUnit = hoveredPixelsPerUnit;
                            s_AxisGizmoDrag.startAngle = gizmoMode == Application::GizmoMode::Rotate
                                ? hoveredStartAngle
                                : std::atan2(mouse.y - centerScreen.y, mouse.x - centerScreen.x);
                            s_AxisGizmoDrag.lastAngle = s_AxisGizmoDrag.startAngle;
                            s_AxisGizmoDrag.accumulatedRotation = 0.0f;
                            s_AxisGizmoDrag.rotationDirection = hoveredRotationDirection;
                            s_AxisGizmoDrag.worldAxis = hoveredWorldAxis;
                            s_AxisGizmoDrag.initialTransform = gizmoTransform;
                            s_AxisGizmoDrag.initialLocalTransform = selected.GetTransform();
                            s_AxisGizmoDrag.hasModelChildren = hasChildren;
                            s_AxisGizmoDrag.initialParentModel = selected.GetTransform().WorldMatrix;
                            app.m_BeforeTransform = selected.GetTransform();
                            app.SetWasUsingGizmo(true);
                            s_FaceDockingActive = false;
                        }

                        if (s_AxisGizmoDrag.active && s_AxisGizmoDrag.entityId == selected.GetID() &&
                            s_AxisGizmoDrag.mode == gizmoMode && s_AxisGizmoDrag.group == isGroupSelection &&
                            (!isGroupSelection || s_AxisGizmoDrag.groupIndices == groupIndices))
                        {
                            gizmoUsing = true;
                            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                            {
                                Transform changed = s_AxisGizmoDrag.initialTransform;
                                if (gizmoMode == Application::GizmoMode::Translate)
                                {
                                    glm::vec3 displacement(0.0f);
                                    if (s_AxisGizmoDrag.plane)
                                    {
                                        const float dx = mouse.x - s_AxisGizmoDrag.startMouse.x;
                                        const float dy = mouse.y - s_AxisGizmoDrag.startMouse.y;
                                        const float determinant = s_AxisGizmoDrag.planeScreenU.x * s_AxisGizmoDrag.planeScreenV.y -
                                            s_AxisGizmoDrag.planeScreenV.x * s_AxisGizmoDrag.planeScreenU.y;
                                        if (std::abs(determinant) > 0.0001f)
                                        {
                                            float amountU = (dx * s_AxisGizmoDrag.planeScreenV.y -
                                                dy * s_AxisGizmoDrag.planeScreenV.x) / determinant;
                                            float amountV = (dy * s_AxisGizmoDrag.planeScreenU.x -
                                                dx * s_AxisGizmoDrag.planeScreenU.y) / determinant;
                                            if (snapSettings.PositionEnabled && snapSettings.PositionStep > 0.0f)
                                            {
                                                amountU = std::round(amountU / snapSettings.PositionStep) * snapSettings.PositionStep;
                                                amountV = std::round(amountV / snapSettings.PositionStep) * snapSettings.PositionStep;
                                            }
                                            displacement = s_AxisGizmoDrag.worldAxis * amountU +
                                                s_AxisGizmoDrag.worldAxis2 * amountV;
                                        }
                                    }
                                    else
                                    {
                                        const float deltaPixels =
                                            (mouse.x - s_AxisGizmoDrag.startMouse.x) * s_AxisGizmoDrag.screenAxis.x +
                                            (mouse.y - s_AxisGizmoDrag.startMouse.y) * s_AxisGizmoDrag.screenAxis.y;
                                        float delta = deltaPixels / std::max(0.5f, s_AxisGizmoDrag.pixelsPerUnit);
                                        if (snapSettings.PositionEnabled && snapSettings.PositionStep > 0.0f)
                                            delta = std::round(delta / snapSettings.PositionStep) * snapSettings.PositionStep;
                                        displacement = s_AxisGizmoDrag.worldAxis * delta;
                                    }
                                    const glm::vec3 moved = glm::vec3(changed.Position.X, changed.Position.Y,
                                        changed.Position.Z) + displacement;
                                    changed.Position = Vector3(moved.x, moved.y, moved.z);
                                }
                                else if (gizmoMode == Application::GizmoMode::Rotate)
                                {
                                    const float currentAngle = std::atan2(mouse.y - s_AxisGizmoDrag.centerScreen.y,
                                                                          mouse.x - s_AxisGizmoDrag.centerScreen.x);
                                    // Soma incrementos curtos para permitir giros contínuos
                                    // sem salto ao cruzar o limite de ±180 graus.
                                    const float angleStep = std::atan2(
                                        std::sin(currentAngle - s_AxisGizmoDrag.lastAngle),
                                        std::cos(currentAngle - s_AxisGizmoDrag.lastAngle));
                                    s_AxisGizmoDrag.lastAngle = currentAngle;
                                    s_AxisGizmoDrag.accumulatedRotation +=
                                        s_AxisGizmoDrag.rotationDirection * angleStep;
                                    float delta = s_AxisGizmoDrag.accumulatedRotation;
                                    if (snapSettings.RotationEnabled && snapSettings.RotationStep > 0.0f)
                                    {
                                        const float step = glm::radians(snapSettings.RotationStep);
                                        delta = std::round(delta / step) * step;
                                    }
                                    const glm::quat initialRotation = glm::normalize(s_AxisGizmoDrag.initialTransform.Rotation);
                                    glm::vec3 rotationAxis = s_AxisGizmoDrag.worldAxis;
                                    if (gizmoSpace == ImGuizmo::LOCAL && s_AxisGizmoDrag.axis >= 0 &&
                                        s_AxisGizmoDrag.axis < 3)
                                    {
                                        rotationAxis = glm::vec3(0.0f);
                                        rotationAxis[s_AxisGizmoDrag.axis] = 1.0f;
                                    }
                                    const glm::quat deltaRotation = glm::angleAxis(delta,
                                        glm::normalize(rotationAxis));
                                    changed.Rotation = gizmoSpace == ImGuizmo::LOCAL
                                        ? glm::normalize(initialRotation * deltaRotation)
                                        : glm::normalize(deltaRotation * initialRotation);
                                    const glm::vec3 euler = glm::degrees(glm::eulerAngles(changed.Rotation));
                                    changed.EulerRotation = Vector3(euler.x, euler.y, euler.z);
                                }
                                else
                                {
                                    // Escala usa as mesmas alças axiais coloridas do novo gizmo.
                                    // O deslocamento é convertido em unidades de escala usando o
                                    // tamanho projetado do eixo, mantendo a escala independente do zoom.
                                    const float deltaPixels =
                                        (mouse.x - s_AxisGizmoDrag.startMouse.x) * s_AxisGizmoDrag.screenAxis.x +
                                        (mouse.y - s_AxisGizmoDrag.startMouse.y) * s_AxisGizmoDrag.screenAxis.y;
                                    if (s_AxisGizmoDrag.axis == -2)
                                    {
                                        const float factor = std::exp(std::clamp(
                                            (s_AxisGizmoDrag.startMouse.y - mouse.y) / gizmoPixelSize,
                                            -6.0f, 6.0f));
                                        changed.Scale = Vector3(
                                            std::max(0.01f, s_AxisGizmoDrag.initialTransform.Scale.X * factor),
                                            std::max(0.01f, s_AxisGizmoDrag.initialTransform.Scale.Y * factor),
                                            is2DWorld
                                                ? s_AxisGizmoDrag.initialTransform.Scale.Z
                                                : std::max(0.01f, s_AxisGizmoDrag.initialTransform.Scale.Z * factor));
                                    }
                                    else
                                    {
                                    const float halfExtent = std::max(0.001f,
                                        (boundsMax[s_AxisGizmoDrag.axis] - boundsMin[s_AxisGizmoDrag.axis]) * 0.5f);
                                    float scaleDelta = deltaPixels /
                                        (std::max(0.5f, s_AxisGizmoDrag.pixelsPerUnit) * 2.0f * halfExtent);
                                    float* scaleValues[3] = { &changed.Scale.X, &changed.Scale.Y, &changed.Scale.Z };
                                    const float initialScaleValues[3] = {
                                        s_AxisGizmoDrag.initialTransform.Scale.X,
                                        s_AxisGizmoDrag.initialTransform.Scale.Y,
                                        s_AxisGizmoDrag.initialTransform.Scale.Z
                                    };
                                    float nextScale = std::max(0.01f,
                                        initialScaleValues[s_AxisGizmoDrag.axis] + scaleDelta);
                                    if (snapSettings.ScaleEnabled && snapSettings.ScaleStep > 0.0f)
                                        nextScale = std::max(0.01f,
                                            std::round(nextScale / snapSettings.ScaleStep) * snapSettings.ScaleStep);
                                    *scaleValues[s_AxisGizmoDrag.axis] = nextScale;
                                    }
                                }
                                if (isGroupSelection)
                                {
                                    const glm::mat4 pivotInitial = BuildModel(s_AxisGizmoDrag.initialTransform);
                                    const glm::mat4 pivotChanged = BuildModel(changed);
                                    const glm::mat4 delta = pivotChanged * glm::inverse(pivotInitial);
                                    for (size_t i = 0; i < s_AxisGizmoDrag.groupIndices.size(); ++i)
                                    {
                                        const int index = s_AxisGizmoDrag.groupIndices[i];
                                        if (index < 0 || index >= static_cast<int>(entities.size())) continue;
                                        Entity& item = const_cast<Entity&>(entities[static_cast<size_t>(index)]);
                                        ApplyWorldGizmoMatrix(app.GetScene(), item, delta * s_AxisGizmoDrag.groupInitialModels[i]);
                                    }
                                    app.GetScene().UpdateTransforms();
                                }
                                else if (s_AxisGizmoDrag.hasModelChildren)
                                {
                                    const glm::mat4 pivotInitial = BuildModel(s_AxisGizmoDrag.initialTransform);
                                    const glm::mat4 pivotChanged = BuildModel(changed);
                                    const glm::mat4 delta = pivotChanged * glm::inverse(pivotInitial);
                                    ApplyWorldGizmoMatrix(app.GetScene(), selected, delta * s_AxisGizmoDrag.initialParentModel);
                                    app.GetScene().UpdateTransforms();
                                }
                                else
                                {
                                    ApplyWorldGizmoMatrix(app.GetScene(), selected, BuildModel(changed));
                                    app.GetScene().UpdateTransforms();
                                }
                                if (!isGroupSelection && gizmoMode == Application::GizmoMode::Translate)
                                {
                                    if (s_AxisGizmoDrag.plane)
                                        ApplyFaceDocking(app, selected, nullptr);
                                    else
                                        ApplyFaceDocking(app, selected, &s_AxisGizmoDrag.worldAxis);
                                }
                                app.MarkDirty();

                                char readout[160]{};
                                if (gizmoMode == Application::GizmoMode::Translate)
                                {
                                    const glm::vec3 initial(s_AxisGizmoDrag.initialTransform.Position.X,
                                        s_AxisGizmoDrag.initialTransform.Position.Y,
                                        s_AxisGizmoDrag.initialTransform.Position.Z);
                                    const glm::vec3 current(changed.Position.X, changed.Position.Y,
                                        changed.Position.Z);
                                    const glm::vec3 delta = current - initial;
                                    if (is2DWorld)
                                        std::snprintf(readout, sizeof(readout),
                                            "Mover · ΔX %+.2f m   ΔY %+.2f m", delta.x, delta.y);
                                    else
                                        std::snprintf(readout, sizeof(readout),
                                            "Mover · ΔX %+.2f m   ΔY %+.2f m   ΔZ %+.2f m",
                                            delta.x, delta.y, delta.z);
                                }
                                else if (gizmoMode == Application::GizmoMode::Rotate)
                                {
                                    const char* axisName = s_AxisGizmoDrag.axis == 3
                                        ? UI("Câmera")
                                        : s_AxisGizmoDrag.axis >= 0 && s_AxisGizmoDrag.axis < 3
                                            ? axisLabels[s_AxisGizmoDrag.axis] : UI("Eixos");
                                    std::snprintf(readout, sizeof(readout), "Girar %s · %+.1f°",
                                        axisName, glm::degrees(s_AxisGizmoDrag.accumulatedRotation));
                                }
                                else if (s_AxisGizmoDrag.axis == -2)
                                {
                                    const float initialScale = std::max(0.001f,
                                        s_AxisGizmoDrag.initialTransform.Scale.X);
                                    std::snprintf(readout, sizeof(readout), "Escala uniforme · %.2f×",
                                        changed.Scale.X / initialScale);
                                }
                                else if (s_AxisGizmoDrag.axis >= 0 && s_AxisGizmoDrag.axis < 3)
                                {
                                    const float initialValues[3] = {
                                        s_AxisGizmoDrag.initialTransform.Scale.X,
                                        s_AxisGizmoDrag.initialTransform.Scale.Y,
                                        s_AxisGizmoDrag.initialTransform.Scale.Z
                                    };
                                    const float currentValues[3] = {
                                        changed.Scale.X, changed.Scale.Y, changed.Scale.Z
                                    };
                                    const float relativeScale = currentValues[s_AxisGizmoDrag.axis] /
                                        std::max(0.001f, initialValues[s_AxisGizmoDrag.axis]);
                                    std::snprintf(readout, sizeof(readout), "Escala %s · %.2f×",
                                        axisLabels[s_AxisGizmoDrag.axis], relativeScale);
                                }
                                DrawGizmoReadout(drawList, readout, mouse, imageMin, imageMax);
                            }
                            else
                            {
                                TransformCommand groupCommand;
                                groupCommand.Type = CommandType::Transform;
                                bool hasSingleTransformChange = false;
                                const size_t commandCount = s_AxisGizmoDrag.group
                                    ? s_AxisGizmoDrag.groupIndices.size() : 1;
                                for (size_t i = 0; i < commandCount; ++i)
                                {
                                    const int index = s_AxisGizmoDrag.group
                                        ? s_AxisGizmoDrag.groupIndices[i] : selectedIndex;
                                    if (index < 0 || index >= static_cast<int>(entities.size())) continue;
                                    Entity& item = const_cast<Entity&>(entities[static_cast<size_t>(index)]);
                                    Transform before = s_AxisGizmoDrag.group
                                        ? s_AxisGizmoDrag.groupInitialTransforms[i]
                                        : s_AxisGizmoDrag.initialLocalTransform;
                                    const Transform& after = item.GetTransform();
                                    const bool changedTransform =
                                        std::abs(before.Position.X - after.Position.X) > 0.00001f ||
                                        std::abs(before.Position.Y - after.Position.Y) > 0.00001f ||
                                        std::abs(before.Position.Z - after.Position.Z) > 0.00001f ||
                                        std::abs(glm::dot(before.Rotation, after.Rotation)) < 0.999999f ||
                                        std::abs(before.Scale.X - after.Scale.X) > 0.00001f ||
                                        std::abs(before.Scale.Y - after.Scale.Y) > 0.00001f ||
                                        std::abs(before.Scale.Z - after.Scale.Z) > 0.00001f;
                                    if (!changedTransform) continue;
                                    if (s_AxisGizmoDrag.group)
                                        groupCommand.BatchTransforms.push_back(
                                            EntityTransformSnapshot{ item.GetID(), before, after });
                                    else
                                    {
                                        groupCommand.EntityID = item.GetID();
                                        groupCommand.Before = before;
                                        groupCommand.After = after;
                                        hasSingleTransformChange = true;
                                    }
                                }
                                if (!groupCommand.BatchTransforms.empty() ||
                                    (!s_AxisGizmoDrag.group && hasSingleTransformChange))
                                    app.GetUndoRedoManager().Push(groupCommand);
                                s_AxisGizmoDrag.active = false;
                                s_AxisGizmoDrag.group = false;
                                s_AxisGizmoDrag.groupIndices.clear();
                                s_AxisGizmoDrag.groupInitialTransforms.clear();
                                s_AxisGizmoDrag.groupInitialModels.clear();
                                app.SetWasUsingGizmo(false);
                                s_FaceDockingActive = false;
                            }
                        }
                        else if (s_AxisGizmoDrag.active)
                        {
                            s_AxisGizmoDrag.active = false;
                            app.SetWasUsingGizmo(false);
                        }
                    }
                    else
                    {
                        if (s_AxisGizmoDrag.active)
                        {
                            s_AxisGizmoDrag.active = false;
                            app.SetWasUsingGizmo(false);
                        }
                        glm::mat4 model = hasChildren ? BuildModel(gizmoTransform) : selected.GetTransform().WorldMatrix;
                        gizmoUsing = ImGuizmo::Manipulate(
                            &view[0][0], &projection[0][0], GetGizmoOperation(gizmoMode), gizmoSpace,
                            &model[0][0], nullptr, snap[0] > 0.0f ? snap : nullptr);
                        if (gizmoUsing)
                        {
                            if (!app.WasUsingGizmo()) app.m_BeforeTransform = selected.GetTransform();
                            if (hasChildren)
                            {
                                const glm::mat4 pivotInitial = BuildModel(gizmoTransform);
                                const glm::mat4 delta = model * glm::inverse(pivotInitial);
                                ApplyWorldGizmoMatrix(app.GetScene(), selected, delta * selected.GetTransform().WorldMatrix);
                                app.GetScene().UpdateTransforms();
                            }
                            else
                            {
                                ApplyWorldGizmoMatrix(app.GetScene(), selected, model);
                                app.GetScene().UpdateTransforms();
                            }
                            if (!isGroupSelection && gizmoMode == Application::GizmoMode::Translate && app.GetSnapSettings().FaceSnapEnabled)
                            {
                                ApplyFaceDocking(app, selected, nullptr);
                            }
                            else
                            {
                                s_FaceDockingActive = false;
                                s_ActiveDocks.clear();
                            }
                            app.SetWasUsingGizmo(true);
                            app.MarkDirty();
                        }
                        else if (app.WasUsingGizmo() && !ImGuizmo::IsUsing())
                        {
                            app.SetWasUsingGizmo(false);
                            s_FaceDockingActive = false;
                        }
                    }
                }
            }
        }
        else if (editing && modeling && selectedHasEditableGeometry && selectedIndex >= 0 &&
                 selectedIndex < static_cast<int>(entities.size()) &&
                 !MeshPainter::IsBrushActive())
        {
            // -- GIZMO NO MODO MODELADOR (DIRETAMENTE NAS FACES/V�RTICES) --
            Entity& selected = const_cast<Entity&>(entities[static_cast<size_t>(selectedIndex)]);
            auto& meshComp = selected.GetMeshComponent();
            const auto& selection = InspectorPanel::GetModelingSelection();

            static bool s_FaceGizmoActive = false;
            static glm::mat4 s_InitialGizmoMatrix(1.0f);
            static glm::mat4 s_CurrentGizmoMatrix(1.0f);
            static MeshTopology s_SnapshotTopology;
            static uint64_t s_LastSelectedEntityID = 0;
            static std::vector<int> s_LastSelection;
            static Application::GizmoMode s_LastGizmoMode = Application::GizmoMode::Translate;

            const Application::GizmoMode currentGizmoMode = app.GetGizmoMode();
            if (currentGizmoMode != s_LastGizmoMode)
            {
                // Trocar W/E/R não reaplica o delta do gizmo anterior.
                // A seleção permanece e o pivô é reconstruído no frame seguinte.
                s_FaceGizmoActive = false;
                app.SetWasUsingGizmo(false);
                s_LastGizmoMode = currentGizmoMode;
            }

            if (true) // sempre atualiza s_LastSelection
            {
                // Compara seleção ordenada para ignorar reordenamento entre frames
                std::vector<int> sortedCurrent = selection;
                std::vector<int> sortedLast = s_LastSelection;
                std::sort(sortedCurrent.begin(), sortedCurrent.end());
                std::sort(sortedLast.begin(), sortedLast.end());
                const bool entityChanged = s_LastSelectedEntityID != selected.GetID();
                const bool selectionEmpty = selection.empty();
                // Só reseta o gizmo se mudar de entidade ou seleção zerar
                // (não reseta quando companion é adicionado = evita o salto)
                if (entityChanged || selectionEmpty)
                {
                    s_FaceGizmoActive = false;
                }
                s_LastSelectedEntityID = selected.GetID();
                s_LastSelection = selection;
            }

            if (meshComp.MeshData && meshComp.MeshData->IsValid() && !selection.empty())
            {
                auto vertices = meshComp.MeshData->GetVertices();
                const auto& cached = GetOrUpdateModelingTopology(selected);
                const MeshTopology& currentTopology = cached.Topology;
                const int selMode = InspectorPanel::GetModelingSelectionMode();

                std::unordered_set<uint32_t> selectedVertIndices;
                const MeshTopology& refTopology = s_FaceGizmoActive ? s_SnapshotTopology : currentTopology;

                if (selMode == 2) // Faces
                {
                    for (int fIdx : selection)
                    {
                        if (fIdx >= 0 && fIdx < static_cast<int>(refTopology.Faces.size()))
                        {
                            const auto& face = refTopology.Faces[static_cast<size_t>(fIdx)];
                            if (face.Vertices[0] < refTopology.Vertices.size()) selectedVertIndices.insert(face.Vertices[0]);
                            if (face.Vertices[1] < refTopology.Vertices.size()) selectedVertIndices.insert(face.Vertices[1]);
                            if (face.Vertices[2] < refTopology.Vertices.size()) selectedVertIndices.insert(face.Vertices[2]);
                        }
                    }
                }
                else if (selMode == 0) // V�rtices
                {
                    for (int vIdx : selection)
                    {
                        if (vIdx >= 0 && vIdx < static_cast<int>(refTopology.Vertices.size()))
                            selectedVertIndices.insert(static_cast<uint32_t>(vIdx));
                    }
                }
                else if (selMode == 1) // Arestas
                {
                    const auto& edges = s_FaceGizmoActive ? s_SnapshotTopology.GetEdges() : cached.Edges;
                    for (int eIdx : selection)
                    {
                        if (eIdx >= 0 && eIdx < static_cast<int>(edges.size()))
                        {
                            if (edges[static_cast<size_t>(eIdx)].A < refTopology.Vertices.size())
                                selectedVertIndices.insert(edges[static_cast<size_t>(eIdx)].A);
                            if (edges[static_cast<size_t>(eIdx)].B < refTopology.Vertices.size())
                                selectedVertIndices.insert(edges[static_cast<size_t>(eIdx)].B);
                        }
                    }
                }

                if (!selectedVertIndices.empty())
                {
                    glm::vec3 centroidLocal(0.0f);
                    for (uint32_t vIdx : selectedVertIndices)
                    {
                        if (vIdx < refTopology.Vertices.size())
                        {
                            const auto& v = refTopology.Vertices[vIdx].Value;
                            centroidLocal += glm::vec3(v.X, v.Y, v.Z);
                        }
                    }
                    centroidLocal /= static_cast<float>(selectedVertIndices.size());

                    app.GetScene().UpdateTransforms();
                    glm::mat4 modelMat = selected.GetTransform().WorldMatrix;
                    glm::vec3 centroidWorld = glm::vec3(modelMat * glm::vec4(centroidLocal, 1.0f));

                    glm::vec3 worldScale(1.0f), worldTranslation(0.0f), worldSkew(0.0f);
                    glm::vec4 worldPerspective(0.0f);
                    glm::quat worldRotation(1.0f, 0.0f, 0.0f, 0.0f);
                    glm::decompose(modelMat, worldScale, worldRotation, worldTranslation,
                                   worldSkew, worldPerspective);

                    if (!s_FaceGizmoActive)
                    {
                        s_CurrentGizmoMatrix = glm::translate(glm::mat4(1.0f), centroidWorld) *
                                              glm::mat4_cast(glm::normalize(worldRotation));
                        s_InitialGizmoMatrix = s_CurrentGizmoMatrix;
                        // Sempre carrega a topologia a partir dos vértices atuais do objeto,
                        // garantindo que modificações em faces anteriores nunca sejam perdidas!
                        s_SnapshotTopology = MeshTopology::FromVertices(meshComp.MeshData->GetVertices());
                    }

                    const ImGuizmo::MODE gizmoSpace = app.GetGizmoSpace() == Application::GizmoSpace::Local
                        ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
                    const Application::GizmoMode gizmoMode = app.GetGizmoMode();

                    gizmoUsing = ImGuizmo::Manipulate(
                        &view[0][0], &projection[0][0], GetGizmoOperation(gizmoMode), gizmoSpace,
                        &s_CurrentGizmoMatrix[0][0], nullptr, nullptr);

                    if (gizmoUsing || ImGuizmo::IsUsing())
                    {
                        s_FaceGizmoActive = true;

                        glm::mat4 deltaWorld = s_CurrentGizmoMatrix * glm::inverse(s_InitialGizmoMatrix);
                        glm::mat4 deltaLocal = glm::inverse(modelMat) * deltaWorld * modelMat;

                        MeshTopology deformedTopology = s_SnapshotTopology;
                        for (uint32_t vIdx : selectedVertIndices)
                        {
                            if (vIdx < deformedTopology.Vertices.size())
                            {
                                const auto& origV = s_SnapshotTopology.Vertices[vIdx].Value;
                                glm::vec4 orig(origV.X, origV.Y, origV.Z, 1.0f);
                                glm::vec4 transformed = deltaLocal * orig;
                                deformedTopology.Vertices[vIdx].Value.X = transformed.x;
                                deformedTopology.Vertices[vIdx].Value.Y = transformed.y;
                                deformedTopology.Vertices[vIdx].Value.Z = transformed.z;
                            }
                        }

                        meshComp.MeshData->UpdateVertices(deformedTopology.ToVertices(false), "FaceGizmoDeform");
                        app.SetWasUsingGizmo(true);
                        app.MarkDirty();
                    }
                    else if (s_FaceGizmoActive && !ImGuizmo::IsUsing())
                    {
                        s_FaceGizmoActive = false;
                        app.SetWasUsingGizmo(false);

                        // Recalcula normais ao soltar o gizmo
                        auto finalVertices = meshComp.MeshData->GetVertices();
                        ModelingOperations::RecalculateNormals(finalVertices);
                        meshComp.MeshData->UpdateVertices(finalVertices, "FaceGizmoFinalize");
                        InspectorPanel::SaveModelingMesh(app, selected);

                        // Invalida cache de topologia para que próximas seleções usem os vértices deformados
                        s_CachedModelingTopology.EntityID = 0;
                        s_SnapshotTopology = MeshTopology::FromVertices(finalVertices);
                        app.MarkDirty();
                    }
                }
            }
            else
            {
                s_FaceGizmoActive = false;
            }
        }

        // -- PINCEL 3D / R�GUA INTERATIVA NA VIEWPORT ----------
        if (editing && modeling && selectedHasEditableGeometry && selectedIndex >= 0 &&
            selectedIndex < static_cast<int>(entities.size()) &&
            MeshPainter::IsBrushActive())
        {
            Entity& selected = const_cast<Entity&>(entities[static_cast<size_t>(selectedIndex)]);
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            const bool isMouseOverViewport = imageHovered ||
                (mousePos.x >= imageMin.x && mousePos.x <= imageMax.x &&
                 mousePos.y >= imageMin.y && mousePos.y <= imageMax.y);

            // Garante que o canvas, o atlas UV e o material estejam vinculados antes da pintura
            MeshPainter::GetOrCreateCanvas(selected);

            // Cursor visual do Pincel na Viewport
            if (isMouseOverViewport)
            {
                const float radius = MeshPainter::GetBrushRadius();
                const glm::vec4 col = MeshPainter::GetBrushColor();
                const ImU32 circleCol = IM_COL32(
                    static_cast<int>(col.r * 255), static_cast<int>(col.g * 255),
                    static_cast<int>(col.b * 255), 235);
                ImGui::GetWindowDrawList()->AddCircle(mousePos, radius, circleCol, 36, 1.5f);

                if (MeshPainter::GetBrushSoftness() > 0.02f)
                {
                    const float innerR = radius * (1.0f - MeshPainter::GetBrushSoftness());
                    ImGui::GetWindowDrawList()->AddCircle(mousePos, innerR, IM_COL32(255, 255, 255, 130), 24, 1.0f);
                }
            }

            // Guia visual da R�gua (linha reta)
            if (MeshPainter::IsRulerDrawing())
            {
                const ImVec2 startScreen = MeshPainter::GetRulerStartScreen();
                const glm::vec4 col = MeshPainter::GetBrushColor();
                const ImU32 lineCol = IM_COL32(
                    static_cast<int>(col.r * 255), static_cast<int>(col.g * 255),
                    static_cast<int>(col.b * 255), 220);
                ImGui::GetWindowDrawList()->AddLine(startScreen, mousePos, lineCol, 2.0f);
                ImGui::GetWindowDrawList()->AddCircleFilled(startScreen, 4.0f, IM_COL32(255, 255, 255, 255));
                ImGui::GetWindowDrawList()->AddCircleFilled(mousePos, 4.0f, lineCol);
            }

            // Tra�o de pintura ao clicar ou arrastar
            if (isMouseOverViewport && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const Picking::Hit hit = Picking::PickEntityDetailed(
                    app.GetScene(), viewportCamera, mousePos.x - imageMin.x, mousePos.y - imageMin.y,
                    static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));

                if (hit.EntityIndex == selectedIndex && hit.TriangleIndex >= 0)
                {
                    auto& meshComp = selected.GetMeshComponent();
                    if (meshComp.MeshData && meshComp.MeshData->IsValid())
                    {
                        const auto& verts = meshComp.MeshData->GetVertices();
                        const size_t tIdx0 = static_cast<size_t>(hit.TriangleIndex * 3);
                        if (tIdx0 + 2 < verts.size())
                        {
                            const auto& v0 = verts[tIdx0];
                            const auto& v1 = verts[tIdx0 + 1];
                            const auto& v2 = verts[tIdx0 + 2];

                            glm::vec3 p0(v0.X, v0.Y, v0.Z);
                            glm::vec3 p1(v1.X, v1.Y, v1.Z);
                            glm::vec3 p2(v2.X, v2.Y, v2.Z);

                            // Baric�ntricas de hit.LocalPosition
                            glm::vec3 e0 = p1 - p0, e1 = p2 - p0, e2 = hit.LocalPosition - p0;
                            float d00 = glm::dot(e0, e0), d01 = glm::dot(e0, e1), d11 = glm::dot(e1, e1);
                            float d20 = glm::dot(e2, e0), d21 = glm::dot(e2, e1);
                            float denom = d00 * d11 - d01 * d01;
                            glm::vec2 hitUV(0.5f);
                            if (std::abs(denom) > 0.00001f)
                            {
                                float v = (d11 * d20 - d01 * d21) / denom;
                                float w = (d00 * d21 - d01 * d20) / denom;
                                float u = 1.0f - v - w;
                                hitUV = u * glm::vec2(v0.U, v0.V) + v * glm::vec2(v1.U, v1.V) + w * glm::vec2(v2.U, v2.V);
                            }

                            const bool isRuler = MeshPainter::IsRulerMode() || ImGui::GetIO().KeyShift;
                            if (isRuler)
                            {
                                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                                {
                                    MeshPainter::StartRuler(hitUV, mousePos);
                                }
                            }
                            else
                            {
                                MeshPainter::ApplyBrushStroke(selected, hitUV);
                                app.MarkDirty();
                            }
                        }
                    }
                }
                else
                {
                    MeshPainter::EndBrushStroke();
                }
            }
            else
            {
                MeshPainter::EndBrushStroke();
            }

            // Finaliza r�gua ao soltar o mouse
            if (MeshPainter::IsRulerDrawing() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const Picking::Hit hit = Picking::PickEntityDetailed(
                    app.GetScene(), viewportCamera, mousePos.x - imageMin.x, mousePos.y - imageMin.y,
                    static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));

                if (hit.EntityIndex == selectedIndex && hit.TriangleIndex >= 0)
                {
                    auto& meshComp = selected.GetMeshComponent();
                    if (meshComp.MeshData && meshComp.MeshData->IsValid())
                    {
                        const auto& verts = meshComp.MeshData->GetVertices();
                        const size_t tIdx0 = static_cast<size_t>(hit.TriangleIndex * 3);
                        if (tIdx0 + 2 < verts.size())
                        {
                            const auto& v0 = verts[tIdx0];
                            const auto& v1 = verts[tIdx0 + 1];
                            const auto& v2 = verts[tIdx0 + 2];

                            glm::vec3 p0(v0.X, v0.Y, v0.Z);
                            glm::vec3 p1(v1.X, v1.Y, v1.Z);
                            glm::vec3 p2(v2.X, v2.Y, v2.Z);

                            glm::vec3 e0 = p1 - p0, e1 = p2 - p0, e2 = hit.LocalPosition - p0;
                            float d00 = glm::dot(e0, e0), d01 = glm::dot(e0, e1), d11 = glm::dot(e1, e1);
                            float d20 = glm::dot(e2, e0), d21 = glm::dot(e2, e1);
                            float denom = d00 * d11 - d01 * d01;
                            glm::vec2 endUV(0.5f);
                            if (std::abs(denom) > 0.00001f)
                            {
                                float v = (d11 * d20 - d01 * d21) / denom;
                                float w = (d00 * d21 - d01 * d20) / denom;
                                float u = 1.0f - v - w;
                                endUV = u * glm::vec2(v0.U, v0.V) + v * glm::vec2(v1.U, v1.V) + w * glm::vec2(v2.U, v2.V);
                            }

                            MeshPainter::ApplyRulerLine(selected, MeshPainter::GetRulerStartUV(), endUV);
                            app.MarkDirty();
                        }
                    }
                }
                MeshPainter::CancelRuler();
            }
        }

        // Quando o modo Pincel estiver ativado, nenhum outro modo de edi��o do bot�o esquerdo do mouse pode operar.
        // Preview de todas as faces e eixos alinhados durante o arraste (multi-docking)
        if (s_FaceDockingActive && !s_ActiveDocks.empty() && (gizmoUsing || ImGuizmo::IsUsing()))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            glm::mat4 ident(1.0f);

            for (const auto& dock : s_ActiveDocks)
            {
                ImVec2 scrCenter{};
                if (!ProjectPoint(view, projection, ident, dock.faceCenter, imageMin, imageMax, scrCenter))
                    continue;

                if (dock.snapKind != 0)
                {
                    ImVec2 targetScreen{};
                    if (ProjectPoint(view, projection, ident, dock.targetCenter,
                                     imageMin, imageMax, targetScreen))
                    {
                        drawList->AddLine(scrCenter, targetScreen,
                                          IM_COL32(30, 255, 190, 230), 2.0f);
                        drawList->AddCircleFilled(targetScreen, 4.0f,
                                                  IM_COL32(30, 255, 190, 245));
                    }
                    drawList->AddCircleFilled(scrCenter, 5.0f, IM_COL32(255, 255, 255, 245));
                    std::string label;
                    if (dock.snapKind == 1)
                    {
                        const char* axisName = dock.alignedAxis == 0 ? "X" : (dock.alignedAxis == 1 ? "Y" : "Z");
                        label = std::string("CENTRO ") + axisName;
                    }
                    else if (dock.snapKind == 2)
                    {
                        const char* axisName = dock.alignedAxis == 0 ? "X" : (dock.alignedAxis == 1 ? "Y" : "Z");
                        label = std::string("ARESTA ") + axisName;
                    }
                    drawList->AddText(ImVec2(scrCenter.x + 9.0f, scrCenter.y - 14.0f),
                                      IM_COL32(30, 255, 190, 255), label.c_str());
                }
                else
                {
                    const glm::vec3 halfU = dock.faceAxisU * std::max(dock.faceExtents.x, 0.05f);
                    const glm::vec3 halfV = dock.faceAxisV * std::max(dock.faceExtents.y, 0.05f);
                    const std::array<glm::vec3, 4> worldCorners = {
                        dock.faceCenter - halfU - halfV,
                        dock.faceCenter + halfU - halfV,
                        dock.faceCenter + halfU + halfV,
                        dock.faceCenter - halfU + halfV
                    };
                    std::array<ImVec2, 4> screenCorners{};
                    bool allCornersVisible = true;
                    for (size_t i = 0; i < worldCorners.size(); ++i)
                        allCornersVisible = ProjectPoint(view, projection, ident, worldCorners[i],
                            imageMin, imageMax, screenCorners[i]) && allCornersVisible;
                    if (allCornersVisible)
                    {
                        drawList->AddConvexPolyFilled(screenCorners.data(),
                            static_cast<int>(screenCorners.size()), IM_COL32(20, 255, 190, 45));
                        for (size_t i = 0; i < screenCorners.size(); ++i)
                            drawList->AddLine(screenCorners[i],
                                screenCorners[(i + 1) % screenCorners.size()],
                                IM_COL32(30, 255, 190, 230), 2.0f);
                    }
                    ImVec2 normalEnd{};
                    if (ProjectPoint(view, projection, ident,
                        dock.faceCenter + dock.faceNormal * 0.25f,
                        imageMin, imageMax, normalEnd))
                        drawList->AddLine(scrCenter, normalEnd, IM_COL32(30, 255, 190, 220), 2.0f);
                    drawList->AddCircleFilled(scrCenter, 4.0f, IM_COL32(255, 255, 255, 245));
                }
            }
        }

        // ---- ATALHOS DE TECLADO DO MODO MODELADOR ----
        const bool isCameraNavigating = app.IsCameraControlling() ||
                                        ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                        ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        if (editing && modeling && !MeshPainter::IsBrushActive() && !isCameraNavigating && (imageHovered || ImGui::IsWindowFocused()))
        {
            if (!ImGui::GetIO().WantTextInput)
            {
                // Alternar Modos de Seleção: 1 = Vértices, 2 = Arestas, 3 = Faces
                if (ImGui::IsKeyPressed(ImGuiKey_1))
                    InspectorPanel::SetModelingSelectionMode(0);
                else if (ImGui::IsKeyPressed(ImGuiKey_2))
                    InspectorPanel::SetModelingSelectionMode(1);
                else if (ImGui::IsKeyPressed(ImGuiKey_3))
                    InspectorPanel::SetModelingSelectionMode(2);

                const bool ctrl = ImGui::GetIO().KeyCtrl;
                const bool shift = ImGui::GetIO().KeyShift;
                const bool alt = ImGui::GetIO().KeyAlt;

                // Modos do Gizmo no Modelador: W (Mover), E (Girar), R (Escalar)
                if (!ctrl && !shift && !alt && !ImGuizmo::IsUsing())
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_W))
                        app.SetGizmoMode(Application::GizmoMode::Translate);
                    else if (ImGui::IsKeyPressed(ImGuiKey_E))
                        app.SetGizmoMode(Application::GizmoMode::Rotate);
                    else if (ImGui::IsKeyPressed(ImGuiKey_R))
                        app.SetGizmoMode(Application::GizmoMode::Scale);
                }

                // Extrude: Ctrl+E (requer Ctrl para não duplicar acidentalmente durante navegação ou rotação)
                const bool hasFaceSelected = (InspectorPanel::GetModelingSelectionMode() == 2 && !InspectorPanel::GetModelingSelection().empty());
                if (ctrl && hasFaceSelected && ImGui::IsKeyPressed(ImGuiKey_E))
                {
                    InspectorPanel::ApplyTopologyAction(app, "Extrude");
                    app.SetGizmoMode(Application::GizmoMode::Translate);
                }

                // I ou Ctrl+I: Inset
                if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_I)) || (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_I)))
                    InspectorPanel::ApplyTopologyAction(app, "Inset");

                // B ou Ctrl+B: Bevel
                if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_B)) || (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_B)))
                    InspectorPanel::ApplyTopologyAction(app, "Bevel");

                // Ctrl+R: Loop Cut
                if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R))
                    InspectorPanel::ApplyTopologyAction(app, "LoopCut");

                // K: Knife
                if (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_K))
                    InspectorPanel::ApplyTopologyAction(app, "Knife");

                // M ou Ctrl+M: Merge
                if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_M)) || (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_M)))
                    InspectorPanel::ApplyTopologyAction(app, "Merge");

                // F: Fill
                if (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_F))
                    InspectorPanel::ApplyTopologyAction(app, "Fill");

                // Del ou X: Excluir faces/componentes selecionados
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) || (!ctrl && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_X)))
                    InspectorPanel::ApplyTopologyAction(app, "Delete");

                // Ctrl+A: Selecionar tudo
                if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A))
                    InspectorPanel::SelectAllModeling(app);

                // Alt+A ou Esc: Desmarcar tudo
                if ((alt && ImGui::IsKeyPressed(ImGuiKey_A)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                    InspectorPanel::ClearModelingSelection();

                // Ctrl+Shift+F: Aplanar / Flatten
                if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_F))
                    InspectorPanel::ApplyTopologyAction(app, "Flatten");

                // Ctrl+Shift+S: Esferizar / Spherize
                if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_S))
                    InspectorPanel::ApplyTopologyAction(app, "Spherize");

                // Alt+P: Poke faces
                if (alt && ImGui::IsKeyPressed(ImGuiKey_P))
                    InspectorPanel::ApplyTopologyAction(app, "Poke");
            }
        }

        if (MeshPainter::IsBrushActive())
        {
            s_ModelingBoxSelecting = false;
        }

        if (editing && modeling && !MeshPainter::IsBrushActive() && imageHovered && !uiInteractionCaptured && imageLeftClicked &&
            !gizmoUsing && !ImGuizmo::IsOver())
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            s_ModelingBoxSelecting = true;
            s_ModelingBoxStart = mouse;
            const Picking::Hit hit = Picking::PickEntityDetailed(
                app.GetScene(), viewportCamera, mouse.x - imageMin.x, mouse.y - imageMin.y,
                static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));
            if (hit.EntityIndex >= 0)
            {
                app.SetSelectedEntity(hit.EntityIndex);
                Entity& hitEntity = const_cast<Entity&>(entities[static_cast<size_t>(hit.EntityIndex)]);
                const bool sameEntity = InspectorPanel::GetModelingEntityID() == hitEntity.GetID();
                if (!sameEntity)
                {
                    InspectorPanel::SetModelingSelection(hitEntity.GetID(),
                        InspectorPanel::GetModelingSelectionMode(), {});
                }
                else
                {
                    const auto& mesh = hitEntity.GetMeshComponent().MeshData;
                    if (mesh)
                    {
                        const MeshTopology& topology = GetOrUpdateModelingTopology(hitEntity).Topology;
                        const int component = FindModelingComponent(
                            topology, InspectorPanel::GetModelingSelectionMode(),
                            hit.TriangleIndex, hit.LocalPosition);
                        if (component >= 0)
                        {
                            std::vector<int> selected = InspectorPanel::GetModelingSelection();
                            int companionFace = -1;
                            if (InspectorPanel::GetModelingSelectionMode() == 2)
                            {
                                // Se estiver no modo Quad/Retângulos e com diagonais ocultas, seleciona a face quad completa (os dois triângulos coplanares)
                                if (InspectorPanel::GetMiniFaceShape() != InspectorPanel::MiniFaceShape::Triangles &&
                                    InspectorPanel::IsHideDiagonalLines())
                                {
                                    // 1. Tenta o par consecutivo comum (0-1, 2-3, ...)
                                    int candidate = (component % 2 == 0) ? (component + 1) : (component - 1);
                                    if (candidate >= 0 && candidate < static_cast<int>(topology.Faces.size()))
                                    {
                                        const auto& fA = topology.Faces[component];
                                        const auto& fB = topology.Faces[candidate];
                                        int sharedVerts = 0;
                                        for (int i = 0; i < 3; ++i)
                                        {
                                            for (int j = 0; j < 3; ++j)
                                            {
                                                if (fA.Vertices[i] == fB.Vertices[j]) ++sharedVerts;
                                            }
                                        }
                                        if (sharedVerts >= 2)
                                        {
                                            const auto& va0 = topology.Vertices[fA.Vertices[0]].Value;
                                            const auto& va1 = topology.Vertices[fA.Vertices[1]].Value;
                                            const auto& va2 = topology.Vertices[fA.Vertices[2]].Value;
                                            const auto& vb0 = topology.Vertices[fB.Vertices[0]].Value;
                                            const auto& vb1 = topology.Vertices[fB.Vertices[1]].Value;
                                            const auto& vb2 = topology.Vertices[fB.Vertices[2]].Value;
                                            glm::vec3 nA = glm::cross(
                                                glm::vec3(va1.X - va0.X, va1.Y - va0.Y, va1.Z - va0.Z),
                                                glm::vec3(va2.X - va0.X, va2.Y - va0.Y, va2.Z - va0.Z));
                                            glm::vec3 nB = glm::cross(
                                                glm::vec3(vb1.X - vb0.X, vb1.Y - vb0.Y, vb1.Z - vb0.Z),
                                                glm::vec3(vb2.X - vb0.X, vb2.Y - vb0.Y, vb2.Z - vb0.Z));
                                            float lA = glm::length(nA);
                                            float lB = glm::length(nB);
                                            if (lA > 1e-6f && lB > 1e-6f && glm::dot(nA / lA, nB / lB) > 0.999f)
                                            {
                                                companionFace = candidate;
                                            }
                                        }
                                    }

                                    // 2. Se não for consecutivo, procura vizinho coplanar que compartilhe 2 vértices
                                    if (companionFace < 0)
                                    {
                                        const auto& fA = topology.Faces[component];
                                        const auto& va0 = topology.Vertices[fA.Vertices[0]].Value;
                                        const auto& va1 = topology.Vertices[fA.Vertices[1]].Value;
                                        const auto& va2 = topology.Vertices[fA.Vertices[2]].Value;
                                        glm::vec3 nA = glm::cross(
                                            glm::vec3(va1.X - va0.X, va1.Y - va0.Y, va1.Z - va0.Z),
                                            glm::vec3(va2.X - va0.X, va2.Y - va0.Y, va2.Z - va0.Z));
                                        float lA = glm::length(nA);
                                        if (lA > 1e-6f)
                                        {
                                            nA /= lA;
                                            for (size_t fIdx = 0; fIdx < topology.Faces.size(); ++fIdx)
                                            {
                                                if (static_cast<int>(fIdx) == component) continue;
                                                const auto& fB = topology.Faces[fIdx];
                                                int shared = 0;
                                                for (int i = 0; i < 3; ++i)
                                                    for (int j = 0; j < 3; ++j)
                                                        if (fA.Vertices[i] == fB.Vertices[j]) ++shared;
                                                if (shared >= 2)
                                                {
                                                    const auto& vb0 = topology.Vertices[fB.Vertices[0]].Value;
                                                    const auto& vb1 = topology.Vertices[fB.Vertices[1]].Value;
                                                    const auto& vb2 = topology.Vertices[fB.Vertices[2]].Value;
                                                    glm::vec3 nB = glm::cross(
                                                        glm::vec3(vb1.X - vb0.X, vb1.Y - vb0.Y, vb1.Z - vb0.Z),
                                                        glm::vec3(vb2.X - vb0.X, vb2.Y - vb0.Y, vb2.Z - vb0.Z));
                                                    float lB = glm::length(nB);
                                                    if (lB > 1e-6f && glm::dot(nA, nB / lB) > 0.999f)
                                                    {
                                                        companionFace = static_cast<int>(fIdx);
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            if (ImGui::GetIO().KeyCtrl)
                            {
                                auto found = std::find(selected.begin(), selected.end(), component);
                                if (found == selected.end())
                                {
                                    selected.push_back(component);
                                    if (companionFace >= 0 && std::find(selected.begin(), selected.end(), companionFace) == selected.end())
                                    {
                                        selected.push_back(companionFace);
                                    }
                                }
                                else
                                {
                                    selected.erase(found);
                                    if (companionFace >= 0)
                                    {
                                        auto compFound = std::find(selected.begin(), selected.end(), companionFace);
                                        if (compFound != selected.end()) selected.erase(compFound);
                                    }
                                }
                            }
                            else
                            {
                                selected = { component };
                                if (companionFace >= 0) selected.push_back(companionFace);
                            }
                            InspectorPanel::SetModelingSelection(
                                hitEntity.GetID(), InspectorPanel::GetModelingSelectionMode(), selected);
                        }
                    }
                }
            }
        }
        else if (editing && !modeling && !MeshPainter::IsBrushActive() && imageHovered && !uiInteractionCaptured && imageLeftClicked &&
            !gizmoUsing && !ImGuizmo::IsOver())
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            s_ViewportBoxSelecting = true;
            s_ViewportBoxStart = mouse;
            s_ViewportBoxCurrent = mouse;
        }

        if (editing && !modeling && !MeshPainter::IsBrushActive() &&
            s_ViewportBoxSelecting)
        {
            const ImVec2 current = ImGui::GetIO().MousePos;
            s_ViewportBoxCurrent = current;
            const float left = std::min(s_ViewportBoxStart.x, current.x);
            const float right = std::max(s_ViewportBoxStart.x, current.x);
            const float top = std::min(s_ViewportBoxStart.y, current.y);
            const float bottom = std::max(s_ViewportBoxStart.y, current.y);
            const bool draggedFarEnough = (right - left) > 5.0f || (bottom - top) > 5.0f;
            if (draggedFarEnough)
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(imageMin, imageMax, true);
                drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                    IM_COL32(20, 65, 125, 75));
                drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom),
                    IM_COL32(35, 105, 210, 235), 0.0f, 0, 1.5f);
                drawList->PopClipRect();
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const bool isBox = (right - left) > 8.0f || (bottom - top) > 8.0f;
                s_ViewportBoxSelecting = false;
                if (isBox)
                {
                    std::vector<int> selected;
                    const glm::mat4 identity(1.0f);
                    for (size_t i = 0; i < entities.size(); ++i)
                    {
                        const Entity& entity = entities[i];
                        if (entity.SelectionLocked || entity.IsEditorOnly() || !entity.Active ||
                            entity.IsGlobalLight() || entity.IsGlobalScript())
                            continue;
                        const glm::mat4 worldMat = GetEntityWorldMatrix(entity);
                        const glm::vec3 position = glm::vec3(worldMat[3]);
                        ImVec2 screen;
                        if (ProjectPoint(view, projection, identity, position, imageMin, imageMax, screen) &&
                            IsInsideViewport(screen, ImVec2(left, top), ImVec2(right, bottom)))
                        {
                            selected.push_back(static_cast<int>(i));
                        }
                    }

                    if (!selected.empty())
                    {
                        app.SetSelectedEntity(selected.front());
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            std::vector<int> combined = app.GetMultiSelectedEntities();
                            for (int index : selected)
                                if (std::find(combined.begin(), combined.end(), index) == combined.end())
                                    combined.push_back(index);
                            app.SetMultiSelectedEntities(combined);
                        }
                        else
                        {
                            app.SetMultiSelectedEntities(selected);
                        }
                    }
                    else if (!ImGui::GetIO().KeyCtrl)
                    {
                        app.SetSelectedEntity(-1);
                        app.ClearMultiSelection();
                    }
                }
                else
                {
                    const Picking::Hit hit = app.Is2DWorldMode()
                        ? PickEntity2DAtScreenPoint(app, view, projection, imageMin, imageMax, current)
                        : Picking::PickEntityDetailed(
                            app.GetScene(), viewportCamera, current.x - imageMin.x,
                            current.y - imageMin.y, static_cast<int>(viewportWidth),
                            static_cast<int>(viewportHeight));
                    if (hit.EntityIndex >= 0)
                    {
                        app.SetSelectedEntity(hit.EntityIndex);
                        app.SetMultiSelectedEntities({ hit.EntityIndex });
                    }
                    else
                    {
                        app.SetSelectedEntity(-1);
                        app.ClearMultiSelection();
                    }
                }
            }
        }

        if (editing && modeling && !MeshPainter::IsBrushActive() && imageHovered && s_ModelingBoxSelecting &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImVec2 current = ImGui::GetIO().MousePos;
            ImGui::GetWindowDrawList()->AddRect(
                s_ModelingBoxStart, current, IM_COL32(95, 185, 255, 230),
                0.0f, 0, 1.5f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                s_ModelingBoxStart, current, IM_COL32(80, 160, 255, 24));
        }

        if (editing && modeling && !MeshPainter::IsBrushActive() && imageHovered && s_ModelingBoxSelecting &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const ImVec2 end = ImGui::GetIO().MousePos;
            const float left = std::min(s_ModelingBoxStart.x, end.x);
            const float right = std::max(s_ModelingBoxStart.x, end.x);
            const float top = std::min(s_ModelingBoxStart.y, end.y);
            const float bottom = std::max(s_ModelingBoxStart.y, end.y);
            const bool isBox = (right - left) > 8.0f || (bottom - top) > 8.0f;
            s_ModelingBoxSelecting = false;
            if (isBox && selectedIndex >= 0 &&
                selectedIndex < static_cast<int>(entities.size()) &&
                InspectorPanel::GetModelingEntityID() ==
                    entities[static_cast<size_t>(selectedIndex)].GetID())
            {
                Entity& selectedEntity = const_cast<Entity&>(
                    entities[static_cast<size_t>(selectedIndex)]);
                const auto& mesh = selectedEntity.GetMeshComponent().MeshData;
                if (mesh)
                {
                        const MeshTopology& topology = GetOrUpdateModelingTopology(selectedEntity).Topology;
                    std::vector<int> selectedComponents;
                    if (ImGui::GetIO().KeyCtrl)
                        selectedComponents = InspectorPanel::GetModelingSelection();
                    const int mode = InspectorPanel::GetModelingSelectionMode();
                    const glm::mat4 model = GetEntityWorldMatrix(selectedEntity);
                    const auto inside = [&](const ImVec2& point)
                    {
                        return point.x >= left && point.x <= right &&
                               point.y >= top && point.y <= bottom;
                    };
                    const auto add = [&](int index)
                    {
                        if (std::find(selectedComponents.begin(), selectedComponents.end(), index) ==
                            selectedComponents.end())
                            selectedComponents.push_back(index);
                    };
                    if (mode == 0)
                    {
                        for (size_t index = 0; index < topology.Vertices.size(); ++index)
                        {
                            const auto& vertex = topology.Vertices[index].Value;
                            ImVec2 point;
                            if (ProjectPoint(view, projection, model,
                                { vertex.X, vertex.Y, vertex.Z }, imageMin, imageMax, point) && inside(point))
                                add(static_cast<int>(index));
                        }
                    }
                    else if (mode == 1)
                    {
                        const auto edges = topology.GetEdges();
                        for (size_t index = 0; index < edges.size(); ++index)
                        {
                            const auto& a = topology.Vertices[edges[index].A].Value;
                            const auto& b = topology.Vertices[edges[index].B].Value;
                            ImVec2 point;
                            const glm::vec3 center((a.X + b.X) * 0.5f,
                                                   (a.Y + b.Y) * 0.5f,
                                                   (a.Z + b.Z) * 0.5f);
                            if (ProjectPoint(view, projection, model, center,
                                imageMin, imageMax, point) && inside(point))
                                add(static_cast<int>(index));
                        }
                    }
                    else
                    {
                        for (size_t index = 0; index < topology.Faces.size(); ++index)
                        {
                            const auto& face = topology.Faces[index];
                            const auto& a = topology.Vertices[face.Vertices[0]].Value;
                            const auto& b = topology.Vertices[face.Vertices[1]].Value;
                            const auto& c = topology.Vertices[face.Vertices[2]].Value;
                            const glm::vec3 center((a.X + b.X + c.X) / 3.0f,
                                                   (a.Y + b.Y + c.Y) / 3.0f,
                                                   (a.Z + b.Z + c.Z) / 3.0f);
                            ImVec2 point;
                            if (ProjectPoint(view, projection, model, center,
                                imageMin, imageMax, point) && inside(point))
                                add(static_cast<int>(index));
                        }
                    }
                    InspectorPanel::SetModelingSelection(
                        selectedEntity.GetID(), mode, selectedComponents);
                }
            }
        }

        // ---- MENU DE CONTEXTO DO VIEWPORT (CLIQUE COM BOTÃO DIREITO) ----
        static ImVec2 s_ViewportRightClickStart(0.0f, 0.0f);
        static bool s_ViewportRightClickTracking = false;

        if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            s_ViewportRightClickStart = ImGui::GetIO().MousePos;
            s_ViewportRightClickTracking = true;
        }

        if (s_ViewportRightClickTracking && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            s_ViewportRightClickTracking = false;
            const ImVec2 releasePos = ImGui::GetIO().MousePos;
            const float dx = releasePos.x - s_ViewportRightClickStart.x;
            const float dy = releasePos.y - s_ViewportRightClickStart.y;
            const float distSq = dx * dx + dy * dy;

            // Se o mouse moveu menos de 6 pixels, foi um clique simples com botão direito (não navegação de câmera)
            if (distSq < 36.0f && imageHovered && !uiInteractionCaptured)
            {
                const Picking::Hit hit = app.Is2DWorldMode()
                    ? PickEntity2DAtScreenPoint(app, view, projection, imageMin, imageMax, releasePos)
                    : Picking::PickEntityDetailed(
                        app.GetScene(), viewportCamera, releasePos.x - imageMin.x,
                        releasePos.y - imageMin.y, static_cast<int>(viewportWidth),
                        static_cast<int>(viewportHeight));

                if (hit.EntityIndex >= 0)
                {
                    if (!app.IsMultiSelectedEntity(hit.EntityIndex) && app.GetSelectedEntityIndex() != hit.EntityIndex)
                    {
                        app.SetSelectedEntity(hit.EntityIndex);
                        app.SetMultiSelectedEntities({ hit.EntityIndex });
                    }
                }

                if (app.GetSelectedEntityIndex() >= 0 || !app.GetMultiSelectedEntities().empty())
                {
                    ImGui::OpenPopup("##viewport_context_menu");
                }
            }
        }

        if (ImGui::BeginPopup("##viewport_context_menu"))
        {
            std::vector<int> selectedIndices = app.GetMultiSelectedEntities();
            const int mainSel = app.GetSelectedEntityIndex();
            if (mainSel >= 0 && std::find(selectedIndices.begin(), selectedIndices.end(), mainSel) == selectedIndices.end())
                selectedIndices.push_back(mainSel);

            const int selCount = static_cast<int>(selectedIndices.size());
            if (selCount == 1 && mainSel >= 0 && mainSel < static_cast<int>(entities.size()))
            {
                ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), "%s", entities[static_cast<size_t>(mainSel)].GetName().c_str());
            }
            else
            {
                ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), UI("%d Objetos Selecionados"), selCount);
            }
            ImGui::Separator();

            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Folder, UI("Selecionar pai...")))
            {
                std::vector<uint64_t> childEntityIDs;
                for (int idx : selectedIndices)
                {
                    if (idx >= 0 && idx < static_cast<int>(entities.size()))
                        childEntityIDs.push_back(entities[static_cast<size_t>(idx)].GetID());
                }
                HierarchyPanel::BeginAddParentForEntities(app, childEntityIDs);
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();

            if (ImGui::MenuItem(UI("Duplicar"), "Ctrl+D"))
            {
                app.DuplicateSelectedEntity();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(UI("Excluir"), "Delete"))
            {
                app.DeleteSelectedEntity();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(UI("Focar"), "F"))
            {
                if (mainSel >= 0 && mainSel < static_cast<int>(entities.size()))
                {
                    Camera& cam = app.GetCamera();
                    const Transform& t = const_cast<Entity&>(entities[static_cast<size_t>(mainSel)]).GetTransform();
                    cam.Position.X = t.Position.X;
                    cam.Position.Y = t.Position.Y;
                    if (!app.Is2DWorldMode())
                        cam.Position.Z = t.Position.Z + 5.0f;
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImGui::End();

    // Console acionável próprio para que o DockSpace o mantenha abaixo do viewport.
    // Os filtros e o detalhe ficam aqui, sem criar uma segunda janela Debug implícita.
    ImGui::Begin(UI("Debug"));
    const float fps = PerformanceMonitor::GetFPS();
    const ImVec4 fpsColor = fps >= 55.0f ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                          : fps >= 30.0f ? ImVec4(0.95f, 0.78f, 0.25f, 1.0f)
                                        : ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
    ImGui::TextColored(fpsColor, "%.1f FPS", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("Viewport: %dx%d  |  Estado: %s",
                        app.GetViewportWidth(), app.GetViewportHeight(),
                        app.GetEditorState() == Application::EditorState::Play ? "Play" : "Editor");

    static bool showDebug = true;
    static bool showInfo = true;
    static bool showWarning = true;
    static bool showError = true;
    static bool showSuccess = true;
    static char logFilter[128] = {};
    static uint64_t selectedSequence = 0;

    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Limpar")))
    {
        MaraeDiagnostics::DiagnosticLog::Get().Clear();
        selectedSequence = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Copiar")))
    {
        std::string clipboard;
        for (const auto& entry : MaraeDiagnostics::DiagnosticLog::Get().Snapshot())
        {
            clipboard += entry.Category.empty() ? "Runtime" : entry.Category;
            clipboard += ": ";
            clipboard += entry.Message;
            if (!entry.File.empty())
            {
                clipboard += " [" + entry.File;
                if (entry.Line > 0) clipboard += ":" + std::to_string(entry.Line);
                clipboard += "]";
            }
            clipboard += '\n';
        }
        ImGui::SetClipboardText(clipboard.c_str());
    }

    ImGui::SameLine();
    ImGui::Checkbox(UI("Debug"), &showDebug);
    ImGui::SameLine();
    ImGui::Checkbox(UI("Info"), &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox(UI("Avisos"), &showWarning);
    ImGui::SameLine();
    ImGui::Checkbox(UI("Erros"), &showError);
    ImGui::SameLine();
    ImGui::Checkbox(UI("OK"), &showSuccess);

    // Debugger C# integrado ao mesmo painel acoplado do console.
    auto& scriptDebugger = MaraeScripting::ScriptDebugger::Instance();
    static bool breakOnScriptExceptions = false;
    breakOnScriptExceptions = scriptDebugger.GetBreakOnExceptions();
    if (ImGui::Checkbox(UI("Parar em exceções C#"), &breakOnScriptExceptions))
        scriptDebugger.SetBreakOnExceptions(breakOnScriptExceptions);

    static char breakpointType[160] = {};
    static char breakpointMethod[80] = "OnUpdate";
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##script_breakpoint_type", UI("Tipo do script"),
                             breakpointType, sizeof(breakpointType));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputTextWithHint("##script_breakpoint_method", UI("Método"),
                             breakpointMethod, sizeof(breakpointMethod));
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Adicionar breakpoint")) && breakpointType[0] != '\0')
        scriptDebugger.SetBreakpoint(breakpointType, breakpointMethod);
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Limpar breakpoints")))
        scriptDebugger.ClearBreakpoints();

    const auto breakpoints = scriptDebugger.GetBreakpoints();
    if (!breakpoints.empty())
    {
        ImGui::TextDisabled(UI("Breakpoints ativos:"));
        for (const std::string& breakpoint : breakpoints)
            ImGui::SameLine(), ImGui::TextDisabled("[%s]", breakpoint.c_str());
    }
    const MaraeScripting::ScriptDebugSnapshot scriptSnapshot = scriptDebugger.GetSnapshot();
    if (scriptDebugger.IsPaused())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.25f, 1.0f));
        ImGui::TextWrapped(UI("Script pausado: %s.%s — %s"),
                           scriptSnapshot.TypeName.c_str(), scriptSnapshot.Method.c_str(),
                           scriptSnapshot.Message.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Continuar script")))
            scriptDebugger.Continue();
    }
    if (!scriptSnapshot.Message.empty() && ImGui::TreeNode(UI("Detalhes do script")))
    {
        ImGui::Text(UI("Entidade: %llu"), static_cast<unsigned long long>(scriptSnapshot.EntityId));
        ImGui::TextWrapped(UI("Mensagem: %s"), scriptSnapshot.Message.c_str());
        if (!scriptSnapshot.StackTrace.empty())
            ImGui::TextWrapped(UI("Stack: %s"), scriptSnapshot.StackTrace.c_str());
        ImGui::TextWrapped(UI("Variáveis: %s"), scriptSnapshot.VariablesJson.c_str());
        ImGui::TreePop();
    }

    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##diagnostic_filter", UI("Filtrar console"),
                             logFilter, sizeof(logFilter));

    const auto entries = MaraeDiagnostics::DiagnosticLog::Get().Snapshot();
    const float detailsHeight = selectedSequence != 0 ? 52.0f : 0.0f;
    const bool listVisible = ImGui::BeginChild(
        "##diagnostic_log_list", ImVec2(0.0f, -detailsHeight), true,
        ImGuiWindowFlags_HorizontalScrollbar);
    const MaraeDiagnostics::LogEntry* selectedEntry = nullptr;
    if (listVisible)
    {
        for (const auto& entry : entries)
        {
            bool visible = false;
            switch (entry.Level)
            {
            case MaraeDiagnostics::LogLevel::Debug: visible = showDebug; break;
            case MaraeDiagnostics::LogLevel::Info: visible = showInfo; break;
            case MaraeDiagnostics::LogLevel::Warning: visible = showWarning; break;
            case MaraeDiagnostics::LogLevel::Error: visible = showError; break;
            case MaraeDiagnostics::LogLevel::Success: visible = showSuccess; break;
            }
            if (!visible) continue;
            if (logFilter[0] != '\0' &&
                entry.Message.find(logFilter) == std::string::npos &&
                entry.Category.find(logFilter) == std::string::npos &&
                entry.File.find(logFilter) == std::string::npos)
                continue;

            ImVec4 color(0.78f, 0.78f, 0.78f, 1.0f);
            const char* label = "INFO";
            switch (entry.Level)
            {
            case MaraeDiagnostics::LogLevel::Debug:
                color = ImVec4(0.60f, 0.70f, 0.85f, 1.0f); label = "DEBUG"; break;
            case MaraeDiagnostics::LogLevel::Warning:
                color = ImVec4(1.00f, 0.78f, 0.25f, 1.0f); label = "AVISO"; break;
            case MaraeDiagnostics::LogLevel::Error:
                color = ImVec4(1.00f, 0.35f, 0.35f, 1.0f); label = "ERRO"; break;
            case MaraeDiagnostics::LogLevel::Success:
                color = ImVec4(0.35f, 1.00f, 0.45f, 1.0f); label = "OK"; break;
            default: break;
            }

            std::string row = "[" + std::string(label) + "] ";
            if (!entry.Category.empty()) row += entry.Category + ": ";
            row += entry.Message;
            if (entry.File.size() > 0)
            {
                row += "  (" + entry.File;
                if (entry.Line > 0) row += ":" + std::to_string(entry.Line);
                row += ")";
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool clicked = ImGui::Selectable(row.c_str(), selectedSequence == entry.Sequence);
            ImGui::PopStyleColor();
            if (clicked) selectedSequence = entry.Sequence;
            if (selectedSequence == entry.Sequence) selectedEntry = &entry;
        }
        if (entries.empty())
            ImGui::TextDisabled(UI("Nenhum diagnóstico registrado."));
    }
    ImGui::EndChild();

    if (selectedEntry)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Contexto: %s | Objeto: %s | Cena: %s",
            selectedEntry->Category.empty() ? "Runtime" : selectedEntry->Category.c_str(),
            selectedEntry->Object.empty() ? "-" : selectedEntry->Object.c_str(),
            selectedEntry->Scene.empty() ? "-" : selectedEntry->Scene.c_str());
        if (!selectedEntry->Suggestion.empty())
            ImGui::TextWrapped(UI("Solução possível: %s"), selectedEntry->Suggestion.c_str());
    }
    ImGui::End();
}
