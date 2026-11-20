#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <filesystem>

#include "Texture.h"

class TextureCache
{
public:

    static std::shared_ptr<Texture> Load(
        const std::string& path,
        bool srgb = true
    );

    static void Clear();

private:

    static std::string NormalizePath(
        const std::string& path
    );

    static std::unordered_map<
        std::string,
        std::weak_ptr<Texture>
    > s_Textures;
};
