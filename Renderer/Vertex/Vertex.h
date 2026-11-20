#pragma once

#include <cstdint>

struct Vertex
{
    float X;
    float Y;
    float Z;

    float NX;
    float NY;
    float NZ;

    float U;
    float V;

    uint16_t JointIndices[4]{};
    float JointWeights[4]{};

    Vertex(
        float x,
        float y,
        float z,

        float nx,
        float ny,
        float nz,

        float u,
        float v
    )
    {
        X = x;
        Y = y;
        Z = z;

        NX = nx;
        NY = ny;
        NZ = nz;

        U = u;
        V = v;
    }

    Vertex() = default;
};
