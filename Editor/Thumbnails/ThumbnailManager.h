#pragma once

#include <glad/glad.h>
#include "../../Assets/AssetDatabase/AssetDatabase.h"
#include <cstdint>
#include <unordered_map>

class ThumbnailManager
{
public:

    static void Initialize();
    static void BeginFrame();
    static void EndFrame();
    static void Shutdown();

    static GLuint GetIcon(
        AssetType type
    );

    static GLuint GetTextureThumbnail(
        const std::string& path
    );

private:

    static GLuint LoadTexture(
        const char* path
    );

    static GLuint s_TextureIcon;
    static GLuint s_MeshIcon;
    static GLuint s_MaterialIcon;
    static GLuint s_PrefabIcon;
    static GLuint s_SceneIcon;
    static GLuint s_AudioIcon;
    static GLuint s_FileIcon;
    static GLuint s_FolderIcon;

    struct CachedThumbnail
    {
        GLuint Texture = 0;
        uint64_t LastUsedFrame = 0;
    };

    static std::unordered_map<std::string, CachedThumbnail> s_ThumbnailCache;
    static uint64_t s_FrameIndex;
       
    
};
