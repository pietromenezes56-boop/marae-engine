#pragma once

#include <glad/glad.h>
#include "Shader/Shader.h"
#include "GraphicsAPI.h"
#include "SceneRenderer/SceneRenderer.h"
#include "CameraRenderer/CameraRenderer.h"
#include "Sky/SkyRenderer.h"
#include "../Engine/Lighting/LightingManager.h"
#include "../Engine/Renderer/PostProcessing/PostProcessingPipeline.h"
#include "../Hub/ProjectManager.h"
#include "Vulkan/VulkanAccelerator.h"
#include "Vulkan/VulkanDynamicLoader.h"
#include "../Engine/UI/UIRenderer.h"
#include <vector>
#include <string>
#include <cstdint>

class Scene;
class Camera;

class Renderer
{
public:
    Renderer();

    void Init();
    void SetQuality(GraphicsQuality quality);
    void SetRequestedGraphicsAPI(GraphicsAPI api) { m_RequestedGraphicsAPI = api; }
    GraphicsAPI GetRequestedGraphicsAPI() const { return m_RequestedGraphicsAPI; }
    void SetActiveGraphicsAPI(GraphicsAPI api) { m_ActiveGraphicsAPI = api; }
    GraphicsAPI GetActiveGraphicsAPI() const { return m_ActiveGraphicsAPI; }
    void ResizePostProcessing(int width, int height);
    void SetPostElapsedTime(float t) { m_PostPipeline.SetElapsedTime(t); }
    PostProcessingPipeline& GetPostPipeline() { return m_PostPipeline; }

    void Render(
        Scene& scene,
        Camera& camera,
        int selectedEntity,
        const std::vector<int>& multiSelectedEntities,
        int viewportWidth,
        int viewportHeight,
        GLuint targetFBO = 0,
        float shadowIntensity = 0.5f,
        float ambientLight = 0.4f,
        bool showAxis = true,
        bool showGrid = true,
        float gridRenderDistance = 500.0f,
        float cameraFov = 60.0f,
        float cameraNear = 0.1f,
        float cameraFar = 10000.0f,
        bool cameraOrthographic = false,
        float cameraOrthoSize = 10.0f,
        bool editor2DMode = false,
        // Desenha a UI 2D (ScreenGui) por cima da cena. Desligado em renders
        // auxiliares (ex.: pré-visualização de câmera) que não devem mostrar UI.
        bool renderUI = true
    );

    CameraRenderer& GetCameraRenderer();
    SceneRenderer& GetSceneRenderer() { return m_SceneRenderer; }
    const SceneRenderer& GetSceneRenderer() const { return m_SceneRenderer; }
    void SetShadowIntensity(float intensity) { m_ShadowIntensity = intensity; }
    void SetAmbientLight(float intensity) { m_AmbientLight = intensity; }
    
    // Vulkan acceleration (OpenGL remains primary)
    VulkanAccelerator& GetVulkanAccelerator() { return *m_VulkanAccelerator; }
    bool IsVulkanAvailable() const { return m_VulkanAvailable; }
    const SkyRenderer& GetSkyRenderer() const { return m_SkyRenderer; }
    double GetLastSkyRenderMs() const { return m_LastSkyRenderMs; }
    const std::string& GetOpenGLVendor() const { return m_OpenGLVendor; }
    const std::string& GetOpenGLRenderer() const { return m_OpenGLRenderer; }
    const std::string& GetOpenGLVersion() const { return m_OpenGLVersion; }
    const std::string& GetVulkanDiagnostics() const { return m_VulkanDiagnostics; }

private:
    Shader m_Shader;
    SceneRenderer m_SceneRenderer;
    SkyRenderer m_SkyRenderer;
    CameraRenderer m_CameraRenderer;
    UIRenderer m_UIRenderer;
    PostProcessingPipeline m_PostPipeline;
    LightingManager m_LightingManager;
    GraphicsAPI m_RequestedGraphicsAPI = GraphicsAPI::OpenGL;
    GraphicsAPI m_ActiveGraphicsAPI = GraphicsAPI::OpenGL;
    // PostProcessingPipeline m_PostPipeline;
    GLuint m_WhiteTextureID = 0;
    float m_ShadowIntensity = 0.5f;
    float m_AmbientLight = 0.4f;
    GraphicsQuality m_Quality = GraphicsQuality::Balanced;
    int m_LastWidth = 1280;
    int m_LastHeight = 720;
    uint64_t m_TAAFrameIndex = 0;
    glm::vec2 m_TAACurrentJitterNDC = glm::vec2(0.0f);
    glm::vec2 m_TAAPreviousJitterNDC = glm::vec2(0.0f);
    
    // Vulkan acceleration (OpenGL remains primary renderer)
    std::unique_ptr<VulkanDynamicLoader> m_VulkanLoader;
    std::unique_ptr<VulkanAccelerator> m_VulkanAccelerator;
    bool m_VulkanAvailable = false;
    double m_LastSkyRenderMs = 0.0;
    std::string m_OpenGLVendor;
    std::string m_OpenGLRenderer;
    std::string m_OpenGLVersion;
    std::string m_VulkanDiagnostics;
};
