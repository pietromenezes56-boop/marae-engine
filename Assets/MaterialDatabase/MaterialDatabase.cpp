#include "MaterialDatabase.h"
#include <filesystem>
#include <iostream>
#include "../../Engine/Renderer/Material/MaterialSerializer.h"

std::vector<Material> MaterialDatabase::s_Materials;

void MaterialDatabase::Initialize()
{
}


std::vector<Material>& MaterialDatabase::GetMaterials()
{
    return s_Materials;
}

void MaterialDatabase::ScanMaterials()
{
    s_Materials.clear();

    std::filesystem::path folder =
        "Assets/Materials";

    if (!std::filesystem::exists(folder))
        return;

    for (const auto& file :
        std::filesystem::directory_iterator(folder))
    {
        if (file.path().extension() == ".mat" ||
            file.path().extension() == ".material")
        {
            Material material =
                MaterialSerializer::Load(
                    file.path().string()
                );

            if (material.GUID.empty())
            {
                continue;
            }

            s_Materials.push_back(material);
        }
    }
}

void MaterialDatabase::RegisterMaterial(
    const Material& material
)
{
    if (material.GUID.empty())
    {
        return;
    }

    for (auto& existing : s_Materials)
    {
        if (existing.GUID == material.GUID)
        {
            existing = material;
            return;
        }
    }

    s_Materials.push_back(material);
}

Material* MaterialDatabase::GetMaterialByGUID(
    const std::string& guid
)
{
    if (guid.empty())
    {
        return nullptr;
    }

    for (auto& material : s_Materials)
    {
        if (material.GUID == guid)
        {
            return &material;
        }
    }

    return nullptr;
}

Material* MaterialDatabase::GetMaterialByName(
    const std::string& name
)
{
    for (auto& material : s_Materials)
    {
        if (material.Name == name)
        {
            return &material;
        }
    }

    return nullptr;
}
