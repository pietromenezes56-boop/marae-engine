#include "TextureLoader.h"

#include <iostream>
#include <filesystem>

Texture TextureLoader::Load(
    const std::string& path
)
{
    Texture texture;
    texture.Load(path);
    return texture;
}
