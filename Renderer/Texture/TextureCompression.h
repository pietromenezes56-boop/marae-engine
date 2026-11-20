#pragma once

#include <string>
#include <glad/glad.h>

// Sistema de compressão de texturas para reduzir uso de RAM e VRAM
// Suporta formatos modernos: BC7, ASTC, ETC2
class TextureCompression
{
public:
    enum class Format
    {
        None,           // Sem compressão (RGBA8)
        Automatic,      // Detecta melhor formato automaticamente
        BC1,            // DXT1 - 4:1 ratio, qualidade baixa
        BC3,            // DXT5 - 4:1 ratio, com alpha
        BC7,            // Melhor qualidade, 4:1 ratio (recomendado)
        ETC2,            // Mobile (Android)
        ASTC_4x4,       // Mobile moderno
        ASTC_6x6        // Mobile mais agressivo
    };

    struct Settings
    {
        Format compressionFormat = Format::BC7;
        bool generateMipmaps = true;
        bool useSrgb = true;
        int maxMipLevels = 0; // 0 = auto
        float quality = 0.9f; // 0.0 - 1.0
    };

    // Detecta melhor formato para o hardware atual
    static Format DetectBestFormat();

    // Comprime textura na GPU (mais rápido que CPU)
    static bool CompressOnGPU(
        GLuint textureID,
        int width,
        int height,
        int channels,
        const Settings& settings
    );

    // Comprime textura na CPU (offline)
    static bool CompressOnCPU(
        const unsigned char* data,
        int width,
        int height,
        int channels,
        unsigned char** outCompressedData,
        size_t* outCompressedSize,
        const Settings& settings
    );

    // Calcula tamanho estimado após compressão
    static size_t EstimateCompressedSize(
        int width,
        int height,
        Format format
    );

    // Obtém formato GL correspondente
    static GLenum GetGLInternalFormat(Format format, bool srgb);
    static GLenum GetGLFormat(Format format);

private:
    static bool s_BC7Supported;
    static bool s_ASTCSupported;
    static bool s_ETC2Supported;
    static bool s_DetectDone;

    static void DetectCapabilities();
};
