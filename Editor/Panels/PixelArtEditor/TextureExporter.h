#pragma once

#include <vector>
#include <string>
#include <cstdint>

class TextureExporter
{
public:
    static bool SaveBMP32(const std::string& path,
                          const std::vector<uint8_t>& rgba,
                          int width, int height);

    static bool SaveTGA32(const std::string& path,
                          const std::vector<uint8_t>& rgba,
                          int width, int height);
};
