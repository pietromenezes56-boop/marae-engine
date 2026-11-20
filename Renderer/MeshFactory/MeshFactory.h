#pragma once

#include "../Vertex/Vertex.h"

#include <string>
#include <vector>

class MeshFactory
{
public:
    static std::vector<Vertex> CreateTriangle();

    static std::vector<Vertex> CreateQuad();

    // Formas preenchidas no plano XY para sprites e objetos 2D.
    static std::vector<Vertex> Create2DPrimitive(const std::string& primitiveType);

    static std::vector<Vertex> CreateCube();

    static std::vector<Vertex> CreateGrid();

    static std::vector<Vertex> CreateAxis();
    static std::vector<Vertex> CreateAxisX();
    static std::vector<Vertex> CreateAxisY();
    static std::vector<Vertex> CreateAxisZ();

    static std::vector<Vertex> CreateSphere(int sectors = 20, int stacks = 20);
    static std::vector<Vertex> CreateSunDisc(int segments = 32);
    static std::vector<Vertex> CreateCylinder(int sectors = 20, float height = 1.0f);
    static std::vector<Vertex> CreateCone(int sectors = 20, float height = 1.0f);
    static std::vector<Vertex> CreateSunHalo(int segments = 32);
    static std::vector<Vertex> CreatePuffyCloud();

    // Primitivas extras
    static std::vector<Vertex> CreatePlane(int subdivisions = 1);
    static std::vector<Vertex> CreateWedge();
    static std::vector<Vertex> CreateTorus(int sectors = 24, int sides = 12, float outerR = 0.5f, float innerR = 0.2f);
    static std::vector<Vertex> CreateCapsule(int sectors = 20, int stacks = 8, float radius = 0.5f, float height = 1.0f);
};
