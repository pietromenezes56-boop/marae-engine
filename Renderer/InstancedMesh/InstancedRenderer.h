#pragma once

#include "InstancedMesh.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <glm/glm.hpp>

// InstancedRenderer - Gerencia renderização instanciada de objetos repetidos
// Detecta automaticamente objetos com o mesmo mesh e renderiza em batch
class InstancedRenderer
{
public:
    struct MeshKey
    {
        std::string meshPath;
        std::string materialPath;

        bool operator==(const MeshKey& other) const
        {
            return meshPath == other.meshPath && materialPath == other.materialPath;
        }
    };

    struct MeshKeyHash
    {
        std::size_t operator()(const MeshKey& key) const
        {
            return std::hash<std::string>()(key.meshPath) ^ std::hash<std::string>()(key.materialPath);
        }
    };

    InstancedRenderer();
    ~InstancedRenderer();

    // Adiciona uma instância para renderização
    void AddInstance(const MeshKey& key, const InstancedMesh::InstanceData& instance);

    // Limpa todas as instâncias (chamado a cada frame)
    void ClearInstances();

    // Renderiza todas as instâncias agrupadas
    void Render();

    // Registra um mesh base para uma chave
    void RegisterMesh(const MeshKey& key, const float* vertices, size_t vertexCount, size_t vertexStride,
                     const unsigned int* indices, size_t indexCount);

    // Define capacidade máxima de instâncias por mesh
    void SetMaxInstancesPerMesh(size_t maxInstances);

    // Estatísticas
    size_t GetTotalInstances() const { return m_TotalInstances; }
    size_t GetUniqueMeshes() const { return m_InstancedMeshes.size(); }
    size_t GetDrawCalls() const { return m_InstancedMeshes.size(); } // 1 draw call por mesh único

private:
    struct InstancedMeshData
    {
        std::unique_ptr<InstancedMesh> mesh;
        std::vector<InstancedMesh::InstanceData> instances;
    };

    std::unordered_map<MeshKey, InstancedMeshData, MeshKeyHash> m_InstancedMeshes;
    size_t m_MaxInstancesPerMesh = 1000;
    size_t m_TotalInstances = 0;
};
