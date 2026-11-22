#include "Entity.h"

static uint64_t s_NextEntityID = 1;

Entity::Entity()
    : m_Name("Entity")
{
    m_ID = s_NextEntityID++;
}

Transform& Entity::GetTransform()            { return m_Transform; }
MeshComponent& Entity::GetMeshComponent()   { return m_MeshComponent; }
ColorComponent& Entity::GetColorComponent() { return m_ColorComponent; }
MaterialComponent& Entity::GetMaterialComponent() { return m_MaterialComponent; }
ScriptComponent& Entity::GetScriptComponent()     { return m_ScriptComponent; }
AnimationComponent& Entity::GetAnimationComponent() { return m_AnimationComponent; }
LightComponent& Entity::GetLightComponent()  { return m_LightComponent; }
LocalLightComponent& Entity::GetLocalLightComponent() { return m_LocalLightComponent; }
AudioComponent& Entity::GetAudioComponent()  { return m_AudioComponent; }
GlobalLightComponent& Entity::GetGlobalLightComponent() { return m_GlobalLightComponent; }
const GlobalLightComponent& Entity::GetGlobalLightComponent() const { return m_GlobalLightComponent; }
PostProcessingVolumeComponent& Entity::GetPostProcessingVolume() { return m_PPVolumeComponent; }
const PostProcessingVolumeComponent& Entity::GetPostProcessingVolume() const { return m_PPVolumeComponent; }

void Entity::SetName(const std::string& name) { m_Name = name; }
const std::string& Entity::GetName() const    { return m_Name; }
bool Entity::IsEditorOnly() const             { return m_EditorOnly; }
void Entity::SetEditorOnly(bool value)        { m_EditorOnly = value; }

void Entity::CopyDataFrom(const Entity& source)
{
    m_Name = source.m_Name;
    m_Transform = source.m_Transform;
    m_EditorOnly = source.m_EditorOnly;
    m_IsGlobalLight = source.m_IsGlobalLight;
    m_IsGlobalScript = source.m_IsGlobalScript;
    m_MeshComponent = source.m_MeshComponent;
    if (source.m_MeshComponent.MeshData && source.m_MeshComponent.MeshData->IsValid())
    {
        m_MeshComponent.MeshData = std::make_shared<Mesh>();
        m_MeshComponent.MeshData->Create(source.m_MeshComponent.MeshData->GetVertices(),
            source.m_MeshComponent.MeshData->GetPrimitiveType(), "Entity::CopyDataFrom");
    }
    else
    {
        m_MeshComponent.MeshData.reset();
    }
    m_ColorComponent = source.m_ColorComponent;
    m_MaterialComponent = source.m_MaterialComponent;
    m_ScriptComponent = source.m_ScriptComponent;
    m_AnimationComponent = source.m_AnimationComponent;
    m_LightComponent = source.m_LightComponent;
    m_HasLight = source.m_HasLight;
    m_LocalLightComponent = source.m_LocalLightComponent;
    m_HasLocalLight = source.m_HasLocalLight;
    m_AudioComponent = source.m_AudioComponent;
    m_HasAudio = source.m_HasAudio;
    m_GlobalLightComponent = source.m_GlobalLightComponent;
    m_HasPPVolume = source.m_HasPPVolume;
    m_PPVolumeComponent = source.m_PPVolumeComponent;
    m_CameraComponent = source.m_CameraComponent;
    m_HasCamera = source.m_HasCamera;
    m_TerrainComponent = source.m_TerrainComponent;
    m_UIComponent = source.m_UIComponent;
    m_HasUI = source.m_HasUI;
    m_ParticleSystem = source.m_ParticleSystem;
    m_HasParticleSystem = source.m_HasParticleSystem;
    m_AIComponent = source.m_AIComponent;
    m_HasAI = source.m_HasAI;
    PickingRadius = source.PickingRadius;
    Active = source.Active;
    Opacity = source.Opacity;
    CastShadows = source.CastShadows;
    RaycastTarget = source.RaycastTarget;
    SelectionLocked = source.SelectionLocked;
    Layer = source.Layer;
    Tag = source.Tag;
    IsFolder = source.IsFolder;
    IsModel = source.IsModel;
    PrefabPath = source.PrefabPath;
    PhysicsEnabled = source.PhysicsEnabled;
    UseGravity = source.UseGravity;
    IsStatic = source.IsStatic;
    ColliderEnabled = source.ColliderEnabled;
    IsTrigger = source.IsTrigger;
    Collider = source.Collider;
    Mass = source.Mass;
    Restitution = source.Restitution;
    Friction = source.Friction;
    Velocity = source.Velocity;
    AngularVelocity = source.AngularVelocity;
    ContinuousCollision = source.ContinuousCollision;
    FreezeRotation = source.FreezeRotation;
    AudioAcousticGeometryEnabled = source.AudioAcousticGeometryEnabled;
    AudioOccluder = source.AudioOccluder;
    AudioTransmissionEnabled = source.AudioTransmissionEnabled;
    AudioTransmission = source.AudioTransmission;
    AudioAbsorption = source.AudioAbsorption;
    AudioDiffraction = source.AudioDiffraction;
}

uint64_t Entity::GetParentID() const          { return m_ParentID; }
void Entity::SetParentID(uint64_t id)         { m_ParentID = id; }

std::vector<uint64_t>& Entity::GetChildrenIDs()             { return m_ChildrenIDs; }
const std::vector<uint64_t>& Entity::GetChildrenIDs() const { return m_ChildrenIDs; }
