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
uint64_t Entity::GetParentID() const          { return m_ParentID; }
void Entity::SetParentID(uint64_t id)         { m_ParentID = id; }

std::vector<uint64_t>& Entity::GetChildrenIDs()             { return m_ChildrenIDs; }
const std::vector<uint64_t>& Entity::GetChildrenIDs() const { return m_ChildrenIDs; }
