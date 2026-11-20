#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "../../Engine/Lighting/LightingManager.h"
#include "../../Engine/Renderer/PostProcessing/CommonStructs.h"

class SkyRenderer
{
public:
    SkyRenderer();
    ~SkyRenderer();

    bool Init();
    void SetCloudQuality(int octaves);
    int GetCloudQuality() const { return m_CloudOctaves; }
    bool AreCloudsEnabled() const { return m_CloudsEnabled; }
    float GetCloudAmount() const { return m_CloudAmount; }
    void SetGlobalLightFilter(
        bool enabled,
        float brightness,
        float contrast,
        float saturation,
        float exposure,
        float tintR,
        float tintG,
        float tintB
    );
    void SetGlobalLightAtmosphere(
        float vignetteStrength,
        float vignetteRadius,
        float gamma,
        float viewportWidth,
        float viewportHeight
    );
    void SetGlobalFog(bool enabled, const glm::vec3& color, float density,
                      float start, float end);
    void SetSkyAppearance(const GlobalLightingSettings& settings);
    void Render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const LightingManager& lighting,
        const LightingSettings* advancedSettings = nullptr
    );

private:
    GLuint m_ProgramID = 0;
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;

    int m_LocInvView = -1;
    int m_LocInvProjection = -1;
    int m_LocSunDirection = -1;
    int m_LocSunColor = -1;
    int m_LocSunIntensity = -1;
    int m_LocMoonDirection = -1;
    int m_LocMoonColor = -1;
    int m_LocMoonIntensity = -1;
    int m_LocSkyAmbientFactor = -1;
    int m_LocTime = -1;
    int m_LocCloudOctaves = -1;
    int m_LocGlobalEnabled = -1;
    int m_LocGlobalBrightness = -1;
    int m_LocGlobalContrast = -1;
    int m_LocGlobalSaturation = -1;
    int m_LocGlobalExposure = -1;
    int m_LocGlobalTint = -1;
    int m_LocGlobalGamma = -1;
    int m_LocGlobalVignetteStrength = -1;
    int m_LocGlobalVignetteRadius = -1;
    int m_LocViewportSize = -1;
    int m_LocFogEnabled = -1;
    int m_LocFogColor = -1;
    int m_LocFogDensity = -1;
    int m_LocFogStart = -1;
    int m_LocFogEnd = -1;
    int m_LocSkyZenithColor = -1;
    int m_LocSkyHorizonColor = -1;
    int m_LocSkyGroundColor = -1;
    int m_LocCloudsEnabled = -1;
    int m_LocCloudColor = -1;
    int m_LocCloudAmount = -1;
    int m_LocCloudMovementEnabled = -1;
    int m_LocCloudSpeed = -1;
    int m_LocCloudVolumeEnabled = -1;
    int m_LocCloudVolumeBaseHeight = -1;
    int m_LocCloudVolumeThickness = -1;
    int m_LocCloudVolumeDensity = -1;
    int m_LocCloudVolumeScale = -1;
    int m_LocCloudVolumeSteps = -1;
    int m_CloudOctaves = 5;
    int m_CloudQualityCap = 5;
    glm::vec3 m_SkyZenithColor = glm::vec3(0.18f, 0.44f, 0.85f);
    glm::vec3 m_SkyHorizonColor = glm::vec3(0.72f, 0.84f, 0.96f);
    glm::vec3 m_SkyGroundColor = glm::vec3(0.38f, 0.42f, 0.46f);
    bool m_CloudsEnabled = true;
    glm::vec3 m_CloudColor = glm::vec3(0.58f, 0.65f, 0.76f);
    float m_CloudAmount = 0.50f;
    bool m_CloudMovementEnabled = true;
    float m_CloudSpeed = 1.0f;
    bool m_CloudVolumeEnabled = false;
    float m_CloudVolumeBaseHeight = 900.0f;
    float m_CloudVolumeThickness = 700.0f;
    float m_CloudVolumeDensity = 0.65f;
    float m_CloudVolumeScale = 1.0f;
    int m_CloudVolumeSteps = 8;
    bool m_GlobalEnabled = true;
    float m_GlobalBrightness = 1.0f;
    float m_GlobalContrast = 1.0f;
    float m_GlobalSaturation = 1.0f;
    float m_GlobalExposure = 0.0f;
    glm::vec3 m_GlobalTint = glm::vec3(1.0f);
    float m_GlobalGamma = 1.0f;
    float m_GlobalVignetteStrength = 0.0f;
    float m_GlobalVignetteRadius = 0.72f;
    glm::vec2 m_ViewportSize = glm::vec2(1280.0f, 720.0f);
    bool m_FogEnabled = false;
    glm::vec3 m_FogColor = glm::vec3(0.55f, 0.62f, 0.70f);
    float m_FogDensity = 0.015f;
    float m_FogStart = 10.0f;
    float m_FogEnd = 250.0f;
};
