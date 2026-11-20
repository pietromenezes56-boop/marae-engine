#include "RenderMemoryPool.h"
#include <iostream>

RenderMemoryPool::RenderMemoryPool()
    : m_TotalAllocated(0)
    , m_TotalUsed(0)
    , m_MaxEntities(256)
    , m_MaxInstances(512)
    , m_MaxLights(16)
{
}

RenderMemoryPool::~RenderMemoryPool()
{
    Release();
}

void RenderMemoryPool::Initialize(
    size_t maxEntities,
    size_t maxInstances,
    size_t maxLights)
{
    m_MaxEntities = maxEntities;
    m_MaxInstances = maxInstances;
    m_MaxLights = maxLights;

    // Pré-aloca buffers
    m_EntityIDs.reserve(maxEntities);
    m_ModelMatrices.reserve(maxEntities);
    m_SelectionFlags.reserve(maxEntities);
    m_InstanceMatrices.reserve(maxInstances);
    m_InstanceAlphas.reserve(maxInstances);
    m_LightPositions.reserve(maxLights);
    m_LightColors.reserve(maxLights);

    m_TotalAllocated =
        maxEntities * (sizeof(int) + sizeof(glm::mat4) + sizeof(unsigned char)) +
        maxInstances * (sizeof(glm::mat4) + sizeof(float)) +
        maxLights * (sizeof(glm::vec3) * 2);

    std::cout << "[RenderMemoryPool] Inicializado com "
              << (m_TotalAllocated / 1024 / 1024) << " MB pré-alocados\n";
}

std::vector<int>& RenderMemoryPool::GetEntityIDBuffer()
{
    if (m_EntityIDs.capacity() < m_MaxEntities)
        m_EntityIDs.reserve(m_MaxEntities);
    return m_EntityIDs;
}

std::vector<glm::mat4>& RenderMemoryPool::GetModelMatrixBuffer()
{
    if (m_ModelMatrices.capacity() < m_MaxEntities)
        m_ModelMatrices.reserve(m_MaxEntities);
    return m_ModelMatrices;
}

std::vector<unsigned char>& RenderMemoryPool::GetSelectionFlagsBuffer()
{
    if (m_SelectionFlags.capacity() < m_MaxEntities)
        m_SelectionFlags.reserve(m_MaxEntities);
    return m_SelectionFlags;
}

std::vector<glm::mat4>& RenderMemoryPool::GetInstanceMatrixBuffer()
{
    if (m_InstanceMatrices.capacity() < m_MaxInstances)
        m_InstanceMatrices.reserve(m_MaxInstances);
    return m_InstanceMatrices;
}

std::vector<float>& RenderMemoryPool::GetInstanceAlphaBuffer()
{
    if (m_InstanceAlphas.capacity() < m_MaxInstances)
        m_InstanceAlphas.reserve(m_MaxInstances);
    return m_InstanceAlphas;
}

std::vector<glm::vec3>& RenderMemoryPool::GetLightPositionBuffer()
{
    if (m_LightPositions.capacity() < m_MaxLights)
        m_LightPositions.reserve(m_MaxLights);
    return m_LightPositions;
}

std::vector<glm::vec3>& RenderMemoryPool::GetLightColorBuffer()
{
    if (m_LightColors.capacity() < m_MaxLights)
        m_LightColors.reserve(m_MaxLights);
    return m_LightColors;
}

void RenderMemoryPool::Reset()
{
    // Limpa buffers sem liberar memória
    m_EntityIDs.clear();
    m_ModelMatrices.clear();
    m_SelectionFlags.clear();
    m_InstanceMatrices.clear();
    m_InstanceAlphas.clear();
    m_LightPositions.clear();
    m_LightColors.clear();

    m_TotalUsed = 0;
}

void RenderMemoryPool::Release()
{
    m_EntityIDs.clear();
    m_EntityIDs.shrink_to_fit();

    m_ModelMatrices.clear();
    m_ModelMatrices.shrink_to_fit();

    m_SelectionFlags.clear();
    m_SelectionFlags.shrink_to_fit();

    m_InstanceMatrices.clear();
    m_InstanceMatrices.shrink_to_fit();

    m_InstanceAlphas.clear();
    m_InstanceAlphas.shrink_to_fit();

    m_LightPositions.clear();
    m_LightPositions.shrink_to_fit();

    m_LightColors.clear();
    m_LightColors.shrink_to_fit();

    m_TotalAllocated = 0;
    m_TotalUsed = 0;

    std::cout << "[RenderMemoryPool] Memória liberada\n";
}

size_t RenderMemoryPool::GetTotalAllocated() const
{
    return m_TotalAllocated;
}

size_t RenderMemoryPool::GetTotalUsed() const
{
    return m_TotalUsed;
}
