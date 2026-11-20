#pragma once

#include <cstdint>
#include <vector>

class Application;
class Entity;

class HierarchyPanel
{
public:

    static void Draw(
        Application& app
    );

    static void BeginAddParentForEntities(
        Application& app,
        const std::vector<uint64_t>& childrenIDs
    );

private:

    // Accepts non-const Entity& so the method can modify the entity (set parent/children)
    static void DrawEntityNode(
        Application& app,
        Entity& entity
    );

    static void DrawComponentNode(
        Application& app,
        Entity& entity,
        const char* componentName,
        uint64_t componentID
    );
};

