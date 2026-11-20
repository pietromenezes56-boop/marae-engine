#include "NavigationSystem.h"

#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace
{
    struct Bounds
    {
        glm::vec3 Min;
        glm::vec3 Max;
    };

    Bounds GetEntityBounds(const Entity& entity)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        glm::vec3 position(transform.Position.X, transform.Position.Y, transform.Position.Z);
        glm::vec3 half(std::abs(transform.Scale.X), std::abs(transform.Scale.Y), std::abs(transform.Scale.Z));
        half *= 0.5f;

        // Rotate the local half extents into a conservative world AABB.
        const glm::mat3 rotation = glm::mat3_cast(transform.Rotation);
        glm::vec3 worldHalf(
            std::abs(rotation[0][0]) * half.x + std::abs(rotation[1][0]) * half.y + std::abs(rotation[2][0]) * half.z,
            std::abs(rotation[0][1]) * half.x + std::abs(rotation[1][1]) * half.y + std::abs(rotation[2][1]) * half.z,
            std::abs(rotation[0][2]) * half.x + std::abs(rotation[1][2]) * half.y + std::abs(rotation[2][2]) * half.z);
        return { position - worldHalf, position + worldHalf };
    }

    float Heuristic(int ax, int az, int bx, int bz, float cellSize)
    {
        const int dx = std::abs(ax - bx);
        const int dz = std::abs(az - bz);
        const int diagonal = std::min(dx, dz);
        const int straight = std::max(dx, dz) - diagonal;
        return (diagonal * 1.41421356f + straight) * cellSize;
    }
}

int NavigationSystem::ToIndex(int x, int z) const
{
    return z * m_Width + x;
}

bool NavigationSystem::Bake(const Scene& scene, const NavigationBakeSettings& requested)
{
    m_Baked = false;
    m_Cells.clear();
    m_WalkableCellCount = 0;
    m_LastError.clear();
    m_Settings = requested;
    m_Settings.CellSize = std::clamp(m_Settings.CellSize, 0.1f, 10.0f);
    m_Settings.AgentRadius = std::clamp(m_Settings.AgentRadius, 0.05f, 10.0f);
    m_Settings.AgentHeight = std::clamp(m_Settings.AgentHeight, 0.2f, 100.0f);
    m_Settings.MaxStepHeight = std::clamp(m_Settings.MaxStepHeight, 0.0f, 10.0f);
    m_Settings.MaxCells = std::clamp<std::size_t>(m_Settings.MaxCells, 256, 1000000);

    std::vector<Bounds> floorBounds;
    float minX = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
    for (const Entity& entity : scene.GetEntities())
    {
        if (!entity.Active || entity.IsEditorOnly() || !entity.PhysicsEnabled ||
            !entity.ColliderEnabled || !entity.IsStatic || entity.HasUI())
            continue;
        const Bounds bounds = GetEntityBounds(entity);
        if (bounds.Max.x - bounds.Min.x < m_Settings.AgentRadius * 2.0f ||
            bounds.Max.z - bounds.Min.z < m_Settings.AgentRadius * 2.0f)
            continue;
        floorBounds.push_back(bounds);
        minX = std::min(minX, bounds.Min.x);
        minZ = std::min(minZ, bounds.Min.z);
        maxX = std::max(maxX, bounds.Max.x);
        maxZ = std::max(maxZ, bounds.Max.z);
    }
    if (floorBounds.empty())
    {
        m_LastError = "Bake falhou: adicione ao menos um colisor estático para servir como piso navegável.";
        return false;
    }

    const float padding = m_Settings.AgentRadius + m_Settings.CellSize;
    minX -= padding; minZ -= padding; maxX += padding; maxZ += padding;
    m_MinXZ = glm::vec2(minX, minZ);
    m_Width = static_cast<int>(std::ceil((maxX - minX) / m_Settings.CellSize));
    m_Height = static_cast<int>(std::ceil((maxZ - minZ) / m_Settings.CellSize));
    if (m_Width <= 0 || m_Height <= 0 ||
        static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height) > m_Settings.MaxCells)
    {
        m_LastError = "Bake falhou: área navegável excede o limite de células. Aumente o tamanho da célula ou reduza a área.";
        return false;
    }
    m_Cells.resize(static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height));

    const auto& entities = scene.GetEntities();
    for (int z = 0; z < m_Height; ++z)
    {
        for (int x = 0; x < m_Width; ++x)
        {
            const float cx = minX + (static_cast<float>(x) + 0.5f) * m_Settings.CellSize;
            const float cz = minZ + (static_cast<float>(z) + 0.5f) * m_Settings.CellSize;
            float floorY = -std::numeric_limits<float>::max();
            const Bounds* support = nullptr;
            for (const Bounds& bounds : floorBounds)
            {
                if (cx >= bounds.Min.x + m_Settings.AgentRadius && cx <= bounds.Max.x - m_Settings.AgentRadius &&
                    cz >= bounds.Min.z + m_Settings.AgentRadius && cz <= bounds.Max.z - m_Settings.AgentRadius &&
                    bounds.Max.y > floorY)
                {
                    floorY = bounds.Max.y;
                    support = &bounds;
                }
            }
            if (!support)
                continue;

            bool blocked = false;
            for (const Entity& entity : entities)
            {
                if (!entity.Active || entity.IsEditorOnly() || !entity.PhysicsEnabled ||
                    !entity.ColliderEnabled || entity.HasUI())
                    continue;
                const Bounds bounds = GetEntityBounds(entity);
                const bool overlapsCell = cx + m_Settings.AgentRadius > bounds.Min.x &&
                    cx - m_Settings.AgentRadius < bounds.Max.x &&
                    cz + m_Settings.AgentRadius > bounds.Min.z &&
                    cz - m_Settings.AgentRadius < bounds.Max.z;
                const bool blocksBody = bounds.Max.y > floorY + 0.05f &&
                    bounds.Min.y < floorY + m_Settings.AgentHeight;
                if (overlapsCell && blocksBody)
                {
                    blocked = true;
                    break;
                }
            }
            Cell& cell = m_Cells[static_cast<std::size_t>(ToIndex(x, z))];
            cell.Height = floorY;
            cell.Walkable = !blocked;
            if (cell.Walkable)
                ++m_WalkableCellCount;
        }
    }

    if (m_WalkableCellCount == 0)
    {
        m_LastError = "Bake concluiu sem células livres. Verifique altura do agente, colisores e escala do piso.";
        m_Cells.clear();
        return false;
    }
    m_Baked = true;
    return true;
}

bool NavigationSystem::FindNearestCell(const glm::vec3& position, int& outIndex) const
{
    if (!m_Baked || m_Width <= 0 || m_Height <= 0)
        return false;
    const int centerX = static_cast<int>(std::floor((position.x - m_MinXZ.x) / m_Settings.CellSize));
    const int centerZ = static_cast<int>(std::floor((position.z - m_MinXZ.y) / m_Settings.CellSize));
    float bestScore = std::numeric_limits<float>::max();
    int bestIndex = -1;
    const int searchRadius = std::max(m_Width, m_Height);
    for (int radius = 0; radius <= searchRadius; ++radius)
    {
        const int minCellX = std::max(0, centerX - radius);
        const int maxCellX = std::min(m_Width - 1, centerX + radius);
        const int minCellZ = std::max(0, centerZ - radius);
        const int maxCellZ = std::min(m_Height - 1, centerZ + radius);
        for (int z = minCellZ; z <= maxCellZ; ++z)
        {
            for (int x = minCellX; x <= maxCellX; ++x)
            {
                const Cell& cell = m_Cells[static_cast<std::size_t>(ToIndex(x, z))];
                if (!cell.Walkable) continue;
                const glm::vec3 center(m_MinXZ.x + (x + 0.5f) * m_Settings.CellSize,
                    cell.Height, m_MinXZ.y + (z + 0.5f) * m_Settings.CellSize);
                const float dx = center.x - position.x;
                const float dz = center.z - position.z;
                const float score = dx * dx + dz * dz +
                    std::abs(center.y - position.y) * std::abs(center.y - position.y) * 2.0f;
                if (score < bestScore) { bestScore = score; bestIndex = ToIndex(x, z); }
            }
        }
        if (bestIndex >= 0 && radius * m_Settings.CellSize > std::sqrt(bestScore))
            break;
    }
    outIndex = bestIndex;
    return bestIndex >= 0;
}

bool NavigationSystem::CanStep(int fromIndex, int toIndex) const
{
    const Cell& from = m_Cells[static_cast<std::size_t>(fromIndex)];
    const Cell& to = m_Cells[static_cast<std::size_t>(toIndex)];
    return from.Walkable && to.Walkable && std::abs(from.Height - to.Height) <= m_Settings.MaxStepHeight;
}

NavigationPath NavigationSystem::FindPath(const glm::vec3& start, const glm::vec3& destination) const
{
    NavigationPath result;
    int startIndex = -1, goalIndex = -1;
    if (!FindNearestCell(start, startIndex) || !FindNearestCell(destination, goalIndex))
        return result;

    const int goalX = goalIndex % m_Width, goalZ = goalIndex / m_Width;
    const std::size_t count = m_Cells.size();
    std::vector<float> gScore(count, std::numeric_limits<float>::max());
    std::vector<int> parent(count, -1);
    std::vector<bool> closed(count, false);
    using QueueEntry = std::pair<float, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;
    gScore[static_cast<std::size_t>(startIndex)] = 0.0f;
    open.push({0.0f, startIndex});
    constexpr int offsets[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};

    while (!open.empty())
    {
        const int current = open.top().second;
        open.pop();
        if (closed[static_cast<std::size_t>(current)]) continue;
        if (current == goalIndex) break;
        closed[static_cast<std::size_t>(current)] = true;
        const int x = current % m_Width, z = current / m_Width;
        for (const auto& offset : offsets)
        {
            const int nx = x + offset[0], nz = z + offset[1];
            if (nx < 0 || nz < 0 || nx >= m_Width || nz >= m_Height) continue;
            const int next = ToIndex(nx, nz);
            if (closed[static_cast<std::size_t>(next)] || !CanStep(current, next)) continue;
            if (offset[0] != 0 && offset[1] != 0 &&
                (!CanStep(current, ToIndex(x + offset[0], z)) || !CanStep(current, ToIndex(x, z + offset[1]))) )
                continue;
            const float horizontal = offset[0] != 0 && offset[1] != 0 ? 1.41421356f : 1.0f;
            const float vertical = std::abs(m_Cells[static_cast<std::size_t>(current)].Height - m_Cells[static_cast<std::size_t>(next)].Height);
            const float tentative = gScore[static_cast<std::size_t>(current)] +
                m_Settings.CellSize * horizontal + vertical * 1.5f;
            if (tentative >= gScore[static_cast<std::size_t>(next)]) continue;
            parent[static_cast<std::size_t>(next)] = current;
            gScore[static_cast<std::size_t>(next)] = tentative;
            const float h = Heuristic(nx, nz, goalX, goalZ, m_Settings.CellSize);
            open.push({tentative + h, next});
        }
    }

    if (startIndex != goalIndex && parent[static_cast<std::size_t>(goalIndex)] < 0)
        return result;
    std::vector<int> reversePath;
    for (int current = goalIndex; current >= 0; current = parent[static_cast<std::size_t>(current)])
    {
        reversePath.push_back(current);
        if (current == startIndex) break;
    }
    if (reversePath.empty() || reversePath.back() != startIndex)
        return result;
    std::reverse(reversePath.begin(), reversePath.end());
    for (int index : reversePath)
    {
        const int x = index % m_Width, z = index / m_Width;
        const Cell& cell = m_Cells[static_cast<std::size_t>(index)];
        glm::vec3 point(m_MinXZ.x + (x + 0.5f) * m_Settings.CellSize,
            cell.Height, m_MinXZ.y + (z + 0.5f) * m_Settings.CellSize);
        if (!result.Points.empty()) result.Length += glm::distance(result.Points.back(), point);
        result.Points.push_back(point);
    }
    result.Found = !result.Points.empty();
    return result;
}

bool NavigationSystem::RunRegressionTests()
{
    Scene scene;
    Entity& floor = scene.CreateEntity();
    floor.SetName("NavTestFloor");
    floor.IsStatic = true;
    floor.GetTransform().Position = Vector3(0.0f, -0.5f, 0.0f);
    floor.GetTransform().Scale = Vector3(12.0f, 1.0f, 12.0f);
    Entity& wall = scene.CreateEntity();
    wall.SetName("NavTestWall");
    wall.IsStatic = true;
    wall.GetTransform().Position = Vector3(0.0f, 1.5f, 0.0f);
    wall.GetTransform().Scale = Vector3(1.0f, 3.0f, 6.0f);

    NavigationBakeSettings settings;
    settings.CellSize = 0.25f;
    NavigationSystem navigation;
    if (!navigation.Bake(scene, settings)) return false;
    const NavigationPath path = navigation.FindPath(glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    if (!path.Found || path.Points.size() < 3 || navigation.GetWalkableCellCount() == 0) return false;
    for (const glm::vec3& point : path.Points)
    {
        const bool intersectsWall = point.x > -0.9f && point.x < 0.9f && point.z > -3.1f && point.z < 3.1f;
        if (intersectsWall) return false;
    }
    return true;
}
