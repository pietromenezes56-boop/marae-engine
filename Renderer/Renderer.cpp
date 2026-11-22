#include "Renderer.h"
#include <glad/glad.h>
#include "../Scene/Scene/Scene.h"
#include "../Scene/Camera/Camera.h"
#include "../Engine/Lighting/LightingManager.h"
#include "../Scene/Components/GlobalLightComponent.h"
#include "../Scene/Components/MaterialComponent.h"
#include "../Scene/Entity/Entity.h"
#include "../Engine/Renderer/Material/Material.h"
#include "VulkanRuntimeProbe.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>

Renderer::Renderer()
{
}

namespace
{
    float HaltonSequence(uint64_t index, uint32_t base)
    {
        float result = 0.0f;
        float fraction = 1.0f;
        while (index > 0)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    BlendedSettings BuildFallbackFromGlobalLight(const GlobalLightComponent& gl, float ambientLight, float shadowIntensity)
    {
        BlendedSettings s;
        s.Global.Enabled = gl.Enabled;
        s.Global.GlobalIntensity = 1.0f;
        s.Global.AmbientIntensity = ambientLight;
        s.Global.AmbientColor = Vector3(1, 1, 1);
        s.Global.SkyLightIntensity = 1.0f;
        // Legacy exposure is applied by u_G_ExposureEV in the composite pass.
        // Copying it to SceneExposure as well applied the slider twice (2^EV squared).
        s.Global.SceneExposure = 0.0f;
        s.Global.Brightness = gl.Lighting.Brightness;
        s.Global.LegacyContrast = gl.Lighting.LegacyContrast;
        s.Global.LegacySaturation = gl.Lighting.LegacySaturation;
        s.Global.LegacyExposureEV = gl.Lighting.LegacyExposureEV;
        s.Global.ColorTint = gl.Lighting.ColorTint;
        s.Global.ShadowStrength = gl.Lighting.ShadowStrength;
        s.Global.FogEnabled = gl.Lighting.FogEnabled;
        s.Global.FogColor = gl.Lighting.FogColor;
        s.Global.FogDensity = gl.Lighting.FogDensity;
        s.Global.FogStart = gl.Lighting.FogStart;
        s.Global.FogEnd = gl.Lighting.FogEnd;
        s.Global.FogMode = gl.Lighting.FogMode;
        s.Global.ScreenSpaceAOEnabled = gl.Lighting.ScreenSpaceAOEnabled;
        s.Global.ScreenSpaceAOIntensity = gl.Lighting.ScreenSpaceAOIntensity;
        s.Global.ScreenSpaceAORadius = gl.Lighting.ScreenSpaceAORadius;
        s.Global.ScreenSpaceReflectionsEnabled = gl.Lighting.ScreenSpaceReflectionsEnabled;
        s.Global.ScreenSpaceReflectionsStrength = gl.Lighting.ScreenSpaceReflectionsStrength;
        s.Global.ScreenSpaceReflectionsDistance = gl.Lighting.ScreenSpaceReflectionsDistance;
        s.Global.TemporalAAEnabled = gl.Lighting.TemporalAAEnabled;
        s.Global.TemporalAAHistoryWeight = gl.Lighting.TemporalAAHistoryWeight;
        s.Global.VolumetricCloudsEnabled = gl.Lighting.VolumetricCloudsEnabled;
        s.Global.VolumetricCloudBaseHeight = gl.Lighting.VolumetricCloudBaseHeight;
        s.Global.VolumetricCloudThickness = gl.Lighting.VolumetricCloudThickness;
        s.Global.VolumetricCloudDensity = gl.Lighting.VolumetricCloudDensity;
        s.Global.VolumetricCloudScale = gl.Lighting.VolumetricCloudScale;
        s.Global.VolumetricCloudSteps = gl.Lighting.VolumetricCloudSteps;
        s.Global.SunDirection = gl.Lighting.SunDirection;
        s.Global.GodRaysEnabled = gl.Lighting.GodRaysEnabled;
        s.Global.GodRaysIntensity = gl.Lighting.GodRaysIntensity;
        s.Global.GodRaysDensity = gl.Lighting.GodRaysDensity;
        s.Global.GodRaysDecay = gl.Lighting.GodRaysDecay;
        s.Global.GodRaysWeight = gl.Lighting.GodRaysWeight;
        s.Global.GlareRaysEnabled = gl.Lighting.GlareRaysEnabled;
        s.Global.GlareRaysIntensity = gl.Lighting.GlareRaysIntensity;
        s.Global.GlareRaysLength = gl.Lighting.GlareRaysLength;
        s.Global.GlareRaysThreshold = gl.Lighting.GlareRaysThreshold;
        s.Global.Override = true;

        s.Color.Exposure = 0.0f;
        s.Color.Contrast = 1.0f;
        s.Color.Saturation = 1.0f;
        s.Color.Temperature = gl.Temperature;
        s.Color.Tint = gl.Tint;
        s.Color.Brightness = 1.0f;
        s.Color.Gamma = gl.Gamma;
        s.Color.Gain = Vector3(1, 1, 1);
        s.Color.Lift = Vector3(0, 0, 0);
        s.Color.PresetGamma = Vector3(1, 1, 1);
        s.Color.Shadows = Vector3(1, 1, 1);
        s.Color.ShadowsWeight = 0.08f;
        s.Color.Midtones = Vector3(1, 1, 1);
        s.Color.MidtonesWeight = 0.54f;
        s.Color.Highlights = Vector3(1, 1, 1);
        s.Color.HighlightsWeight = 0.92f;
        s.Color.Override = true;

        s.Tone.Enabled = true;
        s.Tone.Op = (ToneMappingSettings::Operator)(int)gl.ToneMapping;
        s.Tone.ReinhardWhitePoint = 1.0f;
        s.Tone.Intensity = 1.0f;
        s.Tone.Override = true;

        s.Bloom.Enabled = gl.BloomStrength > 0.001f;
        s.Bloom.Intensity = gl.BloomStrength;
        s.Bloom.Threshold = 0.9f;
        s.Bloom.Knee = 0.5f;
        s.Bloom.Radius = 0.7f;
        s.Bloom.MipCount = 5;
        s.Bloom.Override = true;

        s.Vignette.Enabled = gl.VignetteStrength > 0.001f;
        s.Vignette.Intensity = gl.VignetteStrength;
        s.Vignette.Radius = gl.VignetteRadius;
        s.Vignette.Smoothness = 0.4f;
        s.Vignette.Feather = 0.2f;
        s.Vignette.Color = Vector3(0, 0, 0);
        s.Vignette.Override = true;

        s.FilmGrain.Enabled = gl.FilmGrainEnabled;
        s.FilmGrain.Intensity = gl.FilmGrainStrength;
        s.FilmGrain.GrainSize = gl.FilmGrainSize;
        s.FilmGrain.Speed = gl.FilmGrainSpeed;
        s.FilmGrain.ShadowWeight = 0.6f;
        s.FilmGrain.MidWeight = 1.0f;
        s.FilmGrain.HighWeight = 0.8f;
        s.FilmGrain.Override = true;

        s.Chromatic.Enabled = gl.ChromaticAberrationEnabled;
        s.Chromatic.Strength = gl.ChromaticAberrationStrength;
        s.Chromatic.EdgeBias = 1.0f;
        s.Chromatic.Override = true;

        s.Sharpen.Enabled = gl.SharpenStrength > 0.001f;
        s.Sharpen.Strength = gl.SharpenStrength;
        s.Sharpen.Clamp = 0.035f;
        s.Sharpen.Override = true;

        s.Lut.Override = true;
        s.LensDistortion.Override = true;
        s.DoF.Override = true;
        s.MotionBlur.Override = true;

        (void)shadowIntensity;
        return s;
    }
}

namespace
{
    bool IsVulkanEnabledInConfig()
    {
        // Try multiple possible config file locations
        std::vector<std::string> configPaths = {
            "Library/renderer_config.cfg",
            "../Library/renderer_config.cfg",
            "../../Library/renderer_config.cfg"
        };

        for (const auto& path : configPaths)
        {
            std::ifstream config(path);
            if (config.is_open())
            {
                std::cout << "[Renderer] Config file found: " << path << std::endl;

                std::string line;
                while (std::getline(config, line))
                {
                    if (line.find("VulkanEnabled=") == 0)
                    {
                        std::string value = line.substr(line.find('=') + 1);
                        bool enabled = (value == "true" || value == "1");
                        std::cout << "[Renderer] Config: VulkanEnabled=" << (enabled ? "true" : "false") << std::endl;
                        return enabled;
                    }
                }

                std::cout << "[Renderer] VulkanEnabled not found in config, disabled by default" << std::endl;
                return false;
            }
        }

        std::cout << "[Renderer] Config file not found in any location, Vulkan disabled by default" << std::endl;
        std::cout << "[Renderer] Tried: Library/renderer_config.cfg, ../Library/renderer_config.cfg, ../../Library/renderer_config.cfg" << std::endl;
        return false;
    }

    bool DetectsIntelUHD(const std::string& vulkanOutput)
    {
        // Check if the output contains Intel UHD or Intel HD Graphics
        return (vulkanOutput.find("Intel(R) UHD") != std::string::npos) ||
               (vulkanOutput.find("Intel HD Graphics") != std::string::npos) ||
               (vulkanOutput.find("INCOMPATIBLE") != std::string::npos);
    }

    bool IsKnownRiskIntelIntegratedRenderer(const std::string& renderer)
    {
        std::string normalized = renderer;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Intel Arc is deliberately excluded; this guard targets legacy Intel
        // integrated adapters for which this engine has a known Vulkan probe risk.
        if (normalized.find("arc") != std::string::npos)
            return false;

        return normalized.find("intel(r) uhd") != std::string::npos ||
               normalized.find("intel uhd") != std::string::npos ||
               normalized.find("intel(r) hd graphics") != std::string::npos ||
               normalized.find("intel hd graphics") != std::string::npos;
    }
}

void Renderer::SetQuality(GraphicsQuality quality)
{
    m_Quality = quality;
    m_SceneRenderer.SetQuality(quality);

    switch (quality)
    {
        case GraphicsQuality::Performance:
            m_SkyRenderer.SetCloudQuality(0);
            m_PostPipeline.SetQuality(PostProcessingPipeline::QualityLevel::Low);
            break;
        case GraphicsQuality::UltraPerformance:
            m_SkyRenderer.SetCloudQuality(0);
            m_PostPipeline.SetQuality(PostProcessingPipeline::QualityLevel::Low);
            break;
        case GraphicsQuality::Balanced:
            m_SkyRenderer.SetCloudQuality(3);
            m_PostPipeline.SetQuality(PostProcessingPipeline::QualityLevel::Medium);
            break;
        case GraphicsQuality::HighFidelity:
            m_SkyRenderer.SetCloudQuality(4);
            m_PostPipeline.SetQuality(PostProcessingPipeline::QualityLevel::High);
            break;
        case GraphicsQuality::Ultra:
            m_SkyRenderer.SetCloudQuality(5);
            m_PostPipeline.SetQuality(PostProcessingPipeline::QualityLevel::Ultra);
            break;
    }
}

void Renderer::ResizePostProcessing(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    m_LastWidth = width;
    m_LastHeight = height;
    if (m_PostPipeline.GetWidth() > 0 && m_PostPipeline.GetHeight() > 0)
        m_PostPipeline.Resize(width, height);
}

void Renderer::Init()
{
    if (!gladLoadGL())
    {
        std::cout << "Erro: GLAD não foi carregado corretamente!" << std::endl;
        return;
    }

    if (!glEnable)
    {
        std::cout << "Erro: Funções OpenGL não disponíveis!" << std::endl;
        return;
    }

    const auto glString = [](GLenum name) -> std::string
    {
        const GLubyte* value = glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "Indisponível";
    };
    m_OpenGLVendor = glString(GL_VENDOR);
    m_OpenGLRenderer = glString(GL_RENDERER);
    m_OpenGLVersion = glString(GL_VERSION);
    std::cout << "[Renderer] OpenGL hardware: " << m_OpenGLVendor << " | "
              << m_OpenGLRenderer << " | " << m_OpenGLVersion << std::endl;

    m_Shader.Create();
    m_SkyRenderer.Init();
    m_UIRenderer.Init();

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &m_WhiteTextureID);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (!m_PostPipeline.Init(m_LastWidth, m_LastHeight))
    {
        std::cerr << "[Renderer] Falha ao inicializar pipeline de Post-Processing." << std::endl;
    }

    // The Vulkan probe itself has previously triggered a critical driver fault
    // on legacy Intel UHD/HD adapters. Do not enter that code path on those GPUs.
    bool vulkanConfigEnabled = IsVulkanEnabledInConfig();

    if (vulkanConfigEnabled && IsKnownRiskIntelIntegratedRenderer(m_OpenGLRenderer))
    {
        std::cout << "[Renderer] Vulkan probe skipped: legacy Intel integrated GPU detected ("
                  << m_OpenGLRenderer << ")." << std::endl;
        std::cout << "[Renderer] OpenGL remains active; no Vulkan runtime probe was attempted." << std::endl;
        vulkanConfigEnabled = false;
    }

    if (!vulkanConfigEnabled)
    {
        std::cout << "[Renderer] Vulkan acceleration DISABLED by configuration" << std::endl;
        std::cout << "[Renderer] OpenGL will be used as the primary and only renderer" << std::endl;
        std::cout << "[Renderer] To enable Vulkan, set VulkanEnabled=true in Library/renderer_config.cfg" << std::endl;
        m_VulkanAvailable = false;
    }
    else
    {
        std::cout << "[Renderer] Vulkan acceleration ENABLED by configuration" << std::endl;
        std::cout << "[Renderer] Detecting GPU compatibility..." << std::endl;

        try
        {
            std::stringstream vulkanOutput;
            m_VulkanAvailable = VulkanRuntimeProbe::RunDiagnostics(vulkanOutput);
            m_VulkanDiagnostics = vulkanOutput.str();
            if (m_VulkanAvailable)
            {
                std::cout << "[Renderer] Vulkan detected:" << std::endl;
                std::cout << m_VulkanDiagnostics;

                // Auto-detect Intel UHD Graphics
                if (DetectsIntelUHD(m_VulkanDiagnostics))
                {
                    std::cout << "[Renderer] Intel UHD Graphics detected - Vulkan DISABLED" << std::endl;
                    std::cout << "[Renderer] Intel UHD driver has critical bug in vkGetInstanceProcAddr" << std::endl;
                    std::cout << "[Renderer] Validation layers did NOT fix the issue" << std::endl;
                    std::cout << "[Renderer] Vulkan is NOT compatible with Intel UHD Graphics (pre-Arc)" << std::endl;
                    std::cout << "[Renderer] OpenGL will be used as the only renderer" << std::endl;
                    std::cout << "[Renderer] Note: Intel Arc GPUs may work with Vulkan" << std::endl;
                    m_VulkanAvailable = false;
                }
                else
                {
                    std::cout << "[Renderer] GPU is compatible - attempting Vulkan initialization..." << std::endl;
                    m_VulkanLoader = std::make_unique<VulkanDynamicLoader>();
                    if (m_VulkanLoader->Initialize())
                    {
                        std::cout << "[Renderer] Vulkan dynamic loader initialized successfully" << std::endl;
                        std::cout << "[Renderer] Vulkan hybrid system ready (OpenGL remains primary)" << std::endl;
                    }
                    else
                    {
                        std::cout << "[Renderer] Vulkan loader initialization failed, using OpenGL only" << std::endl;
                        m_VulkanAvailable = false;
                        m_VulkanLoader.reset();
                    }
                }
            }
            else
            {
                std::cout << "[Renderer] Vulkan not available on this system, using OpenGL only" << std::endl;
                std::cout << vulkanOutput.str();
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "[Renderer] Exception during Vulkan initialization: " << e.what() << std::endl;
            std::cout << "[Renderer] Falling back to OpenGL only" << std::endl;
            m_VulkanAvailable = false;
            m_VulkanLoader.reset();
        }
        catch (...)
        {
            std::cout << "[Renderer] Unknown exception during Vulkan initialization" << std::endl;
            std::cout << "[Renderer] Falling back to OpenGL only" << std::endl;
            m_VulkanAvailable = false;
            m_VulkanLoader.reset();
        }
    }

    std::cout << "Renderer inicializado!" << std::endl;
}


void Renderer::Render(
    Scene& scene,
    Camera& camera,
    int selectedEntity,
    const std::vector<int>& multiSelectedEntities,
    int viewportWidth,
    int viewportHeight,
    GLuint targetFBO,
    float shadowIntensity,
    float ambientLight,
    bool showAxis,
    bool showGrid,
    float gridRenderDistance,
    float cameraFov,
    float cameraNear,
    float cameraFar,
    bool cameraOrthographic,
    float cameraOrthoSize,
    bool editor2DMode,
    bool renderUI)
{
    if (viewportWidth <= 0) viewportWidth = 1;
    if (viewportHeight <= 0) viewportHeight = 1;
    m_LastWidth = viewportWidth;
    m_LastHeight = viewportHeight;

    // Ultra Performance deve evitar o framebuffer HDR e o passe de composição
    // por completo. Redimensioná-lo mesmo sem usá-lo alocava memória e trabalho
    // de GPU desnecessários em cada mudança de tamanho do viewport.
    const bool useSceneEffectsComposite =
        !editor2DMode && m_Quality != GraphicsQuality::UltraPerformance &&
        m_PostPipeline.IsValid();
    if (useSceneEffectsComposite &&
        (m_PostPipeline.GetWidth() != viewportWidth || m_PostPipeline.GetHeight() != viewportHeight))
        m_PostPipeline.Resize(viewportWidth, viewportHeight);

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    GlobalLightComponent globalLight;
    for (const Entity& entity : scene.GetEntities())
    {
        if (entity.IsGlobalLight())
        {
            globalLight = entity.GetGlobalLightComponent();
            break;
        }
    }

    // O perfil é um limite de custo em tempo de execução: opções pesadas que
    // estejam salvas na cena continuam intactas no Inspector/arquivo, mas não
    // podem reativar efeitos caros enquanto Ultra Performance estiver ativo.
    if (m_Quality == GraphicsQuality::UltraPerformance)
    {
        globalLight.Lighting.ScreenSpaceAOEnabled = false;
        globalLight.Lighting.ScreenSpaceReflectionsEnabled = false;
        globalLight.Lighting.TemporalAAEnabled = false;
        globalLight.Lighting.VolumetricCloudsEnabled = false;
        globalLight.Lighting.GodRaysEnabled = false;
        globalLight.Lighting.GlareRaysEnabled = false;
        globalLight.BloomStrength = 0.0f;
        globalLight.VignetteStrength = 0.0f;
    }

    glm::mat4 view = m_CameraRenderer.GetViewMatrix(camera);

    const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);

    glm::mat4 projection;
    if (cameraOrthographic)
    {
        const float halfHeight = std::max(0.01f, cameraOrthoSize) * 0.5f;
        const float halfWidth = halfHeight * aspectRatio;
        projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                std::max(0.001f, cameraNear), std::max(cameraNear + 0.01f, cameraFar));
    }
    else
    {
        projection = m_CameraRenderer.GetProjectionMatrix(
            std::clamp(cameraFov, 1.0f, 179.0f),
            aspectRatio,
            std::max(0.001f, cameraNear),
            std::max(cameraNear + 0.01f, cameraFar));
    }

    // Sub-pixel Halton jitter gives the temporal filter different samples over
    // time. The history UV offset follows that jitter and is invalidated when
    // TAA is switched off or the viewport enters 2D mode.
    if (globalLight.Lighting.TemporalAAEnabled && !editor2DMode && m_PostPipeline.IsValid())
    {
        m_TAAPreviousJitterNDC = m_TAACurrentJitterNDC;
        const uint64_t sample = (m_TAAFrameIndex++ % 8u) + 1u;
        const glm::vec2 projectionJitter(
            (HaltonSequence(sample, 2u) - 0.5f) * 2.0f / static_cast<float>(viewportWidth),
            (HaltonSequence(sample, 3u) - 0.5f) * 2.0f / static_cast<float>(viewportHeight));
        if (cameraOrthographic)
        {
            projection[3][0] += projectionJitter.x;
            projection[3][1] += projectionJitter.y;
            m_TAACurrentJitterNDC = projectionJitter;
        }
        else
        {
            projection[2][0] += projectionJitter.x;
            projection[2][1] += projectionJitter.y;
            m_TAACurrentJitterNDC = -projectionJitter;
        }
        m_PostPipeline.SetTemporalHistoryUVOffset(
            (m_TAAPreviousJitterNDC - m_TAACurrentJitterNDC) * 0.5f);
    }
    else
    {
        m_TAAFrameIndex = 0;
        m_TAACurrentJitterNDC = glm::vec2(0.0f);
        m_TAAPreviousJitterNDC = glm::vec2(0.0f);
        m_PostPipeline.SetTemporalHistoryUVOffset(glm::vec2(0.0f));
    }

    // Compose the complete scene through HDR so Global Light screen filters
    // affect geometry and sky uniformly. If post processing is unavailable,
    // rendering falls back to the direct target below.
    if (!globalLight.Lighting.TemporalAAEnabled || editor2DMode || !m_PostPipeline.IsValid())
        m_PostPipeline.ReleaseTemporalHistory();

    // Screen filters now run once on the final composite, rather than being
    // baked into the sky and then repeated on top of the complete image.
    m_SkyRenderer.SetGlobalLightFilter(
        useSceneEffectsComposite ? true : globalLight.Enabled,
        useSceneEffectsComposite ? 1.0f : globalLight.Brightness,
        useSceneEffectsComposite ? 1.0f : globalLight.Contrast,
        useSceneEffectsComposite ? 1.0f : globalLight.Saturation,
        useSceneEffectsComposite ? 0.0f : globalLight.Exposure,
        useSceneEffectsComposite ? 1.0f : globalLight.ColorTint.X,
        useSceneEffectsComposite ? 1.0f : globalLight.ColorTint.Y,
        useSceneEffectsComposite ? 1.0f : globalLight.ColorTint.Z
    );
    m_SkyRenderer.SetGlobalLightAtmosphere(
        useSceneEffectsComposite ? 0.0f : globalLight.VignetteStrength,
        globalLight.VignetteRadius,
        useSceneEffectsComposite ? 1.0f : globalLight.Gamma,
        static_cast<float>(viewportWidth),
        static_cast<float>(viewportHeight)
    );
    m_SkyRenderer.SetGlobalFog(
        globalLight.FogEnabled && !useSceneEffectsComposite,
        glm::vec3(globalLight.FogColor.X, globalLight.FogColor.Y, globalLight.FogColor.Z),
        globalLight.FogDensity,
        globalLight.FogStart,
        globalLight.FogEnd
    );
    m_SkyRenderer.SetSkyAppearance(globalLight.Lighting);

    auto& lightingSettings = scene.GetLightingManager().Settings;
    lightingSettings.Clamp();

    // Nunca deixe a cena presa em um FBO HDR sem composição: se um shader de
    // pós-processamento falhar, renderize diretamente no alvo do viewport.
    glBindFramebuffer(GL_FRAMEBUFFER,
        useSceneEffectsComposite ? m_PostPipeline.GetSceneFBO() : targetFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    // O mundo 2D não usa céu procedural nem o horizonte atmosférico 3D.
    // Um fundo neutro mantém sprites e formas legíveis no editor.
    if (editor2DMode)
        glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
    else
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!editor2DMode)
    {
        const auto skyStart = std::chrono::steady_clock::now();
        m_SkyRenderer.Render(
            view,
            projection,
            scene.GetLightingManager(),
            &lightingSettings
        );
        m_LastSkyRenderMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - skyStart).count();
    }
    else
        m_LastSkyRenderMs = 0.0;

    m_Shader.Use();

    m_Shader.SetViewMatrix(view);
    m_Shader.SetProjectionMatrix(projection);
    m_Shader.SetCameraPosition(
        camera.Position.X,
        camera.Position.Y,
        camera.Position.Z
    );

    const float effectiveShadow = shadowIntensity * (globalLight.Enabled ? globalLight.ShadowStrength : 1.0f);
    m_Shader.SetShadowIntensity(effectiveShadow);
    m_Shader.SetAmbientLight(
        editor2DMode ? 0.0f : (lightingSettings.EnvironmentLighting ? ambientLight : 0.0f));
    m_Shader.SetGlobalLightingParams(
        editor2DMode ? false : globalLight.Enabled,
        std::max(globalLight.Brightness, 0.001f),
        lightingSettings.EnvironmentLighting ? 1.0f : 0.0f,
        lightingSettings.EnvironmentLighting ? 1.0f : 0.0f,
        globalLight.Exposure,
        glm::vec3(
            std::max(globalLight.ColorTint.X, 0.001f),
            std::max(globalLight.ColorTint.Y, 0.001f),
            std::max(globalLight.ColorTint.Z, 0.001f))
        , editor2DMode ? false : lightingSettings.Enabled,
        lightingSettings.IndirectLighting ? lightingSettings.IndirectIntensity : 0.0f,
        lightingSettings.ContactShadows ? lightingSettings.ContactShadowStrength : 0.0f,
        lightingSettings.ContactShadowDistance
    );
    m_Shader.SetGlobalLightFilter(
        globalLight.Enabled,
        globalLight.Brightness,
        globalLight.Contrast,
        globalLight.Saturation,
        globalLight.Exposure,
        globalLight.ColorTint.X,
        globalLight.ColorTint.Y,
        globalLight.ColorTint.Z
    );
    m_Shader.SetGlobalLightAtmosphere(
        globalLight.FogEnabled && !useSceneEffectsComposite && !editor2DMode,
        globalLight.FogColor.X,
        globalLight.FogColor.Y,
        globalLight.FogColor.Z,
        globalLight.FogEnabled ? std::max(globalLight.FogDensity, 0.0001f) : 0.0f,
        std::max(globalLight.FogStart, 0.0f),
        std::max(globalLight.FogEnd, globalLight.FogStart + 1.0f),
        globalLight.VignetteStrength,
        globalLight.VignetteRadius,
        globalLight.Gamma,
        static_cast<float>(viewportWidth),
        static_cast<float>(viewportHeight)
    );
    m_Shader.SetPostProcessing(
        static_cast<int>(globalLight.ToneMapping),
        globalLight.Temperature,
        globalLight.Tint,
        globalLight.FilmGrainEnabled,
        globalLight.FilmGrainStrength,
        globalLight.FilmGrainSize,
        globalLight.FilmGrainSpeed,
        globalLight.ChromaticAberrationEnabled,
        globalLight.ChromaticAberrationStrength,
        globalLight.BloomStrength,
        globalLight.SharpenStrength
    );

    // ── Pré-passo: Reflexo Planar para entidades Mirror ───────────────────
    // Resolve qual qualidade usar (por entidade ou global) e renderiza a
    // cena do ponto de vista refletido no FBO antes do render principal.
    if (!editor2DMode)
    {
        // Mapeamento de GraphicsQuality/MirrorQuality → resolução do FBO
        auto mirrorResFromQuality = [](GraphicsQuality gq, MirrorQuality mq) -> int
        {
            // Se a entidade herda do projeto, usa a qualidade gráfica da engine
            MirrorQuality effective = (mq == MirrorQuality::InheritProject)
                ? static_cast<MirrorQuality>(static_cast<int>(gq) + 1)
                : mq;
            switch (effective)
            {
                case MirrorQuality::UltraPerformance: return 0;    // 0 = Sem passe extra de FBO (usa reflexo cromado PBR instantâneo)
                case MirrorQuality::Performance:      return 256;  // 256x256 econômico
                case MirrorQuality::Balanced:         return 512;  // 512x512 padrão de boa qualidade
                case MirrorQuality::HighFidelity:     return 1024; // 1024x1024 alta fidelidade
                case MirrorQuality::Ultra:            return 1024; // 1024x1024 cristalino em tempo real
                default:                              return 512;
            }
        };

        static int s_MirrorFrameCounter = 0;
        ++s_MirrorFrameCounter;

        for (Entity& entity : scene.GetEntities())
        {
            if (entity.IsEditorOnly()) continue;
            MaterialComponent& mc = entity.GetMaterialComponent();
            Material* mat = &mc.MaterialData;
            if (mat->Type != MaterialType::Mirror) continue;

            int res = mirrorResFromQuality(m_Quality, mat->MirrorReflectionQuality);
            if (res <= 0) continue;

            // Throttle: Performance=a cada 2 frames, Balanced/High/Ultra=todo frame
            MirrorQuality mq = mat->MirrorReflectionQuality == MirrorQuality::InheritProject
                ? static_cast<MirrorQuality>(static_cast<int>(m_Quality) + 1)
                : mat->MirrorReflectionQuality;
            int stride = (mq == MirrorQuality::Performance) ? 2 : 1;
            if ((s_MirrorFrameCounter % stride) != 0) continue;

            // Usa a matriz de mundo real da entidade (respeita pais e modelos!)
            const glm::mat4& model = entity.GetTransform().WorldMatrix;
            glm::mat4 invView = glm::inverse(view);
            glm::vec3 camPos = glm::vec3(invView[3]);
            glm::vec3 mirrorPos = glm::vec3(model[3]);
            glm::vec3 toCam = camPos - mirrorPos;

            // Determina a normal correta no espaço de mundo do espelho
            glm::vec3 localNormal(0.0f, 1.0f, 0.0f); // Default Y (chão / plane)
            const std::string& prim = entity.GetMeshComponent().PrimitiveType;
            if (prim == "Quad")
            {
                localNormal = glm::vec3(0.0f, 0.0f, 1.0f); // Quad é virado para +Z
            }
            else if (prim == "Cube")
            {
                // Para cubo, escolhe a face mais voltada para a câmera
                const glm::vec3 cubeNormals[6] = {
                    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
                    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
                    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }
                };
                float bestDot = -1e9f;
                for (int n = 0; n < 6; ++n)
                {
                    glm::vec3 wN = glm::normalize(glm::vec3(model * glm::vec4(cubeNormals[n], 0.0f)));
                    float d = glm::dot(wN, toCam);
                    if (d > bestDot)
                    {
                        bestDot = d;
                        localNormal = cubeNormals[n];
                    }
                }
            }
            else if (entity.GetMeshComponent().MeshData && !entity.GetMeshComponent().MeshData->GetVertices().empty())
            {
                const auto& v = entity.GetMeshComponent().MeshData->GetVertices();
                localNormal = glm::vec3(v[0].NX, v[0].NY, v[0].NZ);
            }

            glm::vec3 normalWorld = glm::normalize(glm::vec3(model * glm::vec4(localNormal, 0.0f)));
            if (glm::dot(normalWorld, toCam) < 0.0f && prim == "Plane")
            {
                normalWorld = -normalWorld;
            }

            if (prim == "Cube")
            {
                glm::vec3 halfScale = glm::vec3(
                    glm::length(glm::vec3(model[0])),
                    glm::length(glm::vec3(model[1])),
                    glm::length(glm::vec3(model[2]))
                ) * 0.5f;
                mirrorPos += normalWorld * std::abs(glm::dot(halfScale, localNormal));
            }

            m_SceneRenderer.RenderReflection(
                scene, m_Shader, scene.GetLightingManager(),
                model, normalWorld, view, projection, res, m_WhiteTextureID, entity.GetID());

            break; // Primeiro espelho por frame
        }
    }

    m_SceneRenderer.Render(
        scene,
        m_Shader,
        scene.GetLightingManager(),
        selectedEntity,
        multiSelectedEntities,
        m_WhiteTextureID,
        view,
        projection,
        showAxis,
        showGrid,
        gridRenderDistance,
        editor2DMode
    );

    BlendedSettings settings = BuildFallbackFromGlobalLight(globalLight, ambientLight, shadowIntensity);
    settings.Global.SunDirection = scene.GetLightingManager().Sun.Direction;

    if (useSceneEffectsComposite)
    {
        m_PostPipeline.Execute(
            settings,
            view,
            projection,
            glm::vec3(camera.Position.X, camera.Position.Y, camera.Position.Z),
            targetFBO);
    }

    // A UI 2D é desenhada por cima do resultado final (depois do
    // pós-processamento) para não sofrer color grading/blur e para aparecer
    // igual no editor e no jogo exportado.
    if (renderUI)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(0, 0, viewportWidth, viewportHeight);
        m_UIRenderer.Render(scene, viewportWidth, viewportHeight);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}

CameraRenderer& Renderer::GetCameraRenderer()
{
    return m_CameraRenderer;
}
