#include "PostProcessingVolumeBlender.h"
#include <algorithm>
#include <cmath>

namespace
{
    static void MixF(float& a, float b, float t)
    {
        a = a * (1.0f - t) + b * t;
    }

    static void MixV(Vector3& a, const Vector3& b, float t)
    {
        MixF(a.X, b.X, t);
        MixF(a.Y, b.Y, t);
        MixF(a.Z, b.Z, t);
    }

    static void ApplyGlobal(GlobalLightingSettings& dst, const GlobalLightingSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.GlobalIntensity, src.GlobalIntensity, w);
        MixV(dst.AmbientColor, src.AmbientColor, w);
        MixF(dst.AmbientIntensity, src.AmbientIntensity, w);
        MixF(dst.SkyLightIntensity, src.SkyLightIntensity, w);
        MixF(dst.SceneExposure, src.SceneExposure, w);
        dst.Sky = w >= 0.5f ? src.Sky : dst.Sky;
        dst.ScreenSpaceSlotMask = w >= 0.5f ? src.ScreenSpaceSlotMask : dst.ScreenSpaceSlotMask;
        MixF(dst.Brightness, src.Brightness, w);
        MixF(dst.LegacyContrast, src.LegacyContrast, w);
        MixF(dst.LegacySaturation, src.LegacySaturation, w);
        MixF(dst.LegacyExposureEV, src.LegacyExposureEV, w);
        MixV(dst.ColorTint, src.ColorTint, w);
        MixF(dst.ShadowStrength, src.ShadowStrength, w);
        dst.FogEnabled = w >= 0.5f ? src.FogEnabled : dst.FogEnabled;
        MixV(dst.FogColor, src.FogColor, w);
        MixF(dst.FogDensity, src.FogDensity, w);
        MixF(dst.FogStart, src.FogStart, w);
        MixF(dst.FogEnd, src.FogEnd, w);
    }

    static void ApplyColor(ColorGradingSettings& dst, const ColorGradingSettings& src, float w, bool override)
    {
        if (!override) return;
        MixF(dst.Exposure, src.Exposure, w);
        MixF(dst.Contrast, src.Contrast, w);
        MixF(dst.Saturation, src.Saturation, w);
        MixF(dst.Temperature, src.Temperature, w);
        MixF(dst.Tint, src.Tint, w);
        MixF(dst.Brightness, src.Brightness, w);
        MixF(dst.Gamma, src.Gamma, w);
        MixV(dst.Gain, src.Gain, w);
        MixV(dst.Lift, src.Lift, w);
        MixV(dst.PresetGamma, src.PresetGamma, w);
        MixV(dst.Shadows, src.Shadows, w);
        MixF(dst.ShadowsWeight, src.ShadowsWeight, w);
        MixV(dst.Midtones, src.Midtones, w);
        MixF(dst.MidtonesWeight, src.MidtonesWeight, w);
        MixV(dst.Highlights, src.Highlights, w);
        MixF(dst.HighlightsWeight, src.HighlightsWeight, w);
        MixV(dst.BalanceShadows, src.BalanceShadows, w);
        MixV(dst.BalanceMidtones, src.BalanceMidtones, w);
        MixV(dst.BalanceHighlights, src.BalanceHighlights, w);
    }

    static void ApplyBloom(BloomSettings& dst, const BloomSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Threshold, src.Threshold, w);
        MixF(dst.Knee, src.Knee, w);
        MixF(dst.Intensity, src.Intensity, w);
        MixF(dst.Radius, src.Radius, w);
        MixF(dst.DirtIntensity, src.DirtIntensity, w);
        dst.MipCount = w >= 0.5f ? src.MipCount : dst.MipCount;
    }

    static void ApplyVignette(VignetteSettings& dst, const VignetteSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Intensity, src.Intensity, w);
        MixF(dst.Radius, src.Radius, w);
        MixF(dst.Smoothness, src.Smoothness, w);
        MixF(dst.Feather, src.Feather, w);
        MixV(dst.Color, src.Color, w);
    }

    static void ApplyTone(ToneMappingSettings& dst, const ToneMappingSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.ReinhardWhitePoint, src.ReinhardWhitePoint, w);
        MixF(dst.Intensity, src.Intensity, w);
    }

    static void ApplyEnums(ToneMappingSettings& dst, const ToneMappingSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Op = w >= 0.5f ? src.Op : dst.Op;
    }

    static void ApplyLut(LUTSettings& dst, const LUTSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Intensity, src.Intensity, w);
        if (w >= 0.5f) dst.AssetPath = src.AssetPath;
    }

    static void ApplyChromatic(ChromaticAberrationSettings& dst, const ChromaticAberrationSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Strength, src.Strength, w);
        MixF(dst.EdgeBias, src.EdgeBias, w);
    }

    static void ApplySharpen(SharpenSettings& dst, const SharpenSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Strength, src.Strength, w);
        MixF(dst.Clamp, src.Clamp, w);
    }

    static void ApplyLensD(LensDistortionSettings& dst, const LensDistortionSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.K1, src.K1, w);
        MixF(dst.K2, src.K2, w);
        MixF(dst.Cubic, src.Cubic, w);
    }

    static void ApplyGrain(FilmGrainSettings& dst, const FilmGrainSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Intensity, src.Intensity, w);
        MixF(dst.GrainSize, src.GrainSize, w);
        MixF(dst.Speed, src.Speed, w);
        MixF(dst.ShadowWeight, src.ShadowWeight, w);
        MixF(dst.MidWeight, src.MidWeight, w);
        MixF(dst.HighWeight, src.HighWeight, w);
    }

    static void ApplyDoF(DoFSettings& dst, const DoFSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.FocalDistance, src.FocalDistance, w);
        MixF(dst.FocalRange, src.FocalRange, w);
        MixF(dst.Aperture, src.Aperture, w);
        MixF(dst.MaxBlur, src.MaxBlur, w);
    }

    static void ApplyMoBlur(MotionBlurSettings& dst, const MotionBlurSettings& src, float w, bool override)
    {
        if (!override) return;
        dst.Enabled = w >= 0.5f ? src.Enabled : dst.Enabled;
        MixF(dst.Intensity, src.Intensity, w);
        dst.Samples = w >= 0.5f ? src.Samples : dst.Samples;
    }
}

float PostProcessingVolumeBlender::Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

void PostProcessingVolumeBlender::SphereContains(const Vector3& center, float radius, const Vector3& p, float& signedDist)
{
    float dx = p.X - center.X;
    float dy = p.Y - center.Y;
    float dz = p.Z - center.Z;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    signedDist = len - radius;
}

void PostProcessingVolumeBlender::BoxContains(const Vector3& min, const Vector3& max, const Vector3& p, float& signedDist)
{
    float dxMin = min.X - p.X;
    float dxMax = p.X - max.X;
    float dx = std::max(dxMin, dxMax);

    float dyMin = min.Y - p.Y;
    float dyMax = p.Y - max.Y;
    float dy = std::max(dyMin, dyMax);

    float dzMin = min.Z - p.Z;
    float dzMax = p.Z - max.Z;
    float dz = std::max(dzMin, dzMax);

    signedDist = std::max(std::max(dx, dy), dz);
}

BlendedSettings PostProcessingVolumeBlender::Blend(const Scene& scene, const Vector3& cameraWorldPos)
{
    BlendedSettings out{};
    bool foundGlobal = false;

    const auto& entities = scene.GetEntities();

    for (const auto& entity : entities)
    {
        if (!entity.HasPostProcessingVolume()) continue;
        const auto& v = entity.GetPostProcessingVolume();
        bool isGlobalVol = entity.IsGlobalLight() || v.IsGlobal;
        if (isGlobalVol)
        {
            ApplyGlobal(out.Global, v.Global, 1.0f, !!(v.OverrideMask & OVERRIDE_GLOBAL_LIGHT));
            ApplyColor(out.Color, v.Color, 1.0f, !!(v.OverrideMask & OVERRIDE_COLOR));
            ApplyBloom(out.Bloom, v.Bloom, 1.0f, !!(v.OverrideMask & OVERRIDE_BLOOM));
            ApplyVignette(out.Vignette, v.Vignette, 1.0f, !!(v.OverrideMask & OVERRIDE_VIGNETTE));
            ApplyTone(out.Tone, v.Tone, 1.0f, !!(v.OverrideMask & OVERRIDE_TM));
            ApplyEnums(out.Tone, v.Tone, 1.0f, !!(v.OverrideMask & OVERRIDE_TM));
            ApplyLut(out.Lut, v.Lut, 1.0f, !!(v.OverrideMask & OVERRIDE_LUT));
            ApplyChromatic(out.Chromatic, v.Chromatic, 1.0f, !!(v.OverrideMask & OVERRIDE_CHROM));
            ApplySharpen(out.Sharpen, v.Sharpen, 1.0f, !!(v.OverrideMask & OVERRIDE_SHARP));
            ApplyLensD(out.LensDistortion, v.LensDistortion, 1.0f, !!(v.OverrideMask & OVERRIDE_LENSD));
            ApplyGrain(out.FilmGrain, v.FilmGrain, 1.0f, !!(v.OverrideMask & OVERRIDE_GRAIN));
            ApplyDoF(out.DoF, v.DoF, 1.0f, !!(v.OverrideMask & OVERRIDE_DOF));
            ApplyMoBlur(out.MotionBlur, v.MotionBlur, 1.0f, !!(v.OverrideMask & OVERRIDE_MB));
            foundGlobal = true;
            break;
        }
    }

    for (const auto& entity : entities)
    {
        if (!entity.HasPostProcessingVolume()) continue;
        const auto& v = entity.GetPostProcessingVolume();
        bool isGlobalVol = entity.IsGlobalLight() || v.IsGlobal;
        if (isGlobalVol) continue;

        const Transform& t = entity.GetTransform();
        Vector3 center = t.Position;
        float signedDist = 0.0f;

        if (v.Shape == PostProcessingVolumeComponent::SPHERE)
        {
            SphereContains(center, v.Radius, cameraWorldPos, signedDist);
        }
        else
        {
            Vector3 half(std::fabs(v.Size.X * 0.5f), std::fabs(v.Size.Y * 0.5f), std::fabs(v.Size.Z * 0.5f));
            Vector3 vmin(center.X - half.X, center.Y - half.Y, center.Z - half.Z);
            Vector3 vmax(center.X + half.X, center.Y + half.Y, center.Z + half.Z);
            BoxContains(vmin, vmax, cameraWorldPos, signedDist);
        }

        if (signedDist > v.Falloff) continue;

        float factor = 1.0f;
        if (signedDist > 0.0f)
            factor = 1.0f - Smoothstep(0.0f, std::max(0.001f, v.Falloff), signedDist);

        float priorityW = (1.0f + float(v.Priority)) * 0.125f;
        float w = v.BlendWeight * priorityW * factor;

        if (w <= 0.00001f) continue;

        ApplyGlobal(out.Global, v.Global, w, !!(v.OverrideMask & OVERRIDE_GLOBAL_LIGHT));
        ApplyColor(out.Color, v.Color, w, !!(v.OverrideMask & OVERRIDE_COLOR));
        ApplyBloom(out.Bloom, v.Bloom, w, !!(v.OverrideMask & OVERRIDE_BLOOM));
        ApplyVignette(out.Vignette, v.Vignette, w, !!(v.OverrideMask & OVERRIDE_VIGNETTE));
        ApplyTone(out.Tone, v.Tone, w, !!(v.OverrideMask & OVERRIDE_TM));
        ApplyLut(out.Lut, v.Lut, w, !!(v.OverrideMask & OVERRIDE_LUT));
        ApplyChromatic(out.Chromatic, v.Chromatic, w, !!(v.OverrideMask & OVERRIDE_CHROM));
        ApplySharpen(out.Sharpen, v.Sharpen, w, !!(v.OverrideMask & OVERRIDE_SHARP));
        ApplyLensD(out.LensDistortion, v.LensDistortion, w, !!(v.OverrideMask & OVERRIDE_LENSD));
        ApplyGrain(out.FilmGrain, v.FilmGrain, w, !!(v.OverrideMask & OVERRIDE_GRAIN));
        ApplyDoF(out.DoF, v.DoF, w, !!(v.OverrideMask & OVERRIDE_DOF));
        ApplyMoBlur(out.MotionBlur, v.MotionBlur, w, !!(v.OverrideMask & OVERRIDE_MB));
        ApplyEnums(out.Tone, v.Tone, w, !!(v.OverrideMask & OVERRIDE_TM));
    }

    return out;
}
