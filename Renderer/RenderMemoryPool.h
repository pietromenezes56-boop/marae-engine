#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

// Memory pool pré-alocado para reduzir alocações durante renderização
// Evita malloc/free a cada frame em hot paths
class RenderMemoryPool
{
public:
    RenderMemoryPool();
    ~RenderMemoryPool();

    // Inicializa pools com tamanhos estimados
    // Valores reduzidos para menor uso de RAM inicial; crescem automaticamente sob demanda
    void Initialize(
        size_t maxEntities = 256,
        size_t maxInstances = 512,
        size_t maxLights = 16
    );

    // Obtém buffer scratch para entidades (resizado automaticamente se necessário)
    std::vector<int>& GetEntityIDBuffer();
    std::vector<glm::mat4>& GetModelMatrixBuffer();
    std::vector<unsigned char>& GetSelectionFlagsBuffer();

    // Obtém buffer para instanced rendering
    std::vector<glm::mat4>& GetInstanceMatrixBuffer();
    std::vector<float>& GetInstanceAlphaBuffer();

    // Obtém buffer para luzes
    std::vector<glm::vec3>& GetLightPositionBuffer();
    std::vector<glm::vec3>& GetLightColorBuffer();

    // Reseta pools para reuso (não libera memória)
    void Reset();

    // Libera toda memória (chamado ao fechar aplicação)
    void Release();

    // Estatísticas
    size_t GetTotalAllocated() const;
    size_t GetTotalUsed() const;

private:
    // Buffers pré-alocados
    std::vector<int> m_EntityIDs;
    std::vector<glm::mat4> m_ModelMatrices;
    std::vector<unsigned char> m_SelectionFlags;
    std::vector<glm::mat4> m_InstanceMatrices;
    std::vector<float> m_InstanceAlphas;
    std::vector<glm::vec3> m_LightPositions;
    std::vector<glm::vec3> m_LightColors;

    // Contadores de uso
    size_t m_TotalAllocated = 0;
    size_t m_TotalUsed = 0;

    // Configuração (valores compactos; crescem sob demanda via push_back)
    size_t m_MaxEntities = 256;
    size_t m_MaxInstances = 512;
    size_t m_MaxLights = 16;
};
