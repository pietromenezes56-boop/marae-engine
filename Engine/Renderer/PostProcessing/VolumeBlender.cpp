#include "VolumeBlender.h"
#include "../../../Scene/Scene/Scene.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../../Scene/Transform/Transform.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    inline float SDF_Box(const Vector3& p, const Vector3& center, const Vector3& halfSize)
    {
        float dx = std::max({ center.X - halfSize.X - p.X, 0.0f, p.X - (center.X + halfSize.X) });
        float dy = std::max({ center.Y - halfSize.Y - p.Y, 0.0f, p.Y - (center.Y + halfSize.Y) });
        float dz = std::max({ center.Z - halfSize.Z - p.Z, 0.0f, p.Z - (center.Z + halfSize.Z) });
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }

    inline float SDF_Sphere(const Vector3& p, const Vector3& center, float radius)
    {
        float dx = p.X - center.X;
        float dy = p.Y - center.Y;
        float dz = p.Z - center.Z;
        return std::sqrt(dx*dx + dy*dy + dz*dz) - radius;
    }

    float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t)
    {
        return Vector3(
            a.X + (b.X - a.X) * t,
            a.Y + (b.Y - a.Y) * t,
            a.Z + (b.Z - a.Z) * t
        );
    }

    template <typename T>
    void BlendField(T& dst, const T& src, float weight, bool overrideEnabled)
    {
        if (weight <= 0.0f) return;
        if (!overrideEnabled) return;
        dst = Lerp(dst, src, std::clamp(weight, 0.0f, 1.0f));
    }

    void BlendGlobalLighting(GlobalLightingSettings& base, const GlobalLightingSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_GLOBAL_LIGHT)) return;
        BlendField(base.Enabled,           v.Enabled,           w, v.Override);
        BlendField(base.GlobalIntensity,   v.GlobalIntensity,   w, v.Override);
        BlendField(base.AmbientColor,      v.AmbientColor,      w, v.Override);
        BlendField(base.AmbientIntensity,  v.AmbientIntensity,  w, v.Override);
        BlendField(base.SkyLightIntensity, v.SkyLightIntensity, w, v.Override);
        BlendField(base.SceneExposure,     v.SceneExposure,     w, v.Override);
        BlendField(base.Brightness,        v.Brightness,        w, v.Override);
        BlendField(base.LegacyContrast,    v.LegacyContrast,    w, v.Override);
        BlendField(base.LegacySaturation,  v.LegacySaturation,  w, v.Override);
        BlendField(base.LegacyExposureEV,  v.LegacyExposureEV,  w, v.Override);
        BlendField(base.ColorTint,         v.ColorTint,         w, v.Override);
        BlendField(base.ShadowStrength,    v.ShadowStrength,    w, v.Override);
        BlendField(base.FogEnabled,        v.FogEnabled,        w, v.Override);
        BlendField(base.FogColor,          v.FogColor,          w, v.Override);
        BlendField(base.FogDensity,        v.FogDensity,        w, v.Override);
        BlendField(base.FogStart,          v.FogStart,          w, v.Override);
        BlendField(base.FogEnd,            v.FogEnd,            w, v.Override);
        BlendField(base.GodRaysEnabled,       v.GodRaysEnabled,       w, v.Override);
        BlendField(base.GodRaysIntensity,     v.GodRaysIntensity,     w, v.Override);
        BlendField(base.GodRaysDensity,       v.GodRaysDensity,       w, v.Override);
        BlendField(base.GodRaysDecay,         v.GodRaysDecay,         w, v.Override);
        BlendField(base.GodRaysWeight,        v.GodRaysWeight,        w, v.Override);
        BlendField(base.GodRaysTint,          v.GodRaysTint,          w, v.Override);
        BlendField(base.GodRaysMieG,          v.GodRaysMieG,          w, v.Override);
        BlendField(base.GodRaysHaloRadius,    v.GodRaysHaloRadius,    w, v.Override);
        BlendField(base.GodRaysHaloIntensity, v.GodRaysHaloIntensity, w, v.Override);
        BlendField(base.GlareRaysEnabled,  v.GlareRaysEnabled,  w, v.Override);
        BlendField(base.GlareRaysIntensity,v.GlareRaysIntensity,w, v.Override);
        BlendField(base.GlareRaysLength,   v.GlareRaysLength,   w, v.Override);
        BlendField(base.GlareRaysThreshold,v.GlareRaysThreshold,w, v.Override);
    }

    void BlendColorGrading(ColorGradingSettings& base, const ColorGradingSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_COLOR)) return;
        BlendField(base.Exposure,        v.Exposure,        w, v.Override);
        BlendField(base.Contrast,        v.Contrast,        w, v.Override);
        BlendField(base.Saturation,      v.Saturation,      w, v.Override);
        BlendField(base.Temperature,     v.Temperature,     w, v.Override);
        BlendField(base.Tint,            v.Tint,            w, v.Override);
        BlendField(base.Brightness,      v.Brightness,      w, v.Override);
        BlendField(base.Gamma,           v.Gamma,           w, v.Override);
        BlendField(base.Gain,            v.Gain,            w, v.Override);
        BlendField(base.Lift,            v.Lift,            w, v.Override);
        BlendField(base.PresetGamma,     v.PresetGamma,     w, v.Override);
        BlendField(base.Shadows,         v.Shadows,         w, v.Override);
        BlendField(base.ShadowsWeight,   v.ShadowsWeight,   w, v.Override);
        BlendField(base.Midtones,        v.Midtones,        w, v.Override);
        BlendField(base.MidtonesWeight,  v.MidtonesWeight,  w, v.Override);
        BlendField(base.Highlights,      v.Highlights,      w, v.Override);
        BlendField(base.HighlightsWeight,v.HighlightsWeight,w, v.Override);
        BlendField(base.BalanceShadows,  v.BalanceShadows,  w, v.Override);
        BlendField(base.BalanceMidtones, v.BalanceMidtones, w, v.Override);
        BlendField(base.BalanceHighlights, v.BalanceHighlights, w, v.Override);
    }

    void BlendToneMapping(ToneMappingSettings& base, const ToneMappingSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_TM)) return;
        if (v.Override && w > 0.0f)
        {
            float f = std::clamp(w, 0.0f, 1.0f);
            BlendField(base.Enabled,           v.Enabled,           w, v.Override);
            BlendField(base.ReinhardWhitePoint,v.ReinhardWhitePoint,w, v.Override);
            BlendField(base.Intensity,         v.Intensity,         w, v.Override);
            if (f >= 0.5f) base.Op = v.Op;
        }
    }

    void BlendLUT(LUTSettings& base, const LUTSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_LUT)) return;
        if (v.Override && w > 0.0f)
        {
            float f = std::clamp(w, 0.0f, 1.0f);
            BlendField(base.Enabled, v.Enabled, w, v.Override);
            BlendField(base.Intensity, v.Intensity, w, v.Override);
            if (f >= 0.5f && !v.AssetPath.empty()) base.AssetPath = v.AssetPath;
        }
    }

    template <typename T>
    void BlendPPField(T& dst, const T& src, float w, bool overrideEnabled)
    {
        if (w <= 0.0f || !overrideEnabled) return;
        BlendField(dst, src, w, true);
    }

    void BlendBloom(BloomSettings& base, const BloomSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_BLOOM)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,       v.Enabled,       w, true);
        BlendPPField(base.Threshold,     v.Threshold,     w, true);
        BlendPPField(base.Knee,          v.Knee,          w, true);
        BlendPPField(base.Intensity,     v.Intensity,     w, true);
        BlendPPField(base.Radius,        v.Radius,        w, true);
        BlendPPField(base.DirtIntensity, v.DirtIntensity, w, true);
        if (w >= 0.5f) base.MipCount = v.MipCount;
    }

    void BlendVignette(VignetteSettings& base, const VignetteSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_VIGNETTE)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,    v.Enabled,    w, true);
        BlendPPField(base.Intensity,  v.Intensity,  w, true);
        BlendPPField(base.Radius,     v.Radius,     w, true);
        BlendPPField(base.Smoothness, v.Smoothness, w, true);
        BlendPPField(base.Feather,    v.Feather,    w, true);
        BlendPPField(base.Color,      v.Color,      w, true);
    }

    void BlendChromatic(ChromaticAberrationSettings& base, const ChromaticAberrationSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_CHROM)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,   v.Enabled,   w, true);
        BlendPPField(base.Strength,  v.Strength,  w, true);
        BlendPPField(base.EdgeBias,  v.EdgeBias,  w, true);
    }

    void BlendSharpen(SharpenSettings& base, const SharpenSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_SHARP)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,  v.Enabled,  w, true);
        BlendPPField(base.Strength, v.Strength, w, true);
        BlendPPField(base.Clamp,    v.Clamp,    w, true);
    }

    void BlendLensDistortion(LensDistortionSettings& base, const LensDistortionSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_LENSD)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled, v.Enabled, w, true);
        BlendPPField(base.K1,      v.K1,      w, true);
        BlendPPField(base.K2,      v.K2,      w, true);
        BlendPPField(base.Cubic,   v.Cubic,   w, true);
    }

    void BlendFilmGrain(FilmGrainSettings& base, const FilmGrainSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_GRAIN)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,     v.Enabled,     w, true);
        BlendPPField(base.Intensity,   v.Intensity,   w, true);
        BlendPPField(base.GrainSize,   v.GrainSize,   w, true);
        BlendPPField(base.Speed,       v.Speed,       w, true);
        BlendPPField(base.ShadowWeight,v.ShadowWeight,w, true);
        BlendPPField(base.MidWeight,   v.MidWeight,   w, true);
        BlendPPField(base.HighWeight,  v.HighWeight,  w, true);
    }

    void BlendDoF(DoFSettings& base, const DoFSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_DOF)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,      v.Enabled,      w, true);
        BlendPPField(base.FocalDistance,v.FocalDistance,w, true);
        BlendPPField(base.FocalRange,   v.FocalRange,   w, true);
        BlendPPField(base.Aperture,     v.Aperture,     w, true);
        BlendPPField(base.MaxBlur,      v.MaxBlur,      w, true);
    }

    void BlendMotionBlur(MotionBlurSettings& base, const MotionBlurSettings& v, float w, uint32_t mask)
    {
        if (!(mask & OVERRIDE_MB)) return;
        if (!v.Override || w <= 0.0f) return;
        BlendPPField(base.Enabled,   v.Enabled,   w, true);
        BlendPPField(base.Intensity, v.Intensity, w, true);
        if (w >= 0.5f) base.Samples = v.Samples;
    }
}

float VolumeBlender::ComputeVolumeWeight(
    const PostProcessingVolumeComponent& vol,
    const Vector3& volumeWorldPos,
    const Vector3& cameraWorldPos)
{
    if (vol.IsGlobal)
    {
        return std::clamp(vol.BlendWeight, 0.0f, 1.0f);
    }

    float sdf = 0.0f;
    if (vol.Shape == PostProcessingVolumeComponent::SPHERE)
    {
        sdf = SDF_Sphere(cameraWorldPos, volumeWorldPos, std::max(0.001f, vol.Radius));
    }
    else
    {
        Vector3 half(
            std::max(0.001f, vol.Size.X * 0.5f),
            std::max(0.001f, vol.Size.Y * 0.5f),
            std::max(0.001f, vol.Size.Z * 0.5f));
        sdf = SDF_Box(cameraWorldPos, volumeWorldPos, half);
    }

    float falloff = std::max(0.001f, vol.Falloff);
    float t = std::clamp(1.0f - (sdf / falloff), 0.0f, 1.0f);
    // smoothstep para transição suave
    t = t * t * (3.0f - 2.0f * t);
    return t * std::clamp(vol.BlendWeight, 0.0f, 1.0f);
}

BlendedSettings VolumeBlender::Evaluate(
    const Scene& scene,
    const Vector3& cameraWorldPosition)
{
    BlendedSettings result;

    const auto& entities = scene.GetEntities();

    // Primeiro pass: encontrar o volume global (apenas 1) e coletar volumes locais
    std::vector<const Entity*> locals;
    const Entity* globalEntity = nullptr;

    for (const Entity& e : entities)
    {
        if (!e.Active) continue;
        if (!e.HasPostProcessingVolume()) continue;
        const auto& pp = e.GetPostProcessingVolume();
        if (pp.IsGlobal)
        {
            if (!globalEntity) globalEntity = &e;
        }
        else
        {
            locals.push_back(&e);
        }
    }

    // Base = valores padrão; se existir global, usamos seus valores como base
    if (globalEntity)
    {
        const auto& v = globalEntity->GetPostProcessingVolume();
        result.Global   = v.Global;
        result.Color    = v.Color;
        result.Tone     = v.Tone;
        result.Lut      = v.Lut;
        result.Bloom    = v.Bloom;
        result.Vignette = v.Vignette;
        result.Chromatic = v.Chromatic;
        result.Sharpen  = v.Sharpen;
        result.LensDistortion = v.LensDistortion;
        result.FilmGrain = v.FilmGrain;
        result.DoF      = v.DoF;
        result.MotionBlur = v.MotionBlur;

        // Garante que o global tem override para tudo como fallback
        result.Global.Override = true;
        result.Color.Override = true;
        result.Tone.Override = true;
        result.Lut.Override = true;
        result.Bloom.Override = true;
        result.Vignette.Override = true;
        result.Chromatic.Override = true;
        result.Sharpen.Override = true;
        result.LensDistortion.Override = true;
        result.FilmGrain.Override = true;
        result.DoF.Override = true;
        result.MotionBlur.Override = true;
    }

    // Ordenar volumes locais por: Prioridade DESC, depois peso calculado
    std::vector<std::pair<float, const Entity*>> sortedLocals;
    sortedLocals.reserve(locals.size());
    for (const Entity* e : locals)
    {
        const auto& pp = e->GetPostProcessingVolume();
        const Transform& tr = const_cast<Entity*>(e)->GetTransform();
        float w = ComputeVolumeWeight(pp, tr.Position, cameraWorldPosition);
        if (w > 0.0001f)
            sortedLocals.emplace_back(w, e);
    }
    std::sort(sortedLocals.begin(), sortedLocals.end(),
        [](const auto& a, const auto& b)
        {
            const auto& pa = a.second->GetPostProcessingVolume();
            const auto& pb = b.second->GetPostProcessingVolume();
            if (pa.Priority != pb.Priority) return pa.Priority > pb.Priority;
            return a.first > b.first;
        });

    // Aplicar blends (volume mais prioritário por último para ter maior influência)
    for (const auto& [weight, ent] : sortedLocals)
    {
        const auto& v = ent->GetPostProcessingVolume();
        const uint32_t mask = v.OverrideMask;
        BlendGlobalLighting  (result.Global,   v.Global,   weight, mask);
        BlendColorGrading    (result.Color,    v.Color,    weight, mask);
        BlendToneMapping     (result.Tone,     v.Tone,     weight, mask);
        BlendLUT             (result.Lut,      v.Lut,      weight, mask);
        BlendBloom           (result.Bloom,    v.Bloom,    weight, mask);
        BlendVignette        (result.Vignette, v.Vignette, weight, mask);
        BlendChromatic       (result.Chromatic,v.Chromatic,weight, mask);
        BlendSharpen         (result.Sharpen,  v.Sharpen,  weight, mask);
        BlendLensDistortion  (result.LensDistortion, v.LensDistortion, weight, mask);
        BlendFilmGrain       (result.FilmGrain,v.FilmGrain,weight, mask);
        BlendDoF             (result.DoF,      v.DoF,      weight, mask);
        BlendMotionBlur      (result.MotionBlur,v.MotionBlur,weight, mask);
    }

    return result;
}
