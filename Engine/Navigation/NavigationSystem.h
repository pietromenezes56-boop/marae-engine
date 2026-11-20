#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <vector>

class Scene;

struct NavigationBakeSettings
{
    float CellSize = 0.5f;
    float AgentRadius = 0.3f;
    float AgentHeight = 1.8f;
    float MaxStepHeight = 0.45f;
    std::size_t MaxCells = 250000;
};

struct NavigationPath
{
    bool Found = false;
    std::vector<glm::vec3> Points;
    float Length = 0.0f;
};

// Grid-baked navigation over static collider tops with collider-based clearance.
// Deliberately bounded: projects with oversized navigation bounds fail with a
// diagnostic rather than allocating an unbounded grid.
class NavigationSystem
{
public:
    bool Bake(const Scene& scene, const NavigationBakeSettings& settings = {});
    NavigationPath FindPath(const glm::vec3& start, const glm::vec3& destination) const;
    bool IsBaked() const { return m_Baked; }
    std::size_t GetWalkableCellCount() const { return m_WalkableCellCount; }
    const std::string& GetLastError() const { return m_LastError; }
    float GetCellSize() const { return m_Settings.CellSize; }

    static bool RunRegressionTests();

private:
    struct Cell
    {
        float Height = 0.0f;
        bool Walkable = false;
    };

    int ToIndex(int x, int z) const;
    bool FindNearestCell(const glm::vec3& position, int& outIndex) const;
    bool CanStep(int fromIndex, int toIndex) const;

    NavigationBakeSettings m_Settings;
    glm::vec2 m_MinXZ{0.0f};
    int m_Width = 0;
    int m_Height = 0;
    std::vector<Cell> m_Cells;
    std::size_t m_WalkableCellCount = 0;
    bool m_Baked = false;
    std::string m_LastError;
};
