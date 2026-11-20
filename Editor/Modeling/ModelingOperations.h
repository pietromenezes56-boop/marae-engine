#pragma once

#include "../../Renderer/Vertex/Vertex.h"
#include <vector>

namespace ModelingOperations
{
    // Todas as operações trabalham em uma lista triangulada. Isso mantém o
    // modo Modelador compatível com meshes importados e com os primitivos da
    // Marae sem introduzir um segundo formato de GPU.
    void RecalculateNormals(std::vector<Vertex>& vertices);
    void FlipNormals(std::vector<Vertex>& vertices);
    void Smooth(std::vector<Vertex>& vertices, float strength);
    void Inflate(std::vector<Vertex>& vertices, float distance);
    std::vector<Vertex> IncreaseResolution(const std::vector<Vertex>& vertices,
                                           int levels = 1);
    std::vector<Vertex> ReduceResolution(const std::vector<Vertex>& vertices,
                                         float strength = 0.15f);
    std::vector<Vertex> RebuildRadialResolution(const std::vector<Vertex>& vertices,
                                                int sectors);
    void RemoveDegenerate(std::vector<Vertex>& vertices, float epsilon = 0.000001f);
    void RemoveInternalFaces(std::vector<Vertex>& vertices);
    void Optimize(std::vector<Vertex>& vertices,
                  float positionTolerance = 0.00001f);

    std::vector<Vertex> Subdivide(const std::vector<Vertex>& vertices);
    std::vector<Vertex> ExtrudeFaces(const std::vector<Vertex>& vertices, float distance);
    std::vector<Vertex> InsetFaces(const std::vector<Vertex>& vertices, float amount);
    std::vector<Vertex> BevelFaces(const std::vector<Vertex>& vertices, float amount);
    std::vector<Vertex> Mirror(const std::vector<Vertex>& vertices, int axis);

    // Versões que respeitam os índices de faces selecionadas no Modelador.
    std::vector<Vertex> SubdivideSelected(const std::vector<Vertex>& vertices,
                                          const std::vector<int>& selectedFaces);
    std::vector<Vertex> ExtrudeFacesSelected(const std::vector<Vertex>& vertices,
                                              float distance,
                                              const std::vector<int>& selectedFaces);
    std::vector<Vertex> InsetFacesSelected(const std::vector<Vertex>& vertices,
                                            float amount,
                                            const std::vector<int>& selectedFaces);
    std::vector<Vertex> BevelFacesSelected(const std::vector<Vertex>& vertices,
                                            float amount,
                                            const std::vector<int>& selectedFaces);
    std::vector<Vertex> DeleteFacesSelected(const std::vector<Vertex>& vertices,
                                            const std::vector<int>& selectedFaces);
    std::vector<Vertex> InflateSelected(const std::vector<Vertex>& vertices,
                                        float distance,
                                        const std::vector<int>& selectedFaces);

    enum class MiniFaceShape
    {
        Squares = 0,
        Rectangles = 1,
        Triangles = 2
    };

    enum class MiniFaceOrientation
    {
        Grid = 0,
        Vertical = 1,
        Horizontal = 2
    };

    std::vector<Vertex> SubdivideMiniFaces(
        const std::vector<Vertex>& vertices,
        const std::vector<int>& selectedFaces,
        MiniFaceShape shape,
        MiniFaceOrientation orientation,
        int verticalLines,
        int horizontalLines);
}
