#include "../../Engine/Localization/Localization.h"
#include "HierarchyPanel.h"
#include "../../Core/Application.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Engine/Assets/ModelImporter/ModelImporter.h"
#include "../../Assets/AssetDatabase/AssetDatabase.h"
#include "../../Scene/Components/LightComponent.h"
#include "../../Scene/Components/UIComponent.h"
#include "../../Engine/Localization/Localization.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../../Renderer/Texture/TextureCache.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include "../UI/EditorIcons.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

// Alias visual simples (sem dep de font de ícones)

namespace
{
    std::unordered_set<uint64_t> s_HierarchyVisitedEntities;
    std::vector<std::pair<std::string, uint64_t>> s_PendingHierarchyAssetDrops;
    struct HierarchySelectionRow
    {
        int EntityIndex = -1;
        ImVec2 Min{};
        ImVec2 Max{};
    };
    std::vector<HierarchySelectionRow> s_HierarchySelectionRows;
    std::vector<int> s_HierarchySelectionBeforeMarquee;
    bool s_HierarchyMarqueeActive = false;
    bool s_HierarchyMarqueeAdditive = false;
    ImVec2 s_HierarchyMarqueeStart{};
    std::vector<uint64_t> s_DraggedEntityIDs;
    int s_PotentialSingleSelectIndex = -1;
    // Rastreia se o usuário começou a arrastar um item já selecionado (multi-drag)
    // Valor = entityIndex do item que originou o drag; -1 = nenhum
    int s_MultiDragSourceIndex = -1;
    bool s_MultiDragStarted = false; // true após threshold de distância
    enum class HierarchyContextAction { Duplicate, Delete };
    std::vector<std::pair<HierarchyContextAction, uint64_t>> s_PendingHierarchyContextActions;
    uint64_t s_PendingChildParentID = 0;
    std::vector<uint64_t> s_PendingParentForChildrenIDs;
    bool s_OpenChildCreatorPending = false;
    std::unordered_set<uint64_t> s_EntityIDsBeforeChildCreation;
    uint64_t s_ContextRenameEntityID = 0;
    char s_ContextRenameBuffer[256]{};
    bool s_ContextRenameFocusPending = false;
    uint64_t s_InlineRenameEntityID = 0;
    char s_InlineRenameBuffer[256]{};
    bool s_InlineRenameFocusPending = false;
    int s_HierarchyForceTreeState = -1; // -1 normal, 0 collapse, 1 expand

    bool IsHierarchyNameDoubleClicked(const Entity& entity, const ImVec2& rowMin,
                                      const ImVec2& rowMax)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float textStart = rowMin.x + 32.0f;
        const float textEnd = textStart + ImGui::CalcTextSize(entity.GetName().c_str()).x + 8.0f;
        return ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(0) &&
            mouse.x >= textStart && mouse.x <= textEnd &&
            mouse.y >= rowMin.y && mouse.y <= rowMax.y;
    }

    void BeginHierarchyInlineRename(const Entity& entity)
    {
        s_InlineRenameEntityID = entity.GetID();
        std::snprintf(s_InlineRenameBuffer, sizeof(s_InlineRenameBuffer), "%s",
            entity.GetName().c_str());
        s_InlineRenameFocusPending = true;
    }

    void DrawHierarchyInlineRename(Application& app, Entity& entity,
                                    const ImVec2& rowMin, const ImVec2& rowMax)
    {
        if (s_InlineRenameEntityID != entity.GetID())
            return;

        const float inputX = rowMin.x + 32.0f;
        const float windowRight = ImGui::GetWindowPos().x +
            ImGui::GetWindowContentRegionMax().x;
        const float inputRight = windowRight - 24.0f;
        const float inputWidth = std::max(80.0f, inputRight - inputX);
        const ImVec2 cursorBeforeInput = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos(ImVec2(inputX, rowMin.y + 1.0f));
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::PushID(static_cast<int>(entity.GetID() & 0x7fffffff));
        if (s_InlineRenameFocusPending)
        {
            ImGui::SetKeyboardFocusHere();
            s_InlineRenameFocusPending = false;
        }

        const bool submitted = ImGui::InputText("##HierarchyInlineRename",
            s_InlineRenameBuffer, sizeof(s_InlineRenameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool cancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool finished = submitted || ImGui::IsItemDeactivated();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(cursorBeforeInput);

        if (cancelled)
        {
            s_InlineRenameEntityID = 0;
            return;
        }

        if (finished)
        {
            const std::string newName = s_InlineRenameBuffer;
            if (!newName.empty() && newName != entity.GetName())
            {
                entity.SetName(newName);
                app.MarkDirty();
            }
            s_InlineRenameEntityID = 0;
        }
    }

    MaraeUI::EditorIcon EntityIcon(Entity& entity)
    {
        if (entity.IsFolder) return MaraeUI::EditorIcon::Folder;
        if (entity.IsModel) return MaraeUI::EditorIcon::Model;
        if (entity.HasCamera()) return MaraeUI::EditorIcon::Camera;
        if (entity.HasUI())
        {
            switch (entity.GetUIComponent().Type)
            {
            case UIElementType::ScreenGui: return MaraeUI::EditorIcon::ScreenGui;
            case UIElementType::Frame: return MaraeUI::EditorIcon::Frame;
            case UIElementType::TextLabel:
            case UIElementType::TextBox: return MaraeUI::EditorIcon::Text;
            case UIElementType::TextButton:
            case UIElementType::ImageButton:
            case UIElementType::Checkbox:
            case UIElementType::Dropdown: return MaraeUI::EditorIcon::Button;
            case UIElementType::ImageLabel: return MaraeUI::EditorIcon::Image;
            case UIElementType::ScrollingFrame:
            case UIElementType::Slider:
            case UIElementType::ProgressBar: return MaraeUI::EditorIcon::Frame;
            }
        }
        if (entity.IsGlobalScript()) return MaraeUI::EditorIcon::Script;
        if (entity.IsGlobalLight() || entity.HasLight()) return MaraeUI::EditorIcon::Light;

        const std::string& primitive = entity.GetMeshComponent().PrimitiveType;
        if (primitive == "Cube") return MaraeUI::EditorIcon::Cube;
        if (primitive == "Sphere") return MaraeUI::EditorIcon::Sphere;
        if (primitive == "Cylinder") return MaraeUI::EditorIcon::Cylinder;
        if (primitive == "Cone") return MaraeUI::EditorIcon::Cone;
        if (primitive == "Torus") return MaraeUI::EditorIcon::Torus;
        if (primitive == "Capsule") return MaraeUI::EditorIcon::Capsule;
        if (primitive == "Plane" || primitive == "Terrain") return MaraeUI::EditorIcon::Plane;
        if (primitive == "Wedge") return MaraeUI::EditorIcon::Wedge;
        if (primitive == "Triangle") return MaraeUI::EditorIcon::Triangle;
        if (primitive == "Quad") return MaraeUI::EditorIcon::Image;
        if (primitive == "Circle2D") return MaraeUI::EditorIcon::Circle;
        if (primitive == "Pentagon2D") return MaraeUI::EditorIcon::Pentagon;
        if (primitive == "Hexagon2D") return MaraeUI::EditorIcon::Hexagon;
        if (primitive == "Octagon2D") return MaraeUI::EditorIcon::Octagon;
        if (primitive == "Diamond2D") return MaraeUI::EditorIcon::Diamond;
        if (primitive == "Star2D") return MaraeUI::EditorIcon::Star;
        if (primitive == "None") return MaraeUI::EditorIcon::Empty;
        return MaraeUI::EditorIcon::Model;
    }

    MaraeUI::EditorIcon ComponentIcon(const char* componentName)
    {
        if (!componentName) return MaraeUI::EditorIcon::Empty;
        if (std::strcmp(componentName, "Script C#") == 0 || std::strcmp(componentName, "Global Script") == 0) return MaraeUI::EditorIcon::Script;
        if (std::strcmp(componentName, "Light") == 0 || std::strcmp(componentName, "Global Light") == 0)
            return MaraeUI::EditorIcon::Light;
        if (std::strcmp(componentName, "Audio Source") == 0) return MaraeUI::EditorIcon::Audio;
        if (std::strcmp(componentName, "Camera") == 0) return MaraeUI::EditorIcon::Camera;
        if (std::strcmp(componentName, "Physics") == 0) return MaraeUI::EditorIcon::Physics;
        if (std::strcmp(componentName, "Animation") == 0) return MaraeUI::EditorIcon::Animation;
        if (std::strcmp(componentName, "Particle System 2D") == 0) return MaraeUI::EditorIcon::Animation;
        if (std::strcmp(componentName, "ScreenGui") == 0) return MaraeUI::EditorIcon::ScreenGui;
        if (std::strcmp(componentName, "Frame") == 0) return MaraeUI::EditorIcon::Frame;
        if (std::strcmp(componentName, "TextLabel") == 0 || std::strcmp(componentName, "TextBox") == 0)
            return MaraeUI::EditorIcon::Text;
        if (std::strcmp(componentName, "TextButton") == 0 || std::strcmp(componentName, "ImageButton") == 0)
            return MaraeUI::EditorIcon::Button;
        if (std::strcmp(componentName, "ImageLabel") == 0) return MaraeUI::EditorIcon::Image;
        if (std::strcmp(componentName, "ScrollingFrame") == 0 ||
            std::strcmp(componentName, "Slider") == 0 ||
            std::strcmp(componentName, "ProgressBar") == 0)
            return MaraeUI::EditorIcon::Frame;
        if (std::strcmp(componentName, "Checkbox") == 0 ||
            std::strcmp(componentName, "Dropdown") == 0)
            return MaraeUI::EditorIcon::Button;
        return MaraeUI::EditorIcon::Empty;
    }

    bool IsHierarchyEntityVisible(const Application& app, const Entity& entity)
    {
        return !entity.IsEditorOnly() || (entity.IsGlobalLight() && !app.Is2DWorldMode()) || entity.IsGlobalScript();
    }

    void SelectHierarchyEntity(Application& app, int index, bool additive)
    {
        if (!additive)
        {
            app.SetSelectedEntity(index);
            return;
        }

        std::vector<int> selection = app.GetMultiSelectedEntities();
        if (selection.empty() && app.GetSelectedEntityIndex() >= 0)
        {
            const auto& entities = app.GetScene().GetEntities();
            const int activeIndex = app.GetSelectedEntityIndex();
            if (activeIndex < static_cast<int>(entities.size()) &&
                app.GetSelectedID() == entities[static_cast<size_t>(activeIndex)].GetID())
                selection.push_back(activeIndex);
        }

        const auto existing = std::find(selection.begin(), selection.end(), index);
        if (existing == selection.end())
            selection.push_back(index);
        else
            selection.erase(existing);

        if (selection.empty())
        {
            app.SetSelectedEntity(-1);
            return;
        }

        // SetSelectedEntity limpa a seleção anterior; depois recompomos o
        // conjunto para que o item clicado seja o ativo quando continuar nele.
        app.SetSelectedEntity(index);
        app.SetMultiSelectedEntities(selection);
    }

    void CreateAnimationGroup(Application& app)
    {
        Scene& scene = app.GetScene();
        scene.UpdateTransforms();

        std::vector<uint64_t> selectedIDs;
        const auto& entities = scene.GetEntities();
        std::vector<int> selectedIndices = app.GetMultiSelectedEntities();
        if (selectedIndices.empty() && app.GetSelectedEntityIndex() >= 0)
            selectedIndices.push_back(app.GetSelectedEntityIndex());

        std::unordered_set<uint64_t> selectedSet;
        glm::vec3 groupPosition(0.0f);
        for (int index : selectedIndices)
        {
            if (index < 0 || index >= static_cast<int>(entities.size()))
                continue;
            const Entity& entity = entities[static_cast<size_t>(index)];
            if (entity.IsEditorOnly() || entity.IsGlobalLight() || entity.IsGlobalScript() || entity.HasCamera() ||
                entity.HasUI() || entity.IsFolder)
                continue;
            if (selectedSet.insert(entity.GetID()).second)
                selectedIDs.push_back(entity.GetID());
        }

        // Keep an already-selected parent/child subtree intact: only reparent
        // selected roots, so marquee grouping does not flatten a rig.
        std::erase_if(selectedIDs, [&](uint64_t id)
        {
            const Entity* entity = scene.FindEntityByID(id);
            uint64_t parentID = entity ? entity->GetParentID() : 0;
            std::unordered_set<uint64_t> visited;
            while (parentID != 0 && visited.insert(parentID).second)
            {
                if (selectedSet.contains(parentID))
                    return true;
                const Entity* parent = scene.FindEntityByID(parentID);
                parentID = parent ? parent->GetParentID() : 0;
            }
            return false;
        });

        for (uint64_t id : selectedIDs)
        {
            const Entity* entity = scene.FindEntityByID(id);
            if (entity)
                groupPosition += glm::vec3(
                    const_cast<Entity*>(entity)->GetTransform().WorldMatrix[3]);
        }
        if (!selectedIDs.empty())
            groupPosition /= static_cast<float>(selectedIDs.size());

        app.CreateModelEntity();
        Entity* group = scene.FindEntityByID(app.GetSelectedID());
        if (!group)
            return;

        static unsigned int animationGroupCounter = 1;
        group->SetName("Animation Group " + std::to_string(animationGroupCounter++));
        if (selectedIDs.empty())
        {
            const Transform& spawnTransform = group->GetTransform();
            groupPosition = glm::vec3(
                spawnTransform.Position.X, spawnTransform.Position.Y, spawnTransform.Position.Z);
        }
        group->GetTransform().Position = Vector3(groupPosition.x, groupPosition.y, groupPosition.z);
        group->GetTransform().UpdateLocalMatrix();
        AnimationComponent& animation = group->GetAnimationComponent();
        const Transform& transform = group->GetTransform();
        animation.SetClip(AnimationClip::CreateDefault(
            "New Animation", 5.0f, groupPosition, transform.Rotation,
            glm::vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z)));
        animation.Enabled = true;
        animation.PlayOnStart = true;

        const uint64_t groupID = group->GetID();
        for (uint64_t childID : selectedIDs)
            scene.SetParent(childID, groupID, true);

        app.MarkDirty();
    }

    void FocusHierarchyEntity(Application& app, uint64_t entityID)
    {
        Scene& scene = app.GetScene();
        Entity* entity = scene.FindEntityByID(entityID);
        if (!entity)
            return;
        scene.UpdateTransforms();
        const glm::vec3 center(entity->GetTransform().WorldMatrix[3]);
        Camera& camera = app.GetCamera();
        camera.Position = Vector3(center.x, center.y, center.z + 5.0f);

        const float dx = center.x - camera.Position.X;
        const float dy = center.y - camera.Position.Y;
        const float dz = center.z - camera.Position.Z;
        const float yaw = std::atan2(dx, -dz);
        const float distanceXZ = std::sqrt(dx * dx + dz * dz);
        const float pitch = std::atan2(dy, distanceXZ);
        camera.Rotation.Y = glm::degrees(yaw);
        camera.Rotation.X = glm::degrees(pitch);
        camera.Rotation.Z = 0.1f;
    }

    void BeginAddHierarchyChild(Application& app, uint64_t parentID)
    {
        s_PendingParentForChildrenIDs.clear();
        s_PendingChildParentID = parentID;
        s_EntityIDsBeforeChildCreation.clear();
        for (const Entity& entity : app.GetScene().GetEntities())
            s_EntityIDsBeforeChildCreation.insert(entity.GetID());
    }

    void BeginAddHierarchyParent(Application& app, const std::vector<uint64_t>& childrenIDs)
    {
        s_PendingChildParentID = 0;
        s_PendingParentForChildrenIDs = childrenIDs;
        s_EntityIDsBeforeChildCreation.clear();
        for (const Entity& entity : app.GetScene().GetEntities())
            s_EntityIDsBeforeChildCreation.insert(entity.GetID());

        app.GetShowHierarchy() = true;
        s_OpenChildCreatorPending = true;
    }

    void FinishAddHierarchyChild(Application& app)
    {
        if (s_PendingChildParentID == 0 && s_PendingParentForChildrenIDs.empty())
            return;
        Scene& scene = app.GetScene();

        if (s_PendingChildParentID != 0)
        {
            if (!scene.FindEntityByID(s_PendingChildParentID))
            {
                s_PendingChildParentID = 0;
                s_EntityIDsBeforeChildCreation.clear();
                return;
            }

            std::vector<uint64_t> newRoots;
            for (const Entity& entity : scene.GetEntities())
            {
                if (!s_EntityIDsBeforeChildCreation.contains(entity.GetID()) &&
                    entity.GetParentID() == 0 && entity.GetID() != s_PendingChildParentID)
                    newRoots.push_back(entity.GetID());
            }
            if (newRoots.empty())
            {
                s_PendingChildParentID = 0;
                s_EntityIDsBeforeChildCreation.clear();
                return;
            }
            for (uint64_t childID : newRoots)
            {
                const Entity* child = scene.FindEntityByID(childID);
                const Entity* parent = scene.FindEntityByID(s_PendingChildParentID);
                const bool preserveWorld = !child || !parent ||
                    !(child->HasUI() && parent->HasUI());
                scene.SetParent(childID, s_PendingChildParentID, preserveWorld);
            }
            app.MarkDirty();

            s_PendingChildParentID = 0;
            s_EntityIDsBeforeChildCreation.clear();
            return;
        }

        if (!s_PendingParentForChildrenIDs.empty())
        {
            std::vector<uint64_t> newRoots;
            for (const Entity& entity : scene.GetEntities())
            {
                if (!s_EntityIDsBeforeChildCreation.contains(entity.GetID()) &&
                    entity.GetParentID() == 0)
                    newRoots.push_back(entity.GetID());
            }
            if (!newRoots.empty())
            {
                uint64_t newParentID = newRoots.front();
                for (uint64_t childID : s_PendingParentForChildrenIDs)
                {
                    if (childID == newParentID)
                        continue;
                    const Entity* child = scene.FindEntityByID(childID);
                    const Entity* parent = scene.FindEntityByID(newParentID);
                    const bool preserveWorld = !child || !parent ||
                        !(child->HasUI() && parent->HasUI());
                    scene.SetParent(childID, newParentID, preserveWorld);
                }
                scene.UpdateTransforms();
                app.MarkDirty();
            }
            s_PendingParentForChildrenIDs.clear();
            s_EntityIDsBeforeChildCreation.clear();
        }
    }

    void CreateLightChild(Application& app, LightType type)
    {
        Scene& scene = app.GetScene();
        const Entity* parent = scene.FindEntityByID(s_PendingChildParentID);
        if (!parent)
            return;
        scene.UpdateTransforms();
        const glm::vec3 worldPosition(
            const_cast<Entity*>(parent)->GetTransform().WorldMatrix[3]);
        Entity& child = scene.CreateEntity();
        child.SetName(type == LightType::Point ? "Point Light" :
            type == LightType::Spot ? "Spot Light" :
            type == LightType::Directional ? "Directional Light" : "Area Light");
        child.GetTransform().Position = Vector3(worldPosition.x, worldPosition.y, worldPosition.z);
        child.GetMeshComponent().MeshData.reset();
        child.GetMeshComponent().MeshPath.clear();
        child.GetMeshComponent().PrimitiveType = "None";
        child.PhysicsEnabled = false;
        child.ColliderEnabled = false;
        child.RaycastTarget = false;
        child.AddLight(type);
        app.SetSelectedEntity(static_cast<int>(scene.GetEntities().size()) - 1);
        app.MarkDirty();
    }

    void CreateAudioChild(Application& app)
    {
        Scene& scene = app.GetScene();
        const Entity* parent = scene.FindEntityByID(s_PendingChildParentID);
        if (!parent)
            return;
        scene.UpdateTransforms();
        const glm::vec3 worldPosition(
            const_cast<Entity*>(parent)->GetTransform().WorldMatrix[3]);
        Entity& child = scene.CreateEntity();
        child.SetName("Audio Source");
        child.GetTransform().Position = Vector3(worldPosition.x, worldPosition.y, worldPosition.z);
        child.GetMeshComponent().MeshData.reset();
        child.GetMeshComponent().MeshPath.clear();
        child.GetMeshComponent().PrimitiveType = "None";
        child.PhysicsEnabled = false;
        child.ColliderEnabled = false;
        child.RaycastTarget = false;
        child.AddAudio();
        app.SetSelectedEntity(static_cast<int>(scene.GetEntities().size()) - 1);
        app.MarkDirty();
    }

    void DrawHierarchyEntityContextMenu(Application& app, Entity& entity, int entityIndex)
    {
        char popupID[64];
        snprintf(popupID, sizeof(popupID), "##entity_context_%llu",
            static_cast<unsigned long long>(entity.GetID()));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (entityIndex >= 0 && !app.IsMultiSelectedEntity(entityIndex))
                app.SetSelectedEntity(entityIndex);
            s_ContextRenameEntityID = entity.GetID();
            std::snprintf(s_ContextRenameBuffer, sizeof(s_ContextRenameBuffer), "%s",
                entity.GetName().c_str());
            s_ContextRenameFocusPending = true;
            ImGui::OpenPopup(popupID);
        }

        ImGui::SetNextWindowSize(ImVec2(250.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup(popupID))
        {
            ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), UI("Objeto"));
            ImGui::Separator();
            if (ImGui::IsWindowAppearing() && s_ContextRenameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                s_ContextRenameFocusPending = false;
            }
            ImGui::SetNextItemWidth(132.0f);
            const bool renameSubmitted = ImGui::InputText("##HierarchyRename",
                s_ContextRenameBuffer, sizeof(s_ContextRenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            const bool renameClicked = ImGui::Button(UI("Renomear"));
            if ((renameSubmitted || renameClicked) && s_ContextRenameEntityID == entity.GetID())
            {
                const std::string newName = s_ContextRenameBuffer;
                if (!newName.empty() && newName != entity.GetName())
                {
                    entity.SetName(newName);
                    app.MarkDirty();
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem(UI("Duplicar"), "Ctrl+D"))
                s_PendingHierarchyContextActions.emplace_back(
                    HierarchyContextAction::Duplicate, entity.GetID());
            if (ImGui::MenuItem(UI("Excluir"), "Delete"))
                s_PendingHierarchyContextActions.emplace_back(
                    HierarchyContextAction::Delete, entity.GetID());
            if (ImGui::MenuItem(UI("Focar"), "F"))
                FocusHierarchyEntity(app, entity.GetID());
            bool selectionLocked = entity.SelectionLocked;
            if (ImGui::MenuItem(selectionLocked ? UI("Desbloquear seleção") : UI("Bloquear seleção")))
            {
                entity.SelectionLocked = !selectionLocked;
                app.MarkDirty();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(UI("Adicionar filho")))
            {
                BeginAddHierarchyChild(app, entity.GetID());
                ImGui::CloseCurrentPopup();
                s_OpenChildCreatorPending = true;
            }
            if (ImGui::MenuItem(UI("Adicionar componente")))
            {
                char componentPopupID[64];
                snprintf(componentPopupID, sizeof(componentPopupID), "##add_comp_%llu",
                    static_cast<unsigned long long>(entity.GetID()));
                ImGui::CloseCurrentPopup();
                ImGui::OpenPopup(componentPopupID);
            }
            ImGui::EndPopup();
        }
    }
}

static bool WouldCreateParentCycle(Scene& scene, uint64_t childID, uint64_t parentID)
{
    if (childID == parentID)
        return true;

    Entity* cursor = scene.FindEntityByID(parentID);
    std::unordered_set<uint64_t> visited;
    while (cursor)
    {
        if (cursor->GetID() == childID || !visited.insert(cursor->GetID()).second)
            return true;
        const uint64_t ancestorID = cursor->GetParentID();
        cursor = ancestorID == 0 ? nullptr : scene.FindEntityByID(ancestorID);
    }
    return false;
}

static bool HasValidParentChain(Scene& scene, const Entity& entity)
{
    std::unordered_set<uint64_t> visited;
    const Entity* current = &entity;
    while (current)
    {
        if (!visited.insert(current->GetID()).second)
            return false;
        const uint64_t parentID = current->GetParentID();
        if (parentID == 0)
            return true;
        current = scene.FindEntityByID(parentID);
    }
    return false;
}

static Entity* CreateEntityFromAsset(Application& app, const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" ||
        extension == ".glb" || extension == ".mesh2d")
        return ModelImporter::ImportToScene(path, app.GetScene());

    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".tga" || extension == ".webp" ||
        extension == ".hdr")
    {
        Entity& entity = app.GetScene().CreateEntity();
        entity.SetName(std::filesystem::path(path).stem().string());
        entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
        entity.GetMeshComponent().MeshData->Create(
            MeshFactory::CreateQuad(), GL_TRIANGLES, "HierarchyPanel::ImageAsset");
        entity.GetMeshComponent().MeshPath = "builtin://quad";
        entity.GetMaterialComponent().MaterialData.TexturePath = path;
        entity.GetMaterialComponent().MaterialData.TextureName =
            std::filesystem::path(path).stem().string();
        entity.GetMaterialComponent().MaterialData.DiffuseTexture = TextureCache::Load(path);
        entity.GetColorComponent().Color = Vector3(1.0f, 1.0f, 1.0f);
        entity.PickingRadius = 1.0f;
        return &entity;
    }
    return nullptr;
}

static Vector3 AssetSpawnPosition(Application& app)
{
    const Camera& camera = app.GetCamera();
    const float yaw = camera.Rotation.Y * 3.1415926535f / 180.0f;
    const float pitch = camera.Rotation.X * 3.1415926535f / 180.0f;
    const float distance = 4.0f;
    return Vector3(
        camera.Position.X + std::sin(yaw) * std::cos(pitch) * distance,
        camera.Position.Y - std::sin(pitch) * distance,
        camera.Position.Z - std::cos(yaw) * std::cos(pitch) * distance);
}

static void CreateUIElementEntity(Application& app, UIElementType type)
{
    Entity& entity = app.GetScene().CreateEntity();
    entity.AddUI(type);
    // UI 2D não é uma malha 3D. A entidade continua existindo na cena para
    // seleção, hierarquia e serialização, mas não deve carregar o Cube
    // padrão do Entity/MeshComponent.
    entity.GetMeshComponent().MeshData.reset();
    entity.GetMeshComponent().MeshPath.clear();
    entity.GetMeshComponent().PrimitiveType = "UI";
    entity.PhysicsEnabled = false;
    entity.ColliderEnabled = false;
    entity.SetName(UIElementTypeName(type));

    UIComponent& ui = entity.GetUIComponent();
    ui.PositionX = type == UIElementType::ScreenGui ? 0.0f : 40.0f;
    ui.PositionY = type == UIElementType::ScreenGui ? 0.0f : 40.0f;
    ui.Width = type == UIElementType::ScreenGui ? 960.0f : 240.0f;
    ui.Height = type == UIElementType::ScreenGui ? 540.0f : 64.0f;

    // Valores padrão por widget, para que o elemento já apareça utilizável.
    switch (type)
    {
    case UIElementType::TextLabel:
    case UIElementType::TextButton:
    case UIElementType::TextBox:
        ui.Text = UIElementTypeName(type);
        break;
    case UIElementType::Checkbox:
        ui.Text = UIElementTypeName(type);
        ui.Width = 200.0f;
        ui.Height = 28.0f;
        break;
    case UIElementType::Slider:
        ui.MinValue = 0.0f;
        ui.MaxValue = 1.0f;
        ui.Value = 0.5f;
        ui.Width = 240.0f;
        ui.Height = 24.0f;
        break;
    case UIElementType::ProgressBar:
        ui.MinValue = 0.0f;
        ui.MaxValue = 1.0f;
        ui.Value = 0.5f;
        ui.Width = 240.0f;
        ui.Height = 24.0f;
        break;
    case UIElementType::Dropdown:
        ui.Options = { "Opção 1", "Opção 2", "Opção 3" };
        ui.SelectedIndex = 0;
        ui.Width = 220.0f;
        ui.Height = 32.0f;
        break;
    case UIElementType::ScrollingFrame:
        ui.ClipsContent = true;
        ui.ScrollViewEnabled = true;
        ui.Height = 200.0f;
        break;
    default:
        break;
    }

    app.SetSelectedEntity(static_cast<int>(app.GetScene().GetEntities().size()) - 1);
    app.MarkDirty();
}



// Popup de adicionar componente específico para uma entidade
// A chave do popup inclui o ID da entidade para ser único por item
static void DrawAddComponentPopup(Application& app, Entity& entity)
{
    // ID único por entidade
    char popupId[64];
    snprintf(popupId, sizeof(popupId), "##add_comp_%llu", (unsigned long long)entity.GetID());

    if (!ImGui::BeginPopup(popupId))
        return;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), L("component.add"));
    ImGui::Separator();
    ImGui::Dummy({0.f, 2.f});

    // ---- Contêineres ----
    MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Folder, UI("Organização"), ImVec4(1, 1, 1, 1));
    ImGui::Separator();
    if (!entity.IsFolder && !entity.IsModel)
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Folder, UI("Adicionar Pasta")))
        {
            entity.IsFolder = true;
            entity.IsModel = false;
            entity.PhysicsEnabled = false;
            entity.ColliderEnabled = false;
            entity.RaycastTarget = false;
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Model, UI("Adicionar Modelo")))
        {
            entity.IsFolder = false;
            entity.IsModel = true;
            entity.PhysicsEnabled = false;
            entity.ColliderEnabled = false;
            entity.RaycastTarget = false;
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("  Tipo: %s", entity.IsModel ? "Modelo" : "Pasta");
        if (ImGui::MenuItem(UI("  Remover tipo de contêiner")))
        {
            entity.IsFolder = false;
            entity.IsModel = false;
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});

    // ---- Script C# ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
    MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Script, "Script C#", ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();

    ScriptComponent& script = entity.GetScriptComponent();
    if (script.ScriptPath.empty())
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Script, L("component.new_script")))
        {
            app.CreateScriptForSelectedEntity();
            ImGui::CloseCurrentPopup();
        }
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Script, L("component.open_vscode")))
        {
            app.CreateScriptForSelectedEntity();
            app.OpenSelectedScriptInVSCode();
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("  %s: %s", L("component.script_associated"),
            script.ScriptPath.c_str());
    }

    ImGui::Dummy({0.f, 4.f});

    // ---- UI 2D (ScreenGui / GuiObject) ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 1.0f, 1.0f));
    MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::ScreenGui, UI("UI 2D"), ImVec4(0.45f, 0.85f, 1.0f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (!entity.HasUI())
    {
        const struct { const char* label; UIElementType type; MaraeUI::EditorIcon icon; } uiTypes[] = {
            { "ScreenGui", UIElementType::ScreenGui, MaraeUI::EditorIcon::ScreenGui },
            { "Frame", UIElementType::Frame, MaraeUI::EditorIcon::Frame },
            { "TextLabel", UIElementType::TextLabel, MaraeUI::EditorIcon::Text },
            { "TextButton", UIElementType::TextButton, MaraeUI::EditorIcon::Button },
            { "ImageLabel", UIElementType::ImageLabel, MaraeUI::EditorIcon::Image },
            { "ImageButton", UIElementType::ImageButton, MaraeUI::EditorIcon::Button },
            { "TextBox", UIElementType::TextBox, MaraeUI::EditorIcon::Text },
            { "ScrollingFrame", UIElementType::ScrollingFrame, MaraeUI::EditorIcon::Frame },
            { "Slider", UIElementType::Slider, MaraeUI::EditorIcon::Frame },
            { "Checkbox", UIElementType::Checkbox, MaraeUI::EditorIcon::Button },
            { "ProgressBar", UIElementType::ProgressBar, MaraeUI::EditorIcon::Frame },
            { "Dropdown", UIElementType::Dropdown, MaraeUI::EditorIcon::Frame }
        };
        for (const auto& ui : uiTypes)
        {
            if (MaraeUI::IconMenuItem(ui.icon, ui.label))
            {
                entity.AddUI(ui.type);
                if (app.Is2DWorldMode())
                {
                    entity.GetMeshComponent().MeshData.reset();
                    entity.GetMeshComponent().MeshPath.clear();
                    entity.GetMeshComponent().PrimitiveType = "UI";
                    entity.PhysicsEnabled = false;
                    entity.ColliderEnabled = false;
                }
                entity.SetName(UIElementTypeName(ui.type));
                UIComponent& addedUI = entity.GetUIComponent();
                if (ui.type == UIElementType::TextButton || ui.type == UIElementType::TextLabel ||
                    ui.type == UIElementType::TextBox || ui.type == UIElementType::Checkbox)
                    addedUI.Text = UIElementTypeName(ui.type);
                else if (ui.type == UIElementType::Dropdown)
                    addedUI.Options = { "Opção 1", "Opção 2", "Opção 3" };
                else if (ui.type == UIElementType::ProgressBar || ui.type == UIElementType::Slider)
                    addedUI.Value = 0.5f;
                else if (ui.type == UIElementType::ScrollingFrame)
                {
                    addedUI.ClipsContent = true;
                    addedUI.ScrollViewEnabled = true;
                }
                app.MarkDirty();
                ImGui::CloseCurrentPopup();
            }
        }
    }
    else
    {
        ImGui::TextDisabled("  Tipo: %s", UIElementTypeName(entity.GetUIComponent().Type));
        if (ImGui::MenuItem(UI("  Remover UI 2D")))
        {
            entity.RemoveUI();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});

    // ---- Light ----
    // Luzes são componentes do espaço 3D; não aparecem no menu quando o
    // usuário está trabalhando no mundo 2D.
    if (!app.Is2DWorldMode())
    {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Light, L("common.light"), ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!entity.HasLight())
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Light, L("component.add_point")))
        {
            entity.AddLight(LightType::Point);
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::SpotLight, L("component.add_spot")))
        {
            entity.AddLight(LightType::Spot);
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::DirectionalLight, UI("Adicionar Directional")))
        {
            entity.AddLight(LightType::Directional);
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::AreaLight, UI("Adicionar Area Light")))
        {
            entity.AddLight(LightType::Area);
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("  %s", L("component.light_already"));
        if (ImGui::MenuItem((std::string("  ") + L("component.remove_light")).c_str()))
        {
            entity.RemoveLight();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});
    }

    // Emissor de sprites 2D voltados para a câmera, dentro do mundo 3D.
    if (!app.Is2DWorldMode())
    {
        MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Animation,
            UI("Efeitos 2D no mundo 3D"), ImVec4(0.55f, 0.82f, 1.0f, 1.0f));
        ImGui::Separator();
        if (!entity.HasParticleSystem())
        {
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Animation,
                    UI("Adicionar sistema de partículas 2D")))
            {
                entity.AddParticleSystem();
                app.MarkDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        else if (ImGui::MenuItem(UI("  Remover sistema de partículas 2D")))
        {
            entity.RemoveParticleSystem();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Dummy({0.f, 4.f});
    }

    // IA de jogo (perception, behavior tree e FSM); independente do painel
    // assistente que gera scripts C#.
    MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Script,
        UI("IA de jogo"), ImVec4(0.65f, 0.85f, 1.0f, 1.0f));
    ImGui::Separator();
    if (!entity.HasAI())
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Script,
                UI("Adicionar componente de IA")))
        {
            entity.AddAI();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }
    else if (ImGui::MenuItem(UI("  Remover componente de IA")))
    {
        entity.RemoveAI();
        app.MarkDirty();
        ImGui::CloseCurrentPopup();
    }

    // ---- Physics ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
        MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Physics, L("common.physics"), ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!entity.PhysicsEnabled)
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Physics, L("component.enable_physics")))
        {
            entity.PhysicsEnabled = true;
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("  %s", L("component.physics_already"));
        if (ImGui::MenuItem((std::string("  ") + L("component.disable_physics")).c_str()))
        {
            entity.PhysicsEnabled = false;
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});

    // ---- Animation ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.8f, 1.0f));
        MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Animation, L("common.animation"), ImVec4(1.0f, 0.6f, 0.8f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();

    AnimationComponent& anim = entity.GetAnimationComponent();
    if (anim.Clip && !anim.Clip->Name.empty())
    {
        ImGui::TextDisabled("  %s", L("component.animation_already"));
        ImGui::TextWrapped(UI("  %s"), L("component.create_timeline_hint"));
    }
    else
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Animation, L("component.create_animation")))
        {
            // Cria um clip inicial com duas poses válidas. Um clip vazio
            // pode ser avaliado durante o playback e produzir escala zero /
            // quaternion inválido antes de o usuário inserir os keyframes.
            const Transform& transform = entity.GetTransform();
            const glm::vec3 position(
                transform.Position.X, transform.Position.Y, transform.Position.Z);
            const glm::vec3 scale(
                transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
            anim.Clip = AnimationClip::CreateDefault(
                "New Animation", 5.0f, position, transform.Rotation, scale);
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});

    // ---- Audio Source ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Audio, L("common.audio_source"), ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!entity.HasAudio())
    {
        if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Audio, L("component.create_audio")))
        {
            entity.AddAudio();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("  %s", L("component.already_added"));
        if (ImGui::MenuItem((std::string("  ") + L("component.test_audio")).c_str()))
        {
            app.PreviewAudioForSelectedEntity();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem((std::string("  ") + L("component.stop_audio")).c_str()))
        {
            app.StopAudioPreview();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem((std::string("  ") + L("component.remove_audio")).c_str()))
        {
            app.StopAudioPreview();
            entity.RemoveAudio();
            app.MarkDirty();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void HierarchyPanel::DrawComponentNode(
    Application& app,
    Entity& entity,
    const char* componentName,
    uint64_t componentID
)
{
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_NoTreePushOnOpen;

    uint64_t selectedID = app.GetSelectedID();
    const bool selected = selectedID == componentID;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushStyleColor(ImGuiCol_Header,
        selected ? ImVec4(0.12f, 0.43f, 0.70f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.34f, 0.48f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.16f, 0.48f, 0.76f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.72f, 0.79f, 0.88f, 1.0f));

    ImGui::TreeNodeEx(
        (void*)(uintptr_t)componentID,
        flags,
        "   %s",
        componentName
    );
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    const float iconSize = std::max(11.0f, std::min(14.0f, rowMax.y - rowMin.y - 3.0f));
    MaraeUI::DrawEditorIcon(ImGui::GetWindowDrawList(), ComponentIcon(componentName),
        ImVec2(rowMin.x + 18.0f, rowMin.y + (rowMax.y - rowMin.y - iconSize) * 0.5f), iconSize);
    ImGui::PopStyleColor(4);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(UI("Componente anexado ao objeto; não é um objeto 3D independente."));

    // Selecionar ao clicar no componente
    if (ImGui::IsItemClicked(0))
    {
        app.SetSelectedID(componentID);

        // Abrir Timeline automaticamente ao clicar em Animation
        if (strcmp(componentName, "Animation") == 0)
        {
            app.SetShowTimeline(true);
        }
    }

    // Duplo clique em script abre o internal editor (alternativa que funciona no ImGui)
    static float lastClickTime = 0.0f;
    static uint64_t lastClickedComponentID = 0;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && strcmp(componentName, "Script C#") == 0)
    {
        const float currentTime = static_cast<float>(ImGui::GetTime());
        if (componentID == lastClickedComponentID && (currentTime - lastClickTime) < 0.3f)
        {
            // Duplo clique detectado
            const ScriptComponent& script = entity.GetScriptComponent();
            if (!script.ScriptPath.empty())
            {
                app.OpenScriptInInternalEditor(script.ScriptPath);
                std::cout << "Abrindo script no editor: " << script.ScriptPath << std::endl;
            }
            lastClickTime = 0.0f; // Reset para evitar triplo clique
            lastClickedComponentID = 0;
        }
        else
        {
            lastClickTime = currentTime;
            lastClickedComponentID = componentID;
        }
    }
}

void HierarchyPanel::DrawEntityNode(
    Application& app,
    Entity& entity
)
{
    if (!s_HierarchyVisitedEntities.insert(entity.GetID()).second)
        return; // protege contra ciclos/referências duplicadas na árvore serializada

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    // Verifica se a entidade tem componentes para mostrar como filhos
    int componentCount = 0;
    const ScriptComponent& script = entity.GetScriptComponent();
    if (!script.ScriptPath.empty()) ++componentCount;
    if (!app.Is2DWorldMode() && entity.HasLight()) ++componentCount;
    if (entity.HasAudio()) ++componentCount;
    if (!app.Is2DWorldMode() && entity.HasParticleSystem()) ++componentCount;
    if (!app.Is2DWorldMode() && entity.HasCamera()) ++componentCount;
    if (app.Is2DWorldMode() ? entity.ColliderEnabled
                            : entity.PhysicsEnabled) ++componentCount;
    if (entity.IsGlobalLight()) ++componentCount;
    if (entity.IsGlobalScript()) ++componentCount;
    if (entity.GetAnimationComponent().Clip && !entity.GetAnimationComponent().Clip->Name.empty()) ++componentCount;
    if (entity.HasUI()) ++componentCount;

    bool hasChildren = false;
    for (const Entity& child : app.GetScene().GetEntities())
    {
        if (child.GetParentID() == entity.GetID() &&
            IsHierarchyEntityVisible(app, child))
        {
            hasChildren = true;
            break;
        }
    }
    if (!hasChildren && componentCount == 0)
        flags |= ImGuiTreeNodeFlags_Leaf;

    const auto& entities = app.GetEntities();
    int entityIndex = -1;

    for (size_t i = 0; i < entities.size(); i++)
    {
        if (entities[i].GetID() == entity.GetID())
        {
            entityIndex = static_cast<int>(i);
            if (app.GetSelectedID() == entity.GetID() || app.IsMultiSelectedEntity(entityIndex))
                flags |= ImGuiTreeNodeFlags_Selected;
            break;
        }
    }

    const bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;
    if (s_HierarchyForceTreeState >= 0)
        ImGui::SetNextItemOpen(s_HierarchyForceTreeState != 0, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_Header,
        selected ? ImVec4(0.08f, 0.38f, 0.68f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.31f, 0.43f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.46f, 0.78f, 1.0f));
    const bool isPrefabLinked = !entity.PrefabPath.empty();
    const bool isContainer = entity.IsFolder || entity.IsModel;
    if (isPrefabLinked || isContainer)
        ImGui::PushStyleColor(ImGuiCol_Text, isPrefabLinked
            ? ImVec4(0.45f, 0.78f, 1.0f, 1.0f)
            : (entity.IsModel ? ImVec4(0.92f, 0.76f, 0.42f, 1.0f)
                              : ImVec4(0.67f, 0.78f, 0.91f, 1.0f)));

    const bool editingName = s_InlineRenameEntityID == entity.GetID();
    bool opened = ImGui::TreeNodeEx(
        (void*)(uintptr_t)entity.GetID(),
        flags,
        editingName
            ? (entity.SelectionLocked ? "   [Bloqueado]" : "   ")
            : (entity.SelectionLocked ? "   %s  [Bloqueado]" : "   %s"),
        entity.GetName().c_str()
    );

    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    if (entityIndex >= 0)
    {
        const float left = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x -
            ImGui::GetStyle().WindowPadding.x;
        s_HierarchySelectionRows.push_back({
            entityIndex, ImVec2(left, rowMin.y), ImVec2(right, rowMax.y) });
    }
    const float iconSize = std::max(11.0f, std::min(14.0f, rowMax.y - rowMin.y - 3.0f));
    MaraeUI::DrawEditorIcon(ImGui::GetWindowDrawList(), EntityIcon(entity),
        ImVec2(rowMin.x + 18.0f, rowMin.y + (rowMax.y - rowMin.y - iconSize) * 0.5f), iconSize);

    // Botão / Ícone interativo de bloqueio de seleção no lado direito da linha
    const float lockBtnSize = 16.0f;
    const float lockBtnX = (ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) - lockBtnSize - 4.0f;
    const float lockBtnY = rowMin.y + (rowMax.y - rowMin.y - lockBtnSize) * 0.5f;
    const ImVec2 lockMin(lockBtnX, lockBtnY);
    const ImVec2 lockMax(lockBtnX + lockBtnSize, lockBtnY + lockBtnSize);

    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const bool lockHovered = mousePos.x >= lockMin.x && mousePos.x <= lockMax.x &&
                             mousePos.y >= lockMin.y && mousePos.y <= lockMax.y &&
                             ImGui::IsWindowHovered();

    if (entity.SelectionLocked || lockHovered)
    {
        const ImU32 lockColor = entity.SelectionLocked
            ? (lockHovered ? IM_COL32(255, 195, 60, 255) : IM_COL32(240, 150, 40, 255))
            : (lockHovered ? IM_COL32(200, 215, 235, 220) : IM_COL32(130, 145, 165, 140));

        if (lockHovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(lockMin, lockMax, IM_COL32(255, 255, 255, 35), 3.0f);
            ImGui::SetTooltip(entity.SelectionLocked
                ? UI("Seleção bloqueada no viewport. Clique para desbloquear.")
                : UI("Bloquear seleção no viewport"));
        }

        MaraeUI::DrawEditorIcon(ImGui::GetWindowDrawList(),
            entity.SelectionLocked ? MaraeUI::EditorIcon::Lock : MaraeUI::EditorIcon::Unlock,
            ImVec2(lockBtnX + 2.0f, lockBtnY + 2.0f), lockBtnSize - 4.0f, lockColor);

        if (lockHovered && ImGui::IsMouseClicked(0))
        {
            entity.SelectionLocked = !entity.SelectionLocked;
            app.MarkDirty();
        }
    }

    if (isPrefabLinked || isContainer)
        ImGui::PopStyleColor();
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered() && isContainer)
    {
        ImGui::SetTooltip(entity.IsModel
            ? UI("Modelo: grupo espacial; mover, girar ou escalar afeta os filhos.")
            : UI("Pasta: organização da árvore; filhos não herdam transformações da pasta."));
    }

    const bool isAlreadySelected = (app.GetSelectedID() == entity.GetID()) ||
                                   (entityIndex >= 0 && app.IsMultiSelectedEntity(entityIndex));
    const bool hasMultiSelection = app.GetMultiSelectedEntities().size() > 1;

    // Seleção simples/Ctrl+clique múltipla sem confundir componentes virtuais
    // com entidades reais.
    if (ImGui::IsItemClicked(0) && !lockHovered)
    {
        // Se o item clicado já faz parte da seleção múltipla e não há modificador (Ctrl/Shift),
        // NÃO desseleciona no clique (MouseDown) para permitir que o usuário arraste todos os selecionados juntos!
        if (isAlreadySelected && hasMultiSelection && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
        {
            s_PotentialSingleSelectIndex = entityIndex;
            // Registra esse item como possível origem de multi-drag
            s_MultiDragSourceIndex = entityIndex;
            s_MultiDragStarted = false;
        }
        else
        {
            if (entityIndex >= 0)
                SelectHierarchyEntity(app, entityIndex, ImGui::GetIO().KeyCtrl);
            s_PotentialSingleSelectIndex = -1;
            s_MultiDragSourceIndex = -1;
            s_MultiDragStarted = false;
        }
    }

    // Se soltou o mouse no mesmo item sem arrastar (foi apenas um clique simples exclusivo):
    if (s_PotentialSingleSelectIndex == entityIndex && ImGui::IsMouseReleased(0))
    {
        if (!s_MultiDragStarted && ImGui::IsItemHovered())
        {
            SelectHierarchyEntity(app, entityIndex, false);
        }
        s_PotentialSingleSelectIndex = -1;
        s_MultiDragSourceIndex = -1;
        s_MultiDragStarted = false;
    }

    if (IsHierarchyNameDoubleClicked(entity, rowMin, rowMax))
        BeginHierarchyInlineRename(entity);

    DrawHierarchyEntityContextMenu(app, entity, entityIndex);
    DrawAddComponentPopup(app, entity);

    // Drag source: só ativa se o item clicado estiver selecionado e o usuário estiver arrastando!
    if (isAlreadySelected && !ImGui::GetIO().KeyAlt && !s_HierarchyMarqueeActive &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        s_PotentialSingleSelectIndex = -1; // Arrasto confirmado! Mantém todos selecionados!
        s_MultiDragStarted = true;
        s_DraggedEntityIDs.clear();
        const std::vector<int>& multiSel = app.GetMultiSelectedEntities();
        const bool inMultiSel = std::find(multiSel.begin(), multiSel.end(), entityIndex) != multiSel.end();
        if (inMultiSel && multiSel.size() > 1)
        {
            const auto& allEnts = app.GetScene().GetEntities();
            for (int idx : multiSel)
            {
                if (idx >= 0 && idx < static_cast<int>(allEnts.size()))
                    s_DraggedEntityIDs.push_back(allEnts[static_cast<size_t>(idx)].GetID());
            }
        }
        else
        {
            s_DraggedEntityIDs.push_back(entity.GetID());
        }

        uint64_t id = entity.GetID();
        ImGui::SetDragDropPayload("ENTITY", &id, sizeof(uint64_t));

        if (s_DraggedEntityIDs.size() > 1)
        {
            ImGui::Text(UI("Tornar filhos: %d objetos selecionados"), static_cast<int>(s_DraggedEntityIDs.size()));
        }
        else
        {
            ImGui::Text(UI("Tornar filho: %s"), entity.GetName().c_str());
        }

        ImGui::EndDragDropSource();
    }

    // Drag target
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            // Feedback visual sobre o objeto que será o novo pai
            const ImVec2 targetMin = ImGui::GetItemRectMin();
            const ImVec2 targetMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(targetMin, targetMax, IM_COL32(90, 195, 255, 255), 2.0f, 0, 2.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(targetMin, targetMax, IM_COL32(40, 140, 240, 45), 2.0f);

            if (payload->Delivery)
            {
                std::vector<uint64_t> idsToReparent = s_DraggedEntityIDs;
                if (idsToReparent.empty() && payload->DataSize == sizeof(uint64_t))
                {
                    uint64_t singleID = 0;
                    std::memcpy(&singleID, payload->Data, sizeof(singleID));
                    idsToReparent.push_back(singleID);
                }

                Scene& scene = app.GetScene();
                for (uint64_t draggedID : idsToReparent)
                {
                    if (draggedID == entity.GetID())
                        continue;
                    if (WouldCreateParentCycle(scene, draggedID, entity.GetID()))
                        continue;

                    Entity* child = scene.FindEntityByID(draggedID);
                    const bool preserveWorld = !child || !(child->HasUI() && entity.HasUI());
                    scene.SetParent(draggedID, entity.GetID(), preserveWorld);
                }

                scene.UpdateTransforms();
                app.MarkDirty();
                s_DraggedEntityIDs.clear();
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && path[0] != '\0')
                s_PendingHierarchyAssetDrops.emplace_back(path, entity.GetID());
        }

        ImGui::EndDragDropTarget();
    }

    DrawHierarchyInlineRename(app, entity, rowMin, rowMax);

    if (opened)
    {
        if (entity.GetParentID() != 0 &&
            ImGui::SmallButton((std::string("Desvincular##Unparent_") +
                                std::to_string(entity.GetID())).c_str()))
        {
            app.GetScene().RemoveParent(entity.GetID());
            app.MarkDirty();
        }

        // Componentes são metadados/comportamentos do objeto, não filhos
        // geométricos. Agrupá-los visualmente evita confundi-los com objetos
        // que podem ser reparentados.
        if (componentCount > 0)
        {
            if (s_HierarchyForceTreeState >= 0)
                ImGui::SetNextItemOpen(s_HierarchyForceTreeState != 0, ImGuiCond_Always);
            const std::string componentGroupID =
                "##HierarchyComponents_" + std::to_string(entity.GetID());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.72f, 0.89f, 1.0f));
            const bool componentsOpen = ImGui::TreeNodeEx(
                componentGroupID.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth |
                ImGuiTreeNodeFlags_DefaultOpen, "Componentes (%d)", componentCount);
            ImGui::PopStyleColor();
            if (componentsOpen)
            {
        const ScriptComponent& scriptComp = entity.GetScriptComponent();
        if (!scriptComp.ScriptPath.empty())
        {
            uint64_t scriptID = entity.GetID() | 0x100000000ULL; // ID virtual para script
            DrawComponentNode(app, entity, "Script C#", scriptID);
        }

        if (!app.Is2DWorldMode() && entity.HasLight())
        {
            uint64_t lightID = entity.GetID() | 0x200000000ULL; // ID virtual para light
            DrawComponentNode(app, entity, "Light", lightID);
        }

        if (entity.HasAudio())
        {
            uint64_t audioID = entity.GetID() | 0x500000000ULL; // ID virtual para audio
            DrawComponentNode(app, entity, "Audio Source", audioID);
        }

        if (!app.Is2DWorldMode() && entity.HasParticleSystem())
        {
            uint64_t particleID = entity.GetID() | 0x900000000ULL;
            DrawComponentNode(app, entity, "Particle System 2D", particleID);
        }

        if (!app.Is2DWorldMode() && entity.HasCamera())
        {
            uint64_t cameraID = entity.GetID() | 0x600000000ULL;
            DrawComponentNode(app, entity, "Camera", cameraID);
        }

        if (app.Is2DWorldMode() ? entity.ColliderEnabled : entity.PhysicsEnabled)
        {
            uint64_t physicsID = entity.GetID() | 0x300000000ULL; // ID virtual para physics
            DrawComponentNode(app, entity,
                app.Is2DWorldMode() ? "Collider 2D" : "Physics", physicsID);
        }

        if (entity.GetAnimationComponent().Clip && !entity.GetAnimationComponent().Clip->Name.empty())
        {
            uint64_t animID = entity.GetID() | 0x400000000ULL; // ID virtual para animation
            DrawComponentNode(app, entity, "Animation", animID);
        }

        if (entity.HasUI())
        {
            uint64_t uiID = entity.GetID() | 0x700000000ULL;
            DrawComponentNode(app, entity, UIElementTypeName(entity.GetUIComponent().Type), uiID);
        }
        if (entity.IsGlobalLight())
        {
            uint64_t globalLightID = entity.GetID() | 0x800000000ULL;
            DrawComponentNode(app, entity, "Global Light", globalLightID);
        }
        if (entity.IsGlobalScript())
        {
            uint64_t globalScriptID = entity.GetID() | 0x900000000ULL;
            DrawComponentNode(app, entity, "Global Script", globalScriptID);
        }
                ImGui::TreePop();
            }
        }

        // ParentID é a relação canônica; isso também recupera filhos quando
        // a lista auxiliar ChildrenIDs ficou desatualizada após um drop.
        std::vector<uint64_t> childIDs;
        for (const Entity& child : app.GetScene().GetEntities())
        {
            if (child.GetParentID() == entity.GetID() &&
                IsHierarchyEntityVisible(app, child))
                childIDs.push_back(child.GetID());
        }
        for (uint64_t childID : childIDs)
        {
            Entity* child = app.GetScene().FindEntityByID(childID);
            if (child && child->GetParentID() == entity.GetID())
                DrawEntityNode(app, *child);
        }

        ImGui::TreePop();
    }
}

static void DrawCreateEntityPopup(Application& app)
{
    if (!ImGui::BeginPopupModal("##hierarchy_create_popup", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if ((s_PendingChildParentID != 0 || !s_PendingParentForChildrenIDs.empty()) &&
            !ImGui::IsPopupOpen("##hierarchy_create_popup"))
        {
            s_PendingChildParentID = 0;
            s_PendingParentForChildrenIDs.clear();
            s_EntityIDsBeforeChildCreation.clear();
        }
        return;
    }

    if (!s_PendingParentForChildrenIDs.empty())
    {
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), UI("Adicionar pai para %d objeto(s) selecionado(s)"),
            static_cast<int>(s_PendingParentForChildrenIDs.size()));
        ImGui::Separator();
    }
    else if (s_PendingChildParentID != 0)
    {
        const Entity* parent = app.GetScene().FindEntityByID(s_PendingChildParentID);
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), UI("Adicionar filho de: %s"),
            parent ? parent->GetName().c_str() : UI("(objeto removido)"));
        ImGui::Separator();
    }

    const float btnW = 140.0f;
    const ImVec2 btnSize = ImVec2(btnW, 0.0f);

    if (ImGui::BeginTabBar("##create_tabs", ImGuiTabBarFlags_None))
    {
        // =========================================================
        // Aba 1: Formatos Geometricos 3D
        // =========================================================
        if (!app.Is2DWorldMode() && ImGui::BeginTabItem(L("create.basic3d")))
        {
            ImGui::TextUnformatted(L("create.primitives"));
            ImGui::Separator();

            if (ImGui::Button(L("create.cube"), btnSize))      { app.CreateCubeEntity();      ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(L("create.sphere"), btnSize))    { app.CreateSphereEntity();    ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(L("create.cylinder"), btnSize))  { app.CreateCylinderEntity();  ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(L("create.cone"), btnSize))      { app.CreateConeEntity();      ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(L("create.wedge"), btnSize))     { app.CreateWedgeEntity();     ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(L("create.torus"), btnSize)) { app.CreateTorusEntity();  ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(L("create.capsule"), btnSize))   { app.CreateCapsuleEntity();   ImGui::CloseCurrentPopup(); }

            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 2: Planos & 2D
        // =========================================================
        if (ImGui::BeginTabItem(L("create.planes2d")))
        {
            ImGui::TextUnformatted(L("create.flat_shapes"));
            ImGui::Separator();

            if (ImGui::Button(L("create.triangle"), btnSize))  { app.CreateTriangleEntity();  ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(L("create.quad"), btnSize))       { app.CreateQuadEntity();      ImGui::CloseCurrentPopup(); }

            if (app.Is2DWorldMode())
            {
                if (ImGui::Button(UI("Circle"), btnSize))       { app.Create2DShapeEntity("Circle2D");   ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button(UI("Pentagon"), btnSize))     { app.Create2DShapeEntity("Pentagon2D"); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button(UI("Hexagon"), btnSize))      { app.Create2DShapeEntity("Hexagon2D");  ImGui::CloseCurrentPopup(); }
                if (ImGui::Button(UI("Octagon"), btnSize))      { app.Create2DShapeEntity("Octagon2D");  ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button(UI("Star"), btnSize))         { app.Create2DShapeEntity("Star2D");     ImGui::CloseCurrentPopup(); }
            }
            else if (ImGui::Button(L("create.plane"), btnSize)) { app.CreatePlaneEntity(); ImGui::CloseCurrentPopup(); }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Pixel Art e Sprites"));
            ImGui::TextDisabled("Crie um Sprite / Quad e use o Inspector 2D para snap, paleta e animação.");
            if (ImGui::Button(UI("Sprite / Quad 2D"), btnSize))
            {
                app.CreateQuadEntity();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 3: UI 2D (modelo ScreenGui / GuiObject)
        // =========================================================
        if (ImGui::BeginTabItem(UI("UI 2D")))
        {
            ImGui::TextUnformatted(UI("Blocos de interface 2D"));
            ImGui::TextDisabled("ScreenGui funciona como contêiner; os demais são GuiObjects.");
            ImGui::Separator();

            if (ImGui::Button(UI("ScreenGui"), btnSize))
            { CreateUIElementEntity(app, UIElementType::ScreenGui); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("Frame"), btnSize))
            { CreateUIElementEntity(app, UIElementType::Frame); ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(UI("TextLabel"), btnSize))
            { CreateUIElementEntity(app, UIElementType::TextLabel); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("TextButton"), btnSize))
            { CreateUIElementEntity(app, UIElementType::TextButton); ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(UI("ImageLabel"), btnSize))
            { CreateUIElementEntity(app, UIElementType::ImageLabel); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("ImageButton"), btnSize))
            { CreateUIElementEntity(app, UIElementType::ImageButton); ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(UI("TextBox"), btnSize))
            { CreateUIElementEntity(app, UIElementType::TextBox); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("ScrollingFrame"), btnSize))
            { CreateUIElementEntity(app, UIElementType::ScrollingFrame); ImGui::CloseCurrentPopup(); }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Controles"));
            if (ImGui::Button(UI("Slider"), btnSize))
            { CreateUIElementEntity(app, UIElementType::Slider); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("Checkbox"), btnSize))
            { CreateUIElementEntity(app, UIElementType::Checkbox); ImGui::CloseCurrentPopup(); }

            if (ImGui::Button(UI("ProgressBar"), btnSize))
            { CreateUIElementEntity(app, UIElementType::ProgressBar); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(UI("Dropdown"), btnSize))
            { CreateUIElementEntity(app, UIElementType::Dropdown); ImGui::CloseCurrentPopup(); }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Propriedades comuns: posição, tamanho, âncora, rotação, ZIndex, cor, transparência e texto/imagem.");
            ImGui::TextDisabled("Os controles (Slider, Checkbox, ProgressBar, Dropdown) têm propriedades próprias no Inspector.");
            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 4: Entidades
        // =========================================================
        if (ImGui::BeginTabItem(L("create.entities")))
        {
            ImGui::TextUnformatted(L("create.entities_desc"));
            ImGui::Separator();

            if (ImGui::Button(L("create.empty_entity"), btnSize))
            {
                app.CreateEmptyEntity();
                ImGui::CloseCurrentPopup();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(UI("Contêineres"));
            ImGui::Separator();
            if (ImGui::Button(UI("Pasta"), btnSize))
            {
                app.CreateFolderEntity();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(UI("Modelo"), btnSize))
            {
                app.CreateModelEntity();
                ImGui::CloseCurrentPopup();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(UI("Animação de grupo"));
            ImGui::TextWrapped(UI("Cria um pai com um único clip e coloca nele os objetos selecionados. Anime o pai para mover o boneco inteiro; anime as partes individualmente no mesmo Timeline quando precisar de articulação."));
            if (ImGui::Button(UI("Criar grupo de animação"), ImVec2(-1.0f, 0.0f)))
            {
                CreateAnimationGroup(app);
                ImGui::CloseCurrentPopup();
            }

            if (!app.Is2DWorldMode() && ImGui::Button(UI("Camera"), btnSize))
            {
                app.CreateCameraEntity();
                ImGui::CloseCurrentPopup();
            }

            if (app.Is2DWorldMode())
            {
                ImGui::TextDisabled("O mundo 2D usa uma câmera ortográfica integrada.");
                ImGui::TextDisabled("Câmeras, luzes e física 3D ficam disponíveis apenas em mundos 3D.");
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(L("create.selection"));
            ImGui::Separator();

            if (ImGui::Button(L("create.duplicate_selected"), btnSize))
            {
                app.DuplicateSelectedEntity();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("create.delete_selected"), btnSize))
            {
                app.DeleteSelectedEntity();
                ImGui::CloseCurrentPopup();
            }

            if (!s_PendingParentForChildrenIDs.empty())
            {
                ImGui::Spacing();
                ImGui::TextUnformatted(UI("Ou escolher existente como pai:"));
                ImGui::Separator();
                if (ImGui::Button(UI("Desanexar pai (Mover para Raiz)"), ImVec2(btnW * 2 + ImGui::GetStyle().ItemSpacing.x, 0.0f)))
                {
                    for (uint64_t childID : s_PendingParentForChildrenIDs)
                        app.GetScene().RemoveParent(childID, true);
                    app.GetScene().UpdateTransforms();
                    app.MarkDirty();
                    s_PendingParentForChildrenIDs.clear();
                    s_EntityIDsBeforeChildCreation.clear();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::BeginCombo("##existing_parent_combo", UI("Escolher existente na cena...")))
                {
                    for (const Entity& e : app.GetScene().GetEntities())
                    {
                        if (e.IsEditorOnly() || e.IsGlobalLight() || e.IsGlobalScript())
                            continue;
                        if (e.GetName() == "Grid" || e.GetName() == "Axis")
                            continue;
                        bool isSelfOrDescendant = false;
                        for (uint64_t childID : s_PendingParentForChildrenIDs)
                        {
                            if (e.GetID() == childID || WouldCreateParentCycle(app.GetScene(), childID, e.GetID()))
                            {
                                isSelfOrDescendant = true;
                                break;
                            }
                        }
                        if (isSelfOrDescendant)
                            continue;

                        if (ImGui::Selectable(e.GetName().c_str()))
                        {
                            for (uint64_t childID : s_PendingParentForChildrenIDs)
                                app.GetScene().SetParent(childID, e.GetID(), true);
                            app.GetScene().UpdateTransforms();
                            app.MarkDirty();
                            s_PendingParentForChildrenIDs.clear();
                            s_EntityIDsBeforeChildCreation.clear();
                            ImGui::CloseCurrentPopup();
                            break;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 4: Importar Modelos 3D
        // =========================================================
        if (!app.Is2DWorldMode() && ImGui::BeginTabItem(L("create.import")))
        {
            ImGui::TextUnformatted(L("create.import_desc"));
            ImGui::Separator();

            if (ImGui::Button(L("create.import_model"), ImVec2(btnW * 2 + ImGui::GetStyle().ItemSpacing.x, 0.0f)))
            {
                const std::string selected = ModelImporter::OpenFileDialog();
                if (!selected.empty())
                {
                    Entity* imported = ModelImporter::ImportToScene(selected, app.GetScene());
                    if (imported)
                    {
                        AssetDatabase::ScanAssets(app.GetProjectConfig().ProjectPath);
                        app.SetSelectedEntity((int)app.GetScene().GetEntities().size() - 1);
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::Spacing();
            ImGui::TextWrapped(UI("%s"), L("create.supported_formats"));

            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 5: Luzes e Áudio
        // =========================================================
        if (!app.Is2DWorldMode() && ImGui::BeginTabItem(L("create.lights_audio")))
        {
            ImGui::TextUnformatted(L("create.lighting_systems"));
            ImGui::Separator();

            if (s_PendingChildParentID != 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", L("create.lighting"));
                if (ImGui::Button(L("component.add_point"), btnSize))
                {
                    CreateLightChild(app, LightType::Point);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("component.add_spot"), btnSize))
                {
                    CreateLightChild(app, LightType::Spot);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::Button(UI("Directional"), btnSize))
                {
                    CreateLightChild(app, LightType::Directional);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(UI("Area Light"), btnSize))
                {
                    CreateLightChild(app, LightType::Area);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", L("create.audio"));
                if (ImGui::Button(L("component.create_audio"), btnSize))
                {
                    CreateAudioChild(app);
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {

            Entity* selected = nullptr;
            const int selectedIndex = app.GetSelectedEntityIndex();
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(app.GetScene().GetEntities().size()))
                selected = &app.GetScene().GetEntities()[static_cast<size_t>(selectedIndex)];

            if (!selected)
                ImGui::TextDisabled("%s", L("create.select_entity"));

            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", L("create.lighting"));
            ImGui::BulletText(UI("Ambient Light — global (configurada em Settings)"));
            ImGui::BulletText(UI("Directional Light — sol/lua"));
            ImGui::BulletText(UI("Point Light — luz pontual"));
            ImGui::BulletText(UI("Spot Light — holofote"));

            if (selected && !selected->HasLight())
            {
                if (ImGui::Button(L("component.add_point"), btnSize))
                {
                    selected->AddLight(LightType::Point);
                    app.MarkDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("component.add_spot"), btnSize))
                {
                    selected->AddLight(LightType::Spot);
                    app.MarkDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button(UI("Directional"), btnSize))
                {
                    selected->AddLight(LightType::Directional);
                    app.MarkDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button(UI("Area Light"), btnSize))
                {
                    selected->AddLight(LightType::Area);
                    app.MarkDirty();
                }
            }
            else if (selected)
            {
                ImGui::TextDisabled("%s", L("create.entity_has_light"));
                if (ImGui::Button(L("component.remove_light"), btnSize))
                {
                    selected->RemoveLight();
                    app.MarkDirty();
                }
            }

            ImGui::Spacing();
            ImGui::TextWrapped(UI("%s"), L("create.lighting_hint"));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", L("create.audio"));
            ImGui::BulletText(UI("Audio Source — 2D / 3D"));
            ImGui::BulletText(UI("Audio Listener"));
            ImGui::BulletText(UI("Formatos: .wav, .ogg, .mp3"));

            if (selected && !selected->HasAudio())
            {
                if (ImGui::Button(L("component.create_audio"), btnSize))
                {
                    selected->AddAudio();
                    app.MarkDirty();
                }
            }
            else if (selected)
            {
                ImGui::TextDisabled("%s", L("create.entity_has_audio"));
                if (ImGui::Button(L("component.test_audio"), btnSize))
                    app.PreviewAudioForSelectedEntity();
                ImGui::SameLine();
                if (ImGui::Button(L("component.stop_audio"), btnSize))
                    app.StopAudioPreview();
                if (ImGui::Button(L("component.remove_audio"), btnSize))
                {
                    app.StopAudioPreview();
                    selected->RemoveAudio();
                    app.MarkDirty();
                }
            }

            ImGui::Spacing();
            ImGui::TextWrapped(UI("%s"), L("create.audio_hint"));
            }

            ImGui::EndTabItem();
        }

        // =========================================================
        // Aba 6: Scripts & Animação
        // =========================================================
        if (ImGui::BeginTabItem(L("create.scripts_animation")))
        {
            ImGui::TextUnformatted(L("create.behavior_animation"));
            ImGui::Separator();

            if (s_PendingChildParentID != 0)
            {
                ImGui::TextWrapped(UI("O Script C# aparecerá como filho deste objeto no Hierarchy."));
                Entity* scriptParent = app.GetScene().FindEntityByID(s_PendingChildParentID);
                const bool alreadyHasScript = scriptParent &&
                    !scriptParent->GetScriptComponent().ScriptPath.empty();
                if (alreadyHasScript)
                {
                    ImGui::TextDisabled(UI("Este objeto já tem um Script C#. Remova ou abra o existente pelo Inspector."));
                }
                else if (ImGui::Button(UI("Adicionar Script C# ao objeto"), btnSize))
                {
                    Scene& scene = app.GetScene();
                    Entity* parent = scene.FindEntityByID(s_PendingChildParentID);
                    if (parent)
                    {
                        const uint64_t parentID = parent->GetID();
                        const auto& entities = scene.GetEntities();
                        for (size_t index = 0; index < entities.size(); ++index)
                        {
                            if (entities[index].GetID() == parentID)
                            {
                                app.SetSelectedEntity(static_cast<int>(index));
                                app.CreateScriptForSelectedEntity();
                                break;
                            }
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                if (ImGui::Button(L("create.create_script_selected"), btnSize))
                {
                    app.CreateScriptForSelectedEntity();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Spacing();
                ImGui::TextUnformatted(L("create.shortcuts"));
                ImGui::Separator();

                if (ImGui::Button(L("create.open_script_vscode"), btnSize))
                {
                    app.OpenSelectedScriptInVSCode();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("create.open_scripts_folder"), btnSize))
                {
                    app.OpenScriptsFolderInVSCode();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Spacing();
                ImGui::TextDisabled("%s", L("create.animation_hint"));
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button(L("create.close"), ImVec2(120, 0)))
    {
        s_PendingParentForChildrenIDs.clear();
        s_PendingChildParentID = 0;
        s_EntityIDsBeforeChildCreation.clear();
        ImGui::CloseCurrentPopup();
    }

    FinishAddHierarchyChild(app);
    ImGui::EndPopup();
}

void HierarchyPanel::Draw(
    Application& app)
{
    s_HierarchySelectionRows.clear();
    std::string title = std::string(L("panel.hierarchy")) + "###Hierarchy";
    ImGui::Begin(
        title.c_str(),
        nullptr,
        ImGuiWindowFlags_NoCollapse
    );

    // ---- Ações rápidas da árvore ----
    if (ImGui::Button(UI("+ Adicionar")))
    {
        s_PendingChildParentID = 0;
        s_EntityIDsBeforeChildCreation.clear();
        ImGui::OpenPopup("##hierarchy_create_popup");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Expandir")))
        s_HierarchyForceTreeState = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Recolher")))
        s_HierarchyForceTreeState = 0;
    ImGui::TextDisabled(UI("Arraste para reorganizar; Ctrl+clique ou Alt+arraste seleciona vários."));

    ImGui::Separator();

    // ---- Search / Filter ----
    static char s_SearchBuf[128] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", L("common.search"), s_SearchBuf, sizeof(s_SearchBuf));
    const bool filtering = s_SearchBuf[0] != '\0';
    const std::string filterLower = [&]() {
        std::string s = s_SearchBuf;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return s;
    }();

    ImGui::Separator();

    // ---- Lista de entidades ----
    auto& entities = app.GetScene().GetEntities();

    // Contagens distinguem objetos da cena dos componentes anexados.
    const int selectedIndex = app.GetSelectedEntityIndex();
    int visibleEntities = 0;
    int attachedComponents = 0;
    for (Entity& entity : entities)
    {
        if (!IsHierarchyEntityVisible(app, entity)) continue;
        ++visibleEntities;
        attachedComponents += !entity.GetScriptComponent().ScriptPath.empty() ? 1 : 0;
        attachedComponents += entity.HasAudio() ? 1 : 0;
        attachedComponents += !app.Is2DWorldMode() && entity.HasParticleSystem() ? 1 : 0;
        attachedComponents += entity.HasUI() ? 1 : 0;
        attachedComponents += !app.Is2DWorldMode() && entity.HasLight() ? 1 : 0;
        attachedComponents += !app.Is2DWorldMode() && entity.HasCamera() ? 1 : 0;
        attachedComponents += (app.Is2DWorldMode() ? entity.ColliderEnabled
                                                   : entity.PhysicsEnabled) ? 1 : 0;
        attachedComponents += entity.GetAnimationComponent().Clip &&
            !entity.GetAnimationComponent().Clip->Name.empty() ? 1 : 0;
        attachedComponents += entity.IsGlobalLight() ? 1 : 0;
        attachedComponents += entity.IsGlobalScript() ? 1 : 0;
    }
    ImGui::TextDisabled("%d objeto(s)  ·  %d componente(s)%s", visibleEntities,
        attachedComponents, filtering ? "  ·  filtro ativo" : "");
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(entities.size()))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("| Selecionada: %s",
            entities[static_cast<size_t>(selectedIndex)].GetName().c_str());
    }
    const float selectionRowsTop = ImGui::GetCursorScreenPos().y;

    if (filtering)
    {
        // Resultados planos preservam o ID real e mostram o pai para manter
        // o contexto da árvore durante a busca.
        for (size_t i = 0; i < entities.size(); ++i)
        {
            Entity& entity = entities[i];
            if (!IsHierarchyEntityVisible(app, entity)) continue;

            std::string nameLower = entity.GetName();
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });

            std::string tagLower = entity.Tag;
            std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (nameLower.find(filterLower) == std::string::npos &&
                tagLower.find(filterLower) == std::string::npos) continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            const bool selected = app.GetSelectedID() == entity.GetID() || app.IsMultiSelectedEntity(static_cast<int>(i));
            if (selected)
                flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::PushStyleColor(ImGuiCol_Header,
                selected ? ImVec4(0.08f, 0.38f, 0.68f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.31f, 0.43f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.10f, 0.46f, 0.78f, 1.0f));
            const bool editingName = s_InlineRenameEntityID == entity.GetID();
            ImGui::TreeNodeEx((void*)(uintptr_t)entity.GetID(), flags,
                editingName
                    ? (entity.SelectionLocked ? "   [Bloqueado]" : "   ")
                    : (entity.SelectionLocked ? "   %s  [Bloqueado]" : "   %s"),
                entity.GetName().c_str());
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            if (IsHierarchyNameDoubleClicked(entity, rowMin, rowMax))
                BeginHierarchyInlineRename(entity);
            s_HierarchySelectionRows.push_back({
                static_cast<int>(i),
                ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, rowMin.y),
                ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x -
                    ImGui::GetStyle().WindowPadding.x, rowMax.y) });
            const float iconSize = std::max(11.0f, std::min(14.0f, rowMax.y - rowMin.y - 3.0f));
            MaraeUI::DrawEditorIcon(ImGui::GetWindowDrawList(), EntityIcon(entity),
                ImVec2(rowMin.x + 18.0f, rowMin.y + (rowMax.y - rowMin.y - iconSize) * 0.5f), iconSize);
            ImGui::PopStyleColor(3);

            const float lockBtnSize = 16.0f;
            const float lockBtnX = (ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) - lockBtnSize - 4.0f;
            const float lockBtnY = rowMin.y + (rowMax.y - rowMin.y - lockBtnSize) * 0.5f;
            const ImVec2 lockMin(lockBtnX, lockBtnY);
            const ImVec2 lockMax(lockBtnX + lockBtnSize, lockBtnY + lockBtnSize);

            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            const bool lockHovered = mousePos.x >= lockMin.x && mousePos.x <= lockMax.x &&
                                     mousePos.y >= lockMin.y && mousePos.y <= lockMax.y &&
                                     ImGui::IsWindowHovered();

            if (entity.SelectionLocked || lockHovered)
            {
                const ImU32 lockColor = entity.SelectionLocked
                    ? (lockHovered ? IM_COL32(255, 195, 60, 255) : IM_COL32(240, 150, 40, 255))
                    : (lockHovered ? IM_COL32(200, 215, 235, 220) : IM_COL32(130, 145, 165, 140));

                if (lockHovered)
                {
                    ImGui::GetWindowDrawList()->AddRectFilled(lockMin, lockMax, IM_COL32(255, 255, 255, 35), 3.0f);
                    ImGui::SetTooltip(entity.SelectionLocked
                        ? UI("Seleção bloqueada no viewport. Clique para desbloquear.")
                        : UI("Bloquear seleção no viewport"));
                }

                MaraeUI::DrawEditorIcon(ImGui::GetWindowDrawList(),
                    entity.SelectionLocked ? MaraeUI::EditorIcon::Lock : MaraeUI::EditorIcon::Unlock,
                    ImVec2(lockBtnX + 2.0f, lockBtnY + 2.0f), lockBtnSize - 4.0f, lockColor);

                if (lockHovered && ImGui::IsMouseClicked(0))
                {
                    entity.SelectionLocked = !entity.SelectionLocked;
                    app.MarkDirty();
                }
            }

            if (ImGui::IsItemClicked(0) && !lockHovered)
                SelectHierarchyEntity(app, static_cast<int>(i), ImGui::GetIO().KeyCtrl);
            DrawHierarchyEntityContextMenu(app, entity, static_cast<int>(i));
            DrawAddComponentPopup(app, entity);

            if (entity.GetParentID() != 0)
            {
                const Entity* parent = app.GetScene().FindEntityByID(entity.GetParentID());
                if (parent)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(UI("↳ %s"), parent->GetName().c_str());
                }
            }

            DrawHierarchyInlineRename(app, entity, rowMin, rowMax);
        }
    }
    else
    {
        // Árvore hierárquica com recuperação de órfãos/ciclos, para que uma
        // referência de pai inválida nunca faça um objeto sumir do painel.
        s_HierarchyVisitedEntities.clear();
        for (Entity& entity : entities)
        {
            if (!IsHierarchyEntityVisible(app, entity)) continue;
            if (entity.GetParentID() != 0) continue;
            DrawEntityNode(app, entity);
        }

        bool hasDisconnectedEntities = false;
        for (const Entity& entity : entities)
            if (IsHierarchyEntityVisible(app, entity) &&
                !HasValidParentChain(app.GetScene(), entity))
            {
                hasDisconnectedEntities = true;
                break;
            }
        if (hasDisconnectedEntities)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.32f, 1.0f),
                UI("Itens desconectados (pai ausente ou ciclo detectado)"));
            for (Entity& entity : entities)
            {
                if (!IsHierarchyEntityVisible(app, entity) ||
                    HasValidParentChain(app.GetScene(), entity))
                    continue;
                DrawEntityNode(app, entity);
            }
        }
    }
    s_HierarchyForceTreeState = -1;

    // ---- Ações do Marquee (Seleção translúcida por caixa) ----
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float canvasLeft = windowPos.x;
    const float canvasRight = windowPos.x + windowSize.x;
    const float canvasTop = windowPos.y;
    const float canvasBottom = windowPos.y + windowSize.y;

    const ImVec2 clickStart = io.MouseClickedPos[0];
    const bool clickStartedInHierarchy = clickStart.x >= canvasLeft && clickStart.x <= canvasRight &&
                                         clickStart.y >= canvasTop && clickStart.y <= canvasBottom;

    // Iniciar marquee arrastando com botão esquerdo em qualquer lugar da hierarquia (se não for arrasto de item selecionado)
    // Não inicia se: já há marquee, há payload de dragdrop ativo, ou se o usuário clicou num item selecionado (multi-drag)
    if (!s_HierarchyMarqueeActive && ImGui::GetDragDropPayload() == nullptr &&
        s_MultiDragSourceIndex < 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) &&
        clickStartedInHierarchy)
    {
        s_HierarchyMarqueeActive = true;
        s_HierarchyMarqueeStart = clickStart;
        s_HierarchyMarqueeAdditive = io.KeyCtrl || io.KeyShift;
        s_HierarchySelectionBeforeMarquee = app.GetMultiSelectedEntities();
        if (s_HierarchySelectionBeforeMarquee.empty() && app.GetSelectedEntityIndex() >= 0)
            s_HierarchySelectionBeforeMarquee.push_back(app.GetSelectedEntityIndex());
    }

    for (const auto& [action, entityID] : s_PendingHierarchyContextActions)
    {
        int entityIndex = -1;
        const auto& currentEntities = app.GetScene().GetEntities();
        for (size_t index = 0; index < currentEntities.size(); ++index)
        {
            if (currentEntities[index].GetID() == entityID)
            {
                entityIndex = static_cast<int>(index);
                break;
            }
        }
        if (entityIndex < 0)
            continue;
        if (app.GetSelectedEntityIndex() != entityIndex && !app.IsMultiSelectedEntity(entityIndex))
            app.SetSelectedEntity(entityIndex);
        if (action == HierarchyContextAction::Duplicate)
            app.DuplicateSelectedEntity();
        else
            app.DeleteSelectedEntity();
    }
    s_PendingHierarchyContextActions.clear();

    // Importar pode realocar o vetor de entidades. Só executamos os drops
    // sobre linhas depois de terminar a recursão da árvore, evitando invalidar
    // a referência da entidade que está sendo desenhada.
    for (const auto& [assetPath, parentID] : s_PendingHierarchyAssetDrops)
    {
        std::string extension = std::filesystem::path(assetPath).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".cs")
        {
            Entity* parent = app.GetScene().FindEntityByID(parentID);
            if (parent && parent->GetScriptComponent().ScriptPath.empty())
            {
                ScriptComponent& script = parent->GetScriptComponent();
                script.ScriptPath = std::filesystem::path(assetPath).lexically_normal().generic_string();
                script.ScriptAssemblyQualifiedTypeName.clear();
                script.Enabled = true;
                script.Dirty = true;
                app.MarkDirty();
            }
            continue;
        }

        Entity* imported = CreateEntityFromAsset(app, assetPath);
        if (!imported) continue;
        const uint64_t importedID = imported->GetID();
        if (app.GetScene().FindEntityByID(parentID))
            app.GetScene().SetParent(importedID, parentID);
        const auto& currentEntities = app.GetScene().GetEntities();
        for (size_t index = 0; index < currentEntities.size(); ++index)
        {
            if (currentEntities[index].GetID() == importedID)
            {
                app.SetSelectedEntity(static_cast<int>(index));
                break;
            }
        }
        app.MarkDirty();
    }
    s_PendingHierarchyAssetDrops.clear();

    // Área vazia restante do painel: clique limpa seleção, arrasto inicia marquee, drop desanexa/importa
    const float dropHeight = std::max(40.0f, ImGui::GetContentRegionAvail().y);
    ImGui::InvisibleButton("##HierarchyAssetDropArea", ImVec2(-1.0f, dropHeight));
    const bool dropAreaActive = ImGui::IsItemActive();

    // Clique simples na área vazia desseleciona tudo
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt)
    {
        app.SetSelectedEntity(-1);
        app.ClearMultiSelection();
    }

    // Clique com botão direito na área vazia abre popup de criação
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        s_PendingChildParentID = 0;
        s_PendingParentForChildrenIDs.clear();
        s_EntityIDsBeforeChildCreation.clear();
        ImGui::OpenPopup("##hierarchy_create_popup");
    }

    // Iniciar marquee a partir da área vazia
    if (!s_HierarchyMarqueeActive && dropAreaActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
    {
        s_HierarchyMarqueeActive = true;
        s_HierarchyMarqueeStart = io.MouseClickedPos[0];
        s_HierarchyMarqueeAdditive = io.KeyCtrl || io.KeyShift;
        s_HierarchySelectionBeforeMarquee = app.GetMultiSelectedEntities();
        if (s_HierarchySelectionBeforeMarquee.empty() && app.GetSelectedEntityIndex() >= 0)
            s_HierarchySelectionBeforeMarquee.push_back(app.GetSelectedEntityIndex());
    }

    if (ImGui::BeginDragDropTarget())
    {
        // Soltar entidades no fundo da hierarquia as desanexa de qualquer pai (move para raiz)
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            const ImVec2 dropMin = ImGui::GetItemRectMin();
            const ImVec2 dropMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(dropMin, dropMax, IM_COL32(100, 220, 140, 220), 2.0f, 0, 2.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(dropMin, dropMax, IM_COL32(50, 200, 100, 35), 2.0f);

            if (payload->Delivery)
            {
                std::vector<uint64_t> idsToUnparent = s_DraggedEntityIDs;
                if (idsToUnparent.empty() && payload->DataSize == sizeof(uint64_t))
                {
                    uint64_t singleID = 0;
                    std::memcpy(&singleID, payload->Data, sizeof(singleID));
                    idsToUnparent.push_back(singleID);
                }

                Scene& scene = app.GetScene();
                for (uint64_t draggedID : idsToUnparent)
                {
                    scene.RemoveParent(draggedID, true);
                }
                scene.UpdateTransforms();
                app.MarkDirty();
                s_DraggedEntityIDs.clear();
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && path[0] != '\0')
            {
                Entity* imported = CreateEntityFromAsset(app, path);
                if (imported)
                {
                    const uint64_t importedID = imported->GetID();
                    imported->GetTransform().Position = AssetSpawnPosition(app);
                    const auto& currentEntities = app.GetScene().GetEntities();
                    for (size_t index = 0; index < currentEntities.size(); ++index)
                    {
                        if (currentEntities[index].GetID() == importedID)
                        {
                            app.SetSelectedEntity(static_cast<int>(index));
                            break;
                        }
                    }
                    app.MarkDirty();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Desenhar e atualizar Marquee translúcido em tempo real
    if (s_HierarchyMarqueeActive)
    {
        const ImVec2 dragEnd = io.MousePos;
        const ImVec2 rectMin(std::min(s_HierarchyMarqueeStart.x, dragEnd.x),
                             std::min(s_HierarchyMarqueeStart.y, dragEnd.y));
        const ImVec2 rectMax(std::max(s_HierarchyMarqueeStart.x, dragEnd.x),
                             std::max(s_HierarchyMarqueeStart.y, dragEnd.y));

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        // Fundo translúcido moderno azul celeste
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(40, 145, 245, 45));
        // Contorno nítido azul brilhante
        drawList->AddRect(rectMin, rectMax, IM_COL32(80, 185, 255, 230), 0.0f, 0, 1.5f);

        std::vector<int> liveSelection = s_HierarchyMarqueeAdditive
            ? s_HierarchySelectionBeforeMarquee : std::vector<int>{};

        for (const HierarchySelectionRow& row : s_HierarchySelectionRows)
        {
            const bool intersects = row.Max.x >= rectMin.x && row.Min.x <= rectMax.x &&
                                    row.Max.y >= rectMin.y && row.Min.y <= rectMax.y;
            if (intersects)
            {
                if (std::find(liveSelection.begin(), liveSelection.end(), row.EntityIndex) == liveSelection.end())
                    liveSelection.push_back(row.EntityIndex);
            }
        }

        if (!liveSelection.empty())
        {
            app.SetSelectedEntity(liveSelection.front());
            app.SetMultiSelectedEntities(liveSelection);
        }
        else if (!s_HierarchyMarqueeAdditive)
        {
            app.SetSelectedEntity(-1);
            app.ClearMultiSelection();
        }

        if (!io.MouseDown[ImGuiMouseButton_Left])
        {
            s_HierarchySelectionBeforeMarquee.clear();
            s_HierarchyMarqueeActive = false;
        }
    }

    // ---- Clique direito em área vazia do painel ----
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseClicked(1) &&
        !ImGui::IsAnyItemHovered())
    {
        s_PendingChildParentID = 0;
        s_EntityIDsBeforeChildCreation.clear();
        ImGui::OpenPopup("##hierarchy_create_popup");
    }

    if (s_OpenChildCreatorPending)
    {
        ImGui::OpenPopup("##hierarchy_create_popup");
        s_OpenChildCreatorPending = false;
    }
    DrawCreateEntityPopup(app);

    if (!io.MouseDown[ImGuiMouseButton_Left])
    {
        s_PotentialSingleSelectIndex = -1;
        s_MultiDragSourceIndex = -1;
        s_MultiDragStarted = false;
    }

    app.SetHierarchyWidth(ImGui::GetWindowSize().x);
    ImGui::End();
}

void HierarchyPanel::BeginAddParentForEntities(Application& app, const std::vector<uint64_t>& childrenIDs)
{
    BeginAddHierarchyParent(app, childrenIDs);
}
