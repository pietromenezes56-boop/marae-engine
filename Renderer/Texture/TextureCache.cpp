#include "TextureCache.h"
#include "../../Assets/AssetDatabase/AssetDatabase.h"

#include <iostream>
#include <filesystem>

std::unordered_map<
    std::string,
    std::weak_ptr<Texture>
>
TextureCache::s_Textures;


std::string TextureCache::NormalizePath(
    const std::string& path
)
{
    std::filesystem::path filesystemPath(path);

    // Normaliza . e ..
    std::error_code error;

    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(
            filesystemPath,
            error
        );

    if (error)
    {
        // Se não conseguir canonicalizar,
        // ainda normaliza separadores.
        normalized = filesystemPath.lexically_normal();
    }

    // generic_string() usa "/" independentemente do Windows.
    return normalized.generic_string();
}


std::shared_ptr<Texture> TextureCache::Load(
    const std::string& path,
    bool srgb
)
{
    const std::string resolvedPath = AssetDatabase::ResolveAssetPath(path).string();
    const std::string key = NormalizePath(resolvedPath) + (srgb ? "|srgb" : "|linear");


    auto found =
        s_Textures.find(key);

    if (found != s_Textures.end())
    {
        if (auto cached = found->second.lock())
            return cached;
        s_Textures.erase(found);
    }

    // Materials own live textures. Drop expired cache keys periodically so
    // switching scenes/projects does not retain every previously seen path.
    static size_t loadsSincePrune = 0;
    if ((++loadsSincePrune & 63u) == 0u)
    {
        for (auto it = s_Textures.begin(); it != s_Textures.end();)
        {
            if (it->second.expired())
                it = s_Textures.erase(it);
            else
                ++it;
        }
    }

    auto texture =
        std::make_shared<Texture>();

    if (!texture->Load(resolvedPath, srgb))
    {
        return nullptr;
    }

    s_Textures[key] = texture;

    return texture;
}


void TextureCache::Clear()
{
    s_Textures.clear();
}
