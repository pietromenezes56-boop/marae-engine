#pragma once

#include "../../Renderer/Vertex/Vertex.h"
#include <array>
#include <cstdint>
#include <vector>

// Representação editável da malha usada pelo Modelador. O renderer continua
// recebendo uma lista triangulada, mas as ferramentas trabalham com vértices
// compartilhados e faces explícitas. Isso evita que uma edição de topologia
// fique limitada a triângulos independentes.
class MeshTopology
{
public:
    struct VertexData
    {
        Vertex Value{};
    };

    struct Face
    {
        std::array<uint32_t, 3> Vertices{};
    };

    struct Edge
    {
        uint32_t A = 0;
        uint32_t B = 0;
        std::vector<uint32_t> Faces;
    };

    static MeshTopology FromVertices(const std::vector<Vertex>& vertices,
                                     float positionTolerance = 0.00001f);

    std::vector<Vertex> ToVertices(bool smoothShading = false) const;

    std::vector<Edge> GetEdges() const;
    std::vector<uint32_t> GetBoundaryEdges() const;

    void RecalculateNormals(bool smoothShading = false);
    void RemoveDegenerate(float epsilon = 0.000001f);
    bool IsValid() const;

    std::vector<VertexData> Vertices;
    std::vector<Face> Faces;
};
