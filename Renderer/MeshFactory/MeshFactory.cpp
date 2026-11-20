#include "MeshFactory.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace
{
    std::vector<Vertex> CreateFilledPolygon2D(const std::vector<glm::vec2>& points)
    {
        std::vector<Vertex> vertices;
        if (points.size() < 3)
            return vertices;

        vertices.reserve(points.size() * 3);
        const auto addVertex = [&vertices](const glm::vec2& point)
        {
            vertices.emplace_back(point.x, point.y, 0.0f,
                0.0f, 0.0f, 1.0f,
                point.x + 0.5f, point.y + 0.5f);
        };

        // Triângulos em leque, winding anti-horário e normais voltadas para +Z.
        for (size_t i = 0; i < points.size(); ++i)
        {
            addVertex(glm::vec2(0.0f));
            addVertex(points[i]);
            addVertex(points[(i + 1) % points.size()]);
        }
        return vertices;
    }

    std::vector<Vertex> CreateRegularPolygon2D(int sides, float rotationRadians)
    {
        sides = std::clamp(sides, 3, 128);
        std::vector<glm::vec2> points;
        points.reserve(static_cast<size_t>(sides));
        const float step = glm::two_pi<float>() / static_cast<float>(sides);
        for (int i = 0; i < sides; ++i)
        {
            const float angle = rotationRadians + step * static_cast<float>(i);
            points.emplace_back(std::cos(angle) * 0.5f, std::sin(angle) * 0.5f);
        }
        return CreateFilledPolygon2D(points);
    }
}

std::vector<Vertex> MeshFactory::Create2DPrimitive(const std::string& primitiveType)
{
    if (primitiveType == "Circle2D")
        return CreateRegularPolygon2D(48, 0.0f);
    if (primitiveType == "Pentagon2D")
        return CreateRegularPolygon2D(5, glm::half_pi<float>());
    if (primitiveType == "Hexagon2D")
        return CreateRegularPolygon2D(6, 0.0f);
    if (primitiveType == "Octagon2D")
        return CreateRegularPolygon2D(8, glm::half_pi<float>());
    if (primitiveType == "Diamond2D")
        return CreateRegularPolygon2D(4, glm::half_pi<float>());
    if (primitiveType == "Star2D")
    {
        constexpr int pointsCount = 5;
        std::vector<glm::vec2> points;
        points.reserve(pointsCount * 2);
        for (int i = 0; i < pointsCount * 2; ++i)
        {
            const float angle = glm::half_pi<float>() +
                glm::pi<float>() * static_cast<float>(i) / pointsCount;
            const float radius = (i % 2 == 0) ? 0.5f : 0.22f;
            points.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius);
        }
        return CreateFilledPolygon2D(points);
    }
    return {};
}

std::vector<Vertex> MeshFactory::CreateTriangle()
{
    return
    {
        Vertex(
            0.0f, 0.5f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.5f, 1.0f
        ),

        Vertex(
            -0.5f, -0.5f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f
        ),

        Vertex(
            0.5f, -0.5f, 0.0f,
            0.0f, 0.0f, 1.0f,
            1.0f, 0.0f
        )
    };
}

std::vector<Vertex> MeshFactory::CreateCylinder(int sectors, float height)
{
    std::vector<Vertex> vertices;
    sectors = std::max(3, sectors);
    height = std::max(0.001f, height);
    const float PI = glm::pi<float>();
    const float halfH = height * 0.5f;
    const float step = (2.0f * PI) / static_cast<float>(sectors);

    // Lateral: todos os triângulos são orientados para fora. A versão
    // anterior começava no vértice inferior e invertia o winding da faixa.
    for (int i = 0; i < sectors; ++i)
    {
        const float a0 = static_cast<float>(i) * step;
        const float a1 = static_cast<float>(i + 1) * step;

        const float x0 = cosf(a0) * 0.5f;
        const float z0 = sinf(a0) * 0.5f;
        const float x1 = cosf(a1) * 0.5f;
        const float z1 = sinf(a1) * 0.5f;

        vertices.emplace_back(x0,  halfH, z0, x0, 0.0f, z0, 0.0f, 1.0f);
        vertices.emplace_back(x1,  halfH, z1, x1, 0.0f, z1, 1.0f, 1.0f);
        vertices.emplace_back(x1, -halfH, z1, x1, 0.0f, z1, 1.0f, 0.0f);
        vertices.emplace_back(x0,  halfH, z0, x0, 0.0f, z0, 0.0f, 1.0f);
        vertices.emplace_back(x1, -halfH, z1, x1, 0.0f, z1, 1.0f, 0.0f);
        vertices.emplace_back(x0, -halfH, z0, x0, 0.0f, z0, 0.0f, 0.0f);
    }

    // Tampa inferior (-Y): centro, ponto do setor atual, próximo ponto.
    for (int i = 0; i < sectors; ++i)
    {
        const float a0 = static_cast<float>(i) * step;
        const float a1 = static_cast<float>(i + 1) * step;
        const float x0 = cosf(a0) * 0.5f;
        const float z0 = sinf(a0) * 0.5f;
        const float x1 = cosf(a1) * 0.5f;
        const float z1 = sinf(a1) * 0.5f;
        vertices.emplace_back(0.0f, -halfH, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
        vertices.emplace_back(x0, -halfH, z0, 0.0f, -1.0f, 0.0f, x0 + 0.5f, z0 + 0.5f);
        vertices.emplace_back(x1, -halfH, z1, 0.0f, -1.0f, 0.0f, x1 + 0.5f, z1 + 0.5f);

        // Tampa superior (+Y): centro, próximo ponto, ponto atual.
        vertices.emplace_back(0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f);
        vertices.emplace_back(x1, halfH, z1, 0.0f, 1.0f, 0.0f, x1 + 0.5f, z1 + 0.5f);
        vertices.emplace_back(x0, halfH, z0, 0.0f, 1.0f, 0.0f, x0 + 0.5f, z0 + 0.5f);
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateCone(int sectors, float height)
{
    std::vector<Vertex> vertices;
    sectors = std::max(3, sectors);
    height = std::max(0.001f, height);
    const float PI = glm::pi<float>();
    const float halfH = height * 0.5f;
    const float step = (2.0f * PI) / static_cast<float>(sectors);

    // Lateral: p0 -> ápice -> p1 produz normal para fora.
    for (int i = 0; i < sectors; ++i)
    {
        const float ai = static_cast<float>(i) * step;
        const float aj = static_cast<float>(i + 1) * step;
        const float xi = cosf(ai) * 0.5f;
        const float zi = sinf(ai) * 0.5f;
        const float xj = cosf(aj) * 0.5f;
        const float zj = sinf(aj) * 0.5f;

        const glm::vec3 p0(xi, -halfH, zi);
        const glm::vec3 p1(xj, -halfH, zj);
        const glm::vec3 apex(0.0f, halfH, 0.0f);
        const glm::vec3 n = glm::normalize(glm::cross(apex - p0, p1 - p0));

        vertices.emplace_back(xi, -halfH, zi, n.x, n.y, n.z, 0.0f, 0.0f);
        vertices.emplace_back(0.0f, halfH, 0.0f, n.x, n.y, n.z, 0.5f, 1.0f);
        vertices.emplace_back(xj, -halfH, zj, n.x, n.y, n.z, 1.0f, 0.0f);

        // Base (-Y): centro, ponto atual, próximo ponto.
        vertices.emplace_back(0.0f, -halfH, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
        vertices.emplace_back(xi, -halfH, zi, 0.0f, -1.0f, 0.0f, xi + 0.5f, zi + 0.5f);
        vertices.emplace_back(xj, -halfH, zj, 0.0f, -1.0f, 0.0f, xj + 0.5f, zj + 0.5f);
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateQuad()
{
    return
    {
        Vertex(
            -0.5f,-0.5f,0.0f,
            0.0f,0.0f,1.0f,
            0.0f,0.0f
        ),

        Vertex(
             0.5f,-0.5f,0.0f,
             0.0f,0.0f,1.0f,
             1.0f,0.0f
        ),

        Vertex(
             0.5f,0.5f,0.0f,
             0.0f,0.0f,1.0f,
             1.0f,1.0f
        ),

        Vertex(
            -0.5f,-0.5f,0.0f,
             0.0f,0.0f,1.0f,
             0.0f,0.0f
        ),

        Vertex(
             0.5f,0.5f,0.0f,
             0.0f,0.0f,1.0f,
             1.0f,1.0f
        ),

        Vertex(
            -0.5f,0.5f,0.0f,
             0.0f,0.0f,1.0f,
             0.0f,1.0f
        )
    };
}

std::vector<Vertex> MeshFactory::CreateCube()
{
    return
    {
        // =====================================================
        // FRENTE (+Z)
        // Normal = (0,0,1)
        // =====================================================

        Vertex(
            -0.5f,-0.5f, 0.5f,
             0.0f, 0.0f, 1.0f,
             0.0f,0.0f
        ),

        Vertex(
             0.5f,-0.5f, 0.5f,
             0.0f,0.0f,1.0f,
             1.0f,0.0f
        ),

        Vertex(
             0.5f, 0.5f, 0.5f,
             0.0f,0.0f,1.0f,
             1.0f,1.0f
        ),

        Vertex(
            -0.5f,-0.5f, 0.5f,
             0.0f,0.0f,1.0f,
             0.0f,0.0f
        ),

        Vertex(
             0.5f, 0.5f, 0.5f,
             0.0f,0.0f,1.0f,
             1.0f,1.0f
        ),

        Vertex(
            -0.5f, 0.5f, 0.5f,
             0.0f,0.0f,1.0f,
             0.0f,1.0f
        ),

            // =====================================================
            // TRÁS (-Z)
            // Normal = (0,0,-1)
            // =====================================================

            Vertex(
                 0.5f,-0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 0.0f,0.0f
            ),

            Vertex(
                -0.5f,-0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 1.0f,0.0f
            ),

            Vertex(
                -0.5f, 0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 1.0f,1.0f
            ),

            Vertex(
                 0.5f,-0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 0.0f,0.0f
            ),

            Vertex(
                -0.5f, 0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 1.0f,1.0f
            ),

            Vertex(
                 0.5f, 0.5f,-0.5f,
                 0.0f,0.0f,-1.0f,
                 0.0f,1.0f
            ),

// =====================================================
// ESQUERDA (-X)
// Normal = (-1,0,0)
// =====================================================

Vertex(
    -0.5f,-0.5f,-0.5f,
    -1.0f,0.0f,0.0f,
    0.0f,0.0f
),

Vertex(
    -0.5f,-0.5f, 0.5f,
    -1.0f,0.0f,0.0f,
    1.0f,0.0f
),

Vertex(
    -0.5f, 0.5f, 0.5f,
    -1.0f,0.0f,0.0f,
    1.0f,1.0f
),

Vertex(
    -0.5f,-0.5f,-0.5f,
    -1.0f,0.0f,0.0f,
    0.0f,0.0f
),

Vertex(
    -0.5f, 0.5f, 0.5f,
    -1.0f,0.0f,0.0f,
    1.0f,1.0f
),

Vertex(
    -0.5f, 0.5f,-0.5f,
    -1.0f,0.0f,0.0f,
    0.0f,1.0f
),

// =====================================================
// DIREITA (+X)
// Normal = (1,0,0)
// =====================================================

Vertex(
     0.5f,-0.5f, 0.5f,
     1.0f,0.0f,0.0f,
     0.0f,0.0f
),

Vertex(
     0.5f,-0.5f,-0.5f,
     1.0f,0.0f,0.0f,
     1.0f,0.0f
),

Vertex(
     0.5f, 0.5f,-0.5f,
     1.0f,0.0f,0.0f,
     1.0f,1.0f
),

Vertex(
     0.5f,-0.5f, 0.5f,
     1.0f,0.0f,0.0f,
     0.0f,0.0f
),

Vertex(
     0.5f, 0.5f,-0.5f,
     1.0f,0.0f,0.0f,
     1.0f,1.0f
),

Vertex(
     0.5f, 0.5f, 0.5f,
     1.0f,0.0f,0.0f,
     0.0f,1.0f
),

        // =====================================================
        // TOPO (Y = +0.5)
        // Normal = (0,1,0)
        // =====================================================

        Vertex(
            -0.5f,  0.5f,  0.5f,
             0.0f,  1.0f,  0.0f,
             0.0f,  0.0f
        ),

        Vertex(
             0.5f,  0.5f,  0.5f,
             0.0f,  1.0f,  0.0f,
             1.0f,  0.0f
        ),

        Vertex(
             0.5f,  0.5f, -0.5f,
             0.0f,  1.0f,  0.0f,
             1.0f,  1.0f
        ),

        Vertex(
            -0.5f,  0.5f,  0.5f,
             0.0f,  1.0f,  0.0f,
             0.0f,  0.0f
        ),

        Vertex(
             0.5f,  0.5f, -0.5f,
             0.0f,  1.0f,  0.0f,
             1.0f,  1.0f
        ),

        Vertex(
            -0.5f,  0.5f, -0.5f,
             0.0f,  1.0f,  0.0f,
             0.0f,  1.0f
        ),

        // =====================================================
        // BASE (Y = -0.5)
        // Normal = (0,-1,0)
        // =====================================================

        Vertex(
            -0.5f, -0.5f, -0.5f,
             0.0f, -1.0f,  0.0f,
             0.0f,  0.0f
        ),

        Vertex(
             0.5f, -0.5f, -0.5f,
             0.0f, -1.0f,  0.0f,
             1.0f,  0.0f
        ),

        Vertex(
             0.5f, -0.5f,  0.5f,
             0.0f, -1.0f,  0.0f,
             1.0f,  1.0f
        ),

        Vertex(
            -0.5f, -0.5f, -0.5f,
             0.0f, -1.0f,  0.0f,
             0.0f,  0.0f
        ),

        Vertex(
             0.5f, -0.5f,  0.5f,
             0.0f, -1.0f,  0.0f,
             1.0f,  1.0f
        ),

        Vertex(
            -0.5f, -0.5f,  0.5f,
             0.0f, -1.0f,  0.0f,
             0.0f,  1.0f
        )
    };

}

std::vector<Vertex> MeshFactory::CreateGrid()
{
    std::vector<Vertex> vertices;

    const int size = 20;

    for (int i = -size; i <= size; i++)
    {
        // Skip the central lines (i == 0) so they don't overlap or conflict with Axis X and Axis Z!
        if (i == 0)
            continue;

        // Lines parallel to Z (varying in X)
        vertices.push_back(
            Vertex(
                (float)i,
                0.0f,
                (float)-size,
                0.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f
            )
        );

        vertices.push_back(
            Vertex(
                (float)i,
                0.0f,
                (float)size,
                0.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f
            )
        );

        // Lines parallel to X (varying in Z)
        vertices.push_back(
            Vertex(
                (float)-size,
                0.0f,
                (float)i,
                0.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f
            )
        );

        vertices.push_back(
            Vertex(
                (float)size,
                0.0f,
                (float)i,
                0.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f
            )
        );
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateAxis()
{
    return
    {
        Vertex(0.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(20.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),

        Vertex(0.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(0.0f, 20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),

        Vertex(0.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(0.0f, 0.001f, 20.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)
    };
}

std::vector<Vertex> MeshFactory::CreateAxisX()
{
    return
    {
        Vertex(-20.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex( 20.0f, 0.001f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)
    };
}

std::vector<Vertex> MeshFactory::CreateAxisY()
{
    return
    {
        Vertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(0.0f, 20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)
    };
}

std::vector<Vertex> MeshFactory::CreateAxisZ()
{
    return
    {
        Vertex(0.0f, 0.001f, -20.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(0.0f, 0.001f,  20.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)
    };
}

std::vector<Vertex> MeshFactory::CreateSphere(int sectors, int stacks)
{
    std::vector<Vertex> vertices;
    float const R = 1.0f / (float)(stacks - 1);
    float const S = 1.0f / (float)(sectors - 1);

    struct TempVert {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
    };
    std::vector<TempVert> grid;
    grid.reserve(stacks * sectors);

    const float PI = 3.14159265358979323846f;

    for (int r = 0; r < stacks; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            float const y = (float)sin(-PI / 2.0f + PI * (float)r * R);
            float const x = (float)(cos(2.0f * PI * (float)s * S) * sin(PI * (float)r * R));
            float const z = (float)(sin(2.0f * PI * (float)s * S) * sin(PI * (float)r * R));

            grid.push_back({ x, y, z, x, y, z, (float)s * S, (float)r * R });
        }
    }

    for (int r = 0; r < stacks - 1; ++r)
    {
        for (int s = 0; s < sectors - 1; ++s)
        {
            auto const& v0 = grid[r * sectors + s];
            auto const& v1 = grid[r * sectors + (s + 1)];
            auto const& v2 = grid[(r + 1) * sectors + (s + 1)];
            auto const& v3 = grid[(r + 1) * sectors + s];

            // Ensure consistent winding (outward) by flipping order if necessary
            // Triangle 1 (v0, v2, v1)
            vertices.push_back(Vertex(v0.x, v0.y, v0.z, v0.nx, v0.ny, v0.nz, v0.u, v0.v));
            vertices.push_back(Vertex(v2.x, v2.y, v2.z, v2.nx, v2.ny, v2.nz, v2.u, v2.v));
            vertices.push_back(Vertex(v1.x, v1.y, v1.z, v1.nx, v1.ny, v1.nz, v1.u, v1.v));

            // Triangle 2 (v0, v3, v2)
            vertices.push_back(Vertex(v0.x, v0.y, v0.z, v0.nx, v0.ny, v0.nz, v0.u, v0.v));
            vertices.push_back(Vertex(v3.x, v3.y, v3.z, v3.nx, v3.ny, v3.nz, v3.u, v3.v));
            vertices.push_back(Vertex(v2.x, v2.y, v2.z, v2.nx, v2.ny, v2.nz, v2.u, v2.v));
        }
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateSunDisc(int segments)
{
    std::vector<Vertex> vertices;
    const float PI = 3.14159265358979323846f;
    float step = (2.0f * PI) / (float)segments;

    for (int i = 0; i < segments; i++)
    {
        float theta1 = (float)i * step;
        float theta2 = (float)(i + 1) * step;

        float x1 = (float)cos(theta1);
        float y1 = (float)sin(theta1);
        float x2 = (float)cos(theta2);
        float y2 = (float)sin(theta2);

        // Center vertex
        vertices.push_back(Vertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f));
        // Point 1
        vertices.push_back(Vertex(x1, y1, 0.0f, 0.0f, 0.0f, 1.0f, x1 * 0.5f + 0.5f, y1 * 0.5f + 0.5f));
        // Point 2
        vertices.push_back(Vertex(x2, y2, 0.0f, 0.0f, 0.0f, 1.0f, x2 * 0.5f + 0.5f, y2 * 0.5f + 0.5f));
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateSunHalo(int segments)
{
    std::vector<Vertex> vertices;
    const float PI = 3.14159265358979323846f;
    float step = (2.0f * PI) / (float)segments;

    const float innerR = 0.8f;
    const float outerR = 3.0f;

    for (int i = 0; i < segments; i++)
    {
        float theta1 = (float)i * step;
        float theta2 = (float)(i + 1) * step;

        float inX1 = (float)cos(theta1) * innerR;
        float inY1 = (float)sin(theta1) * innerR;
        float inX2 = (float)cos(theta2) * innerR;
        float inY2 = (float)sin(theta2) * innerR;

        float outX1 = (float)cos(theta1) * outerR;
        float outY1 = (float)sin(theta1) * outerR;
        float outX2 = (float)cos(theta2) * outerR;
        float outY2 = (float)sin(theta2) * outerR;

        // Quad between inner and outer ring
        // Tri 1
        vertices.push_back(Vertex(inX1, inY1, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f));
        vertices.push_back(Vertex(outX1, outY1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));
        vertices.push_back(Vertex(outX2, outY2, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));

        // Tri 2
        vertices.push_back(Vertex(inX1, inY1, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f));
        vertices.push_back(Vertex(outX2, outY2, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f));
        vertices.push_back(Vertex(inX2, inY2, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f));
    }

    return vertices;
}

std::vector<Vertex> MeshFactory::CreatePuffyCloud()
{
    std::vector<Vertex> allVertices;

    // Organic cumulus cloud puff offsets and dimensions (X, Y, Z, ScaleX, ScaleY, ScaleZ)
    struct CloudPuff {
        float ox, oy, oz;
        float sx, sy, sz;
    };

    const CloudPuff puffs[] = {
        // Main central dome
        {  0.0f,  0.0f,  0.0f, 6.0f, 3.2f, 4.2f },
        // Top puffs creating classic cumulus fluffy crests
        { -1.8f,  1.4f,  0.2f, 3.8f, 2.8f, 3.2f },
        {  1.6f,  1.2f, -0.3f, 4.2f, 2.6f, 3.4f },
        {  0.2f,  2.0f,  0.0f, 3.4f, 2.4f, 2.8f },
        // Flank puffs for wide cloud span
        { -4.2f, -0.4f,  0.3f, 4.0f, 2.2f, 3.0f },
        {  4.4f, -0.5f, -0.2f, 4.2f, 2.2f, 3.2f },
        { -6.2f, -0.8f,  0.0f, 2.8f, 1.6f, 2.4f },
        {  6.4f, -0.8f,  0.1f, 3.0f, 1.6f, 2.4f },
        // Front and back soft billows
        { -0.8f, -0.3f,  2.2f, 3.6f, 2.0f, 2.6f },
        {  1.2f, -0.4f, -2.2f, 3.8f, 2.0f, 2.6f }
    };

    auto baseSphere = CreateSphere(14, 10);

    for (const auto& puff : puffs)
    {
        for (const auto& v : baseSphere)
        {
            float px = v.X * puff.sx + puff.ox;
            float py = v.Y * puff.sy + puff.oy;
            float pz = v.Z * puff.sz + puff.oz;

            // Slight bottom flattening for authentic cumulus cloud base
            if (py < -1.8f)
                py = -1.8f + (py + 1.8f) * 0.3f;

            allVertices.push_back(
                Vertex(
                    px, py, pz,
                    v.NX, v.NY, v.NZ,
                    v.U, v.V
                )
            );
        }
    }

    return allVertices;
}

std::vector<Vertex> MeshFactory::CreatePlane(int subdivisions)
{
    std::vector<Vertex> vertices;
    int segs = subdivisions + 1;
    float step = 1.0f / (float)subdivisions;

    for (int y = 0; y <= subdivisions; ++y)
    {
        for (int x = 0; x <= subdivisions; ++x)
        {
            float px = (float)x * step - 0.5f;
            float pz = (float)y * step - 0.5f;
            vertices.push_back(Vertex(px, 0.0f, pz, 0.0f, 1.0f, 0.0f, (float)x * step, (float)y * step));
        }
    }

    std::vector<Vertex> tris;
    for (int y = 0; y < subdivisions; ++y)
    {
        for (int x = 0; x < subdivisions; ++x)
        {
            int i0 = y * segs + x;
            int i1 = y * segs + x + 1;
            int i2 = (y + 1) * segs + x;
            int i3 = (y + 1) * segs + x + 1;
            tris.push_back(vertices[i0]);
            tris.push_back(vertices[i2]);
            tris.push_back(vertices[i1]);
            tris.push_back(vertices[i1]);
            tris.push_back(vertices[i2]);
            tris.push_back(vertices[i3]);
        }
    }

    return tris;
}

std::vector<Vertex> MeshFactory::CreateWedge()
{
    std::vector<Vertex> vertices;

    // A cunha é um prisma triangular: base, fundo, plano inclinado e duas
    // tampas triangulares. Todas as faces usam winding CCW visto de fora.
    const float lo = -0.5f;
    const float hi = 0.5f;
    const float front = 0.5f;
    const float back = -0.5f;

    // Base (-Y)
    vertices.emplace_back(-hi, lo, back, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f);
    vertices.emplace_back( hi, lo, back, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    vertices.emplace_back( hi, lo, front, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f);
    vertices.emplace_back(-hi, lo, back, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f);
    vertices.emplace_back( hi, lo, front, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f);
    vertices.emplace_back(-hi, lo, front, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f);

    // Fundo vertical (-Z)
    vertices.emplace_back(-hi, lo, back, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    vertices.emplace_back( hi, hi, back, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);
    vertices.emplace_back( hi, lo, back, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
    vertices.emplace_back(-hi, lo, back, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    vertices.emplace_back(-hi, hi, back, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
    vertices.emplace_back( hi, hi, back, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);

    // Plano inclinado: normal constante (0, +Y, +Z).
    const glm::vec3 slopeNormal = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
    vertices.emplace_back(-hi, hi, back, slopeNormal.x, slopeNormal.y, slopeNormal.z, 0.0f, 0.0f);
    vertices.emplace_back(-hi, lo, front, slopeNormal.x, slopeNormal.y, slopeNormal.z, 0.0f, 1.0f);
    vertices.emplace_back( hi, lo, front, slopeNormal.x, slopeNormal.y, slopeNormal.z, 1.0f, 1.0f);
    vertices.emplace_back(-hi, hi, back, slopeNormal.x, slopeNormal.y, slopeNormal.z, 0.0f, 0.0f);
    vertices.emplace_back( hi, lo, front, slopeNormal.x, slopeNormal.y, slopeNormal.z, 1.0f, 1.0f);
    vertices.emplace_back( hi, hi, back, slopeNormal.x, slopeNormal.y, slopeNormal.z, 1.0f, 0.0f);

    // Tampa esquerda (-X)
    vertices.emplace_back(-hi, lo, front, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    vertices.emplace_back(-hi, hi, back, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    vertices.emplace_back(-hi, lo, back, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // Tampa direita (+X)
    vertices.emplace_back( hi, lo, front, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    vertices.emplace_back( hi, lo, back, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    vertices.emplace_back( hi, hi, back, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    return vertices;
}

std::vector<Vertex> MeshFactory::CreateTorus(int sectors, int sides, float outerR, float innerR)
{
    std::vector<Vertex> vertices;
    const float PI = glm::pi<float>();

    std::vector<Vertex> ringVerts;
    for (int j = 0; j <= sides; ++j)
    {
        float phi = (float)j / (float)sides * 2.0f * PI;
        float cx = outerR + innerR * cosf(phi);
        float cy = innerR * sinf(phi);
        float nx = cosf(phi);
        float ny = sinf(phi);
        ringVerts.push_back(Vertex(cx, cy, 0.0f, nx, ny, 0.0f, (float)j / (float)sides, 0.0f));
    }

    for (int i = 0; i <= sectors; ++i)
    {
        float theta = (float)i / (float)sectors * 2.0f * PI;
        float ct = cosf(theta), st = sinf(theta);
        for (int j = 0; j <= sides; ++j)
        {
            Vertex base = ringVerts[j];
            float x = base.X * ct;
            float z = -base.X * st;
            float y = base.Y;
            float nx = base.NX * ct;
            float nz = -base.NX * st;
            float ny = base.NY;
            vertices.push_back(Vertex(x, y, z, nx, ny, nz, (float)i / (float)sectors, (float)j / (float)sides));
        }
    }

    std::vector<Vertex> result;
    int stride = sides + 1;
    for (int i = 0; i < sectors; ++i)
    {
        for (int j = 0; j < sides; ++j)
        {
            int a = i * stride + j;
            int b = a + 1;
            int c = a + stride;
            int d = c + 1;
            result.push_back(vertices[a]);
            result.push_back(vertices[c]);
            result.push_back(vertices[b]);
            result.push_back(vertices[b]);
            result.push_back(vertices[c]);
            result.push_back(vertices[d]);
        }
    }
    return result;
}

std::vector<Vertex> MeshFactory::CreateCapsule(int sectors, int stacks, float radius, float height)
{
    std::vector<Vertex> vertices;
    sectors = std::max(3, sectors);
    stacks = std::max(4, stacks);
    if (stacks % 2 != 0)
        ++stacks;
    radius = std::max(0.001f, radius);
    height = std::max(0.001f, height);

    const float PI = glm::pi<float>();
    const float halfH = height * 0.5f;
    const int hemisphereRings = stacks / 2;
    const int cylinderRings = 4;
    const int stride = sectors + 1;

    // Cada anel é gerado em ordem do polo superior ao polo inferior. O
    // cilindro conecta exatamente os dois equadores; não há mais a
    // descontinuidade de altura que existia na implementação anterior.
    for (int r = 0; r <= hemisphereRings; ++r)
    {
        const float phi = (PI * 0.5f) * static_cast<float>(r) /
                          static_cast<float>(hemisphereRings);
        const float ringRadius = radius * sinf(phi);
        const float centerY = halfH;
        const float ringY = centerY + radius * cosf(phi);
        for (int s = 0; s <= sectors; ++s)
        {
            const float theta = 2.0f * PI * static_cast<float>(s) /
                                static_cast<float>(sectors);
            const float nx = sinf(phi) * cosf(theta);
            const float ny = cosf(phi);
            const float nz = sinf(phi) * sinf(theta);
            vertices.emplace_back(ringRadius * cosf(theta), ringY,
                                  ringRadius * sinf(theta), nx, ny, nz,
                                  static_cast<float>(s) / sectors,
                                  0.5f * static_cast<float>(r) / hemisphereRings);
        }
    }

    for (int r = 1; r <= cylinderRings; ++r)
    {
        const float t = static_cast<float>(r) / cylinderRings;
        const float ringY = halfH - height * t;
        for (int s = 0; s <= sectors; ++s)
        {
            const float theta = 2.0f * PI * static_cast<float>(s) /
                                static_cast<float>(sectors);
            vertices.emplace_back(radius * cosf(theta), ringY,
                                  radius * sinf(theta), cosf(theta), 0.0f,
                                  sinf(theta), static_cast<float>(s) / sectors,
                                  0.5f + 0.25f * t);
        }
    }

    for (int r = 1; r <= hemisphereRings; ++r)
    {
        const float phi = PI * 0.5f + (PI * 0.5f) * static_cast<float>(r) /
                          static_cast<float>(hemisphereRings);
        const float ringRadius = radius * sinf(phi);
        const float ringY = -halfH + radius * cosf(phi);
        for (int s = 0; s <= sectors; ++s)
        {
            const float theta = 2.0f * PI * static_cast<float>(s) /
                                static_cast<float>(sectors);
            const float nx = sinf(phi) * cosf(theta);
            const float ny = cosf(phi);
            const float nz = sinf(phi) * sinf(theta);
            vertices.emplace_back(ringRadius * cosf(theta), ringY,
                                  ringRadius * sinf(theta), nx, ny, nz,
                                  static_cast<float>(s) / sectors,
                                  0.75f + 0.25f * static_cast<float>(r) /
                                  hemisphereRings);
        }
    }

    const int ringCount = static_cast<int>(vertices.size()) / stride;
    std::vector<Vertex> result;
    result.reserve((ringCount - 1) * sectors * 6);
    for (int r = 0; r < ringCount - 1; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            const int a = r * stride + s;
            const int b = a + 1;
            const int c = a + stride;
            const int d = c + 1;
            // Ordem externa: a,b,c e b,d,c. A ordem anterior invertia a
            // cápsula, fazendo o culling remover o lado de fora.
            result.push_back(vertices[a]);
            result.push_back(vertices[b]);
            result.push_back(vertices[c]);
            result.push_back(vertices[b]);
            result.push_back(vertices[d]);
            result.push_back(vertices[c]);
        }
    }
    return result;
}
