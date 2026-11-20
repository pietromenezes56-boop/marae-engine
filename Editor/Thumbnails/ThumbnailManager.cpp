#include "ThumbnailManager.h"
#include "../../Assets/AssetDatabase/AssetDatabase.h"

#include "../ThirdParty/stb/stb_image.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

GLuint ThumbnailManager::s_TextureIcon = 0;
GLuint ThumbnailManager::s_MeshIcon = 0;
GLuint ThumbnailManager::s_MaterialIcon = 0;
GLuint ThumbnailManager::s_PrefabIcon = 0;
GLuint ThumbnailManager::s_SceneIcon = 0;
GLuint ThumbnailManager::s_AudioIcon = 0;
GLuint ThumbnailManager::s_FileIcon = 0;
GLuint ThumbnailManager::s_FolderIcon = 0;
uint64_t ThumbnailManager::s_FrameIndex = 0;

namespace
{
    constexpr int kMaxThumbnailDimension = 128;   // reduzido: menos VRAM por thumbnail
    constexpr size_t kMaxCachedThumbnails = 24;   // reduzido: cache maximo ~6MB
    constexpr uint64_t kThumbnailIdleFrames = 60; // reduzido: descarta mais rapido

    void DeleteThumbnail(GLuint texture)
    {
        if (texture != 0)
            glDeleteTextures(1, &texture);
    }
}

GLuint ThumbnailManager::LoadTexture(const char* path)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_set_flip_vertically_on_load(true);

    unsigned char* data =
        stbi_load(
            path,
            &width,
            &height,
            &channels,
            4
        );

    if (!data)
    {
        return 0;
    }

    // Asset cards are at most ~132 px wide. Keep a 256 px GPU copy for
    // high-DPI sharpness instead of uploading multi-megapixel images.
    std::vector<unsigned char> resized;
    const int targetWidth = std::max(1, std::min(width, kMaxThumbnailDimension));
    const int targetHeight = std::max(1, std::min(height, kMaxThumbnailDimension));
    const unsigned char* uploadData = data;
    if (targetWidth != width || targetHeight != height)
    {
        resized.resize(static_cast<size_t>(targetWidth) * targetHeight * 4);
        for (int y = 0; y < targetHeight; ++y)
        {
            const float sourceY = std::clamp(
                (static_cast<float>(y) + 0.5f) * height / targetHeight - 0.5f,
                0.0f, static_cast<float>(height - 1));
            const int y0 = static_cast<int>(sourceY);
            const int y1 = std::min(y0 + 1, height - 1);
            const float fy = sourceY - y0;
            for (int x = 0; x < targetWidth; ++x)
            {
                const float sourceX = std::clamp(
                    (static_cast<float>(x) + 0.5f) * width / targetWidth - 0.5f,
                    0.0f, static_cast<float>(width - 1));
                const int x0 = static_cast<int>(sourceX);
                const int x1 = std::min(x0 + 1, width - 1);
                const float fx = sourceX - x0;
                const size_t outputOffset = (static_cast<size_t>(y) * targetWidth + x) * 4;
                const size_t p00 = (static_cast<size_t>(y0) * width + x0) * 4;
                const size_t p10 = (static_cast<size_t>(y0) * width + x1) * 4;
                const size_t p01 = (static_cast<size_t>(y1) * width + x0) * 4;
                const size_t p11 = (static_cast<size_t>(y1) * width + x1) * 4;
                for (int channel = 0; channel < 4; ++channel)
                {
                    const float top = data[p00 + channel] * (1.0f - fx) + data[p10 + channel] * fx;
                    const float bottom = data[p01 + channel] * (1.0f - fx) + data[p11 + channel] * fx;
                    resized[outputOffset + channel] = static_cast<unsigned char>(
                        std::clamp(std::lround(top * (1.0f - fy) + bottom * fy), 0L, 255L));
                }
            }
        }
        uploadData = resized.data();
    }

    GLuint texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        targetWidth,
        targetHeight,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        uploadData
    );

    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);

    return texture;
}

void ThumbnailManager::Initialize()
{
    Shutdown();
    s_TextureIcon =
        LoadTexture("Editor/Thumbnails/Icons/Texture.png");

    s_MeshIcon =
        LoadTexture("Editor/Thumbnails/Icons/Mesh.png");

    s_MaterialIcon =
        LoadTexture("Editor/Thumbnails/Icons/Material.png");

    s_PrefabIcon =
        LoadTexture("Editor/Thumbnails/Icons/Prefab.png");

    s_SceneIcon =
        LoadTexture("Editor/Thumbnails/Icons/Scene.png");

    s_AudioIcon =
        LoadTexture("Editor/Thumbnails/Icons/Audio.png");

    s_FileIcon =
        LoadTexture("Editor/Thumbnails/Icons/File.png");

    s_FolderIcon =
        LoadTexture("Editor/Thumbnails/Icons/Folder.png");
}

void ThumbnailManager::BeginFrame()
{
    ++s_FrameIndex;
}

void ThumbnailManager::EndFrame()
{
    // Run after ImGui submits this frame; stale IDs are no longer referenced.
    for (auto it = s_ThumbnailCache.begin(); it != s_ThumbnailCache.end();)
    {
        if (s_FrameIndex - it->second.LastUsedFrame > kThumbnailIdleFrames)
        {
            DeleteThumbnail(it->second.Texture);
            it = s_ThumbnailCache.erase(it);
        }
        else
        {
            ++it;
        }
    }

    while (s_ThumbnailCache.size() > kMaxCachedThumbnails)
    {
        auto oldest = s_ThumbnailCache.end();
        for (auto it = s_ThumbnailCache.begin(); it != s_ThumbnailCache.end(); ++it)
        {
            if (oldest == s_ThumbnailCache.end() ||
                it->second.LastUsedFrame < oldest->second.LastUsedFrame)
                oldest = it;
        }
        if (oldest == s_ThumbnailCache.end())
            break;
        DeleteThumbnail(oldest->second.Texture);
        s_ThumbnailCache.erase(oldest);
    }
}

void ThumbnailManager::Shutdown()
{
    for (const auto& entry : s_ThumbnailCache)
        DeleteThumbnail(entry.second.Texture);
    s_ThumbnailCache.clear();

    DeleteThumbnail(s_TextureIcon);
    DeleteThumbnail(s_MeshIcon);
    DeleteThumbnail(s_MaterialIcon);
    DeleteThumbnail(s_PrefabIcon);
    DeleteThumbnail(s_SceneIcon);
    DeleteThumbnail(s_AudioIcon);
    DeleteThumbnail(s_FileIcon);
    DeleteThumbnail(s_FolderIcon);
    s_TextureIcon = s_MeshIcon = s_MaterialIcon = s_PrefabIcon = 0;
    s_SceneIcon = s_AudioIcon = s_FileIcon = s_FolderIcon = 0;
}

GLuint ThumbnailManager::GetIcon(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture:
        return s_TextureIcon;

    case AssetType::Mesh:
        return s_MeshIcon;

    case AssetType::Material:
        return s_MaterialIcon;

    case AssetType::Prefab:
        return s_PrefabIcon;

    case AssetType::Scene:
        return s_SceneIcon;

    case AssetType::Audio:
        return s_AudioIcon;

    default:
        return s_FileIcon;
    }
}

std::unordered_map<std::string, ThumbnailManager::CachedThumbnail>
    ThumbnailManager::s_ThumbnailCache;

GLuint ThumbnailManager::GetTextureThumbnail(
    const std::string& path
)
{
    auto it =
        s_ThumbnailCache.find(path);

    if (it != s_ThumbnailCache.end())
    {
        it->second.LastUsedFrame = s_FrameIndex;
        return it->second.Texture;
    }

    const std::string resolvedPath = AssetDatabase::ResolveAssetPath(path).string();
    GLuint texture = LoadTexture(resolvedPath.c_str());

    if (texture != 0)
        s_ThumbnailCache[path] = { texture, s_FrameIndex };

    return texture;
}
