#pragma once

class Scene;
class Entity;

class SceneCommands
{
public:

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