#include "InstancedMesh.h"
#include <iostream>

InstancedMesh::InstancedMesh()
{
}

InstancedMesh::~InstancedMesh()
{
    Clear();
}

void InstancedMesh::CreateBaseMesh(const float* vertices, size_t vertexCount, size_t vertexStride,
                                   const unsigned int* indices, size_t indexCount)
{
    m_VertexCount = vertexCount;
    m_IndexCount = indexCount;
    m_UsingIndices = (indices != nullptr && indexCount > 0);

    // Create vertex buffer
    glGenBuffers(1, &m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexStride, vertices, GL_STATIC_DRAW);

    // Create index buffer if provided
    if (m_UsingIndices)
    {
        glGenBuffers(1, &m_IBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(unsigned int), indices, GL_STATIC_DRAW);
    }

    std::cout << "[InstancedMesh] Base mesh created: " << vertexCount << " vertices, "
              << indexCount << " indices" << std::endl;
}

void InstancedMesh::SetMaxInstances(size_t maxInstances)
{
    if (m_MaxInstances == maxInstances)
        return;

    m_MaxInstances = maxInstances;
    CreateInstanceBuffer();
    CreateVAO();

    std::cout << "[InstancedMesh] Max instances set to: " << maxInstances << std::endl;
}

void InstancedMesh::UpdateInstances(const std::vector<InstanceData>& instances)
{
    if (instances.empty())
    {
        m_InstanceCount = 0;
        return;
    }

    if (instances.size() > m_MaxInstances)
    {
        std::cout << "[InstancedMesh] WARNING: Instance count (" << instances.size()
                  << ") exceeds max (" << m_MaxInstances << ")" << std::endl;
        m_InstanceCount = m_MaxInstances;
    }
    else
    {
        m_InstanceCount = instances.size();
    }

    // Update instance buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, m_InstanceCount * sizeof(InstanceData), instances.data());
}

void InstancedMesh::Draw()
{
    if (!IsValid() || m_InstanceCount == 0)
        return;

    glBindVertexArray(m_VAO);

    if (m_UsingIndices)
    {
        glDrawElementsInstanced(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr, m_InstanceCount);
    }
    else
    {
        glDrawArraysInstanced(GL_TRIANGLES, 0, m_VertexCount, m_InstanceCount);
    }

    glBindVertexArray(0);
}

void InstancedMesh::Clear()
{
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_IBO) glDeleteBuffers(1, &m_IBO);
    if (m_InstanceVBO) glDeleteBuffers(1, &m_InstanceVBO);

    m_VAO = 0;
    m_VBO = 0;
    m_IBO = 0;
    m_InstanceVBO = 0;
    m_VertexCount = 0;
    m_IndexCount = 0;
    m_InstanceCount = 0;
    m_MaxInstances = 0;
}

void InstancedMesh::CreateVAO()
{
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);

    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // Bind vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    // Vertex attributes (assuming standard P3N3T2 format)
    // Position (3 floats)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    // Normal (3 floats)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    // UV (2 floats)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    // Bind index buffer
    if (m_UsingIndices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
    }

    // Bind instance buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);

    // Instance attributes (divisor = 1 means one per instance)
    GLsizei stride = sizeof(InstanceData);

    // Model matrix (4x4 = 4 vec4s)
    for (int i = 0; i < 4; i++)
    {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, stride, (void*)(i * sizeof(glm::vec4)));
        glVertexAttribDivisor(3 + i, 1);
    }

    // Color (vec4)
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4)));
    glVertexAttribDivisor(7, 1);

    // Emissive (vec3)
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4) + sizeof(glm::vec4)));
    glVertexAttribDivisor(8, 1);

    // Emission strength (float)
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec3)));
    glVertexAttribDivisor(9, 1);

    // UV offset (vec2)
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec4)));
    glVertexAttribDivisor(10, 1);

    // Metallic (float)
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec2)));
    glVertexAttribDivisor(11, 1);

    // Roughness (float)
    glEnableVertexAttribArray(12);
    glVertexAttribPointer(12, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec4) + sizeof(glm::vec2) + sizeof(float)));
    glVertexAttribDivisor(12, 1);

    glBindVertexArray(0);
}

void InstancedMesh::CreateInstanceBuffer()
{
    if (m_InstanceVBO) glDeleteBuffers(1, &m_InstanceVBO);

    glGenBuffers(1, &m_InstanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, m_MaxInstances * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

    std::cout << "[InstancedMesh] Instance buffer created: " << m_MaxInstances
              << " instances, " << (m_MaxInstances * sizeof(InstanceData) / 1024.0 / 1024.0) << " MB" << std::endl;
}
