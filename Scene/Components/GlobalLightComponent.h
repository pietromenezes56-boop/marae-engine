#pragma once

#include "../../Math/Vector3.h"
#include "../../Engine/Renderer/PostProcessing/CommonStructs.h"

struct GlobalLightComponent
{
    enum class ToneMappingOperator
    {
        None = 0,
        Reinhard = 1,
        ACES = 2,
        Filmic = 3
    };

    GlobalLightingSettings Lighting;

    float Gamma = 0.60f;

    float VignetteStrength = 0.0f;
    float VignetteRadius = 0.72f;

    ToneMappingOperator ToneMapping = ToneMappingOperator::ACES;
    float Temperature = 0.0f;
    float Tint = 0.0f;

    bool FilmGrainEnabled = false;
    float FilmGrainStrength = 0.0f;
    float FilmGrainSize = 1.0f;
    float FilmGrainSpeed = 1.0f;

    bool ChromaticAberrationEnabled = false;
    float ChromaticAberrationStrength = 0.0f;

    float BloomStrength = 0.0f;
    float SharpenStrength = 0.0f;

    bool& Enabled = Lighting.Enabled;
    float& Brightness = Lighting.Brightness;
    float& Contrast = Lighting.LegacyContrast;
    float& Saturation = Lighting.LegacySaturation;
    float& Exposure = Lighting.LegacyExposureEV;
    float& ShadowStrength = Lighting.ShadowStrength;
    Vector3& ColorTint = Lighting.ColorTint;

    bool& FogEnabled = Lighting.FogEnabled;
    Vector3& FogColor = Lighting.FogColor;
    float& FogDensity = Lighting.FogDensity;
    float& FogStart = Lighting.FogStart;
    float& FogEnd = Lighting.FogEnd;

    GlobalLightComponent() = default;

    GlobalLightComponent(const GlobalLightComponent& other)
        : Lighting(other.Lighting)
        , Gamma(other.Gamma)
        , VignetteStrength(other.VignetteStrength)
        , VignetteRadius(other.VignetteRadius)
        , ToneMapping(other.ToneMapping)
        , Temperature(other.Temperature)
        , Tint(other.Tint)
        , FilmGrainEnabled(other.FilmGrainEnabled)
        , FilmGrainStrength(other.FilmGrainStrength)
        , FilmGrainSize(other.FilmGrainSize)
        , FilmGrainSpeed(other.FilmGrainSpeed)
        , ChromaticAberrationEnabled(other.ChromaticAberrationEnabled)
        , ChromaticAberrationStrength(other.ChromaticAberrationStrength)
        , BloomStrength(other.BloomStrength)
        , SharpenStrength(other.SharpenStrength)
        , Enabled(Lighting.Enabled)
        , Brightness(Lighting.Brightness)
        , Contrast(Lighting.LegacyContrast)
        , Saturation(Lighting.LegacySaturation)
        , Exposure(Lighting.LegacyExposureEV)
        , ShadowStrength(Lighting.ShadowStrength)
        , ColorTint(Lighting.ColorTint)
        , FogEnabled(Lighting.FogEnabled)
        , FogColor(Lighting.FogColor)
        , FogDensity(Lighting.FogDensity)
        , FogStart(Lighting.FogStart)
        , FogEnd(Lighting.FogEnd)
    {
    }

    GlobalLightComponent& operator=(const GlobalLightComponent& other)
    {
        if (this != &other)
        {
            Lighting = other.Lighting;
            Gamma = other.Gamma;
            VignetteStrength = other.VignetteStrength;
            VignetteRadius = other.VignetteRadius;
            ToneMapping = other.ToneMapping;
            Temperature = other.Temperature;
            Tint = other.Tint;
            FilmGrainEnabled = other.FilmGrainEnabled;
            FilmGrainStrength = other.FilmGrainStrength;
            FilmGrainSize = other.FilmGrainSize;
            FilmGrainSpeed = other.FilmGrainSpeed;
            ChromaticAberrationEnabled = other.ChromaticAberrationEnabled;
            ChromaticAberrationStrength = other.ChromaticAberrationStrength;
            BloomStrength = other.BloomStrength;
            SharpenStrength = other.SharpenStrength;
        }
        return *this;
    }

    GlobalLightComponent(GlobalLightComponent&&) = delete;
    GlobalLightComponent& operator=(GlobalLightComponent&&) = delete;
};
