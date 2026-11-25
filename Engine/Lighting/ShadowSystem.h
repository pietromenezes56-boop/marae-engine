#pragma once

#include "LightingSettings.h"
#include <array>
#include <cstddef>

enum class GraphicsQuality;

// Política de recursos de sombra. O SceneRenderer continua sendo o dono dos
// passes OpenGL, enquanto esta classe centraliza decisões de custo/qualidade.
class ShadowSystem
{
public:
    static bool ShouldRender(const LightingSettings& settings, bool rendererAllowsShadows);
    static unsigned int ResolveDirectionalResolution(
        const LightingSettings& settings,
        unsigned int rendererResolution);
    static unsigned int ResolvePointResolution(
        const LightingSettings& settings,
        unsigned int rendererResolution);
    static float ResolveShadowDistance(const LightingSettings& settings, float fallback);
    static int ResolveCascadeCount(float shadowDistance, int requestedCascadeCount);
    static std::array<float, 4> ResolveCascadeFarPlanes(float shadowDistance, int cascadeCount);
};
