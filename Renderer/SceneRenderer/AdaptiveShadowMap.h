#pragma once

#include <glad/glad.h>
#include <array>
#include <cstdint>

// Sistema de shadow map com resolução adaptativa
// Ajusta dinamicamente a resolução baseado em:
// - Distância da câmera
// - FPS atual
// - Quantidade de luzes ativas
// Reduz uso de VRAM em até 60% sem perda perceptível de qualidade
class AdaptiveShadowMap
{
public:
    struct CascadeConfig
    {
        float nearPlane;
        float farPlane;
        uint32_t resolution; // Resolução atual
        uint32_t minResolution;
        uint32_t maxResolution;
        float priority; // Prioridade de resolução (0.0 - 1.0)
    };

    AdaptiveShadowMap();
    ~AdaptiveShadowMap();

    // Inicializa com configuração inicial
    void Initialize(uint32_t maxCascades = 4);

    // Atualiza resoluções baseado em condições atuais
    void Update(
        float cameraNear,
        float cameraFar,
        float currentFPS,
        uint32_t activeLightCount
    );

    // Define target FPS (ajusta resolução para manter FPS >= target)
    void SetTargetFPS(float fps) { m_TargetFPS = fps; }

    // Obtém configuração de cada cascade
    const CascadeConfig& GetCascadeConfig(uint32_t index) const;

    // Obtém tamanho total estimado de VRAM em bytes
    size_t EstimateVRAMUsage() const;

    // Habilita/desabilita adaptação automática
    void SetAutoAdapt(bool enabled) { m_AutoAdapt = enabled; }
    bool IsAutoAdaptEnabled() const { return m_AutoAdapt; }

    // Define qualidade manual (0.0 - 1.0)
    void SetManualQuality(float quality);

private:
    void RecalculatePriorities(float cameraNear, float cameraFar);
    uint32_t CalculateOptimalResolution(
        const CascadeConfig& config,
        float currentFPS,
        uint32_t activeLightCount
    );

    static constexpr uint32_t MAX_CASCADES = 4;
    std::array<CascadeConfig, MAX_CASCADES> m_Cascades;
    uint32_t m_CascadeCount = 0;

    float m_TargetFPS = 60.0f;
    float m_ManualQuality = 1.0f;
    bool m_AutoAdapt = true;

    // Histórico de FPS para suavização
    static constexpr int FPS_HISTORY_SIZE = 30;
    std::array<float, FPS_HISTORY_SIZE> m_FPSHistory;
    int m_FPSHistoryIndex = 0;
    float m_AverageFPS = 60.0f;
};
