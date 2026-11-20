#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <glad/glad.h>

// InstancedMesh - Renderiza múltiplas cópias do mesmo mesh em um draw call
// Reduz drásticamente o número de draw calls para tiles e objetos repetidos
class InstancedMesh
{
public:
    struct InstanceData
    {
        glm::mat4 model;        // Transform matrix
        glm::vec4 color;        // Color (r,g,b,a)
        glm::vec3 emissive;     // Emissive color
        float emissionStrength; // Emission strength
        glm::vec2 uvOffset;     // UV offset for texture atlas
        float metallic;         // Metallic property
        float roughness;        // Roughness property
    };

    InstancedMesh();
    ~InstancedMesh();

    // Cria o mesh base (VBO com vértices)
    void CreateBaseMesh(const float* vertices, size_t vertexCount, size_t vertexStride,
                        const unsigned int* indices, size_t indexCount);

    // Define capacidade máxima de instâncias
    void SetMaxInstances(size_t maxInstances);

    // Atualiza os dados das instâncias
    void UpdateInstances(const std::vector<InstanceData>& instances);

    // Renderiza todas as instâncias em um draw call
    void Draw();

    // Limpa recursos
    void Clear();

    // Estatísticas
    size_t GetInstanceCount() const { return m_InstanceCount; }
    size_t GetMaxInstances() const { return m_MaxInstances; }
    bool IsValid() const { return m_VAO != 0 && m_VBO != 0; }

private:
    void CreateVAO();
    void CreateInstanceBuffer();

    GLuint m_VAO = 0;
    GLuint m_VBO = 0;           // Vertex buffer (mesh base)
    GLuint m_IBO = 0;           // Index buffer
    GLuint m_InstanceVBO = 0;    // Instance buffer (transforms, colors, etc.)

    size_t m_VertexCount = 0;
    size_t m_IndexCount = 0;
    size_t m_InstanceCount = 0;
    size_t m_MaxInstances = 0;

    bool m_UsingIndices = false;
};
