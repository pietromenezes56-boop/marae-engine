#include "MaterialSerializer.h"
#include "../../../ThirdParty/json/json.hpp"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

Material MaterialSerializer::Load(
    const std::string& path
)
{
    Material material;

    std::ifstream file(path);
    if (!file.is_open())
    {
        return material;
    }

    try
    {
        json j;
        file >> j;

        material.Name = j.value("Name", "Material");
        material.GUID = j.value("GUID", "");
        material.TexturePath = j.value("Texture", "");
        material.PBRParametersEnabled = j.value("PBRParametersEnabled", false);
        material.Roughness = j.value("Roughness", material.Roughness);
        material.Metallic = j.value("Metallic", material.Metallic);
        material.Specular = j.value("Specular", material.Specular);
        material.EmissionStrength = j.value("EmissionStrength", material.EmissionStrength);
        material.Alpha = j.value("Alpha", material.Alpha);
        material.NormalStrength = j.value("NormalStrength", material.NormalStrength);
        material.NormalMapPath = j.value("NormalMap", "");
        material.EmissionTexturePath = j.value("EmissionTexture", "");

        material.ScaleMode    = static_cast<UVScaleMode>(j.value("UVScaleMode", 0));
        material.UVWorldScale = j.value("UVWorldScale", 1.0f);
        material.TileSize     = j.value("TileSize", 1.0f);
        material.FixedAlignment = j.value("FixedAlignment", 0);
        material.FaceTileRepeat = j.value("FaceTileRepeat", 1.0f);
        material.NineSliceBorder = j.value("NineSliceBorder", 0.25f);

        const int type = j.value("Type", 0);
        if (type >= static_cast<int>(MaterialType::Plastic) &&
            type <= static_cast<int>(MaterialType::Fabric))
        {
            material.Type = static_cast<MaterialType>(type);
        }

        if (j.contains("Color") && j["Color"].is_array() && j["Color"].size() >= 4)
        {
            auto color = j["Color"];
            material.Color[0] = color[0];
            material.Color[1] = color[1];
            material.Color[2] = color[2];
            material.Color[3] = color[3];
        }
    }
    catch (const std::exception&)
    {
    }

    return material;
}

void MaterialSerializer::Save(
    const Material& material,
    const std::string& path
)
{
    json j;

    j["Name"] = material.Name;
    j["GUID"] = material.GUID;
    j["Texture"] = material.TexturePath;
    j["Type"] = static_cast<int>(material.Type);
    j["PBRParametersEnabled"] = material.PBRParametersEnabled;
    j["Roughness"] = material.Roughness;
    j["Metallic"] = material.Metallic;
    j["Specular"] = material.Specular;
    j["EmissionStrength"] = material.EmissionStrength;
    j["Alpha"] = material.Alpha;
    j["NormalStrength"] = material.NormalStrength;
    j["NormalMap"] = material.NormalMapPath;
    j["EmissionTexture"] = material.EmissionTexturePath;

    j["UVScaleMode"]  = static_cast<int>(material.ScaleMode);
    j["UVWorldScale"] = material.UVWorldScale;
    j["TileSize"]     = material.TileSize;
    j["FixedAlignment"] = material.FixedAlignment;
    j["FaceTileRepeat"] = material.FaceTileRepeat;
    j["NineSliceBorder"] = material.NineSliceBorder;

    j["Color"] =
    {
        material.Color[0],
        material.Color[1],
        material.Color[2],
        material.Color[3]
    };

    std::ofstream file(path);

    file << j.dump(4);
}
