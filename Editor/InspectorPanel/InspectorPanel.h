#pragma once

#include "../../Assets/AssetDatabase/AssetDatabase.h"
#include "../../Assets/MaterialDatabase/MaterialDatabase.h"
#include <cstdint>
#include <vector>

class Application;
class Entity;

class InspectorPanel
{
public:

    static void Draw(Application& app);

    static void DrawMaterialInspector(
        const AssetInfo& asset
    );

    // Estado compartilhado com o viewport para a seleção de componentes da
    // malha no modo Modelador.
    static bool IsModelingMode();
    static void SetModelingMode(bool enabled);
    static int GetModelingSelectionMode();
    static const std::vector<int>& GetModelingSelection();
    static uint64_t GetModelingEntityID();
    static void SetModelingSelection(uint64_t entityID,
                                     int mode,
                                     const std::vector<int>& components);
    static void SetModelingSelectionMode(int mode);
    static void ClearModelingSelection();
    static void SelectAllModeling(Application& app);
    static void ApplyTopologyAction(Application& app, const std::string& actionName);

    // Persiste qualquer alteração de vértices feita por ferramentas externas
    // ao Inspector, como o gizmo de faces da viewport.
    static void SaveModelingMesh(Application& app, Entity& entity);

    enum class MiniFaceShape { Squares = 0, Rectangles = 1, Triangles = 2 };
    enum class MiniFaceOrientation { Grid = 0, Vertical = 1, Horizontal = 2 };

    static MiniFaceShape GetMiniFaceShape();
    static void SetMiniFaceShape(MiniFaceShape shape);

    static MiniFaceOrientation GetMiniFaceOrientation();
    static void SetMiniFaceOrientation(MiniFaceOrientation orientation);

    static int GetMiniFaceVerticalLines();
    static void SetMiniFaceVerticalLines(int lines);

    static int GetMiniFaceHorizontalLines();
    static void SetMiniFaceHorizontalLines(int lines);

    static bool IsHideDiagonalLines();
    static void SetHideDiagonalLines(bool hide);

    static void SubdivideSelectedIntoMiniFaces(Application& app, Entity& entity);
    static void SubdivideObjectIntoMiniFaces(Application& app, Entity& entity);

private:

    static Material* m_CurrentMaterial;

    static void DrawScriptInspector(Application& app, Entity& entity);
    static void DrawLightInspector(Application& app, Entity& entity);
    static void DrawLocalLightInspector(Application& app, Entity& entity);
    static void DrawAudioInspector(Application& app, Entity& entity);
    static void DrawCameraInspector(Application& app, Entity& entity);
    static void DrawPhysicsInspector(Application& app, Entity& entity);
    static void DrawAnimationInspector(Application& app, Entity& entity);
    static void DrawGlobalLightInspector(Application& app, Entity& entity);
    static void DrawGlobalScriptInspector(Application& app, Entity& entity);
    static void DrawModelingInspector(Application& app, Entity& entity);
    static void DrawComponentInspector(Application& app, Entity& entity, uint64_t componentID);
};
