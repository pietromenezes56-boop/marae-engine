#include "Texture.h"
#include "TextureCompression.h"
#include "../../Core/Diagnostics/GpuMemoryTracker.h"

#include <glad/glad.h>
#include "../../ThirdParty/stb/stb_image.h"

#include <atomic>
#include <iostream>
#include <utility>
#include <filesystem>
#include <vector>

static std::atomic<int> s_TextureCount{ 0 };

Texture::Texture()
{
    ID = 0;
    Width = 0;
    Height = 0;
    Channels = 0;
    Loaded = false;
    Compressed = false;
    CPUDataReleased = false;
    m_CompressedData = nullptr;
    m_CompressedSize = 0;
}

Texture::Texture(Texture&& other) noexcept
{
    ID = other.ID;
    Width = other.Width;
    Height = other.Height;
    Channels = other.Channels;
    Loaded = other.Loaded;
    SRGB = other.SRGB;
    Compressed = other.Compressed;
    CPUDataReleased = other.CPUDataReleased;
    Path = std::move(other.Path);
    m_CompressedData = other.m_CompressedData;
    m_CompressedSize = other.m_CompressedSize;
    m_RegisteredGpuBytes = other.m_RegisteredGpuBytes;

    other.ID = 0;
    other.Width = 0;
    other.Height = 0;
    other.Channels = 0;
    other.Loaded = false;
    other.SRGB = true;
    other.Compressed = false;
    other.CPUDataReleased = false;
    other.m_CompressedData = nullptr;
    other.m_CompressedSize = 0;
    other.m_RegisteredGpuBytes = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this == &other)
        return *this;

    // Destroy our current texture first.
    if (ID != 0)
    {
        glDeleteTextures(1, &ID);
        ID = 0;
        Loaded = false;
        --s_TextureCount;
    }
    SyncGpuMemoryRegistration();

    // Libera dados comprimidos anteriores
    if (m_CompressedData)
    {
        delete[] m_CompressedData;
        m_CompressedData = nullptr;
        m_CompressedSize = 0;
    }

    ID = other.ID;
    Width = other.Width;
    Height = other.Height;
    Channels = other.Channels;
    Loaded = other.Loaded;
    SRGB = other.SRGB;
    Compressed = other.Compressed;
    CPUDataReleased = other.CPUDataReleased;
    Path = std::move(other.Path);
    m_CompressedData = other.m_CompressedData;
    m_CompressedSize = other.m_CompressedSize;
    m_RegisteredGpuBytes = other.m_RegisteredGpuBytes;

    other.ID = 0;
    other.Width = 0;
    other.Height = 0;
    other.Channels = 0;
    other.Loaded = false;
    other.SRGB = true;
    other.Compressed = false;
    other.CPUDataReleased = false;
    other.m_CompressedData = nullptr;
    other.m_CompressedSize = 0;
    other.m_RegisteredGpuBytes = 0;

    return *this;
}

bool Texture::Load(const std::string& path, bool srgb)
{
    // If this Texture already owns a GL texture,
    // release it before loading another one.
    if (ID != 0)
    {
        glDeleteTextures(1, &ID);
        ID = 0;
        Loaded = false;
        --s_TextureCount;
        SyncGpuMemoryRegistration();
    }

    Path = path;
    SRGB = srgb;

    stbi_set_flip_vertically_on_load(true);

    std::vector<std::string> pathsToTry;
    pathsToTry.push_back(path);

    // Adiciona fallbacks de caminho comuns
    if (!path.empty())
    {
        pathsToTry.push_back("MaraeEngine/" + path);
        pathsToTry.push_back("../MaraeEngine/" + path);
        pathsToTry.push_back("Assets/" + path);
        pathsToTry.push_back("../Assets/" + path);
        pathsToTry.push_back("./" + path);
    }

    unsigned char* data = nullptr;
    std::string successfulPath;

    for (const std::string& testPath : pathsToTry)
    {
        if (testPath.empty()) continue;

        // Verifica se o arquivo existe antes de tentar carregar
        if (!std::filesystem::exists(testPath))
        {
            continue;
        }

        data = stbi_load(testPath.c_str(), &Width, &Height, &Channels, 4);

        if (data)
        {
            successfulPath = testPath;
            std::cout << "[Texture] Carregado: " << successfulPath
                      << " (" << Width << "x" << Height << ", " << Channels << " channels)\n";
            break;
        }
    }

    if (!data)
    {
        std::cerr << "[Texture] ERRO: Falha ao carregar textura: " << path << "\n";
        std::cerr << "[Texture] Tentados " << pathsToTry.size() << " caminhos\n";

        // Cria textura de fallback (magenta xadrez para indicar erro)
        CreateFallbackTexture();

        Width = 64;
        Height = 64;
        Channels = 4;
        Loaded = true;
        ++s_TextureCount;
        SyncGpuMemoryRegistration();

        return false;
    }

    glGenTextures(1, &ID);

    glBindTexture(
        GL_TEXTURE_2D,
        ID
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_REPEAT
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_REPEAT
    );

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

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8,
        Width,
        Height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        data
    );

    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(
        GL_TEXTURE_2D,
        0
    );

    stbi_image_free(data);

    Loaded = true;

    ++s_TextureCount;
    SyncGpuMemoryRegistration();

    return true;
}

bool Texture::LoadCompressed(const std::string& path, bool srgb,
                            TextureCompression::Format format)
{
    // Detecta melhor formato se Automatic
    if (format == TextureCompression::Format::Automatic)
    {
        format = TextureCompression::DetectBestFormat();
    }

    // Se formato for None, usa Load normal
    if (format == TextureCompression::Format::None)
    {
        return Load(path, srgb);
    }

    // Carrega dados originais primeiro
    if (!Load(path, srgb))
        return false;

    // Aplica compressão na GPU
    TextureCompression::Settings settings;
    settings.compressionFormat = format;
    settings.useSrgb = srgb;
    settings.generateMipmaps = true;

    bool success = TextureCompression::CompressOnGPU(ID, Width, Height, Channels, settings);

    if (success)
    {
        Compressed = true;
        std::cout << "[Texture] Compressão aplicada: " << path
                  << " (formato: " << static_cast<int>(format) << ")\n";
    }
    else
    {
        std::cout << "[Texture] Compressão falhou, usando formato não comprimido\n";
        Compressed = false;
    }

    SyncGpuMemoryRegistration();
    return success;
}

void Texture::ReleaseCPUData()
{
    if (m_CompressedData)
    {
        delete[] m_CompressedData;
        m_CompressedData = nullptr;
        m_CompressedSize = 0;
    }
    CPUDataReleased = true;
}

size_t Texture::GetGPUMemoryUsage() const
{
    if (!Loaded || Width == 0 || Height == 0)
        return 0;

    if (Compressed)
    {
        return TextureCompression::EstimateCompressedSize(
            Width, Height,
            TextureCompression::DetectBestFormat()
        );
    }

    // RGBA8: 4 bytes por pixel
    return Width * Height * 4;
}

size_t Texture::GetCPUMemoryUsage() const
{
    return m_CompressedSize;
}

void Texture::SyncGpuMemoryRegistration()
{
    const size_t desired = GetGPUMemoryUsage();
    if (desired == m_RegisteredGpuBytes)
        return;
    if (desired > m_RegisteredGpuBytes)
        GpuMemoryTracker::Add(static_cast<int64_t>(desired - m_RegisteredGpuBytes));
    else
        GpuMemoryTracker::Remove(static_cast<int64_t>(m_RegisteredGpuBytes - desired));
    m_RegisteredGpuBytes = desired;
}

Texture::~Texture()
{
    GpuMemoryTracker::Remove(static_cast<int64_t>(m_RegisteredGpuBytes));
    m_RegisteredGpuBytes = 0;

    if (ID == 0)
        return;

    glDeleteTextures(
        1,
        &ID
    );

    ID = 0;
    Loaded = false;

    --s_TextureCount;
}

void Texture::CreateFallbackTexture()
{
    // Cria textura de fallback: padrão xadrez magenta/cinza
    const int size = 64;
    unsigned char fallbackData[size * size * 4];

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int index = (y * size + x) * 4;
            bool magenta = ((x / 8) + (y / 8)) % 2 == 0;

            if (magenta)
            {
                fallbackData[index + 0] = 255; // R
                fallbackData[index + 1] = 0;   // G
                fallbackData[index + 2] = 255; // B
                fallbackData[index + 3] = 255; // A
            }
            else
            {
                fallbackData[index + 0] = 128; // R
                fallbackData[index + 1] = 128; // G
                fallbackData[index + 2] = 128; // B
                fallbackData[index + 3] = 255; // A
            }
        }
    }

    glGenTextures(1, &ID);
    glBindTexture(GL_TEXTURE_2D, ID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        size,
        size,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        fallbackData
    );

    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[Texture] Textura de fallback criada (erro de carregamento)\n";
}
