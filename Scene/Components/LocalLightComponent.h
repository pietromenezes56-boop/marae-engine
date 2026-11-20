#pragma once

// ---------------------------------------------------------------------------
// LocalLightComponent
//
// Componente que configura como um objeto 3D RECEBE luz.
// Similar ao Global Light, mas específico para cada objeto.
// Controla influência de diferentes tipos de luz (solar, artificial, ambiente).
// Permite override de sombras, reflexo e AO por objeto.
// ---------------------------------------------------------------------------

struct LocalLightComponent
{
    bool Enabled = true;

    // Refletividade
    float Metallic = 0.0f;        // 0 = dielétrico, 1 = metálico
    float Roughness = 0.5f;      // 0 = espelho, 1 = mate
    float Emission = 0.0f;       // Intensidade de luz emitida

    // Cor da emissão
    float EmissionColor[3] = { 1.0f, 1.0f, 1.0f };

    // Influência de luzes
    float SunLightInfluence = 1.0f;     // Influência da luz solar
    float ArtificialLightInfluence = 1.0f; // Influência de luzes artificiais
    float AmbientLightInfluence = 1.0f;  // Influência da luz ambiente

    // Sombras (override por objeto)
    bool ReceiveShadows = true;
    float ShadowStrength = 1.0f;
    float ShadowBias = 0.005f;

    // Reflexo (override por objeto)
    bool EnableReflections = false;
    float ReflectionStrength = 0.5f;

    // Oclusão ambiente (override por objeto)
    bool EnableAO = true;
    float AOStrength = 1.0f;

    LocalLightComponent() = default;
};
