#pragma once

#include "Entity/Entity.h"
#include <vector>

class Scene;
class Entity;

class SceneCommands
{
public:

    struct EntitySelectionSnapshot
    {
        std::vector<Entity> Entities;
        std::vector<uint64_t> RootIDs;
    };

    static EntitySelectionSnapshot CaptureSelection(
        Scene& scene,
        const std::vector<int>& indices
    );

    static std::vector<int> PasteSelection(
        Scene& scene,
        const EntitySelectionSnapshot& snapshot,
        std::vector<uint64_t>* createdIDs = nullptr,
        bool suffixRootNames = true
    );

    static void DeleteEntity(
        Scene& scene,
        int index
    );

    static int DuplicateEntity(
        Scene& scene,
        int index
    );

    static Entity& CreateCube(
        Scene& scene
    );
};
