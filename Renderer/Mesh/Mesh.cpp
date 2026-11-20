#include "Mesh.h"
#include <cstddef>
#include <glad/glad.h>
#include <atomic>
#include <iostream>
#include <algorithm>
#include <array>
#include <unordered_map>
#include "../../Core/Diagnostics/PerformanceMonitor.h"
#include "../../Core/Diagnostics/GpuMemoryTracker.h"
#include <chrono>
#include <cmath>

static std::atomic<int> s_VAOCount{ 0 };
static std::atomic<int> s_VBOCount{ 0 };

namespace
{
    struct QuantizedPoint
    {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const QuantizedPoint& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct FaceKey
    {
        std::array<QuantizedPoint, 3> points{};

        bool operator==(const FaceKey& other) const
        {
            return points == other.points;
        }
    };

    struct FaceKeyHash
    {
        std::size_t operator()(const FaceKey& key) const
        {
            std::size_t hash = 0;
            for (const QuantizedPoint& point : key.points)
            {
                hash ^= static_cast<std::size_t>(point.x) * 73856093u;
                hash ^= static_cast<std::size_t>(point.y) * 19349663u;
                hash ^= static_cast<std::size_t>(point.z) * 83492791u;
                hash = (hash << 5u) | (hash >> (sizeof(std::size_t) * 8u - 5u));
            }
            return hash;
        }
    };

    QuantizedPoint Quantize(const Vertex& vertex)
    {
        constexpr float scale = 100000.0f;
        return {
            static_cast<int>(std::lround(vertex.X * scale)),
            static_cast<int>(std::lround(vertex.Y * scale)),
            static_cast<int>(std::lround(vertex.Z * scale))
        };
    }

    FaceKey MakeFaceKey(const Vertex& a, const Vertex& b, const Vertex& c)
    {
        FaceKey key{ { Quantize(a), Quantize(b), Quantize(c) } };
        std::sort(key.points.begin(), key.points.end(), [](const QuantizedPoint& left,
                                                            const QuantizedPoint& right)
        {
            if (left.x != right.x) return left.x < right.x;
            if (left.y != right.y) return left.y < right.y;
            return left.z < right.z;
        });
        return key;
    }

    glm::vec3 FaceNormal(const Vertex& a, const Vertex& b, const Vertex& c)
    {
        return glm::cross(
            glm::vec3(b.X - a.X, b.Y - a.Y, b.Z - a.Z),
            glm::vec3(c.X - a.X, c.Y - a.Y, c.Z - a.Z));
    }

    bool IsOppositeFace(const Vertex& a, const Vertex& b, const Vertex& c,
                        const Vertex& x, const Vertex& y, const Vertex& z)
    {
        const glm::vec3 first = FaceNormal(a, b, c);
        const glm::vec3 second = FaceNormal(x, y, z);
        const float firstLength = glm::length(first);
        const float secondLength = glm::length(second);
        return firstLength > 0.000001f && secondLength > 0.000001f &&
            glm::dot(first / firstLength, second / secondLength) < -0.999f;
    }

    std::vector<Vertex> RemoveInternalDuplicateFaces(const std::vector<Vertex>& vertices)
    {
        const size_t triangleCount = vertices.size() / 3;
        std::vector<bool> removed(triangleCount, false);
        std::unordered_map<FaceKey, std::vector<size_t>, FaceKeyHash> faces;

        for (size_t triangle = 0; triangle < triangleCount; ++triangle)
        {
            const size_t base = triangle * 3;
            const FaceKey key = MakeFaceKey(vertices[base], vertices[base + 1], vertices[base + 2]);
            auto& matches = faces[key];
            for (const size_t previous : matches)
            {
                const size_t previousBase = previous * 3;
                if (IsOppositeFace(vertices[previousBase], vertices[previousBase + 1], vertices[previousBase + 2],
                                   vertices[base], vertices[base + 1], vertices[base + 2]))
                {
                    removed[previous] = true;
                    removed[triangle] = true;
                    break;
                }
            }
            matches.push_back(triangle);
        }

        std::vector<Vertex> cleaned;
        cleaned.reserve(vertices.size());
        for (size_t triangle = 0; triangle < triangleCount; ++triangle)
        {
            if (removed[triangle])
                continue;
            const size_t base = triangle * 3;
            cleaned.push_back(vertices[base]);
            cleaned.push_back(vertices[base + 1]);
            cleaned.push_back(vertices[base + 2]);
        }
        if (vertices.size() % 3 != 0)
            cleaned.insert(cleaned.end(), vertices.end() - (vertices.size() % 3), vertices.end());
        return cleaned;
    }
}

Mesh::Mesh()
    : m_VAO(0),
    m_VBO(0),
    m_VertexCount(0),
    m_PrimitiveType(GL_TRIANGLES)
{
}

// Copy operations are deleted in the header to avoid shallow copying GPU handles.

// Note: move operations removed to match header (Mesh is non-copyable in this build)
Mesh::Mesh(Mesh&& other) noexcept
    : m_VAO(other.m_VAO),
      m_VBO(other.m_VBO),
      m_VertexCount(other.m_VertexCount),
      m_PrimitiveType(other.m_PrimitiveType),
      m_Vertices(std::move(other.m_Vertices)),
      m_BoundingRadius(other.m_BoundingRadius),
      m_BoundingCenter(other.m_BoundingCenter),
      m_BoundingHalfExtents(other.m_BoundingHalfExtents),
      m_Revision(other.m_Revision),
      m_RegisteredGpuBytes(other.m_RegisteredGpuBytes)
{
    // Transfer ownership of GPU handles; the counters remain unchanged because
    // the actual GPU resources are not created or destroyed by a move.
    other.m_VAO = 0;
    other.m_VBO = 0;
    other.m_VertexCount = 0;
    other.m_PrimitiveType = 0;
    other.m_BoundingRadius = 0.0f;
    other.m_BoundingCenter = glm::vec3(0.0f);
    other.m_BoundingHalfExtents = glm::vec3(0.5f);
    other.m_Revision = 0;
    other.m_RegisteredGpuBytes = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        // Destroy current resources (if any) before taking ownership of other's.
        if (m_VBO != 0)
        {
            glDeleteBuffers(1, &m_VBO);
            --s_VBOCount;
            m_VBO = 0;
        }
        GpuMemoryTracker::Remove(static_cast<int64_t>(m_RegisteredGpuBytes));
        m_RegisteredGpuBytes = 0;

        if (m_VAO != 0)
        {
            glDeleteVertexArrays(1, &m_VAO);
            --s_VAOCount;
            m_VAO = 0;
        }

        // Steal resources from other
        m_VAO = other.m_VAO;
        m_VBO = other.m_VBO;
        m_VertexCount = other.m_VertexCount;
        m_PrimitiveType = other.m_PrimitiveType;
        m_Vertices = std::move(other.m_Vertices);
        m_BoundingRadius = other.m_BoundingRadius;
        m_BoundingCenter = other.m_BoundingCenter;
        m_BoundingHalfExtents = other.m_BoundingHalfExtents;
        m_Revision = other.m_Revision;
        m_RegisteredGpuBytes = other.m_RegisteredGpuBytes;
        m_FaceRangesRevision = ~uint64_t{ 0 };
        m_FaceRanges.clear();

        // Leave other in a safe-to-destroy state
        other.m_VAO = 0;
        other.m_VBO = 0;
        other.m_VertexCount = 0;
        other.m_PrimitiveType = 0;
        other.m_BoundingRadius = 0.0f;
        other.m_BoundingCenter = glm::vec3(0.0f);
        other.m_BoundingHalfExtents = glm::vec3(0.5f);
        other.m_Revision = 0;
        other.m_RegisteredGpuBytes = 0;
    }
    return *this;

}


Mesh::~Mesh()
{
    GpuMemoryTracker::Remove(static_cast<int64_t>(m_RegisteredGpuBytes));
    m_RegisteredGpuBytes = 0;

    if (m_VBO != 0)
    {
        glDeleteBuffers(1, &m_VBO);
        --s_VBOCount;
        m_VBO = 0;
    }

    if (m_VAO != 0)
    {
        glDeleteVertexArrays(1, &m_VAO);
        --s_VAOCount;
        m_VAO = 0;
    }
}


void Mesh::Draw()
{
    glBindVertexArray(m_VAO);

    glDrawArrays(
        m_PrimitiveType,
        0,
        m_VertexCount
    );
}

void Mesh::DrawRange(unsigned int firstVertex, unsigned int vertexCount)
{
    if (m_VAO == 0 || vertexCount == 0 || firstVertex >= m_VertexCount)
        return;
    const unsigned int safeCount = std::min(vertexCount, m_VertexCount - firstVertex);
    glBindVertexArray(m_VAO);
    glDrawArrays(m_PrimitiveType, static_cast<GLint>(firstVertex), static_cast<GLsizei>(safeCount));
}

const std::vector<Mesh::FaceRange>& Mesh::GetFaceRanges() const
{
    if (m_FaceRangesRevision == m_Revision)
        return m_FaceRanges;

    m_FaceRanges.clear();
    m_FaceRangesRevision = m_Revision;
    if (m_PrimitiveType != GL_TRIANGLES || m_Vertices.size() < 3)
        return m_FaceRanges;

    constexpr float coplanarThreshold = 0.9995f;
    glm::vec3 previousNormal(0.0f);
    float previousPlane = 0.0f;
    unsigned int rangeStart = 0;
    unsigned int rangeCount = 0;
    for (size_t i = 0; i + 2 < m_Vertices.size(); i += 3)
    {
        const Vertex& a = m_Vertices[i];
        const Vertex& b = m_Vertices[i + 1];
        const Vertex& c = m_Vertices[i + 2];
        const glm::vec3 p0(a.X, a.Y, a.Z);
        const glm::vec3 p1(b.X, b.Y, b.Z);
        const glm::vec3 p2(c.X, c.Y, c.Z);
        const glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
        const float length = glm::length(cross);
        const glm::vec3 normal = length > 1e-8f ? cross / length : glm::vec3(0.0f);
        const float plane = glm::dot(normal, p0);
        const bool sameFace = rangeCount > 0 && glm::dot(previousNormal, normal) >= coplanarThreshold &&
            std::abs(previousPlane - plane) <= 1e-4f;
        if (!sameFace && rangeCount > 0)
        {
            m_FaceRanges.push_back({ rangeStart, rangeCount });
            rangeCount = 0;
        }
        if (rangeCount == 0)
        {
            rangeStart = static_cast<unsigned int>(i);
            previousNormal = normal;
            previousPlane = plane;
        }
        rangeCount += 3;
    }
    if (rangeCount > 0)
        m_FaceRanges.push_back({ rangeStart, rangeCount });
    return m_FaceRanges;
}

void Mesh::Create(
    const std::vector<Vertex>& vertices,
    unsigned int primitiveType,
    const char* debugTag
)
{
    auto meshCreateStart =
        std::chrono::high_resolution_clock::now();

    // Se já existe um mesh, destrói os recursos antigos
    if (m_VBO != 0)
    {
        glDeleteBuffers(1, &m_VBO);
        --s_VBOCount;
        m_VBO = 0;
    }

    if (m_VAO != 0)
    {
        glDeleteVertexArrays(1, &m_VAO);
        --s_VAOCount;
        m_VAO = 0;
    }

    m_PrimitiveType =
        primitiveType == 0
        ? GL_TRIANGLES
        : primitiveType;

    const std::vector<Vertex> cleanedVertices = RemoveInternalDuplicateFaces(vertices);

    m_VertexCount =
        static_cast<unsigned int>(
            cleanedVertices.size()
            );
    m_Vertices = cleanedVertices;
    ++m_Revision;
    m_FaceRangesRevision = ~uint64_t{ 0 };
    m_FaceRanges.clear();
    if (!cleanedVertices.empty())
    {
        glm::vec3 minimum(cleanedVertices.front().X, cleanedVertices.front().Y, cleanedVertices.front().Z);
        glm::vec3 maximum = minimum;
        for (const Vertex& vertex : cleanedVertices)
        {
            const glm::vec3 position(vertex.X, vertex.Y, vertex.Z);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        m_BoundingCenter = (minimum + maximum) * 0.5f;
        m_BoundingHalfExtents = (maximum - minimum) * 0.5f;
    }
    else
    {
        m_BoundingCenter = glm::vec3(0.0f);
        m_BoundingHalfExtents = glm::vec3(0.5f);
    }
    m_BoundingRadius = 0.0f;
    for (const Vertex& vertex : cleanedVertices)
    {
        const glm::vec3 position(vertex.X, vertex.Y, vertex.Z);
        m_BoundingRadius = std::max(
            m_BoundingRadius, glm::length(position - m_BoundingCenter));
    }

    // Assets e prefabs também podem ser lidos por testes, importadores e
    // ferramentas antes da criação do contexto OpenGL. Nesse estágio os
    // dados CPU continuam válidos; a parte GPU será criada quando o renderer
    // reconstruir o mesh com um contexto disponível.
    if (glGenVertexArrays == nullptr || glGenBuffers == nullptr)
    {
        SyncGpuMemoryRegistration();
        return;
    }

    glGenVertexArrays(
        1,
        &m_VAO
    );

    glGenBuffers(
        1,
        &m_VBO
    );

    ++s_VAOCount;
    ++s_VBOCount;

    glBindVertexArray(m_VAO);

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_VBO
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        cleanedVertices.size() * sizeof(Vertex),
        cleanedVertices.data(),
        GL_STATIC_DRAW
    );

    // Position
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(Vertex, X)
    );

    glEnableVertexAttribArray(0);

    // Normal
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(Vertex, NX)
    );

    glEnableVertexAttribArray(1);

    // UV
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (void*)offsetof(Vertex, U)
    );

    glEnableVertexAttribArray(2);

    // Four weighted bone influences used by imported glTF/FBX meshes.
    glVertexAttribIPointer(
        3,
        4,
        GL_UNSIGNED_SHORT,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, JointIndices))
    );
    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        4,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, JointWeights))
    );
    glEnableVertexAttribArray(4);

    glBindBuffer(
        GL_ARRAY_BUFFER,
        0
    );

    glBindVertexArray(0);

    auto meshCreateEnd =
        std::chrono::high_resolution_clock::now();

    double meshCreateTime =
        std::chrono::duration<double, std::milli>(
            meshCreateEnd - meshCreateStart
        ).count();

    PerformanceMonitor::SetLastMeshCreateTimeMs(
        meshCreateTime
    );

    SyncGpuMemoryRegistration();
}

void Mesh::SyncGpuMemoryRegistration()
{
    // O VBO contém exatamente m_Vertices (o mesmo cleanedVertices do upload).
    const size_t desired = (m_VBO != 0)
        ? m_Vertices.size() * sizeof(Vertex)
        : 0;
    if (desired == m_RegisteredGpuBytes)
        return;
    if (desired > m_RegisteredGpuBytes)
        GpuMemoryTracker::Add(static_cast<int64_t>(desired - m_RegisteredGpuBytes));
    else
        GpuMemoryTracker::Remove(static_cast<int64_t>(m_RegisteredGpuBytes - desired));
    m_RegisteredGpuBytes = desired;
}
