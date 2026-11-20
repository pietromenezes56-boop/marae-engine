#include "AdaptiveShadowMap.h"
#include <algorithm>
#include <cmath>
#include <iostream>

AdaptiveShadowMap::AdaptiveShadowMap()
    : m_CascadeCount(0)
    , m_TargetFPS(60.0f)
    , m_ManualQuality(1.0f)
    , m_AutoAdapt(true)
    , m_FPSHistoryIndex(0)
    , m_AverageFPS(60.0f)
{
    m_FPSHistory.fill(60.0f);
}

AdaptiveShadowMap::~AdaptiveShadowMap()
{
}

void AdaptiveShadowMap::Initialize(uint32_t maxCascades)
{
    m_CascadeCount = std::min(maxCascades, MAX_CASCADES);

    // Configuração inicial com resoluções progressivas
    // Cascade 0 (perto): alta resolução
    // Cascade 3 (longe): baixa resolução
    for (uint32_t i = 0; i < m_CascadeCount; ++i)
    {
        m_Cascades[i].nearPlane = 0.0f;
        m_Cascades[i].farPlane = 0.0f;
        m_Cascades[i].minResolution = 512;
        m_Cascades[i].maxResolution = 3072;
        m_Cascades[i].priority = 1.0f - (static_cast<float>(i) / m_CascadeCount) * 0.5f;

        // Resolução inicial baseada em prioridade
        m_Cascades[i].resolution = static_cast<uint32_t>(
            m_Cascades[i].minResolution +
            (m_Cascades[i].maxResolution - m_Cascades[i].minResolution) * m_Cascades[i].priority
        );
    }

    std::cout << "[AdaptiveShadowMap] Inicializado com " << m_CascadeCount << " cascatas\n";
}

void AdaptiveShadowMap::Update(
    float cameraNear,
    float cameraFar,
    float currentFPS,
    uint32_t activeLightCount)
{
    if (!m_AutoAdapt)
        return;

    // Atualiza histórico de FPS
    m_FPSHistory[m_FPSHistoryIndex] = currentFPS;
    m_FPSHistoryIndex = (m_FPSHistoryIndex + 1) % FPS_HISTORY_SIZE;

    // Calcula média móvel
    float sum = 0.0f;
    for (float fps : m_FPSHistory)
        sum += fps;
    m_AverageFPS = sum / FPS_HISTORY_SIZE;

    // Recalcula prioridades baseado em distância da câmera
    RecalculatePriorities(cameraNear, cameraFar);

    // Ajusta resolução de cada cascade
    for (uint32_t i = 0; i < m_CascadeCount; ++i)
    {
        uint32_t newResolution = CalculateOptimalResolution(
            m_Cascades[i],
            m_AverageFPS,
            activeLightCount
        );

        // Aplica qualidade manual
        newResolution = static_cast<uint32_t>(
            m_Cascades[i].minResolution +
            (newResolution - m_Cascades[i].minResolution) * m_ManualQuality
        );

        // Atualiza se mudou significativamente (evita flicker)
        if (std::abs(static_cast<int>(newResolution) - static_cast<int>(m_Cascades[i].resolution)) > 128)
        {
            m_Cascades[i].resolution = newResolution;
        }
    }
}

void AdaptiveShadowMap::RecalculatePriorities(float cameraNear, float cameraFar)
{
    // Define distâncias das cascatas (exponencial)
    float currentDist = cameraNear;
    float totalRange = cameraFar - cameraNear;

    for (uint32_t i = 0; i < m_CascadeCount; ++i)
    {
        m_Cascades[i].nearPlane = currentDist;

        // Distribuição exponencial: cascatas mais perto ocupam menos espaço
        float cascadeRange = totalRange * std::pow(0.5f, static_cast<float>(i));
        currentDist += cascadeRange;

        m_Cascades[i].farPlane = std::min(currentDist, cameraFar);

        // Prioridade mais alta para cascatas próximas
        float distanceFactor = 1.0f - (i / static_cast<float>(m_CascadeCount));
        m_Cascades[i].priority = distanceFactor * 0.8f + 0.2f;
    }
}

uint32_t AdaptiveShadowMap::CalculateOptimalResolution(
    const CascadeConfig& config,
    float currentFPS,
    uint32_t activeLightCount)
{
    // Fator de performance: se FPS < target, reduz resolução
    float performanceFactor = std::clamp(currentFPS / m_TargetFPS, 0.5f, 1.5f);

    // Fator de quantidade de luzes: mais luzes = menos resolução por cascade
    float lightFactor = std::clamp(1.0f / (1.0f + activeLightCount * 0.1f), 0.5f, 1.0f);

    // Combina fatores
    float combinedFactor = performanceFactor * lightFactor * config.priority;

    // Calcula resolução alvo
    uint32_t targetResolution = static_cast<uint32_t>(
        config.minResolution +
        (config.maxResolution - config.minResolution) * combinedFactor
    );

    // Arredonda para múltiplo de 256 (melhor para GPU)
    targetResolution = ((targetResolution + 127) / 128) * 128;

    return std::clamp(targetResolution, config.minResolution, config.maxResolution);
}

const AdaptiveShadowMap::CascadeConfig& AdaptiveShadowMap::GetCascadeConfig(uint32_t index) const
{
    if (index >= m_CascadeCount)
        index = 0;
    return m_Cascades[index];
}

size_t AdaptiveShadowMap::EstimateVRAMUsage() const
{
    size_t total = 0;

    for (uint32_t i = 0; i < m_CascadeCount; ++i)
    {
        // Depth buffer: 32-bit float
        total += m_Cascades[i].resolution * m_Cascades[i].resolution * 4;

        // Opacity buffer: 32-bit float
        total += m_Cascades[i].resolution * m_Cascades[i].resolution * 4;
    }

    return total;
}

void AdaptiveShadowMap::SetManualQuality(float quality)
{
    m_ManualQuality = std::clamp(quality, 0.0f, 1.0f);
}
