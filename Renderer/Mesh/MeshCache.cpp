#include "MeshCache.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include "../../Engine/Assets/ModelImporter/ModelImporter.h"
#include <glad/glad.h>

#include <filesystem>
#include <iostream>


static std::shared_ptr<Mesh> s_CubeMesh = nullptr;
std::shared_ptr<Mesh> MeshCache::s_QuadMesh = nullptr;

std::unordered_map<std::string, std::weak_ptr<Mesh>> MeshCache::s_Meshes;

std::string MeshCache::NormalizePath(const std::string& path)
{
    std::filesystem::path filesystemPath(path);
    std::error_code error;

    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(filesystemPath, error);

    if (error)
        normalized = filesystemPath.lexically_normal();

    return normalized.generic_string();
}

std::shared_ptr<Mesh> MeshCache::GetCube()
{
    if (!s_CubeMesh)
    {
        s_CubeMesh = std::make_shared<Mesh>();
        s_CubeMesh->Create(
            MeshFactory::CreateCube(),
            GL_TRIANGLES,
            "MeshCache::Cube"
        );
    }

    return s_CubeMesh;
}

std::shared_ptr<Mesh> MeshCache::GetQuad()
{
    if (!s_QuadMesh)
    {
        s_QuadMesh = std::make_shared<Mesh>();
        s_QuadMesh->Create(
            MeshFactory::CreateQuad(), GL_TRIANGLES, "MeshCache::Shared2DQuad");
    }
    return s_QuadMesh;
}

std::shared_ptr<Mesh> MeshCache::CreateCubeInstance()
{
    auto mesh = std::make_shared<Mesh>();
    mesh->Create(
        MeshFactory::CreateCube(),
        GL_TRIANGLES,
        "MeshCache::CubeInstance"
    );
    return mesh;
}

std::shared_ptr<Mesh> MeshCache::Load(const std::string& path)
{
    const std::string key = NormalizePath(path);

    auto found = s_Meshes.find(key);
    if (found != s_Meshes.end())
    {
        if (auto cached = found->second.lock())
            return cached;
        s_Meshes.erase(found);
    }

    // Expired entries otherwise leave path strings behind as projects/assets
    // are opened and removed repeatedly in the same editor session.
    static size_t loadsSincePrune = 0;
    if ((++loadsSincePrune & 63u) == 0u)
    {
        for (auto it = s_Meshes.begin(); it != s_Meshes.end();)
        {
            if (it->second.expired())
                it = s_Meshes.erase(it);
            else
                ++it;
        }
    }

    auto mesh = ModelImporter::LoadMesh(path);
    if (!mesh || !mesh->IsValid())
        return nullptr;

    s_Meshes[key] = mesh;
    return mesh;
}

void MeshCache::Invalidate(const std::string& path)
{
    s_Meshes.erase(NormalizePath(path));
}

void MeshCache::Clear()
{
    s_Meshes.clear();
    s_CubeMesh = nullptr;
    s_QuadMesh = nullptr;
}
