#pragma once
#include "../Entity/Entity.h"
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include "../../Math/Vector3.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../../Engine/Lighting/LightingManager.h"

class Scene
{
public:
    Scene();
    void Clear();
    Entity& CreateEntity();
    void DeleteEntity(int index);
    Entity* FindEntityByID(
        uint64_t id
    );
    std::vector<Entity>& GetEntities();
    const std::vector<Entity>& GetEntities() const;

    void SetParent(
        uint64_t childID,
        uint64_t parentID,
        bool preserveWorldTransform = true
    );

    void RemoveParent(
        uint64_t childID,
        bool preserveWorldTransform = false
    );

    Entity* FindParent(
        const Entity& entity
    );

    std::vector<Entity*> GetChildren(
        const Entity& entity
    );

    void UpdateTransforms();

    LightingManager& GetLightingManager();
    const LightingManager& GetLightingManager() const;

    void MoveChildren(
        Entity& parent,
        const Vector3& delta
    );

    void RotateChildren(
        Entity& parent,
        const glm::quat& deltaRotation,
        const Vector3& pivot
    );

    void ScaleChildren(
        Entity& parent,
        const Vector3& oldScale,
        const Vector3& newScale,
        const Vector3& pivot
    );

private:
    void RebuildEntityIndex() const;
    void UpdateEntityTransform(
        Entity& entity,
        const glm::mat4& parentMatrix,
        std::unordered_set<uint64_t>& visited
    );
    std::vector<Entity> m_Entities;
    // IDs são consultados repetidamente por hierarquia, scripts e física.
    // Guardamos o índice (não ponteiros, pois o vetor pode realocar) e
    // validamos a entrada para tolerar alterações legadas via GetEntities().
    mutable std::unordered_map<uint64_t, size_t> m_EntityIndex;
    LightingManager m_LightingManager;
    // Rebuild: no-op change to force header update.
};
