#pragma once

#include "../../Engine/Renderer/Material/Material.h"
#include "../../Engine/Assets/GUID/GUID.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class FaceTextureMode : uint8_t
{
    AllFaces = 0,
    IndividualFaces = 1
};

class MaterialComponent
{
public:

    Material MaterialData;

    std::string MaterialGUID;
    FaceTextureMode TextureMode = FaceTextureMode::AllFaces;
    // Caminho vazio = face existente sem textura no modo individual.
    std::unordered_map<uint32_t, std::string> FaceTexturePaths;
    std::unordered_map<uint32_t, std::shared_ptr<Texture>> FaceTextures;
};
