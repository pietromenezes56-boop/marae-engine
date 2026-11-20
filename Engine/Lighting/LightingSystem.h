#pragma once

#include "LightingSettings.h"
#include <glm/glm.hpp>

struct DayNightSample
{
    glm::vec3 RayDirection{ -0.4f, -1.0f, -0.4f };
    glm::vec3 Color{ 1.0f };
    float Intensity = 0.0f;
    float TemperatureKelvin = 5600.0f;
    float Daylight = 0.0f;
    float Night = 1.0f;
};

// Funções sem estado usadas pelo renderer para converter parâmetros físicos
// em dados seguros para o shader. Mantê-las fora do SceneRenderer facilita
// adicionar outras técnicas de iluminação depois.
class LightingSystem
{
public:
    static glm::vec3 ColorTemperatureToRGB(float kelvin);
    static float InverseSquareAttenuation(float distance, float sourceRadius);
    static DayNightSample EvaluateDayNightCycle(const LightingSettings& settings);
    static void AdvanceDayNightCycle(LightingSettings& settings, float deltaSeconds);
    static bool IsEnabled(const LightingSettings& settings) { return settings.Enabled; }
};
