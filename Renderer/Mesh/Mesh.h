#pragma once

#include "../Vertex/Vertex.h"

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

class Mesh
{
public:
    struct FaceRange
    {
        unsigned int FirstVertex = 0;
        unsigned int VertexCount = 0;
    };

    Mesh();
    ~Mesh();
    // Prevent copying to avoid duplicate GPU resource ownership
    Mesh(const Mesh& other) = delete;
    Mesh& operator=(const Mesh& other) = delete;
    // Allow moving ownership of GPU resources
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    // NOTE: Mesh is copyable in this build; be careful with resource ownership.

    void Create(
        const std::vector<Vertex>& vertices,
        unsigned int primitiveType = 0
        , const char* debugTag = nullptr
    );

    void Draw();
    void DrawRange(unsigned int firstVertex, unsigned int vertexCount);
    const std::vector<FaceRange>& GetFaceRanges() const;

    // Cópia CPU usada pelo modo Modelador. O renderer continua desenhando o
    // VBO normalmente, mas o editor pode reconstruí-lo depois de uma edição.
    const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
    unsigned int GetPrimitiveType() const { return m_PrimitiveType; }
    void UpdateVertices(const std::vector<Vertex>& vertices, const char* debugTag = nullptr)
    {
        Create(vertices, m_PrimitiveType, debugTag);
    }

    bool IsValid() const
    {
        return m_VAO != 0 &&
            m_VBO != 0 &&
            m_VertexCount > 0;
    }

    unsigned int GetVertexCount() const
    {
        return m_VertexCount;
    }

    uint64_t GetRevision() const
    {
        return m_Revision;
    }

    float GetBoundingRadius() const
    {
        return m_BoundingRadius;
    }

    glm::vec3 GetBoundingCenter() const
    {
        return m_BoundingCenter;
    }

    glm::vec3 GetBoundingHalfExtents() const
    {
        return m_BoundingHalfExtents;
    }

private:
    unsigned int m_VAO;
    unsigned int m_VBO;
    unsigned int m_VertexCount;

    unsigned int m_PrimitiveType;
    std::vector<Vertex> m_Vertices;
    float m_BoundingRadius = 0.0f;
    glm::vec3 m_BoundingCenter{ 0.0f };
    glm::vec3 m_BoundingHalfExtents{ 0.5f };
    uint64_t m_Revision = 0;
    mutable uint64_t m_FaceRangesRevision = ~uint64_t{ 0 };
    mutable std::vector<FaceRange> m_FaceRanges;
    // Bytes do VBO já reportados ao GpuMemoryTracker (evita contagem dupla
    // em rebuild/move).
    size_t m_RegisteredGpuBytes = 0;
    void SyncGpuMemoryRegistration();
};
