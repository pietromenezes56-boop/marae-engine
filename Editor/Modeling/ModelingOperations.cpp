#include "ModelingOperations.h"
#include "MeshTopology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

namespace
{
    constexpr size_t kMaxGeneratedVertices = 250000;

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
        std::size_t operator()(const PositionKey& key) const
        {
            std::size_t value = static_cast<std::size_t>(key.x) * 73856093u;
            value ^= static_cast<std::size_t>(key.y) * 19349663u;
            value ^= static_cast<std::size_t>(key.z) * 83492791u;
            return value;
        }
    };

    PositionKey MakeKey(const Vertex& vertex)
    {
        constexpr float scale = 100000.0f;
        return {
            static_cast<int>(std::lround(vertex.X * scale)),
            static_cast<int>(std::lround(vertex.Y * scale)),
            static_cast<int>(std::lround(vertex.Z * scale))
        };
    }

    Vertex Midpoint(const Vertex& a, const Vertex& b)
    {
        return Vertex(
            (a.X + b.X) * 0.5f,
            (a.Y + b.Y) * 0.5f,
            (a.Z + b.Z) * 0.5f,
            (a.NX + b.NX) * 0.5f,
            (a.NY + b.NY) * 0.5f,
            (a.NZ + b.NZ) * 0.5f,
            (a.U + b.U) * 0.5f,
            (a.V + b.V) * 0.5f);
    }

    void PushTriangle(std::vector<Vertex>& result,
                      const Vertex& a,
                      const Vertex& b,
                      const Vertex& c)
    {
        result.push_back(a);
        result.push_back(b);
        result.push_back(c);
    }

    void Normalize(Vertex& vertex)
    {
        const float length = std::sqrt(
            vertex.NX * vertex.NX +
            vertex.NY * vertex.NY +
            vertex.NZ * vertex.NZ);
        if (length > 0.00001f)
        {
            vertex.NX /= length;
            vertex.NY /= length;
            vertex.NZ /= length;
        }
    }
}

namespace ModelingOperations
{
    namespace
    {
        float TriangleAreaSquared(const Vertex& a, const Vertex& b, const Vertex& c)
        {
            const float abX = b.X - a.X;
            const float abY = b.Y - a.Y;
            const float abZ = b.Z - a.Z;
            const float acX = c.X - a.X;
            const float acY = c.Y - a.Y;
            const float acZ = c.Z - a.Z;
            const float nx = abY * acZ - abZ * acY;
            const float ny = abZ * acX - abX * acZ;
            const float nz = abX * acY - abY * acX;
            return nx * nx + ny * ny + nz * nz;
        }

        struct FaceKey
        {
            PositionKey A;
            PositionKey B;
            PositionKey C;

            bool operator==(const FaceKey& other) const
            {
                return A == other.A && B == other.B && C == other.C;
            }
        };

        struct FaceKeyHash
        {
            std::size_t operator()(const FaceKey& key) const
            {
                PositionKeyHash hash;
                return hash(key.A) ^ (hash(key.B) << 1u) ^ (hash(key.C) << 2u);
            }
        };

        FaceKey MakeFaceKey(const Vertex& a, const Vertex& b, const Vertex& c)
        {
            std::array<PositionKey, 3> keys = { MakeKey(a), MakeKey(b), MakeKey(c) };
            std::sort(keys.begin(), keys.end(), [](const PositionKey& left, const PositionKey& right)
            {
                if (left.x != right.x) return left.x < right.x;
                if (left.y != right.y) return left.y < right.y;
                return left.z < right.z;
            });
            return { keys[0], keys[1], keys[2] };
        }

        bool RayIntersectsTriangle(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                   float& outT)
        {
            constexpr float kEpsilon = 1e-6f;
            const glm::vec3 edge1 = v1 - v0;
            const glm::vec3 edge2 = v2 - v0;
            const glm::vec3 h = glm::cross(rayDir, edge2);
            const float a = glm::dot(edge1, h);
            if (a > -kEpsilon && a < kEpsilon)
                return false;

            const float f = 1.0f / a;
            const glm::vec3 s = rayOrigin - v0;
            const float u = f * glm::dot(s, h);
            if (u < 0.0f || u > 1.0f)
                return false;

            const glm::vec3 q = glm::cross(s, edge1);
            const float v = f * glm::dot(rayDir, q);
            if (v < 0.0f || u + v > 1.0f)
                return false;

            const float t = f * glm::dot(edge2, q);
            if (t > kEpsilon)
            {
                outT = t;
                return true;
            }
            return false;
        }
    }

    void RemoveDegenerate(std::vector<Vertex>& vertices, float epsilon)
    {
        const float minimumArea = std::max(0.0f, epsilon);
        std::vector<Vertex> result;
        result.reserve(vertices.size());
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            if (TriangleAreaSquared(vertices[i], vertices[i + 1], vertices[i + 2]) > minimumArea)
            {
                PushTriangle(result, vertices[i], vertices[i + 1], vertices[i + 2]);
            }
        }
        vertices.swap(result);
    }

    void RemoveInternalFaces(std::vector<Vertex>& vertices)
    {
        if (vertices.size() < 6)
            return;

        RemoveDegenerate(vertices, 0.000001f);
        const size_t faceCount = vertices.size() / 3;
        if (faceCount == 0)
            return;

        std::vector<bool> isInternal(faceCount, false);

        // 1. Detectar faces coincidentes (back-to-back e duplicadas)
        std::unordered_map<FaceKey, std::vector<size_t>, FaceKeyHash> faceMap;
        std::vector<glm::vec3> faceNormals(faceCount);
        std::vector<glm::vec3> faceCenters(faceCount);

        for (size_t f = 0; f < faceCount; ++f)
        {
            const Vertex& v0 = vertices[f * 3];
            const Vertex& v1 = vertices[f * 3 + 1];
            const Vertex& v2 = vertices[f * 3 + 2];

            const glm::vec3 a(v0.X, v0.Y, v0.Z);
            const glm::vec3 b(v1.X, v1.Y, v1.Z);
            const glm::vec3 c(v2.X, v2.Y, v2.Z);

            faceCenters[f] = (a + b + c) / 3.0f;
            const glm::vec3 n = glm::cross(b - a, c - a);
            const float len = glm::length(n);
            faceNormals[f] = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);

            FaceKey key = MakeFaceKey(v0, v1, v2);
            faceMap[key].push_back(f);
        }

        // Se duas faces compartilham a mesma posição e têm normais opostas,
        // são divisórias internas (ex: blocos contíguos unidos ou extrusão com paredes internas).
        // Ambas as faces devem ser removidas para abrir o interior e economizar triângulos.
        for (const auto& [key, faces] : faceMap)
        {
            if (faces.size() >= 2)
            {
                for (size_t i = 0; i < faces.size(); ++i)
                {
                    for (size_t j = i + 1; j < faces.size(); ++j)
                    {
                        size_t f1 = faces[i];
                        size_t f2 = faces[j];
                        float dotProd = glm::dot(faceNormals[f1], faceNormals[f2]);
                        if (dotProd < -0.2f)
                        {
                            isInternal[f1] = true;
                            isInternal[f2] = true;
                        }
                        else if (dotProd > 0.8f)
                        {
                            isInternal[f2] = true;
                        }
                    }
                }
            }
        }

        // 2. Teste volumétrico por Ray-casting para detectar faces isoladas ou presas no interior
        if (faceCount <= 8000)
        {
            const glm::vec3 rayDir = glm::normalize(glm::vec3(0.57735f, 0.7071f, 0.4082f));
            for (size_t f = 0; f < faceCount; ++f)
            {
                if (isInternal[f])
                    continue;

                const glm::vec3& center = faceCenters[f];
                const glm::vec3& normal = faceNormals[f];

                const glm::vec3 p1 = center + normal * 0.002f;
                const glm::vec3 p2 = center - normal * 0.002f;

                int hits1 = 0;
                int hits2 = 0;
                float outT = 0.0f;

                for (size_t other = 0; other < faceCount; ++other)
                {
                    if (other == f || isInternal[other])
                        continue;

                    const glm::vec3 ov0(vertices[other * 3].X, vertices[other * 3].Y, vertices[other * 3].Z);
                    const glm::vec3 ov1(vertices[other * 3 + 1].X, vertices[other * 3 + 1].Y, vertices[other * 3 + 1].Z);
                    const glm::vec3 ov2(vertices[other * 3 + 2].X, vertices[other * 3 + 2].Y, vertices[other * 3 + 2].Z);

                    if (RayIntersectsTriangle(p1, rayDir, ov0, ov1, ov2, outT) && outT > 1e-4f)
                        ++hits1;
                    if (RayIntersectsTriangle(p2, rayDir, ov0, ov1, ov2, outT) && outT > 1e-4f)
                        ++hits2;
                }

                // Se ambos os lados estão dentro do volume fechado (hits ímpares), a face é interna
                if ((hits1 % 2 == 1) && (hits2 % 2 == 1))
                {
                    isInternal[f] = true;
                }
            }
        }

        std::vector<Vertex> result;
        result.reserve(vertices.size());
        for (size_t f = 0; f < faceCount; ++f)
        {
            if (!isInternal[f])
            {
                PushTriangle(result, vertices[f * 3], vertices[f * 3 + 1], vertices[f * 3 + 2]);
            }
        }

        vertices = std::move(result);
        RecalculateNormals(vertices);
    }

    void Optimize(std::vector<Vertex>& vertices, float positionTolerance)
    {
        (void)positionTolerance; // MakeKey usa tolerância fixa para estabilidade.

        RemoveDegenerate(vertices, 0.000001f);
        RemoveInternalFaces(vertices);
        RecalculateNormals(vertices);
    }

    void RecalculateNormals(std::vector<Vertex>& vertices)
    {
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            glm::vec3 a(vertices[i].X, vertices[i].Y, vertices[i].Z);
            glm::vec3 b(vertices[i + 1].X, vertices[i + 1].Y, vertices[i + 1].Z);
            glm::vec3 c(vertices[i + 2].X, vertices[i + 2].Y, vertices[i + 2].Z);
            glm::vec3 geometric = glm::cross(b - a, c - a);
            const float geometricLength = glm::length(geometric);

            glm::vec3 authored(
                vertices[i].NX + vertices[i + 1].NX + vertices[i + 2].NX,
                vertices[i].NY + vertices[i + 1].NY + vertices[i + 2].NY,
                vertices[i].NZ + vertices[i + 1].NZ + vertices[i + 2].NZ);
            const float authoredLength = glm::length(authored);

            if (geometricLength > 0.00001f)
            {
                geometric = glm::normalize(geometric);
            }
            else if (authoredLength > 0.00001f)
            {
                geometric = authored / authoredLength;
            }
            else
            {
                geometric = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            for (size_t j = i; j < i + 3; ++j)
            {
                vertices[j].NX = geometric.x;
                vertices[j].NY = geometric.y;
                vertices[j].NZ = geometric.z;
            }
        }
    }

    void FlipNormals(std::vector<Vertex>& vertices)
    {
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            std::swap(vertices[i + 1], vertices[i + 2]);
            for (size_t j = i; j < i + 3; ++j)
            {
                vertices[j].NX = -vertices[j].NX;
                vertices[j].NY = -vertices[j].NY;
                vertices[j].NZ = -vertices[j].NZ;
            }
        }
    }

    void Smooth(std::vector<Vertex>& vertices, float strength)
    {
        strength = std::clamp(strength, 0.0f, 1.0f);
        if (vertices.empty()) return;

        std::unordered_map<PositionKey, Vertex, PositionKeyHash> average;
        std::unordered_map<PositionKey, int, PositionKeyHash> counts;
        for (const Vertex& vertex : vertices)
        {
            const PositionKey key = MakeKey(vertex);
            Vertex& sum = average[key];
            sum.NX += vertex.NX;
            sum.NY += vertex.NY;
            sum.NZ += vertex.NZ;
            counts[key]++;
        }

        for (Vertex& vertex : vertices)
        {
            const PositionKey key = MakeKey(vertex);
            const Vertex& sum = average[key];
            const float count = static_cast<float>(counts[key]);
            const float nx = sum.NX / count;
            const float ny = sum.NY / count;
            const float nz = sum.NZ / count;
            vertex.NX = vertex.NX * (1.0f - strength) + nx * strength;
            vertex.NY = vertex.NY * (1.0f - strength) + ny * strength;
            vertex.NZ = vertex.NZ * (1.0f - strength) + nz * strength;
            Normalize(vertex);
        }
    }

    void Inflate(std::vector<Vertex>& vertices, float distance)
    {
        if (vertices.empty() || std::abs(distance) < 0.000001f)
            return;

        distance = std::clamp(distance, -100.0f, 100.0f);
        for (Vertex& vertex : vertices)
        {
            const float length = std::sqrt(
                vertex.NX * vertex.NX + vertex.NY * vertex.NY + vertex.NZ * vertex.NZ);
            if (length <= 0.00001f)
                continue;
            vertex.X += (vertex.NX / length) * distance;
            vertex.Y += (vertex.NY / length) * distance;
            vertex.Z += (vertex.NZ / length) * distance;
        }
        RecalculateNormals(vertices);
    }

    std::vector<Vertex> IncreaseResolution(const std::vector<Vertex>& vertices, int levels)
    {
        levels = std::clamp(levels, 1, 3);
        if (vertices.empty() || vertices.size() % 3 != 0)
            return {};

        std::vector<Vertex> result = vertices;
        bool subdivided = false;
        for (int level = 0; level < levels; ++level)
        {
            if (result.size() > kMaxGeneratedVertices / 4)
                break;
            std::vector<Vertex> next = Subdivide(result);
            if (next.empty() || next.size() > kMaxGeneratedVertices)
                break;
            result = std::move(next);
            subdivided = true;
        }
        return subdivided ? result : std::vector<Vertex>{};
    }

    std::vector<Vertex> ReduceResolution(const std::vector<Vertex>& vertices, float strength)
    {
        if (vertices.empty())
            return {};

        strength = std::clamp(strength, 0.01f, 0.75f);
        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
        for (const Vertex& vertex : vertices)
        {
            minPoint = glm::min(minPoint, glm::vec3(vertex.X, vertex.Y, vertex.Z));
            maxPoint = glm::max(maxPoint, glm::vec3(vertex.X, vertex.Y, vertex.Z));
        }

        const float extent = std::max({ maxPoint.x - minPoint.x,
                                        maxPoint.y - minPoint.y,
                                        maxPoint.z - minPoint.z, 0.0001f });
        const float cellSize = extent * strength * 0.12f;
        std::vector<Vertex> quantized = vertices;
        for (Vertex& vertex : quantized)
        {
            auto snap = [cellSize](float value)
            {
                return std::round(value / cellSize) * cellSize;
            };
            vertex.X = snap(vertex.X);
            vertex.Y = snap(vertex.Y);
            vertex.Z = snap(vertex.Z);
        }

        Optimize(quantized);
        return quantized;
    }

    std::vector<Vertex> RebuildRadialResolution(const std::vector<Vertex>& vertices, int sectors)
    {
        sectors = std::clamp(sectors, 3, 128);
        if (vertices.size() < 6)
            return vertices;

        struct Ring { float y = 0.0f; float radius = 0.0f; int count = 0; };
        std::vector<Ring> rings;
        float centerX = 0.0f;
        float centerZ = 0.0f;
        for (const Vertex& vertex : vertices)
        {
            centerX += vertex.X;
            centerZ += vertex.Z;
        }
        centerX /= static_cast<float>(vertices.size());
        centerZ /= static_cast<float>(vertices.size());

        constexpr float ringTolerance = 0.0001f;
        for (const Vertex& vertex : vertices)
        {
            auto ring = std::find_if(rings.begin(), rings.end(), [&](const Ring& candidate)
            {
                return std::abs(candidate.y - vertex.Y) <= ringTolerance;
            });
            if (ring == rings.end())
            {
                const float dx = vertex.X - centerX;
                const float dz = vertex.Z - centerZ;
                const float radialDistance = std::sqrt(dx * dx + dz * dz);
                rings.push_back({ vertex.Y,
                                  radialDistance > 0.0001f ? radialDistance : 0.0f,
                                  radialDistance > 0.0001f ? 1 : 0 });
            }
            else
            {
                const float dx = vertex.X - centerX;
                const float dz = vertex.Z - centerZ;
                const float radialDistance = std::sqrt(dx * dx + dz * dz);
                if (radialDistance > 0.0001f)
                {
                    ring->radius += radialDistance;
                    ++ring->count;
                }
            }
        }
        for (Ring& ring : rings)
            if (ring.count > 0) ring.radius /= static_cast<float>(ring.count);
        std::sort(rings.begin(), rings.end(), [](const Ring& a, const Ring& b) { return a.y < b.y; });
        if (rings.size() < 2)
            return vertices;

        const float pi = 3.14159265358979323846f;
        const float step = 2.0f * pi / static_cast<float>(sectors);
        std::vector<Vertex> result;
        result.reserve(static_cast<size_t>(sectors) * (rings.size() - 1) * 6);
        auto appendTriangle = [&](const glm::vec3& a, const glm::vec2& uvA,
                                  const glm::vec3& b, const glm::vec2& uvB,
                                  const glm::vec3& c, const glm::vec2& uvC)
        {
            glm::vec3 normal = glm::cross(b - a, c - a);
            const float normalLength = glm::length(normal);
            normal = normalLength > 0.000001f
                ? normal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
            result.emplace_back(a.x, a.y, a.z, normal.x, normal.y, normal.z, uvA.x, uvA.y);
            result.emplace_back(b.x, b.y, b.z, normal.x, normal.y, normal.z, uvB.x, uvB.y);
            result.emplace_back(c.x, c.y, c.z, normal.x, normal.y, normal.z, uvC.x, uvC.y);
        };
        auto point = [&](const Ring& ring, int sector) -> glm::vec3
        {
            const float angle = sector * step;
            return { centerX + std::cos(angle) * ring.radius,
                     ring.y, centerZ + std::sin(angle) * ring.radius };
        };
        for (size_t r = 0; r + 1 < rings.size(); ++r)
        {
            for (int sector = 0; sector < sectors; ++sector)
            {
                const glm::vec3 a = point(rings[r], sector);
                const glm::vec3 b = point(rings[r], (sector + 1) % sectors);
                const glm::vec3 c = point(rings[r + 1], (sector + 1) % sectors);
                const glm::vec3 d = point(rings[r + 1], sector);
                if (rings[r].radius <= 0.0001f)
                {
                    appendTriangle(a, { 0.0f, 0.0f }, d, { 0.0f, 1.0f },
                                   c, { 1.0f, 1.0f });
                    continue;
                }
                if (rings[r + 1].radius <= 0.0001f)
                {
                    appendTriangle(a, { 0.0f, 0.0f }, c, { 1.0f, 1.0f },
                                   b, { 1.0f, 0.0f });
                    continue;
                }
                const float u0 = static_cast<float>(sector) / static_cast<float>(sectors);
                const float u1 = static_cast<float>(sector + 1) / static_cast<float>(sectors);
                appendTriangle(a, { u0, 0.0f }, c, { u1, 1.0f }, b, { u1, 0.0f });
                appendTriangle(a, { u0, 0.0f }, d, { u0, 1.0f }, c, { u1, 1.0f });
            }
        }
        if (rings.front().radius > 0.0001f)
        {
            for (int sector = 0; sector < sectors; ++sector)
            {
                const glm::vec3 a = point(rings.front(), sector);
                const glm::vec3 b = point(rings.front(), (sector + 1) % sectors);
                appendTriangle({ centerX, rings.front().y, centerZ }, { 0.5f, 0.5f },
                               a, { 1.0f, 0.0f }, b, { 0.0f, 0.0f });
            }
        }
        if (rings.back().radius > 0.0001f)
        {
            for (int sector = 0; sector < sectors; ++sector)
            {
                const glm::vec3 a = point(rings.back(), sector);
                const glm::vec3 b = point(rings.back(), (sector + 1) % sectors);
                appendTriangle({ centerX, rings.back().y, centerZ }, { 0.5f, 0.5f },
                               b, { 0.0f, 0.0f }, a, { 1.0f, 0.0f });
            }
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> Subdivide(const std::vector<Vertex>& vertices)
    {
        if (vertices.empty() || vertices.size() % 3 != 0 ||
            vertices.size() > kMaxGeneratedVertices / 4)
            return {};

        std::vector<Vertex> result;
        result.reserve(vertices.size() * 4);
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            const Vertex& a = vertices[i];
            const Vertex& b = vertices[i + 1];
            const Vertex& c = vertices[i + 2];
            const Vertex ab = Midpoint(a, b);
            const Vertex bc = Midpoint(b, c);
            const Vertex ca = Midpoint(c, a);
            PushTriangle(result, a, ab, ca);
            PushTriangle(result, ab, b, bc);
            PushTriangle(result, ca, bc, c);
            PushTriangle(result, ab, bc, ca);
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> ExtrudeFaces(const std::vector<Vertex>& vertices, float distance)
    {
        std::vector<Vertex> result;
        result.reserve(vertices.size() * 5);
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            const Vertex& a = vertices[i];
            const Vertex& b = vertices[i + 1];
            const Vertex& c = vertices[i + 2];
            const float abX = b.X - a.X;
            const float abY = b.Y - a.Y;
            const float abZ = b.Z - a.Z;
            const float acX = c.X - a.X;
            const float acY = c.Y - a.Y;
            const float acZ = c.Z - a.Z;
            float nx = abY * acZ - abZ * acY;
            float ny = abZ * acX - abX * acZ;
            float nz = abX * acY - abY * acX;
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length > 0.00001f)
            {
                nx /= length; ny /= length; nz /= length;
            }

            auto moved = [distance, nx, ny, nz](const Vertex& vertex)
            {
                Vertex result = vertex;
                result.X += nx * distance;
                result.Y += ny * distance;
                result.Z += nz * distance;
                return result;
            };
            const Vertex aa = moved(a);
            const Vertex bb = moved(b);
            const Vertex cc = moved(c);

            PushTriangle(result, aa, bb, cc);
            PushTriangle(result, a, b, bb);
            PushTriangle(result, a, bb, aa);
            PushTriangle(result, b, c, cc);
            PushTriangle(result, b, cc, bb);
            PushTriangle(result, c, a, aa);
            PushTriangle(result, c, aa, cc);
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> InsetFaces(const std::vector<Vertex>& vertices, float amount)
    {
        amount = std::clamp(amount, 0.0f, 0.95f);
        if (vertices.empty()) return {};
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        for (const Vertex& vertex : vertices)
        {
            cx += vertex.X; cy += vertex.Y; cz += vertex.Z;
        }
        const float count = static_cast<float>(vertices.size());
        cx /= count; cy /= count; cz /= count;
        std::vector<Vertex> result = vertices;
        const float factor = 1.0f - amount;
        for (Vertex& vertex : result)
        {
            vertex.X = cx + (vertex.X - cx) * factor;
            vertex.Y = cy + (vertex.Y - cy) * factor;
            vertex.Z = cz + (vertex.Z - cz) * factor;
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> BevelFaces(const std::vector<Vertex>& vertices, float amount)
    {
        amount = std::clamp(amount, 0.0f, 0.5f);
        std::vector<Vertex> result;
        result.reserve(vertices.size() * 4);
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            const Vertex& a = vertices[i];
            const Vertex& b = vertices[i + 1];
            const Vertex& c = vertices[i + 2];
            auto toward = [amount](const Vertex& from, const Vertex& target)
            {
                Vertex value = from;
                value.X += (target.X - from.X) * amount;
                value.Y += (target.Y - from.Y) * amount;
                value.Z += (target.Z - from.Z) * amount;
                return value;
            };
            const Vertex ab = toward(a, b);
            const Vertex ba = toward(b, a);
            const Vertex bc = toward(b, c);
            const Vertex cb = toward(c, b);
            const Vertex ca = toward(c, a);
            const Vertex ac = toward(a, c);
            PushTriangle(result, ab, bc, ca);
            PushTriangle(result, ba, cb, ac);
            PushTriangle(result, ab, ba, ac);
            PushTriangle(result, ab, ac, ca);
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> Mirror(const std::vector<Vertex>& vertices, int axis)
    {
        axis = std::clamp(axis, 0, 2);
        std::vector<Vertex> result = vertices;
        result.reserve(vertices.size() * 2);
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            Vertex a = vertices[i];
            Vertex b = vertices[i + 1];
            Vertex c = vertices[i + 2];
            auto mirror = [axis](Vertex& vertex)
            {
                float* position = axis == 0 ? &vertex.X : axis == 1 ? &vertex.Y : &vertex.Z;
                float* normal = axis == 0 ? &vertex.NX : axis == 1 ? &vertex.NY : &vertex.NZ;
                *position = -*position;
                *normal = -*normal;
            };
            mirror(a); mirror(b); mirror(c);
            PushTriangle(result, c, b, a);
        }
        RecalculateNormals(result);
        return result;
    }

    namespace
    {
        bool FaceWasSelected(const std::vector<int>& selectedFaces, int face)
        {
            return std::find(selectedFaces.begin(), selectedFaces.end(), face) !=
                selectedFaces.end();
        }

        template <typename Operation>
        std::vector<Vertex> ApplyToSelectedFaces(
            const std::vector<Vertex>& vertices,
            const std::vector<int>& selectedFaces,
            Operation operation)
        {
            if (selectedFaces.empty())
                return vertices;

            std::vector<Vertex> result;
            result.reserve(vertices.size());
            for (size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                const int face = static_cast<int>(i / 3);
                if (!FaceWasSelected(selectedFaces, face))
                {
                    PushTriangle(result, vertices[i], vertices[i + 1], vertices[i + 2]);
                    continue;
                }

                const std::vector<Vertex> triangle = {
                    vertices[i], vertices[i + 1], vertices[i + 2]};
                const std::vector<Vertex> transformed = operation(triangle);
                result.insert(result.end(), transformed.begin(), transformed.end());
            }
            return result;
        }
    }

    std::vector<Vertex> SubdivideSelected(
        const std::vector<Vertex>& vertices,
        const std::vector<int>& selectedFaces)
    {
        if (vertices.empty() || vertices.size() % 3 != 0 || selectedFaces.empty())
            return vertices;

        std::unordered_set<int> validFaces;
        const size_t faceCount = vertices.size() / 3;
        for (int face : selectedFaces)
            if (face >= 0 && static_cast<size_t>(face) < faceCount)
                validFaces.insert(face);
        if (validFaces.empty() ||
            vertices.size() > kMaxGeneratedVertices ||
            validFaces.size() > (kMaxGeneratedVertices - vertices.size()) / 9)
            return {};

        std::vector<int> uniqueFaces(validFaces.begin(), validFaces.end());
        std::vector<Vertex> result = ApplyToSelectedFaces(
            vertices, uniqueFaces,
            [](const std::vector<Vertex>& triangle) { return Subdivide(triangle); });
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> ExtrudeFacesSelected(
        const std::vector<Vertex>& vertices,
        float distance,
        const std::vector<int>& selectedFaces)
    {
        if (selectedFaces.empty() || vertices.size() < 3)
            return vertices;

        std::unordered_set<int> selectedSet(selectedFaces.begin(), selectedFaces.end());
        const size_t faceCount = vertices.size() / 3;

        struct EdgeKeyLocal
        {
            PositionKey a{};
            PositionKey b{};
            bool operator==(const EdgeKeyLocal& other) const
            {
                return (a == other.a && b == other.b) || (a == other.b && b == other.a);
            }
        };
        struct EdgeKeyLocalHash
        {
            size_t operator()(const EdgeKeyLocal& k) const
            {
                PositionKeyHash h;
                return h(k.a) ^ (h(k.b) << 1u);
            }
        };

        std::unordered_map<EdgeKeyLocal, int, EdgeKeyLocalHash> edgeOccurrences;
        std::unordered_map<PositionKey, glm::vec3, PositionKeyHash> vertexNormals;

        for (int face : selectedFaces)
        {
            if (face < 0 || static_cast<size_t>(face) >= faceCount)
                continue;
            const size_t base = static_cast<size_t>(face) * 3;
            const Vertex& v0 = vertices[base];
            const Vertex& v1 = vertices[base + 1];
            const Vertex& v2 = vertices[base + 2];

            const glm::vec3 a(v0.X, v0.Y, v0.Z);
            const glm::vec3 b(v1.X, v1.Y, v1.Z);
            const glm::vec3 c(v2.X, v2.Y, v2.Z);
            glm::vec3 n = glm::cross(b - a, c - a);
            const float len = glm::length(n);
            n = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);

            PositionKey k0 = MakeKey(v0);
            PositionKey k1 = MakeKey(v1);
            PositionKey k2 = MakeKey(v2);

            vertexNormals[k0] += n;
            vertexNormals[k1] += n;
            vertexNormals[k2] += n;

            edgeOccurrences[{ k0, k1 }]++;
            edgeOccurrences[{ k1, k2 }]++;
            edgeOccurrences[{ k2, k0 }]++;
        }

        auto extrudeVertex = [&](const Vertex& v) -> Vertex
        {
            PositionKey key = MakeKey(v);
            glm::vec3 n = vertexNormals[key];
            const float len = glm::length(n);
            n = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);

            Vertex res = v;
            res.X += n.x * distance;
            res.Y += n.y * distance;
            res.Z += n.z * distance;
            return res;
        };

        std::vector<Vertex> result;
        result.reserve(vertices.size() + selectedFaces.size() * 12);

        // Manter faces não selecionadas
        for (size_t f = 0; f < faceCount; ++f)
        {
            if (selectedSet.count(static_cast<int>(f)) == 0)
            {
                PushTriangle(result, vertices[f * 3], vertices[f * 3 + 1], vertices[f * 3 + 2]);
            }
        }

        // Adicionar faces do topo extrudado
        for (int face : selectedFaces)
        {
            if (face < 0 || static_cast<size_t>(face) >= faceCount)
                continue;
            const size_t base = static_cast<size_t>(face) * 3;
            Vertex aa = extrudeVertex(vertices[base]);
            Vertex bb = extrudeVertex(vertices[base + 1]);
            Vertex cc = extrudeVertex(vertices[base + 2]);
            PushTriangle(result, aa, bb, cc);

            // Adicionar quads laterais apenas em arestas de borda (ocorrência == 1)
            // Arestas internas compartilhadas entre faces selecionadas têm ocorrência >= 2 e não ganham quad!
            const Vertex* faceVerts[3] = { &vertices[base], &vertices[base + 1], &vertices[base + 2] };
            const Vertex faceExt[3] = { aa, bb, cc };
            for (int i = 0; i < 3; ++i)
            {
                int next = (i + 1) % 3;
                PositionKey ki = MakeKey(*faceVerts[i]);
                PositionKey kn = MakeKey(*faceVerts[next]);
                if (edgeOccurrences[{ ki, kn }] == 1)
                {
                    PushTriangle(result, *faceVerts[i], *faceVerts[next], faceExt[next]);
                    PushTriangle(result, *faceVerts[i], faceExt[next], faceExt[i]);
                }
            }
        }

        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> InsetFacesSelected(
        const std::vector<Vertex>& vertices,
        float amount,
        const std::vector<int>& selectedFaces)
    {
        std::vector<Vertex> result = ApplyToSelectedFaces(
            vertices, selectedFaces,
            [amount](const std::vector<Vertex>& triangle)
            { return InsetFaces(triangle, amount); });
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> BevelFacesSelected(
        const std::vector<Vertex>& vertices,
        float amount,
        const std::vector<int>& selectedFaces)
    {
        std::vector<Vertex> result = ApplyToSelectedFaces(
            vertices, selectedFaces,
            [amount](const std::vector<Vertex>& triangle)
            { return BevelFaces(triangle, amount); });
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> DeleteFacesSelected(
        const std::vector<Vertex>& vertices,
        const std::vector<int>& selectedFaces)
    {
        if (selectedFaces.empty())
            return vertices;

        std::vector<Vertex> result;
        result.reserve(vertices.size());
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
        {
            const int face = static_cast<int>(i / 3);
            if (!FaceWasSelected(selectedFaces, face))
                PushTriangle(result, vertices[i], vertices[i + 1], vertices[i + 2]);
        }
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> InflateSelected(
        const std::vector<Vertex>& vertices,
        float distance,
        const std::vector<int>& selectedFaces)
    {
        std::vector<Vertex> result = ApplyToSelectedFaces(
            vertices, selectedFaces,
            [distance](const std::vector<Vertex>& triangle)
            {
                std::vector<Vertex> inflated = triangle;
                Inflate(inflated, distance);
                return inflated;
            });
        RecalculateNormals(result);
        return result;
    }

    std::vector<Vertex> SubdivideMiniFaces(
        const std::vector<Vertex>& vertices,
        const std::vector<int>& selectedFaces,
        MiniFaceShape shape,
        MiniFaceOrientation orientation,
        int verticalLines,
        int horizontalLines)
    {
        if (vertices.empty() || vertices.size() % 3 != 0)
            return vertices;

        int vLines = (orientation == MiniFaceOrientation::Horizontal) ? 0 : std::clamp(verticalLines, 0, 32);
        int hLines = (orientation == MiniFaceOrientation::Vertical) ? 0 : std::clamp(horizontalLines, 0, 32);

        if (shape == MiniFaceShape::Squares && orientation == MiniFaceOrientation::Grid)
        {
            int common = std::max(vLines, hLines);
            vLines = common;
            hLines = common;
        }

        int cols = vLines + 1;
        int rows = hLines + 1;

        if (cols <= 1 && rows <= 1 && shape != MiniFaceShape::Triangles)
            return vertices;

        MeshTopology topology = MeshTopology::FromVertices(vertices);
        const auto edges = topology.GetEdges();
        std::vector<bool> faceHandled(topology.Faces.size(), false);

        std::unordered_set<int> selectedSet(selectedFaces.begin(), selectedFaces.end());
        const bool selectAll = selectedFaces.empty();

        std::vector<Vertex> result;
        result.reserve(vertices.size() * (cols * rows * 2 + 1));

        auto BilinearVertex = [](const Vertex& p00, const Vertex& p10, const Vertex& p11, const Vertex& p01, float u, float v, const glm::vec3& norm) -> Vertex
        {
            Vertex res{};
            float w00 = (1.0f - u) * (1.0f - v);
            float w10 = u * (1.0f - v);
            float w11 = u * v;
            float w01 = (1.0f - u) * v;

            res.X = w00 * p00.X + w10 * p10.X + w11 * p11.X + w01 * p01.X;
            res.Y = w00 * p00.Y + w10 * p10.Y + w11 * p11.Y + w01 * p01.Y;
            res.Z = w00 * p00.Z + w10 * p10.Z + w11 * p11.Z + w01 * p01.Z;

            res.NX = norm.x; res.NY = norm.y; res.NZ = norm.z;

            res.U = w00 * p00.U + w10 * p10.U + w11 * p11.U + w01 * p01.U;
            res.V = w00 * p00.V + w10 * p10.V + w11 * p11.V + w01 * p01.V;

            return res;
        };

        // 1. Process Quads (coplanar adjacent pairs of triangles)
        for (const auto& edge : edges)
        {
            if (edge.Faces.size() != 2) continue;
            uint32_t fA = edge.Faces[0];
            uint32_t fB = edge.Faces[1];
            if (faceHandled[fA] || faceHandled[fB]) continue;

            const auto& faceA = topology.Faces[fA];
            const auto& faceB = topology.Faces[fB];

            const auto& va0 = topology.Vertices[faceA.Vertices[0]].Value;
            const auto& va1 = topology.Vertices[faceA.Vertices[1]].Value;
            const auto& va2 = topology.Vertices[faceA.Vertices[2]].Value;
            glm::vec3 pa0(va0.X, va0.Y, va0.Z), pa1(va1.X, va1.Y, va1.Z), pa2(va2.X, va2.Y, va2.Z);
            glm::vec3 nA = glm::cross(pa1 - pa0, pa2 - pa0);
            float lenA = glm::length(nA);
            if (lenA < 0.00001f) continue;
            nA /= lenA;

            const auto& vb0 = topology.Vertices[faceB.Vertices[0]].Value;
            const auto& vb1 = topology.Vertices[faceB.Vertices[1]].Value;
            const auto& vb2 = topology.Vertices[faceB.Vertices[2]].Value;
            glm::vec3 pb0(vb0.X, vb0.Y, vb0.Z), pb1(vb1.X, vb1.Y, vb1.Z), pb2(vb2.X, vb2.Y, vb2.Z);
            glm::vec3 nB = glm::cross(pb1 - pb0, pb2 - pb0);
            float lenB = glm::length(nB);
            if (lenB < 0.00001f) continue;
            nB /= lenB;

            if (glm::dot(nA, nB) < 0.999f) continue;

            uint32_t u = edge.A;
            uint32_t v = edge.B;
            uint32_t cA = 0, cB = 0;
            for (uint32_t idx : faceA.Vertices) if (idx != u && idx != v) cA = idx;
            for (uint32_t idx : faceB.Vertices) if (idx != u && idx != v) cB = idx;

            bool uThenVInA = false;
            for (int k = 0; k < 3; ++k)
            {
                if (faceA.Vertices[k] == u && faceA.Vertices[(k + 1) % 3] == v)
                {
                    uThenVInA = true;
                    break;
                }
            }

            Vertex p00, p10, p11, p01;
            if (uThenVInA)
            {
                p00 = topology.Vertices[u].Value;
                p10 = topology.Vertices[cB].Value;
                p11 = topology.Vertices[v].Value;
                p01 = topology.Vertices[cA].Value;
            }
            else
            {
                p00 = topology.Vertices[v].Value;
                p10 = topology.Vertices[cA].Value;
                p11 = topology.Vertices[u].Value;
                p01 = topology.Vertices[cB].Value;
            }

            faceHandled[fA] = true;
            faceHandled[fB] = true;

            const bool isSelected = selectAll ||
                (selectedSet.find(static_cast<int>(fA)) != selectedSet.end()) ||
                (selectedSet.find(static_cast<int>(fB)) != selectedSet.end());

            if (!isSelected)
            {
                p00.NX = nA.x; p00.NY = nA.y; p00.NZ = nA.z;
                p10.NX = nA.x; p10.NY = nA.y; p10.NZ = nA.z;
                p11.NX = nA.x; p11.NY = nA.y; p11.NZ = nA.z;
                p01.NX = nA.x; p01.NY = nA.y; p01.NZ = nA.z;
                PushTriangle(result, p00, p10, p11);
                PushTriangle(result, p00, p11, p01);
                continue;
            }

            glm::vec3 edgeU(p10.X - p00.X, p10.Y - p00.Y, p10.Z - p00.Z);
            glm::vec3 edgeV(p01.X - p00.X, p01.Y - p00.Y, p01.Z - p00.Z);

            // Identifica qual vetor está mais alinhado com a vertical
            bool uIsVertical = false;
            if (std::abs(edgeU.y - edgeV.y) > 0.001f)
                uIsVertical = std::abs(edgeU.y) > std::abs(edgeV.y);
            else
                uIsVertical = std::abs(edgeU.z) > std::abs(edgeV.z);

            int curUCols = uIsVertical ? rows : cols;
            int curVRows = uIsVertical ? cols : rows;

            for (int r = 0; r < curVRows; ++r)
            {
                float v0 = static_cast<float>(r) / static_cast<float>(curVRows);
                float v1 = static_cast<float>(r + 1) / static_cast<float>(curVRows);

                for (int c = 0; c < curUCols; ++c)
                {
                    float u0 = static_cast<float>(c) / static_cast<float>(curUCols);
                    float u1 = static_cast<float>(c + 1) / static_cast<float>(curUCols);

                    Vertex c00 = BilinearVertex(p00, p10, p11, p01, u0, v0, nA);
                    Vertex c10 = BilinearVertex(p00, p10, p11, p01, u1, v0, nA);
                    Vertex c11 = BilinearVertex(p00, p10, p11, p01, u1, v1, nA);
                    Vertex c01 = BilinearVertex(p00, p10, p11, p01, u0, v1, nA);

                    if (shape == MiniFaceShape::Triangles)
                    {
                        Vertex cmid = BilinearVertex(p00, p10, p11, p01, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, nA);
                        PushTriangle(result, c00, c10, cmid);
                        PushTriangle(result, c10, c11, cmid);
                        PushTriangle(result, c11, c01, cmid);
                        PushTriangle(result, c01, c00, cmid);
                    }
                    else
                    {
                        PushTriangle(result, c00, c10, c11);
                        PushTriangle(result, c00, c11, c01);
                    }
                }
            }
        }

        // 2. Standalone triangles
        for (size_t fIdx = 0; fIdx < topology.Faces.size(); ++fIdx)
        {
            if (faceHandled[fIdx]) continue;
            const auto& face = topology.Faces[fIdx];
            const auto& v0 = topology.Vertices[face.Vertices[0]].Value;
            const auto& v1 = topology.Vertices[face.Vertices[1]].Value;
            const auto& v2 = topology.Vertices[face.Vertices[2]].Value;

            const bool isSelected = selectAll ||
                (selectedSet.find(static_cast<int>(fIdx)) != selectedSet.end());

            if (!isSelected || (cols <= 1 && rows <= 1))
            {
                PushTriangle(result, v0, v1, v2);
                continue;
            }

            std::vector<Vertex> tri = { v0, v1, v2 };
            std::vector<Vertex> sub = Subdivide(tri);
            for (auto& v : sub)
            {
                v.NX = v0.NX; v.NY = v0.NY; v.NZ = v0.NZ;
                result.push_back(v);
            }
        }

        return result;
    }
}
