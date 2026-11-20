#pragma once

#include <vector>

#include "AmbientLight.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "LightingSettings.h"

class LightingManager
{
public:

    LightingManager()
    {
        Moon.Direction = Vector3{0.4f, 1.0f, 0.4f};
        Moon.Color = Vector3{1.0f, 1.0f, 1.0f};
        Moon.Intensity = 0.08f;
        Moon.SourceRadius = 0.25f;
        Moon.ShadowSoftness = 0.45f;
    }

    LightingSettings Settings;

    AmbientLight Ambient;

    DirectionalLight Sun;

    // Fonte celeste noturna. Ela usa o mesmo caminho de iluminação e sombras
    // do sol; durante o ciclo, sua direção é calculada como o oposto do sol.
    DirectionalLight Moon;

    std::vector<PointLight> PointLights;

    std::vector<SpotLight> SpotLights;
};
