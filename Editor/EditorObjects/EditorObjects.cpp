#include "EditorObjects.h"
#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include <GLFW/glfw3.h>

void EditorObjects::CreateGrid(
    Scene& scene)
{
    Entity& grid =
        scene.CreateEntity();

    grid.SetName("Grid");

    grid.SetEditorOnly(
        true
    );

    grid.GetMeshComponent().MeshData->Create(
        MeshFactory::CreateGrid(),
        GL_LINES,
        "EditorObjects::CreateGrid"
    );

    grid.GetColorComponent().Color =
        Vector3(
            0.5f,
            0.5f,
            0.5f
        );

    // Sync material color so renderer uses it
    grid.GetMaterialComponent().MaterialData.Color[0] = grid.GetColorComponent().Color.X;
    grid.GetMaterialComponent().MaterialData.Color[1] = grid.GetColorComponent().Color.Y;
    grid.GetMaterialComponent().MaterialData.Color[2] = grid.GetColorComponent().Color.Z;
    grid.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
}

void EditorObjects::CreateAxis(
    Scene& scene)
{
    // Create separate axis entities so each axis can have its own color
    Entity& axisX = scene.CreateEntity();
    axisX.SetName("Axis X");
    axisX.SetEditorOnly(true);
    axisX.GetMeshComponent().MeshData->Create(
        MeshFactory::CreateAxisX(),
        GL_LINES,
        "EditorObjects::CreateAxisX"
    );
    axisX.GetColorComponent().Color = Vector3(1.0f, 0.0f, 0.0f);
    axisX.GetMaterialComponent().MaterialData.Color[0] = axisX.GetColorComponent().Color.X;
    axisX.GetMaterialComponent().MaterialData.Color[1] = axisX.GetColorComponent().Color.Y;
    axisX.GetMaterialComponent().MaterialData.Color[2] = axisX.GetColorComponent().Color.Z;
    axisX.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    axisX.GetTransform().Position.Y = 0.0001f;

    Entity& axisY = scene.CreateEntity();
    axisY.SetName("Axis Y");
    axisY.SetEditorOnly(true);
    axisY.GetMeshComponent().MeshData->Create(
        MeshFactory::CreateAxisY(),
        GL_LINES,
        "EditorObjects::CreateAxisY"
    );
    axisY.GetColorComponent().Color = Vector3(0.0f, 1.0f, 0.0f);
    axisY.GetMaterialComponent().MaterialData.Color[0] = axisY.GetColorComponent().Color.X;
    axisY.GetMaterialComponent().MaterialData.Color[1] = axisY.GetColorComponent().Color.Y;
    axisY.GetMaterialComponent().MaterialData.Color[2] = axisY.GetColorComponent().Color.Z;
    axisY.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    axisY.GetTransform().Position.Y = 0.0001f;

    Entity& axisZ = scene.CreateEntity();
    axisZ.SetName("Axis Z");
    axisZ.SetEditorOnly(true);
    axisZ.GetMeshComponent().MeshData->Create(
        MeshFactory::CreateAxisZ(),
        GL_LINES,
        "EditorObjects::CreateAxisZ"
    );
    axisZ.GetColorComponent().Color = Vector3(0.0f, 0.0f, 1.0f);
    axisZ.GetMaterialComponent().MaterialData.Color[0] = axisZ.GetColorComponent().Color.X;
    axisZ.GetMaterialComponent().MaterialData.Color[1] = axisZ.GetColorComponent().Color.Y;
    axisZ.GetMaterialComponent().MaterialData.Color[2] = axisZ.GetColorComponent().Color.Z;
    axisZ.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    axisZ.GetTransform().Position.Y = 0.0001f;
}

void EditorObjects::CreateDefaultObjects(
    Scene& scene)
{
    CreateGrid(scene);

    // Primeira abertura de uma cena/projeto: apresente iluminação pronta para
    // uso sem exigir que o usuário habilite um sistema avançado manualmente.
    // Cenas existentes com @lighting substituem estes valores ao carregar.
    bool hasGlobalLight = false;
    for (const Entity& entity : scene.GetEntities())
    {
        if (entity.IsGlobalLight())
        {
            hasGlobalLight = true;
            break;
        }
    }
    if (!hasGlobalLight)
    {
        LightingManager& lighting = scene.GetLightingManager();
        LightingSettings& settings = lighting.Settings;
        settings.Enabled = true;
        settings.DynamicShadows = true;
        settings.ContactShadows = true;
        settings.IndirectLighting = true;
        settings.EnvironmentLighting = true;
        settings.Quality = LightingQuality::Performance;
        settings.MaxShadowedLocalLights = 1;
        settings.IndirectIntensity = 0.25f;
        settings.ContactShadowStrength = 0.25f;
        settings.ContactShadowDistance = 0.75f;
        settings.ShadowDistance = 220.0f;
        settings.ShadowMapResolution = 1024;

        lighting.Sun.Direction = Vector3(-0.4f, -1.0f, -0.35f);
        lighting.Sun.Color = Vector3(1.0f, 0.96f, 0.88f);
        lighting.Sun.Intensity = 1.0f;
        lighting.Ambient.Color = Vector3(1.0f, 1.0f, 1.0f);
        lighting.Ambient.Intensity = 0.4f;
    }
    CreateGlobalLight(scene);
    CreateGlobalScript(scene);

    if (!hasGlobalLight)
    {
        for (Entity& entity : scene.GetEntities())
        {
            if (!entity.IsGlobalLight())
                continue;

            GlobalLightComponent& global = entity.GetGlobalLightComponent();
            global.Enabled = true;
            global.Brightness = 1.0f;
            global.Contrast = 1.0f;
            global.Saturation = 1.0f;
            global.Exposure = 0.0f;
            global.Gamma = 1.0f;
            global.ShadowStrength = 0.8f;
            global.Lighting.CloudsEnabled = true;
            global.Lighting.CloudMovementEnabled = false;
            break;
        }
    }
}

void EditorObjects::CreateGlobalLight(Scene& scene)
{
    for (Entity& entity : scene.GetEntities())
    {
        if (entity.IsGlobalLight())
        {
            entity.GetMeshComponent().PrimitiveType = "None";
            entity.GetMeshComponent().MeshData = nullptr;
            entity.GetMeshComponent().MeshPath.clear();
            entity.RaycastTarget = false;
            entity.PhysicsEnabled = false;
            entity.ColliderEnabled = false;
            return;
        }
    }

    Entity& globalLight = scene.CreateEntity();
    globalLight.SetName("Global Light");
    globalLight.SetEditorOnly(true);
    globalLight.SetGlobalLight(true);
    // Global Light não deve ser alvo de raycast e não tem física
    globalLight.RaycastTarget = false;
    globalLight.PhysicsEnabled = false;
    globalLight.ColliderEnabled = false;
    globalLight.GetMeshComponent().PrimitiveType = "None";
    globalLight.GetMeshComponent().MeshData = nullptr;
    globalLight.GetMeshComponent().MeshPath.clear();
}

void EditorObjects::CreateGlobalScript(Scene& scene)
{
    for (Entity& entity : scene.GetEntities())
    {
        if (entity.IsGlobalScript())
        {
            entity.GetMeshComponent().PrimitiveType = "None";
            entity.GetMeshComponent().MeshData = nullptr;
            entity.GetMeshComponent().MeshPath.clear();
            entity.RaycastTarget = false;
            entity.PhysicsEnabled = false;
            entity.ColliderEnabled = false;
            return;
        }
    }

    Entity& globalScript = scene.CreateEntity();
    globalScript.SetName("Global Script");
    globalScript.SetEditorOnly(true);
    globalScript.SetGlobalScript(true);
    globalScript.RaycastTarget = false;
    globalScript.PhysicsEnabled = false;
    globalScript.ColliderEnabled = false;
    globalScript.GetMeshComponent().PrimitiveType = "None";
    globalScript.GetMeshComponent().MeshData = nullptr;
    globalScript.GetMeshComponent().MeshPath.clear();
}
