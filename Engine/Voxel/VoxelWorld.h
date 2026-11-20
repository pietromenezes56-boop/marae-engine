#pragma once

#include "../../Math/Vector3.h"
#include "../../Renderer/Mesh/Mesh.h"
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class VoxelWorld
{
public:
    static constexpr int ChunkSize = 16;
    static constexpr int ChunkHeight = 32;

    enum class BlockType : uint8_t
    {
        Air = 0,
        Grass,
        Dirt,
        Stone,
        Wood,
        Leaves,
        Apple
    };

    struct RenderPart
    {
        std::shared_ptr<Mesh> MeshData;
        Vector3 Color;
        int ChunkX = 0;
        int ChunkZ = 0;
        BlockType Type = BlockType::Air;
    };

    struct Hit
    {
        bool HitBlock = false;
        int X = 0;
        int Y = 0;
        int Z = 0;
        int PreviousX = 0;
        int PreviousY = 0;
        int PreviousZ = 0;
        float Distance = 0.0f;
        BlockType Type = BlockType::Air;
    };

    void Clear();
    void ClearRenderMeshes();
    bool IsEmpty() const;

    void Generate(int seed, int chunkRadius = 2);
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    void BuildMeshes();

    BlockType GetBlock(int x, int y, int z) const;
    bool SetBlock(int x, int y, int z, BlockType type);
    int GetSurfaceHeight(int x, int z) const;
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
                 float maxDistance, Hit& result) const;

    const std::vector<RenderPart>& GetRenderParts() const
    {
        return m_RenderParts;
    }

    int GetSeed() const
    {
        return m_Seed;
    }

    static Vector3 GetBlockColor(BlockType type);
    static bool IsSolid(BlockType type)
    {
        return type != BlockType::Air;
    }

private:
    struct Chunk
    {
        int X = 0;
        int Z = 0;
        std::vector<uint8_t> Blocks;
    };

    Chunk* FindChunk(int chunkX, int chunkZ);
    const Chunk* FindChunk(int chunkX, int chunkZ) const;
    void SetBlockUnchecked(Chunk& chunk, int localX, int y, int localZ,
                           BlockType type);
    static int FloorDiv(int value, int divisor);
    static int PositiveMod(int value, int divisor);
    static uint32_t Hash(int x, int z, int seed);
    static void AddFace(std::vector<Vertex>& vertices, int face,
                        float x, float y, float z);

    int m_Seed = 0;
    std::vector<Chunk> m_Chunks;
    std::vector<RenderPart> m_RenderParts;
};
