#include "Scene.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../../Engine/UI/UILayout.h"
#include <algorithm>
#include <unordered_set>
#include "../../Math/Vector3.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
    void StoreUIRectInParentSpace(
        UIComponent& ui,
        const UIComponent::ResolvedRect& worldRect,
        const UIComponent::ResolvedRect& parentRect)
    {
        const float localX = worldRect.X - parentRect.X;
        const float localY = worldRect.Y - parentRect.Y;
        if (ui.UseResponsiveAnchors)
        {
            const float anchorMinX = std::clamp(ui.AnchorMinX, 0.0f, 1.0f);
            const float anchorMinY = std::clamp(ui.AnchorMinY, 0.0f, 1.0f);
            const float anchorMaxX = std::clamp(ui.AnchorMaxX, 0.0f, 1.0f);
            const float anchorMaxY = std::clamp(ui.AnchorMaxY, 0.0f, 1.0f);
            ui.OffsetMinX = localX - anchorMinX * parentRect.Width;
            ui.OffsetMinY = localY - anchorMinY * parentRect.Height;
            ui.OffsetMaxX = localX + worldRect.Width - anchorMaxX * parentRect.Width;
            ui.OffsetMaxY = localY + worldRect.Height - anchorMaxY * parentRect.Height;
        }
        else
        {
            ui.PositionX = localX + ui.AnchorX * ui.Width;
            ui.PositionY = localY + ui.AnchorY * ui.Height;
        }
    }
}

Scene::Scene()
{
}

void Scene::RotateChildren(
    Entity& parent,
    const glm::quat& deltaRotation,
    const Vector3& pivot
)
{
    std::unordered_set<uint64_t> visited{ parent.GetID() };
    std::vector<uint64_t> pending(parent.GetChildrenIDs().begin(), parent.GetChildrenIDs().end());
    for (size_t next = 0; next < pending.size(); ++next)
    {
        const uint64_t childID = pending[next];
        if (!visited.insert(childID).second) continue;
        Entity* child = FindEntityByID(childID);
        if (!child) continue;
        pending.insert(pending.end(), child->GetChildrenIDs().begin(), child->GetChildrenIDs().end());

        Transform& transform =
            child->GetTransform();

        glm::vec3 position(
            transform.Position.X,
            transform.Position.Y,
            transform.Position.Z
        );

        glm::vec3 center(
            pivot.X,
            pivot.Y,
            pivot.Z
        );

        glm::vec3 offset =
            position - center;

        // rotate offset using rotation matrix from quaternion
        offset = glm::mat3_cast(deltaRotation) * offset;

        position = center + offset;

        transform.Position.X =
            position.x;

        transform.Position.Y =
            position.y;

        transform.Position.Z =
            position.z;

        transform.Rotation =
            deltaRotation *
            transform.Rotation;

    }
}

void Scene::ScaleChildren(
    Entity& parent,
    const Vector3& oldScale,
    const Vector3& newScale,
    const Vector3& pivot
)
{
    float scaleX =
        newScale.X / oldScale.X;

    float scaleY =
        newScale.Y / oldScale.Y;

    float scaleZ =
        newScale.Z / oldScale.Z;

    std::unordered_set<uint64_t> visited{ parent.GetID() };
    std::vector<uint64_t> pending(parent.GetChildrenIDs().begin(), parent.GetChildrenIDs().end());
    for (size_t next = 0; next < pending.size(); ++next)
    {
        const uint64_t childID = pending[next];
        if (!visited.insert(childID).second) continue;
        Entity* child = FindEntityByID(childID);
        if (!child) continue;
        pending.insert(pending.end(), child->GetChildrenIDs().begin(), child->GetChildrenIDs().end());

        Transform& transform =
            child->GetTransform();

        glm::vec3 position(
            transform.Position.X,
            transform.Position.Y,
            transform.Position.Z
        );

        glm::vec3 center(
            pivot.X,
            pivot.Y,
            pivot.Z
        );

        glm::vec3 offset =
            position - center;

        offset.x *= scaleX;
        offset.y *= scaleY;
        offset.z *= scaleZ;

        position =
            center + offset;

        transform.Position.X =
            position.x;

        transform.Position.Y =
            position.y;

        transform.Position.Z =
            position.z;

        transform.Scale.X *=
            scaleX;

        transform.Scale.Y *=
            scaleY;

        transform.Scale.Z *=
            scaleZ;

    }
}

Entity& Scene::CreateEntity()
{
    m_Entities.emplace_back();

    Entity& e = m_Entities.back();
    m_EntityIndex[e.GetID()] = m_Entities.size() - 1;

    return e;
}

void Scene::RebuildEntityIndex() const
{
    m_EntityIndex.clear();
    m_EntityIndex.reserve(m_Entities.size());
    for (size_t index = 0; index < m_Entities.size(); ++index)
        m_EntityIndex[m_Entities[index].GetID()] = index;
}

Entity* Scene::FindEntityByID(
    uint64_t id
)
{
    if (id == 0)
        return nullptr;

    const auto indexed = m_EntityIndex.find(id);
    if (indexed != m_EntityIndex.end() && indexed->second < m_Entities.size())
    {
        Entity& entity = m_Entities[indexed->second];
        if (entity.GetID() == id)
            return &entity;
    }

    // Algumas ferramentas ainda inserem/removem entidades diretamente pelo
    // vetor público. Repare o índice sob demanda para manter compatibilidade.
    for (size_t index = 0; index < m_Entities.size(); ++index)
    {
        if (m_Entities[index].GetID() == id)
        {
            RebuildEntityIndex();
            return &m_Entities[index];
        }
    }

    if (indexed != m_EntityIndex.end())
        RebuildEntityIndex();
    return nullptr;
}

std::vector<Entity>& Scene::GetEntities()
{
    return m_Entities;
}

const std::vector<Entity>& Scene::GetEntities() const
{
    return m_Entities;
}

void Scene::DeleteEntity(int index)
{
    if (index < 0 ||
        index >= (int)m_Entities.size())
    {
        return;
    }

    UpdateTransforms();
    const uint64_t removedID = m_Entities[static_cast<size_t>(index)].GetID();
    std::vector<uint64_t> childrenToDetach;
    for (const Entity& entity : m_Entities)
        if (entity.GetParentID() == removedID)
            childrenToDetach.push_back(entity.GetID());
    for (uint64_t childID : childrenToDetach)
        RemoveParent(childID, true);
    RemoveParent(removedID, true);

    m_Entities.erase(
        m_Entities.begin() + index
    );
    RebuildEntityIndex();
}

void Scene::Clear()
{
    m_Entities.clear();
    m_EntityIndex.clear();
    // Configuração de iluminação pertence à cena. Uma cena nova/antiga sem
    // metadados começa no caminho compatível, sem iluminação avançada.
    m_LightingManager = LightingManager{};
}

void Scene::MoveChildren(
    Entity& parent,
    const Vector3& delta
)
{
    std::unordered_set<uint64_t> visited{ parent.GetID() };
    std::vector<uint64_t> pending(parent.GetChildrenIDs().begin(), parent.GetChildrenIDs().end());
    for (size_t next = 0; next < pending.size(); ++next)
    {
        const uint64_t childID = pending[next];
        if (!visited.insert(childID).second) continue;
        Entity* child = FindEntityByID(childID);
        if (!child) continue;
        pending.insert(pending.end(), child->GetChildrenIDs().begin(), child->GetChildrenIDs().end());

        child->GetTransform().Position.X +=
            delta.X;

        child->GetTransform().Position.Y +=
            delta.Y;

        child->GetTransform().Position.Z +=
            delta.Z;

    }
}

void Scene::SetParent(
    uint64_t childID,
    uint64_t parentID,
    bool preserveWorldTransform
)
{
Entity* child = FindEntityByID(childID);
Entity* parent = FindEntityByID(parentID);
if (!child || !parent || childID == parentID)
    return;

MaraeUI::ResolvedUIMap uiRects;
const bool preserveUIRect = preserveWorldTransform && child->HasUI() && parent->HasUI();
if (preserveUIRect)
    uiRects = MaraeUI::ResolveLayouts(m_Entities, 960.0f, 540.0f);
const auto childUIRectIt = uiRects.find(childID);
const auto parentUIRectIt = uiRects.find(parentID);

glm::mat4 childWorld(1.0f);
glm::mat4 parentWorld(1.0f);
if (preserveWorldTransform)
{
    UpdateTransforms();
    childWorld = child->GetTransform().WorldMatrix;
    parentWorld = parent->GetTransform().WorldMatrix;
}

	std::unordered_set<uint64_t> ancestors;
	for (Entity* ancestor = parent; ancestor; )
	{
	    if (ancestor->GetID() == childID || !ancestors.insert(ancestor->GetID()).second)
	        return;
	    const uint64_t ancestorID = ancestor->GetParentID();
	    ancestor = ancestorID == 0 ? nullptr : FindEntityByID(ancestorID);
	}

	// Repara vínculos duplicados/obsoletos e mantém uma única relação pai-filho.
	for (Entity& entity : m_Entities)
	{
	    auto& childIDs = entity.GetChildrenIDs();
	    childIDs.erase(std::remove(childIDs.begin(), childIDs.end(), childID), childIDs.end());
	}
	child->SetParentID(parentID);
	parent->GetChildrenIDs().push_back(childID);

if (preserveUIRect && childUIRectIt != uiRects.end() && parentUIRectIt != uiRects.end())
    StoreUIRectInParentSpace(
        child->GetUIComponent(), childUIRectIt->second, parentUIRectIt->second);

if (preserveWorldTransform)
{
    const glm::mat4 localMatrix = glm::inverse(parentWorld) * childWorld;
    glm::vec3 translation(0.0f), scale(1.0f), skew(0.0f);
    glm::vec4 perspective(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (glm::decompose(localMatrix, scale, rotation, translation, skew, perspective))
    {
        Transform& local = child->GetTransform();
        local.Position = Vector3(translation.x, translation.y, translation.z);
        local.Rotation = glm::normalize(rotation);
        local.Scale = Vector3(scale.x, scale.y, scale.z);
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(local.Rotation));
        local.EulerRotation = Vector3(euler.x, euler.y, euler.z);
        local.UpdateLocalMatrix();
    }
}
}

void Scene::RemoveParent(
    uint64_t childID,
    bool preserveWorldTransform
)
{
    Entity* child =
        FindEntityByID(childID);

    if (!child)
    {
        return;
    }

    uint64_t parentID =
        child->GetParentID();

    if (parentID == 0)
    {
        return;
    }

    glm::mat4 worldMatrix(1.0f);
    if (preserveWorldTransform)
    {
        UpdateTransforms();
        worldMatrix = child->GetTransform().WorldMatrix;
    }

    Entity* parent =
        FindEntityByID(parentID);

    UIComponent::ResolvedRect childUIRect{};
    bool preserveUIRect = preserveWorldTransform && child->HasUI();
    if (preserveUIRect)
    {
        const MaraeUI::ResolvedUIMap uiRects =
            MaraeUI::ResolveLayouts(m_Entities, 960.0f, 540.0f);
        const auto childRectIt = uiRects.find(childID);
        if (childRectIt != uiRects.end())
            childUIRect = childRectIt->second;
        else
            preserveUIRect = false;
    }

    if (parent)
    {
        auto& children =
            parent->GetChildrenIDs();

        children.erase(
            std::remove(
                children.begin(),
                children.end(),
                childID
            ),
            children.end()
        );
    }

    child->SetParentID(0);
    if (preserveUIRect)
        StoreUIRectInParentSpace(
            child->GetUIComponent(), childUIRect,
            UIComponent::ResolvedRect{ 0.0f, 0.0f, 960.0f, 540.0f });

    if (preserveWorldTransform)
    {
        glm::vec3 translation(0.0f), scale(1.0f), skew(0.0f);
        glm::vec4 perspective(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (glm::decompose(worldMatrix, scale, rotation, translation, skew, perspective))
        {
            Transform& transform = child->GetTransform();
            transform.Position = Vector3(translation.x, translation.y, translation.z);
            transform.Rotation = glm::normalize(rotation);
            transform.Scale = Vector3(scale.x, scale.y, scale.z);
            const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
            transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
            transform.UpdateLocalMatrix();
        }
    }
}

Entity* Scene::FindParent(
    const Entity& entity
)
{
    if (
        entity.GetParentID() == 0
        )
    {
        return nullptr;
    }

    return FindEntityByID(
        entity.GetParentID()
    );
}

std::vector<Entity*> Scene::GetChildren(
    const Entity& entity
)
{
    std::vector<Entity*> children;

    for (auto& current : m_Entities)
    {
        if (
            current.GetParentID() ==
            entity.GetID()
            )
        {
            children.push_back(
                &current
            );
        }
    }

    return children;
}

void Scene::UpdateTransforms()
{
    std::unordered_set<uint64_t> visited;
    for (Entity& entity : m_Entities)
    {
        if (entity.GetParentID() == 0 && !visited.contains(entity.GetID()))
            UpdateEntityTransform(entity, glm::mat4(1.0f), visited);
    }

    // Cenas antigas/importadas podem conter entidades órfãs ou ciclos. Elas
    // ainda recebem uma transformação local e jamais travam a atualização.
    for (Entity& entity : m_Entities)
        if (!visited.contains(entity.GetID()))
            UpdateEntityTransform(entity, glm::mat4(1.0f), visited);
}

void Scene::UpdateEntityTransform(
    Entity& entity,
    const glm::mat4& parentMatrix,
    std::unordered_set<uint64_t>& visited
)
{
    if (!visited.insert(entity.GetID()).second)
        return;

    entity
        .GetTransform()
        .UpdateLocalMatrix();

    entity
        .GetTransform()
        .UpdateWorldMatrix(
            parentMatrix
        );

    // Pasta serve somente para organização; seus filhos herdam o transform
    // do ancestral acima dela. Modelo, por outro lado, é um grupo espacial.
    const glm::mat4& childrenParentMatrix = entity.IsFolder
        ? parentMatrix
        : entity.GetTransform().WorldMatrix;

    for (
        uint64_t childID :
        entity.GetChildrenIDs()
    )
    {
        Entity* child =
            FindEntityByID(
                childID
            );

        if (child && child->GetParentID() == entity.GetID())
        {
            UpdateEntityTransform(
                *child,
                childrenParentMatrix,
                visited
            );
        }
    }
}

LightingManager& Scene::GetLightingManager()
{
    return m_LightingManager;
}

const LightingManager& Scene::GetLightingManager() const
{
    return m_LightingManager;
}
