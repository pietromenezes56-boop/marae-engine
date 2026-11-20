#include "Material.h"

#include <utility>

Material::Material()
{
    Name = "Material";
    Type = MaterialType::Plastic;

    Color[0] = 1.0f;
    Color[1] = 1.0f;
    Color[2] = 1.0f;
    Color[3] = 1.0f;

    PBRParametersEnabled = false;
    Roughness = 0.45f;
    Metallic = 0.0f;
    Specular = 0.5f;
    EmissionStrength = 0.0f;
    Alpha = 1.0f;
    NormalStrength = 1.0f;

    DiffuseTexture = nullptr;
    NormalTexture = nullptr;
    EmissionTexture = nullptr;

    TexturePath = "";
    TextureName = "";
    TextureGUID = "";
    GUID = "";

    ScaleMode    = UVScaleMode::Stretch;
    UVWorldScale = 1.0f;
    TileSize     = 1.0f;
}

Material::Material(const Material& other)
{
    Name = other.Name;
    Type = other.Type;

    Color[0] = other.Color[0];
    Color[1] = other.Color[1];
    Color[2] = other.Color[2];
    Color[3] = other.Color[3];

    PBRParametersEnabled = other.PBRParametersEnabled;
    Roughness = other.Roughness;
    Metallic = other.Metallic;
    Specular = other.Specular;
    EmissionStrength = other.EmissionStrength;
    Alpha = other.Alpha;
    NormalStrength = other.NormalStrength;
    NormalMapPath = other.NormalMapPath;
    EmissionTexturePath = other.EmissionTexturePath;

    // Compartilha a mesma textura.
    // NÃO carrega a textura novamente.
    DiffuseTexture = other.DiffuseTexture;
    NormalTexture = other.NormalTexture;
    EmissionTexture = other.EmissionTexture;

    TexturePath = other.TexturePath;
    TextureName = other.TextureName;
    TextureGUID = other.TextureGUID;
    GUID = other.GUID;
    ScaleMode    = other.ScaleMode;
    UVWorldScale = other.UVWorldScale;
    TileSize     = other.TileSize;
}

Material& Material::operator=(const Material& other)
{
    if (this != &other)
    {
        Name = other.Name;
        Type = other.Type;

        Color[0] = other.Color[0];
        Color[1] = other.Color[1];
        Color[2] = other.Color[2];
        Color[3] = other.Color[3];

        PBRParametersEnabled = other.PBRParametersEnabled;
        Roughness = other.Roughness;
        Metallic = other.Metallic;
        Specular = other.Specular;
        EmissionStrength = other.EmissionStrength;
        Alpha = other.Alpha;
        NormalStrength = other.NormalStrength;
        NormalMapPath = other.NormalMapPath;
        EmissionTexturePath = other.EmissionTexturePath;

        // Compartilha a mesma textura.
        DiffuseTexture = other.DiffuseTexture;
        NormalTexture = other.NormalTexture;
        EmissionTexture = other.EmissionTexture;

        TexturePath = other.TexturePath;
        TextureName = other.TextureName;
        TextureGUID = other.TextureGUID;
        GUID = other.GUID;
        ScaleMode    = other.ScaleMode;
        UVWorldScale = other.UVWorldScale;
        TileSize     = other.TileSize;
    }

    return *this;
}

Material::Material(Material&& other) noexcept
{
    Name = std::move(other.Name);
    Type = other.Type;

    Color[0] = other.Color[0];
    Color[1] = other.Color[1];
    Color[2] = other.Color[2];
    Color[3] = other.Color[3];

    PBRParametersEnabled = other.PBRParametersEnabled;
    Roughness = other.Roughness;
    Metallic = other.Metallic;
    Specular = other.Specular;
    EmissionStrength = other.EmissionStrength;
    Alpha = other.Alpha;
    NormalStrength = other.NormalStrength;
    NormalMapPath = std::move(other.NormalMapPath);
    EmissionTexturePath = std::move(other.EmissionTexturePath);

    DiffuseTexture = std::move(other.DiffuseTexture);
    NormalTexture = std::move(other.NormalTexture);
    EmissionTexture = std::move(other.EmissionTexture);

    TexturePath = std::move(other.TexturePath);
    TextureName = std::move(other.TextureName);
    TextureGUID = std::move(other.TextureGUID);
    GUID = std::move(other.GUID);
    ScaleMode    = other.ScaleMode;
    UVWorldScale = other.UVWorldScale;
    TileSize     = other.TileSize;
}

Material& Material::operator=(Material&& other) noexcept
{
    if (this != &other)
    {
        Name = std::move(other.Name);
        Type = other.Type;

        Color[0] = other.Color[0];
        Color[1] = other.Color[1];
        Color[2] = other.Color[2];
        Color[3] = other.Color[3];

        PBRParametersEnabled = other.PBRParametersEnabled;
        Roughness = other.Roughness;
        Metallic = other.Metallic;
        Specular = other.Specular;
        EmissionStrength = other.EmissionStrength;
        Alpha = other.Alpha;
        NormalStrength = other.NormalStrength;
        NormalMapPath = std::move(other.NormalMapPath);
        EmissionTexturePath = std::move(other.EmissionTexturePath);

        DiffuseTexture = std::move(other.DiffuseTexture);
        NormalTexture = std::move(other.NormalTexture);
        EmissionTexture = std::move(other.EmissionTexture);

        TexturePath = std::move(other.TexturePath);
        TextureName = std::move(other.TextureName);
        TextureGUID = std::move(other.TextureGUID);
        GUID = std::move(other.GUID);
        ScaleMode    = other.ScaleMode;
        UVWorldScale = other.UVWorldScale;
        TileSize     = other.TileSize;
    }

    return *this;
}
