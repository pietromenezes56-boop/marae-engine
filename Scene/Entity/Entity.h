#pragma once

#include "../Transform/Transform.h"
#include "../Components/MeshComponent.h"
#include "../Components/ColorComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Components/ScriptComponent.h"
#include "../Components/AnimationComponent.h"
#include "../Components/LightComponent.h"
#include "../Components/LocalLightComponent.h"
#include "../Components/AudioComponent.h"
#include "../Components/GlobalLightComponent.h"
#include "../Components/PostProcessingVolumeComponent.h"
#include "../Components/CameraComponent.h"
#include "../Components/TerrainComponent.h"
#include "../Components/UIComponent.h"
#include "../Components/ParticleSystemComponent.h"
#include "../Components/AIComponent.h"
#include <cstdint>
#include <string>
#include <vector>


class Entity
{
public:

    enum class ColliderShape
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        Cylinder = 3,
        Cone = 4,
        Mesh = 5,
        ConvexMesh = 6
    };

    Entity();

    uint64_t GetID() const
    {
        return m_ID;
    }

    Transform& GetTransform();

    MeshComponent& GetMeshComponent();
    ColorComponent& GetColorComponent();
    MaterialComponent& GetMaterialComponent();
    ScriptComponent& GetScriptComponent();
    AnimationComponent& GetAnimationComponent();
    LightComponent& GetLightComponent();
    bool HasLight() const { return m_HasLight; }
    void AddLight(LightType type = LightType::Point)
    {
        m_LightComponent = LightComponent{};
        m_LightComponent.Type = type;
        // Com atenuação inversa ao quadrado, 1.0 fica praticamente invisível
        // a poucos metros. Fontes locais começam com uma energia útil e
        // continuam totalmente ajustáveis no Inspector.
        m_LightComponent.Intensity = type == LightType::Directional ? 1.0f : 60.0f;
        m_HasLight = true;
    }
    void RemoveLight()    { m_HasLight = false; }

    LocalLightComponent& GetLocalLightComponent();
    bool HasLocalLight() const { return m_HasLocalLight; }
    void AddLocalLight()
    {
        m_LocalLightComponent = LocalLightComponent{};
        m_HasLocalLight = true;
    }
    void RemoveLocalLight() { m_HasLocalLight = false; }

    AudioComponent& GetAudioComponent();
    bool HasAudio() const { return m_HasAudio; }
    void AddAudio()
    {
        m_HasAudio = true;
        if (m_AudioComponent.SourceID.empty())
            m_AudioComponent.SourceID = "audio_" + std::to_string(m_ID);
    }
    void RemoveAudio()    { m_HasAudio = false; }

    GlobalLightComponent& GetGlobalLightComponent();
    const GlobalLightComponent& GetGlobalLightComponent() const;
    bool IsGlobalLight() const { return m_IsGlobalLight; }
    void SetGlobalLight(bool value) { m_IsGlobalLight = value; }
    bool IsGlobalScript() const { return m_IsGlobalScript; }
    void SetGlobalScript(bool value) { m_IsGlobalScript = value; }

    PostProcessingVolumeComponent& GetPostProcessingVolume();
    const PostProcessingVolumeComponent& GetPostProcessingVolume() const;
    bool HasPostProcessingVolume() const { return m_HasPPVolume; }
    void AddPostProcessingVolume() { m_HasPPVolume = true; }
    void RemovePostProcessingVolume() { m_HasPPVolume = false; }
    CameraComponent& GetCameraComponent() { return m_CameraComponent; }
    const CameraComponent& GetCameraComponent() const { return m_CameraComponent; }
    bool HasCamera() const { return m_HasCamera; }
    void AddCamera() { m_HasCamera = true; }
    void RemoveCamera() { m_HasCamera = false; }
    TerrainComponent& GetTerrainComponent() { return m_TerrainComponent; }
    const TerrainComponent& GetTerrainComponent() const { return m_TerrainComponent; }
    bool HasTerrain() const { return m_TerrainComponent.Enabled; }
    void AddTerrain(int resolution = 33) { m_TerrainComponent.Initialize(resolution); }
    void RemoveTerrain() { m_TerrainComponent.Enabled = false; }
    UIComponent& GetUIComponent() { return m_UIComponent; }
    const UIComponent& GetUIComponent() const { return m_UIComponent; }
    bool HasUI() const { return m_HasUI; }
    void AddUI(UIElementType type = UIElementType::Frame)
    {
        m_UIComponent = UIComponent{};
        m_UIComponent.Type = type;
        // GuiObjects não são meshes 3D. Remover a malha padrão evita que um
        // cubo apareça atrás do painel e que o Inspector entre no fluxo 3D.
        m_MeshComponent.MeshData.reset();
        m_MeshComponent.MeshPath.clear();
        m_MeshComponent.PrimitiveType = "UI";
        PhysicsEnabled = false;
        ColliderEnabled = false;
        m_HasUI = true;
    }
    void RemoveUI() { m_HasUI = false; }

    ParticleSystemComponent& GetParticleSystem() { return m_ParticleSystem; }
    const ParticleSystemComponent& GetParticleSystem() const { return m_ParticleSystem; }
    bool HasParticleSystem() const { return m_HasParticleSystem; }
    void AddParticleSystem() { m_ParticleSystem = ParticleSystemComponent{}; m_HasParticleSystem = true; }
    void RemoveParticleSystem() { m_HasParticleSystem = false; }

    AIComponent& GetAIComponent() { return m_AIComponent; }
    const AIComponent& GetAIComponent() const { return m_AIComponent; }
    bool HasAI() const { return m_HasAI; }
    void AddAI() { m_AIComponent = AIComponent{}; m_HasAI = true; }
    void RemoveAI() { m_HasAI = false; }

    template<typename T>
    bool HasComponent() const
    {
        if constexpr (std::is_same_v<T, ScriptComponent>)
            return true; // ScriptComponent sempre existe
        else if constexpr (std::is_same_v<T, MeshComponent>)
            return true;
        else if constexpr (std::is_same_v<T, ColorComponent>)
            return true;
        else if constexpr (std::is_same_v<T, MaterialComponent>)
            return true;
        else if constexpr (std::is_same_v<T, AnimationComponent>)
            return true;
        else if constexpr (std::is_same_v<T, LightComponent>)
            return m_HasLight;
        else if constexpr (std::is_same_v<T, LocalLightComponent>)
            return m_HasLocalLight;
        else if constexpr (std::is_same_v<T, AudioComponent>)
            return m_HasAudio;
        else if constexpr (std::is_same_v<T, GlobalLightComponent>)
            return m_IsGlobalLight;
        else if constexpr (std::is_same_v<T, PostProcessingVolumeComponent>)
            return m_HasPPVolume;
        else if constexpr (std::is_same_v<T, CameraComponent>)
            return m_HasCamera;
        else if constexpr (std::is_same_v<T, TerrainComponent>)
            return m_TerrainComponent.Enabled;
        else if constexpr (std::is_same_v<T, UIComponent>)
            return m_HasUI;
        else if constexpr (std::is_same_v<T, ParticleSystemComponent>)
            return m_HasParticleSystem;
        else if constexpr (std::is_same_v<T, AIComponent>)
            return m_HasAI;
        return false;
    }

    template<typename T>
    T* GetComponent()
    {
        if constexpr (std::is_same_v<T, ScriptComponent>)
            return &m_ScriptComponent;
        else if constexpr (std::is_same_v<T, MeshComponent>)
            return &m_MeshComponent;
        else if constexpr (std::is_same_v<T, ColorComponent>)
            return &m_ColorComponent;
        else if constexpr (std::is_same_v<T, MaterialComponent>)
            return &m_MaterialComponent;
        else if constexpr (std::is_same_v<T, AnimationComponent>)
            return &m_AnimationComponent;
        else if constexpr (std::is_same_v<T, LightComponent>)
            return m_HasLight ? &m_LightComponent : nullptr;
        else if constexpr (std::is_same_v<T, LocalLightComponent>)
            return m_HasLocalLight ? &m_LocalLightComponent : nullptr;
        else if constexpr (std::is_same_v<T, AudioComponent>)
            return m_HasAudio ? &m_AudioComponent : nullptr;
        else if constexpr (std::is_same_v<T, GlobalLightComponent>)
            return m_IsGlobalLight ? &m_GlobalLightComponent : nullptr;
        else if constexpr (std::is_same_v<T, PostProcessingVolumeComponent>)
            return m_HasPPVolume ? &m_PPVolumeComponent : nullptr;
        else if constexpr (std::is_same_v<T, CameraComponent>)
            return m_HasCamera ? &m_CameraComponent : nullptr;
        else if constexpr (std::is_same_v<T, TerrainComponent>)
            return m_TerrainComponent.Enabled ? &m_TerrainComponent : nullptr;
        else if constexpr (std::is_same_v<T, UIComponent>)
            return m_HasUI ? &m_UIComponent : nullptr;
        else if constexpr (std::is_same_v<T, ParticleSystemComponent>)
            return m_HasParticleSystem ? &m_ParticleSystem : nullptr;
        else if constexpr (std::is_same_v<T, AIComponent>)
            return m_HasAI ? &m_AIComponent : nullptr;
        return nullptr;
    }

    void SetName(
        const std::string& name
    );

    const std::string& GetName() const;

    bool IsEditorOnly() const;

    void SetEditorOnly(
        bool value
    );

    void CopyDataFrom(const Entity& source);

    uint64_t GetParentID() const;

    void SetParentID(
        uint64_t id
    );

    std::vector<uint64_t>& GetChildrenIDs();

    const std::vector<uint64_t>& GetChildrenIDs() const;


public:
    float PickingRadius = 1.0f;

    // Controle de ciclo de vida e organização da entidade.
    // Active desliga a participação no runtime; Opacity controla a renderização
    // e também a intensidade da sombra projetada.
    bool Active = true;
    float Opacity = 1.0f;
    // Define se esta entidade participa dos passes de sombra (sol e luzes locais).
    bool CastShadows = true;
    bool RaycastTarget = true;
    // Editor-only selection lock; does not affect runtime raycasts/physics.
    bool SelectionLocked = false;
    int Layer = 0;
    std::string Tag;

    // Contêineres de organização da Hierarchy.
    // Pasta apenas agrupa; Modelo representa um conjunto editável de filhos.
    bool IsFolder = false;
    bool IsModel = false;

    // Prefab vinculado — vazio se a entidade não é instância de prefab.
    // Preenchido por Prefab::Instantiate e persistido na cena via @prefab.
    std::string PrefabPath;

    // Propriedades físicas editáveis no Inspector.
    bool PhysicsEnabled = true;
    bool UseGravity = true;
    bool IsStatic = false;
    bool ColliderEnabled = true;
    bool IsTrigger = false;
    ColliderShape Collider = ColliderShape::Box;
    float Mass = 1.0f;
    float Restitution = 0.15f;
    float Friction = 0.65f;
    Vector3 Velocity = Vector3(0.0f, 0.0f, 0.0f);
    Vector3 AngularVelocity = Vector3(0.0f, 0.0f, 0.0f);
    bool ContinuousCollision = true;
    bool FreezeRotation = false;

    // Geometria acústica do objeto 3D. Objetos sólidos bloqueiam o som por
    // padrão; habilite transmissão para paredes/portas/materiais permeáveis.
    bool AudioAcousticGeometryEnabled = true;
    bool AudioOccluder = true;
    bool AudioTransmissionEnabled = false;
    float AudioTransmission = 0.35f;
    float AudioAbsorption = 0.80f;
    float AudioDiffraction = 0.25f;



private:
    std::string m_Name;

    Transform m_Transform;

    bool m_EditorOnly = false;
    bool m_IsGlobalLight = false;
    bool m_IsGlobalScript = false;

    MeshComponent m_MeshComponent;
    ColorComponent m_ColorComponent;
    MaterialComponent m_MaterialComponent;
    ScriptComponent m_ScriptComponent;
    AnimationComponent m_AnimationComponent;
    LightComponent m_LightComponent;
    LocalLightComponent m_LocalLightComponent;
    bool m_HasLocalLight = false;
    bool m_HasLight = false;
    AudioComponent m_AudioComponent;
    bool m_HasAudio = false;
    GlobalLightComponent m_GlobalLightComponent;
    bool m_HasPPVolume = false;
    PostProcessingVolumeComponent m_PPVolumeComponent{};
    CameraComponent m_CameraComponent{};
    bool m_HasCamera = false;
    TerrainComponent m_TerrainComponent{};
    UIComponent m_UIComponent{};
    bool m_HasUI = false;
    ParticleSystemComponent m_ParticleSystem;
    bool m_HasParticleSystem = false;
    AIComponent m_AIComponent{};
    bool m_HasAI = false;

    uint64_t m_ID = 0;

    uint64_t m_ParentID = 0;

    std::vector<uint64_t> m_ChildrenIDs;
};
