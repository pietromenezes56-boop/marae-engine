#pragma once

#include "../../Math/Vector3.h"

class Light
{
public:

    Vector3 Color = { 1.0f, 1.0f, 1.0f };

    float Intensity = 1.0f;

    // Kelvin é convertido para RGB no LightingSystem. O recurso fica
    // desligado por padrão para preservar a cor explícita de assets antigos.
    bool UseTemperature = false;
    float TemperatureKelvin = 6500.0f;
    float SourceRadius = 0.15f;
    float ShadowSoftness = 0.25f;
    float ContactShadowStrength = 0.35f;

    virtual ~Light() = default;
};
