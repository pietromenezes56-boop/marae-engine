#pragma once

// ---------------------------------------------------------------------------
// LightComponent
//
// Componente que transforma qualquer entidade numa fonte de luz.
// A posição da luz é sempre a posição do Transform da entidade — não há
// campo Position separado; isso garante que mover a entidade move a luz.
//
// Tipos suportados:
//   Point   — luz omnidirecional com atenuação quadrática
//   Spot    — cone de luz com ângulo interno/externo (soft cutoff)
// ---------------------------------------------------------------------------

enum class LightType
{
    Point = 0,
    Spot  = 1,
    Directional = 2,
    Area = 3
};

struct LightComponent
{
    bool      Enabled   = true;
    bool      CastShadows = true;
    LightType Type      = LightType::Point;

    // Cor e intensidade
    float Color[3]  = { 1.0f, 1.0f, 1.0f };
    float Intensity = 1.0f;

    bool UseTemperature = false;
    float TemperatureKelvin = 6500.0f;

    // Alcance máximo da luz (Point e Spot)
    float Range     = 10.0f;

    // Area usa uma aproximação de fonte pontual com raio emissivo para manter
    // compatibilidade com o pipeline OpenGL atual e produzir penumbra suave.
    float SourceRadius = 0.15f;
    float ShadowSoftness = 0.25f;
    float ShadowStrength = 1.0f;
    bool DistanceSoftening = true;
    float ContactShadowStrength = 0.35f;
    bool IndirectContribution = true;

    // Spot: ângulo do cone interno (área de luz plena) e externo (penumbra)
    // em graus. InnerAngle <= OuterAngle.
    float InnerAngleDeg = 20.0f;
    float OuterAngleDeg = 35.0f;

    // Spot: direção local (normalizada no shader via model matrix da entidade)
    float DirectionLocal[3] = { 0.0f, -1.0f, 0.0f };

    LightComponent() = default;
};
