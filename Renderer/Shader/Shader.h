#pragma once

#include <glm/glm.hpp>
#include <vector>

class Shader
{
public:
    Shader();
    ~Shader();

    bool Create();
    void Use();

    void SetOffset(float x, float y, float z);

    void SetModelMatrix(
        const glm::mat4& model);
    // Mantém espaço para as demais matrizes/uniforms mesmo no limite mínimo
    // de vertex uniforms garantido pelo OpenGL 3.3.
    static constexpr int MAX_SKIN_JOINTS = 48;
    void SetSkinMatrices(const std::vector<glm::mat4>& matrices);

    void SetViewMatrix(
        const glm::mat4& view);

    void SetProjectionMatrix(
        const glm::mat4& projection);
    void SetCameraPosition(float x, float y, float z);
    void SetColor(
        float r,
        float g,
        float b);
    void SetOpacity(float opacity);

    void SetAmbientLight(
        float r,
        float g,
        float b
    );

    void SetLightDirection(
        float x,
        float y,
        float z
    );

    void SetLightColor(
        float r,
        float g,
        float b
    );

    void SetLightIntensity(
        float intensity
    );

    void SetShadowIntensity(
        float intensity
    );

    void SetAmbientLight(
        float intensity
    );

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
        bool fogEnabled,
        float fogR,
        float fogG,
        float fogB,
        float fogDensity,
        float fogStart,
        float fogEnd,
        float vignetteStrength,
        float vignetteRadius,
        float gamma,
        float viewportWidth,
        float viewportHeight
    );

    void SetPostProcessing(
        int toneMapping,
        float temperature,
        float tint,
        bool filmGrainEnabled,
        float filmGrainStrength,
        float filmGrainSize,
        float filmGrainSpeed,
        bool chromaticAberrationEnabled,
        float chromaticAberrationStrength,
        float bloomStrength,
        float sharpenStrength
    );

    void SetGlobalLightingParams(
        bool enabled,
        float globalIntensity,
        float ambientIntensity,
        float skyLightIntensity,
        float sceneExposure,
        const glm::vec3& ambientColor,
        bool advancedLightingEnabled = true,
        float indirectIntensity = 0.35f,
        float contactShadowStrength = 0.35f,
        float contactShadowDistance = 0.75f
    );

    void SetAdvancedLightingEnabled(bool enabled);
    void SetMaterialTextures(
        int normalTextureUnit,
        bool hasNormalMap,
        float normalStrength,
        int emissionTextureUnit,
        bool hasEmissionMap
    );

    void SetEmission(
        float r,
        float g,
        float b
    );

    void SetMaterialProperties(
        float metallic,
        float roughness,
        float specular,
        float emissionStrength
    );

    // Local Light - configura como a entidade recebe luz
    void SetLocalLightEnabled(bool enabled);
    void SetLocalLightInfluence(
        float sunInfluence,
        float artificialInfluence,
        float ambientInfluence
    );
    void SetLocalLightShadow(
        bool receiveShadows,
        float shadowStrength,
        float shadowBias
    );
    void SetLocalLightReflection(
        bool enableReflections,
        float reflectionStrength
    );
    // Textura de reflexo planar (para MaterialType::Mirror)
    void SetMirrorReflection(
        bool  enabled,
        int   reflectionTextureUnit,
        float intensity,
        float blur
    );
    void SetMirrorMode(int mode);
    void SetLocalLightAO(
        bool enableAO,
        float aoStrength
    );

    // UV Scale Mode: controla como a textura se comporta ao escalar a entidade.
    // mode: 0=Stretch, 1=FixedDrawing, 2=TileMesh, 3=WorldSpace, 4=AutoTileFace, 5=NineSlice.
    void SetUVMode(
        int mode,
        float worldScale = 1.0f,
        const glm::vec3& objectScale = glm::vec3(1.0f),
        const glm::vec2& atlasGrid = glm::vec2(1.0f),
        const glm::vec4& bgColor = glm::vec4(1.0f),
        const glm::vec4& extraParams = glm::vec4(0.0f, 1.0f, 0.25f, 0.0f)
    );

    // PointLights — suporta até MAX_POINT_LIGHTS luzes pontuais
    static constexpr int MAX_POINT_LIGHTS = 8;
    void SetPointLightCount(int count);
    void SetPointLight(int index, float px, float py, float pz,
                       float cr, float cg, float cb,
                       float intensity, float radius,
                       int shadowIndex = -1,
                       float sourceRadius = 0.15f,
                       float shadowSoftness = 0.25f,
                       float shadowStrength = 1.0f,
                       bool indirectContribution = true,
                       bool distanceSoftening = true);

    // SpotLights — suporta até MAX_SPOT_LIGHTS spots
    static constexpr int MAX_SPOT_LIGHTS = 8;
    void SetSpotLightCount(int count);
    void SetSpotLight(int index,
                      float px, float py, float pz,
                      float dx, float dy, float dz,
                      float cr, float cg, float cb,
                      float intensity, float range,
                      float innerCos, float outerCos,
                      int shadowIndex = -1,
                      float sourceRadius = 0.15f,
                      float shadowSoftness = 0.25f,
                      float shadowStrength = 1.0f,
                      bool indirectContribution = true,
                      bool distanceSoftening = true);

    static constexpr int MAX_LOCAL_SHADOWS = 4;
    static constexpr int MAX_SHADOW_CASCADES = 4;
    void SetPointShadowMap(int index, int textureUnit);
    void SetPointShadowEnabled(int index, bool enabled);
    void SetPointShadowFarPlane(int index, float farPlane);
    void SetPointShadowAngularTexelSize(float angularTexelSize);

    void SetLightSpaceMatrix(
        const glm::mat4& matrix
    );
    void SetShadowCascadeMatrix(int cascade, const glm::mat4& matrix);
    void SetShadowOpacityArray(int textureUnit);
    void SetShadowCascadeFarPlanes(const float* farPlanes, int count);
    void SetShadowCascadeDepthRanges(const float* depthRanges, int count);
    void SetShadowCascadeWorldTexelSizes(const glm::vec2* sizes, int count);
    void SetDirectionalSourceAngularRadius(float radians);

    void SetUseShadows(
        bool use
    );
    void SetDirectionalShadowsEnabled(bool enabled);
    void SetDirectionalShadowDistanceSoftening(bool enabled);
    void SetDirectionalShadowSubTexelFilter(bool enabled);
    void SetGlobalShadowStrength(float strength);

    bool CreateShadowShader();
    void UseShadowShader();
    void SetShadowModelMatrix(
        const glm::mat4& model
    );
    void SetShadowOpacity(float opacity);
    void SetShadowLightSpaceMatrix(
        const glm::mat4& matrix
    );

    bool CreatePointShadowShader();
    void UsePointShadowShader();
    void SetPointShadowModelMatrix(const glm::mat4& model);
    void SetPointShadowLightSpaceMatrix(const glm::mat4& matrix);
    void SetPointShadowLightPosition(float x, float y, float z);
    void SetPointShadowFarPlane(float farPlane);
    void SetPointShadowOpacity(float opacity);

    unsigned int GetProgramID() const;
    unsigned int GetShadowProgramID() const;

private:
    unsigned int m_ProgramID;
    unsigned int m_ShadowProgramID = 0;

    // Cached uniform locations
    int m_LocModel = -1;
    int m_LocSkinningEnabled = -1;
    int m_LocSkinMatrices = -1;
    int m_LocView = -1;
    int m_LocProjection = -1;
    int m_LocCameraPosition = -1;
    int m_LocColor = -1;
    int m_LocOpacity = -1;
    int m_LocDiffuse = -1;
    int m_LocShadowMap = -1;
    int m_LocShadowOpacityMap = -1;
    int m_LocLightSpaceMatrix = -1;
    int m_LocShadowCascadeMatrices[MAX_SHADOW_CASCADES] = { -1, -1, -1, -1 };
    int m_LocShadowOpacityArray = -1;
    int m_LocShadowCascadeFarPlanes = -1;
    int m_LocShadowCascadeDepthRanges = -1;
    int m_LocShadowCascadeWorldTexelSizes = -1;
    int m_LocDirectionalSourceAngularRadius = -1;
    int m_LocPointShadowAngularTexelSize = -1;
    int m_LocShadowCascadeCount = -1;
    int m_LocUseShadows = -1;
    int m_LocDirectionalShadowsEnabled = -1;
    int m_LocDirectionalShadowDistanceSoftening = -1;
    int m_LocDirectionalShadowSubTexelFilter = -1;
    int m_LocAmbientLight = -1;
    int m_LocLightDirection = -1;
    int m_LocLightColor = -1;
    int m_LocLightIntensity = -1;
    int m_LocShadowIntensity = -1;
    int m_LocEmission = -1;
    int m_LocMetallic = -1;
    int m_LocRoughness = -1;
    int m_LocSpecular = -1;
    int m_LocEmissionStrength = -1;

    // Local Light uniforms
    int m_LocLocalLightEnabled = -1;
    int m_LocSunLightInfluence = -1;
    int m_LocArtificialLightInfluence = -1;
    int m_LocAmbientLightInfluence = -1;
    int m_LocReceiveShadows = -1;
    int m_LocShadowStrength = -1;
    int m_LocShadowBias = -1;
    int m_LocEnableReflections = -1;
    int m_LocReflectionStrength = -1;
    int m_LocEnableAO = -1;
    int m_LocAOStrength = -1;

    int m_LocGlobalEnabled = -1;
    int m_LocGlobalBrightness = -1;
    int m_LocGlobalContrast = -1;
    int m_LocGlobalSaturation = -1;
    int m_LocGlobalExposure = -1;
    int m_LocGlobalTint = -1;
    int m_LocGlobalGamma = -1;
    int m_LocGlobalFogEnabled = -1;
    int m_LocGlobalFogColor = -1;
    int m_LocGlobalFogDensity = -1;
    int m_LocGlobalFogStart = -1;
    int m_LocGlobalFogEnd = -1;
    int m_LocGlobalVignetteStrength = -1;
    int m_LocGlobalVignetteRadius = -1;
    int m_LocViewportSize = -1;
    int m_LocToneMapping = -1;
    int m_LocTemperature = -1;
    int m_LocTint = -1;
    int m_LocFilmGrainEnabled = -1;
    int m_LocFilmGrainStrength = -1;
    int m_LocFilmGrainSize = -1;
    int m_LocFilmGrainSpeed = -1;
    int m_LocChromaticAberrationEnabled = -1;
    int m_LocChromaticAberrationStrength = -1;
    int m_LocBloomStrength = -1;
    int m_LocSharpenStrength = -1;

    int m_LocGlobalLightEnabledNew = -1;
    int m_LocGlobalIntensity = -1;
    int m_LocGlobalShadowStrength = -1;
    int m_LocAmbientIntensity = -1;
    int m_LocSkyLightIntensity = -1;
    int m_LocSceneExposure = -1;
    int m_LocAmbientColorNew = -1;
    int m_LocAdvancedLightingEnabled = -1;
    int m_LocIndirectIntensity = -1;
    int m_LocContactShadowStrength = -1;
    int m_LocContactShadowDistance = -1;
    int m_LocNormalMap = -1;
    int m_LocHasNormalMap = -1;
    int m_LocNormalStrength = -1;
    int m_LocEmissionMap = -1;
    int m_LocHasEmissionMap = -1;
    int m_LocUVMode = -1;
    int m_LocUVWorldScale = -1;
    int m_LocObjectScale = -1;
    int m_LocUVAtlasGrid = -1;
    int m_LocCanvasBgColor = -1;
    int m_LocUVExtraParams = -1;

    // Mirror reflection uniforms
    int m_LocMirrorEnabled       = -1;
    int m_LocMirrorTexture       = -1;
    int m_LocMirrorIntensity     = -1;
    int m_LocMirrorBlur          = -1;
    int m_LocMirrorMode          = -1;

    int m_LocPointShadowMaps[MAX_LOCAL_SHADOWS] = { -1, -1, -1, -1 };
    int m_LocPointShadowOpacityMaps[MAX_LOCAL_SHADOWS] = { -1, -1, -1, -1 };
    int m_LocPointShadowEnabled[MAX_LOCAL_SHADOWS] = { -1, -1, -1, -1 };
    int m_LocPointShadowFarPlanes[MAX_LOCAL_SHADOWS] = { -1, -1, -1, -1 };

    // PointLight uniform locations (array, up to MAX_POINT_LIGHTS)
    int m_LocPointLightCount = -1;
    struct PointLightLocs {
        int pos       = -1;
        int color     = -1;
        int intensity = -1;
        int radius    = -1;
        int sourceRadius = -1;
        int shadowSoftness = -1;
        int shadowStrength = -1;
        int distanceSoftening = -1;
        int indirectContribution = -1;
        int shadowIndex = -1;
    };
    PointLightLocs m_LocPointLights[MAX_POINT_LIGHTS];

    // SpotLight uniform locations
    int m_LocSpotLightCount = -1;
    struct SpotLightLocs {
        int pos       = -1;
        int dir       = -1;
        int color     = -1;
        int intensity = -1;
        int range     = -1;
        int innerCos  = -1;
        int outerCos  = -1;
        int sourceRadius = -1;
        int shadowSoftness = -1;
        int shadowStrength = -1;
        int distanceSoftening = -1;
        int indirectContribution = -1;
        int shadowIndex = -1;
    };
    SpotLightLocs m_LocSpotLights[MAX_SPOT_LIGHTS];

    // Shadow shader uniform locations
    int m_LocShadowModel = -1;
    int m_LocShadowLightSpace = -1;
    int m_LocShadowOpacity = -1;

    unsigned int m_PointShadowProgramID = 0;
    int m_LocPointShadowModel = -1;
    int m_LocPointShadowLightSpace = -1;
    int m_LocPointShadowLightPosition = -1;
    int m_LocPointShadowFarPlane = -1;
    int m_LocPointShadowOpacity = -1;
};
