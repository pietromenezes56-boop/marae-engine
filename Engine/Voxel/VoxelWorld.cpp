#include "VoxelWorld.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

namespace
{
    constexpr uint32_t VOXEL_MAGIC = 0x4D56584Cull; // MVXL
    constexpr uint32_t VOXEL_VERSION = 1;

    size_t BlockIndex(int x, int y, int z)
    {
        return static_cast<size_t>(y * VoxelWorld::ChunkSize * VoxelWorld::ChunkSize +
                                   z * VoxelWorld::ChunkSize + x);
    }
}

void VoxelWorld::Clear()
{
    m_Chunks.clear();
    m_RenderParts.clear();
    m_Seed = 0;
}

void VoxelWorld::ClearRenderMeshes()
{
    m_RenderParts.clear();
}

bool VoxelWorld::IsEmpty() const
{
    return m_Chunks.empty();
}

int VoxelWorld::FloorDiv(int value, int divisor)
{
    if (value >= 0)
        return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

int VoxelWorld::PositiveMod(int value, int divisor)
{
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

uint32_t VoxelWorld::Hash(int x, int z, int seed)
{
    uint32_t value = static_cast<uint32_t>(x) * 374761393u;
    value += static_cast<uint32_t>(z) * 668265263u;
    value += static_cast<uint32_t>(seed) * 2246822519u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

VoxelWorld::Chunk* VoxelWorld::FindChunk(int chunkX, int chunkZ)
{
    for (Chunk& chunk : m_Chunks)
    {
        if (chunk.X == chunkX && chunk.Z == chunkZ)
            return &chunk;
    }
    return nullptr;
}

const VoxelWorld::Chunk* VoxelWorld::FindChunk(int chunkX, int chunkZ) const
{
    for (const Chunk& chunk : m_Chunks)
    {
        if (chunk.X == chunkX && chunk.Z == chunkZ)
            return &chunk;
    }
    return nullptr;
}

void VoxelWorld::SetBlockUnchecked(Chunk& chunk, int localX, int y, int localZ,
                                   BlockType type)
{
    if (localX < 0 || localX >= ChunkSize || localZ < 0 || localZ >= ChunkSize ||
        y < 0 || y >= ChunkHeight)
    {
        return;
    }
    chunk.Blocks[BlockIndex(localX, y, localZ)] = static_cast<uint8_t>(type);
}

VoxelWorld::BlockType VoxelWorld::GetBlock(int x, int y, int z) const
{
    if (y < 0 || y >= ChunkHeight)
        return BlockType::Air;

    const int chunkX = FloorDiv(x, ChunkSize);
    const int chunkZ = FloorDiv(z, ChunkSize);
    const Chunk* chunk = FindChunk(chunkX, chunkZ);
    if (!chunk)
        return BlockType::Air;

    const int localX = PositiveMod(x, ChunkSize);
    const int localZ = PositiveMod(z, ChunkSize);
    return static_cast<BlockType>(chunk->Blocks[BlockIndex(localX, y, localZ)]);
}

bool VoxelWorld::SetBlock(int x, int y, int z, BlockType type)
{
    if (y < 0 || y >= ChunkHeight)
        return false;

    const int chunkX = FloorDiv(x, ChunkSize);
    const int chunkZ = FloorDiv(z, ChunkSize);
    Chunk* chunk = FindChunk(chunkX, chunkZ);
    if (!chunk)
        return false;

    SetBlockUnchecked(*chunk, PositiveMod(x, ChunkSize), y,
                      PositiveMod(z, ChunkSize), type);
    return true;
}

int VoxelWorld::GetSurfaceHeight(int x, int z) const
{
    for (int y = ChunkHeight - 1; y >= 0; --y)
    {
        if (IsSolid(GetBlock(x, y, z)))
            return y + 1;
    }
    return 1;
}

void VoxelWorld::Generate(int seed, int chunkRadius)
{
    Clear();
    m_Seed = seed;

    for (int chunkZ = -chunkRadius; chunkZ <= chunkRadius; ++chunkZ)
    {
        for (int chunkX = -chunkRadius; chunkX <= chunkRadius; ++chunkX)
        {
            Chunk chunk;
            chunk.X = chunkX;
            chunk.Z = chunkZ;
            chunk.Blocks.assign(ChunkSize * ChunkSize * ChunkHeight,
                                static_cast<uint8_t>(BlockType::Air));
            m_Chunks.push_back(std::move(chunk));
        }
    }

    for (Chunk& chunk : m_Chunks)
    {
        for (int localZ = 0; localZ < ChunkSize; ++localZ)
        {
            for (int localX = 0; localX < ChunkSize; ++localX)
            {
                const int worldX = chunk.X * ChunkSize + localX;
                const int worldZ = chunk.Z * ChunkSize + localZ;
                const uint32_t noise = Hash(worldX, worldZ, m_Seed);
                const int height = 4 + static_cast<int>(noise % 4u);

                for (int y = 0; y <= height; ++y)
                {
                    const BlockType type = y == height
                        ? BlockType::Grass
                        : (y >= height - 2 ? BlockType::Dirt : BlockType::Stone);
                    SetBlockUnchecked(chunk, localX, y, localZ, type);
                }

                // Árvores são parte do dado voxel, não entidades soltas.
                if (height + 5 < ChunkHeight &&
                    localX >= 3 && localX <= 12 && localZ >= 3 && localZ <= 12 &&
                    Hash(worldX + 991, worldZ - 431, m_Seed) % 37u == 0u)
                {
                    for (int y = height + 1; y <= height + 3; ++y)
                        SetBlockUnchecked(chunk, localX, y, localZ, BlockType::Wood);

                    for (int dz = -2; dz <= 2; ++dz)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            if (std::abs(dx) + std::abs(dz) <= 3)
                            {
                                const int leafY = height + 3;
                                SetBlockUnchecked(chunk, localX + dx, leafY,
                                                  localZ + dz, BlockType::Leaves);
                            }
                        }
                    }

                    if (Hash(worldX + 13, worldZ + 29, m_Seed) % 3u == 0u)
                        SetBlockUnchecked(chunk, localX + 1, height + 2,
                                          localZ, BlockType::Apple);
                }
            }
        }
    }
}

bool VoxelWorld::Save(const std::string& path) const
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    const uint32_t chunkCount = static_cast<uint32_t>(m_Chunks.size());
    file.write(reinterpret_cast<const char*>(&VOXEL_MAGIC), sizeof(VOXEL_MAGIC));
    file.write(reinterpret_cast<const char*>(&VOXEL_VERSION), sizeof(VOXEL_VERSION));
    file.write(reinterpret_cast<const char*>(&m_Seed), sizeof(m_Seed));
    file.write(reinterpret_cast<const char*>(&chunkCount), sizeof(chunkCount));

    for (const Chunk& chunk : m_Chunks)
    {
        file.write(reinterpret_cast<const char*>(&chunk.X), sizeof(chunk.X));
        file.write(reinterpret_cast<const char*>(&chunk.Z), sizeof(chunk.Z));
        file.write(reinterpret_cast<const char*>(chunk.Blocks.data()),
                   static_cast<std::streamsize>(chunk.Blocks.size()));
    }
    return file.good();
}

bool VoxelWorld::Load(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t chunkCount = 0;
    int seed = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    file.read(reinterpret_cast<char*>(&chunkCount), sizeof(chunkCount));
    if (!file.good() || magic != VOXEL_MAGIC || version != VOXEL_VERSION ||
        chunkCount > 4096)
    {
        return false;
    }

    std::vector<Chunk> loaded;
    loaded.reserve(chunkCount);
    const size_t blockCount = ChunkSize * ChunkSize * ChunkHeight;
    for (uint32_t i = 0; i < chunkCount; ++i)
    {
        Chunk chunk;
        chunk.Blocks.resize(blockCount);
        file.read(reinterpret_cast<char*>(&chunk.X), sizeof(chunk.X));
        file.read(reinterpret_cast<char*>(&chunk.Z), sizeof(chunk.Z));
        file.read(reinterpret_cast<char*>(chunk.Blocks.data()),
                  static_cast<std::streamsize>(chunk.Blocks.size()));
        if (!file.good())
            return false;
        loaded.push_back(std::move(chunk));
    }

    m_Seed = seed;
    m_Chunks = std::move(loaded);
    m_RenderParts.clear();
    return !m_Chunks.empty();
}

Vector3 VoxelWorld::GetBlockColor(BlockType type)
{
    switch (type)
    {
    case BlockType::Grass:  return Vector3(0.20f, 0.55f, 0.16f);
    case BlockType::Dirt:   return Vector3(0.38f, 0.20f, 0.08f);
    case BlockType::Stone:  return Vector3(0.35f, 0.38f, 0.42f);
    case BlockType::Wood:   return Vector3(0.42f, 0.22f, 0.08f);
    case BlockType::Leaves: return Vector3(0.08f, 0.42f, 0.12f);
    case BlockType::Apple:  return Vector3(0.88f, 0.04f, 0.03f);
    default:                return Vector3(1.0f, 1.0f, 1.0f);
    }
}

void VoxelWorld::AddFace(std::vector<Vertex>& vertices, int face,
                         float x, float y, float z)
{
    static const glm::vec3 normals[] = {
        { 0.0f,  1.0f,  0.0f }, { 0.0f, -1.0f,  0.0f },
        { 1.0f,  0.0f,  0.0f }, {-1.0f,  0.0f,  0.0f },
        { 0.0f,  0.0f,  1.0f }, { 0.0f,  0.0f, -1.0f }
    };
    static const glm::vec3 corners[6][4] = {
        {{0,1,0},{1,1,0},{1,1,1},{0,1,1}},
        {{0,0,1},{1,0,1},{1,0,0},{0,0,0}},
        {{1,0,0},{1,0,1},{1,1,1},{1,1,0}},
        {{0,0,1},{0,0,0},{0,1,0},{0,1,1}},
        {{1,0,1},{0,0,1},{0,1,1},{1,1,1}},
        {{0,0,0},{1,0,0},{1,1,0},{0,1,0}}
    };
    const glm::vec3 n = normals[face];
    const glm::vec2 uv[] = {{0,0},{1,0},{1,1},{0,1}};
    const int order[] = {0, 1, 2, 0, 2, 3};
    for (int index : order)
    {
        const glm::vec3 p = corners[face][index];
        vertices.emplace_back(x + p.x, y + p.y, z + p.z,
                              n.x, n.y, n.z, uv[index].x, uv[index].y);
    }
}

void VoxelWorld::BuildMeshes()
{
    m_RenderParts.clear();
    const BlockType meshTypes[] = {
        BlockType::Grass, BlockType::Dirt, BlockType::Stone,
        BlockType::Wood, BlockType::Leaves, BlockType::Apple
    };

    for (const Chunk& chunk : m_Chunks)
    {
        for (BlockType type : meshTypes)
        {
            std::vector<Vertex> vertices;
            for (int y = 0; y < ChunkHeight; ++y)
            {
                for (int z = 0; z < ChunkSize; ++z)
                {
                    for (int x = 0; x < ChunkSize; ++x)
                    {
                        if (static_cast<BlockType>(chunk.Blocks[BlockIndex(x, y, z)]) != type)
                            continue;

                        const int worldX = chunk.X * ChunkSize + x;
                        const int worldZ = chunk.Z * ChunkSize + z;
                        const int neighborX[] = { worldX, worldX, worldX + 1,
                                                  worldX - 1, worldX, worldX };
                        const int neighborY[] = { y + 1, y - 1, y, y, y, y };
                        const int neighborZ[] = { worldZ, worldZ, worldZ,
                                                  worldZ, worldZ + 1, worldZ - 1 };
                        for (int face = 0; face < 6; ++face)
                        {
                            if (!IsSolid(GetBlock(neighborX[face], neighborY[face],
                                                   neighborZ[face])))
                            {
                                AddFace(vertices, face, static_cast<float>(x),
                                        static_cast<float>(y), static_cast<float>(z));
                            }
                        }
                    }
                }
            }

            if (vertices.empty())
                continue;

            RenderPart part;
            part.MeshData = std::make_shared<Mesh>();
            part.MeshData->Create(vertices, GL_TRIANGLES, "VoxelWorld::Chunk");
            part.Color = GetBlockColor(type);
            part.ChunkX = chunk.X;
            part.ChunkZ = chunk.Z;
            part.Type = type;
            m_RenderParts.push_back(std::move(part));
        }
    }
}

bool VoxelWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                         float maxDistance, Hit& result) const
{
    result = Hit();
    if (glm::length(direction) < 0.0001f)
        return false;

    const glm::vec3 dir = glm::normalize(direction);
    glm::ivec3 cell = glm::ivec3(glm::floor(origin));
    glm::ivec3 previous = cell;
    const glm::ivec3 step(
        dir.x >= 0.0f ? 1 : -1,
        dir.y >= 0.0f ? 1 : -1,
        dir.z >= 0.0f ? 1 : -1);

    const glm::vec3 tDelta(
        std::abs(dir.x) > 0.0001f ? std::abs(1.0f / dir.x) : 1e30f,
        std::abs(dir.y) > 0.0001f ? std::abs(1.0f / dir.y) : 1e30f,
        std::abs(dir.z) > 0.0001f ? std::abs(1.0f / dir.z) : 1e30f);
    const glm::vec3 nextBoundary(
        dir.x >= 0.0f ? static_cast<float>(cell.x + 1) : static_cast<float>(cell.x),
        dir.y >= 0.0f ? static_cast<float>(cell.y + 1) : static_cast<float>(cell.y),
        dir.z >= 0.0f ? static_cast<float>(cell.z + 1) : static_cast<float>(cell.z));
    glm::vec3 tMax(
        std::abs(dir.x) > 0.0001f ? (nextBoundary.x - origin.x) / dir.x : 1e30f,
        std::abs(dir.y) > 0.0001f ? (nextBoundary.y - origin.y) / dir.y : 1e30f,
        std::abs(dir.z) > 0.0001f ? (nextBoundary.z - origin.z) / dir.z : 1e30f);

    float distance = 0.0f;
    for (int iteration = 0; iteration < 256 && distance <= maxDistance; ++iteration)
    {
        const BlockType block = GetBlock(cell.x, cell.y, cell.z);
        if (IsSolid(block))
        {
            result.HitBlock = true;
            result.X = cell.x;
            result.Y = cell.y;
            result.Z = cell.z;
            result.PreviousX = previous.x;
            result.PreviousY = previous.y;
            result.PreviousZ = previous.z;
            result.Distance = distance;
            result.Type = block;
            return true;
        }

        previous = cell;
        if (tMax.x < tMax.y && tMax.x < tMax.z)
        {
            distance = tMax.x;
            tMax.x += tDelta.x;
            cell.x += step.x;
        }
        else if (tMax.y < tMax.z)
        {
            distance = tMax.y;
            tMax.y += tDelta.y;
            cell.y += step.y;
        }
        else
        {
            distance = tMax.z;
            tMax.z += tDelta.z;
            cell.z += step.z;
        }
    }
    return false;
}
