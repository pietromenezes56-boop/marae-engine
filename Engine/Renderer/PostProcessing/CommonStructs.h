#pragma once

#include "../../../Math/Vector3.h"
#include <cstdint>
#include <string>
#include "../../../Hub/ProjectManager.h"

enum class PassId : uint8_t { SSAO=0, BLOOM=1, DOF=2, MOTIONBLUR=3, COLORGRADING=4, LUT=5, TONEMAP=6, VIGNETTE=7, CHROMATIC=8, FILMGRAIN=9, SHARPEN=10, LENSDISTORT=11, COUNT=12 };

enum PPOverrideBits : uint32_t {
    OVERRIDE_NONE = 0, OVERRIDE_GLOBAL_LIGHT = 1<<0, OVERRIDE_BLOOM = 1<<1,
    OVERRIDE_COLOR = 1<<2, OVERRIDE_LUT = 1<<3, OVERRIDE_VIGNETTE = 1<<4,
    OVERRIDE_TM = 1<<5, OVERRIDE_DOF = 1<<6, OVERRIDE_MB = 1<<7,
    OVERRIDE_CHROM = 1<<8, OVERRIDE_GRAIN = 1<<9, OVERRIDE_SHARP = 1<<10,
    OVERRIDE_LENSD = 1<<11, OVERRIDE_ALL = 0xFFFFFFFFu
};

struct BloomSettings {
    bool Enabled = false; bool Override = false;
    float Threshold = 0.9f; float Knee = 0.5f;
    float Intensity = 0.0f; float Radius = 0.7f;
    float DirtIntensity = 0.0f; uint8_t MipCount = 5;
};

struct VignetteSettings {
    bool Enabled = false; bool Override = false;
    float Intensity = 0.0f; float Radius = 0.72f;
    float Smoothness = 0.4f; float Feather = 0.2f;
    Vector3 Color = Vector3(0,0,0);
};

struct ChromaticAberrationSettings {
    bool Enabled = false; bool Override = false;
    float Strength = 0.0f; float EdgeBias = 1.0f;
};

struct SharpenSettings {
    bool Enabled = false; bool Override = false;
    float Strength = 0.0f; float Clamp = 0.035f;
};

struct LensDistortionSettings {
    bool Enabled = false; bool Override = false;
    float K1 = 0.0f; float K2 = 0.0f; float Cubic = 0.0f;
};

struct ToneMappingSettings {
    bool Enabled = true; bool Override = false;
    enum Operator { NONE=0, REINHARD=1, ACES=2, FILMIC=3, AGX=4 } Op = ACES;
    float ReinhardWhitePoint = 1.0f; float Intensity = 1.0f;
};

struct FilmGrainSettings {
    bool Enabled = false; bool Override = false;
    float Intensity = 0.0f; float GrainSize = 1.0f; float Speed = 1.0f;
    float ShadowWeight = 0.6f; float MidWeight = 1.0f; float HighWeight = 0.8f;
};

struct DoFSettings {
    bool Enabled = false; bool Override = false;
    float FocalDistance = 5.0f; float FocalRange = 2.0f;
    float Aperture = 2.8f; float MaxBlur = 0.012f;
};

struct MotionBlurSettings {
    bool Enabled = false; bool Override = false;
    float Intensity = 0.0f; int Samples = 4;
};

struct LUTSettings {
    bool Enabled = false; bool Override = false;
    float Intensity = 1.0f; std::string AssetPath;
};

struct ColorGradingSettings {
    bool Override = false;
    float Exposure = 0.0f; float Contrast = 1.0f; float Saturation = 1.0f;
    float Temperature = 0.0f; float Tint = 0.0f; float Brightness = 1.0f; float Gamma = 1.0f;
    Vector3 Gain  = Vector3(1,1,1);
    Vector3 Lift  = Vector3(0,0,0);
    Vector3 PresetGamma = Vector3(1,1,1);
    Vector3 Shadows = Vector3(1,1,1); float ShadowsWeight = 0.08f;
    Vector3 Midtones = Vector3(1,1,1); float MidtonesWeight = 0.54f;
    Vector3 Highlights = Vector3(1,1,1); float HighlightsWeight = 0.92f;
    Vector3 BalanceShadows  = Vector3(0,0,0);
    Vector3 BalanceMidtones = Vector3(0,0,0);
    Vector3 BalanceHighlights = Vector3(0,0,0);
};

struct GlobalLightingSettings {
    bool Override = false; bool Enabled = true;
    float GlobalIntensity = 1.0f;
    Vector3 AmbientColor = Vector3(1,1,1); float AmbientIntensity = 0.4f;
    float SkyLightIntensity = 1.0f; float SceneExposure = 0.0f;
    enum SkyMode { PROCEDURAL=0, HDRI_PLACEHOLDER=1 } Sky = PROCEDURAL;
    uint8_t ScreenSpaceSlotMask = 0;
    float Brightness = 1.0f; float LegacyContrast = 1.0f; float LegacySaturation = 1.0f;
    float LegacyExposureEV = 0.0f; Vector3 ColorTint = Vector3(1,1,1);
    float ShadowStrength = 1.0f;
    // Aparência do céu procedural e das nuvens exibidas pelo SkyRenderer.
    Vector3 SkyZenithColor = Vector3(0.18f, 0.44f, 0.85f);
    Vector3 SkyHorizonColor = Vector3(0.72f, 0.84f, 0.96f);
    Vector3 SkyGroundColor = Vector3(0.38f, 0.42f, 0.46f);
    bool CloudsEnabled = true;
    Vector3 CloudColor = Vector3(0.58f, 0.65f, 0.76f);
    float CloudAmount = 0.50f;
    int CloudOctaves = 5;
    bool CloudMovementEnabled = true;
    float CloudSpeed = 1.0f;
    // Optional screen-space effects. These stay off in existing projects and
    // can be enabled individually from the Global Light inspector.
    bool ScreenSpaceAOEnabled = false;
    float ScreenSpaceAOIntensity = 0.35f;
    float ScreenSpaceAORadius = 1.25f;
    bool ScreenSpaceReflectionsEnabled = false;
    float ScreenSpaceReflectionsStrength = 0.25f;
    float ScreenSpaceReflectionsDistance = 24.0f;
    bool TemporalAAEnabled = false;
    float TemporalAAHistoryWeight = 0.12f;
    // Optional ray-marched cloud layer in world space (all distances in meters).
    bool VolumetricCloudsEnabled = false;
    float VolumetricCloudBaseHeight = 900.0f;
    float VolumetricCloudThickness = 700.0f;
    float VolumetricCloudDensity = 0.65f;
    float VolumetricCloudScale = 1.0f;
    int VolumetricCloudSteps = 8;
    bool FogEnabled = false; Vector3 FogColor = Vector3(0.55f, 0.62f, 0.70f);
    float FogDensity = 0.015f; float FogStart = 10.0f; float FogEnd = 250.0f;
    // 0 = linear, 1 = exponential, 2 = exponential squared.
    int FogMode = 1;

    // Direcao do sol para calculo de projecao em tela
    Vector3 SunDirection = Vector3(-0.4f, -1.0f, -0.4f);

    // Raios Solares (Crepuscular Rays / God Rays)
    bool GodRaysEnabled = false;
    float GodRaysIntensity = 0.5f;
    float GodRaysDensity = 0.8f;
    float GodRaysDecay = 0.95f;
    float GodRaysWeight = 0.35f;
    // Parametros avancados para qualidade AAA
    Vector3 GodRaysTint = Vector3(1.0f, 1.0f, 1.0f); // Tint de cor dos raios
    float GodRaysMieG = 0.72f;              // Forward scattering de Mie (0=isotrop, 0.98=forte)
    float GodRaysHaloRadius = 0.12f;         // Raio do halo solar
    float GodRaysHaloIntensity = 0.6f;       // Intensidade da corona/halo
    float GodRaysSunAltitude = 0.5f;         // Altitude do sol [0=horizonte, 1=zenite]

    // Raios de Brilho / Glare (Luzes Artificiais e Neon)
    bool GlareRaysEnabled = false;
    float GlareRaysIntensity = 0.4f;
    float GlareRaysLength = 0.6f;
    float GlareRaysThreshold = 1.0f;
};

struct BlendedSettings {
    GlobalLightingSettings    Global;
    ColorGradingSettings      Color;
    ToneMappingSettings       Tone;
    LUTSettings               Lut;
    BloomSettings             Bloom;
    VignetteSettings          Vignette;
    ChromaticAberrationSettings Chromatic;
    SharpenSettings           Sharpen;
    LensDistortionSettings    LensDistortion;
    FilmGrainSettings         FilmGrain;
    DoFSettings               DoF;
    MotionBlurSettings        MotionBlur;
};
