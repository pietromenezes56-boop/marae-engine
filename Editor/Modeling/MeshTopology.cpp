#include "MeshTopology.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

namespace
{
    struct PositionKey
    {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const PositionKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct PositionKeyHash
    {
        size_t operator()(const PositionKey& key) const
        {
            size_t result = static_cast<size_t>(key.x) * 73856093u;
            result ^= static_cast<size_t>(key.y) * 19349663u;
            result ^= static_cast<size_t>(key.z) * 83492791u;
            return result;
        }
    };

    PositionKey MakeKey(const Vertex& vertex, float tolerance)
    {
        const float scale = 1.0f / std::max(tolerance, 0.0000001f);
        return {
            static_cast<int>(std::lround(vertex.X * scale)),
            static_cast<int>(std::lround(vertex.Y * scale)),
            static_cast<int>(std::lround(vertex.Z * scale))
        };
    }

    glm::vec3 Position(const MeshTopology::VertexData& vertex)
    {
        return glm::vec3(vertex.Value.X, vertex.Value.Y, vertex.Value.Z);
    }

    glm::vec3 FaceNormal(const MeshTopology& mesh, const MeshTopology::Face& face)
    {
        const glm::vec3 a = Position(mesh.Vertices[face.Vertices[0]]);
        const glm::vec3 b = Position(mesh.Vertices[face.Vertices[1]]);
        const glm::vec3 c = Position(mesh.Vertices[face.Vertices[2]]);
        const glm::vec3 normal = glm::cross(b - a, c - a);
        return glm::length(normal) > 0.000001f
            ? glm::normalize(normal)
            : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

MeshTopology MeshTopology::FromVertices(const std::vector<Vertex>& vertices,
                                        float positionTolerance)
{
    MeshTopology result;
    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> lookup;

    auto getIndex = [&](const Vertex& vertex)
    {
        const PositionKey key = MakeKey(vertex, positionTolerance);
        const auto found = lookup.find(key);
        if (found != lookup.end())
            return found->second;

        const uint32_t index = static_cast<uint32_t>(result.Vertices.size());
        result.Vertices.push_back({ vertex });
        lookup.emplace(key, index);
        return index;
    };

    for (size_t i = 0; i + 2 < vertices.size(); i += 3)
    {
        const uint32_t a = getIndex(vertices[i]);
        const uint32_t b = getIndex(vertices[i + 1]);
        const uint32_t c = getIndex(vertices[i + 2]);
        if (a != b && b != c && c != a)
            result.Faces.push_back({ { a, b, c } });
    }

    result.RecalculateNormals(false);
    return result;
}

std::vector<Vertex> MeshTopology::ToVertices(bool smoothShading) const
{
    std::vector<Vertex> result;
    result.reserve(Faces.size() * 3);
    if (smoothShading)
    {
        MeshTopology copy = *this;
        copy.RecalculateNormals(true);
        return copy.ToVertices(false);
    }

    for (const Face& face : Faces)
    {
        result.push_back(Vertices[face.Vertices[0]].Value);
        result.push_back(Vertices[face.Vertices[1]].Value);
        result.push_back(Vertices[face.Vertices[2]].Value);
    }
    return result;
}

std::vector<MeshTopology::Edge> MeshTopology::GetEdges() const
{
    struct EdgeKey
    {
        uint32_t A = 0;
        uint32_t B = 0;
        bool operator==(const EdgeKey& other) const { return A == other.A && B == other.B; }
    };
    struct EdgeKeyHash
    {
        size_t operator()(const EdgeKey& key) const
        {
            return (static_cast<size_t>(key.A) * 16777619u) ^ key.B;
        }
    };

    std::vector<Edge> result;
    std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> lookup;
    for (uint32_t faceIndex = 0; faceIndex < Faces.size(); ++faceIndex)
    {
        const auto& indices = Faces[faceIndex].Vertices;
        for (int edge = 0; edge < 3; ++edge)
        {
            uint32_t a = indices[edge];
            uint32_t b = indices[(edge + 1) % 3];
            if (a > b) std::swap(a, b);
            const EdgeKey key{ a, b };
            auto found = lookup.find(key);
            if (found == lookup.end())
            {
                const uint32_t edgeIndex = static_cast<uint32_t>(result.size());
                lookup.emplace(key, edgeIndex);
                result.push_back({ a, b, { faceIndex } });
            }
            else
            {
                result[found->second].Faces.push_back(faceIndex);
            }
        }
    }
    return result;
}

std::vector<uint32_t> MeshTopology::GetBoundaryEdges() const
{
    const auto edges = GetEdges();
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < edges.size(); ++i)
        if (edges[i].Faces.size() == 1)
            result.push_back(i);
    return result;
}

void MeshTopology::RecalculateNormals(bool smoothShading)
{
    if (Vertices.empty() || Faces.empty())
        return;

    std::vector<glm::vec3> normals(Vertices.size(), glm::vec3(0.0f));
    for (const Face& face : Faces)
    {
        const glm::vec3 normal = FaceNormal(*this, face);
        if (smoothShading)
        {
            for (uint32_t index : face.Vertices)
                normals[index] += normal;
        }
        else
        {
            for (uint32_t index : face.Vertices)
                normals[index] = normal;
        }
    }

    for (size_t i = 0; i < Vertices.size(); ++i)
    {
        const glm::vec3 normal = glm::length(normals[i]) > 0.000001f
            ? glm::normalize(normals[i])
            : glm::vec3(0.0f, 1.0f, 0.0f);
        Vertices[i].Value.NX = normal.x;
        Vertices[i].Value.NY = normal.y;
        Vertices[i].Value.NZ = normal.z;
    }
}

void MeshTopology::RemoveDegenerate(float epsilon)
{
    const float minimumArea = std::max(0.0f, epsilon);
    Faces.erase(std::remove_if(Faces.begin(), Faces.end(), [&](const Face& face)
    {
        const glm::vec3 a = Position(Vertices[face.Vertices[0]]);
        const glm::vec3 b = Position(Vertices[face.Vertices[1]]);
        const glm::vec3 c = Position(Vertices[face.Vertices[2]]);
        return glm::dot(glm::cross(b - a, c - a), glm::cross(b - a, c - a)) <= minimumArea;
    }), Faces.end());
}

bool MeshTopology::IsValid() const
{
    if (Vertices.empty() || Faces.empty())
        return false;
    for (const Face& face : Faces)
        for (uint32_t index : face.Vertices)
            if (index >= Vertices.size())
                return false;
    return true;
}
