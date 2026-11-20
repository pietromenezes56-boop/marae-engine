#pragma once

#include "../../Renderer/Vertex/Vertex.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Terrain 3D baseado em heightmap. A malha renderizada é uma representação
// derivada dos dados; o heightmap é a fonte persistente e editável.
class TerrainComponent
{
public:
    enum class SculptMode { Raise = 0, Lower = 1, Smooth = 2, Flatten = 3 };

    bool Enabled = false;
    int Resolution = 33;
    float Size = 32.0f;
    float HeightScale = 6.0f;
    float BrushRadius = 3.0f;
    float BrushStrength = 0.35f;
    SculptMode Mode = SculptMode::Raise;
    std::vector<float> Heights;
    std::string DataPath;

    void Initialize(int resolution = 33)
    {
        Resolution = std::clamp(resolution, 3, 257);
        if ((Resolution & 1) == 0) ++Resolution;
        Heights.assign(static_cast<size_t>(Resolution) * Resolution, 0.0f);
        Enabled = true;
    }

    bool IsValid() const
    {
        return Enabled && Resolution >= 3 &&
            Heights.size() == static_cast<size_t>(Resolution) * Resolution;
    }

    float& At(int x, int z)
    {
        return Heights[static_cast<size_t>(z) * Resolution + x];
    }

    float At(int x, int z) const
    {
        x = std::clamp(x, 0, Resolution - 1);
        z = std::clamp(z, 0, Resolution - 1);
        return Heights[static_cast<size_t>(z) * Resolution + x];
    }

    std::vector<Vertex> BuildVertices() const
    {
        std::vector<Vertex> vertices;
        if (!IsValid()) return vertices;
        const int cells = Resolution - 1;
        const float cell = Size / static_cast<float>(cells);
        vertices.reserve(static_cast<size_t>(cells) * cells * 6);

        auto height = [&](int x, int z) { return At(x, z) * HeightScale; };
        auto vertex = [&](int x, int z) {
            const float px = -Size * 0.5f + static_cast<float>(x) * cell;
            const float pz = -Size * 0.5f + static_cast<float>(z) * cell;
            const float left = height(x - 1, z), right = height(x + 1, z);
            const float back = height(x, z - 1), front = height(x, z + 1);
            glm::vec3 normal = glm::normalize(glm::vec3(left - right, 2.0f * cell, back - front));
            return Vertex(px, height(x, z), pz, normal.x, normal.y, normal.z,
                          static_cast<float>(x) / cells, static_cast<float>(z) / cells);
        };

        for (int z = 0; z < cells; ++z)
        {
            for (int x = 0; x < cells; ++x)
            {
                const Vertex a = vertex(x, z);
                const Vertex b = vertex(x + 1, z);
                const Vertex c = vertex(x + 1, z + 1);
                const Vertex d = vertex(x, z + 1);
                vertices.push_back(a); vertices.push_back(c); vertices.push_back(b);
                vertices.push_back(a); vertices.push_back(d); vertices.push_back(c);
            }
        }
        return vertices;
    }

    bool SculptAt(float worldX, float worldZ, float deltaSeconds = 1.0f)
    {
        if (!IsValid() || Size <= 0.001f) return false;
        const float half = Size * 0.5f;
        const float fx = (worldX + half) / Size * (Resolution - 1);
        const float fz = (worldZ + half) / Size * (Resolution - 1);
        const float radiusSamples = std::max(0.5f, BrushRadius / Size * (Resolution - 1));
        const int minX = std::max(0, static_cast<int>(std::floor(fx - radiusSamples)));
        const int maxX = std::min(Resolution - 1, static_cast<int>(std::ceil(fx + radiusSamples)));
        const int minZ = std::max(0, static_cast<int>(std::floor(fz - radiusSamples)));
        const int maxZ = std::min(Resolution - 1, static_cast<int>(std::ceil(fz + radiusSamples)));
        const float amount = std::clamp(BrushStrength * std::max(0.0f, deltaSeconds), 0.0f, 1.0f);
        bool changed = false;

        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float distance = std::sqrt((x - fx) * (x - fx) + (z - fz) * (z - fz));
                if (distance > radiusSamples) continue;
                const float t = distance / radiusSamples;
                const float falloff = 1.0f - (t * t * (3.0f - 2.0f * t));
                const float before = At(x, z);
                float target = before;
                if (Mode == SculptMode::Raise) target += amount * falloff;
                else if (Mode == SculptMode::Lower) target -= amount * falloff;
                else if (Mode == SculptMode::Flatten) target = 0.0f;
                else
                {
                    float sum = 0.0f; int count = 0;
                    for (int nz = std::max(0, z - 1); nz <= std::min(Resolution - 1, z + 1); ++nz)
                        for (int nx = std::max(0, x - 1); nx <= std::min(Resolution - 1, x + 1); ++nx)
                        { sum += At(nx, nz); ++count; }
                    target = before + (sum / std::max(1, count) - before) * amount * falloff;
                }
                At(x, z) = std::clamp(target, -1.0f, 1.0f);
                changed |= std::abs(At(x, z) - before) > 0.000001f;
            }
        }
        return changed;
    }

    bool Save(const std::filesystem::path& path) const
    {
        if (!IsValid()) return false;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file.precision(9);
        file << "MaraeTerrain 1 " << Resolution << ' ' << Size << ' ' << HeightScale << "\n";
        for (float value : Heights) file << value << '\n';
        return static_cast<bool>(file);
    }

    bool Load(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        std::string magic; int version = 0;
        file >> magic >> version >> Resolution >> Size >> HeightScale;
        if (magic != "MaraeTerrain" || version != 1 || Resolution < 3 || Resolution > 257)
            return false;
        Heights.resize(static_cast<size_t>(Resolution) * Resolution);
        for (float& value : Heights) file >> value;
        Enabled = file.good() || file.eof();
        return Enabled;
    }
};
