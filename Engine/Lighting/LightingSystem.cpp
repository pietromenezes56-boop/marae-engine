#include "LightingSystem.h"

#include <algorithm>
#include <cmath>

glm::vec3 LightingSystem::ColorTemperatureToRGB(float kelvin)
{
    const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float red = 255.0f;
    float green = 255.0f;
    float blue = 255.0f;

    if (temperature <= 66.0f)
    {
        red = 255.0f;
        green = 99.4708f * std::log(std::max(temperature, 1.0f)) - 161.1196f;
        if (temperature <= 19.0f)
            blue = 0.0f;
        else
            blue = 138.5177f * std::log(temperature - 10.0f) - 305.0448f;
    }
    else
    {
        red = 329.6987f * std::pow(temperature - 60.0f, -0.1332048f);
        green = 288.1222f * std::pow(temperature - 60.0f, -0.0755148f);
        blue = 255.0f;
    }

    return glm::clamp(glm::vec3(red, green, blue) / 255.0f,
                      glm::vec3(0.0f), glm::vec3(1.0f));
}

float LightingSystem::InverseSquareAttenuation(float distance, float sourceRadius)
{
    const float radius = std::max(sourceRadius, 0.025f);
    const float distanceSquared = std::max(distance * distance, radius * radius);
    return 1.0f / distanceSquared;
}

DayNightSample LightingSystem::EvaluateDayNightCycle(const LightingSettings& settings)
{
    const float hours = std::fmod(std::clamp(settings.DayTimeHours, 0.0f, 24.0f), 24.0f);
    const float solarPhase = (hours - 6.0f) * 3.14159265359f / 12.0f;
    const float altitudeSine = std::sin(solarPhase);
    const float altitudeCosine = std::cos(solarPhase);
    const float azimuth = glm::radians(settings.SunAzimuthDegrees);

    DayNightSample sample;
    sample.RayDirection = glm::normalize(glm::vec3(
        altitudeCosine * std::cos(azimuth),
        -altitudeSine,
        altitudeCosine * std::sin(azimuth)));
    sample.Daylight = glm::smoothstep(-0.12f, 0.10f, altitudeSine);
    sample.Night = 1.0f - sample.Daylight;
    sample.TemperatureKelvin = glm::mix(
        2200.0f,
        5600.0f,
        glm::smoothstep(-0.04f, 0.72f, altitudeSine));
    sample.Color = ColorTemperatureToRGB(sample.TemperatureKelvin);
    sample.Intensity = sample.Daylight * std::clamp(settings.SunIntensityScale, 0.0f, 10.0f);
    return sample;
}

void LightingSystem::AdvanceDayNightCycle(LightingSettings& settings, float deltaSeconds)
{
    if (!settings.DayNightCycleEnabled || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
        return;

    // Limita saltos por pausa/perda de foco, mantendo o ciclo estável e previsível.
    const float safeDelta = std::clamp(deltaSeconds, 0.0f, 1.0f);
    settings.DayTimeHours = std::fmod(
        settings.DayTimeHours + safeDelta * settings.DayNightCycleSpeedHoursPerSecond,
        24.0f);
    if (settings.DayTimeHours < 0.0f)
        settings.DayTimeHours += 24.0f;
}
