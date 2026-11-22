#include "../../Engine/Localization/Localization.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "InspectorPanel.h"
#include "../../Scene/Components/ColorComponent.h"
#include "../../Core/Application.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Transform/Transform.h"
#include <cstring>
#include <imgui.h>
#include "../../Scene/Prefab/Prefab.h"
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include "../../Engine/Assets/GUID/GUID.h"
#include "../../Renderer/Texture/TextureLoader.h"
#include "../Renderer/Texture/TextureCache.h"
#include "../../Assets/AssetDatabase/AssetDatabase.h"
#include "../../Engine/Audio/Clip/AudioAssetLibrary.h"
#include "../../Engine/Audio/Mixer/AudioMixer.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include "../Modeling/ModelingOperations.h"
#include "../Modeling/MeshTopology.h"
#include "../Modeling/ModelingSelection.h"
#include "../Modeling/ModelingToolset.h"
#include "../Modeling/MeshPainter.h"
#include "../UI/EditorIcons.h"
#include "../../Engine/Assets/ModelImporter/ModelImporter.h"
#include "../../Scene/Components/AnimationComponent.h"
#include "../../Engine/Systems/AnimationSystem.h"
#include "../../Scene/Components/LightComponent.h"
#include "../../Scene/Components/LocalLightComponent.h"
#include "../../Scene/Components/UIComponent.h"
#include "../../Engine/UI/UIFontLibrary.h"
#include "../../Engine/UI/UILayout.h"
#include "../../Engine/Lighting/LightingManager.h"
#include "../../Engine/Localization/Localization.h"
#include "../../Engine/Scripting/ScriptingRuntime.h"
#include "../../ThirdParty/json/json.hpp"
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdint>

Material* InspectorPanel::m_CurrentMaterial = nullptr;

namespace
{
    struct InspectorBatchContext
    {
        Entity* Active = nullptr;
        std::vector<Entity*> Targets;
    };

    InspectorBatchContext* g_InspectorBatchContext = nullptr;

    template<typename T>
    bool CopyEditedRange(T* sourceObject, T* targetObject,
                         const void* editedAddress, std::size_t editedBytes)
    {
        if (!sourceObject || !targetObject || !editedAddress || editedBytes == 0)
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(sourceObject);
        const auto address = reinterpret_cast<std::uintptr_t>(editedAddress);
        if (address < base || address - base > sizeof(T) ||
            editedBytes > sizeof(T) - (address - base))
            return false;

        const std::size_t offset = static_cast<std::size_t>(address - base);
        for (Entity* target : g_InspectorBatchContext->Targets)
        {
            T* targetObject = nullptr;
            if constexpr (std::is_same_v<T, Transform>)
                targetObject = &target->GetTransform();
            else
                targetObject = target->GetComponent<T>();
            if (!targetObject)
                continue;
            std::memcpy(reinterpret_cast<unsigned char*>(targetObject) + offset,
                        editedAddress, editedBytes);
        }
        return true;
    }

    template<typename T>
    bool CopyEditedComponentRange(const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active)
            return false;
        Entity& active = *g_InspectorBatchContext->Active;
        T* component = nullptr;
        if constexpr (std::is_same_v<T, Transform>)
            component = &active.GetTransform();
        else
            component = active.GetComponent<T>();
        return CopyEditedRange(component, component, editedAddress, editedBytes);
    }

    template<typename T>
    bool IsComponentRangeMixed(const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active ||
            !editedAddress || editedBytes == 0)
            return false;
        Entity& active = *g_InspectorBatchContext->Active;
        T* source = nullptr;
        if constexpr (std::is_same_v<T, Transform>)
            source = &active.GetTransform();
        else
            source = active.GetComponent<T>();
        if (!source)
            return false;

        const auto base = reinterpret_cast<std::uintptr_t>(source);
        const auto address = reinterpret_cast<std::uintptr_t>(editedAddress);
        if (address < base || address - base > sizeof(T) ||
            editedBytes > sizeof(T) - (address - base))
            return false;
        const std::size_t offset = static_cast<std::size_t>(address - base);
        for (Entity* target : g_InspectorBatchContext->Targets)
        {
            T* targetComponent = nullptr;
            if constexpr (std::is_same_v<T, Transform>)
                targetComponent = &target->GetTransform();
            else
                targetComponent = target->GetComponent<T>();
            if (targetComponent && std::memcmp(
                    reinterpret_cast<const unsigned char*>(source) + offset,
                    reinterpret_cast<const unsigned char*>(targetComponent) + offset,
                    editedBytes) != 0)
                return true;
        }
        return false;
    }

    template<typename T>
    bool IsEntityRangeMixed(T* field, const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active || !field ||
            !editedAddress || editedBytes == 0)
            return false;
        const auto fieldAddress = reinterpret_cast<std::uintptr_t>(field);
        const auto address = reinterpret_cast<std::uintptr_t>(editedAddress);
        if (address < fieldAddress || address - fieldAddress > sizeof(T) ||
            editedBytes > sizeof(T) - (address - fieldAddress))
            return false;
        const std::size_t innerOffset = static_cast<std::size_t>(address - fieldAddress);
        Entity* active = g_InspectorBatchContext->Active;
        const auto entityBase = reinterpret_cast<std::uintptr_t>(active);
        if (fieldAddress < entityBase || fieldAddress - entityBase > sizeof(Entity) ||
            sizeof(T) > sizeof(Entity) - (fieldAddress - entityBase))
            return false;
        const std::size_t fieldOffset = static_cast<std::size_t>(fieldAddress - entityBase);
        for (Entity* target : g_InspectorBatchContext->Targets)
        {
            const auto* targetAddress = reinterpret_cast<const unsigned char*>(target) +
                fieldOffset + innerOffset;
            if (std::memcmp(editedAddress, targetAddress, editedBytes) != 0)
                return true;
        }
        return false;
    }

    bool InspectorWidgetHasMixedValue(const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active ||
            !editedAddress || editedBytes == 0)
            return false;
        if (IsComponentRangeMixed<ColorComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<Transform>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<MaterialComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<ScriptComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<AnimationComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<LightComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<LocalLightComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<AudioComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<GlobalLightComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<PostProcessingVolumeComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<CameraComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<TerrainComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<UIComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<ParticleSystemComponent>(editedAddress, editedBytes) ||
            IsComponentRangeMixed<AIComponent>(editedAddress, editedBytes))
            return true;

        Entity& active = *g_InspectorBatchContext->Active;
#define MARAE_CHECK_MIXED_ENTITY_FIELD(Field) \
        if (IsEntityRangeMixed(&active.Field, editedAddress, editedBytes)) return true
        MARAE_CHECK_MIXED_ENTITY_FIELD(PickingRadius);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Active);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Opacity);
        MARAE_CHECK_MIXED_ENTITY_FIELD(CastShadows);
        MARAE_CHECK_MIXED_ENTITY_FIELD(RaycastTarget);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Layer);
        MARAE_CHECK_MIXED_ENTITY_FIELD(PhysicsEnabled);
        MARAE_CHECK_MIXED_ENTITY_FIELD(UseGravity);
        MARAE_CHECK_MIXED_ENTITY_FIELD(IsStatic);
        MARAE_CHECK_MIXED_ENTITY_FIELD(ColliderEnabled);
        MARAE_CHECK_MIXED_ENTITY_FIELD(IsTrigger);
        MARAE_CHECK_MIXED_ENTITY_FIELD(SelectionLocked);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Collider);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Mass);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Restitution);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Friction);
        MARAE_CHECK_MIXED_ENTITY_FIELD(Velocity);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AngularVelocity);
        MARAE_CHECK_MIXED_ENTITY_FIELD(ContinuousCollision);
        MARAE_CHECK_MIXED_ENTITY_FIELD(FreezeRotation);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioAcousticGeometryEnabled);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioOccluder);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioTransmissionEnabled);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioTransmission);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioAbsorption);
        MARAE_CHECK_MIXED_ENTITY_FIELD(AudioDiffraction);
#undef MARAE_CHECK_MIXED_ENTITY_FIELD
        return false;
    }

    class InspectorMixedValueScope
    {
    public:
        explicit InspectorMixedValueScope(bool mixed) : m_Mixed(mixed)
        {
            if (m_Mixed)
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        }
        ~InspectorMixedValueScope()
        {
            if (m_Mixed)
                ImGui::PopItemFlag();
        }
        InspectorMixedValueScope(const InspectorMixedValueScope&) = delete;
        InspectorMixedValueScope& operator=(const InspectorMixedValueScope&) = delete;
    private:
        bool m_Mixed;
    };

    void DrawNeutralMixedCheckboxMark(bool mixed, bool checked)
    {
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const float side = ImGui::GetFrameHeight();
        const float inset = std::max(2.0f, side * 0.12f);
        const float radius = ImGui::GetStyle().FrameRounding;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 boxMax(itemMin.x + side, itemMin.y + side);
        drawList->AddRectFilled(itemMin, boxMax, IM_COL32(255, 255, 255, 255), radius);
        drawList->AddRect(itemMin, boxMax, IM_COL32(48, 48, 48, 255), radius, 0, 1.0f);
        if (mixed)
        {
            const float dashHeight = std::max(2.0f, side * 0.14f);
            const float centerY = itemMin.y + side * 0.5f;
            drawList->AddRectFilled(
                ImVec2(itemMin.x + inset, centerY - dashHeight * 0.5f),
                ImVec2(boxMax.x - inset, centerY + dashHeight * 0.5f),
                IM_COL32(20, 20, 20, 255), radius * 0.25f);
        }
        else if (checked)
        {
            const float thickness = std::max(1.5f, side * 0.10f);
            const ImVec2 p1(itemMin.x + side * 0.22f, itemMin.y + side * 0.52f);
            const ImVec2 p2(itemMin.x + side * 0.43f, itemMin.y + side * 0.72f);
            const ImVec2 p3(itemMin.x + side * 0.80f, itemMin.y + side * 0.29f);
            drawList->AddLine(p1, p2, IM_COL32(20, 20, 20, 255), thickness);
            drawList->AddLine(p2, p3, IM_COL32(20, 20, 20, 255), thickness);
        }
    }

    template<typename T>
    bool CopyEditedEntityField(T* field, const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active || !field)
            return false;
        Entity* active = g_InspectorBatchContext->Active;
        const auto begin = reinterpret_cast<std::uintptr_t>(field);
        const auto address = reinterpret_cast<std::uintptr_t>(editedAddress);
        if (address < begin || address - begin > sizeof(T) ||
            editedBytes > sizeof(T) - (address - begin))
            return false;
        const std::size_t offset = static_cast<std::size_t>(address - begin);
        const auto entityBase = reinterpret_cast<std::uintptr_t>(active);
        if (begin < entityBase || begin - entityBase > sizeof(Entity) ||
            sizeof(T) > sizeof(Entity) - (begin - entityBase))
            return false;
        const std::size_t entityOffset = static_cast<std::size_t>(begin - entityBase);
        for (Entity* target : g_InspectorBatchContext->Targets)
            std::memcpy(reinterpret_cast<unsigned char*>(target) + entityOffset + offset,
                        editedAddress, editedBytes);
        return true;
    }

    void SyncInspectorWidgetEdit(const void* editedAddress, std::size_t editedBytes)
    {
        if (!g_InspectorBatchContext || !g_InspectorBatchContext->Active ||
            !editedAddress || editedBytes == 0)
            return;
        Entity& active = *g_InspectorBatchContext->Active;

        // Component-backed Inspector controls. Offsets are copied only inside
        // the matching component type, and entities without that component
        // are skipped, so optional component state is never fabricated.
        if (CopyEditedComponentRange<ColorComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<Transform>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<MaterialComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<ScriptComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<AnimationComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<LightComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<LocalLightComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<AudioComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<GlobalLightComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<PostProcessingVolumeComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<CameraComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<TerrainComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<UIComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<ParticleSystemComponent>(editedAddress, editedBytes) ||
            CopyEditedComponentRange<AIComponent>(editedAddress, editedBytes))
            return;

        // Physics and shared object properties live directly on Entity.
#define MARAE_SYNC_ENTITY_FIELD(Field) \
        if (CopyEditedEntityField(&active.Field, editedAddress, editedBytes)) return
        MARAE_SYNC_ENTITY_FIELD(PickingRadius);
        MARAE_SYNC_ENTITY_FIELD(Active);
        MARAE_SYNC_ENTITY_FIELD(Opacity);
        MARAE_SYNC_ENTITY_FIELD(CastShadows);
        MARAE_SYNC_ENTITY_FIELD(RaycastTarget);
        MARAE_SYNC_ENTITY_FIELD(Layer);
        MARAE_SYNC_ENTITY_FIELD(PhysicsEnabled);
        MARAE_SYNC_ENTITY_FIELD(UseGravity);
        MARAE_SYNC_ENTITY_FIELD(IsStatic);
        MARAE_SYNC_ENTITY_FIELD(ColliderEnabled);
        MARAE_SYNC_ENTITY_FIELD(IsTrigger);
        MARAE_SYNC_ENTITY_FIELD(SelectionLocked);
        MARAE_SYNC_ENTITY_FIELD(Collider);
        MARAE_SYNC_ENTITY_FIELD(Mass);
        MARAE_SYNC_ENTITY_FIELD(Restitution);
        MARAE_SYNC_ENTITY_FIELD(Friction);
        MARAE_SYNC_ENTITY_FIELD(Velocity);
        MARAE_SYNC_ENTITY_FIELD(AngularVelocity);
        MARAE_SYNC_ENTITY_FIELD(ContinuousCollision);
        MARAE_SYNC_ENTITY_FIELD(FreezeRotation);
        MARAE_SYNC_ENTITY_FIELD(AudioAcousticGeometryEnabled);
        MARAE_SYNC_ENTITY_FIELD(AudioOccluder);
        MARAE_SYNC_ENTITY_FIELD(AudioTransmissionEnabled);
        MARAE_SYNC_ENTITY_FIELD(AudioTransmission);
        MARAE_SYNC_ENTITY_FIELD(AudioAbsorption);
        MARAE_SYNC_ENTITY_FIELD(AudioDiffraction);
#undef MARAE_SYNC_ENTITY_FIELD
    }

    class InspectorBatchScope
    {
    public:
        InspectorBatchScope(Application& app, Entity& active)
        {
            const auto& selection = app.GetMultiSelectedEntities();
            if (selection.size() < 2)
                return;
            m_Context.Active = &active;
            auto& entities = app.GetScene().GetEntities();
            std::unordered_set<int> seen;
            for (int index : selection)
            {
                if (index < 0 || index >= static_cast<int>(entities.size()) ||
                    !seen.insert(index).second)
                    continue;
                Entity& target = entities[static_cast<std::size_t>(index)];
                if (&target != &active && !target.IsEditorOnly())
                    m_Context.Targets.push_back(&target);
            }
            if (!m_Context.Targets.empty())
            {
                m_Previous = g_InspectorBatchContext;
                g_InspectorBatchContext = &m_Context;
            }
        }

        ~InspectorBatchScope()
        {
            if (g_InspectorBatchContext == &m_Context)
                g_InspectorBatchContext = m_Previous;
        }

    private:
        InspectorBatchContext m_Context;
        InspectorBatchContext* m_Previous = nullptr;
    };

    class UniformScaleButtonStyle
    {
    public:
        explicit UniformScaleButtonStyle(bool locked)
            : m_Pushed(locked)
        {
            if (m_Pushed)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
            }
        }

        ~UniformScaleButtonStyle()
        {
            if (m_Pushed)
                ImGui::PopStyleColor(2);
        }

        UniformScaleButtonStyle(const UniformScaleButtonStyle&) = delete;
        UniformScaleButtonStyle& operator=(const UniformScaleButtonStyle&) = delete;

    private:
        bool m_Pushed;
    };

    template<typename T, typename Callback>
    void ForEachInspectorBatchComponent(Callback&& callback)
    {
        if (!g_InspectorBatchContext)
            return;
        for (Entity* target : g_InspectorBatchContext->Targets)
            if (T* component = target->GetComponent<T>())
                callback(*component);
    }

    template<typename Callback>
    void ForEachInspectorBatchEntity(Callback&& callback)
    {
        if (!g_InspectorBatchContext)
            return;
        for (Entity* target : g_InspectorBatchContext->Targets)
            callback(*target);
    }
}

namespace ImGui
{
    inline bool BatchCheckbox(const char* label, bool* value)
    {
        const bool mixed = InspectorWidgetHasMixedValue(value, sizeof(*value));
        bool changed = false;
        {
            InspectorMixedValueScope mixedScope(mixed);
            changed = Checkbox(label, value);
        }
        if (changed) SyncInspectorWidgetEdit(value, sizeof(*value));
        if (mixed) DrawNeutralMixedCheckboxMark(!changed, *value);
        return changed;
    }
    inline bool BatchDragFloat(const char* label, float* value, float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = DragFloat(label, value, speed, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
    inline bool BatchDragFloat2(const char* label, float value[2], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 2)); const bool changed = DragFloat2(label, value, speed, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 2); return changed; }
    inline bool BatchDragFloat3(const char* label, float value[3], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 3)); const bool changed = DragFloat3(label, value, speed, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 3); return changed; }
    inline bool BatchDragFloat4(const char* label, float value[4], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 4)); const bool changed = DragFloat4(label, value, speed, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 4); return changed; }
    inline bool BatchDragInt(const char* label, int* value, float speed = 1.0f, int min = 0, int max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = DragInt(label, value, speed, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
    inline bool BatchSliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = SliderFloat(label, value, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
    inline bool BatchSliderFloat2(const char* label, float value[2], float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 2)); const bool changed = SliderFloat2(label, value, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 2); return changed; }
    inline bool BatchSliderFloat3(const char* label, float value[3], float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 3)); const bool changed = SliderFloat3(label, value, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 3); return changed; }
    inline bool BatchSliderFloat4(const char* label, float value[4], float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 4)); const bool changed = SliderFloat4(label, value, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 4); return changed; }
    inline bool BatchSliderInt(const char* label, int* value, int min, int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = SliderInt(label, value, min, max, format, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
    inline bool BatchColorEdit3(const char* label, float value[3], ImGuiColorEditFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 3)); const bool changed = ColorEdit3(label, value, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 3); return changed; }
    inline bool BatchColorEdit4(const char* label, float value[4], ImGuiColorEditFlags flags = 0)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(float) * 4)); const bool changed = ColorEdit4(label, value, flags); if (changed) SyncInspectorWidgetEdit(value, sizeof(float) * 4); return changed; }
    inline bool BatchCombo(const char* label, int* value, const char* const items[], int count, int height = -1)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = Combo(label, value, items, count, height); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
    inline bool BatchCombo(const char* label, int* value, const char* (*getter)(void*, int), void* data, int count, int height = -1)
    { InspectorMixedValueScope mixed(InspectorWidgetHasMixedValue(value, sizeof(*value))); const bool changed = Combo(label, value, getter, data, count, height); if (changed) SyncInspectorWidgetEdit(value, sizeof(*value)); return changed; }
}

#define Checkbox(...) BatchCheckbox(__VA_ARGS__)
#define DragFloat(...) BatchDragFloat(__VA_ARGS__)
#define DragFloat2(...) BatchDragFloat2(__VA_ARGS__)
#define DragFloat3(...) BatchDragFloat3(__VA_ARGS__)
#define DragFloat4(...) BatchDragFloat4(__VA_ARGS__)
#define DragInt(...) BatchDragInt(__VA_ARGS__)
#define SliderFloat(...) BatchSliderFloat(__VA_ARGS__)
#define SliderFloat2(...) BatchSliderFloat2(__VA_ARGS__)
#define SliderFloat3(...) BatchSliderFloat3(__VA_ARGS__)
#define SliderFloat4(...) BatchSliderFloat4(__VA_ARGS__)
#define SliderInt(...) BatchSliderInt(__VA_ARGS__)
#define ColorEdit3(...) BatchColorEdit3(__VA_ARGS__)
#define ColorEdit4(...) BatchColorEdit4(__VA_ARGS__)
#define Combo(...) BatchCombo(__VA_ARGS__)


namespace
{
    void DrawHelpMarker(const char* id, const char* explanation);

    void DrawAudioMixerPanel(Application& app)
    {
        AudioMixer& mixer = AudioMixer::Get();
        static bool savePending = false;
        static double lastChange = 0.0;
        bool changed = false;
        if (ImGui::CollapsingHeader(UI("Mixer de áudio##AudioMixer")))
        {
            ImGui::TextDisabled(UI("Buses, volumes efetivos e snapshots deste projeto."));
            for (const auto& group : mixer.GetAllGroups())
            {
                ImGui::PushID(group->GetID().c_str());
                ImGui::Text("%s", group->GetName().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)  %d voices", group->GetID().c_str(), group->GetActiveVoiceCount());
                float volume = group->GetVolume();
                if (ImGui::SliderFloat(UI("Volume##MixerVolume"), &volume, 0.0f, 1.0f, "%.2f"))
                { group->SetVolume(volume); changed = true; }
                bool muted = group->IsMuted();
                if (ImGui::Checkbox(UI("Mute##MixerMute"), &muted))
                { group->SetMuted(muted); changed = true; }
                ImGui::SameLine();
                bool solo = group->IsSolo();
                if (ImGui::Checkbox(UI("Solo##MixerSolo"), &solo))
                { group->SetSolo(solo); changed = true; }
                ImGui::TextDisabled(UI("Efetivo: %.2f"), mixer.GetEffectiveVolume(group->GetID()));
                ImGui::PopID();
                ImGui::Separator();
            }

            static char groupName[96] = {};
            static int parentIndex = 0;
            ImGui::InputText(UI("Novo grupo##MixerGroupName"), groupName, sizeof(groupName));
            const auto& groups = mixer.GetAllGroups();
            std::vector<const char*> parents;
            parents.reserve(groups.size());
            for (const auto& group : groups) parents.push_back(group->GetName().c_str());
            if (!parents.empty())
            {
                parentIndex = std::clamp(parentIndex, 0, static_cast<int>(parents.size()) - 1);
                ImGui::Combo(UI("Grupo pai##MixerParent"), &parentIndex, parents.data(), static_cast<int>(parents.size()));
                if (ImGui::Button(UI("Criar grupo##MixerCreate")) && groupName[0] != '\0')
                {
                    std::string id;
                    for (unsigned char ch : std::string(groupName))
                        if (std::isalnum(ch)) id.push_back(static_cast<char>(std::tolower(ch)));
                        else if (!id.empty() && id.back() != '_') id.push_back('_');
                    while (!id.empty() && id.back() == '_') id.pop_back();
                    if (!id.empty())
                    {
                        id = "custom_" + id;
                        mixer.CreateGroup(id, groupName, groups[static_cast<size_t>(parentIndex)]->GetID());
                        groupName[0] = '\0';
                        changed = true;
                    }
                }
            }

            static char snapshotName[96] = {};
            static float transition = 0.25f;
            ImGui::Separator();
            ImGui::InputText(UI("Snapshot##MixerSnapshotName"), snapshotName, sizeof(snapshotName));
            ImGui::SameLine();
            if (ImGui::Button(UI("Capturar##MixerCapture")) && snapshotName[0] != '\0')
            {
                if (mixer.CaptureSnapshot(snapshotName))
                    mixer.Save(app.GetProjectConfig().ProjectPath);
            }
            ImGui::SliderFloat(UI("Transição##MixerTransition"), &transition, 0.0f, 10.0f, "%.2f s");
            const auto snapshots = mixer.GetSnapshots();
            for (const auto& snapshot : snapshots)
            {
                ImGui::PushID(snapshot.Name.c_str());
                ImGui::TextUnformatted(snapshot.Name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(UI("Aplicar##MixerApply")))
                    mixer.ApplySnapshot(snapshot.Name, transition);
                ImGui::SameLine();
                if (ImGui::SmallButton(UI("Excluir##MixerDelete")))
                {
                    mixer.RemoveSnapshot(snapshot.Name);
                    mixer.Save(app.GetProjectConfig().ProjectPath);
                }
                ImGui::PopID();
            }
        }

        if (changed)
        { savePending = true; lastChange = ImGui::GetTime(); }
        if (savePending && ImGui::GetTime() - lastChange > 0.6)
        {
            mixer.Save(app.GetProjectConfig().ProjectPath);
            savePending = false;
        }
    }

    void DrawAcousticGeometryInspector(Application& app, Entity& entity)
    {
        if (app.Is2DWorldMode() || !entity.GetMeshComponent().MeshData)
            return;

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.30f, 0.52f, 0.78f, 0.85f));
        const bool open = ImGui::CollapsingHeader(
            UI("[Acústica] Geometria de áudio##AcousticGeometry"),
            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        if (!open) return;

        ImGui::TextDisabled("Define como este objeto interfere entre uma fonte e o listener.");
        bool changed = false;
        changed |= ImGui::Checkbox(UI("Participa da acústica##AcousticGeometry"), &entity.AudioAcousticGeometryEnabled);
        DrawHelpMarker("AcousticGeometryEnabled",
            "Inclui este objeto nas consultas acústicas das fontes de áudio.");
        if (entity.AudioAcousticGeometryEnabled)
        {
            changed |= ImGui::Checkbox(UI("Bloqueia o áudio##AcousticOccluder"), &entity.AudioOccluder);
            DrawHelpMarker("AcousticOccluder",
                "Este objeto pode reduzir o som quando bloqueia a linha entre fonte e listener.");
            changed |= ImGui::Checkbox(UI("Permitir transmissão##AcousticTransmission"), &entity.AudioTransmissionEnabled);
            DrawHelpMarker("AcousticTransmissionEnabled",
                "Permite que parte do som atravesse este objeto, conforme transmissão e absorção.");
            changed |= ImGui::SliderFloat(UI("Transmissão##AcousticTransmissionValue"), &entity.AudioTransmission, 0.0f, 1.0f, "%.2f");
            DrawHelpMarker("AcousticTransmissionValue", "0 bloqueia a transmissão; 1 permite a passagem máxima configurada.");
            changed |= ImGui::SliderFloat(UI("Absorção##AcousticAbsorption"), &entity.AudioAbsorption, 0.0f, 1.0f, "%.2f");
            DrawHelpMarker("AcousticAbsorption", "Quanto do som é absorvido pelo objeto quando ele interfere no caminho.");
            changed |= ImGui::SliderFloat(UI("Difração##AcousticDiffraction"), &entity.AudioDiffraction, 0.0f, 1.0f, "%.2f");
            DrawHelpMarker("AcousticDiffraction",
                "Controla quanto o cálculo tenta recuperar o som por caminhos próximos às bordas.");
        }
        if (changed) app.MarkDirty();
    }

    void DrawUIInspector(Application& app, Entity& entity)
    {
        if (!entity.HasUI())
            return;

        const bool uiShownInEditor = app.GetShowUIInEditor();
        if (uiShownInEditor)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button(UI("Mostrar no modo editor##UIShowInEditor"), ImVec2(-1, 0)))
            app.GetShowUIInEditor() = !uiShownInEditor;
        if (uiShownInEditor) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", uiShownInEditor
                ? UI("UI 2D visivel no modo editor - clique para ocultar")
                : UI("UI 2D oculta no modo editor - clique para mostrar"));
        ImGui::Separator();

        UIComponent& ui = entity.GetUIComponent();
        const MaraeUI::ResolvedUIMap resolvedUIRects =
            MaraeUI::ResolveLayouts(app.GetScene().GetEntities(), 960.0f, 540.0f);
        UIComponent::ResolvedRect parentRect{ 0.0f, 0.0f, 960.0f, 540.0f };
        const Entity* uiParent = entity.GetParentID() != 0
            ? app.GetScene().FindEntityByID(entity.GetParentID()) : nullptr;
        if (uiParent && uiParent->HasUI())
        {
            const auto parentRectIt = resolvedUIRects.find(uiParent->GetID());
            if (parentRectIt != resolvedUIRects.end())
                parentRect = parentRectIt->second;
        }
        const float parentWidth = parentRect.Width;
        const float parentHeight = parentRect.Height;
        const auto localResolvedRect = ui.ResolveRect(parentWidth, parentHeight);
        ImGui::Separator();
        ImGui::TextUnformatted(UI("UI 2D / GuiObject"));
        ImGui::TextDisabled("Modelo inspirado em ScreenGui, Frame, TextButton e ImageButton.");

        const char* types[] = { "ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel",
                                "ImageButton", "TextBox", "ScrollingFrame", "Slider", "Checkbox",
                                "ProgressBar", "Dropdown" };
        int type = static_cast<int>(ui.Type);
        if (ImGui::Combo(UI("Tipo##UI"), &type, types, IM_ARRAYSIZE(types)))
        {
            ui.Type = static_cast<UIElementType>(std::clamp(type, 0, static_cast<int>(IM_ARRAYSIZE(types)) - 1));
            app.MarkDirty();
        }
        if (ImGui::Checkbox(UI("Ativo##UI"), &ui.Enabled)) app.MarkDirty();
        bool responsive = ui.UseResponsiveAnchors;
        if (ImGui::Checkbox(UI("Âncoras responsivas##UI"), &responsive))
        {
            if (responsive) ui.EnableResponsiveAnchors(parentWidth, parentHeight);
            else ui.UseResponsiveAnchors = false;
            app.MarkDirty();
        }
        if (ui.UseResponsiveAnchors)
        {
            const auto resolvedIt = resolvedUIRects.find(entity.GetID());
            const auto resolved = resolvedIt != resolvedUIRects.end()
                ? resolvedIt->second : localResolvedRect;
            ImGui::Text(UI("Retângulo resolvido: %.0f, %.0f  |  %.0f × %.0f"),
                        resolved.X, resolved.Y, resolved.Width, resolved.Height);
            ImGui::TextDisabled(UI("Âncoras relativas ao contêiner; offsets em pixels."));
            if (ImGui::DragFloat2(UI("Âncora mínima##UI"), &ui.AnchorMinX, 0.005f, 0.0f, 1.0f)) app.MarkDirty();
            if (ImGui::DragFloat2(UI("Âncora máxima##UI"), &ui.AnchorMaxX, 0.005f, 0.0f, 1.0f)) app.MarkDirty();
            if (ImGui::DragFloat2(UI("Offset mínimo##UI"), &ui.OffsetMinX, 1.0f)) app.MarkDirty();
            if (ImGui::DragFloat2(UI("Offset máximo##UI"), &ui.OffsetMaxX, 1.0f)) app.MarkDirty();
            if (ImGui::SmallButton(UI("Esticar na horizontal##UI")))
            {
                ui.AnchorMinX = 0.0f; ui.AnchorMaxX = 1.0f;
                ui.OffsetMinX = localResolvedRect.X;
                ui.OffsetMaxX = localResolvedRect.X + localResolvedRect.Width - parentWidth;
                app.MarkDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(UI("Esticar na vertical##UI")))
            {
                ui.AnchorMinY = 0.0f; ui.AnchorMaxY = 1.0f;
                ui.OffsetMinY = localResolvedRect.Y;
                ui.OffsetMaxY = localResolvedRect.Y + localResolvedRect.Height - parentHeight;
                app.MarkDirty();
            }
        }
        else
        {
            if (ImGui::DragFloat2(uiParent ? UI("Posição local##UI") : UI("Posição##UI"),
                                  &ui.PositionX, 1.0f)) app.MarkDirty();
            if (ImGui::DragFloat2("Tamanho##UI", &ui.Width, 1.0f, 1.0f, 4096.0f)) app.MarkDirty();
            if (ImGui::DragFloat2("Âncora##UI", &ui.AnchorX, 0.01f, 0.0f, 1.0f)) app.MarkDirty();
        }
        if (ImGui::CollapsingHeader(UI("Layout rápido##UI")))
        {
            ImGui::TextDisabled(UI("Âncora e alinhamento no pai ou canvas"));
            const char* anchors[3][3] = { { "Sup. esq.", "Sup. centro", "Sup. dir." },
                                          { "Meio esq.", "Centro", "Meio dir." },
                                          { "Inf. esq.", "Inf. centro", "Inf. dir." } };
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    if (column > 0) ImGui::SameLine();
                    ImGui::PushID(row * 3 + column);
                    if (ImGui::SmallButton(anchors[row][column]))
                    {
                        ui.AnchorX = static_cast<float>(column) * 0.5f;
                        ui.AnchorY = static_cast<float>(row) * 0.5f;
                        ui.PositionX = ui.AnchorX * parentWidth;
                        ui.PositionY = ui.AnchorY * parentHeight;
                        if (ui.UseResponsiveAnchors)
                        {
                            ui.AnchorMinX = ui.AnchorMaxX = ui.AnchorX;
                            ui.AnchorMinY = ui.AnchorMaxY = ui.AnchorY;
                            ui.OffsetMinX = -ui.AnchorX * ui.Width;
                            ui.OffsetMinY = -ui.AnchorY * ui.Height;
                            ui.OffsetMaxX = (1.0f - ui.AnchorX) * ui.Width;
                            ui.OffsetMaxY = (1.0f - ui.AnchorY) * ui.Height;
                        }
                        app.MarkDirty();
                    }
                    ImGui::PopID();
                }
            }
            const struct { const char* label; float width; float height; } sizes[] = {
                { "Pequeno", 120.0f, 40.0f }, { "Padrão", 240.0f, 64.0f },
                { "Grande", 480.0f, 120.0f }, { "Canvas", 960.0f, 540.0f }
            };
            for (const auto& size : sizes)
            {
                if (ImGui::Button(UI(size.label)))
                {
                    ui.Width = size.width;
                    ui.Height = size.height;
                    if (ui.UseResponsiveAnchors)
                    {
                        ui.OffsetMaxX = ui.OffsetMinX + size.width -
                            (ui.AnchorMaxX - ui.AnchorMinX) * parentWidth;
                        ui.OffsetMaxY = ui.OffsetMinY + size.height -
                            (ui.AnchorMaxY - ui.AnchorMinY) * parentHeight;
                    }
                    app.MarkDirty();
                }
                if (&size != &sizes[std::size(sizes) - 1]) ImGui::SameLine();
            }
        }
        if (ImGui::DragFloat(UI("Rotação##UI"), &ui.Rotation, 0.5f, -360.0f, 360.0f)) app.MarkDirty();
        if (ImGui::DragInt(UI("ZIndex##UI"), &ui.ZIndex, 1.0f, -1000, 1000)) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Transparência##UI"), &ui.Transparency, 0.0f, 1.0f)) app.MarkDirty();
        if (ImGui::DragFloat(UI("Raio dos cantos##UI"), &ui.CornerRadius, 1.0f, 0.0f, 128.0f)) app.MarkDirty();
        if (ImGui::Checkbox(UI("Recortar conteúdo##UI"), &ui.ClipsContent)) app.MarkDirty();
        if (ImGui::Checkbox(UI("Scroll view##UI"), &ui.ScrollViewEnabled)) app.MarkDirty();

        if (ImGui::CollapsingHeader(UI("Layout automático##UI")))
        {
            const char* layoutModes[] = { "Manual", "Horizontal", "Vertical", "Grade" };
            int layoutMode = static_cast<int>(ui.LayoutMode);
            if (ImGui::Combo(UI("Modo##UILayoutMode"), &layoutMode, layoutModes, IM_ARRAYSIZE(layoutModes)))
            {
                ui.LayoutMode = static_cast<UILayoutMode>(std::clamp(layoutMode, 0, 3));
                ForEachInspectorBatchComponent<UIComponent>([&](UIComponent& target) { target.LayoutMode = ui.LayoutMode; });
                app.MarkDirty();
            }
            if (ui.LayoutMode != UILayoutMode::None || ui.ScrollViewEnabled)
            {
                if (ImGui::DragFloat(UI("Padding##UILayout"), &ui.LayoutPadding, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                if (ImGui::DragFloat(UI("Espaçamento##UILayout"), &ui.LayoutSpacing, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                if (ui.LayoutMode == UILayoutMode::Grid &&
                    ImGui::DragInt(UI("Colunas##UILayout"), &ui.LayoutColumns, 1.0f, 1, 64)) app.MarkDirty();
                if (ImGui::Checkbox(UI("Esticar filhos##UILayout"), &ui.StretchLayoutChildren)) app.MarkDirty();
                ImGui::TextDisabled(UI("O layout organiza filhos diretos deste elemento."));
            }
        }
        const char* uiThemes[] = { "Personalizado", "Marae", "Claro", "Retro" };
        if (ImGui::Combo(UI("Tema##UI"), &ui.Theme, uiThemes, IM_ARRAYSIZE(uiThemes))) app.MarkDirty();

        if (ImGui::CollapsingHeader(UI("Aparência##UI"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Cor de fundo##UI", ui.BackgroundColor,
                                  ImGuiColorEditFlags_AlphaBar))
            {
                ui.Theme = 0;
                app.MarkDirty();
            }
            if (ImGui::ColorEdit4("Cor da borda##UI", ui.BorderColor,
                                  ImGuiColorEditFlags_AlphaBar))
            {
                ui.Theme = 0;
                app.MarkDirty();
            }
            if (ImGui::DragFloat(UI("Espessura da borda##UI"), &ui.BorderThickness,
                                 0.25f, 0.0f, 32.0f, "%.1f px")) app.MarkDirty();
            ImGui::TextUnformatted(UI("Cores rápidas"));
            const struct { const char* label; float color[4]; } colors[] = {
                { "Azul", { 0.10f, 0.35f, 0.72f, 1.0f } },
                { "Claro", { 0.82f, 0.86f, 0.92f, 1.0f } },
                { "Escuro", { 0.08f, 0.09f, 0.12f, 1.0f } },
                { "Verde", { 0.10f, 0.52f, 0.32f, 1.0f } }
            };
            for (const auto& preset : colors)
            {
                if (ImGui::SmallButton(UI(preset.label)))
                {
                    std::copy(std::begin(preset.color), std::end(preset.color), ui.BackgroundColor);
                    ui.Theme = 0;
                    app.MarkDirty();
                }
                if (&preset != &colors[std::size(colors) - 1]) ImGui::SameLine();
            }
        }

        // ── Propriedades específicas de widget ───────────────────────────────
        const bool isSlider = ui.Type == UIElementType::Slider;
        const bool isProgressBar = ui.Type == UIElementType::ProgressBar;
        const bool isCheckbox = ui.Type == UIElementType::Checkbox;
        const bool isDropdown = ui.Type == UIElementType::Dropdown;

        if (isSlider || isProgressBar || isCheckbox || isDropdown)
        {
            if (ImGui::CollapsingHeader(UI("Controle##UIWidget"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (isSlider || isProgressBar)
                {
                    if (ImGui::DragFloat(UI("Valor mínimo##UIWidget"), &ui.MinValue, 0.1f)) app.MarkDirty();
                    if (ImGui::DragFloat(UI("Valor máximo##UIWidget"), &ui.MaxValue, 0.1f)) app.MarkDirty();
                    if (ui.MaxValue < ui.MinValue)
                    {
                        ui.MaxValue = ui.MinValue;
                        app.MarkDirty();
                    }
                    if (ImGui::SliderFloat(UI("Valor##UIWidget"), &ui.Value, ui.MinValue, ui.MaxValue, "%.3f"))
                    {
                        ui.Value = std::clamp(ui.Value, ui.MinValue, ui.MaxValue);
                        app.MarkDirty();
                    }
                    if (ImGui::ColorEdit4("Preenchimento##UIWidget", ui.FillColor,
                                          ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                    if (ImGui::ColorEdit4("Trilho##UIWidget", ui.TrackColor,
                                          ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                }
                if (isCheckbox)
                {
                    if (ImGui::Checkbox(UI("Marcado##UIWidget"), &ui.Checked)) app.MarkDirty();
                    if (ImGui::ColorEdit4("Preenchimento##UIWidget", ui.FillColor,
                                          ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                }
                if (isDropdown)
                {
                    ImGui::TextUnformatted(UI("Opções"));
                    int removeIndex = -1;
                    for (size_t i = 0; i < ui.Options.size(); ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));
                        char option[256] = {};
                        strncpy_s(option, ui.Options[i].c_str(), _TRUNCATE);
                        if (ImGui::InputText(UI("##UIOption"), option, sizeof(option)))
                        {
                            ui.Options[i] = option;
                            ForEachInspectorBatchComponent<UIComponent>([&](UIComponent& target)
                            {
                                if (i < target.Options.size()) target.Options[i] = option;
                            });
                            app.MarkDirty();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton(UI("x##UIOptionRemove")))
                            removeIndex = static_cast<int>(i);
                        ImGui::PopID();
                    }
                    if (removeIndex >= 0)
                    {
                        ui.Options.erase(ui.Options.begin() + removeIndex);
                        if (ui.SelectedIndex >= static_cast<int>(ui.Options.size()))
                            ui.SelectedIndex = std::max(0, static_cast<int>(ui.Options.size()) - 1);
                        app.MarkDirty();
                    }
                    if (ImGui::SmallButton(UI("Adicionar opção##UIWidget")))
                    {
                        ui.Options.push_back("Opção " + std::to_string(ui.Options.size() + 1));
                        app.MarkDirty();
                    }
                    if (!ui.Options.empty())
                    {
                        std::vector<const char*> optionPtrs;
                        optionPtrs.reserve(ui.Options.size());
                        for (const std::string& option : ui.Options)
                            optionPtrs.push_back(option.c_str());
                        int selected = std::clamp(ui.SelectedIndex, 0, static_cast<int>(ui.Options.size()) - 1);
                        if (ImGui::Combo(UI("Selecionado##UIWidget"), &selected,
                                         optionPtrs.data(), static_cast<int>(optionPtrs.size())))
                        {
                            ui.SelectedIndex = selected;
                            app.MarkDirty();
                        }
                    }
                    if (ImGui::ColorEdit4("Cor ao passar##UIWidget", ui.HoverColor,
                                          ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                    if (ImGui::ColorEdit4("Cor pressionada##UIWidget", ui.PressedColor,
                                          ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                }
            }
        }

        if (UIElementIsButton(ui.Type))
        {
            if (ImGui::CollapsingHeader(UI("Estados do botão##UIStates")))
            {
                if (ImGui::ColorEdit4("Cor ao passar##UIStates", ui.HoverColor,
                                      ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
                if (ImGui::ColorEdit4("Cor pressionada##UIStates", ui.PressedColor,
                                      ImGuiColorEditFlags_AlphaBar)) app.MarkDirty();
            }
        }

        if (UIElementHasImage(ui.Type))
        {
            if (ImGui::CollapsingHeader(UI("Imagem##UIImage"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                char image[512] = {};
                strncpy_s(image, ui.ImagePath.c_str(), _TRUNCATE);
                if (ImGui::InputText(UI("Caminho##UI"), image, sizeof(image)))
                {
                    ui.ImagePath = image;
                    ForEachInspectorBatchComponent<UIComponent>([&](UIComponent& target) { target.ImagePath = image; });
                    app.MarkDirty();
                }
                if (ImGui::Checkbox(UI("Nine-slice##UI"), &ui.NineSlice)) app.MarkDirty();
                if (ui.NineSlice)
                {
                    if (ImGui::DragFloat(UI("Borda esquerda##UI"), &ui.SliceLeft, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                    if (ImGui::DragFloat(UI("Borda superior##UI"), &ui.SliceTop, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                    if (ImGui::DragFloat(UI("Borda direita##UI"), &ui.SliceRight, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                    if (ImGui::DragFloat(UI("Borda inferior##UI"), &ui.SliceBottom, 1.0f, 0.0f, 512.0f)) app.MarkDirty();
                }
            }
        }

        const bool hasEditableText = ui.Type == UIElementType::TextLabel ||
            ui.Type == UIElementType::TextButton || ui.Type == UIElementType::TextBox ||
            ui.Type == UIElementType::Checkbox || ui.Type == UIElementType::ProgressBar;

        if (hasEditableText)
        {
            int fontIndex = MaraeUI::GetUIFontIndex(ui.Font);
            if (ImGui::Combo(UI("Fonte##UIFont"), &fontIndex, MaraeUI::UIFontNames.data(),
                             static_cast<int>(MaraeUI::UIFontNames.size())))
            {
                ui.Font = MaraeUI::UIFontNames[static_cast<size_t>(std::clamp(
                    fontIndex, 0, static_cast<int>(MaraeUI::UIFontNames.size()) - 1))];
                app.MarkDirty();
            }
            if (ImGui::Checkbox(UI("Rich text / marcação##UIRichText"), &ui.RichTextEnabled)) app.MarkDirty();
            if (ui.RichTextEnabled)
                ImGui::TextWrapped(UI("Tags: <b>negrito</b>, <color=#RRGGBB>cor</color>, <size=24>tamanho</size> e <br/> quebra de linha."));
            char text[2048] = {};
            strncpy_s(text, ui.Text.c_str(), _TRUNCATE);
            ImGui::TextUnformatted(UI("Conteúdo de texto"));
            if (ImGui::InputTextMultiline(UI("##UIText"), text, sizeof(text),
                                          ImVec2(-1.0f, 72.0f)))
            {
                ui.Text = text;
                ForEachInspectorBatchComponent<UIComponent>([&](UIComponent& target) { target.Text = text; });
                app.MarkDirty();
            }
            int alignment = static_cast<int>(ui.TextAlignment);
            const char* alignments[] = { "Esquerda", "Centro", "Direita" };
            if (ImGui::Combo(UI("Alinhamento##UI"), &alignment, alignments, 3))
            {
                ui.TextAlignment = static_cast<UITextAlignment>(std::clamp(alignment, 0, 2));
                ForEachInspectorBatchComponent<UIComponent>([&](UIComponent& target) { target.TextAlignment = ui.TextAlignment; });
                app.MarkDirty();
            }
            if (ImGui::SliderFloat(UI("Tamanho da fonte##UI"), &ui.FontSize, 6.0f, 128.0f, "%.0f px")) app.MarkDirty();
            if (ImGui::ColorEdit4("Cor do texto##UI", ui.TextColor))
            {
                ui.Theme = 0;
                app.MarkDirty();
            }
        }
        const char* centerLabel = uiParent
            ? "Centralizar no pai##UI" : "Centralizar no canvas##UI";
        if (ImGui::Button(UI(centerLabel), ImVec2(-1.0f, 0.0f)))
        {
            ui.AnchorX = ui.AnchorY = 0.5f;
            ui.PositionX = parentWidth * 0.5f;
            ui.PositionY = parentHeight * 0.5f;
            if (ui.UseResponsiveAnchors)
            {
                ui.AnchorMinX = ui.AnchorMaxX = ui.AnchorMinY = ui.AnchorMaxY = 0.5f;
                ui.OffsetMinX = -ui.Width * 0.5f;
                ui.OffsetMinY = -ui.Height * 0.5f;
                ui.OffsetMaxX = ui.Width * 0.5f;
                ui.OffsetMaxY = ui.Height * 0.5f;
            }
            app.MarkDirty();
        }
    }

    void DrawAIInspector(Application& app, Entity& entity)
    {
        if (!entity.HasAI()) return;
        AIComponent& ai = entity.GetAIComponent();
        ImGui::Separator();
        if (!ImGui::CollapsingHeader(UI("IA de jogo / Behavior##GameAI"), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        bool changed = ImGui::Checkbox(UI("Ativa##GameAI"), &ai.Enabled);
        int mode = static_cast<int>(ai.CurrentMode);
        const char* modes[] = { "Behavior Tree", "State Machine" };
        if (ImGui::Combo(UI("Modo##GameAI"), &mode, modes, IM_ARRAYSIZE(modes)))
        {
            ai.CurrentMode = static_cast<AIComponent::Mode>(std::clamp(mode, 0, 1));
            ai.ResetRuntime();
            changed = true;
        }

        ImGui::SeparatorText(UI("Percepção visual"));
        changed |= ImGui::Checkbox(UI("Visão ativa##GameAI"), &ai.SightEnabled);
        char tag[128]{};
        std::snprintf(tag, sizeof(tag), "%s", ai.TargetTag.c_str());
        if (ImGui::InputText(UI("Tag alvo (* = qualquer)##GameAI"), tag, sizeof(tag)))
        {
            ai.TargetTag = tag;
            changed = true;
        }
        changed |= ImGui::DragFloat(UI("Alcance da visão##GameAI"), &ai.SightRadius, 0.1f, 0.0f, 10000.0f, "%.1f m");
        changed |= ImGui::SliderFloat(UI("Campo de visão##GameAI"), &ai.FieldOfViewDegrees, 1.0f, 360.0f, "%.0f°");
        changed |= ImGui::DragFloat(UI("Memória do alvo##GameAI"), &ai.TargetMemorySeconds, 0.05f, 0.0f, 120.0f, "%.2f s");
        Entity* target = app.GetScene().FindEntityByID(ai.VisibleTargetID);
        ImGui::TextDisabled(UI("Visível: %s"), target ? target->GetName().c_str() : "nenhum");
        if (ai.RememberedTargetID && ai.RememberedTargetID != ai.VisibleTargetID)
            ImGui::TextDisabled(UI("Último alvo: %llu  (%.1f s)"),
                static_cast<unsigned long long>(ai.RememberedTargetID), ai.RememberedTargetTime);

        if (ai.CurrentMode == AIComponent::Mode::BehaviorTree)
        {
            ImGui::SeparatorText(UI("Árvore de comportamento"));
            ImGui::TextDisabled(UI("Sequence executa todos; Selector tenta até um sucesso. Use Parent ID para montar a árvore."));
            const char* nodeTypes[] = { "Root", "Sequence", "Selector", "Inverter", "Alvo visível?",
                "Alvo dentro do alcance?", "Perseguir alvo", "Fugir do alvo", "Esperar", "Parar" };
            static int parentNodeId = 1;
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt(UI("Parent ID##AINodeParent"), &parentNodeId);
            parentNodeId = std::max(1, parentNodeId);
            auto addNode = [&](AIComponent::NodeType nodeType)
            {
                if (ai.Nodes.size() >= 128) return;
                int nextId = 1;
                for (const auto& node : ai.Nodes) nextId = std::max(nextId, node.Id + 1);
                const auto parent = std::find_if(ai.Nodes.begin(), ai.Nodes.end(),
                    [&](const auto& node) { return node.Id == parentNodeId; });
                if (parent == ai.Nodes.end()) parentNodeId = ai.RootNodeId;
                ai.Nodes.push_back({ nextId, parentNodeId, nodeType });
                changed = true;
            };
            const struct { const char* Label; AIComponent::NodeType Type; } buttons[] = {
                { "Sequence", AIComponent::NodeType::Sequence }, { "Selector", AIComponent::NodeType::Selector },
                { "Condição alvo", AIComponent::NodeType::HasVisibleTarget }, { "Perseguir", AIComponent::NodeType::ChaseTarget },
                { "Fugir", AIComponent::NodeType::FleeTarget }, { "Esperar", AIComponent::NodeType::Wait },
                { "Parar", AIComponent::NodeType::Stop }
            };
            for (const auto& button : buttons)
            {
                if (ImGui::SmallButton(UI(button.Label))) addNode(button.Type);
                ImGui::SameLine();
            }
            ImGui::NewLine();
            for (size_t i = 0; i < ai.Nodes.size(); ++i)
            {
                auto& node = ai.Nodes[i];
                ImGui::PushID(node.Id);
                ImGui::Text("#%d", node.Id); ImGui::SameLine();
                int type = static_cast<int>(node.Type);
                if (ImGui::Combo(UI("Tipo##AINodeType"), &type, nodeTypes, IM_ARRAYSIZE(nodeTypes)))
                {
                    node.Type = static_cast<AIComponent::NodeType>(std::clamp(type, 0, 9));
                    changed = true;
                }
                if (node.Id != ai.RootNodeId)
                {
                    changed |= ImGui::DragInt(UI("Pai##AINodeParent"), &node.ParentId, 1.0f, 1, 100000);
                    if (node.ParentId == node.Id) node.ParentId = ai.RootNodeId;
                }
                if (node.Type == AIComponent::NodeType::TargetWithinRange ||
                    node.Type == AIComponent::NodeType::ChaseTarget ||
                    node.Type == AIComponent::NodeType::FleeTarget)
                    changed |= ImGui::DragFloat(UI("Distância##AINodeRange"), &node.Range, 0.1f, 0.0f, 10000.0f);
                if (node.Type == AIComponent::NodeType::ChaseTarget || node.Type == AIComponent::NodeType::FleeTarget)
                    changed |= ImGui::DragFloat(UI("Velocidade##AINodeSpeed"), &node.Speed, 0.1f, 0.0f, 100.0f);
                if (node.Type == AIComponent::NodeType::Wait)
                    changed |= ImGui::DragFloat(UI("Duração##AINodeWait"), &node.Duration, 0.05f, 0.0f, 3600.0f, "%.2f s");
                const bool hasChildren = std::any_of(ai.Nodes.begin(), ai.Nodes.end(),
                    [&](const auto& other) { return other.ParentId == node.Id; });
                if (node.Id != ai.RootNodeId && !hasChildren && ImGui::SmallButton(UI("Remover nó##AINodeRemove")))
                {
                    ai.Nodes.erase(ai.Nodes.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        else
        {
            ImGui::SeparatorText(UI("Máquina de estados de IA"));
            char initial[96]{};
            std::snprintf(initial, sizeof(initial), "%s", ai.InitialState.c_str());
            if (ImGui::InputText(UI("Estado inicial##AIInitial"), initial, sizeof(initial)))
            {
                ai.InitialState = initial;
                changed = true;
            }
            ImGui::TextDisabled(UI("Estado atual: %s"), ai.CurrentState.c_str());
            const char* tasks[] = { "Idle", "Perseguir alvo", "Fugir do alvo", "Aguardar" };
            for (size_t i = 0; i < ai.States.size(); ++i)
            {
                auto& state = ai.States[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextUnformatted(state.Name.c_str());
                int action = static_cast<int>(state.Action);
                if (ImGui::Combo(UI("Ação##AIStateTask"), &action, tasks, IM_ARRAYSIZE(tasks)))
                {
                    state.Action = static_cast<AIComponent::Task>(std::clamp(action, 0, 3));
                    changed = true;
                }
                changed |= ImGui::DragFloat(UI("Velocidade##AIStateSpeed"), &state.Speed, 0.1f, 0.0f, 100.0f);
                changed |= ImGui::DragFloat(UI("Alcance##AIStateRange"), &state.Range, 0.1f, 0.0f, 10000.0f);
                if (ImGui::SmallButton(UI("Remover##AIStateRemove")) && ai.States.size() > 1)
                {
                    const std::string removed = state.Name;
                    ai.States.erase(ai.States.begin() + static_cast<std::ptrdiff_t>(i));
                    std::erase_if(ai.Transitions, [&](const auto& tr) { return tr.From == removed || tr.To == removed; });
                    if (ai.InitialState == removed) ai.InitialState = ai.States.front().Name;
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            static char newStateName[96] = "Chase";
            ImGui::InputText(UI("Novo estado##AIStateNewName"), newStateName, sizeof(newStateName));
            ImGui::SameLine();
            if (ImGui::Button(UI("Adicionar estado##AIStateAdd")) && newStateName[0] && ai.States.size() < 64 &&
                std::none_of(ai.States.begin(), ai.States.end(), [&](const auto& s) { return s.Name == newStateName; }))
            {
                ai.States.push_back({ newStateName, AIComponent::Task::ChaseTarget });
                changed = true;
            }
            static int fromIndex = 0, toIndex = 0, conditionIndex = 0;
            if (!ai.States.empty())
            {
                fromIndex = std::clamp(fromIndex, 0, static_cast<int>(ai.States.size()) - 1);
                toIndex = std::clamp(toIndex, 0, static_cast<int>(ai.States.size()) - 1);
                const char* conditions[] = { "Alvo visível", "Alvo perdido", "Alvo perto", "Alvo longe" };
                ImGui::Combo(UI("De##AITransitionFrom"), &fromIndex,
                    [](void* data, int idx) -> const char* { auto* v = static_cast<std::vector<AIComponent::State>*>(data); return v->at(idx).Name.c_str(); },
                    &ai.States, static_cast<int>(ai.States.size()));
                ImGui::SameLine();
                ImGui::Combo(UI("Para##AITransitionTo"), &toIndex,
                    [](void* data, int idx) -> const char* { auto* v = static_cast<std::vector<AIComponent::State>*>(data); return v->at(idx).Name.c_str(); },
                    &ai.States, static_cast<int>(ai.States.size()));
                ImGui::Combo(UI("Quando##AITransitionCondition"), &conditionIndex, conditions, IM_ARRAYSIZE(conditions));
                static float transitionRange = 8.0f;
                ImGui::DragFloat(UI("Distância##AITransitionRange"), &transitionRange, 0.1f, 0.0f, 10000.0f);
                if (ImGui::Button(UI("Criar transição##AITransitionAdd")) && fromIndex != toIndex)
                {
                    ai.Transitions.push_back({ ai.States[fromIndex].Name, ai.States[toIndex].Name,
                        static_cast<AIComponent::TransitionCondition>(conditionIndex), transitionRange });
                    changed = true;
                }
                for (size_t i = 0; i < ai.Transitions.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("%s → %s", ai.Transitions[i].From.c_str(), ai.Transitions[i].To.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(UI("Remover transição##AITransitionRemove")))
                    {
                        ai.Transitions.erase(ai.Transitions.begin() + static_cast<std::ptrdiff_t>(i));
                        changed = true; ImGui::PopID(); break;
                    }
                    ImGui::PopID();
                }
            }
        }
        if (changed) app.MarkDirty();
    }

    void DrawHelpMarker(const char* id, const char* explanation)
    {
        ImGui::SameLine(0.0f, 5.0f);
        const std::string buttonID = std::string("?##Help_") + id;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.38f, 0.55f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.78f, 1.0f));
        ImGui::SmallButton(buttonID.c_str());
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(430.0f);
            ImGui::TextUnformatted(explanation);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    uint64_t s_ModelingEntityID = 0;
    uint64_t s_ModelingLocalMeshEntityID = 0;
    uint64_t s_ModelingStatsEntityID = 0;
    uint64_t s_ModelingStatsRevision = std::numeric_limits<uint64_t>::max();
    const Mesh* s_ModelingStatsMesh = nullptr;
    MeshTopology s_ModelingStatsTopology;
    size_t s_ModelingStatsEdgeCount = 0;
    constexpr size_t kMaxModelingVertices = 250000;
    bool s_ModelingMode = false;
    int s_ModelingSelectionMode = 2; // Face
    float s_ModelingAmount = 0.05f;
    int s_ModelingResolutionLevels = 1;
    int s_ModelingRadialSectors = 20;
    float s_ModelingProportionalRadius = 1.0f;
    float s_ModelingSolidifyThickness = 0.05f;
    float s_ModelingDecimateRatio = 0.75f;
    int s_ModelingArrayCount = 2;
    glm::vec3 s_ModelingTranslation(0.0f);
    glm::vec3 s_ModelingRotation(0.0f);
    glm::vec3 s_ModelingScale(1.0f);
    glm::vec3 s_ModelingArrayOffset(1.5f, 0.0f, 0.0f);
    bool s_ModelingProportional = false;
    std::vector<int> s_ModelingSelection;
    InspectorPanel::MiniFaceShape s_MiniFaceShape = InspectorPanel::MiniFaceShape::Squares;
    InspectorPanel::MiniFaceOrientation s_MiniFaceOrientation = InspectorPanel::MiniFaceOrientation::Grid;
    int s_MiniFaceVerticalLines = 2;
    int s_MiniFaceHorizontalLines = 2;
    bool s_HideDiagonalLines = true;
    bool s_ScaleUniformLocked = false;
    float s_ScaleStep = 0.05f;
    bool s_ModelingScaleUniform = false;

    glm::mat4 BuildModelingTransform(const Transform& transform)
    {
        glm::mat4 model = glm::translate(glm::mat4(1.0f),
            glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
        model *= glm::mat4_cast(transform.Rotation);
        return glm::scale(model, glm::vec3(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z));
    }

    bool SaveGeneratedMeshAsset(Application& app, Entity& entity, const Mesh& mesh)
    {
        namespace fs = std::filesystem;
        fs::path root = app.GetProjectConfig().ProjectPath.empty()
            ? fs::current_path()
            : fs::path(app.GetProjectConfig().ProjectPath);
        fs::path relativePath = fs::path("Library") / "Generated" /
            ("Modeling_" + std::to_string(entity.GetID()) + ".obj");
        fs::path absolutePath = root / relativePath;

        std::error_code error;
        fs::create_directories(absolutePath.parent_path(), error);
        if (error)
            return false;

        std::ofstream file(absolutePath);
        if (!file.is_open())
            return false;

        file << std::setprecision(9);
        const auto& vertices = mesh.GetVertices();
        for (const Vertex& vertex : vertices)
            file << "v " << vertex.X << ' ' << vertex.Y << ' ' << vertex.Z << "\n";
        for (const Vertex& vertex : vertices)
            file << "vt " << vertex.U << ' ' << vertex.V << "\n";
        for (const Vertex& vertex : vertices)
            file << "vn " << vertex.NX << ' ' << vertex.NY << ' ' << vertex.NZ << "\n";
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            const size_t a = i + 1;
            const size_t b = i + 2;
            const size_t c = i + 3;
            file << "f "
                 << a << '/' << a << '/' << a << ' '
                 << b << '/' << b << '/' << b << ' '
                 << c << '/' << c << '/' << c << "\n";
        }

        file.close();
        MeshCache::Invalidate(absolutePath.string());
        entity.GetMeshComponent().MeshPath = relativePath.generic_string();
        return true;
    }

    Mesh* EnsureEditableMesh(Application& app, Entity& entity)
    {
        MeshComponent& component = entity.GetMeshComponent();
        if (!component.MeshData || !component.MeshData->IsValid())
            return nullptr;

        if (s_ModelingLocalMeshEntityID != entity.GetID())
        {
            auto localMesh = std::make_shared<Mesh>();
            localMesh->Create(
                component.MeshData->GetVertices(),
                component.MeshData->GetPrimitiveType(),
                "InspectorPanel::ModelingClone");
            component.MeshData = std::move(localMesh);
            s_ModelingLocalMeshEntityID = entity.GetID();
            SaveGeneratedMeshAsset(app, entity, *component.MeshData);
        }
        return component.MeshData.get();
    }

    void ApplyEntityMeshDirect(Application& app, Entity& entity, std::vector<Vertex> vertices, const char* tag)
    {
        if (vertices.empty() || vertices.size() > 1000000) return;
        auto& component = entity.GetMeshComponent();
        if (!component.MeshData) return;
        const std::vector<Vertex> beforeVertices = component.MeshData->GetVertices();
        const Transform beforeTransform = entity.GetTransform();
        Mesh* editableMesh = EnsureEditableMesh(app, entity);
        if (!editableMesh) return;
        editableMesh->UpdateVertices(vertices, tag);
        SaveGeneratedMeshAsset(app, entity, *editableMesh);

        TransformCommand command;
        command.Type = CommandType::Transform;
        command.EntityID = entity.GetID();
        command.EntityIndex = app.GetSelectedEntityIndex();
        command.Before = beforeTransform;
        command.After = entity.GetTransform();
        command.HasMeshData = true;
        command.MeshBefore = beforeVertices;
        command.MeshAfter = std::move(vertices);
        app.GetUndoRedoManager().Push(command);
        s_ModelingSelection.clear();
        app.MarkDirty();
    }

    void SetObjectRadialSegments(Application& app, Entity& entity, int sectors)
    {
        sectors = std::clamp(sectors, 3, 128);
        auto& comp = entity.GetMeshComponent();
        if (!comp.MeshData || !comp.MeshData->IsValid() || !comp.MeshPath.empty())
            return;

        std::string primitive = comp.PrimitiveType;
        std::transform(primitive.begin(), primitive.end(), primitive.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::vector<Vertex> newVerts;

        if (primitive == "sphere")
        {
            int stacks = std::max(6, sectors * 3 / 4);
            newVerts = MeshFactory::CreateSphere(sectors, stacks);
        }
        else if (primitive == "cylinder")
        {
            newVerts = MeshFactory::CreateCylinder(sectors, 1.0f);
        }
        else if (primitive == "cone")
        {
            newVerts = MeshFactory::CreateCone(sectors, 1.0f);
        }
        else if (primitive == "torus")
        {
            newVerts = MeshFactory::CreateTorus(sectors, std::max(6, sectors / 2));
        }
        else if (primitive == "capsule")
        {
            newVerts = MeshFactory::CreateCapsule(sectors, std::max(4, sectors / 3));
        }
        else
        {
            return;
        }

        if (!newVerts.empty())
        {
            ApplyEntityMeshDirect(app, entity, std::move(newVerts), "SetRadialSegments");
        }
    }

    bool SupportsRadialResolution(Entity& entity)
    {
        // Esse comando recria a primitiva base; ele não deve sobrescrever uma
        // malha importada nem um mesh que já foi editado/gerado localmente.
        const auto& component = entity.GetMeshComponent();
        if (!component.MeshPath.empty())
            return false;
        std::string primitive = component.PrimitiveType;
        std::transform(primitive.begin(), primitive.end(), primitive.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return primitive == "sphere" || primitive == "cylinder" || primitive == "cone" ||
               primitive == "torus" || primitive == "capsule";
    }

    void Draw2DTools(Application& app, Entity& entity)
    {
        static int pixelGrid = 1;
        static bool pixelSnap = true;
        static float palette[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.55f, 0.82f, 0.90f));
        const bool open = ImGui::CollapsingHeader(UI("[2D] Ferramentas de desenho e animação##2DTools"),
                                                   ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        if (!open)
            return;

        ImGui::TextUnformatted(UI("Canvas 2D"));
        ImGui::TextDisabled("Edite sprites e formas no plano XY, sem iluminação ou componentes 3D.");
        ImGui::Checkbox(UI("Pixel snap##2DTools"), &pixelSnap);
        const char* grids[] = { "1 px", "2 px", "4 px", "8 px", "16 px" };
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo(UI("Grade##2DTools"), &pixelGrid, grids, 5);
        const float gridSize = static_cast<float>(1 << pixelGrid);

        if (ImGui::Button(UI("Aplicar snap de pixel##2DTools"), ImVec2(-1.0f, 0.0f)))
        {
            Transform& transform = entity.GetTransform();
            transform.Position.X = std::round(transform.Position.X / gridSize) * gridSize;
            transform.Position.Y = std::round(transform.Position.Y / gridSize) * gridSize;
            transform.Position.Z = 0.0f;
            transform.LocalPosition = transform.Position;
            transform.EulerRotation.X = 0.0f;
            transform.EulerRotation.Y = 0.0f;
            transform.Rotation = glm::quat(glm::radians(glm::vec3(
                0.0f, 0.0f, transform.EulerRotation.Z)));
            transform.LocalRotation = transform.Rotation;
            app.MarkDirty();
        }

        if (pixelSnap)
            ImGui::TextDisabled("Posição travada no grid de %g px", gridSize);

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Paleta Pixel Art"));
        if (ImGui::ColorEdit4("Cor do sprite##2DTools", palette,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
        {
            ColorComponent& color = entity.GetColorComponent();
            color.Color.X = palette[0];
            color.Color.Y = palette[1];
            color.Color.Z = palette[2];
            app.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Azul##2DPalette")))
        {
            palette[0] = 0.12f; palette[1] = 0.42f; palette[2] = 1.0f;
            entity.GetColorComponent().Color = Vector3(palette[0], palette[1], palette[2]);
            app.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Branco##2DPalette")))
        {
            palette[0] = palette[1] = palette[2] = 1.0f;
            entity.GetColorComponent().Color = Vector3(1.0f, 1.0f, 1.0f);
            app.MarkDirty();
        }
        ImGui::TextDisabled("Dica: use múltiplos de 2/4/8 px para preservar bordas nítidas.");

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Mesh 2D"));
        if (entity.GetMeshComponent().MeshData && entity.GetMeshComponent().MeshData->IsValid())
        {
            ImGui::TextDisabled("%u vértices triangulados", entity.GetMeshComponent().MeshData->GetVertexCount());
            if (ImGui::Button(UI("Abrir editor de mesh##2DTools"), ImVec2(-1.0f, 0.0f)))
                InspectorPanel::SetModelingMode(true);
            ImGui::TextWrapped(UI("No editor: selecione vértices/arestas/faces e use Espelhar, Mesclar,"));
            ImGui::TextWrapped(UI("Conectar/Bridge, Knife, Fill, Extrude e o gizmo para deformar."));
        }
        else
        {
            ImGui::TextDisabled("Crie um Sprite / Quad 2D ou importe um arquivo .mesh2d.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Animação 2D"));
        AnimationComponent& animation = entity.GetAnimationComponent();
        if (!animation.Clip || animation.Clip->Name.empty())
        {
            if (ImGui::Button(UI("Criar clipe de sprite##2DTools"), ImVec2(-1.0f, 0.0f)))
            {
                const Transform& transform = entity.GetTransform();
                animation.SetClip(AnimationClip::CreateDefault(
                    entity.GetName() + "_2D", 1.0f,
                    glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z),
                    transform.Rotation,
                    glm::vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z)));
                app.MarkDirty();
            }
        }
        else
        {
            ImGui::Text(UI("Clipe: %s (%.2fs)"), animation.Clip->Name.c_str(), animation.Clip->Duration);
            if (ImGui::Button(animation.Playing ? "Pausar##2DTools" : "Reproduzir##2DTools"))
                animation.Playing ? animation.Pause() : animation.Play();
            ImGui::SameLine();
            if (ImGui::Button(UI("Parar##2DTools")))
                animation.Stop();
            ImGui::SameLine();
            if (ImGui::Checkbox(UI("Ao iniciar##2DTools"), &animation.PlayOnStart))
                app.MarkDirty();
            ImGui::TextDisabled("Mova, gire ou escale no tempo desejado: a timeline cria a keyframe automaticamente.");
        }
    }
}

bool InspectorPanel::IsModelingMode()
{
    return s_ModelingMode;
}

int InspectorPanel::RunBatchEditRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* label)
    {
        std::cout << (condition ? "[InspectorBatchTest] PASS: " : "[InspectorBatchTest] FAIL: ")
                  << label << std::endl;
        if (!condition) ++failures;
    };

    Entity active;
    Entity selected;
    Entity notSelected;
    InspectorBatchContext context;
    context.Active = &active;
    context.Targets = { &selected };
    InspectorBatchContext* previous = g_InspectorBatchContext;
    g_InspectorBatchContext = &context;

    const auto checkFloat = [&](float* source, float* target, float* untouched,
                                const char* label)
    {
        constexpr float editedValue = 0.731f;
        *source = editedValue;
        *target = 0.217f;
        *untouched = 0.413f;
        SyncInspectorWidgetEdit(source, sizeof(float));
        check(std::abs(*target - editedValue) < 0.0001f &&
              std::abs(*untouched - 0.413f) < 0.0001f, label);
    };
    const auto checkBool = [&](bool* source, bool* target, bool* untouched,
                               const char* label)
    {
        *source = true;
        *target = false;
        *untouched = false;
        const std::string mixedLabel = std::string(label) + " mostra valor misto";
        check(InspectorWidgetHasMixedValue(source, sizeof(bool)), mixedLabel.c_str());
        SyncInspectorWidgetEdit(source, sizeof(bool));
        const std::string appliedLabel = std::string(label) + " aplica a todos selecionados";
        check(*target && !*untouched &&
              !InspectorWidgetHasMixedValue(source, sizeof(bool)), appliedLabel.c_str());
    };

    checkBool(&active.IsStatic, &selected.IsStatic, &notSelected.IsStatic, "Estático");
    checkFloat(&active.Mass, &selected.Mass, &notSelected.Mass,
               "campo de física do objeto copia apenas para a seleção");
    checkFloat(&active.GetColorComponent().Color.X,
               &selected.GetColorComponent().Color.X,
               &notSelected.GetColorComponent().Color.X,
               "cor do mesh sincroniza por componente");
    checkFloat(&active.GetMaterialComponent().MaterialData.Roughness,
               &selected.GetMaterialComponent().MaterialData.Roughness,
               &notSelected.GetMaterialComponent().MaterialData.Roughness,
               "material sincroniza apenas o campo editado");
    check(std::abs(selected.GetMaterialComponent().MaterialData.EmissionStrength -
                   active.GetMaterialComponent().MaterialData.EmissionStrength) < 0.0001f,
          "outros campos do material preservam seus valores");

    active.AddLight(); selected.AddLight(); notSelected.AddLight();
    checkBool(&active.GetLightComponent().Enabled, &selected.GetLightComponent().Enabled,
              &notSelected.GetLightComponent().Enabled, "Light ativada");
    checkFloat(&active.GetLightComponent().Intensity,
               &selected.GetLightComponent().Intensity,
               &notSelected.GetLightComponent().Intensity,
               "luz sincroniza intensidade sem alterar objeto fora da seleção");
    active.AddLocalLight(); selected.AddLocalLight(); notSelected.AddLocalLight();
    checkBool(&active.GetLocalLightComponent().ReceiveShadows,
              &selected.GetLocalLightComponent().ReceiveShadows,
              &notSelected.GetLocalLightComponent().ReceiveShadows,
              "Local Light receber sombras");
    checkFloat(&active.GetLocalLightComponent().Roughness,
               &selected.GetLocalLightComponent().Roughness,
               &notSelected.GetLocalLightComponent().Roughness,
               "LocalLight sincroniza o campo editado");
    active.AddAudio(); selected.AddAudio(); notSelected.AddAudio();
    checkBool(&active.GetAudioComponent().Loop, &selected.GetAudioComponent().Loop,
              &notSelected.GetAudioComponent().Loop, "Áudio em loop");
    checkFloat(&active.GetAudioComponent().Volume,
               &selected.GetAudioComponent().Volume,
               &notSelected.GetAudioComponent().Volume,
               "áudio sincroniza volume por componente");
    active.SetGlobalLight(true); selected.SetGlobalLight(true); notSelected.SetGlobalLight(true);
    checkBool(&active.GetGlobalLightComponent().FilmGrainEnabled,
              &selected.GetGlobalLightComponent().FilmGrainEnabled,
              &notSelected.GetGlobalLightComponent().FilmGrainEnabled,
              "Global Light film grain");
    checkFloat(&active.GetTransform().Position.X,
               &selected.GetTransform().Position.X,
               &notSelected.GetTransform().Position.X,
               "transform sincroniza posição sem alterar objeto fora da seleção");
    checkFloat(&active.GetGlobalLightComponent().Lighting.AmbientIntensity,
               &selected.GetGlobalLightComponent().Lighting.AmbientIntensity,
               &notSelected.GetGlobalLightComponent().Lighting.AmbientIntensity,
               "Global Light sincroniza parâmetros comuns");
    active.AddPostProcessingVolume(); selected.AddPostProcessingVolume(); notSelected.AddPostProcessingVolume();
    checkBool(&active.GetPostProcessingVolume().IsGlobal,
              &selected.GetPostProcessingVolume().IsGlobal,
              &notSelected.GetPostProcessingVolume().IsGlobal,
              "Volume de pós-processamento global");
    checkFloat(&active.GetPostProcessingVolume().BlendWeight,
               &selected.GetPostProcessingVolume().BlendWeight,
               &notSelected.GetPostProcessingVolume().BlendWeight,
               "volume de pós-processamento sincroniza parâmetros");
    active.AddCamera(); selected.AddCamera(); notSelected.AddCamera();
    checkBool(&active.GetCameraComponent().Orthographic, &selected.GetCameraComponent().Orthographic,
              &notSelected.GetCameraComponent().Orthographic, "Câmera ortográfica");
    checkFloat(&active.GetCameraComponent().FieldOfView,
               &selected.GetCameraComponent().FieldOfView,
               &notSelected.GetCameraComponent().FieldOfView,
               "câmera sincroniza campo de visão");
    active.AddTerrain(3); selected.AddTerrain(3); notSelected.AddTerrain(3);
    checkFloat(&active.GetTerrainComponent().HeightScale,
               &selected.GetTerrainComponent().HeightScale,
               &notSelected.GetTerrainComponent().HeightScale,
               "terreno sincroniza parâmetros");
    active.AddUI(UIElementType::Frame); selected.AddUI(UIElementType::Frame); notSelected.AddUI(UIElementType::Frame);
    checkBool(&active.GetUIComponent().ClipsContent, &selected.GetUIComponent().ClipsContent,
              &notSelected.GetUIComponent().ClipsContent, "UI recortar conteúdo");
    checkFloat(&active.GetUIComponent().FontSize,
               &selected.GetUIComponent().FontSize,
               &notSelected.GetUIComponent().FontSize,
               "UI 2D sincroniza o campo editado");
    active.AddParticleSystem(); selected.AddParticleSystem(); notSelected.AddParticleSystem();
    checkBool(&active.GetParticleSystem().Lit, &selected.GetParticleSystem().Lit,
              &notSelected.GetParticleSystem().Lit, "Partículas iluminadas");
    checkFloat(&active.GetParticleSystem().EmissionRate,
               &selected.GetParticleSystem().EmissionRate,
               &notSelected.GetParticleSystem().EmissionRate,
               "partículas sincronizam taxa de emissão");
    active.AddAI(); selected.AddAI(); notSelected.AddAI();
    checkBool(&active.GetAIComponent().SightEnabled, &selected.GetAIComponent().SightEnabled,
              &notSelected.GetAIComponent().SightEnabled, "IA visão ativa");
    checkFloat(&active.GetAIComponent().SightRadius,
               &selected.GetAIComponent().SightRadius,
               &notSelected.GetAIComponent().SightRadius,
               "IA sincroniza alcance de visão");
    checkBool(&active.GetScriptComponent().Enabled,
              &selected.GetScriptComponent().Enabled,
              &notSelected.GetScriptComponent().Enabled,
              "Script C# sincroniza estado sem alterar objeto fora da seleção");
    checkBool(&active.GetAnimationComponent().PlayOnStart,
              &selected.GetAnimationComponent().PlayOnStart,
              &notSelected.GetAnimationComponent().PlayOnStart,
              "animação sincroniza flag booleana");

    bool lockFromFree = false;
    {
        UniformScaleButtonStyle style(lockFromFree);
        lockFromFree = true; // simula clicar em Escala livre -> proporcional
    }
    check(lockFromFree, "estilo do botão desempilha corretamente ao ativar proporção");
    bool freeFromLock = true;
    {
        UniformScaleButtonStyle style(freeFromLock);
        freeFromLock = false; // simula clicar em proporcional -> Escala livre
    }
    check(!freeFromLock, "estilo do botão desempilha corretamente ao liberar proporção");

    Entity withoutLight;
    context.Targets = { &withoutLight };
    SyncInspectorWidgetEdit(&active.GetLightComponent().Intensity, sizeof(float));
    check(!withoutLight.HasLight(), "componentes opcionais ausentes não são criados ao editar em lote");

    g_InspectorBatchContext = previous;
    std::cout << "[InspectorBatchTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures;
}

void InspectorPanel::SetModelingMode(bool enabled)
{
    s_ModelingMode = enabled;
    if (!enabled)
    {
        // O pincel só faz sentido sobre o canvas do modo Modelador. Ao voltar
        // ao Inspector, encerra também qualquer traço ou régua pendente.
        MeshPainter::SetBrushActive(false);
        s_ModelingSelection.clear();
        s_ModelingEntityID = 0;
    }
}

int InspectorPanel::GetModelingSelectionMode()
{
    return s_ModelingSelectionMode;
}

const std::vector<int>& InspectorPanel::GetModelingSelection()
{
    return s_ModelingSelection;
}

uint64_t InspectorPanel::GetModelingEntityID()
{
    return s_ModelingEntityID;
}

void InspectorPanel::SetModelingSelection(
    uint64_t entityID,
    int mode,
    const std::vector<int>& components)
{
    s_ModelingEntityID = entityID;
    s_ModelingSelectionMode = std::clamp(mode, 0, 2);
    s_ModelingSelection = components;
}

void InspectorPanel::SetModelingSelectionMode(int mode)
{
    s_ModelingSelectionMode = std::clamp(mode, 0, 2);
    s_ModelingSelection.clear();
}

void InspectorPanel::ClearModelingSelection()
{
    s_ModelingSelection.clear();
}

InspectorPanel::MiniFaceShape InspectorPanel::GetMiniFaceShape()
{
    return s_MiniFaceShape;
}

void InspectorPanel::SetMiniFaceShape(InspectorPanel::MiniFaceShape shape)
{
    s_MiniFaceShape = shape;
}

InspectorPanel::MiniFaceOrientation InspectorPanel::GetMiniFaceOrientation()
{
    return s_MiniFaceOrientation;
}

void InspectorPanel::SetMiniFaceOrientation(InspectorPanel::MiniFaceOrientation orientation)
{
    s_MiniFaceOrientation = orientation;
}

int InspectorPanel::GetMiniFaceVerticalLines()
{
    return s_MiniFaceVerticalLines;
}

void InspectorPanel::SetMiniFaceVerticalLines(int lines)
{
    s_MiniFaceVerticalLines = std::clamp(lines, 0, 32);
}

int InspectorPanel::GetMiniFaceHorizontalLines()
{
    return s_MiniFaceHorizontalLines;
}

void InspectorPanel::SetMiniFaceHorizontalLines(int lines)
{
    s_MiniFaceHorizontalLines = std::clamp(lines, 0, 32);
}

bool InspectorPanel::IsHideDiagonalLines()
{
    return s_HideDiagonalLines;
}

void InspectorPanel::SetHideDiagonalLines(bool hide)
{
    s_HideDiagonalLines = hide;
}

void InspectorPanel::SubdivideSelectedIntoMiniFaces(Application& app, Entity& entity)
{
    auto& comp = entity.GetMeshComponent();
    if (!comp.MeshData || !comp.MeshData->IsValid()) return;
    const auto verts = comp.MeshData->GetVertices();
    if (verts.empty()) return;
    ModelingOperations::MiniFaceShape shape = static_cast<ModelingOperations::MiniFaceShape>(s_MiniFaceShape);
    ModelingOperations::MiniFaceOrientation orient = static_cast<ModelingOperations::MiniFaceOrientation>(s_MiniFaceOrientation);
    std::vector<Vertex> result = ModelingOperations::SubdivideMiniFaces(
        verts, s_ModelingSelection, shape, orient, s_MiniFaceVerticalLines, s_MiniFaceHorizontalLines);
    if (!result.empty())
    {
        ApplyEntityMeshDirect(app, entity, std::move(result), "Subdividir Mini-Faces");
    }
}

void InspectorPanel::SubdivideObjectIntoMiniFaces(Application& app, Entity& entity)
{
    auto& comp = entity.GetMeshComponent();
    if (!comp.MeshData || !comp.MeshData->IsValid()) return;
    const auto verts = comp.MeshData->GetVertices();
    if (verts.empty()) return;
    ModelingOperations::MiniFaceShape shape = static_cast<ModelingOperations::MiniFaceShape>(s_MiniFaceShape);
    ModelingOperations::MiniFaceOrientation orient = static_cast<ModelingOperations::MiniFaceOrientation>(s_MiniFaceOrientation);
    std::vector<int> allFaces;
    std::vector<Vertex> result = ModelingOperations::SubdivideMiniFaces(
        verts, allFaces, shape, orient, s_MiniFaceVerticalLines, s_MiniFaceHorizontalLines);
    if (!result.empty())
    {
        ApplyEntityMeshDirect(app, entity, std::move(result), "Subdividir Objeto em Mini-Faces");
    }
}

void InspectorPanel::SelectAllModeling(Application& app)
{
    if (!s_ModelingMode || s_ModelingEntityID == 0) return;
    Entity* entity = app.GetScene().FindEntityByID(s_ModelingEntityID);
    if (!entity || !entity->GetMeshComponent().MeshData) return;
    const auto vertices = entity->GetMeshComponent().MeshData->GetVertices();
    if (vertices.empty()) return;
    const MeshTopology topology = MeshTopology::FromVertices(vertices);
    s_ModelingSelection.clear();
    if (s_ModelingSelectionMode == 0)
    {
        for (size_t i = 0; i < topology.Vertices.size(); ++i)
            s_ModelingSelection.push_back(static_cast<int>(i));
    }
    else if (s_ModelingSelectionMode == 1)
    {
        const auto edges = topology.GetEdges();
        for (size_t i = 0; i < edges.size(); ++i)
            s_ModelingSelection.push_back(static_cast<int>(i));
    }
    else
    {
        for (size_t i = 0; i < topology.Faces.size(); ++i)
            s_ModelingSelection.push_back(static_cast<int>(i));
    }
}

void InspectorPanel::ApplyTopologyAction(Application& app, const std::string& actionName)
{
    if (!s_ModelingMode || s_ModelingEntityID == 0) return;
    Entity* entity = app.GetScene().FindEntityByID(s_ModelingEntityID);
    if (!entity || !entity->GetMeshComponent().MeshData || !entity->GetMeshComponent().MeshData->IsValid()) return;

    auto& component = entity->GetMeshComponent();
    std::vector<int> nextSelection;
    auto executeMesh = [&](std::vector<Vertex> vertices, const char* tag)
    {
        if (vertices.empty() || vertices.size() > 1000000) return;
        const std::vector<Vertex> beforeVertices = component.MeshData->GetVertices();
        const Transform beforeTransform = entity->GetTransform();
        Mesh* editableMesh = EnsureEditableMesh(app, *entity);
        if (!editableMesh) return;
        editableMesh->UpdateVertices(vertices, tag);
        SaveGeneratedMeshAsset(app, *entity, *editableMesh);

        TransformCommand command;
        command.Type = CommandType::Transform;
        command.EntityID = entity->GetID();
        command.EntityIndex = app.GetSelectedEntityIndex();
        command.Before = beforeTransform;
        command.After = entity->GetTransform();
        command.HasMeshData = true;
        command.MeshBefore = beforeVertices;
        command.MeshAfter = std::move(vertices);
        app.GetUndoRedoManager().Push(command);
        if (nextSelection.empty())
            s_ModelingSelection.clear();
        else
            s_ModelingSelection = nextSelection;
        app.MarkDirty();
    };

    auto executeTopology = [&](auto operation, const char* tag)
    {
        nextSelection.clear();
        MeshTopology mesh = MeshTopology::FromVertices(component.MeshData->GetVertices());
        ModelingSelection selection;
        selection.Mode = static_cast<ModelingSelectionMode>(s_ModelingSelectionMode);
        selection.Indices = s_ModelingSelection;
        if constexpr (std::is_void_v<decltype(operation(mesh, selection))>)
        {
            operation(mesh, selection);
        }
        else
        {
            nextSelection = operation(mesh, selection);
        }
        if (!mesh.IsValid()) return;
        executeMesh(mesh.ToVertices(false), tag);
    };

    if (actionName == "Extrude")
    {
        executeTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
        { return ModelingToolset::ExtrudeSelectedFaces(mesh, selection, s_ModelingAmount); }, "Modeling::Extrude");
    }
    else if (actionName == "Inset")
    {
        executeTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::InsetSelectedFaces(mesh, selection, s_ModelingAmount); }, "Modeling::Inset");
    }
    else if (actionName == "Bevel")
    {
        executeTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::BevelSelected(mesh, selection, s_ModelingAmount); }, "Modeling::Bevel");
    }
    else if (actionName == "LoopCut")
    {
        executeTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::LoopCut(mesh, selection); }, "Modeling::LoopCut");
    }
    else if (actionName == "Knife")
    {
        executeTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::Knife(mesh, selection); }, "Modeling::Knife");
    }
    else if (actionName == "Merge")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::MergeSelected(mesh, selection); }, "Modeling::Merge");
    }
    else if (actionName == "Dissolve")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::DissolveSelected(mesh, selection); }, "Modeling::Dissolve");
    }
    else if (actionName == "Fill")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::FillSelected(mesh, selection); }, "Modeling::Fill");
    }
    else if (actionName == "Delete")
    {
        if (s_ModelingSelectionMode == 2 && !s_ModelingSelection.empty())
        {
            std::vector<Vertex> vertices = ModelingOperations::DeleteFacesSelected(
                component.MeshData->GetVertices(), s_ModelingSelection);
            executeMesh(std::move(vertices), "Modeling::DeleteFaces");
        }
    }
    else if (actionName == "Flatten")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::FlattenSelected(mesh, selection, -1); }, "Modeling::Flatten");
    }
    else if (actionName == "Spherize")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::SpherizeSelected(mesh, selection, 1.0f); }, "Modeling::Spherize");
    }
    else if (actionName == "Poke")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::PokeSelectedFaces(mesh, selection, 0.0f); }, "Modeling::Poke");
    }
    else if (actionName == "Weld")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection&)
        { ModelingToolset::WeldByDistance(mesh, 0.02f); }, "Modeling::Weld");
    }
    else if (actionName == "Twist")
    {
        executeTopology([](MeshTopology& mesh, const ModelingSelection& selection)
        { ModelingToolset::TwistSelected(mesh, selection, 1, 45.0f); }, "Modeling::Twist");
    }
    else if (actionName == "Subdivide")
    {
        if (!s_ModelingSelection.empty() && s_ModelingSelectionMode == 2)
            executeMesh(ModelingOperations::SubdivideSelected(component.MeshData->GetVertices(), s_ModelingSelection), "Modeling::Subdivide");
        else
            executeMesh(ModelingOperations::Subdivide(component.MeshData->GetVertices()), "Modeling::Subdivide");
    }
    else if (actionName == "Optimize")
    {
        std::vector<Vertex> verts = component.MeshData->GetVertices();
        ModelingOperations::Optimize(verts);
        executeMesh(std::move(verts), "Modeling::Optimize");
    }
    else if (actionName == "RemoveInternalFaces")
    {
        std::vector<Vertex> verts = component.MeshData->GetVertices();
        ModelingOperations::RemoveInternalFaces(verts);
        executeMesh(std::move(verts), "Modeling::RemoveInternalFaces");
    }
}

void InspectorPanel::DrawScriptInspector(
    Application& app,
    Entity& entity
)
{
    ScriptComponent& script = entity.GetScriptComponent();
    if (ImGui::Checkbox((std::string(L("common.enabled")) + "##Script").c_str(), &script.Enabled))
        app.MarkDirty();

    if (script.ScriptPath.empty())
    {
        ImGui::Button(UI("Arraste um script .cs de Assets para cá##ScriptDrop"), ImVec2(-1.0f, 38.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath && *droppedPath)
                {
                    const std::filesystem::path assetPath(droppedPath);
                    std::string extension = assetPath.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".cs")
                    {
                        script.ScriptPath = assetPath.lexically_normal().generic_string();
                        // The managed compiler resolves the ScriptBehaviour declared in
                        // this source file, so the class does not need the filename as its name.
                        script.ScriptAssemblyQualifiedTypeName.clear();
                        script.Enabled = true;
                        script.Dirty = true;
                        ForEachInspectorBatchComponent<ScriptComponent>([&](ScriptComponent& target)
                        {
                            target.ScriptPath = script.ScriptPath;
                            target.ScriptAssemblyQualifiedTypeName.clear();
                            target.Enabled = true;
                            target.Dirty = true;
                        });
                        app.MarkDirty();
                        if (app.GetEditorState() == Application::EditorState::Play)
                            ScriptingRuntime::Instance().ReloadAll();
                    }
                    else
                    {
                        ImGui::SetTooltip(UI("O componente Script C# aceita somente arquivos .cs."));
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextWrapped(UI("%s"), L("inspector.no_script"));
        ImGui::TextDisabled("A classe deve herdar de ScriptBehaviour. A Marae identifica o script pelo arquivo.");
        if (ImGui::Button(L("inspector.create_script")))
        {
            ImGui::OpenPopup("Create Script Options");
        }

        if (ImGui::BeginPopupModal(L("inspector.script_options"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(UI("%s %s"), L("inspector.new_script_for"), entity.GetName().c_str());
            ImGui::TextWrapped(UI("A Marae cria e vincula o arquivo e a classe automaticamente."));
            ImGui::Separator();
            ImGui::TextWrapped(UI("%s"), L("inspector.choose_script_editor"));
            ImGui::Spacing();
            if (ImGui::Button(L("component.open_vscode")))
            {
                app.CreateScriptForSelectedEntity();
                app.OpenSelectedScriptInVSCode();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("inspector.internal_editor")))
            {
                app.CreateScriptForSelectedEntity();
                std::string p = entity.GetScriptComponent().ScriptPath;
                if (!p.empty())
                    app.OpenScriptInInternalEditor(p);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("common.cancel")))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    else
    {
        ImGui::Text(UI("%s: %s"), L("inspector.script_label"), script.ScriptPath.c_str());
        if (ImGui::Button(L("component.open_vscode")))
        {
            app.OpenSelectedScriptInVSCode();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("inspector.internal_editor")))
        {
            app.OpenScriptInInternalEditor(script.ScriptPath);
        }
        ImGui::SameLine();
        if (ImGui::Button(L("common.remove")))
        {
            script.ScriptPath.clear();
            script.ScriptAssemblyQualifiedTypeName.clear();
            script.SerializedFields.clear();
            app.MarkDirty();
        }

        if (app.GetEditorState() == Application::EditorState::Play)
        {
            std::string fieldsJson = ScriptingRuntime::Instance().GetPublicFieldsJson(entity.GetID());
            if (!fieldsJson.empty() && fieldsJson != "{}")
            {
                try
                {
                    auto j = nlohmann::json::parse(fieldsJson);
                    if (j.is_object() && !j.empty())
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Variaveis C# (Tempo Real)");
                        for (auto& [key, val] : j.items())
                        {
                            if (val.is_number_float())
                            {
                                float fVal = val.get<float>();
                                if (ImGui::DragFloat(key.c_str(), &fVal, 1.0f))
                                {
                                    ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, std::to_string(fVal));
                                }
                            }
                            else if (val.is_number_integer())
                            {
                                int iVal = val.get<int>();
                                if (ImGui::DragInt(key.c_str(), &iVal, 1))
                                {
                                    ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, std::to_string(iVal));
                                }
                            }
                            else if (val.is_boolean())
                            {
                                bool bVal = val.get<bool>();
                                if (ImGui::Checkbox(key.c_str(), &bVal))
                                {
                                    ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, bVal ? "true" : "false");
                                }
                            }
                            else if (val.is_string())
                            {
                                std::string sVal = val.get<std::string>();
                                char buf[256];
                                strncpy_s(buf, sVal.c_str(), sizeof(buf));
                                if (ImGui::InputText(key.c_str(), buf, sizeof(buf)))
                                {
                                    ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, "\"" + std::string(buf) + "\"");
                                }
                            }
                        }
                    }
                }
                catch (...) {}
            }
        }
    }
}

void InspectorPanel::DrawLightInspector(
    Application& app,
    Entity& entity
)
{
    if (!entity.HasLight())
    {
        ImGui::TextWrapped(UI("%s"), L("inspector.no_light"));
        if (ImGui::Button(L("component.add_point")))
        {
            entity.AddLight(LightType::Point);
            app.MarkDirty();
        }
        return;
    }

    LightComponent& light = entity.GetLightComponent();
    if (ImGui::Checkbox((std::string(L("common.enabled")) + "##Light").c_str(), &light.Enabled)) app.MarkDirty();
    if (ImGui::Checkbox(UI("Projetar sombras##Light"), &light.CastShadows)) app.MarkDirty();

    const char* lightTypes[] = { "Point", "Spot", "Directional", "Area" };
    int currentType = static_cast<int>(light.Type);
    if (ImGui::Combo(L("inspector.light_type"), &currentType, lightTypes, 4))
    {
        currentType = std::clamp(currentType, 0, 3);
        light.Type = static_cast<LightType>(currentType);
        ForEachInspectorBatchComponent<LightComponent>([&](LightComponent& target) { target.Type = light.Type; });
        app.MarkDirty();
    }

    float col[3] = { light.Color[0], light.Color[1], light.Color[2] };
    if (ImGui::ColorEdit3((std::string(L("inspector.color")) + "##Light").c_str(), col))
    {
        light.Color[0] = col[0];
        light.Color[1] = col[1];
        light.Color[2] = col[2];
        ForEachInspectorBatchComponent<LightComponent>([&](LightComponent& target)
        {
            std::copy(std::begin(col), std::end(col), target.Color);
        });
        app.MarkDirty();
    }

    if (ImGui::DragFloat((std::string(L("inspector.intensity")) + "##Light").c_str(), &light.Intensity, 0.1f, 0.0f, 100.0f, "%.2f"))
    {
        app.MarkDirty();
    }

    if (light.Type != LightType::Directional &&
        ImGui::DragFloat((std::string(L("inspector.range")) + "##Light").c_str(), &light.Range, 0.5f, 0.1f, 10000.0f, "%.2f"))
    {
        light.Range = std::clamp(light.Range, 0.1f, 10000.0f);
        app.MarkDirty();
    }

    if (ImGui::Checkbox(UI("Usar temperatura de cor##Light"), &light.UseTemperature))
        app.MarkDirty();
    if (light.UseTemperature && ImGui::DragFloat(UI("Temperatura (Kelvin)##Light"),
                                                  &light.TemperatureKelvin, 50.0f,
                                                  1000.0f, 40000.0f, "%.0f K"))
        app.MarkDirty();

    if (light.Type == LightType::Directional)
    {
        if (ImGui::DragFloat3("Direção local##Light", light.DirectionLocal,
                              0.01f, -1.0f, 1.0f, "%.3f"))
            app.MarkDirty();
        if (ImGui::Checkbox(UI("Desfoque por distância##Light"), &light.DistanceSoftening))
            app.MarkDirty();
    }
    else
    {
        if (ImGui::DragFloat(UI("Raio da fonte##Light"), &light.SourceRadius,
                             0.01f, 0.025f, 5.0f, "%.3f m"))
            app.MarkDirty();
        if (ImGui::SliderFloat(UI("Suavidade da sombra##Light"), &light.ShadowSoftness,
                               0.0f, 1.0f, "%.2f"))
            app.MarkDirty();
        if (ImGui::SliderFloat(UI("Opacidade da sombra##Light"), &light.ShadowStrength,
                               0.0f, 1.0f, "%.2f"))
            app.MarkDirty();
        DrawHelpMarker("LightShadowStrength",
            "Controla quanto esta luz escurece as áreas bloqueadas; não altera as sombras do sol.");
        if (ImGui::Checkbox(UI("Desfoque por distância##Light"), &light.DistanceSoftening))
            app.MarkDirty();
        if (ImGui::SliderFloat(UI("Força do contact shadow##Light"),
                               &light.ContactShadowStrength, 0.0f, 1.0f, "%.2f"))
            app.MarkDirty();
        if (ImGui::Checkbox(UI("Contribui na iluminação indireta##Light"),
                            &light.IndirectContribution))
            app.MarkDirty();
    }

    if (light.Type == LightType::Spot)
    {
        ImGui::Separator();
        if (ImGui::DragFloat3(UI("Direcao local##SpotLight"), light.DirectionLocal, 0.01f, -1.0f, 1.0f, "%.3f"))
        {
            glm::vec3 dir(light.DirectionLocal[0], light.DirectionLocal[1], light.DirectionLocal[2]);
            if (glm::length(dir) > 0.001f)
            {
                dir = glm::normalize(dir);
                light.DirectionLocal[0] = dir.x;
                light.DirectionLocal[1] = dir.y;
                light.DirectionLocal[2] = dir.z;
            }
            app.MarkDirty();
        }
        if (ImGui::SmallButton(UI("Apontar para Baixo")))
        {
            light.DirectionLocal[0] = 0.0f;
            light.DirectionLocal[1] = -1.0f;
            light.DirectionLocal[2] = 0.0f;
            app.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Apontar para Frente")))
        {
            light.DirectionLocal[0] = 0.0f;
            light.DirectionLocal[1] = 0.0f;
            light.DirectionLocal[2] = 1.0f;
            app.MarkDirty();
        }
        if (ImGui::DragFloat(L("inspector.inner_angle"), &light.InnerAngleDeg, 1.0f, 1.0f, 88.0f, "%.1f deg"))
        {
            light.InnerAngleDeg = std::clamp(light.InnerAngleDeg, 1.0f, 88.0f);
            light.OuterAngleDeg = std::clamp(
                std::max(light.OuterAngleDeg, light.InnerAngleDeg + 0.5f),
                light.InnerAngleDeg + 0.5f, 89.0f);
            app.MarkDirty();
        }
        if (ImGui::DragFloat(L("inspector.outer_angle"), &light.OuterAngleDeg, 1.0f, 1.5f, 89.0f, "%.1f deg"))
        {
            light.OuterAngleDeg = std::clamp(
                light.OuterAngleDeg, light.InnerAngleDeg + 0.5f, 89.0f);
            app.MarkDirty();
        }
    }

    ImGui::Separator();
    if (ImGui::Button(L("component.remove_light")))
    {
        entity.RemoveLight();
        app.MarkDirty();
    }
}

void InspectorPanel::DrawAudioInspector(
    Application& app,
    Entity& entity
)
{
    if (!entity.HasAudio())
    {
        ImGui::TextWrapped(UI("%s"), L("inspector.no_audio"));
        if (ImGui::Button(L("component.create_audio")))
        {
            entity.AddAudio();
            app.MarkDirty();
        }
        return;
    }

    AudioComponent& audio = entity.GetAudioComponent();
    if (audio.SourceID.empty())
        audio.SourceID = "audio_" + std::to_string(entity.GetID());
    static uint64_t sourceIDEntity = 0;
    static char sourceIDBuffer[128] = {};
    if (sourceIDEntity != entity.GetID())
    {
        std::snprintf(sourceIDBuffer, sizeof(sourceIDBuffer), "%s",
                      audio.SourceID.c_str());
        sourceIDEntity = entity.GetID();
    }
    ImGui::TextUnformatted(UI("ID do som"));
    DrawHelpMarker("AudioSourceID",
        "Identificador único desta fonte. Use-o em scripts para localizar ou controlar este áudio.");
    ImGui::PushItemWidth(-92.0f);
    ImGui::InputText(UI("##AudioSourceID"), sourceIDBuffer, sizeof(sourceIDBuffer),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(UI("Copiar##AudioSourceID")))
        ImGui::SetClipboardText(audio.SourceID.c_str());
    if (ImGui::Button(UI("Gerar novo ID##AudioSourceID")))
    {
        static uint32_t nextGeneratedID = 1;
        audio.SourceID = "audio_" + std::to_string(entity.GetID()) +
                         "_" + std::to_string(nextGeneratedID++);
        std::snprintf(sourceIDBuffer, sizeof(sourceIDBuffer), "%s",
                      audio.SourceID.c_str());
        app.MarkDirty();
    }
    ImGui::Separator();
    if (ImGui::Checkbox((std::string(L("common.enabled")) + "##Audio").c_str(), &audio.Enabled)) app.MarkDirty();
    DrawHelpMarker("AudioEnabled", "Ativa ou desativa a reprodução desta fonte de áudio.");
    if (ImGui::Checkbox((std::string(L("inspector.loop")) + "##Audio").c_str(), &audio.Loop)) app.MarkDirty();
    DrawHelpMarker("AudioLoop", "Reinicia o clipe automaticamente quando ele chega ao fim.");
    if (ImGui::Checkbox((std::string(L("inspector.play_start")) + "##Audio").c_str(), &audio.PlayOnStart)) app.MarkDirty();
    DrawHelpMarker("AudioPlayOnStart", "Inicia este clipe quando a cena ou o jogo começa.");

    static uint64_t pathEntityID = 0;
    static char pathBuffer[512] = {};
    if (pathEntityID != entity.GetID())
    {
        const size_t copyLength = std::min(audio.ClipPath.size(), sizeof(pathBuffer) - 1);
        std::memcpy(pathBuffer, audio.ClipPath.data(), copyLength);
        pathBuffer[copyLength] = '\0';
        pathEntityID = entity.GetID();
    }
    ImGui::TextUnformatted(UI("Arquivo de áudio"));
    DrawHelpMarker("AudioClip",
        "Arraste um WAV, MP3, OGG ou FLAC para a área abaixo, ou informe o caminho do arquivo.");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
    ImGui::BeginChild("##AudioDropZone", ImVec2(0.0f, 52.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool hasAudioFile = !audio.ClipPath.empty() || !audio.ClipID.empty();
    ImGui::TextColored(hasAudioFile ? ImVec4(0.70f, 0.85f, 0.72f, 1.0f)
                                    : ImVec4(0.60f, 0.65f, 0.72f, 1.0f),
                       hasAudioFile ? "Som configurado — arraste outro para substituir"
                                    : "Arraste um arquivo WAV, MP3, OGG ou FLAC aqui");
    ImGui::TextDisabled("ou informe o caminho abaixo");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const std::string droppedPath(static_cast<const char*>(payload->Data),
                                          payload->DataSize > 0 ? payload->DataSize - 1 : 0);
            const std::string extension = std::filesystem::path(droppedPath)
                                              .extension().string();
            const bool validAudio = extension == ".wav" || extension == ".WAV" ||
                                    extension == ".mp3" || extension == ".MP3" ||
                                    extension == ".ogg" || extension == ".OGG" ||
                                    extension == ".flac" || extension == ".FLAC";
            if (validAudio)
            {
                audio.ClipPath = droppedPath;
                audio.ClipID = AssetDatabase::GetGUIDForPath(droppedPath);
                if (audio.ClipID.empty())
                {
                    auto importedClip = AudioAssetLibrary::Get().FindByPath(droppedPath);
                    if (importedClip)
                        audio.ClipID = importedClip->GetID();
                }
                std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", droppedPath.c_str());
                pathEntityID = entity.GetID();
                app.MarkDirty();
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (ImGui::InputText(L("inspector.wav_file"), pathBuffer, sizeof(pathBuffer)))
    {
        audio.ClipPath = pathBuffer;
        audio.ClipID = AssetDatabase::GetGUIDForPath(audio.ClipPath);
        if (audio.ClipID.empty())
        {
            auto importedClip = AudioAssetLibrary::Get().FindByPath(audio.ClipPath);
            if (importedClip)
                audio.ClipID = importedClip->GetID();
        }
        app.MarkDirty();
    }

    if (ImGui::DragFloat((std::string(L("inspector.volume")) + "##Audio").c_str(), &audio.Volume, 0.01f, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
    DrawHelpMarker("AudioVolume", "Volume desta fonte, de silencioso (0) até o nível máximo (1).");
    if (ImGui::DragFloat((std::string(L("inspector.pitch")) + "##Audio").c_str(), &audio.Pitch, 0.01f, 0.25f, 4.0f, "%.2f")) app.MarkDirty();
    DrawHelpMarker("AudioPitch", "Altera a velocidade e o tom da reprodução; 1 mantém o valor original.");

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Espacialização 3D"));
    DrawHelpMarker("AudioSpatializationOverview",
        "Com Spatial Blend em 0 o som é 2D; em 1, volume e posição dependem da distância na cena.");
    if (ImGui::SliderFloat(UI("Spatial Blend##Audio"), &audio.SpatialBlend, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
    DrawHelpMarker("AudioSpatialBlend", "0 reproduz em 2D; 1 posiciona o som no espaço 3D da cena.");
    if (ImGui::DragFloat(UI("Distância mínima##Audio"), &audio.MinDistance, 0.1f, 0.01f, 10000.0f, "%.2f m")) app.MarkDirty();
    DrawHelpMarker("AudioMinDistance", "Distância de referência abaixo da qual o som mantém seu volume próximo do máximo.");
    if (ImGui::DragFloat(UI("Distância máxima##Audio"), &audio.MaxDistance, 0.5f, 0.02f, 10000.0f, "%.2f m")) app.MarkDirty();
    DrawHelpMarker("AudioMaxDistance", "Distância máxima considerada no decaimento; não significa necessariamente corte total do som.");

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Acústica avançada"));
    DrawHelpMarker("AudioAcousticsOverview",
        "A acústica atual é uma estimativa geométrica simplificada: verifica o segmento fonte-listener contra caixas alinhadas aos eixos, não simula ondas nem múltiplos raios.");
    ImGui::TextDisabled("Estimativa geométrica simplificada; configure também os objetos que bloqueiam o som.");
    if (ImGui::Checkbox(UI("Propagação dinâmica##AudioAcoustics"), &audio.AcousticPropagationEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioAcousticPropagation",
        "Ativa a avaliação acústica desta fonte. Para um resultado, configure objetos participantes e bloqueadores no Inspector deles.");
    if (ImGui::Checkbox(UI("Ray tracing acústico##AudioAcousticRayTracing"), &audio.AcousticRayTracingEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioAcousticRayTracing",
        "Testa o segmento reto entre fonte e listener contra os limites dos objetos; não é um traçado físico de vários raios.");
    if (ImGui::Checkbox(UI("Áudio geométrico##AudioGeometric"), &audio.GeometricAcousticsEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioGeometricAcoustics",
        "Usa a geometria da cena para estimar o efeito dos objetos sobre o som.");
    if (ImGui::Checkbox(UI("Oclusão##AudioOcclusion"), &audio.OcclusionEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioOcclusion", "Atenua o caminho direto quando uma superfície bloqueia a fonte.");
    if (ImGui::Checkbox(UI("Obstrução##AudioObstruction"), &audio.ObstructionEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioObstruction", "Aplica atenuação parcial para obstáculos que não bloqueiam totalmente.");
    if (ImGui::Checkbox(UI("Difração##AudioDiffraction"), &audio.DiffractionEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioDiffraction", "Estima a recuperação de som por caminhos próximos às bordas dos obstáculos.");
    if (ImGui::Checkbox(UI("Acoustic Pathfinding##AudioPathfinding"), &audio.AcousticPathfindingEnabled)) app.MarkDirty();
    DrawHelpMarker("AudioPathfinding", "Tenta encontrar uma rota acústica alternativa simplificada.");
    if (ImGui::DragFloat(UI("Transmissão da fonte##AudioTransmission"), &audio.AcousticTransmission,
                        0.01f, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
    DrawHelpMarker("AudioSourceTransmission", "Parâmetro salvo da fonte. A transmissão efetiva do obstáculo é ajustada no Inspector do objeto.");
    if (ImGui::DragFloat(UI("Intervalo de atualização##AudioAcousticInterval"), &audio.AcousticUpdateInterval,
                        0.01f, 0.01f, 1.0f, "%.2f s")) app.MarkDirty();
    DrawHelpMarker("AudioAcousticUpdateInterval", "Valor salvo nas configurações; o cálculo atual ainda não usa este intervalo para agendar atualizações.");

    // const char* audioBuses[] = { "Master", "Music", "SFX" };
    // int audioBus = static_cast<int>(audio.Bus);
    // if (ImGui::Combo(UI("Mixer Bus##Audio"), &audioBus, audioBuses, 3))
    // {
    //     audio.Bus = static_cast<AudioBus>(std::clamp(audioBus, 0, 2));
    //     app.MarkDirty();
    // }

    // ImGui::Separator();
    // ImGui::TextUnformatted(UI("Mixer global"));
    // AudioSystem& mixer = app.GetAudioSystem();
    // float masterVolume = mixer.GetMasterVolume();
    // if (ImGui::SliderFloat(UI("Master##Mixer"), &masterVolume, 0.0f, 1.0f, "%.2f"))
    //     mixer.SetMasterVolume(masterVolume);
    // float musicVolume = mixer.GetBusVolume(AudioBus::Music);
    // if (ImGui::SliderFloat(UI("Music##Mixer"), &musicVolume, 0.0f, 1.0f, "%.2f"))
    //     mixer.SetBusVolume(AudioBus::Music, musicVolume);
    // float sfxVolume = mixer.GetBusVolume(AudioBus::SFX);
    // if (ImGui::SliderFloat(UI("SFX##Mixer"), &sfxVolume, 0.0f, 1.0f, "%.2f"))
    //     mixer.SetBusVolume(AudioBus::SFX, sfxVolume);

    ImGui::Separator();
    DrawAudioMixerPanel(app);

    if (ImGui::Button(L("component.test_audio"))) app.PreviewAudioForSelectedEntity();
    ImGui::SameLine();
    if (ImGui::Button(L("component.stop_audio"))) app.StopAudioPreview();
    if (ImGui::Button(L("component.remove_audio")))
    {
        app.StopAudioPreview();
        entity.RemoveAudio();
        app.MarkDirty();
    }
}

void InspectorPanel::DrawLocalLightInspector(
    Application& app,
    Entity& entity
)
{
    if (!entity.HasLocalLight())
    {
        ImGui::TextWrapped(UI("%s"), L("inspector.no_local_light"));
        if (ImGui::Button(L("component.add_local_light")))
        {
            entity.AddLocalLight();
            app.MarkDirty();
        }
        return;
    }

    LocalLightComponent& local = entity.GetLocalLightComponent();
    bool changed = false;

    if (ImGui::Checkbox((std::string(L("common.enabled")) + "##LocalLight").c_str(), &local.Enabled))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.82f, 1.0f, 1.0f), "Local Light");
    ImGui::TextWrapped(UI("Configura como este objeto recebe luz. Similar ao Global Light, mas específico para este objeto."));

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Refletividade"));

    if (ImGui::SliderFloat(UI("Metálico##LocalLight"), &local.Metallic, 0.0f, 1.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    DrawHelpMarker("Metallic", "0 = dielétrico (madeira, plástico), 1 = metálico (ferro, ouro)");

    if (ImGui::SliderFloat(UI("Rugosidade##LocalLight"), &local.Roughness, 0.0f, 1.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    DrawHelpMarker("Roughness", "0 = espelho perfeito, 1 = completamente mate");

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Emissão"));

    if (ImGui::SliderFloat(UI("Intensidade##LocalLightEmission"), &local.Emission, 0.0f, 10.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::ColorEdit3(UI("Cor da emissão##LocalLight"), local.EmissionColor))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Influência de Luzes"));

    if (ImGui::SliderFloat(UI("Luz Solar##LocalLight"), &local.SunLightInfluence, 0.0f, 2.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Luz Artificial##LocalLight"), &local.ArtificialLightInfluence, 0.0f, 2.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Luz Ambiente##LocalLight"), &local.AmbientLightInfluence, 0.0f, 2.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Sombras"));

    if (ImGui::Checkbox(UI("Receber sombras##LocalLight"), &local.ReceiveShadows))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Força da sombra##LocalLight"), &local.ShadowStrength, 0.0f, 1.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Bias da sombra##LocalLight"), &local.ShadowBias, 0.0f, 0.1f, "%.4f"))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Reflexo"));

    if (ImGui::Checkbox(UI("Habilitar reflexos##LocalLight"), &local.EnableReflections))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Força do reflexo##LocalLight"), &local.ReflectionStrength, 0.0f, 1.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Oclusão Ambiente"));

    if (ImGui::Checkbox(UI("Habilitar AO##LocalLight"), &local.EnableAO))
    {
        changed = true;
        app.MarkDirty();
    }
    if (ImGui::SliderFloat(UI("Força do AO##LocalLight"), &local.AOStrength, 0.0f, 2.0f, "%.2f"))
    {
        changed = true;
        app.MarkDirty();
    }

    ImGui::Spacing();
    if (ImGui::Button(UI("Reset##LocalLight")))
    {
        LocalLightComponent temp;
        local = temp;
        changed = true;
        app.MarkDirty();
    }

    if (changed)
        app.MarkDirty();
}

void InspectorPanel::DrawPhysicsInspector(
    Application& app,
    Entity& entity
)
{
    if (app.Is2DWorldMode())
        ImGui::TextDisabled("Física 2D: colisões e gravidade no plano XY.");
    if (!app.Is2DWorldMode())
    {
        if (ImGui::Checkbox(UI("Projetar sombras##ObjectPhysics"), &entity.CastShadows))
            app.MarkDirty();
        DrawHelpMarker("ObjectCastShadows",
            "Controla se este objeto escurece outros objetos com sua sombra. Mesmo desligado, continua bloqueando a luz e pode receber sombras de outros objetos.");
    }
    if (ImGui::Checkbox(L("physics.enabled"), &entity.PhysicsEnabled))
        app.MarkDirty();
    if (ImGui::Checkbox(L("physics.use_gravity"), &entity.UseGravity))
        app.MarkDirty();
    if (ImGui::Checkbox(L("physics.is_static"), &entity.IsStatic))
        app.MarkDirty();

    if (ImGui::Checkbox(UI("Collider ativo##Physics"), &entity.ColliderEnabled))
        app.MarkDirty();
    if (ImGui::Checkbox(UI("Trigger (detecta sem empurrar)##Physics"), &entity.IsTrigger))
        app.MarkDirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(UI("Dispara OnTriggerEnter/Stay/Exit sem aplicar impulso nem correção de posição."));
    const char* colliderShapes3D[] = {
        L("physics.collider_box"), L("physics.collider_sphere"),
        L("physics.collider_capsule"), L("physics.collider_cylinder"),
        L("physics.collider_cone"), UI("Mesh Bounds (caixa limite)"),
        UI("Convex Mesh (envoltória convexa)")
    };
    const char* colliderShapes2D[] = {
        "Caixa 2D", "Círculo 2D"
    };
    const char** colliderShapes = app.Is2DWorldMode() ? colliderShapes2D : colliderShapes3D;
    const int colliderShapeCount = app.Is2DWorldMode() ? 2 : 7;
    int colliderShape = static_cast<int>(entity.Collider);
    if (ImGui::Combo(L("physics.collider"), &colliderShape, colliderShapes, colliderShapeCount))
    {
        entity.Collider = static_cast<Entity::ColliderShape>(
            std::clamp(colliderShape, 0, app.Is2DWorldMode() ? 1 : 6));
        ForEachInspectorBatchEntity([&](Entity& target) { target.Collider = entity.Collider; });
        app.MarkDirty();
    }
    if (!app.Is2DWorldMode() && entity.Collider == Entity::ColliderShape::Mesh)
        ImGui::TextWrapped(UI("Mesh Bounds usa a caixa alinhada à malha; use Convex Mesh para seguir uma forma convexa. Cavidades de meshes côncavos não são colisores físicos.") );
    else if (!app.Is2DWorldMode() && entity.Collider == Entity::ColliderShape::ConvexMesh)
        ImGui::TextWrapped(UI("A envoltória convexa acompanha meshes convexos. Cavidades e reentrâncias são preenchidas; para cenários use Mesh Bounds ou collider composto.") );

    if (!app.Is2DWorldMode())
    {
        if (ImGui::Checkbox(UI("Colisão contínua##Physics"), &entity.ContinuousCollision))
            app.MarkDirty();
        if (ImGui::Checkbox(UI("Congelar rotação##Physics"), &entity.FreezeRotation))
            app.MarkDirty();
    }

    if (ImGui::DragFloat(L("physics.mass"), &entity.Mass, 0.1f, 0.01f, 10000.0f, "%.2f kg"))
        app.MarkDirty();

    if (ImGui::SliderFloat(L("physics.restitution"), &entity.Restitution, 0.0f, 1.0f, "%.2f"))
        app.MarkDirty();

    if (ImGui::SliderFloat(L("physics.friction"), &entity.Friction, 0.0f, 1.0f, "%.2f"))
        app.MarkDirty();

    float vel[3] = { entity.Velocity.X, entity.Velocity.Y, entity.Velocity.Z };
    bool velocityChanged = app.Is2DWorldMode()
        ? ImGui::DragFloat2("Velocidade XY##Physics", vel, 0.1f, -100.0f, 100.0f, "%.2f")
        : ImGui::DragFloat3(L("physics.velocity"), vel, 0.1f, -100.0f, 100.0f, "%.2f");
    if (velocityChanged)
    {
        entity.Velocity.X = vel[0];
        entity.Velocity.Y = vel[1];
        entity.Velocity.Z = app.Is2DWorldMode() ? 0.0f : vel[2];
        app.MarkDirty();
    }

    if (app.Is2DWorldMode())
        return;

    float angularVelocity[3] = {
        entity.AngularVelocity.X, entity.AngularVelocity.Y, entity.AngularVelocity.Z
    };
    if (ImGui::DragFloat3("Velocidade angular##Physics", angularVelocity,
                          0.1f, -50.0f, 50.0f, "%.2f rad/s"))
    {
        entity.AngularVelocity = Vector3(
            angularVelocity[0], angularVelocity[1], angularVelocity[2]);
        app.MarkDirty();
    }
}

void InspectorPanel::DrawAnimationInspector(
    Application& app,
    Entity& entity
)
{
	AnimationComponent& anim = entity.GetAnimationComponent();
	bool changed = false;
	changed |= ImGui::Checkbox((std::string(L("common.enabled")) + "##Animation").c_str(), &anim.Enabled);
	changed |= ImGui::Checkbox(UI("Executar ao iniciar Play##AnimationAutoStart"), &anim.PlayOnStart);

	if (!anim.Clip || anim.Clip->Name.empty())
	{
		ImGui::TextWrapped(UI("Esta entidade ainda não possui uma animação."));
		if (ImGui::Button(UI("Criar animação padrão##AnimationCreate"), ImVec2(-1.0f, 0.0f)))
		{
			const Transform& transform = entity.GetTransform();
			const glm::vec3 position(transform.Position.X, transform.Position.Y, transform.Position.Z);
			const glm::vec3 scale(transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
			anim.SetClip(AnimationClip::CreateDefault(
				entity.GetName() + " Animation", 2.0f, position, transform.Rotation, scale));
			anim.Time = 0.0f;
			anim.Playing = false;
			changed = true;
		}
		if (changed)
			app.MarkDirty();
		return;
	}

	if (anim.ImportedAsset && anim.ImportedAsset->Clips.size() > 1)
	{
		const char* preview = anim.ImportedClipName.empty() ? anim.Clip->Name.c_str() : anim.ImportedClipName.c_str();
		if (ImGui::BeginCombo(UI("Clipe importado##ImportedAnimationClip"), preview))
		{
			for (const auto& importedClip : anim.ImportedAsset->Clips)
			{
				if (!importedClip) continue;
				const bool selected = anim.Clip == importedClip;
				if (ImGui::Selectable(importedClip->Name.c_str(), selected))
				{
					anim.Clip = importedClip;
					anim.ImportedClipName = importedClip->Name;
					anim.Time = 0.0f;
					AnimationSystem::ApplyPoseAtTime(entity, anim.Time, 0.0f);
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Text(UI("%s: %s"), L("inspector.clip"), anim.Clip->Name.c_str());
	ImGui::Text(UI("%s: %.2f s"), L("inspector.duration"), anim.Clip->Duration);
	ImGui::TextWrapped(UI("ID da animação: %s"), anim.Clip->ID.c_str());
	if (ImGui::SmallButton(UI("Copiar ID##AnimationID")))
		ImGui::SetClipboardText(anim.Clip->ID.c_str());
	changed |= ImGui::DragFloat((std::string(L("inspector.speed")) + "##AnimSpeed").c_str(), &anim.Speed, 0.1f, 0.0f, 10.0f, "%.2f");
	changed |= ImGui::SliderFloat(UI("Peso##AnimationWeight"), &anim.Weight, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::Checkbox(L("inspector.loop"), &anim.Clip->Loop);

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Suavização e easing"));
    changed |= ImGui::Checkbox(UI("Ativar suavização##AnimationSmoothing"), &anim.UseSmoothing);
    changed |= ImGui::DragFloat(UI("Intensidade##AnimationSmoothness"), &anim.Smoothness,
        0.25f, 0.0f, 50.0f, "%.2f");
    const char* easingNames[] = { "Linear", "SmoothStep", "Ease In", "Ease Out", "Ease In Out" };
    int easing = static_cast<int>(anim.Easing);
    if (ImGui::Combo(UI("Easing##AnimationEasing"), &easing, easingNames, IM_ARRAYSIZE(easingNames)))
    {
        anim.Easing = static_cast<AnimationComponent::EasingType>(easing);
        changed = true;
    }
    changed |= ImGui::DragFloat(UI("Blend In##AnimationBlendIn"), &anim.BlendIn,
        0.05f, 0.0f, 60.0f, "%.2f s");
    changed |= ImGui::DragFloat(UI("Blend Out##AnimationBlendOut"), &anim.BlendOut,
        0.05f, 0.0f, 60.0f, "%.2f s");

	ImGui::Separator();
	ImGui::TextUnformatted(UI("Controles da animação"));
	if (ImGui::Button(UI("Executar animação##AnimationPlay"), ImVec2(-1.0f, 0.0f)))
	{
		anim.Time = 0.0f;
		anim.Play();
        AnimationSystem::ApplyPoseAtTime(entity, anim.Time, 0.0f);
	}
	if (ImGui::Button(UI("Pausar##AnimationPause")))
		anim.Pause();
	ImGui::SameLine();
	if (ImGui::Button(UI("Parar##AnimationStop")))
	{
		anim.Stop();
        AnimationSystem::ApplyPoseAtTime(entity, anim.Time, 0.0f);
	}

	float previewTime = anim.Time;
	if (ImGui::SliderFloat(UI("Tempo##AnimationTime"), &previewTime, 0.0f,
	                       std::max(0.01f, anim.Clip->Duration), "%.2f s"))
	{
		anim.Time = previewTime;
        AnimationSystem::ApplyPoseAtTime(entity, anim.Time, 0.0f);
	}
	ImGui::TextDisabled(anim.Playing ? "Estado: executando" : "Estado: parado/pausado");

	ImGui::Separator();
	ImGui::TextUnformatted(UI("Máquina de estados"));
	if (ImGui::Checkbox(UI("Ativar controller de animação##AnimFSM"), &anim.StateMachineEnabled))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(UI("Escolhe clips por estados e troca por parâmetros bool ou tempo de saída."));
	if (anim.StateMachineEnabled)
	{
		if (anim.States.empty())
			ImGui::TextWrapped(UI("Adicione estados vinculados a clips desta cena. O estado inicial começa no Play."));
		else
		{
			int initialIndex = 0;
			for (int i = 0; i < static_cast<int>(anim.States.size()); ++i)
				if (anim.States[static_cast<size_t>(i)].Name == anim.InitialState) initialIndex = i;
			if (ImGui::BeginCombo(UI("Estado inicial##AnimFSMInitial"), anim.States[static_cast<size_t>(initialIndex)].Name.c_str()))
			{
				for (int i = 0; i < static_cast<int>(anim.States.size()); ++i)
				{
					const bool selected = i == initialIndex;
					if (ImGui::Selectable(anim.States[static_cast<size_t>(i)].Name.c_str(), selected))
					{
						anim.InitialState = anim.States[static_cast<size_t>(i)].Name;
						changed = true;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::TextDisabled(UI("Estado atual: %s"), anim.CurrentState.empty() ? "(parado)" : anim.CurrentState.c_str());
		}

		std::vector<std::pair<std::string, std::shared_ptr<AnimationClip>>> availableClips;
		for (const Entity& candidate : app.GetScene().GetEntities())
		{
			const auto& clip = const_cast<Entity&>(candidate).GetAnimationComponent().Clip;
			if (clip && !clip->ID.empty())
				availableClips.emplace_back(candidate.GetName() + " / " + clip->Name, clip);
		}
		static int candidateClip = 0;
		static char newStateName[128] = "Idle";
		if (!availableClips.empty())
		{
			candidateClip = std::clamp(candidateClip, 0, static_cast<int>(availableClips.size()) - 1);
			if (ImGui::BeginCombo(UI("Clip para novo estado##AnimFSMClip"), availableClips[static_cast<size_t>(candidateClip)].first.c_str()))
			{
				for (int i = 0; i < static_cast<int>(availableClips.size()); ++i)
				{
					const bool selected = candidateClip == i;
					if (ImGui::Selectable(availableClips[static_cast<size_t>(i)].first.c_str(), selected))
						candidateClip = i;
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SetNextItemWidth(170.0f);
			ImGui::InputText(UI("Nome do estado##AnimFSMName"), newStateName, sizeof(newStateName));
			ImGui::SameLine();
			if (ImGui::Button(UI("Adicionar estado##AnimFSMAddState")) && newStateName[0] != '\0')
			{
				const std::string name(newStateName);
				const bool exists = std::any_of(anim.States.begin(), anim.States.end(),
					[&](const AnimationComponent::State& state) { return state.Name == name; });
				if (!exists)
				{
					auto stateClip = std::make_shared<AnimationClip>(*availableClips[static_cast<size_t>(candidateClip)].second);
					stateClip->ID = GUID::Generate();
					stateClip->Name = name;
					anim.States.push_back({ name, std::move(stateClip) });
					if (anim.InitialState.empty()) anim.InitialState = name;
					std::snprintf(newStateName, sizeof(newStateName), "State %zu", anim.States.size() + 1);
					changed = true;
				}
			}
		}
		else
			ImGui::TextDisabled(UI("Esta cena ainda não tem clips para vincular."));

		for (size_t i = 0; i < anim.States.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::BulletText(UI("%s  ·  %s"), anim.States[i].Name.c_str(),
				anim.States[i].Clip ? anim.States[i].Clip->ID.c_str() : "(clip ausente)");
			ImGui::SameLine();
			if (ImGui::SmallButton(UI("Remover##AnimFSMState")))
			{
				const std::string removed = anim.States[i].Name;
				anim.States.erase(anim.States.begin() + static_cast<std::ptrdiff_t>(i));
				std::erase_if(anim.Transitions, [&](const AnimationComponent::Transition& t) { return t.From == removed || t.To == removed; });
				if (anim.InitialState == removed) anim.InitialState = anim.States.empty() ? "" : anim.States.front().Name;
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}

		if (anim.States.size() >= 2)
		{
			static int transitionFrom = 0, transitionTo = 1;
			static char transitionParameter[96] = "Moving";
			transitionFrom = std::clamp(transitionFrom, 0, static_cast<int>(anim.States.size()) - 1);
			transitionTo = std::clamp(transitionTo, 0, static_cast<int>(anim.States.size()) - 1);
			auto drawStateCombo = [&](const char* label, int& index)
			{
				if (ImGui::BeginCombo(label, anim.States[static_cast<size_t>(index)].Name.c_str()))
				{
					for (int i = 0; i < static_cast<int>(anim.States.size()); ++i)
					{
						const bool selected = index == i;
						if (ImGui::Selectable(anim.States[static_cast<size_t>(i)].Name.c_str(), selected)) index = i;
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			};
			drawStateCombo(UI("De##AnimFSMFrom"), transitionFrom);
			ImGui::SameLine(); drawStateCombo(UI("Para##AnimFSMTo"), transitionTo);
			ImGui::SetNextItemWidth(130.0f);
			ImGui::InputText(UI("Parâmetro bool##AnimFSMParam"), transitionParameter, sizeof(transitionParameter));
			static bool expectedValue = true;
			ImGui::SameLine(); ImGui::Checkbox(UI("Ativo##AnimFSMExpected"), &expectedValue);
			static float exitTime = 0.0f;
			ImGui::SetNextItemWidth(190.0f);
			ImGui::SliderFloat(UI("Tempo de saída##AnimFSMExit"), &exitTime, 0.0f, 1.0f, "%.2f");
			if (ImGui::Button(UI("Criar transição##AnimFSMAddTransition")) && transitionFrom != transitionTo && transitionParameter[0] != '\0')
			{
				const std::string parameter(transitionParameter);
				anim.Transitions.push_back({ anim.States[static_cast<size_t>(transitionFrom)].Name,
					anim.States[static_cast<size_t>(transitionTo)].Name, parameter, expectedValue, exitTime });
				anim.BoolParameters.try_emplace(parameter, false);
				changed = true;
			}
		}
		for (size_t i = 0; i < anim.Transitions.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			auto& transition = anim.Transitions[i];
			ImGui::TextWrapped(UI("%s → %s  |  %s = %s  |  saída %.2f"), transition.From.c_str(), transition.To.c_str(),
				transition.BoolParameter.c_str(), transition.ExpectedValue ? "true" : "false", transition.ExitTimeNormalized);
			if (ImGui::DragFloat(UI("Duração do blend (s)##AnimFSMBlend"), &transition.BlendDuration, 0.01f, 0.0f, 10.0f, "%.2f"))
			{
				transition.BlendDuration = std::clamp(transition.BlendDuration, 0.0f, 30.0f);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(UI("Excluir##AnimFSMTransition")))
			{
				anim.Transitions.erase(anim.Transitions.begin() + static_cast<std::ptrdiff_t>(i));
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (!anim.States.empty())
		{
			ImGui::TextUnformatted(UI("Visão do controller"));
			const float graphWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
			const float nodeWidth = std::max(90.0f, (graphWidth - 42.0f) * 0.5f);
			const float rowHeight = 58.0f;
			const int rows = static_cast<int>((anim.States.size() + 1) / 2);
			const float graphHeight = std::max(54.0f, 18.0f + rows * rowHeight);
			const ImVec2 graphOrigin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(graphWidth, graphHeight)); // registra a área do desenho no layout ImGui
			ImDrawList* draw = ImGui::GetWindowDrawList();
			auto centerFor = [&](size_t index)
			{
				return ImVec2(graphOrigin.x + 12.0f + (index % 2) * (nodeWidth + 12.0f) + nodeWidth * 0.5f,
					graphOrigin.y + 9.0f + (index / 2) * rowHeight + 19.0f);
			};
			for (const auto& transition : anim.Transitions)
			{
				auto from = std::find_if(anim.States.begin(), anim.States.end(), [&](const auto& s) { return s.Name == transition.From; });
				auto to = std::find_if(anim.States.begin(), anim.States.end(), [&](const auto& s) { return s.Name == transition.To; });
				if (from == anim.States.end() || to == anim.States.end()) continue;
				const size_t fromIndex = static_cast<size_t>(std::distance(anim.States.begin(), from));
				const size_t toIndex = static_cast<size_t>(std::distance(anim.States.begin(), to));
				const ImVec2 a = centerFor(fromIndex), b = centerFor(toIndex);
				draw->AddLine(a, b, IM_COL32(90, 170, 235, 220), 2.0f);
				const float angle = std::atan2(b.y - a.y, b.x - a.x);
				const ImVec2 tip(b.x - std::cos(angle) * (nodeWidth * 0.5f + 3.0f),
					b.y - std::sin(angle) * 20.0f);
				const ImVec2 wing1(tip.x - std::cos(angle - 0.55f) * 7.0f, tip.y - std::sin(angle - 0.55f) * 7.0f);
				const ImVec2 wing2(tip.x - std::cos(angle + 0.55f) * 7.0f, tip.y - std::sin(angle + 0.55f) * 7.0f);
				draw->AddTriangleFilled(tip, wing1, wing2, IM_COL32(90, 170, 235, 240));
			}
			for (size_t i = 0; i < anim.States.size(); ++i)
			{
				const ImVec2 center = centerFor(i);
				const ImVec2 min(center.x - nodeWidth * 0.5f, center.y - 19.0f);
				const ImVec2 max(center.x + nodeWidth * 0.5f, center.y + 19.0f);
				const bool initial = anim.States[i].Name == anim.InitialState;
				draw->AddRectFilled(min, max, initial ? IM_COL32(37, 74, 112, 245) : IM_COL32(39, 43, 52, 245), 6.0f);
				draw->AddRect(min, max, initial ? IM_COL32(100, 180, 255, 255) : IM_COL32(94, 102, 116, 255), 6.0f, 0, 1.5f);
				const std::string label = anim.States[i].Name + (initial ? "  [Inicial]" : "");
				draw->AddText(ImVec2(min.x + 8.0f, min.y + 11.0f), IM_COL32(235, 239, 245, 255), label.c_str());
			}
		}
		for (auto& [name, value] : anim.BoolParameters)
		{
			ImGui::PushID(name.c_str());
			if (ImGui::Checkbox((name + "##AnimFSMParameter").c_str(), &value) && app.GetEditorState() != Application::EditorState::Play)
				changed = true;
			ImGui::PopID();
		}
	}
	if (changed)
		app.MarkDirty();
}

void InspectorPanel::DrawComponentInspector(
    Application& app,
    Entity& entity,
    uint64_t componentID
)
{
    uint64_t mask = 0xFF0000000ULL;
    if ((componentID & mask) == 0x100000000ULL)
    {
        DrawScriptInspector(app, entity);
    }
    else if ((componentID & mask) == 0x200000000ULL)
    {
        if (app.Is2DWorldMode())
            ImGui::TextDisabled("Luzes não pertencem a um mundo 2D.");
        else
            DrawLightInspector(app, entity);
    }
    else if ((componentID & mask) == 0x300000000ULL)
    {
        if (app.Is2DWorldMode())
            ImGui::TextDisabled("Física 3D não pertence a um mundo 2D.");
        else
            DrawPhysicsInspector(app, entity);
    }
    else if ((componentID & mask) == 0x400000000ULL)
    {
        DrawAnimationInspector(app, entity);
    }
    else if ((componentID & mask) == 0x500000000ULL)
    {
        DrawAudioInspector(app, entity);
    }
    else if ((componentID & mask) == 0x800000000ULL)
    {
        DrawGlobalLightInspector(app, entity);
    }
    else if ((componentID & mask) == 0x900000000ULL)
    {
        DrawGlobalScriptInspector(app, entity);
    }
}

static void DrawModelAggregateInspector(Application& app, Entity& model)
{
    ImGui::TextUnformatted(UI("Modelo composto"));
    ImGui::TextDisabled("Grupo de objetos: cada filho conserva seus componentes e assets.");
    ImGui::Separator();
    char modelName[256]{};
    strcpy_s(modelName, model.GetName().c_str());
    if (ImGui::InputText(UI("Nome"), modelName, sizeof(modelName)))
    {
        model.SetName(modelName);
        app.MarkDirty();
    }

    std::vector<Entity*> members;
    std::unordered_set<uint64_t> memberIDs{ model.GetID() };
    std::vector<std::pair<uint64_t, uint64_t>> pending;
    for (uint64_t id : model.GetChildrenIDs())
        pending.emplace_back(id, model.GetID());
    size_t meshCount = 0, scriptCount = 0, audioCount = 0, lightCount = 0, physicsCount = 0;
    for (size_t cursor = 0; cursor < pending.size(); ++cursor)
    {
        const auto [childID, expectedParentID] = pending[cursor];
        if (!memberIDs.insert(childID).second)
            continue;
        Entity* child = app.GetScene().FindEntityByID(childID);
        if (!child || child->GetParentID() != expectedParentID)
            continue;
        members.push_back(child);
        if (child->GetMeshComponent().MeshData && child->GetMeshComponent().MeshData->IsValid()) ++meshCount;
        if (!child->GetScriptComponent().ScriptPath.empty()) ++scriptCount;
        if (child->HasAudio()) ++audioCount;
        if (child->HasLight()) ++lightCount;
        if (child->PhysicsEnabled) ++physicsCount;
        for (uint64_t id : child->GetChildrenIDs())
            pending.emplace_back(id, childID);
    }
    ImGui::Text(UI("%zu objeto(s) | %zu mesh | %zu script | %zu áudio | %zu luz | %zu física"),
        members.size(), meshCount, scriptCount, audioCount, lightCount, physicsCount);
    ImGui::BeginDisabled(members.empty());
    if (ImGui::Button(UI("Selecionar todos os filhos do Modelo")) && !members.empty())
    {
        std::vector<int> indices;
        const auto& sceneEntities = app.GetScene().GetEntities();
        for (Entity* member : members)
            for (size_t i = 0; i < sceneEntities.size(); ++i)
                if (sceneEntities[i].GetID() == member->GetID())
                {
                    indices.push_back(static_cast<int>(i));
                    break;
                }
        if (!indices.empty())
        {
            app.SetSelectedEntity(indices.front());
            app.SetMultiSelectedEntities(indices);
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    std::unordered_set<uint64_t> visited{ model.GetID() };
    std::function<void(Entity&, int)> drawNode = [&](Entity& child, int depth)
    {
        if (!visited.insert(child.GetID()).second)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.30f, 1.0f),
                UI("Referência repetida/ciclo detectado: %s"), child.GetName().c_str());
            return;
        }
        const std::string label = child.GetName() + "##ModelChild" + std::to_string(child.GetID());
        if (!ImGui::TreeNode(label.c_str()))
            return;

        int childIndex = -1;
        const auto& sceneEntities = app.GetScene().GetEntities();
        for (size_t i = 0; i < sceneEntities.size(); ++i)
            if (sceneEntities[i].GetID() == child.GetID())
            {
                childIndex = static_cast<int>(i);
                break;
            }
        ImGui::BeginDisabled(childIndex < 0);
        const std::string inspectLabel = std::string(UI("Inspecionar")) + "##ModelChild" +
            std::to_string(child.GetID());
        if (ImGui::SmallButton(inspectLabel.c_str()) && childIndex >= 0)
        {
            app.SetSelectedEntity(childIndex);
            app.SetMultiSelectedEntities({ childIndex });
        }
        ImGui::EndDisabled();

        ImGui::TextDisabled("Transform");
        ImGui::Text(UI("Posição: %.2f, %.2f, %.2f"),
            child.GetTransform().Position.X, child.GetTransform().Position.Y,
            child.GetTransform().Position.Z);

        if (child.GetMeshComponent().MeshData)
            ImGui::BulletText(UI("Mesh: %s (%u vértices)"),
                child.GetMeshComponent().MeshPath.empty()
                    ? child.GetMeshComponent().PrimitiveType.c_str()
                    : child.GetMeshComponent().MeshPath.c_str(),
                child.GetMeshComponent().MeshData->GetVertexCount());
        if (!child.GetScriptComponent().ScriptPath.empty())
            ImGui::BulletText(UI("Script: %s"), child.GetScriptComponent().ScriptPath.c_str());
        if (child.HasAudio())
            ImGui::BulletText(UI("Audio: %s"), child.GetAudioComponent().ClipPath.empty()
                ? "sem arquivo" : child.GetAudioComponent().ClipPath.c_str());
        if (child.HasLight())
            ImGui::BulletText(UI("Light"));
        if (child.PhysicsEnabled)
            ImGui::BulletText(UI("Physics"));
        if (child.GetAnimationComponent().Clip &&
            !child.GetAnimationComponent().Clip->Name.empty())
            ImGui::BulletText(UI("Animation: %s"), child.GetAnimationComponent().Clip->Name.c_str());

        for (uint64_t childID : child.GetChildrenIDs())
        {
            Entity* nested = app.GetScene().FindEntityByID(childID);
            if (nested && nested->GetParentID() == child.GetID())
                drawNode(*nested, depth + 1);
        }
        ImGui::TreePop();
    };

    int childCount = 0;
    for (uint64_t childID : model.GetChildrenIDs())
    {
        Entity* child = app.GetScene().FindEntityByID(childID);
        if (child && child->GetParentID() == model.GetID())
        {
            ++childCount;
            drawNode(*child, 0);
        }
    }
    if (childCount == 0)
        ImGui::TextDisabled("Nenhum filho. Arraste entidades para este Modelo na Hierarchy.");
}

void InspectorPanel::SaveModelingMesh(Application& app, Entity& entity)
{
    MeshComponent& component = entity.GetMeshComponent();
    if (component.MeshData && component.MeshData->IsValid())
        SaveGeneratedMeshAsset(app, entity, *component.MeshData);
}

void InspectorPanel::Draw(Application& app)
{
    std::string title = std::string(L("panel.inspector")) + "###Inspector";
    ImGui::Begin(
        title.c_str(),
        nullptr,
        ImGuiWindowFlags_NoCollapse
    );

    app.SetInspectorWidth(ImGui::GetWindowWidth());

    if (app.GetEditorState() == Application::EditorState::Play &&
        !app.GetEditDuringPlay())
    {
        ImGui::TextDisabled("%s", L("settings.edit_during_play_locked"));
        ImGui::End();
        return;
    }


    const auto& entities = app.GetScene().GetEntities();
    if (!entities.empty())
    {
        uint64_t selectedID = app.GetSelectedID();
        uint64_t mask = 0xFF0000000ULL;
        uint64_t componentType = selectedID & mask;

        if (componentType != 0)
        {
            uint64_t parentID = selectedID & ~mask;
            Entity* parentEntity = app.GetScene().FindEntityByID(parentID);
            if (parentEntity)
            {
                ImGui::TextUnformatted(L("inspector.component"));
                ImGui::Separator();
                DrawComponentInspector(app, *parentEntity, selectedID);
            }
            ImGui::End();
            return;
        }

        const int selectedIndex = app.GetSelectedEntityIndex();
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entities.size()))
        {
            ImGui::TextUnformatted(UI("Nenhuma entidade selecionada."));
            ImGui::End();
            return;
        }

        Entity& entity = app.GetSelectedEntity();
        InspectorBatchScope batchInspector(app, entity);
        const auto& multiSelection = app.GetMultiSelectedEntities();
        if (multiSelection.size() > 1)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.10f, 0.20f, 0.95f));
            ImGui::BeginChild("##MultiSelectionSummary", ImVec2(0.0f, 48.0f), true);
            ImGui::Text(UI("%zu objetos selecionados"), multiSelection.size());
            ImGui::TextDisabled("Traço = valores diferentes. Edite o campo para aplicar aos selecionados compatíveis.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        if (entity.IsGlobalLight())
        {
            if (app.Is2DWorldMode())
                ImGui::TextDisabled("A iluminação é desativada no mundo 2D.");
            else
                DrawGlobalLightInspector(app, entity);
            ImGui::End();
            return;
        }

        if (entity.IsGlobalScript())
        {
            DrawGlobalScriptInspector(app, entity);
            ImGui::End();
            return;
        }

        if (entity.IsEditorOnly())
        {
            ImGui::Text(UI("%s"), L("inspector.engine_obj"));
            ImGui::End();
            return;
        }

        if (entity.IsModel)
        {
            DrawModelAggregateInspector(app, entity);
            ImGui::End();
            return;
        }

        if (entity.IsFolder)
        {
            ImGui::TextUnformatted(UI("Pasta"));
            ImGui::TextDisabled("Contêiner organizacional; não possui mesh, física nem transform próprio.");
            ImGui::Separator();
            char folderName[256]{};
            strcpy_s(folderName, entity.GetName().c_str());
            if (ImGui::InputText(UI("Nome"), folderName, sizeof(folderName)))
            {
                entity.SetName(folderName);
                app.MarkDirty();
            }
            int childCount = 0;
            for (uint64_t childID : entity.GetChildrenIDs())
            {
                const Entity* child = app.GetScene().FindEntityByID(childID);
                if (child && child->GetParentID() == entity.GetID())
                    ++childCount;
            }
            ImGui::TextDisabled(UI("%d filho(s) direto(s)"), childCount);
            ImGui::TextWrapped("Arraste objetos na Hierarchy para organizar. Use Modelo para transformar um grupo.");
            ImGui::End();
            return;
        }

        // UI tem um Inspector próprio em qualquer modo. Nunca exibir os
        // componentes padrão de mesh/física como se um GuiObject fosse um cubo.
        if (entity.HasUI())
        {
            ImGui::TextUnformatted(UI("UI 2D"));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", UIElementTypeName(entity.GetUIComponent().Type));
            ImGui::Separator();
            DrawUIInspector(app, entity);
            ImGui::End();
            return;
        }

        // O modo Modelador ocupa o mesmo painel do Inspector para não criar
        // uma janela extra e para manter o fluxo de seleção do editor.
        ImGui::TextDisabled(UI("Modo de edição"));
        const float modeGap = ImGui::GetStyle().ItemSpacing.x;
        const float modeButtonWidth = std::max(1.0f,
            (ImGui::GetContentRegionAvail().x - modeGap) * 0.5f);
        const auto drawModeButton = [&](MaraeUI::EditorIcon icon, const char* label,
                                        bool active, const auto& action)
        {
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.38f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.48f, 0.82f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.32f, 0.58f, 1.0f));
            }
            if (MaraeUI::IconButton(icon, label, ImVec2(modeButtonWidth, 0.0f)))
                action();
            if (active)
                ImGui::PopStyleColor(3);
        };
        drawModeButton(MaraeUI::EditorIcon::Inspector, UI("Inspector##ObjectMode"),
            !s_ModelingMode, [] { SetModelingMode(false); });
        ImGui::SameLine();
        drawModeButton(MaraeUI::EditorIcon::Model,
            app.Is2DWorldMode() ? UI("Editar mesh 2D##ObjectMode") : UI("Modelador##ObjectMode"),
            s_ModelingMode, [&]
        {
            auto& mesh = entity.GetMeshComponent().MeshData;
            if (mesh && mesh.use_count() > 1)
            {
                auto uniqueMesh = std::make_shared<Mesh>();
                uniqueMesh->Create(mesh->GetVertices(), mesh->GetPrimitiveType(),
                                   "Inspector::MeshCopyOnWrite");
                mesh = std::move(uniqueMesh);
            }
            SetModelingMode(true);
        });
        ImGui::Separator();

        if (s_ModelingMode)
        {
            DrawModelingInspector(app, entity);
            ImGui::End();
            return;
        }

        Transform& transform = entity.GetTransform();

        if (app.Is2DWorldMode())
            Draw2DTools(app, entity);

        if (entity.HasTerrain())
        {
            TerrainComponent& terrain = entity.GetTerrainComponent();
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Terrain 3D"));
            ImGui::TextDisabled("Heightmap editável com pincel e mesh triangulado.");

            int resolution = terrain.Resolution;
            if (ImGui::SliderInt(UI("Resolução##Terrain"), &resolution, 3, 129))
            {
                if ((resolution & 1) == 0) ++resolution;
                terrain.Initialize(resolution);
                entity.GetMeshComponent().MeshData->UpdateVertices(
                    terrain.BuildVertices(), "Terrain::Resolution");
                app.MarkDirty();
            }
            ImGui::DragFloat(UI("Tamanho##Terrain"), &terrain.Size, 0.25f, 1.0f, 1000.0f, "%.1f m");
            ImGui::DragFloat(UI("Altura##Terrain"), &terrain.HeightScale, 0.05f, 0.1f, 100.0f, "%.2f m");
            ImGui::DragFloat(UI("Raio do pincel##Terrain"), &terrain.BrushRadius, 0.1f, 0.25f, 100.0f, "%.2f m");
            ImGui::DragFloat(UI("Força##Terrain"), &terrain.BrushStrength, 0.01f, 0.01f, 2.0f, "%.2f");
            const char* terrainModes[] = { "Elevar", "Abaixar", "Suavizar", "Nivelar" };
            int mode = static_cast<int>(terrain.Mode);
            if (ImGui::Combo(UI("Modo##Terrain"), &mode, terrainModes, 4))
                terrain.Mode = static_cast<TerrainComponent::SculptMode>(mode);
            if (ImGui::Button(UI("Reconstruir mesh##Terrain"), ImVec2(-1.0f, 0.0f)))
            {
                if (!entity.GetMeshComponent().MeshData)
                    entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
                entity.GetMeshComponent().MeshData->UpdateVertices(
                    terrain.BuildVertices(), "Terrain::Rebuild");
                app.MarkDirty();
            }
            ImGui::TextWrapped(UI("No viewport: segure o botão esquerdo sobre o terreno para esculpir. Use o gizmo para posicionar."));
        }

        // O Inspector usa o objeto ativo para exibir os valores, mas aplica
        // alterações de transformação em todo o conjunto selecionado.
        auto forEachSelectedTransform = [&](auto&& callback)
        {
            const auto& selectedIndices = app.GetMultiSelectedEntities();
            if (selectedIndices.empty())
            {
                callback(transform);
                return;
            }

            auto& sceneEntities = app.GetScene().GetEntities();
            for (int index : selectedIndices)
            {
                if (index >= 0 && index < static_cast<int>(sceneEntities.size()) &&
                    !sceneEntities[index].IsEditorOnly())
                {
                    callback(sceneEntities[index].GetTransform());
                }
            }
        };

        auto syncTransform = [](Transform& value)
        {
            value.LocalPosition = value.Position;
            value.LocalRotation = value.Rotation;
            value.LocalScale = value.Scale;
            value.LocalEulerRotation = value.EulerRotation;
        };

        static char nameBuffer[256];
        strcpy_s(nameBuffer, entity.GetName().c_str());
        if (ImGui::InputText(L("inspector.name"), nameBuffer, sizeof(nameBuffer)))
        {
            entity.SetName(nameBuffer);
            ForEachInspectorBatchEntity([&](Entity& target) { target.SetName(nameBuffer); });
            app.MarkDirty();
        }

        // Botão de Bloquear / Desbloquear Seleção
        if (entity.SelectionLocked)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.40f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.50f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.32f, 0.08f, 1.0f));
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Unlock, UI("Desbloquear seleção##LockTopBtn"), ImVec2(-1.0f, 0.0f)))
            {
                entity.SelectionLocked = false;
                app.MarkDirty();
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("A seleção deste objeto no viewport está bloqueada. Clique aqui para desbloquear."));
        }
        else
        {
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Lock, UI("Bloquear seleção##LockTopBtn"), ImVec2(-1.0f, 0.0f)))
            {
                entity.SelectionLocked = true;
                app.MarkDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("Bloqueia a seleção deste objeto no viewport.\nEle não poderá ser clicado ou selecionado pelo viewport, apenas pela Hierarquia."));
        }

        DrawUIInspector(app, entity);
        DrawAIInspector(app, entity);

        // ── PREFAB VINCULADO ──────────────────────────────────
        // Só aparece quando a entidade é instância de um prefab
        if (!entity.PrefabPath.empty())
        {
            ImGui::Dummy({0.f, 2.f});

            const bool isHierarchy = !entity.GetChildrenIDs().empty() || Prefab::HasHierarchy(entity.PrefabPath);

            // Faixa azul com nome do prefab
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.20f, 0.45f, 0.85f));
            ImGui::BeginChild("##prefab_bar", ImVec2(0.f, 96.f), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            // Ícone + nome do arquivo .prefab
            namespace fs = std::filesystem;
            const std::string prefabName = fs::path(entity.PrefabPath).filename().string();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
            if (isHierarchy)
                ImGui::Text(UI("  [Prefab Hierarquico]  %s"), prefabName.c_str());
            else
                ImGui::Text(UI("  [Prefab]  %s"), prefabName.c_str());
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);

            // Aplicar salva o prefab e propaga as alterações para suas instâncias.
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.46f, 0.80f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.60f, 1.00f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.04f, 0.35f, 0.65f, 1.f));
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Apply,
                    UI("Aplicar alterações ao prefab##PrefabApply"), ImVec2(-1.0f, 0.0f)))
            {
                if (Prefab::ApplyToFile(entity, &app.GetScene()))
                {
                    // Propaga para todas as outras instâncias da cena (incluindo filhos!)
                    Prefab::ApplyToAllInstances(app.GetScene(), entity.PrefabPath, true);
                    app.MarkDirty();
                }
            }
            ImGui::PopStyleColor(3);

            const float prefabActionGap = ImGui::GetStyle().ItemSpacing.x;
            const float prefabActionWidth = std::max(1.0f,
                (ImGui::GetContentRegionAvail().x - prefabActionGap) * 0.5f);
            // Reverter e desvincular ficam separados do comando principal.
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.40f, 0.28f, 0.06f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.48f, 0.10f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f, 0.20f, 0.04f, 1.f));
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Revert,
                    UI("Reverter##PrefabRevert"), ImVec2(prefabActionWidth, 0.0f)))
            {
                Prefab::RevertInstance(entity, &app.GetScene());
                app.MarkDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("Descarta as alterações locais e restaura esta instância a partir do prefab."));
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.f, 6.f);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.10f, 0.10f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.15f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.08f, 0.08f, 1.f));
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Unlink,
                    UI("Desvincular##PrefabUnlink"), ImVec2(prefabActionWidth, 0.0f)))
            {
                Prefab::UnlinkHierarchy(app.GetScene(), entity);
                app.MarkDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("Remove o vínculo desta hierarquia com o arquivo prefab."));
            ImGui::PopStyleColor(3);

            ImGui::EndChild();
            ImGui::PopStyleColor(); // ChildBg

            ImGui::Dummy({0.f, 2.f});
        }

        // ── TRANSFORM ─────────────────────────────────────────
        // Unity: Transform é sempre o 1º componente, logo após Nome/Active
        if (app.Is2DWorldMode())
        {
            ImGui::TextUnformatted(UI("Transformação 2D"));
            float pos2D[2] = { transform.Position.X, transform.Position.Y };
            if (ImGui::DragFloat2("Posição XY##2DTransform", pos2D, 0.1f))
            {
                const glm::vec2 delta(pos2D[0] - transform.Position.X,
                                      pos2D[1] - transform.Position.Y);
                forEachSelectedTransform([&](Transform& value)
                {
                    value.Position.X += delta.x;
                    value.Position.Y += delta.y;
                    value.Position.Z = 0.0f;
                    syncTransform(value);
                });
                app.MarkDirty();
            }
            float angle2D = transform.EulerRotation.Z;
            if (ImGui::DragFloat(UI("Rotação##2DTransform"), &angle2D, 0.5f, -360.0f, 360.0f, "%.1f°"))
            {
                const float delta = angle2D - transform.EulerRotation.Z;
                forEachSelectedTransform([&](Transform& value)
                {
                    value.EulerRotation.X = 0.0f;
                    value.EulerRotation.Y = 0.0f;
                    value.EulerRotation.Z += delta;
                    value.Rotation = glm::quat(glm::radians(glm::vec3(0.0f, 0.0f, value.EulerRotation.Z)));
                    syncTransform(value);
                });
                app.MarkDirty();
            }
            float scale2D[2] = { transform.Scale.X, transform.Scale.Y };
            float oldScale2D[2] = { scale2D[0], scale2D[1] };
            ImGui::TextUnformatted(UI("Escala 2D"));
            const float scale2DAvailable = ImGui::GetContentRegionAvail().x;
            const float scale2DActionGap = ImGui::GetStyle().ItemSpacing.x;
            const float scale2DResetWidth = 38.0f;
            const float scale2DLockWidth = std::max(1.0f, scale2DAvailable - scale2DResetWidth - scale2DActionGap);
            if (MaraeUI::IconButton(s_ScaleUniformLocked ? MaraeUI::EditorIcon::Lock : MaraeUI::EditorIcon::Unlock,
                    s_ScaleUniformLocked ? UI("Proporcional##2DSclLock") : UI("Escala livre##2DSclLock"),
                    ImVec2(scale2DLockWidth, 0.0f)))
            {
                s_ScaleUniformLocked = !s_ScaleUniformLocked;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Alterna entre escala proporcional ou independente para X e Y.");
            }
            ImGui::SameLine();
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::Reset,
                    UI("##2DSclReset"), ImVec2(scale2DResetWidth, 0.0f)))
            {
                forEachSelectedTransform([&](Transform& value)
                {
                    value.Scale.X = 1.0f;
                    value.Scale.Y = 1.0f;
                    value.Scale.Z = 1.0f;
                    syncTransform(value);
                });
                app.MarkDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("Redefine a escala 2D para 1 × 1."));

            bool scale2DChanged = false;
            const float itemSpacing2D = ImGui::GetStyle().ItemSpacing.x;
            const float labelWidth2D = 18.0f;
            const float totalAvail2D = ImGui::GetContentRegionAvail().x;
            const float widgetWidth2D = std::max(40.0f, (totalAvail2D - (labelWidth2D * 2.0f + itemSpacing2D * 3.0f)) / 2.0f);

            // X 2D
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
            if (ImGui::Button("X##2DSclLbl", ImVec2(labelWidth2D, 0.0f)))
            {
                scale2D[0] = 1.0f;
                scale2DChanged = true;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(widgetWidth2D);
            if (ImGui::DragFloat("##2DSclX", &scale2D[0], s_ScaleStep, 0.001f, 10000.0f, "%.3f")) scale2DChanged = true;

            // Y 2D
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
            if (ImGui::Button("Y##2DSclLbl", ImVec2(labelWidth2D, 0.0f)))
            {
                scale2D[1] = 1.0f;
                scale2DChanged = true;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(widgetWidth2D);
            if (ImGui::DragFloat("##2DSclY", &scale2D[1], s_ScaleStep, 0.001f, 10000.0f, "%.3f")) scale2DChanged = true;

            scale2D[0] = std::max(0.001f, scale2D[0]);
            scale2D[1] = std::max(0.001f, scale2D[1]);

            if (scale2DChanged)
            {
                if (s_ScaleUniformLocked)
                {
                    float ratio = 1.0f;
                    if (std::abs(scale2D[0] - oldScale2D[0]) > 0.00001f && std::abs(oldScale2D[0]) > 0.0001f)
                        ratio = scale2D[0] / oldScale2D[0];
                    else if (std::abs(scale2D[1] - oldScale2D[1]) > 0.00001f && std::abs(oldScale2D[1]) > 0.0001f)
                        ratio = scale2D[1] / oldScale2D[1];

                    scale2D[0] = std::max(0.001f, oldScale2D[0] * ratio);
                    scale2D[1] = std::max(0.001f, oldScale2D[1] * ratio);
                }

                forEachSelectedTransform([&](Transform& value)
                {
                    value.Scale.X = std::max(0.001f, scale2D[0]);
                    value.Scale.Y = std::max(0.001f, scale2D[1]);
                    value.Scale.Z = 1.0f;
                    syncTransform(value);
                });
                app.MarkDirty();
            }
        }
        else
        {
        ImGui::Text(UI("%s"), L("inspector.position"));
        float pos[3] = { transform.Position.X, transform.Position.Y, transform.Position.Z };
        if (ImGui::DragFloat3("##Pos", pos, 0.1f, 0.0f, 0.0f, "%.3f"))
        {
            const glm::vec3 delta(
                pos[0] - transform.Position.X,
                pos[1] - transform.Position.Y,
                pos[2] - transform.Position.Z);
            forEachSelectedTransform([&](Transform& value)
            {
                value.Position.X += delta.x;
                value.Position.Y += delta.y;
                value.Position.Z += delta.z;
                syncTransform(value);
            });
            app.MarkDirty();
        }

        ImGui::Text(UI("%s"), L("inspector.rotation"));
        float rot[3] = { transform.EulerRotation.X, transform.EulerRotation.Y, transform.EulerRotation.Z };
        if (ImGui::DragFloat3("##Rot", rot, 0.5f, 0.0f, 0.0f, "%.2f deg"))
        {
            const glm::vec3 delta(
                rot[0] - transform.EulerRotation.X,
                rot[1] - transform.EulerRotation.Y,
                rot[2] - transform.EulerRotation.Z);
            forEachSelectedTransform([&](Transform& value)
            {
                value.EulerRotation.X += delta.x;
                value.EulerRotation.Y += delta.y;
                value.EulerRotation.Z += delta.z;
                value.Rotation = glm::quat(glm::radians(glm::vec3(
                    value.EulerRotation.X,
                    value.EulerRotation.Y,
                    value.EulerRotation.Z)));
                syncTransform(value);
            });
            app.MarkDirty();
        }

        ImGui::Text(UI("%s"), L("inspector.scale"));
        const float scaleActionAvailable = ImGui::GetContentRegionAvail().x;
        const float scaleActionGap = ImGui::GetStyle().ItemSpacing.x;
        const float scaleResetWidth = 38.0f;
        const float scaleLockWidth = std::max(1.0f, scaleActionAvailable - scaleResetWidth - scaleActionGap);
        // Botão de Proporção / Trava Uniforme [=] ou [≠]
        {
            bool toggleScaleLock = false;
            {
                UniformScaleButtonStyle buttonStyle(s_ScaleUniformLocked);
                toggleScaleLock = MaraeUI::IconButton(
                    s_ScaleUniformLocked ? MaraeUI::EditorIcon::Lock : MaraeUI::EditorIcon::Unlock,
                    s_ScaleUniformLocked ? UI("Proporcional##SclLock") : UI("Escala livre##SclLock"),
                    ImVec2(scaleLockWidth, 0.0f));
            }
            if (toggleScaleLock)
            {
                s_ScaleUniformLocked = !s_ScaleUniformLocked;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(s_ScaleUniformLocked ?
                    "Escala Uniforme ATIVA: alterar qualquer eixo ajusta os outros proporcionalmente.\nClique para destravar (ajuste por eixo livre)." :
                    "Escala Livre: alteração independente por eixo (X, Y, Z).\nClique para travar proporção uniforme.");
            }
        }
        ImGui::SameLine();
        if (MaraeUI::IconButton(MaraeUI::EditorIcon::Reset,
                UI("##ResetScale"), ImVec2(scaleResetWidth, 0.0f)))
        {
            forEachSelectedTransform([&](Transform& value)
            {
                value.Scale.X = 1.0f;
                value.Scale.Y = 1.0f;
                value.Scale.Z = 1.0f;
                syncTransform(value);
            });
            app.MarkDirty();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Redefine a escala selecionada para (1.0, 1.0, 1.0).");
        }

        float scl[3] = { transform.Scale.X, transform.Scale.Y, transform.Scale.Z };
        float oldScl[3] = { scl[0], scl[1], scl[2] };
        bool scaleChanged = false;

        // Visual aprimorado com badges coloridos (X: vermelho, Y: verde, Z: azul)
        const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float labelWidth = 18.0f;
        const float totalAvail = ImGui::GetContentRegionAvail().x;
        const float widgetWidth = std::max(40.0f, (totalAvail - (labelWidth * 3.0f + itemSpacing * 5.0f)) / 3.0f);

        // --- Eixo X ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        if (ImGui::Button("X##SclLbl", ImVec2(labelWidth, 0.0f)))
        {
            scl[0] = 1.0f;
            scaleChanged = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eixo X. Clique para resetar X para 1.0.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(widgetWidth);
        if (ImGui::DragFloat("##SclX", &scl[0], s_ScaleStep, 0.001f, 10000.0f, "%.3f")) scaleChanged = true;

        // --- Eixo Y ---
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.85f, 0.30f, 1.0f));
        if (ImGui::Button("Y##SclLbl", ImVec2(labelWidth, 0.0f)))
        {
            scl[1] = 1.0f;
            scaleChanged = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eixo Y. Clique para resetar Y para 1.0.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(widgetWidth);
        if (ImGui::DragFloat("##SclY", &scl[1], s_ScaleStep, 0.001f, 10000.0f, "%.3f")) scaleChanged = true;

        // --- Eixo Z ---
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.60f, 1.0f, 1.0f));
        if (ImGui::Button("Z##SclLbl", ImVec2(labelWidth, 0.0f)))
        {
            scl[2] = 1.0f;
            scaleChanged = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eixo Z. Clique para resetar Z para 1.0.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(widgetWidth);
        if (ImGui::DragFloat("##SclZ", &scl[2], s_ScaleStep, 0.001f, 10000.0f, "%.3f")) scaleChanged = true;

        // Garante que escalas sejam positivas e válidas
        scl[0] = std::max(0.001f, scl[0]);
        scl[1] = std::max(0.001f, scl[1]);
        scl[2] = std::max(0.001f, scl[2]);

        if (scaleChanged)
        {
            glm::vec3 factor(1.0f);
            if (s_ScaleUniformLocked)
            {
                // Se proporcional/travado, pega o eixo que mudou e aplica a mesma razão a todos
                float ratio = 1.0f;
                if (std::abs(scl[0] - oldScl[0]) > 0.00001f && std::abs(oldScl[0]) > 0.0001f)
                    ratio = scl[0] / oldScl[0];
                else if (std::abs(scl[1] - oldScl[1]) > 0.00001f && std::abs(oldScl[1]) > 0.0001f)
                    ratio = scl[1] / oldScl[1];
                else if (std::abs(scl[2] - oldScl[2]) > 0.00001f && std::abs(oldScl[2]) > 0.0001f)
                    ratio = scl[2] / oldScl[2];

                factor = glm::vec3(ratio, ratio, ratio);
            }
            else
            {
                factor = glm::vec3(
                    std::abs(oldScl[0]) > 0.0001f ? scl[0] / oldScl[0] : 1.0f,
                    std::abs(oldScl[1]) > 0.0001f ? scl[1] / oldScl[1] : 1.0f,
                    std::abs(oldScl[2]) > 0.0001f ? scl[2] / oldScl[2] : 1.0f);
            }

            forEachSelectedTransform([&](Transform& value)
            {
                value.Scale.X = std::max(0.001f, value.Scale.X * factor.x);
                value.Scale.Y = std::max(0.001f, value.Scale.Y * factor.y);
                value.Scale.Z = std::max(0.001f, value.Scale.Z * factor.z);
                syncTransform(value);
            });
            app.MarkDirty();
        }

        // Multiplicadores rápidos úteis (0.5x, 2x, 10x)
        {
            ImGui::TextDisabled("Multiplicar:");
            ImGui::SameLine();
            auto applyScaleMult = [&](float mult)
            {
                forEachSelectedTransform([&](Transform& value)
                {
                    value.Scale.X = std::max(0.001f, value.Scale.X * mult);
                    value.Scale.Y = std::max(0.001f, value.Scale.Y * mult);
                    value.Scale.Z = std::max(0.001f, value.Scale.Z * mult);
                    syncTransform(value);
                });
                app.MarkDirty();
            };
            if (ImGui::SmallButton("0.5x##SclHalf")) applyScaleMult(0.5f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reduz pela metade a escala atual.");
            ImGui::SameLine();
            if (ImGui::SmallButton("2x##SclDouble")) applyScaleMult(2.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dobra a escala atual.");
            ImGui::SameLine();
            if (ImGui::SmallButton("10x##SclTen")) applyScaleMult(10.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplica por 10x a escala atual.");
            ImGui::SameLine();
            if (ImGui::SmallButton("0.1x##SclTenth")) applyScaleMult(0.1f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reduz a 1/10 da escala atual.");
        }
        }

        ImGui::Separator();

        // ── ENTITY LIFECYCLE / FILTERS ────────────────────────
        // These flags are intentionally independent: Active controls gameplay,
        // Opacity controls drawing and shadow strength; Raycast target controls
        // editor picking independently.
        if (ImGui::Checkbox(UI("Ativo##EntityState"), &entity.Active))
        {
            app.MarkDirty();
        }
        if (ImGui::SliderFloat(UI("Visibilidade##EntityState"), &entity.Opacity, 0.0f, 1.0f, "%.2f"))
        {
            entity.Opacity = std::clamp(entity.Opacity, 0.0f, 1.0f);
            app.MarkDirty();
        }
        if (ImGui::Checkbox(UI("Alvo de raycast##EntityState"), &entity.RaycastTarget))
        {
            app.MarkDirty();
        }
        if (ImGui::Checkbox(UI("Bloquear seleção##EntityLockCheck"), &entity.SelectionLocked))
        {
            app.MarkDirty();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(UI("Impede que este objeto seja selecionado ao clicar no viewport. Só pode ser selecionado ou desbloqueado na Hierarquia."));
        }

        int layer = entity.Layer;
        if (ImGui::DragInt(UI("Camada##EntityState"), &layer, 1.0f,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(), "%d"))
        {
            entity.Layer = layer;
            app.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Default##LayerDefault")))
        {
            entity.Layer = 0;
            app.MarkDirty();
        }
        ImGui::TextDisabled("Menor = mais profundo; maior = mais acima (ordem de renderizacao).");

        static uint64_t tagEntityID = 0;
        static char tagBuffer[128] = {};
        if (tagEntityID != entity.GetID())
        {
            const size_t copyLength = std::min(entity.Tag.size(), sizeof(tagBuffer) - 1);
            std::memcpy(tagBuffer, entity.Tag.data(), copyLength);
            tagBuffer[copyLength] = '\0';
            tagEntityID = entity.GetID();
        }
        if (ImGui::InputText(UI("Tag##EntityState"), tagBuffer, sizeof(tagBuffer)))
        {
            entity.Tag = tagBuffer;
            app.MarkDirty();
        }

        ImGui::Separator();

        if (!app.Is2DWorldMode() && entity.HasCamera())
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            const bool cameraOpen = ImGui::CollapsingHeader(UI("Câmera de Cena"), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();
            if (cameraOpen)
                DrawCameraInspector(app, entity);
            ImGui::Separator();
        }

        // ── MESH ───────────────────────────────────────────────
        ImGui::Text(UI("%s"), L("inspector.mesh"));
        MeshComponent& meshComponent = entity.GetMeshComponent();
        const std::string meshLabel = meshComponent.MeshPath.empty() ? "Cube (built-in)" : meshComponent.MeshPath;
        ImGui::Button(meshLabel.c_str(), ImVec2(220, 36));

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            {
                const char* path = static_cast<const char*>(payload->Data);
                if (path && path[0] != '\0')
                {
                    std::string assetPath = path;
                    std::string extension = std::filesystem::path(assetPath).extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                        extension == ".bmp" || extension == ".tga" || extension == ".webp" || extension == ".hdr")
                    {
                        auto quad = std::make_shared<Mesh>();
                        quad->Create(MeshFactory::CreateQuad(), GL_TRIANGLES, "InspectorPanel::ImageAsset");
                        meshComponent.MeshData = quad;
                        meshComponent.MeshPath = "builtin://quad";
                        entity.GetMaterialComponent().MaterialData.TexturePath = assetPath;
                        entity.GetMaterialComponent().MaterialData.TextureName = std::filesystem::path(assetPath).stem().string();
                        entity.GetMaterialComponent().MaterialData.DiffuseTexture = TextureCache::Load(assetPath);
                        entity.GetColorComponent().Color = Vector3(1.0f, 1.0f, 1.0f);
                        ForEachInspectorBatchComponent<MeshComponent>([&](MeshComponent& target)
                        {
                            target.MeshPath = meshComponent.MeshPath;
                            target.MeshData = meshComponent.MeshData;
                        });
                        ForEachInspectorBatchComponent<MaterialComponent>([&](MaterialComponent& target)
                        {
                            target.MaterialData.TexturePath = entity.GetMaterialComponent().MaterialData.TexturePath;
                            target.MaterialData.TextureName = entity.GetMaterialComponent().MaterialData.TextureName;
                            target.MaterialData.DiffuseTexture = entity.GetMaterialComponent().MaterialData.DiffuseTexture;
                        });
                        ForEachInspectorBatchComponent<ColorComponent>([&](ColorComponent& target)
                        {
                            target.Color = entity.GetColorComponent().Color;
                        });
                        app.MarkDirty();
                    }
                    else
                    {
                        auto mesh = MeshCache::Load(assetPath);
                        if (mesh && mesh->IsValid())
                        {
                            meshComponent.MeshData = mesh;
                            meshComponent.MeshPath = assetPath;
                            ForEachInspectorBatchComponent<MeshComponent>([&](MeshComponent& target)
                            {
                                target.MeshData = mesh;
                                target.MeshPath = assetPath;
                            });
                            app.MarkDirty();
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();

        // ── MATERIAL ───────────────────────────────────────────
        MaterialComponent& materialComponent = entity.GetMaterialComponent();
        Material* material = MaterialDatabase::GetMaterialByGUID(materialComponent.MaterialGUID);
        if (!material)
            material = &materialComponent.MaterialData;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
        const bool materialOpen = ImGui::CollapsingHeader(
            (std::string("[M] ") + L("material.section")).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen
        );
        ImGui::PopStyleColor();

        if (materialOpen)
        {
            char buffer[256];
            strcpy_s(buffer, sizeof(buffer), material->Name.c_str());
            if (ImGui::InputText(L("inspector.material"), buffer, sizeof(buffer)))
            {
                material->Name = buffer;
                app.MarkDirty();
            }

            const char* materialTypes[] = {
                L("material.plastic"),
                L("material.metal"),
                L("material.wood"),
                L("material.neon"),
                L("material.glass"),
                L("material.rubber"),
                L("material.ceramic"),
                L("material.fabric"),
                L("material.mirror")
            };
            int materialType = static_cast<int>(material->Type);
            if (ImGui::Combo(L("material.type"), &materialType, materialTypes, 9))
            {
                materialType = std::clamp(materialType, 0, 8);
                material->Type = static_cast<MaterialType>(materialType);
                ForEachInspectorBatchComponent<MaterialComponent>([&](MaterialComponent& target)
                {
                    target.MaterialData.Type = material->Type;
                });
                app.MarkDirty();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "Modo de Escala (Anti-Esticamento)");
            const char* uvScaleModes[] = {
                "1. Esticar Normal (Padrao)",
                "2. Fixar Desenho (Sem Esticar / Sem Duplicar)",
                "3. Repetir por Face (Tile na Face)",
                "4. 9-Fatias / Nine-Slice (Preservar Bordas)",
                "5. Duplicar Mesh Modular (Tile Mesh)",
                "6. Repetir no Mundo (Triplanar World Space)"
            };
            int currentSelection = 0;
            if (material->ScaleMode == UVScaleMode::Stretch) currentSelection = 0;
            else if (material->ScaleMode == UVScaleMode::FixedDrawing) currentSelection = 1;
            else if (material->ScaleMode == UVScaleMode::AutoTileFace) currentSelection = 2;
            else if (material->ScaleMode == UVScaleMode::NineSlice) currentSelection = 3;
            else if (material->ScaleMode == UVScaleMode::TileMesh) currentSelection = 4;
            else if (material->ScaleMode == UVScaleMode::WorldSpace) currentSelection = 5;

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##MaterialScaleMode", &currentSelection, uvScaleModes, 6))
            {
                if (currentSelection == 0) material->ScaleMode = UVScaleMode::Stretch;
                else if (currentSelection == 1) material->ScaleMode = UVScaleMode::FixedDrawing;
                else if (currentSelection == 2) material->ScaleMode = UVScaleMode::AutoTileFace;
                else if (currentSelection == 3) material->ScaleMode = UVScaleMode::NineSlice;
                else if (currentSelection == 4) material->ScaleMode = UVScaleMode::TileMesh;
                else if (currentSelection == 5) material->ScaleMode = UVScaleMode::WorldSpace;
                app.MarkDirty();
            }

            if (material->ScaleMode == UVScaleMode::Stretch)
            {
                ImGui::TextDisabled("Comportamento padrao: a textura se estica junto com o objeto.");
            }
            else if (material->ScaleMode == UVScaleMode::FixedDrawing)
            {
                ImGui::TextDisabled("O desenho mantem seu tamanho real, sem esticar e sem duplicar.");
                const char* alignNames[] = {
                    "Centro",
                    "Canto Inferior-Esquerdo",
                    "Canto Inferior-Direito",
                    "Canto Superior-Esquerdo",
                    "Canto Superior-Direito"
                };
                int align = std::clamp(material->FixedAlignment, 0, 4);
                if (ImGui::Combo(UI("Alinhamento do Desenho##FixedAlign"), &align, alignNames, 5))
                {
                    material->FixedAlignment = align;
                    app.MarkDirty();
                }
            }
            else if (material->ScaleMode == UVScaleMode::AutoTileFace)
            {
                ImGui::TextDisabled("Repete a textura em cada face de acordo com as dimensoes do objeto sem distorcer.");
                if (ImGui::DragFloat(UI("Repeticao por Face##FaceTile"), &material->FaceTileRepeat, 0.05f, 0.1f, 20.0f, "%.2fx"))
                {
                    material->FaceTileRepeat = std::max(0.01f, material->FaceTileRepeat);
                    app.MarkDirty();
                }
            }
            else if (material->ScaleMode == UVScaleMode::NineSlice)
            {
                ImGui::TextDisabled("Preserva os 4 cantos e bordas com escala original; estica somente o centro (ideal para botoes e molduras).");
                if (ImGui::SliderFloat(UI("Borda (Margem)##NineSliceB"), &material->NineSliceBorder, 0.02f, 0.45f, "%.2f"))
                {
                    material->NineSliceBorder = std::clamp(material->NineSliceBorder, 0.01f, 0.45f);
                    app.MarkDirty();
                }
            }
            else if (material->ScaleMode == UVScaleMode::TileMesh)
            {
                if (ImGui::DragFloat(UI("Tamanho do Bloco##MatTileSize"), &material->TileSize, 0.05f, 0.1f, 10.0f, "%.2f m"))
                {
                    material->TileSize = std::max(0.05f, material->TileSize);
                    app.MarkDirty();
                }
                ImGui::TextDisabled("Ao esticar, o mesh e duplicado em blocos modulares independentes.");
            }
            else if (material->ScaleMode == UVScaleMode::WorldSpace)
            {
                if (ImGui::DragFloat(UI("Escala UV Mundo##MatUVScale"), &material->UVWorldScale, 0.05f, 0.01f, 20.0f, "%.2f"))
                {
                    material->UVWorldScale = std::max(0.001f, material->UVWorldScale);
                    app.MarkDirty();
                }
                ImGui::TextDisabled("Textura triplanar em espaco de mundo: continua perfeitamente entre paredes e pisos.");
            }
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Parâmetros PBR"));
            if (ImGui::Checkbox(UI("Usar parâmetros personalizados##MaterialPBR"),
                                &material->PBRParametersEnabled))
                app.MarkDirty();
            if (material->PBRParametersEnabled)
            {
                bool changed = false;
                changed |= ImGui::SliderFloat(UI("Base Color Roughness##MaterialPBR"),
                                              &material->Roughness, 0.02f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat(UI("Metallic##MaterialPBR"),
                                              &material->Metallic, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat(UI("Specular##MaterialPBR"),
                                              &material->Specular, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat(UI("Emission##MaterialPBR"),
                                              &material->EmissionStrength, 0.0f, 10.0f, "%.3f");
                changed |= ImGui::SliderFloat(UI("Alpha##MaterialPBR"),
                                              &material->Alpha, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat(UI("Normal Strength##MaterialPBR"),
                                              &material->NormalStrength, 0.0f, 2.0f, "%.3f");
                char normalMap[512];
                strcpy_s(normalMap, sizeof(normalMap), material->NormalMapPath.c_str());
                if (ImGui::InputText(UI("Normal Map##MaterialPBR"), normalMap, sizeof(normalMap)))
                {
                    material->NormalMapPath = normalMap;
                    material->NormalTexture.reset();
                    changed = true;
                }
                char emissionMap[512];
                strcpy_s(emissionMap, sizeof(emissionMap), material->EmissionTexturePath.c_str());
                if (ImGui::InputText(UI("Emission Map##MaterialPBR"), emissionMap, sizeof(emissionMap)))
                {
                    material->EmissionTexturePath = emissionMap;
                    material->EmissionTexture.reset();
                    changed = true;
                }
                if (changed)
                    app.MarkDirty();
            }

            // ── Mirror Material Properties ────────────────────────────────
            if (material->Type == MaterialType::Mirror)
            {
                ImGui::Separator();
                ImGui::TextUnformatted(UI("Propriedades do Espelho"));

                const char* mirrorQualityLabels[] = {
                    L("mirror.quality.inherit"),
                    L("mirror.quality.ultra_performance"),
                    L("mirror.quality.performance"),
                    L("mirror.quality.balanced"),
                    L("mirror.quality.high_fidelity"),
                    L("mirror.quality.ultra")
                };
                int mirrorQualityIdx = static_cast<int>(material->MirrorReflectionQuality);
                if (ImGui::Combo(L("mirror.quality"), &mirrorQualityIdx, mirrorQualityLabels, 6))
                {
                    material->MirrorReflectionQuality = static_cast<MirrorQuality>(mirrorQualityIdx);
                    app.MarkDirty();
                }
                ImGui::TextDisabled("%s", L("mirror.quality_hint"));

                bool changed = false;
                changed |= ImGui::SliderFloat(L("mirror.intensity"), 
                                              &material->MirrorIntensity, 0.0f, 1.0f, "%.3f");
                ImGui::TextDisabled("%s", L("mirror.intensity_hint"));

                changed |= ImGui::SliderFloat(L("mirror.blur"), 
                                              &material->MirrorBlur, 0.0f, 1.0f, "%.3f");
                ImGui::TextDisabled("%s", L("mirror.blur_hint"));

                changed |= ImGui::SliderFloat(L("mirror.clip_offset"), 
                                              &material->MirrorClipOffset, 0.0f, 0.5f, "%.4f");
                ImGui::TextDisabled("%s", L("mirror.clip_offset_hint"));

                if (changed)
                    app.MarkDirty();
            }

            ImGui::TextDisabled("%s", L("material.color_hint"));

            ImGui::Text(UI("%s"), L("inspector.texture"));
            std::string textureName = material->TextureName.empty()
                ? std::string(L("common.none"))
                : material->TextureName;
            ImGui::Button(textureName.c_str(), ImVec2(220, 36));

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && path[0] != '\0')
                    {
                        material->TextureName = std::filesystem::path(path).stem().string();
                        material->TexturePath = path;
                        material->TextureGUID = "";
                        material->DiffuseTexture = TextureCache::Load(material->TexturePath);
                        app.MarkDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (!app.Is2DWorldMode() && meshComponent.MeshData &&
                meshComponent.MeshData->GetPrimitiveType() == GL_TRIANGLES)
            {
                const auto& faces = meshComponent.MeshData->GetFaceRanges();
                bool applyTextureToAllFaces = materialComponent.TextureMode == FaceTextureMode::AllFaces;
                if (ImGui::Checkbox("Aplicar textura em todas as faces", &applyTextureToAllFaces))
                {
                    materialComponent.TextureMode = applyTextureToAllFaces
                        ? FaceTextureMode::AllFaces : FaceTextureMode::IndividualFaces;
                    app.MarkDirty();
                }

                if (materialComponent.TextureMode == FaceTextureMode::IndividualFaces)
                {
                    ImGui::TextDisabled("Cada face mantém sua geometria. Remover uma imagem limpa só a textura.");
                    static uint64_t facePageEntity = 0;
                    static int facePage = 0;
                    if (facePageEntity != entity.GetID())
                    {
                        facePageEntity = entity.GetID();
                        facePage = 0;
                    }
                    constexpr int facesPerPage = 12;
                    const int pageCount = std::max(1, static_cast<int>((faces.size() + facesPerPage - 1) / facesPerPage));
                    facePage = std::clamp(facePage, 0, pageCount - 1);
                    if (ImGui::SmallButton("<<##FacePagePrev")) facePage = std::max(0, facePage - 1);
                    ImGui::SameLine();
                    ImGui::Text("Página %d / %d  (%zu faces)", facePage + 1, pageCount, faces.size());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(">>##FacePageNext")) facePage = std::min(pageCount - 1, facePage + 1);

                    const size_t firstFace = static_cast<size_t>(facePage) * facesPerPage;
                    const size_t lastFace = std::min(faces.size(), firstFace + facesPerPage);
                    for (size_t faceIndex = firstFace; faceIndex < lastFace; ++faceIndex)
                    {
                        ImGui::PushID(static_cast<int>(faceIndex));
                        ImGui::Text("Face %zu", faceIndex + 1);
                        ImGui::SameLine(78.0f);
                        const auto pathIt = materialComponent.FaceTexturePaths.find(
                            static_cast<uint32_t>(faceIndex));
                        const bool hasTexture = pathIt != materialComponent.FaceTexturePaths.end() &&
                            !pathIt->second.empty();
                        const std::string faceTextureName = hasTexture
                            ? std::filesystem::path(pathIt->second).filename().string()
                            : "Sem textura";
                        ImGui::Button(faceTextureName.c_str(), ImVec2(165.0f, 28.0f));
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                            {
                                const char* droppedPath = static_cast<const char*>(payload->Data);
                                if (droppedPath && droppedPath[0] != '\0')
                                {
                                    const std::string path = droppedPath;
                                    const std::string extension = [&]() {
                                        std::string ext = std::filesystem::path(path).extension().string();
                                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                        return ext;
                                    }();
                                    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                                        extension == ".bmp" || extension == ".tga" || extension == ".webp" ||
                                        extension == ".hdr")
                                    {
                                        const uint32_t key = static_cast<uint32_t>(faceIndex);
                                        materialComponent.FaceTexturePaths[key] = path;
                                        materialComponent.FaceTextures.erase(key);
                                        app.MarkDirty();
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##RemoveFaceTexture") && hasTexture)
                        {
                            const uint32_t key = static_cast<uint32_t>(faceIndex);
                            materialComponent.FaceTexturePaths.erase(key);
                            materialComponent.FaceTextures.erase(key);
                            app.MarkDirty();
                        }
                        ImGui::PopID();
                    }
                }
            }

            // A cor é uma propriedade do objeto/mesh, nunca do material.
            ColorComponent& color = entity.GetColorComponent();
            float meshColor[4] = { color.Color.X, color.Color.Y, color.Color.Z, 1.0f };
            if (ImGui::ColorEdit4(L("inspector.mesh_color"), meshColor))
            {
                color.Color.X = meshColor[0];
                color.Color.Y = meshColor[1];
                color.Color.Z = meshColor[2];
                ForEachInspectorBatchComponent<ColorComponent>([&](ColorComponent& target)
                {
                    target.Color = color.Color;
                });
                app.MarkDirty();
            }
        }

        ImGui::Separator();

        // A animação só vira um componente visível depois que o usuário a
        // adiciona pelo menu da Hierarchy. Entidades comuns não exibem este
        // painel vazio.
        const AnimationComponent& entityAnimation = entity.GetAnimationComponent();
        if (entityAnimation.Clip && !entityAnimation.Clip->Name.empty())
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(UI("Animação##EntityAnimation"),
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                DrawAnimationInspector(app, entity);
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── PHYSICS ────────────────────────────────────────────
        if (app.Is2DWorldMode())
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            const bool physics2DOpen = ImGui::CollapsingHeader(
                UI("Física 2D · Colisor e gravidade##Physics2D"),
                ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();
            if (physics2DOpen)
            {
                DrawPhysicsInspector(app, entity);
                ImGui::TextDisabled(UI("Simulação 2D no plano XY; caixas usam limites alinhados ao eixo e círculos usam raio."));
            }
        }
        else
        {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
        bool physicsOpen = ImGui::CollapsingHeader(
            (std::string("[Physics] ") + L("inspector.physics")).c_str(),
            ImGuiTreeNodeFlags_DefaultOpen
        );
        ImGui::PopStyleColor();

        if (physicsOpen)
        {
            DrawPhysicsInspector(app, entity);
        }
        }

        if (!app.Is2DWorldMode())
            DrawAcousticGeometryInspector(app, entity);

        // ── LIGHT (IF PRESENT) ─────────────────────────────────
        if (!app.Is2DWorldMode() && entity.HasLight())
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(L("inspector.light"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                DrawLightInspector(app, entity);
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── AUDIO (IF PRESENT) ─────────────────────────────────
        if (entity.HasAudio())
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(L("inspector.audio"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                DrawAudioInspector(app, entity);
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── LOCAL LIGHT (IF PRESENT) ─────────────────────────────────
        if (!app.Is2DWorldMode() && entity.HasLocalLight())
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(L("inspector.local_light"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                DrawLocalLightInspector(app, entity);
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── 2D PARTICLES IN 3D ─────────────────────────────────
        if (!app.Is2DWorldMode() && entity.HasParticleSystem())
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(UI("Partículas 2D##ParticleSystem"),
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                ParticleSystemComponent& particles = entity.GetParticleSystem();
                bool changed = ImGui::Checkbox(UI("Ativo##ParticleSystem"), &particles.Enabled);
                changed |= ImGui::SliderInt(UI("Partículas máximas##ParticleSystem"),
                    &particles.MaxParticles, 1, 512);
                changed |= ImGui::DragFloat(UI("Emissão por segundo##ParticleSystem"),
                    &particles.EmissionRate, 0.25f, 0.1f, 256.0f, "%.1f");
                changed |= ImGui::DragFloat(UI("Tempo de vida##ParticleSystem"),
                    &particles.Lifetime, 0.02f, 0.05f, 30.0f, "%.2f s");
                changed |= ImGui::DragFloat(UI("Velocidade##ParticleSystem"),
                    &particles.Speed, 0.02f, 0.0f, 100.0f, "%.2f");
                changed |= ImGui::SliderAngle(UI("Abertura##ParticleSystem"),
                    &particles.Spread, 0.0f, 180.0f, "%.0f deg");
                changed |= ImGui::DragFloat(UI("Tamanho inicial##ParticleSystem"),
                    &particles.StartSize, 0.005f, 0.001f, 20.0f, "%.3f");
                changed |= ImGui::DragFloat(UI("Tamanho final##ParticleSystem"),
                    &particles.EndSize, 0.005f, 0.001f, 20.0f, "%.3f");
                const char* particleShapes[] = { "Círculo", "Quadrado", "Triângulo", "Losango", "Pentágono", "Estrela" };
                changed |= ImGui::Combo(UI("Forma##ParticleSystem"), &particles.Shape,
                    particleShapes, IM_ARRAYSIZE(particleShapes));
                changed |= ImGui::SliderFloat(UI("Brilho aditivo##ParticleSystem"),
                    &particles.Glow, 0.0f, 5.0f, "%.2f");
                ImGui::SetNextItemWidth(-1.0f);
                const std::string textureDropLabel = particles.TexturePath.empty()
                    ? std::string(UI("Arraste uma imagem de partículas aqui"))
                    : std::filesystem::path(particles.TexturePath).filename().string();
                ImGui::Button(textureDropLabel.c_str(), ImVec2(-1.0f, 54.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        const char* droppedPath = static_cast<const char*>(payload->Data);
                        if (droppedPath && droppedPath[0] != '\0')
                        {
                            const std::string extension = [&]()
                            {
                                std::string ext = std::filesystem::path(droppedPath).extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(),
                                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                return ext;
                            }();
                            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                                extension == ".bmp" || extension == ".tga" || extension == ".webp")
                            {
                                particles.TexturePath = droppedPath;
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!particles.TexturePath.empty())
                {
                    if (ImGui::SmallButton(UI("Remover imagem##ParticleSystem")))
                    {
                        particles.TexturePath.clear();
                        changed = true;
                    }
                }
                float startColor[4] = { particles.StartColor.r, particles.StartColor.g,
                                        particles.StartColor.b, particles.StartColor.a };
                float endColor[4] = { particles.EndColor.r, particles.EndColor.g,
                                      particles.EndColor.b, particles.EndColor.a };
                if (ImGui::ColorEdit4(UI("Cor inicial##ParticleSystem"), startColor))
                {
                    particles.StartColor = glm::vec4(startColor[0], startColor[1], startColor[2], startColor[3]);
                    ForEachInspectorBatchComponent<ParticleSystemComponent>([&](ParticleSystemComponent& target)
                    {
                        target.StartColor = particles.StartColor;
                    });
                    changed = true;
                }
                if (ImGui::ColorEdit4(UI("Cor final##ParticleSystem"), endColor))
                {
                    particles.EndColor = glm::vec4(endColor[0], endColor[1], endColor[2], endColor[3]);
                    ForEachInspectorBatchComponent<ParticleSystemComponent>([&](ParticleSystemComponent& target)
                    {
                        target.EndColor = particles.EndColor;
                    });
                    changed = true;
                }
                float gravity[3] = { particles.Gravity.x, particles.Gravity.y, particles.Gravity.z };
                if (ImGui::DragFloat3(UI("Gravidade##ParticleSystem"), gravity, 0.01f, -100.0f, 100.0f, "%.2f"))
                {
                    particles.Gravity = glm::vec3(gravity[0], gravity[1], gravity[2]);
                    changed = true;
                }
                if (ImGui::TreeNode(UI("Emissão e rajadas##ParticleSystem")))
                {
                    const char* emissionShapes[] = { "Ponto", "Esfera", "Caixa" };
                    changed |= ImGui::Combo(UI("Região de emissão##ParticleSystem"),
                        &particles.EmissionShape, emissionShapes, IM_ARRAYSIZE(emissionShapes));
                    changed |= ImGui::SliderInt(UI("Partículas por rajada##ParticleSystem"),
                        &particles.BurstCount, 1, 64);
                    ImGui::TextDisabled(UI("A taxa define intervalos; cada intervalo emite uma rajada."));
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(UI("Variação##ParticleSystem")))
                {
                    changed |= ImGui::SliderFloat(UI("Variação de velocidade##ParticleSystem"),
                        &particles.SpeedVariation, 0.0f, 1.0f, "%.0f%%");
                    changed |= ImGui::SliderFloat(UI("Variação de duração##ParticleSystem"),
                        &particles.LifetimeVariation, 0.0f, 0.95f, "%.0f%%");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(UI("Forças e movimento##ParticleSystem")))
                {
                    float wind[3] = { particles.Wind.x, particles.Wind.y, particles.Wind.z };
                    if (ImGui::DragFloat3(UI("Vento##ParticleSystem"), wind, 0.01f, -100.0f, 100.0f, "%.2f"))
                    {
                        particles.Wind = glm::vec3(wind[0], wind[1], wind[2]);
                        changed = true;
                    }
                    changed |= ImGui::DragFloat(UI("Arrasto##ParticleSystem"),
                        &particles.Drag, 0.01f, 0.0f, 20.0f, "%.2f");
                    changed |= ImGui::DragFloat(UI("Turbulência##ParticleSystem"),
                        &particles.Turbulence, 0.01f, 0.0f, 50.0f, "%.2f");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(UI("Rotação##ParticleSystem")))
                {
                    changed |= ImGui::SliderAngle(UI("Rotação inicial##ParticleSystem"),
                        &particles.StartRotation, -360.0f, 360.0f, "%.0f deg");
                    changed |= ImGui::DragFloat(UI("Velocidade angular##ParticleSystem"),
                        &particles.RotationSpeed, 0.01f, -100.0f, 100.0f, "%.2f rad/s");
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(UI("Curva de vida##ParticleSystem")))
                {
                    changed |= ImGui::DragFloat(UI("Tamanho no meio da vida##ParticleSystem"),
                        &particles.MidSize, 0.005f, 0.0f, 20.0f, "%.3f");
                    if (particles.MidSize <= 0.0f)
                        ImGui::TextDisabled(UI("Tamanho linear entre início e fim (meio = 0)."));
                    changed |= ImGui::Checkbox(UI("Usar cor intermediária##ParticleSystem"), &particles.UseMidColor);
                    float midColor[4] = { particles.MidColor.r, particles.MidColor.g,
                                          particles.MidColor.b, particles.MidColor.a };
                    if (particles.UseMidColor && ImGui::ColorEdit4(UI("Cor no meio da vida##ParticleSystem"), midColor))
                    {
                        particles.MidColor = glm::vec4(midColor[0], midColor[1], midColor[2], midColor[3]);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(UI("Iluminação e desempenho##ParticleSystem")))
                {
                    changed |= ImGui::Checkbox(UI("Receber iluminação da cena##ParticleSystem"), &particles.Lit);
                    ImGui::TextDisabled(UI("Limite global: 4096 partículas por quadro. Partículas não projetam sombras."));
                    ImGui::TreePop();
                }
                ImGui::TextDisabled(UI("Sprites billboard. Colisão com meshes, módulos GPU e sub-emissores ainda não estão disponíveis."));
                ImGui::TextDisabled(UI("Brilho usa composição aditiva; para bloom, ative-o no Global Light."));
                particles.Clamp();
                if (changed) app.MarkDirty();
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── SCRIPT (always visible so a .cs asset can be attached by drag/drop) ──
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.88f));
            if (ImGui::CollapsingHeader(L("inspector.script"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PopStyleColor();
                DrawScriptInspector(app, entity);
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        // ── ADD COMPONENT — botão no rodapé, exatamente como Unity ────────
        ImGui::Dummy({0.f, 8.f});
        ImGui::Separator();
        ImGui::Dummy({0.f, 4.f});
        {
            // Centraliza o botão
            const float btnW = 200.f;
            float indent = (ImGui::GetContentRegionAvail().x - btnW) * 0.5f;
            if (indent > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.22f, 0.22f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.18f, 0.18f, 1.f));
            if (MaraeUI::IconButton(MaraeUI::EditorIcon::ComponentPlus,
                    UI("Adicionar Componente"), ImVec2(btnW, 0.f)))
                ImGui::OpenPopup("##inspector_add_component");
            ImGui::PopStyleColor(3);
        }

        // Popup de componentes — mesmo conteúdo do clique-direito da Hierarchy
        if (ImGui::BeginPopup("##inspector_add_component"))
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), L("component.add"));
            ImGui::Separator();

            // Criar formas/câmeras cria novas entidades na cena; não anexa
            // geometria ao objeto atualmente inspecionado.
            MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Cube,
                UI("Criar objeto na cena"), ImVec4(0.75f, 0.82f, 0.92f, 1.0f));
            ImGui::Separator();
            const auto createSceneObject = [&](const char* label, MaraeUI::EditorIcon icon,
                const auto& create)
            {
                if (MaraeUI::IconMenuItem(icon, label))
                {
                    create();
                    ImGui::CloseCurrentPopup();
                }
            };

            if (!app.Is2DWorldMode())
            {
                createSceneObject(UI("Cubo"), MaraeUI::EditorIcon::Cube, [&]() { app.CreateCubeEntity(); });
                createSceneObject(UI("Esfera"), MaraeUI::EditorIcon::Sphere, [&]() { app.CreateSphereEntity(); });
                createSceneObject(UI("Cilindro"), MaraeUI::EditorIcon::Cylinder, [&]() { app.CreateCylinderEntity(); });
                createSceneObject(UI("Cone"), MaraeUI::EditorIcon::Cone, [&]() { app.CreateConeEntity(); });
                createSceneObject(UI("Cápsula"), MaraeUI::EditorIcon::Capsule, [&]() { app.CreateCapsuleEntity(); });
                createSceneObject(UI("Torus"), MaraeUI::EditorIcon::Torus, [&]() { app.CreateTorusEntity(); });
                createSceneObject(UI("Wedge"), MaraeUI::EditorIcon::Wedge, [&]() { app.CreateWedgeEntity(); });
            }

            createSceneObject(UI("Triângulo 2D"), MaraeUI::EditorIcon::Triangle, [&]() { app.CreateTriangleEntity(); });
            createSceneObject(UI("Quad / Plano 2D"), MaraeUI::EditorIcon::Image, [&]() { app.CreateQuadEntity(); });
            if (!app.Is2DWorldMode())
            {
                createSceneObject(UI("Plano 3D"), MaraeUI::EditorIcon::Plane, [&]() { app.CreatePlaneEntity(); });
                createSceneObject(UI("Câmera"), MaraeUI::EditorIcon::Camera, [&]() { app.CreateCameraEntity(); });
            }
            ImGui::Dummy({0.f, 4.f});
            ImGui::Separator();

            // Script C#
            MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Script, "Script C#", ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
            ImGui::Separator();
            ScriptComponent& sc = entity.GetScriptComponent();
            if (sc.ScriptPath.empty())
            {
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Script, UI("Criar novo script"))) { app.CreateScriptForSelectedEntity(); ImGui::CloseCurrentPopup(); }
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Script, UI("Abrir no VS Code"))) { app.CreateScriptForSelectedEntity(); app.OpenSelectedScriptInVSCode(); ImGui::CloseCurrentPopup(); }
            }
            else
                ImGui::TextDisabled("  Script: %s", sc.ScriptPath.c_str());

            ImGui::Dummy({0.f, 4.f});

            // Luz (apenas 3D)
            if (!app.Is2DWorldMode())
            {
                MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Light, UI("Luz"), ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
                ImGui::Separator();
                if (!entity.HasLight())
                {
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Light, UI("Point Light"))) { entity.AddLight(LightType::Point); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::SpotLight, UI("Spot Light"))) { entity.AddLight(LightType::Spot); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::DirectionalLight, UI("Directional Light"))) { entity.AddLight(LightType::Directional); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::AreaLight, UI("Area Light"))) { entity.AddLight(LightType::Area); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                }
                else
                {
                    ImGui::TextDisabled("  Luz já adicionada");
                    if (ImGui::MenuItem(UI("  Remover Luz"))) { entity.RemoveLight(); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                }
                ImGui::Dummy({0.f, 4.f});
            }

            // Física usa controles próprios de acordo com o tipo de mundo.
            {
                MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Physics,
                    app.Is2DWorldMode() ? "Physics 2D" : "Physics",
                    ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
                ImGui::Separator();
                if (!entity.PhysicsEnabled)
                {
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Physics,
                            app.Is2DWorldMode() ? UI("Adicionar corpo 2D") : UI("Adicionar Rigidbody")))
                    { entity.PhysicsEnabled = true; app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                }
                else
                {
                    ImGui::TextDisabled(app.Is2DWorldMode() ? "  Corpo 2D já adicionado" : "  Rigidbody já adicionado");
                    if (ImGui::MenuItem(app.Is2DWorldMode() ? UI("  Remover corpo 2D") : UI("  Remover Rigidbody")))
                    { entity.PhysicsEnabled = false; app.MarkDirty(); ImGui::CloseCurrentPopup(); }
                }
                ImGui::Dummy({0.f, 4.f});
            }

            // Audio Source
            MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Audio, "Audio Source", ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
            ImGui::Separator();
            if (!entity.HasAudio())
            {
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Audio, UI("Adicionar Audio Source"))) { entity.AddAudio(); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
            }
            else
            {
                ImGui::TextDisabled("  Audio Source já adicionado");
                if (ImGui::MenuItem(UI("  Remover Audio Source"))) { app.StopAudioPreview(); entity.RemoveAudio(); app.MarkDirty(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::Dummy({0.f, 4.f});

            // Local Light
            if (!app.Is2DWorldMode())
            {
                MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Light, UI("Local Light"), ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
                ImGui::Separator();
                ImGui::TextDisabled(UI("Configura como este objeto recebe luz"));
                if (!entity.HasLocalLight())
                {
                    if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Light, UI("Adicionar Local Light")))
                    {
                        entity.AddLocalLight();
                        app.MarkDirty();
                        ImGui::CloseCurrentPopup();
                    }
                }
                else
                {
                    ImGui::TextDisabled("  Local Light já adicionado");
                    if (ImGui::MenuItem(UI("  Remover Local Light")))
                    {
                        entity.RemoveLocalLight();
                        app.MarkDirty();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::Dummy({0.f, 4.f});
            }

            if (!app.Is2DWorldMode())
            {
                MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Animation,
                    UI("Efeitos 2D"), ImVec4(0.55f, 0.82f, 1.0f, 1.0f));
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

            // Animation
            MaraeUI::DrawIconLabel(MaraeUI::EditorIcon::Animation, "Animation", ImVec4(1.0f, 0.6f, 0.8f, 1.0f));
            ImGui::Separator();
            AnimationComponent& anim = entity.GetAnimationComponent();
            if (!anim.Clip || anim.Clip->Name.empty())
            {
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Animation, UI("Criar Animation")))
                {
                    const Transform& t = entity.GetTransform();
                    anim.SetClip(AnimationClip::CreateDefault(
                        "New Animation", 5.0f,
                        glm::vec3(t.Position.X, t.Position.Y, t.Position.Z),
                        t.Rotation,
                        glm::vec3(t.Scale.X, t.Scale.Y, t.Scale.Z)));
                    app.MarkDirty();
                    ImGui::CloseCurrentPopup();
                }
            }
            else
                ImGui::TextDisabled("  Animation já adicionada");

            ImGui::EndPopup();
        }
        ImGui::Dummy({0.f, 4.f});
    }

    ImGui::End();
}

void InspectorPanel::DrawCameraInspector(Application& app, Entity& entity)
{
    CameraComponent& camera = entity.GetCameraComponent();
    bool changed = false;
    changed |= ImGui::Checkbox(UI("Ativa no runtime##Camera"), &camera.Enabled);
    DrawHelpMarker("CameraEnabled",
        "Define se esta é uma câmera disponível para assumir a visão da cena. "
        "A primeira câmera ativa encontrada é usada; se nenhuma estiver ativa, o fly camera permanece em uso.");
    changed |= ImGui::Checkbox(UI("Mostrar preview##Camera"), &camera.PreviewEnabled);
    DrawHelpMarker("CameraPreview",
        "Mostra no Inspector a imagem renderizada exatamente pela posição e orientação desta câmera.");
    changed |= ImGui::Checkbox(UI("Mostrar linhas na viewport (Gizmo/Frustum)##CameraGizmo"), &camera.ShowFrustumGizmo);
    DrawHelpMarker("CameraGizmo",
        "Desenha as linhas amareladas/azuis do corpo e pirâmide de projeção da câmera na viewport do editor.");
    if (camera.Orthographic)
        ImGui::TextDisabled("Projeção ortográfica");
    else
        ImGui::TextDisabled("Projeção perspectiva");

    changed |= ImGui::Checkbox(UI("Ortográfica##Camera"), &camera.Orthographic);
    DrawHelpMarker("CameraProjection",
        "Perspectiva faz objetos distantes parecerem menores. Ortográfica mantém o mesmo tamanho aparente "
        "e é útil para vistas técnicas, planta baixa ou interfaces 3D.");
    if (camera.Orthographic)
        changed |= ImGui::DragFloat(UI("Tamanho##Camera"), &camera.OrthographicSize, 0.05f, 0.1f, 10000.0f, "%.2f");
    else
        changed |= ImGui::SliderFloat(UI("Campo de visão##Camera"), &camera.FieldOfView, 1.0f, 179.0f, "%.1f°");
    changed |= ImGui::DragFloat(UI("Near Clip##Camera"), &camera.NearClip, 0.01f, 0.001f, 1000.0f, "%.3f");
    DrawHelpMarker("CameraNearClip",
        "Distância mínima visível a partir da câmera. Aumentar demais pode esconder objetos próximos.");
    changed |= ImGui::DragFloat(UI("Far Clip##Camera"), &camera.FarClip, 1.0f, 1.0f, 100000.0f, "%.1f");
    DrawHelpMarker("CameraFarClip",
        "Distância máxima visível. Use o menor valor que cubra a cena para melhorar a precisão do depth buffer.");
    camera.NearClip = std::max(0.001f, camera.NearClip);
    camera.FarClip = std::max(camera.NearClip + 0.01f, camera.FarClip);
    camera.FieldOfView = std::clamp(camera.FieldOfView, 1.0f, 179.0f);
    camera.OrthographicSize = std::max(0.1f, camera.OrthographicSize);
    if (changed)
        app.MarkDirty();

    if (camera.Enabled && camera.PreviewEnabled)
    {
        const float availableWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
        const float previewWidth = std::min(availableWidth, 420.0f);
        const float previewHeight = previewWidth * 9.0f / 16.0f;
        app.RenderCameraPreview(entity, static_cast<int>(previewWidth), static_cast<int>(previewHeight));
        const uint32_t texture = app.GetCameraPreviewTexture();
        if (texture != 0 && app.GetWindow().GetGraphicsAPI() == GraphicsAPI::OpenGL)
        {
            ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), "Preview da câmera");
            ImGui::Image((ImTextureID)(intptr_t)texture,
                         ImVec2(previewWidth, previewHeight),
                         ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else if (texture != 0)
            ImGui::TextDisabled("Preview usa OpenGL; indisponível na apresentação Vulkan.");
    }
    else
    {
        ImGui::TextDisabled("Ative a câmera e o preview para visualizar a imagem.");
    }
}

void InspectorPanel::DrawModelingInspector(Application& app, Entity& entity)
{
    MeshComponent& component = entity.GetMeshComponent();
    if (!component.MeshData || !component.MeshData->IsValid() ||
        component.MeshData->GetVertices().empty())
    {
        ImGui::TextWrapped(UI("Este objeto não possui uma malha editável."));
        ImGui::TextDisabled("Importe um modelo ou crie um primitivo para começar.");
        return;
    }

    if (s_ModelingEntityID != entity.GetID())
    {
        s_ModelingEntityID = entity.GetID();
        s_ModelingLocalMeshEntityID = 0;
        s_ModelingSelection.clear();
    }

    const Mesh* currentMesh = component.MeshData.get();
    const uint64_t currentRevision = currentMesh->GetRevision();
    if (s_ModelingStatsEntityID != entity.GetID() ||
        s_ModelingStatsMesh != currentMesh ||
        s_ModelingStatsRevision != currentRevision)
    {
        s_ModelingStatsTopology = MeshTopology::FromVertices(currentMesh->GetVertices());
        s_ModelingStatsEdgeCount = s_ModelingStatsTopology.GetEdges().size();
        s_ModelingStatsEntityID = entity.GetID();
        s_ModelingStatsMesh = currentMesh;
        s_ModelingStatsRevision = currentRevision;
    }
    const MeshTopology& topologySnapshot = s_ModelingStatsTopology;
    const size_t vertexCount = topologySnapshot.Vertices.size();
    const size_t edgeCount = s_ModelingStatsEdgeCount;
    const size_t faceCount = topologySnapshot.Faces.size();
    ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), "Modo Modelador");
    ImGui::TextWrapped(
        UI("Edite a malha diretamente no objeto. A primeira operação cria uma cópia local, "
           "para não alterar outros objetos que usam o mesmo mesh."));
    ImGui::Separator();

    MaterialComponent& modelingMaterialComponent = entity.GetMaterialComponent();
    Material* modelingMaterial = MaterialDatabase::GetMaterialByGUID(
        modelingMaterialComponent.MaterialGUID);
    if (!modelingMaterial)
        modelingMaterial = &modelingMaterialComponent.MaterialData;
    if (ImGui::CollapsingHeader(UI("Material em tempo real##ModelingMaterial"),
                               ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("O material é o mesmo asset usado pela cena e pelo jogo.");
        ColorComponent& modelingColor = entity.GetColorComponent();
        float baseColor[3] = {
            modelingColor.Color.X, modelingColor.Color.Y, modelingColor.Color.Z };
        if (ImGui::ColorEdit3("Base Color##ModelingMaterial", baseColor))
        {
            modelingColor.Color = Vector3(baseColor[0], baseColor[1], baseColor[2]);
            app.MarkDirty();
        }
        if (ImGui::Checkbox(UI("PBR personalizado##ModelingMaterial"),
                            &modelingMaterial->PBRParametersEnabled))
            app.MarkDirty();
        if (modelingMaterial->PBRParametersEnabled)
        {
            bool changed = false;
            changed |= ImGui::SliderFloat(UI("Roughness##ModelingMaterial"),
                                          &modelingMaterial->Roughness, 0.02f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat(UI("Metallic##ModelingMaterial"),
                                          &modelingMaterial->Metallic, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat(UI("Emission##ModelingMaterial"),
                                          &modelingMaterial->EmissionStrength, 0.0f, 10.0f, "%.3f");
            changed |= ImGui::SliderFloat(UI("Alpha##ModelingMaterial"),
                                          &modelingMaterial->Alpha, 0.0f, 1.0f, "%.3f");
            char normalMap[512];
            strcpy_s(normalMap, sizeof(normalMap), modelingMaterial->NormalMapPath.c_str());
            if (ImGui::InputText(UI("Normal Map##ModelingMaterial"), normalMap, sizeof(normalMap)))
            {
                modelingMaterial->NormalMapPath = normalMap;
                changed = true;
            }
            if (changed)
                app.MarkDirty();
        }
    }

    ImGui::TextUnformatted(UI("Seleção"));
    DrawHelpMarker("SelectionScope",
        "Escolha o tipo de componente que será selecionado na Viewport. "
        "Vértices são pontos, arestas são linhas e faces são triângulos. "
        "Clique na malha ou arraste um retângulo para selecionar.");
    if (ImGui::Button(UI("Vértices##ModelSelection")))
    {
        s_ModelingSelectionMode = 0;
        s_ModelingSelection.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(UI("Arestas##ModelSelection")))
    {
        s_ModelingSelectionMode = 1;
        s_ModelingSelection.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(UI("Faces##ModelSelection")))
    {
        s_ModelingSelectionMode = 2;
        s_ModelingSelection.clear();
    }
    ImGui::TextDisabled(
        "Escopo atual: %s | %zu selecionado(s) | %zu vértices / %zu arestas / %zu faces",
        s_ModelingSelectionMode == 0 ? "vértices" :
        s_ModelingSelectionMode == 1 ? "arestas" : "faces",
        s_ModelingSelection.size(),
        vertexCount,
        edgeCount,
        faceCount);

    if (ImGui::SmallButton(UI("Selecionar tudo##ModelSelection")))
    {
        s_ModelingSelection.clear();
        const size_t count = s_ModelingSelectionMode == 0 ? vertexCount :
            s_ModelingSelectionMode == 1 ? edgeCount : faceCount;
        s_ModelingSelection.reserve(count);
        for (size_t index = 0; index < count; ++index)
            s_ModelingSelection.push_back(static_cast<int>(index));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Limpar seleção##ModelSelection")))
        s_ModelingSelection.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("Inverter##ModelSelection")))
    {
        const size_t count = s_ModelingSelectionMode == 0 ? vertexCount :
            s_ModelingSelectionMode == 1 ? edgeCount : faceCount;
        std::vector<int> inverted;
        for (size_t index = 0; index < count; ++index)
        {
            if (std::find(s_ModelingSelection.begin(), s_ModelingSelection.end(),
                          static_cast<int>(index)) == s_ModelingSelection.end())
                inverted.push_back(static_cast<int>(index));
        }
        s_ModelingSelection = std::move(inverted);
    }

    // ── DIVISÃO DE MINI-FACES (GRADE E FORMAS) ──────────────────────────
    if (ImGui::CollapsingHeader(UI("Divisão de Mini-Faces (Grade e Formas)##MiniFacesHeader"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Configure as formas e o número de linhas verticais/horizontais desenhadas nas faces.");

        const char* shapeItems[] = { "Quadrados (Quads)", "Retângulos", "Triângulos" };
        int currentShape = static_cast<int>(s_MiniFaceShape);
        if (ImGui::Combo(UI("Forma das Mini-Faces##MiniFaceShapeCombo"), &currentShape, shapeItems, IM_ARRAYSIZE(shapeItems)))
        {
            s_MiniFaceShape = static_cast<MiniFaceShape>(currentShape);
            app.MarkDirty();
        }

        const char* orientItems[] = { "Grade (Vertical e Horizontal)", "Vertical", "Horizontal" };
        int currentOrient = static_cast<int>(s_MiniFaceOrientation);
        if (ImGui::Combo(UI("Orientação de Desenho##MiniFaceOrientCombo"), &currentOrient, orientItems, IM_ARRAYSIZE(orientItems)))
        {
            s_MiniFaceOrientation = static_cast<MiniFaceOrientation>(currentOrient);
            app.MarkDirty();
        }

        if (s_MiniFaceOrientation != MiniFaceOrientation::Horizontal)
        {
            if (ImGui::SliderInt(UI("Linhas Verticais##MiniFaceVLines"), &s_MiniFaceVerticalLines, 0, 32))
            {
                if (s_MiniFaceShape == MiniFaceShape::Squares && s_MiniFaceOrientation == MiniFaceOrientation::Grid)
                    s_MiniFaceHorizontalLines = s_MiniFaceVerticalLines;
                app.MarkDirty();
            }
        }
        if (s_MiniFaceOrientation != MiniFaceOrientation::Vertical)
        {
            if (ImGui::SliderInt(UI("Linhas Horizontais##MiniFaceHLines"), &s_MiniFaceHorizontalLines, 0, 32))
            {
                if (s_MiniFaceShape == MiniFaceShape::Squares && s_MiniFaceOrientation == MiniFaceOrientation::Grid)
                    s_MiniFaceVerticalLines = s_MiniFaceHorizontalLines;
                app.MarkDirty();
            }
        }

        if (ImGui::Checkbox(UI("Ocultar diagonais internas (evita triângulo dentro de quadrado)##HideDiags"), &s_HideDiagonalLines))
        {
            app.MarkDirty();
        }

        ImGui::Spacing();
        if (ImGui::Button(UI("Subdividir Faces Selecionadas em Mini-Faces##SubdivMiniFaceSel"), ImVec2(-1.0f, 0.0f)))
        {
            SubdivideSelectedIntoMiniFaces(app, entity);
        }
        if (ImGui::Button(UI("Subdividir Cubo / Objeto em Mini-Faces##SubdivMiniFaceObj"), ImVec2(-1.0f, 0.0f)))
        {
            SubdivideObjectIntoMiniFaces(app, entity);
        }
    }

    // ── PINTURA E CORES DE FACES / PINCEL 3D ────────────────
    if (ImGui::CollapsingHeader(UI("Pintura e Cores (Faces & Pincel 3D)##PaintingSection"),
                               ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Pinte faces selecionadas ou ative o pincel para desenhar livremente na malha.");
        DrawHelpMarker("PaintingSection",
            "Pintar Faces Selecionadas aplica uma cor sólida às faces escolhidas. "
            "O Pincel 3D pinta diretamente a textura do objeto na Viewport, sem alterar a geometria.");

        glm::vec4& brushCol = MeshPainter::GetBrushColor();
        float colArr[4] = { brushCol.r, brushCol.g, brushCol.b, brushCol.a };
        if (ImGui::ColorEdit4("Cor da Tinta##BrushCol", colArr, ImGuiColorEditFlags_NoInputs))
        {
            brushCol = glm::vec4(colArr[0], colArr[1], colArr[2], colArr[3]);
        }

        // Paleta rápida de 8 cores
        const ImVec4 palette[] = {
            ImVec4(0.95f, 0.20f, 0.20f, 1.0f), // Vermelho
            ImVec4(0.20f, 0.85f, 0.30f, 1.0f), // Verde
            ImVec4(0.20f, 0.55f, 1.00f, 1.0f), // Azul
            ImVec4(1.00f, 0.88f, 0.15f, 1.0f), // Amarelo
            ImVec4(1.00f, 0.50f, 0.10f, 1.0f), // Laranja
            ImVec4(0.75f, 0.30f, 0.95f, 1.0f), // Roxo
            ImVec4(0.95f, 0.95f, 0.95f, 1.0f), // Branco
            ImVec4(0.12f, 0.12f, 0.12f, 1.0f), // Preto
        };
        for (int p = 0; p < 8; ++p)
        {
            if (p > 0) ImGui::SameLine();
            char pId[16];
            snprintf(pId, sizeof(pId), "##pal_%d", p);
            if (ImGui::ColorButton(pId, palette[p], 0, ImVec2(22, 22)))
            {
                brushCol = glm::vec4(palette[p].x, palette[p].y, palette[p].z, palette[p].w);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        // Botão para pintar faces selecionadas
        const bool canPaintFaces = (s_ModelingSelectionMode == 2 && !s_ModelingSelection.empty());
        if (!canPaintFaces) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.50f, 0.85f, 1.0f));
        if (ImGui::Button(UI("Pintar Faces Selecionadas##PaintFacesBtn"), ImVec2(-1.0f, 26.0f)))
        {
            MeshPainter::PaintSelectedFaces(entity, s_ModelingSelection, brushCol);
            app.MarkDirty();
        }
        ImGui::PopStyleColor();
        if (!canPaintFaces) ImGui::EndDisabled();

        ImGui::Separator();

        // Modo Pincel 3D
        bool brushActive = MeshPainter::IsBrushActive();
        if (ImGui::Checkbox(UI("Modo Pincel (Desenho 3D na Viewport)##BrushToggle"), &brushActive))
        {
            MeshPainter::SetBrushActive(brushActive);
            if (brushActive)
            {
                MeshPainter::GetOrCreateCanvas(entity);
                app.MarkDirty();
            }
        }
        DrawHelpMarker("BrushToggle",
            "Quando ativado, clique e arraste sobre a superfície do objeto na Viewport para pintar. "
            "Ao trocar para Inspector, o modo é desligado automaticamente para evitar pintura acidental.");

        if (brushActive)
        {
            ImGui::Indent();
            ImGui::SliderFloat(UI("Tamanho do Pincel##BrushRadius"), &MeshPainter::GetBrushRadius(), 2.0f, 60.0f, "%.1f px");
            DrawHelpMarker("BrushRadius",
                "Define o raio do pincel em pixels da textura. Valores maiores pintam uma área maior.");
            ImGui::SliderFloat(UI("Suavidade (Falloff)##BrushSoft"), &MeshPainter::GetBrushSoftness(), 0.0f, 1.0f, "%.2f");
            DrawHelpMarker("BrushSoftness",
                "Controla a suavidade da borda: 0 pinta com borda firme; 1 cria uma transição suave até a borda.");
            ImGui::SliderFloat(UI("Opacidade##BrushOpacity"), &MeshPainter::GetBrushOpacity(), 0.05f, 1.0f, "%.2f");
            DrawHelpMarker("BrushOpacity",
                "Define quanto da nova cor mistura com a cor existente a cada passada do pincel.");

            bool ruler = MeshPainter::IsRulerMode();
            if (ImGui::Checkbox(UI("Régua (Linha Reta)##RulerCheck"), &ruler))
            {
                MeshPainter::SetRulerMode(ruler);
            }
            DrawHelpMarker("BrushRuler",
                "Com a Régua ativa, o primeiro clique define o início e o segundo define o fim de uma linha reta. "
                "Também é possível segurar Shift temporariamente.");
            ImGui::TextDisabled("Dica: você também pode segurar Shift na Viewport para traçar linha reta.");
            ImGui::Unindent();
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ImGui::Button(UI("Preencher Objeto##FillAll"), ImVec2(140.0f, 0.0f)))
        {
            MeshPainter::FillEntity(entity, brushCol);
            app.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI("Limpar (Branco)##ClearPaint"), ImVec2(-1.0f, 0.0f)))
        {
            MeshPainter::FillEntity(entity, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            app.MarkDirty();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Operações da malha"));
    ImGui::SliderFloat(UI("Intensidade##ModelAmount"), &s_ModelingAmount, 0.01f, 2.0f, "%.2f");

    std::vector<int> nextSelection;
    auto applyMesh = [&](std::vector<Vertex> vertices, const char* tag)
    {
        if (vertices.empty() || vertices.size() > 1000000)
            return;
        const std::vector<Vertex> beforeVertices = component.MeshData->GetVertices();
        const Transform beforeTransform = entity.GetTransform();
        Mesh* editableMesh = EnsureEditableMesh(app, entity);
        if (!editableMesh)
            return;
        editableMesh->UpdateVertices(vertices, tag);
        SaveGeneratedMeshAsset(app, entity, *editableMesh);

        TransformCommand command;
        command.Type = CommandType::Transform;
        command.EntityID = entity.GetID();
        command.EntityIndex = app.GetSelectedEntityIndex();
        command.Before = beforeTransform;
        command.After = entity.GetTransform();
        command.HasMeshData = true;
        command.MeshBefore = beforeVertices;
        command.MeshAfter = std::move(vertices);
        app.GetUndoRedoManager().Push(command);
        // Índices de faces deixam de ser confiáveis após alterar a topologia se não forem atualizados.
        if (nextSelection.empty())
            s_ModelingSelection.clear();
        else
            s_ModelingSelection = nextSelection;
        app.MarkDirty();
    };

    // As ferramentas novas operam no domínio topológico compartilhado e só
    // convertem de volta para o formato do renderer no fim da transação. Isso
    // mantém Undo/Redo, assets e o objeto da cena no mesmo fluxo.
    auto applyTopology = [&](auto operation, const char* tag)
    {
        nextSelection.clear();
        MeshTopology mesh = MeshTopology::FromVertices(component.MeshData->GetVertices());
        ModelingSelection selection;
        selection.Mode = static_cast<ModelingSelectionMode>(s_ModelingSelectionMode);
        selection.Indices = s_ModelingSelection;
        if constexpr (std::is_void_v<decltype(operation(mesh, selection))>)
        {
            operation(mesh, selection);
        }
        else
        {
            nextSelection = operation(mesh, selection);
        }
        if (!mesh.IsValid())
            return;
        applyMesh(mesh.ToVertices(false), tag);
    };

    if (ImGui::CollapsingHeader(UI("Ferramentas de topologia##ModelingTopology"),
                               ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled(
            "Edição integrada: o resultado continua sendo uma Mesh da cena e pode ser usado imediatamente em materiais, física e gameplay.");
        ImGui::Checkbox(UI("Edição proporcional##ModelingTopology"), &s_ModelingProportional);
        DrawHelpMarker("ProportionalEdit",
            "A edição proporcional também move, gira ou escala componentes próximos da seleção. "
            "O Raio define até que distância essa influência alcança.");
        ImGui::SameLine();
        ImGui::DragFloat(UI("Raio##ModelingTopology"), &s_ModelingProportionalRadius,
                         0.01f, 0.01f, 100.0f, "%.2f");

        ImGui::DragFloat3("Mover##ModelingTransform", &s_ModelingTranslation.x,
                          0.01f, -1000.0f, 1000.0f, "%.3f");
        ImGui::DragFloat3("Girar (graus)##ModelingTransform", &s_ModelingRotation.x,
                          0.25f, -360.0f, 360.0f, "%.2f");
        ImGui::TextUnformatted(UI("Escalar seleção"));
        const float modelingScaleAvail = ImGui::GetContentRegionAvail().x;
        const float modelingScaleGap = ImGui::GetStyle().ItemSpacing.x;
        const float modelingResetWidth = 38.0f;
        const float modelingLockWidth = std::max(1.0f, modelingScaleAvail - modelingResetWidth - modelingScaleGap);
        if (MaraeUI::IconButton(s_ModelingScaleUniform ? MaraeUI::EditorIcon::Lock : MaraeUI::EditorIcon::Unlock,
                s_ModelingScaleUniform ? UI("Proporcional##ModSclLock") : UI("Escala livre##ModSclLock"),
                ImVec2(modelingLockWidth, 0.0f)))
        {
            s_ModelingScaleUniform = !s_ModelingScaleUniform;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Trava ou libera proporção uniforme da escala ao transformar a seleção.");
        }
        ImGui::SameLine();
        if (MaraeUI::IconButton(MaraeUI::EditorIcon::Reset,
                UI("##ResetModScale"), ImVec2(modelingResetWidth, 0.0f)))
        {
            s_ModelingScale = glm::vec3(1.0f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Redefine a escala da seleção para 1 × 1 × 1."));

        glm::vec3 oldModScale = s_ModelingScale;
        if (ImGui::DragFloat3("Escalar##ModelingTransform", &s_ModelingScale.x,
                              0.01f, 0.001f, 100.0f, "%.3f"))
        {
            if (s_ModelingScaleUniform)
            {
                float ratio = 1.0f;
                if (std::abs(s_ModelingScale.x - oldModScale.x) > 0.00001f && std::abs(oldModScale.x) > 0.0001f)
                    ratio = s_ModelingScale.x / oldModScale.x;
                else if (std::abs(s_ModelingScale.y - oldModScale.y) > 0.00001f && std::abs(oldModScale.y) > 0.0001f)
                    ratio = s_ModelingScale.y / oldModScale.y;
                else if (std::abs(s_ModelingScale.z - oldModScale.z) > 0.00001f && std::abs(oldModScale.z) > 0.0001f)
                    ratio = s_ModelingScale.z / oldModScale.z;

                s_ModelingScale = glm::vec3(
                    std::max(0.001f, oldModScale.x * ratio),
                    std::max(0.001f, oldModScale.y * ratio),
                    std::max(0.001f, oldModScale.z * ratio));
            }
        }
        if (ImGui::Button(UI("Aplicar transformação precisa##ModelingTransform"), ImVec2(-1.0f, 0.0f)))
        {
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            {
                ModelingToolset::TransformSelected(
                    mesh, selection, s_ModelingTranslation, s_ModelingScale,
                    s_ModelingRotation, s_ModelingProportional,
                    s_ModelingProportionalRadius);
            }, "Modeling::TransformSelected");
        }

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Operações de topologia"));
        DrawHelpMarker("TopologyOperations",
            "Extrude cria uma nova região a partir das faces. Inset cria uma borda interna. "
            "Bevel arredonda ou chanfrar as bordas. Loop Cut adiciona uma divisão e Knife corta a malha.");
        // Atalhos de teclado no topo das ferramentas
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.22f, 0.60f));
        ImGui::BeginChild("ModelingShortcutsBanner", ImVec2(0.0f, 62.0f), true);
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "Atalhos de Modelagem:");
        ImGui::TextDisabled("1: Vértice | 2: Aresta | 3: Face | W/E/R: Gizmos | Ctrl+E: Extrude | I: Inset | B: Bevel");
        ImGui::TextDisabled("Ctrl+R: LoopCut | K: Knife | M: Merge | F: Fill | Del/X: Excluir | Ctrl+A: Tudo");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::Button(UI("Extrude (Ctrl+E)##ModelingTopology"), ImVec2(136.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { return ModelingToolset::ExtrudeSelectedFaces(mesh, selection, s_ModelingAmount); },
                          "Modeling::TopologyExtrude");
        ImGui::SameLine();
        if (ImGui::Button(UI("Inset (I)##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::InsetSelectedFaces(mesh, selection, s_ModelingAmount); },
                          "Modeling::TopologyInset");
        ImGui::SameLine();
        if (ImGui::Button(UI("Bevel (B)##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::BevelSelected(mesh, selection, s_ModelingAmount); },
                          "Modeling::TopologyBevel");

        if (ImGui::Button(UI("Loop Cut (Ctrl+R)##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::LoopCut(mesh, selection); }, "Modeling::LoopCut");
        ImGui::SameLine();
        if (ImGui::Button(UI("Knife (K)##ModelingTopology"), ImVec2(96.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::Knife(mesh, selection); }, "Modeling::Knife");
        ImGui::SameLine();
        if (ImGui::Button(UI("Slide##ModelingTopology"), ImVec2(96.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::Slide(mesh, selection, s_ModelingAmount); }, "Modeling::Slide");

        if (ImGui::Button(UI("Merge (M)##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::MergeSelected(mesh, selection); }, "Modeling::Merge");
        ImGui::SameLine();
        if (ImGui::Button(UI("Dissolve##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::DissolveSelected(mesh, selection); }, "Modeling::Dissolve");
        ImGui::SameLine();
        if (ImGui::Button(UI("Fill (F)##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::FillSelected(mesh, selection); }, "Modeling::Fill");

        // Novas operações de modelagem solicitadas
        if (ImGui::Button(UI("Aplanar (Flatten)##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::FlattenSelected(mesh, selection, -1); }, "Modeling::Flatten");
        ImGui::SameLine();
        if (ImGui::Button(UI("Esferizar (Spherize)##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::SpherizeSelected(mesh, selection, 1.0f); }, "Modeling::Spherize");

        if (ImGui::Button(UI("Poke Faces (Alt+P)##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::PokeSelectedFaces(mesh, selection, 0.0f); }, "Modeling::Poke");
        ImGui::SameLine();
        if (ImGui::Button(UI("Soldar por distância##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::WeldByDistance(mesh, 0.02f); }, "Modeling::Weld");

        if (ImGui::Button(UI("Torção Y (Twist)##ModelingTopology"), ImVec2(150.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::TwistSelected(mesh, selection, 1, 45.0f); }, "Modeling::Twist");

        if (ImGui::Button(UI("Bridge 2 arestas##ModelingTopology"), ImVec2(180.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::BridgeSelectedEdges(mesh, selection); }, "Modeling::Bridge");
        ImGui::SameLine();
        if (ImGui::Button(UI("Duplicar##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection& selection)
            { ModelingToolset::DuplicateSelected(mesh, selection, s_ModelingArrayOffset); },
                          "Modeling::Duplicate");

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Modificadores integrados"));
        if (ImGui::Button(UI("Espelho X (topologia)##ModelingTopology")))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::Mirror(mesh, 0); }, "Modeling::MirrorX");
        ImGui::SameLine();
        if (ImGui::Button(UI("Espelho Y##ModelingTopology")))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::Mirror(mesh, 1); }, "Modeling::MirrorY");
        ImGui::SameLine();
        if (ImGui::Button(UI("Espelho Z##ModelingTopology")))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::Mirror(mesh, 2); }, "Modeling::MirrorZ");

        ImGui::DragInt(UI("Quantidade do Array##ModelingTopology"), &s_ModelingArrayCount,
                       1.0f, 1, 32);
        ImGui::DragFloat3("Deslocamento do Array##ModelingTopology", &s_ModelingArrayOffset.x,
                          0.01f, -1000.0f, 1000.0f, "%.3f");
        if (ImGui::Button(UI("Aplicar Array##ModelingTopology"), ImVec2(-1.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::Array(mesh, s_ModelingArrayCount, s_ModelingArrayOffset); },
                          "Modeling::Array");

        ImGui::DragFloat(UI("Espessura Solidify##ModelingTopology"), &s_ModelingSolidifyThickness,
                         0.005f, 0.001f, 10.0f, "%.3f");
        if (ImGui::Button(UI("Solidify##ModelingTopology"), ImVec2(118.0f, 0.0f)))
            applyTopology([&](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::Solidify(mesh, s_ModelingSolidifyThickness); },
                          "Modeling::Solidify");

        int booleanOperandIndex = -1;
        for (int index : app.GetMultiSelectedEntities())
        {
            if (index == app.GetSelectedEntityIndex() || index < 0 ||
                index >= static_cast<int>(app.GetScene().GetEntities().size()))
                continue;
            const Entity& candidate = app.GetScene().GetEntities()[static_cast<size_t>(index)];
            Entity& mutableCandidate = const_cast<Entity&>(candidate);
            if (mutableCandidate.GetMeshComponent().MeshData &&
                mutableCandidate.GetMeshComponent().MeshData->IsValid())
            {
                booleanOperandIndex = index;
                break;
            }
        }
        if (booleanOperandIndex >= 0)
        {
            const Entity& operandEntity = app.GetScene().GetEntities()[
                static_cast<size_t>(booleanOperandIndex)];
            Entity& mutableOperandEntity = const_cast<Entity&>(operandEntity);
            MeshTopology booleanOperand = MeshTopology::FromVertices(
                mutableOperandEntity.GetMeshComponent().MeshData->GetVertices());
            const glm::mat4 targetModel = (entity.GetParentID() != 0) ? entity.GetTransform().WorldMatrix : BuildModelingTransform(entity.GetTransform());
            const glm::mat4 operandModel = (mutableOperandEntity.GetParentID() != 0) ? mutableOperandEntity.GetTransform().WorldMatrix : BuildModelingTransform(mutableOperandEntity.GetTransform());
            const glm::mat4 relative = glm::inverse(targetModel) * operandModel;
            for (auto& vertex : booleanOperand.Vertices)
            {
                const glm::vec3 local(vertex.Value.X, vertex.Value.Y, vertex.Value.Z);
                const glm::vec3 transformed = glm::vec3(relative * glm::vec4(local, 1.0f));
                vertex.Value.X = transformed.x;
                vertex.Value.Y = transformed.y;
                vertex.Value.Z = transformed.z;
            }
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Boolean com a segunda malha selecionada"));
            if (ImGui::Button(UI("Boolean Union##ModelingTopology"), ImVec2(150.0f, 0.0f)))
                applyTopology([&](MeshTopology& mesh, const ModelingSelection&)
                { ModelingToolset::BooleanCombine(mesh, booleanOperand, ModelingToolset::BooleanMode::Union); },
                              "Modeling::BooleanUnion");
            ImGui::SameLine();
            if (ImGui::Button(UI("Difference##ModelingTopology"), ImVec2(150.0f, 0.0f)))
                applyTopology([&](MeshTopology& mesh, const ModelingSelection&)
                { ModelingToolset::BooleanCombine(mesh, booleanOperand, ModelingToolset::BooleanMode::Difference); },
                              "Modeling::BooleanDifference");
            ImGui::SameLine();
            if (ImGui::Button(UI("Intersection##ModelingTopology"), ImVec2(150.0f, 0.0f)))
                applyTopology([&](MeshTopology& mesh, const ModelingSelection&)
                { ModelingToolset::BooleanCombine(mesh, booleanOperand, ModelingToolset::BooleanMode::Intersection); },
                              "Modeling::BooleanIntersection");
        }
        else
        {
            ImGui::TextDisabled("Boolean: selecione duas entidades com malha na Hierarquia.");
        }

        if (ImGui::Button(UI("Flat Shading##ModelingTopology"), ImVec2(180.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::SetFlatShading(mesh); }, "Modeling::FlatShading");
        ImGui::SameLine();
        if (ImGui::Button(UI("Smooth Shading##ModelingTopology"), ImVec2(180.0f, 0.0f)))
            applyTopology([](MeshTopology& mesh, const ModelingSelection&)
            { ModelingToolset::SetSmoothShading(mesh); }, "Modeling::SmoothShading");
    }

    const bool hasFaceSelection =
        s_ModelingSelectionMode == 2 && !s_ModelingSelection.empty();

    if (ImGui::Button(UI("Recalcular normais##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::RecalculateNormals(vertices);
        applyMesh(std::move(vertices), "Modeling::RecalculateNormals");
    }
    if (ImGui::Button(UI("Suavizar normais##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::Smooth(vertices, std::clamp(s_ModelingAmount, 0.0f, 1.0f));
        applyMesh(std::move(vertices), "Modeling::Smooth");
    }
    if (ImGui::Button(UI("Inverter normais##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::FlipNormals(vertices);
        applyMesh(std::move(vertices), "Modeling::FlipNormals");
    }

    if (ImGui::Button(UI("Aumentar volume da mesh##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = hasFaceSelection
            ? ModelingOperations::InflateSelected(component.MeshData->GetVertices(),
                                                   std::abs(s_ModelingAmount), s_ModelingSelection)
            : component.MeshData->GetVertices();
        if (!hasFaceSelection)
            ModelingOperations::Inflate(vertices, std::abs(s_ModelingAmount));
        applyMesh(std::move(vertices), "Modeling::Inflate");
    }
    if (ImGui::Button(UI("Diminuir volume da mesh##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = hasFaceSelection
            ? ModelingOperations::InflateSelected(component.MeshData->GetVertices(),
                                                   -std::abs(s_ModelingAmount), s_ModelingSelection)
            : component.MeshData->GetVertices();
        if (!hasFaceSelection)
            ModelingOperations::Inflate(vertices, -std::abs(s_ModelingAmount));
        applyMesh(std::move(vertices), "Modeling::Deflate");
    }

    if (ImGui::Button(UI("Extrudar faces##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        applyMesh(
            hasFaceSelection
                ? ModelingOperations::ExtrudeFacesSelected(
                    component.MeshData->GetVertices(), s_ModelingAmount,
                    s_ModelingSelection)
                : ModelingOperations::ExtrudeFaces(
                    component.MeshData->GetVertices(), s_ModelingAmount),
            "Modeling::ExtrudeFaces");
    }
    if (ImGui::Button(UI("Bevel / chanfrar##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        applyMesh(
            hasFaceSelection
                ? ModelingOperations::BevelFacesSelected(
                    component.MeshData->GetVertices(),
                    std::clamp(s_ModelingAmount, 0.0f, 0.5f),
                    s_ModelingSelection)
                : ModelingOperations::BevelFaces(
                    component.MeshData->GetVertices(),
                    std::clamp(s_ModelingAmount, 0.0f, 0.5f)),
            "Modeling::BevelFaces");
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.40f, 0.65f, 0.85f));
    const bool polygonDetailsOpen = ImGui::CollapsingHeader(
        UI("Quantidade de Polígonos e Resolução##ModelPolyDetail"),
        ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (polygonDetailsOpen)
    {
        const size_t renderVertexCount = component.MeshData->GetVertexCount();
        const size_t triangleCount = renderVertexCount / 3;
        ImGui::Text(UI("%zu triângulos | %zu vértices únicos | %zu vértices renderizados"),
                    triangleCount, vertexCount, renderVertexCount);
        ImGui::TextDisabled("Alterações de topologia entram no Undo/Redo e são salvas como mesh local.");

        size_t selectedFaceCount = 0;
        if (hasFaceSelection)
        {
            std::unordered_set<int> validFaces;
            for (int face : s_ModelingSelection)
                if (face >= 0 && static_cast<size_t>(face) < faceCount)
                    validFaces.insert(face);
            selectedFaceCount = validFaces.size();
        }

        int maxDensityLevels = 0;
        size_t projectedVertices = renderVertexCount;
        if (!hasFaceSelection)
        {
            for (int level = 1; level <= 3; ++level)
            {
                if (projectedVertices > kMaxModelingVertices / 4)
                    break;
                projectedVertices *= 4;
                maxDensityLevels = level;
            }
            if (maxDensityLevels > 0)
                s_ModelingResolutionLevels = std::clamp(
                    s_ModelingResolutionLevels, 1, maxDensityLevels);
        }
        else if (renderVertexCount <= kMaxModelingVertices &&
                 selectedFaceCount <= (kMaxModelingVertices - renderVertexCount) / 9)
        {
            projectedVertices = renderVertexCount + selectedFaceCount * 9;
            maxDensityLevels = 1;
        }

        ImGui::Separator();
        if (hasFaceSelection)
        {
            ImGui::Text(UI("Subdivisão da seleção: %zu faces → estimativa de %zu vértices"),
                        selectedFaceCount, projectedVertices);
        }
        else if (maxDensityLevels > 0)
        {
            ImGui::SliderInt(UI("Níveis de subdivisão##ModelResolution"),
                             &s_ModelingResolutionLevels, 1, maxDensityLevels);
            projectedVertices = renderVertexCount;
            for (int level = 0; level < s_ModelingResolutionLevels; ++level)
                projectedVertices *= 4;
            ImGui::TextDisabled("Resultado estimado: %zu vértices (limite seguro: %zu).",
                                projectedVertices, kMaxModelingVertices);
        }
        else
        {
            ImGui::TextDisabled("Mesh acima do limite seguro para subdivisão (%zu vértices).",
                                kMaxModelingVertices);
        }

        const bool canSubdivide = maxDensityLevels > 0;
        if (!canSubdivide) ImGui::BeginDisabled();
        const char* subdivideLabel = hasFaceSelection
            ? UI("Subdividir faces selecionadas##ModelResolution")
            : UI("Aumentar densidade da malha##ModelResolution");
        if (ImGui::Button(subdivideLabel, ImVec2(-1.0f, 0.0f)))
        {
            if (hasFaceSelection)
            {
                applyMesh(ModelingOperations::SubdivideSelected(
                    component.MeshData->GetVertices(), s_ModelingSelection),
                    "Modeling::SubdivideSelected");
            }
            else
            {
                applyMesh(ModelingOperations::IncreaseResolution(
                    component.MeshData->GetVertices(), s_ModelingResolutionLevels),
                    "Modeling::IncreaseResolution");
            }
        }
        if (!canSubdivide) ImGui::EndDisabled();
        if (hasFaceSelection)
            ImGui::TextDisabled("Cada triângulo selecionado vira quatro; as normais de origem são preservadas.");
        else
            ImGui::TextDisabled("A subdivisão aumenta a densidade, mas não arredonda nem suaviza o contorno.");

        ImGui::Separator();
        ImGui::TextUnformatted(UI("Simplificação segura"));
        ImGui::TextWrapped("Colapsa arestas curtas e protege a forma; a proporção é aproximada e pode não ser atingida em malhas que exigiriam abrir buracos.");
        ImGui::DragFloat(UI("Proporção de triângulos##ModelPolyDecimate"),
                         &s_ModelingDecimateRatio, 0.01f, 0.05f, 1.0f, "%.2f");
        if (ImGui::Button(UI("Simplificar malha##ModelPolyDecimate"), ImVec2(-1.0f, 0.0f)))
        {
            MeshTopology simplified = MeshTopology::FromVertices(
                component.MeshData->GetVertices());
            const size_t beforeFaces = simplified.Faces.size();
            ModelingToolset::Decimate(simplified, s_ModelingDecimateRatio);
            if (simplified.IsValid() && simplified.Faces.size() < beforeFaces)
                applyMesh(simplified.ToVertices(false), "Modeling::Decimate");
        }

        if (SupportsRadialResolution(entity))
        {
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Resolução radial da primitiva"));
            ImGui::TextWrapped("Disponível somente antes de editar a malha; a aplicação recria a primitiva base e é reversível pelo Undo.");
            ImGui::SliderInt(UI("Setores##ModelRadialResolution"),
                             &s_ModelingRadialSectors, 3, 128);
            if (ImGui::Button(UI("Aplicar setores radiais##ModelRadialResolution"),
                              ImVec2(-1.0f, 0.0f)))
                SetObjectRadialSegments(app, entity, s_ModelingRadialSectors);
        }
    }

    if (ImGui::Button(UI("Inset global##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        applyMesh(
            hasFaceSelection
                ? ModelingOperations::InsetFacesSelected(
                    component.MeshData->GetVertices(),
                    std::clamp(s_ModelingAmount, 0.0f, 0.95f),
                    s_ModelingSelection)
                : ModelingOperations::InsetFaces(
                    component.MeshData->GetVertices(),
                    std::clamp(s_ModelingAmount, 0.0f, 0.95f)),
            "Modeling::InsetFaces");
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Limpeza e otimização"));
    if (ImGui::Button(UI("Otimizar malha##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::Optimize(vertices);
        applyMesh(std::move(vertices), "Modeling::Optimize");
    }
    if (ImGui::Button(UI("Remover faces internas##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::RemoveInternalFaces(vertices);
        applyMesh(std::move(vertices), "Modeling::RemoveInternalFaces");
    }
    if (ImGui::Button(UI("Remover triângulos degenerados##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = component.MeshData->GetVertices();
        ModelingOperations::RemoveDegenerate(vertices);
        applyMesh(std::move(vertices), "Modeling::RemoveDegenerate");
    }
    if (hasFaceSelection && ImGui::Button(UI("Excluir faces selecionadas##Modeling"), ImVec2(-1.0f, 0.0f)))
    {
        std::vector<Vertex> vertices = ModelingOperations::DeleteFacesSelected(
            component.MeshData->GetVertices(), s_ModelingSelection);
        s_ModelingSelection.clear();
        applyMesh(std::move(vertices), "Modeling::DeleteFaces");
    }

    ImGui::Separator();
    ImGui::TextUnformatted(UI("Simetria"));
    if (ImGui::Button(UI("Espelhar X##Modeling")))
        applyMesh(ModelingOperations::Mirror(component.MeshData->GetVertices(), 0), "Modeling::MirrorX");
    ImGui::SameLine();
    if (ImGui::Button(UI("Espelhar Y##Modeling")))
        applyMesh(ModelingOperations::Mirror(component.MeshData->GetVertices(), 1), "Modeling::MirrorY");
    ImGui::SameLine();
    if (ImGui::Button(UI("Espelhar Z##Modeling")))
        applyMesh(ModelingOperations::Mirror(component.MeshData->GetVertices(), 2), "Modeling::MirrorZ");

    ImGui::Spacing();
    ImGui::TextDisabled(
        "As edições são gravadas em Library/Generated e continuam depois de salvar o projeto.");
}

void InspectorPanel::DrawGlobalLightInspector(Application& app, Entity& entity)
{
    GlobalLightComponent& global = entity.GetGlobalLightComponent();

    ImGui::TextColored(ImVec4(0.55f, 0.82f, 1.0f, 1.0f), "Global Light");
    ImGui::TextWrapped(UI("Controla o clima visual da cena sem alterar as cores dos meshes."));
    ImGui::Separator();

    if (ImGui::Checkbox(UI("Ativo"), &global.Enabled)) app.MarkDirty();
    ImGui::TextDisabled("Afeta o céu, a iluminação renderizada e as sombras.");

    auto& lighting = app.GetScene().GetLightingManager();
    if (ImGui::CollapsingHeader(UI("Céu e nuvens"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto editSkyColor = [&](const char* label, Vector3& color)
        {
            float rgb[3] = { color.X, color.Y, color.Z };
            if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float))
            {
                color = Vector3(std::clamp(rgb[0], 0.0f, 3.0f),
                                std::clamp(rgb[1], 0.0f, 3.0f),
                                std::clamp(rgb[2], 0.0f, 3.0f));
                app.MarkDirty();
            }
        };
        editSkyColor("Cor do céu (zênite)", global.Lighting.SkyZenithColor);
        editSkyColor("Cor do horizonte", global.Lighting.SkyHorizonColor);
        editSkyColor("Cor abaixo do horizonte", global.Lighting.SkyGroundColor);
        if (ImGui::Checkbox(UI("Nuvens"), &global.Lighting.CloudsEnabled)) app.MarkDirty();
        if (global.Lighting.CloudsEnabled)
        {
            editSkyColor("Cor das nuvens", global.Lighting.CloudColor);
            if (ImGui::SliderFloat(UI("Quantidade de nuvens"), &global.Lighting.CloudAmount, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderInt(UI("Detalhe das nuvens"), &global.Lighting.CloudOctaves, 1, 5)) app.MarkDirty();
            if (ImGui::Checkbox(UI("Movimento das nuvens"), &global.Lighting.CloudMovementEnabled)) app.MarkDirty();
            if (global.Lighting.CloudMovementEnabled)
                if (ImGui::SliderFloat(UI("Velocidade das nuvens"), &global.Lighting.CloudSpeed, 0.0f, 10.0f, "%.2fx")) app.MarkDirty();
            ImGui::Separator();
            if (ImGui::Checkbox(UI("Nuvens volumétricas 3D"), &global.Lighting.VolumetricCloudsEnabled)) app.MarkDirty();
            if (global.Lighting.VolumetricCloudsEnabled)
            {
                ImGui::TextDisabled(UI("Camada volumétrica por ray marching; pode reduzir o FPS. Use o perfil Ultra para mais passos."));
                if (app.GetGraphicsQuality() == GraphicsQuality::Performance ||
                    app.GetGraphicsQuality() == GraphicsQuality::UltraPerformance)
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), UI("O perfil gráfico atual desativa nuvens volumétricas para preservar desempenho."));
                else if (app.GetGraphicsQuality() == GraphicsQuality::Balanced)
                    ImGui::TextDisabled(UI("No perfil Equilibrado, a quantidade de passos é limitada para manter o custo menor."));
                if (ImGui::DragFloat(UI("Altura da base"), &global.Lighting.VolumetricCloudBaseHeight, 10.0f, 0.0f, 10000.0f, "%.0f m")) app.MarkDirty();
                if (ImGui::DragFloat(UI("Espessura da camada"), &global.Lighting.VolumetricCloudThickness, 10.0f, 10.0f, 5000.0f, "%.0f m")) app.MarkDirty();
                if (ImGui::SliderFloat(UI("Volume / densidade"), &global.Lighting.VolumetricCloudDensity, 0.0f, 3.0f, "%.2f")) app.MarkDirty();
                if (ImGui::SliderFloat(UI("Escala das nuvens"), &global.Lighting.VolumetricCloudScale, 0.25f, 4.0f, "%.2fx")) app.MarkDirty();
                if (ImGui::SliderInt(UI("Passos de ray marching"), &global.Lighting.VolumetricCloudSteps, 4, 16)) app.MarkDirty();
                ImGui::TextDisabled(UI("A altura e a espessura são medidas em metros do mundo; as nuvens só aparecem acima do horizonte."));
            }
            ImGui::TextDisabled("As cores e a densidade aparecem imediatamente no viewport 3D.");
        }
    }

    if (ImGui::CollapsingHeader(UI("Efeitos de renderização avançada")))
    {
        ImGui::TextWrapped(UI("Efeitos opcionais de alto custo. Recomendados no perfil Ultra; podem ser desligados individualmente."));
        if (ImGui::Checkbox(UI("Oclusão de ambiente em espaço de tela (SSAO)"), &global.Lighting.ScreenSpaceAOEnabled)) app.MarkDirty();
        if (global.Lighting.ScreenSpaceAOEnabled)
        {
            if (ImGui::SliderFloat(UI("Força do SSAO"), &global.Lighting.ScreenSpaceAOIntensity, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Raio do SSAO"), &global.Lighting.ScreenSpaceAORadius, 0.1f, 5.0f, "%.2f m")) app.MarkDirty();
        }

        if (ImGui::Checkbox(UI("Reflexos em espaço de tela (SSR)"), &global.Lighting.ScreenSpaceReflectionsEnabled)) app.MarkDirty();
        if (global.Lighting.ScreenSpaceReflectionsEnabled)
        {
            if (ImGui::SliderFloat(UI("Intensidade do SSR"), &global.Lighting.ScreenSpaceReflectionsStrength, 0.0f, 0.6f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Distância máxima do SSR"), &global.Lighting.ScreenSpaceReflectionsDistance, 1.0f, 100.0f, "%.0f m")) app.MarkDirty();
            ImGui::TextDisabled(UI("SSR só reflete o que está visível na tela e não usa roughness por material."));
        }

        if (ImGui::Checkbox(UI("Antisserrilhado temporal (TAA)"), &global.Lighting.TemporalAAEnabled)) app.MarkDirty();
        if (global.Lighting.TemporalAAEnabled)
        {
            if (ImGui::SliderFloat(UI("Peso do histórico TAA"), &global.Lighting.TemporalAAHistoryWeight, 0.02f, 0.25f, "%.2f")) app.MarkDirty();
            ImGui::TextDisabled(UI("TAA conservador com rejeição por profundidade. O renderer ainda não fornece vetores de movimento, então objetos rápidos podem deixar rastros leves."));
        }
    }

    if (ImGui::CollapsingHeader(UI("Luz principal e iluminação"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        float sunDir[3] = {
            lighting.Sun.Direction.X,
            lighting.Sun.Direction.Y,
            lighting.Sun.Direction.Z
        };
        if (ImGui::DragFloat3("Direção da luz principal", sunDir, 0.01f, -1.0f, 1.0f))
        {
            lighting.Sun.Direction.X = sunDir[0];
            lighting.Sun.Direction.Y = sunDir[1];
            lighting.Sun.Direction.Z = sunDir[2];
            app.MarkDirty();
        }

        float sunColor[3] = {
            lighting.Sun.Color.X,
            lighting.Sun.Color.Y,
            lighting.Sun.Color.Z
        };
        if (ImGui::ColorEdit3("Cor da luz principal", sunColor))
        {
            lighting.Sun.Color.X = sunColor[0];
            lighting.Sun.Color.Y = sunColor[1];
            lighting.Sun.Color.Z = sunColor[2];
            app.MarkDirty();
        }
        if (ImGui::SliderFloat(UI("Intensidade da luz principal"), &lighting.Sun.Intensity, 0.0f, 10.0f)) app.MarkDirty();
        if (ImGui::Checkbox(UI("Usar temperatura da luz"), &lighting.Sun.UseTemperature)) app.MarkDirty();
        if (lighting.Sun.UseTemperature)
            if (ImGui::DragFloat(UI("Temperatura da luz"), &lighting.Sun.TemperatureKelvin,
                                 50.0f, 1000.0f, 40000.0f, "%.0f K")) app.MarkDirty();

        if (ImGui::SliderFloat(UI("Intensidade ambiente"), &app.GetAmbientLight(), 0.0f, 2.0f, "%.2f")) app.MarkDirty();
    }

    if (ImGui::CollapsingHeader(UI("Iluminação avançada"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped(UI("Opções avançadas de luz e sombra pertencem à cena e são controladas pelo Global Lighting."));
        ImGui::TextDisabled(UI("O perfil Ultra usa o máximo do renderer OpenGL atual. Ray tracing real e reflexos por hardware ainda não são suportados; não serão simulados como se fossem recursos ativos."));
        if (ImGui::Checkbox(UI("Ativar sistema de iluminação avançado"), &lighting.Settings.Enabled)) app.MarkDirty();
        if (lighting.Settings.Enabled)
        {
            const char* qualityNames[] = { "Performance", "Balanced", "High", "Ultra" };
            int quality = static_cast<int>(lighting.Settings.Quality);
            if (ImGui::Combo(UI("Qualidade da iluminação"), &quality, qualityNames, 4))
            {
                lighting.Settings.Quality = static_cast<LightingQuality>(std::clamp(quality, 0, 3));
                app.MarkDirty();
            }
            if (ImGui::Checkbox(UI("Sombras dinâmicas"), &lighting.Settings.DynamicShadows)) app.MarkDirty();
            if (ImGui::Checkbox(UI("Contact shadows (PCF local)"), &lighting.Settings.ContactShadows)) app.MarkDirty();
            if (ImGui::Checkbox(UI("Iluminação indireta / irradiância hemisférica"), &lighting.Settings.IndirectLighting)) app.MarkDirty();
            if (ImGui::Checkbox(UI("Iluminação de ambiente"), &lighting.Settings.EnvironmentLighting)) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Intensidade indireta"), &lighting.Settings.IndirectIntensity, 0.0f, 2.0f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Força contact shadow"), &lighting.Settings.ContactShadowStrength, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
            if (ImGui::Checkbox(UI("Desfoque por distância — sol"), &lighting.Settings.DirectionalDistanceSoftening)) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Distância contact shadow"), &lighting.Settings.ContactShadowDistance, 0.01f, 10.0f, "%.2f m")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Distância máxima das sombras"), &lighting.Settings.ShadowDistance, 10.0f, 10000.0f, "%.0f m")) app.MarkDirty();
            if (ImGui::SliderInt(UI("Máximo de Point Lights"), &lighting.Settings.MaxPointLights, 0, 8)) app.MarkDirty();
            if (ImGui::SliderInt(UI("Máximo de Spot Lights"), &lighting.Settings.MaxSpotLights, 0, 8)) app.MarkDirty();
            if (ImGui::SliderInt(UI("Luzes locais com sombra"), &lighting.Settings.MaxShadowedLocalLights, 0, 4)) app.MarkDirty();
            if (ImGui::SliderInt(UI("Resolução shadow map"), &lighting.Settings.ShadowMapResolution, 512, 4096)) app.MarkDirty();
            if (ImGui::SliderInt(UI("Resolução sombras locais"), &lighting.Settings.PointShadowMapResolution, 256, 2048)) app.MarkDirty();
            ImGui::Separator();
            if (ImGui::Checkbox(UI("Ciclo de dia/noite"), &lighting.Settings.DayNightCycleEnabled)) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Hora local"), &lighting.Settings.DayTimeHours, 0.0f, 24.0f, "%.2f h")) app.MarkDirty();
            if (ImGui::DragFloat(UI("Velocidade do ciclo"), &lighting.Settings.DayNightCycleSpeedHoursPerSecond,
                                 0.0001f, 0.00001f, 0.5f, "%.5f h/s")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Azimute do sol"), &lighting.Settings.SunAzimuthDegrees, -180.0f, 180.0f, "%.1f°")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Diâmetro angular do Sol/Lua"),
                &lighting.Settings.DirectionalSourceAngularDiameterDegrees,
                0.10f, 2.0f, "%.2f°")) app.MarkDirty();
            ImGui::SetItemTooltip("Tamanho aparente da fonte direcional. 0,53° aproxima Sol e Lua vistos da Terra; influencia a penumbra conforme o vão entre oclusor e superfície.");
            if (ImGui::SliderFloat(UI("Intensidade máxima do sol"), &lighting.Settings.SunIntensityScale, 0.0f, 10.0f, "%.2f")) app.MarkDirty();
            ImGui::Separator();
            ImGui::TextUnformatted(UI("Lua (luz noturna)"));
            float moonColor[3] = { lighting.Moon.Color.X, lighting.Moon.Color.Y, lighting.Moon.Color.Z };
            if (ImGui::ColorEdit3("Cor da lua", moonColor, ImGuiColorEditFlags_Float))
            {
                lighting.Moon.Color = Vector3(moonColor[0], moonColor[1], moonColor[2]);
                app.MarkDirty();
            }
            if (ImGui::SliderFloat(UI("Intensidade da lua"), &lighting.Moon.Intensity, 0.0f, 3.0f, "%.2f")) app.MarkDirty();
            if (ImGui::Checkbox(UI("Temperatura da lua"), &lighting.Moon.UseTemperature)) app.MarkDirty();
            if (lighting.Moon.UseTemperature)
                if (ImGui::DragFloat(UI("Temperatura da lua"), &lighting.Moon.TemperatureKelvin,
                                     50.0f, 1000.0f, 40000.0f, "%.0f K")) app.MarkDirty();
            ImGui::TextDisabled("A lua assume as sombras direcionais quando o ciclo entra na noite.");
            ImGui::TextDisabled("O ciclo avança somente em Play. GI é aproximada por irradiância hemisférica no OpenGL 3.3.");
            lighting.Settings.Clamp();
        }
    }

    if (ImGui::CollapsingHeader(UI("Sombras"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::SliderFloat(UI("Força das sombras"), &global.ShadowStrength, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Intensidade das sombras diretas"), &app.GetShadowIntensity(), 0.0f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::Button(UI("Restaurar luz e sombras##GlobalLightingReset")))
        {
            app.GetShadowIntensity() = 0.5f;
            app.GetAmbientLight() = 0.4f;
            global.ShadowStrength = 1.0f;
        }
    }

    if (ImGui::CollapsingHeader(UI("Raios Solares e Brilho (God Rays & Glare)"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Raios Solares (Crepuscular / God Rays)");
        if (ImGui::Checkbox(UI("Ativar Raios Solares"), &global.Lighting.GodRaysEnabled)) app.MarkDirty();
        if (global.Lighting.GodRaysEnabled)
        {
            if (ImGui::SliderFloat(UI("Intensidade dos Raios"), &global.Lighting.GodRaysIntensity, 0.0f, 2.5f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Densidade"), &global.Lighting.GodRaysDensity, 0.1f, 1.5f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Decaimento"), &global.Lighting.GodRaysDecay, 0.5f, 0.99f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Peso / Brilho"), &global.Lighting.GodRaysWeight, 0.05f, 1.0f, "%.2f")) app.MarkDirty();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.88f, 0.55f, 1.0f), "Espalhamento Físico (Mie)");
            // Cor dos raios (tint manual)
            {
                float tint[3] = { global.Lighting.GodRaysTint.X, global.Lighting.GodRaysTint.Y, global.Lighting.GodRaysTint.Z };
                if (ImGui::ColorEdit3(UI("Cor dos Raios"), tint))
                {
                    global.Lighting.GodRaysTint = Vector3(tint[0], tint[1], tint[2]);
                    app.MarkDirty();
                }
            }
            if (ImGui::SliderFloat(UI("Espalhamento (Mie)"), &global.Lighting.GodRaysMieG, 0.0f, 0.98f, "%.2f"))
                app.MarkDirty();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = isotrópico (uniforme)\n0.98 = muito direcional (forward scattering,\ncomo em motores de alta fidelidade)");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.5f, 1.0f), "Halo / Corona Solar");
            if (ImGui::SliderFloat(UI("Raio do Halo"), &global.Lighting.GodRaysHaloRadius, 0.01f, 0.5f, "%.3f"))
                app.MarkDirty();
            if (ImGui::SliderFloat(UI("Intensidade do Halo"), &global.Lighting.GodRaysHaloIntensity, 0.0f, 3.0f, "%.2f"))
                app.MarkDirty();

            ImGui::Spacing();
            ImGui::TextDisabled("Raios de luz volumétricos projetados a partir da posição em tela do sol.");
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.85f, 1.0f), "Raios de Brilho / Glare (Luzes e Neons)");
        if (ImGui::Checkbox(UI("Ativar Raios de Brilho (Glare)"), &global.Lighting.GlareRaysEnabled)) app.MarkDirty();
        if (global.Lighting.GlareRaysEnabled)
        {
            if (ImGui::SliderFloat(UI("Intensidade do Glare"), &global.Lighting.GlareRaysIntensity, 0.0f, 2.0f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Comprimento das Estrias"), &global.Lighting.GlareRaysLength, 0.1f, 1.5f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Limiar HDR (Threshold)"), &global.Lighting.GlareRaysThreshold, 0.5f, 3.0f, "%.2f")) app.MarkDirty();
            ImGui::TextDisabled("Gera estrias de brilho difrativo em materiais Neon e luzes artificiais intensas.");
        }
    }

    if (ImGui::CollapsingHeader(UI("Correção de cor da tela"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled(UI("Aplicada uma vez ao quadro inteiro, incluindo meshes e céu."));
        if (ImGui::SliderFloat(UI("Brilho"), &global.Brightness, 0.0f, 3.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Contraste"), &global.Contrast, 0.0f, 3.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Saturação"), &global.Saturation, 0.0f, 3.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Exposição (EV)"), &global.Exposure, -3.0f, 3.0f, "%.2f")) app.MarkDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("A exposição é aplicada uma única vez: +1 EV dobra a luz; -1 EV reduz pela metade."));
        if (ImGui::SliderFloat(UI("Gamma"), &global.Gamma, 0.1f, 3.0f, "%.2f")) app.MarkDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Ajusta meios-tons depois da exposição. 1.0 não altera a curva."));

        float tint[3] = { global.ColorTint.X, global.ColorTint.Y, global.ColorTint.Z };
        if (ImGui::ColorEdit3("Tonalidade", tint, ImGuiColorEditFlags_Float))
        {
            global.ColorTint = Vector3(tint[0], tint[1], tint[2]);
            app.MarkDirty();
        }

        const char* toneMappingNames[] = { "Nenhum", "Reinhard", "ACES", "Filmic" };
        int toneMapping = static_cast<int>(global.ToneMapping);
        if (ImGui::Combo(UI("Tone Mapping"), &toneMapping, toneMappingNames, 4))
        {
            global.ToneMapping = static_cast<GlobalLightComponent::ToneMappingOperator>(
                std::clamp(toneMapping, 0, 3));
            app.MarkDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Mapeia brilho HDR para a faixa da tela. Reinhard é suave; ACES/Filmic preservam realces."));
    }

    if (ImGui::CollapsingHeader(UI("Atmosfera"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox(UI("Névoa"), &global.FogEnabled))
        {
            if (global.FogEnabled)
            {
                if (global.FogDensity <= 0.0f)
                    global.FogDensity = 0.015f;
                if (global.FogEnd <= global.FogStart + 1.0f)
                {
                    global.FogStart = 10.0f;
                    global.FogEnd = 250.0f;
                }
            }
            app.MarkDirty();
        }
        const char* fogModeNames[] = { "Linear", "Exponencial", "Exponencial quadrática" };
        int fogMode = std::clamp(global.Lighting.FogMode, 0, 2);
        if (ImGui::Combo(UI("Modelo de atenuação da névoa"), &fogMode, fogModeNames, 3))
        {
            global.Lighting.FogMode = fogMode;
            app.MarkDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Linear termina na distância final; exponencial cria transição natural; quadrática fica densa mais rapidamente."));
        float fogColor[3] = { global.FogColor.X, global.FogColor.Y, global.FogColor.Z };
        if (ImGui::ColorEdit3("Cor da nevoa", fogColor, ImGuiColorEditFlags_Float))
        {
            global.FogColor = Vector3(fogColor[0], fogColor[1], fogColor[2]);
            app.MarkDirty();
        }
        if (ImGui::SliderFloat(UI("Densidade da nevoa"), &global.FogDensity, 0.0f, 0.2f, "%.3f")) app.MarkDirty();
        if (ImGui::DragFloat(UI("Inicio da nevoa"), &global.FogStart, 1.0f, 0.0f, 10000.0f, "%.1f m")) app.MarkDirty();
        if (ImGui::DragFloat(UI("Fim da nevoa"), &global.FogEnd, 1.0f, 1.0f, 10000.0f, "%.1f m")) app.MarkDirty();
        if (global.FogEnd < global.FogStart + 1.0f)
        {
            global.FogEnd = global.FogStart + 1.0f;
            app.MarkDirty();
        }
    }

    if (ImGui::CollapsingHeader(UI("Lente e textura de filme")))
    {
        if (ImGui::SliderFloat(UI("Força da vinheta"), &global.VignetteStrength, 0.0f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Raio da vinheta"), &global.VignetteRadius, 0.1f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Temperatura"), &global.Temperature, -1.0f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Tint"), &global.Tint, -1.0f, 1.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Bloom"), &global.BloomStrength, 0.0f, 2.0f, "%.2f")) app.MarkDirty();
        if (ImGui::SliderFloat(UI("Sharpen"), &global.SharpenStrength, 0.0f, 2.0f, "%.2f")) app.MarkDirty();
        if (ImGui::Checkbox(UI("Film Grain"), &global.FilmGrainEnabled)) app.MarkDirty();
        if (global.FilmGrainEnabled)
        {
            if (ImGui::SliderFloat(UI("Forca do grain"), &global.FilmGrainStrength, 0.0f, 0.25f, "%.3f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Tamanho do grain"), &global.FilmGrainSize, 0.1f, 8.0f, "%.2f")) app.MarkDirty();
            if (ImGui::SliderFloat(UI("Velocidade do grain"), &global.FilmGrainSpeed, 0.0f, 10.0f, "%.2f")) app.MarkDirty();
        }
        if (ImGui::Checkbox(UI("Aberracao cromatica"), &global.ChromaticAberrationEnabled))
        {
            if (global.ChromaticAberrationEnabled && global.ChromaticAberrationStrength <= 0.0001f)
                global.ChromaticAberrationStrength = 0.05f;
            app.MarkDirty();
        }
        if (global.ChromaticAberrationEnabled)
        {
            if (ImGui::SliderFloat(UI("Forca da aberracao"), &global.ChromaticAberrationStrength, 0.0f, 0.1f, "%.3f")) app.MarkDirty();
            ImGui::TextDisabled(UI("A separação RGB cresce do centro para as bordas da tela."));
        }
    }

    ImGui::Spacing();
    if (ImGui::Button(UI("Reset")))
    {
        GlobalLightComponent temp;
        global = temp;
        app.MarkDirty();
    }
}

void InspectorPanel::DrawMaterialInspector(
    const AssetInfo& asset
)
{
    m_CurrentMaterial = MaterialDatabase::GetMaterialByGUID(asset.GUID);
    if (!m_CurrentMaterial)
        return;

    char buffer[256];
    strcpy_s(buffer, sizeof(buffer), m_CurrentMaterial->Name.c_str());
    if (ImGui::InputText(L("inspector.name"), buffer, sizeof(buffer)))
        m_CurrentMaterial->Name = buffer;

    const char* materialTypes[] = {
        L("material.plastic"),
        L("material.metal"),
        L("material.wood"),
        L("material.neon"),
        L("material.glass"),
        L("material.rubber"),
        L("material.ceramic"),
        L("material.fabric"),
        L("material.mirror")
    };
    int materialType = static_cast<int>(m_CurrentMaterial->Type);
    if (ImGui::Combo(L("material.type"), &materialType, materialTypes, 9))
    {
        m_CurrentMaterial->Type = static_cast<MaterialType>(std::clamp(materialType, 0, 8));
    }
    ImGui::TextDisabled("%s", L("material.color_hint"));
    ImGui::Separator();

    // ── Mirror Material Properties ────────────────────────────────────────
    if (m_CurrentMaterial->Type == MaterialType::Mirror)
    {
        ImGui::TextUnformatted(UI("Propriedades do Espelho"));

        const char* mirrorQualityLabels[] = {
            L("mirror.quality.inherit"),
            L("mirror.quality.ultra_performance"),
            L("mirror.quality.performance"),
            L("mirror.quality.balanced"),
            L("mirror.quality.high_fidelity"),
            L("mirror.quality.ultra")
        };
        int mirrorQualityIdx = static_cast<int>(m_CurrentMaterial->MirrorReflectionQuality);
        if (ImGui::Combo((std::string(L("mirror.quality")) + "##asset").c_str(), &mirrorQualityIdx, mirrorQualityLabels, 6))
        {
            m_CurrentMaterial->MirrorReflectionQuality = static_cast<MirrorQuality>(mirrorQualityIdx);
        }

        ImGui::SliderFloat((std::string(L("mirror.intensity")) + "##asset").c_str(), 
                          &m_CurrentMaterial->MirrorIntensity, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat((std::string(L("mirror.blur")) + "##asset").c_str(), 
                          &m_CurrentMaterial->MirrorBlur, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat((std::string(L("mirror.clip_offset")) + "##asset").c_str(), 
                          &m_CurrentMaterial->MirrorClipOffset, 0.0f, 0.5f, "%.4f");
        ImGui::Separator();
    }

    ImGui::Text(UI("%s: %s"), L("inspector.texture"), m_CurrentMaterial->TexturePath.c_str());
}

void InspectorPanel::DrawGlobalScriptInspector(Application& app, Entity& entity)
{
    ScriptComponent& script = entity.GetScriptComponent();

    ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.55f, 1.0f), UI("Global Script"));
    ImGui::TextWrapped(UI("Controlador global da cena (como ServerScriptService / Workspace script)."));
    ImGui::TextDisabled("Executa lógica central, controle de dia/noite, clima e comandos para o mundo.");
    ImGui::Separator();

    if (ImGui::Checkbox(UI("Ativo##GlobalScriptEntity"), &entity.Active))
        app.MarkDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox(UI("Script Habilitado##GlobalScriptComp"), &script.Enabled))
        app.MarkDirty();

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(UI("Script C# Vinculado"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (script.ScriptPath.empty())
        {
            ImGui::Button(UI("Arraste um script .cs de Assets para cá##GlobalScriptDrop"), ImVec2(-1.0f, 38.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    if (droppedPath && *droppedPath)
                    {
                        const std::filesystem::path assetPath(droppedPath);
                        std::string extension = assetPath.extension().string();
                        std::transform(extension.begin(), extension.end(), extension.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (extension == ".cs")
                        {
                            script.ScriptPath = assetPath.lexically_normal().generic_string();
                            script.ScriptAssemblyQualifiedTypeName.clear();
                            script.Enabled = true;
                            script.Dirty = true;
                            app.MarkDirty();
                            if (app.GetEditorState() == Application::EditorState::Play)
                                ScriptingRuntime::Instance().ReloadAll();
                        }
                        else
                        {
                            ImGui::SetTooltip(UI("O componente Script aceita somente arquivos .cs."));
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TextDisabled("Nenhum script vinculado ainda ao Global Script.");
            if (ImGui::Button(UI("Criar Script Global (GlobalGameController.cs)"), ImVec2(-1.0f, 30.0f)))
            {
                app.CreateScriptForSelectedEntity();
                app.OpenSelectedScriptInVSCode();
            }
            if (ImGui::Button(UI("Criar e Abrir no Editor Interno"), ImVec2(-1.0f, 26.0f)))
            {
                app.CreateScriptForSelectedEntity();
                if (!script.ScriptPath.empty())
                    app.OpenScriptInInternalEditor(script.ScriptPath);
            }
        }
        else
        {
            ImGui::Text(UI("Arquivo: %s"), script.ScriptPath.c_str());
            if (ImGui::Button(UI("Abrir no VS Code##GS")))
            {
                app.OpenSelectedScriptInVSCode();
            }
            ImGui::SameLine();
            if (ImGui::Button(UI("Editor Interno##GS")))
            {
                app.OpenScriptInInternalEditor(script.ScriptPath);
            }
            ImGui::SameLine();
            if (ImGui::Button(UI("Desanexar##GS")))
            {
                script.ScriptPath.clear();
                script.ScriptAssemblyQualifiedTypeName.clear();
                script.SerializedFields.clear();
                app.MarkDirty();
            }

            if (app.GetEditorState() == Application::EditorState::Play)
            {
                std::string fieldsJson = ScriptingRuntime::Instance().GetPublicFieldsJson(entity.GetID());
                if (!fieldsJson.empty() && fieldsJson != "{}")
                {
                    try
                    {
                        auto j = nlohmann::json::parse(fieldsJson);
                        if (j.is_object() && !j.empty())
                        {
                            ImGui::Separator();
                            ImGui::TextDisabled("Variáveis do Script (Tempo Real)");
                            for (auto& [key, val] : j.items())
                            {
                                if (val.is_number_float())
                                {
                                    float fVal = val.get<float>();
                                    if (ImGui::DragFloat(key.c_str(), &fVal, 1.0f))
                                        ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, std::to_string(fVal));
                                }
                                else if (val.is_number_integer())
                                {
                                    int iVal = val.get<int>();
                                    if (ImGui::DragInt(key.c_str(), &iVal, 1))
                                        ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, std::to_string(iVal));
                                }
                                else if (val.is_boolean())
                                {
                                    bool bVal = val.get<bool>();
                                    if (ImGui::Checkbox(key.c_str(), &bVal))
                                        ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, bVal ? "true" : "false");
                                }
                                else if (val.is_string())
                                {
                                    std::string sVal = val.get<std::string>();
                                    char buf[256];
                                    strncpy_s(buf, sVal.c_str(), sizeof(buf));
                                    if (ImGui::InputText(key.c_str(), buf, sizeof(buf)))
                                        ScriptingRuntime::Instance().ApplyFieldValue(entity.GetID(), key, "\"" + std::string(buf) + "\"");
                                }
                            }
                        }
                    }
                    catch (...) {}
                }
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(UI("Controle do Ambiente e Clima"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& lighting = app.GetScene().GetLightingManager();
        static float s_TimeOfDay = 12.0f;
        if (ImGui::SliderFloat(UI("Horário do Dia"), &s_TimeOfDay, 0.0f, 24.0f, "%.1f h"))
        {
            float angle = ((s_TimeOfDay - 6.0f) / 24.0f) * 2.0f * 3.14159265f;
            float sunY = -std::sin(angle);
            float sunX = -std::cos(angle) * 0.7f;
            float sunZ = -std::cos(angle) * 0.5f;
            lighting.Sun.Direction = Vector3(sunX, sunY, sunZ);

            if (sunY < -0.1f)
            {
                lighting.Sun.Intensity = std::clamp(-sunY * 1.2f, 0.0f, 1.5f);
                lighting.Ambient.Intensity = std::clamp(-sunY * 0.4f, 0.05f, 0.4f);
            }
            else
            {
                lighting.Sun.Intensity = 0.0f;
                lighting.Ambient.Intensity = 0.05f;
            }
            app.MarkDirty();
        }

        float sunDir[3] = { lighting.Sun.Direction.X, lighting.Sun.Direction.Y, lighting.Sun.Direction.Z };
        if (ImGui::DragFloat3(UI("Direção do Sol"), sunDir, 0.01f, -1.0f, 1.0f))
        {
            lighting.Sun.Direction = Vector3(sunDir[0], sunDir[1], sunDir[2]);
            app.MarkDirty();
        }
        if (ImGui::DragFloat(UI("Intensidade do Sol"), &lighting.Sun.Intensity, 0.02f, 0.0f, 10.0f, "%.2f"))
            app.MarkDirty();
        if (ImGui::DragFloat(UI("Intensidade Ambiente"), &lighting.Ambient.Intensity, 0.01f, 0.0f, 2.0f, "%.2f"))
            app.MarkDirty();
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(UI("Ações Globais na Cena")))
    {
        ImGui::TextDisabled("Dispara comandos diretos para todos os objetos da cena:");

        if (ImGui::Button(UI("Congelar Física de Todos os Objetos"), ImVec2(-1.0f, 26.0f)))
        {
            for (Entity& e : app.GetScene().GetEntities())
            {
                if (!e.IsEditorOnly())
                    e.PhysicsEnabled = false;
            }
            app.MarkDirty();
        }
        if (ImGui::Button(UI("Descongelar Física de Todos os Objetos"), ImVec2(-1.0f, 26.0f)))
        {
            for (Entity& e : app.GetScene().GetEntities())
            {
                if (!e.IsEditorOnly() && !e.HasUI() && !e.IsFolder)
                    e.PhysicsEnabled = true;
            }
            app.MarkDirty();
        }
        if (ImGui::Button(UI("Ligar/Desligar Todas as Luzes"), ImVec2(-1.0f, 26.0f)))
        {
            static bool s_LightsToggle = false;
            for (Entity& e : app.GetScene().GetEntities())
            {
                if (e.HasLight()) e.GetLightComponent().Enabled = s_LightsToggle;
                if (e.HasLocalLight()) e.GetLocalLightComponent().Enabled = s_LightsToggle;
            }
            s_LightsToggle = !s_LightsToggle;
            app.MarkDirty();
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader(UI("Guia C# / Como Programar Comandos Globais")))
    {
        ImGui::TextWrapped(UI(
            "Em C#, você pode encontrar e comandar qualquer objeto ou o clima:\n\n"
            "// 1. Encontrar objeto por nome ou tag:\n"
            "Entity item = GameObject.Find(\"MeuCubo\");\n"
            "Entity[] inimigos = GameObject.FindGameObjectsWithTag(\"Inimigo\");\n\n"
            "// 2. Mover tudo de uma vez:\n"
            "foreach (var obj in inimigos)\n"
            "    obj.transform?.Translate(0f, 0f, 2f * dt);\n\n"
            "// 3. Controlar clima / céu:\n"
            "var lightObj = GameObject.Find(\"Global Light\");\n"
            "var gl = lightObj?.GetComponent<GlobalLightComponent>();\n"
            "if (gl != null) gl.ambientIntensity = 0.8f;\n"
        ));
    }
}
