#include "InstancedRenderer.h"
#include <iostream>

InstancedRenderer::InstancedRenderer()
{
}

InstancedRenderer::~InstancedRenderer()
{
    m_InstancedMeshes.clear();
}

void InstancedRenderer::AddInstance(const MeshKey& key, const InstancedMesh::InstanceData& instance)
{
    auto it = m_InstancedMeshes.find(key);
    if (it == m_InstancedMeshes.end())
    {
        std::cout << "[InstancedRenderer] WARNING: Mesh not registered for key: "
                  << key.meshPath << std::endl;
        return;
    }

    it->second.instances.push_back(instance);
    m_TotalInstances++;
}

void InstancedRenderer::ClearInstances()
{
    for (auto& pair : m_InstancedMeshes)
    {
        pair.second.instances.clear();
    }
    m_TotalInstances = 0;
}

void InstancedRenderer::Render()
{
    if (m_InstancedMeshes.empty())
        return;

    size_t totalDrawCalls = 0;
    size_t totalInstances = 0;

    for (auto& pair : m_InstancedMeshes)
    {
        if (pair.second.instances.empty())
            continue;

        // Update instance buffer
        pair.second.mesh->UpdateInstances(pair.second.instances);

        // Draw all instances in one call
        pair.second.mesh->Draw();

        totalDrawCalls++;
        totalInstances += pair.second.instances.size();
    }

    std::cout << "[InstancedRenderer] Rendered " << totalInstances << " instances in "
              << totalDrawCalls << " draw calls" << std::endl;
}

void InstancedRenderer::RegisterMesh(const MeshKey& key, const float* vertices, size_t vertexCount,
                                     size_t vertexStride, const unsigned int* indices, size_t indexCount)
{
    auto it = m_InstancedMeshes.find(key);
    if (it != m_InstancedMeshes.end())
    {
        std::cout << "[InstancedRenderer] Mesh already registered: " << key.meshPath << std::endl;
        return;
    }

    InstancedMeshData data;
    data.mesh = std::make_unique<InstancedMesh>();
    data.mesh->CreateBaseMesh(vertices, vertexCount, vertexStride, indices, indexCount);
    data.mesh->SetMaxInstances(m_MaxInstancesPerMesh);

    m_InstancedMeshes[key] = std::move(data);

    std::cout << "[InstancedRenderer] Registered mesh: " << key.meshPath
              << " with material: " << key.materialPath << std::endl;
}

void InstancedRenderer::SetMaxInstancesPerMesh(size_t maxInstances)
{
    m_MaxInstancesPerMesh = maxInstances;

    // Update existing meshes
    for (auto& pair : m_InstancedMeshes)
    {
        pair.second.mesh->SetMaxInstances(maxInstances);
    }

    std::cout << "[InstancedRenderer] Max instances per mesh set to: " << maxInstances << std::endl;
}
