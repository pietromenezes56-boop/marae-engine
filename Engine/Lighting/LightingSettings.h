#pragma once

#include <algorithm>

// Configuração global do sistema de iluminação avançada. O recurso é opt-in:
// projetos antigos e cenas que não precisam de iluminação dinâmica continuam
// no caminho barato quando Enabled permanece falso.
enum class LightingQuality
{
    Performance = 0,
    Balanced = 1,
    High = 2,
    Ultra = 3
};

struct LightingSettings
{
    bool Enabled = false;
    bool DynamicShadows = true;
    bool ContactShadows = true;
    bool IndirectLighting = true;
    bool EnvironmentLighting = true;
    bool DayNightCycleEnabled = false;
    bool DirectionalDistanceSoftening = true;

    // Padrão leve: iluminação local continua funcionando, mas sem custo de
    // Ultra/irradiância pesada em todo projeto novo.
    LightingQuality Quality = LightingQuality::Performance;

    int MaxPointLights = 4;
    int MaxSpotLights = 4;
    int MaxShadowedLocalLights = 1;

    float IndirectIntensity = 0.35f;
    float ContactShadowStrength = 0.35f;
    float ContactShadowDistance = 0.75f;
    float ShadowDistance = 220.0f;

    int ShadowMapResolution = 1024;
    int PointShadowMapResolution = 512;

    // Hora solar em horas locais (0..24); a velocidade é expressa em
    // horas de jogo por segundo real e só avança durante o runtime.
    float DayTimeHours = 12.0f;
    float DayNightCycleSpeedHoursPerSecond = 0.005f;
    float SunAzimuthDegrees = 35.0f;
    // Diâmetro angular aparente do Sol (e da Lua quando ela assume a luz
    // direcional), em graus. 0.53° aproxima o valor observado da Terra.
    float DirectionalSourceAngularDiameterDegrees = 0.53f;
    float SunIntensityScale = 3.0f;

    void Clamp()
    {
        Quality = static_cast<LightingQuality>(
            std::clamp(static_cast<int>(Quality), 0, 3));
        MaxPointLights = std::clamp(MaxPointLights, 0, 8);
        MaxSpotLights = std::clamp(MaxSpotLights, 0, 8);
        MaxShadowedLocalLights = std::clamp(MaxShadowedLocalLights, 0, 4);
        IndirectIntensity = std::clamp(IndirectIntensity, 0.0f, 2.0f);
        ContactShadowStrength = std::clamp(ContactShadowStrength, 0.0f, 1.0f);
        ContactShadowDistance = std::clamp(ContactShadowDistance, 0.01f, 10.0f);
        ShadowDistance = std::clamp(ShadowDistance, 10.0f, 10000.0f);
        ShadowMapResolution = std::clamp(ShadowMapResolution, 512, 4096);
        PointShadowMapResolution = std::clamp(PointShadowMapResolution, 256, 2048);
        DayTimeHours = std::clamp(DayTimeHours, 0.0f, 24.0f);
        DayNightCycleSpeedHoursPerSecond = std::clamp(
            DayNightCycleSpeedHoursPerSecond, 0.00001f, 1.0f);
        SunAzimuthDegrees = std::clamp(SunAzimuthDegrees, -360.0f, 360.0f);
        DirectionalSourceAngularDiameterDegrees = std::clamp(
            DirectionalSourceAngularDiameterDegrees, 0.01f, 5.0f);
        SunIntensityScale = std::clamp(SunIntensityScale, 0.0f, 10.0f);
    }
};
