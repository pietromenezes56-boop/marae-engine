#pragma once

#include <string>
#include <utility>
#include "TextureCompression.h"

using TextureID = unsigned int;

class Texture
{
public:
	Texture();
	~Texture();

	
		Texture(const Texture& other) = delete;
	Texture& operator=(const Texture& other) = delete;

	Texture(Texture&& other) noexcept;
	Texture& operator=(Texture&& other) noexcept;

	// Texturas de cor (albedo/emissão) devem ser decodificadas de sRGB pelo GPU;
	// mapas de dados (normal/roughness/etc.) permanecem em espaço linear.
	bool Load(const std::string& path, bool srgb = true);

	// Carrega com compressão automática (reduz RAM/VRAM em 75%)
	bool LoadCompressed(const std::string& path, bool srgb = true,
	                   TextureCompression::Format format = TextureCompression::Format::Automatic);

	// Libera dados da CPU após upload para GPU (economiza RAM)
	void ReleaseCPUData();

	TextureID ID = 0;

	int Width = 0;
	int Height = 0;
	int Channels = 0;

	bool Loaded = false;
	bool SRGB = true;
	bool Compressed = false;
	bool CPUDataReleased = false;

	std::string Path;

	// Estatísticas de memória
	size_t GetGPUMemoryUsage() const;
	size_t GetCPUMemoryUsage() const;

private:
	void CreateFallbackTexture();

	// Dados comprimidos em CPU (opcional)
	unsigned char* m_CompressedData = nullptr;
	size_t m_CompressedSize = 0;

	// Bytes GPU já reportados ao GpuMemoryTracker (evita contagem dupla
	// em reload/move).
	size_t m_RegisteredGpuBytes = 0;
	void SyncGpuMemoryRegistration();

};
