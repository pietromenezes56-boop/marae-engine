#include "TextureCompression.h"
#include <iostream>

bool TextureCompression::s_BC7Supported = false;
bool TextureCompression::s_ASTCSupported = false;
bool TextureCompression::s_ETC2Supported = false;
bool TextureCompression::s_DetectDone = false;

void TextureCompression::DetectCapabilities()
{
    if (s_DetectDone)
        return;

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    std::string extStr(extensions ? extensions : "");

    // Detecta suporte a BC7 (BPTC)
    s_BC7Supported = extStr.find("GL_ARB_texture_compression_bptc") != std::string::npos ||
                     extStr.find("GL_EXT_texture_compression_bptc") != std::string::npos;

    // Detecta suporte a ASTC
    s_ASTCSupported = extStr.find("GL_KHR_texture_compression_astc") != std::string::npos ||
                      extStr.find("GL_OES_texture_compression_astc") != std::string::npos;

    // Detecta suporte a ETC2
    s_ETC2Supported = extStr.find("GL_ARB_ES3_compatibility") != std::string::npos ||
                      extStr.find("GL_OES_compressed_ETC2_RGB8_texture") != std::string::npos;

    // Detecta suporte a S3TC (DXT1/DXT5) - mais comum
    bool s3tcSupported = extStr.find("GL_EXT_texture_compression_s3tc") != std::string::npos ||
                         extStr.find("GL_ARB_texture_compression") != std::string::npos;

    // Se BC7 não suportado mas S3TC sim, usa BC3 como fallback
    if (!s_BC7Supported && s3tcSupported)
    {
        std::cout << "[TextureCompression] BC7 não suportado, mas S3TC sim (usará BC3)\n";
    }

    s_DetectDone = true;

    std::cout << "[TextureCompression] BC7: " << (s_BC7Supported ? "YES" : "NO") << "\n";
    std::cout << "[TextureCompression] ASTC: " << (s_ASTCSupported ? "YES" : "NO") << "\n";
    std::cout << "[TextureCompression] ETC2: " << (s_ETC2Supported ? "YES" : "NO") << "\n";
    std::cout << "[TextureCompression] S3TC: " << (s3tcSupported ? "YES" : "NO") << "\n";
}

TextureCompression::Format TextureCompression::DetectBestFormat()
{
    DetectCapabilities();

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    std::string extStr(extensions ? extensions : "");

    // Verifica S3TC (DXT1/DXT5) - mais comum
    bool s3tcSupported = extStr.find("GL_EXT_texture_compression_s3tc") != std::string::npos ||
                         extStr.find("GL_ARB_texture_compression") != std::string::npos;

    if (s_BC7Supported)
        return Format::BC7;
    if (s_ASTCSupported)
        return Format::ASTC_4x4;
    if (s_ETC2Supported)
        return Format::ETC2;
    if (s3tcSupported)
        return Format::BC3;

    // Se nenhum formato de compressão for suportado, retorna None
    std::cout << "[TextureCompression] Nenhum formato de compressão suportado, usando RGBA8\n";
    return Format::None;
}

bool TextureCompression::CompressOnGPU(
    GLuint textureID,
    int width,
    int height,
    int channels,
    const Settings& settings)
{
    DetectCapabilities();

    Format format = settings.compressionFormat;
    if (format == Format::None)
        return false;

    // Se o formato não é suportado, usa BC3 como fallback
    if (format == Format::BC7 && !s_BC7Supported)
        format = Format::BC3;
    if (format == Format::ASTC_4x4 && !s_ASTCSupported)
        format = Format::BC3;
    if (format == Format::ETC2 && !s_ETC2Supported)
        format = Format::BC3;

    GLenum internalFormat = GetGLInternalFormat(format, settings.useSrgb);
    GLenum formatGL = GetGLFormat(format);

    glBindTexture(GL_TEXTURE_2D, textureID);

    // Recria textura com formato comprimido
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internalFormat,
        width,
        height,
        0,
        formatGL,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    if (settings.generateMipmaps)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool TextureCompression::CompressOnCPU(
    const unsigned char* data,
    int width,
    int height,
    int channels,
    unsigned char** outCompressedData,
    size_t* outCompressedSize,
    const Settings& settings)
{
    // Nota: Compressão em CPU requer bibliotecas externas (stb_dxt, etc)
    // Esta é uma implementação placeholder que apenas copia os dados
    // Para produção, integrar stb_dxt ou AMD Compressonator

    size_t dataSize = width * height * channels;
    *outCompressedData = new unsigned char[dataSize];
    *outCompressedSize = dataSize;

    memcpy(*outCompressedData, data, dataSize);

    return true;
}

size_t TextureCompression::EstimateCompressedSize(
    int width,
    int height,
    Format format)
{
    switch (format)
    {
        case Format::BC1:
        case Format::BC3:
        case Format::BC7:
            // Blocos de 4x4 pixels, 8 ou 16 bytes por bloco
            return ((width + 3) / 4) * ((height + 3) / 4) * 16;

        case Format::ETC2:
            return ((width + 3) / 4) * ((height + 3) / 4) * 8;

        case Format::ASTC_4x4:
            return ((width + 3) / 4) * ((height + 3) / 4) * 16;

        case Format::ASTC_6x6:
            return ((width + 5) / 6) * ((height + 5) / 6) * 16;

        case Format::None:
        default:
            return width * height * 4;
    }
}

GLenum TextureCompression::GetGLInternalFormat(Format format, bool srgb)
{
    // Verifica se as constantes de compressão estão disponíveis
    // Se não, usa formatos não comprimidos como fallback
    bool compressionSupported = (glGetString(GL_EXTENSIONS) != nullptr);

    if (!compressionSupported)
    {
        return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    }

    switch (format)
    {
        case Format::BC1:
            // Usa valores hexadecimais como fallback se constantes não definidas
            #ifdef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
            return srgb ? GL_COMPRESSED_SRGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::BC3:
            #ifdef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
            return srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::BC7:
            #ifdef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB
            return srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB : GL_COMPRESSED_RGBA_BPTC_UNORM_ARB;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::ETC2:
            #ifdef GL_COMPRESSED_SRGB8_ETC2
            return srgb ? GL_COMPRESSED_SRGB8_ETC2 : GL_COMPRESSED_RGB8_ETC2;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::ASTC_4x4:
            #ifdef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
            return srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR : GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::ASTC_6x6:
            #ifdef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR
            return srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR : GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
            #else
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            #endif

        case Format::None:
        default:
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    }
}

GLenum TextureCompression::GetGLFormat(Format format)
{
    switch (format)
    {
        case Format::BC1:
            #ifdef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
            return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            #else
            return GL_RGBA;
            #endif

        case Format::BC3:
        case Format::BC7:
            #ifdef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
            return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            #else
            return GL_RGBA;
            #endif

        case Format::ETC2:
            #ifdef GL_COMPRESSED_RGB8_ETC2
            return GL_COMPRESSED_RGB8_ETC2;
            #else
            return GL_RGBA;
            #endif

        case Format::ASTC_4x4:
            #ifdef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
            return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
            #else
            return GL_RGBA;
            #endif

        case Format::ASTC_6x6:
            #ifdef GL_COMPRESSED_RGBA_ASTC_6x6_KHR
            return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
            #else
            return GL_RGBA;
            #endif

        case Format::None:
        default:
            return GL_RGBA;
    }
}
