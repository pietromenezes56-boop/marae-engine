#pragma once

class Scene;

class EditorObjects
{
public:
    static void CreateGrid(
        Scene& scene
    );

    static void CreateAxis(
        Scene& scene
    );

    static void CreateGlobalLight(
        Scene& scene
    );

    static void CreateGlobalScript(
        Scene& scene
    );

    static void CreateDefaultObjects(
        Scene& scene
    );
};
