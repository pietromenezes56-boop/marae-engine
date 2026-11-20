#pragma once

#include <vector>
#include "../../Engine/Renderer/Material/Material.h"

class MaterialDatabase
{
public:

    static void Initialize();

    static void ScanMaterials();

    static void RegisterMaterial(
        const Material& material
    );

    static std::vector<Material>& GetMaterials();

    static Material* GetMaterialByGUID(
        const std::string& guid
    );

    static Material* GetMaterialByName(
        const std::string& name
    );

private:

    static std::vector<Material> s_Materials;
};