#include "ShadowSystem.h"

#include <algorithm>
#include <cmath>

bool ShadowSystem::ShouldRender(
    const LightingSettings& settings,
    bool rendererAllowsShadows)
{
    // Sombras do sol são uma funcionalidade básica da cena. Assim como as
    // sombras de luz artificial (Point/Spot), elas não devem desaparecer só
    // porque o pacote opcional de iluminação avançada (Enabled) está desligado.
    // DynamicShadows continua sendo o controle de custo/qualidade global.
    return settings.DynamicShadows && rendererAllowsShadows;
}

unsigned int ShadowSystem::ResolveDirectionalResolution(
    const LightingSettings& settings,
    unsigned int rendererResolution)
{
    const unsigned int qualityCap[] = { 1024u, 2048u, 3072u, 4096u };
    const int quality = std::clamp(static_cast<int>(settings.Quality), 0, 3);
    const unsigned int requested = static_cast<unsigned int>(
        std::clamp(settings.ShadowMapResolution, 512, 4096));
    return std::min(requested, qualityCap[quality]);
}

unsigned int ShadowSystem::ResolvePointResolution(
    const LightingSettings& settings,
    unsigned int rendererResolution)
{
    const unsigned int qualityCap[] = { 256u, 512u, 1024u, 2048u };
    const int quality = std::clamp(static_cast<int>(settings.Quality), 0, 3);
    const unsigned int requested = static_cast<unsigned int>(
        std::clamp(settings.PointShadowMapResolution, 256, 2048));
    return std::min(requested, qualityCap[quality]);
}

float ShadowSystem::ResolveShadowDistance(const LightingSettings& settings, float fallback)
{
    (void)fallback;
    return std::clamp(settings.ShadowDistance, 10.0f, 10000.0f);
}

int ShadowSystem::ResolveCascadeCount(float shadowDistance, int requestedCascadeCount)
{
    constexpr float nearCascadeLength = 10.0f;
    shadowDistance = std::clamp(shadowDistance, 10.0f, 10000.0f);
    requestedCascadeCount = std::clamp(requestedCascadeCount, 1, 4);

    // Avoid squeezing several cascades into a few centimetres at short ranges.
    // Add a cascade only when the covered distance can at least double from
    // the stable near slice, which preserves useful texel density per slice.
    const float ratio = std::max(1.0f, shadowDistance / nearCascadeLength);
    const int distanceCascadeCount = 1 + static_cast<int>(std::floor(std::log2(ratio)));
    return std::clamp(std::min(requestedCascadeCount, distanceCascadeCount), 1, 4);
}

std::array<float, 4> ShadowSystem::ResolveCascadeFarPlanes(float shadowDistance, int cascadeCount)
{
    constexpr float nearDistance = 0.1f;
    constexpr float nearCascadeLength = 10.0f;

    shadowDistance = std::clamp(shadowDistance, 10.0f, 10000.0f);
    cascadeCount = ResolveCascadeCount(shadowDistance, cascadeCount);
    std::array<float, 4> farPlanes{};

    // Keep the first cascade anchored near the camera and distribute the
    // remaining range geometrically. Fixed 12/35/90 cuts left an enormous
    // final slice (for example 90..490 m), making distant shadows visibly
    // coarser despite an unchanged shadow-map resolution.
    const float firstSplit = std::min(nearCascadeLength, shadowDistance);
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (i == cascadeCount - 1)
        {
            farPlanes[static_cast<std::size_t>(i)] = shadowDistance;
            break;
        }
        if (i == 0)
        {
            farPlanes[static_cast<std::size_t>(i)] = firstSplit;
            continue;
        }

        const float fraction = static_cast<float>(i) /
            static_cast<float>(cascadeCount - 1);
        farPlanes[static_cast<std::size_t>(i)] = firstSplit *
            std::pow(shadowDistance / firstSplit, fraction);
    }

    for (int i = cascadeCount; i < 4; ++i)
        farPlanes[static_cast<std::size_t>(i)] = shadowDistance;
    return farPlanes;
}
