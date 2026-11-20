#include "ShadowSystem.h"

#include <algorithm>

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

std::array<float, 4> ShadowSystem::ResolveCascadeFarPlanes(float shadowDistance, int cascadeCount)
{
    constexpr float nearDistance = 0.1f;
    constexpr std::array<float, 3> referenceSplits = { 12.0f, 35.0f, 90.0f };

    shadowDistance = std::clamp(shadowDistance, 10.0f, 10000.0f);
    cascadeCount = std::clamp(cascadeCount, 1, 4);
    std::array<float, 4> farPlanes{};

    // Use the familiar near-field split points when available, but always
    // make the last active cascade reach the requested maximum distance.
    // Lower quality presets skip intermediate cuts instead of silently
    // truncating shadows at 12/35/90 metres.
    std::array<float, 3> activeReferenceSplits{};
    if (cascadeCount == 4)
    {
        activeReferenceSplits = referenceSplits;
    }
    else if (cascadeCount == 3)
    {
        activeReferenceSplits = { 12.0f, 90.0f, 0.0f };
    }
    else if (cascadeCount == 2)
    {
        activeReferenceSplits = { 35.0f, 0.0f, 0.0f };
    }

    float previous = nearDistance;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (i == cascadeCount - 1)
        {
            farPlanes[static_cast<std::size_t>(i)] = shadowDistance;
            break;
        }

        // A distância máxima só estende a última cascata. Escalar todos os
        // cortes pelo alcance total também aumentava o frustum das cascatas
        // próximas, reduzindo a densidade de texels mesmo sem mudar a resolução.
        const float split = activeReferenceSplits[static_cast<std::size_t>(i)];
        const float remainingSplits = static_cast<float>(cascadeCount - i - 1);
        const float minimumForThisSplit = previous + 0.05f;
        const float maximumForThisSplit = std::max(
            minimumForThisSplit,
            shadowDistance - remainingSplits * 0.05f);
        previous = std::clamp(split, minimumForThisSplit, maximumForThisSplit);
        farPlanes[static_cast<std::size_t>(i)] = previous;
    }

    for (int i = cascadeCount; i < 4; ++i)
        farPlanes[static_cast<std::size_t>(i)] = shadowDistance;
    return farPlanes;
}
