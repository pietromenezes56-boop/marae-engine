#include "TextureExporter.h"

#include <fstream>
#include <cstring>

static void WriteU16_LE(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
static void WriteU32_LE(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
static void WriteI32_LE(std::vector<uint8_t>& buf, int32_t v)
{
    WriteU32_LE(buf, static_cast<uint32_t>(v));
}

bool TextureExporter::SaveBMP32(const std::string& path,
                                const std::vector<uint8_t>& rgba,
                                int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (static_cast<size_t>(width * height * 4) > rgba.size()) return false;

    const uint32_t pixelDataSize = static_cast<uint32_t>(width * height * 4);
    const uint32_t dibHeaderSize = 124;
    const uint32_t fileHeaderSize = 14;
    const uint32_t totalSize = fileHeaderSize + dibHeaderSize + pixelDataSize;

    std::vector<uint8_t> out;
    out.reserve(totalSize);

    out.push_back('B'); out.push_back('M');
    WriteU32_LE(out, totalSize);
    WriteU16_LE(out, 0);
    WriteU16_LE(out, 0);
    WriteU32_LE(out, fileHeaderSize + dibHeaderSize);

    WriteU32_LE(out, dibHeaderSize);
    WriteI32_LE(out, width);
    WriteI32_LE(out, height);
    WriteU16_LE(out, 1);
    WriteU16_LE(out, 32);
    WriteU32_LE(out, 3);
    WriteU32_LE(out, pixelDataSize);
    WriteI32_LE(out, 2835);
    WriteI32_LE(out, 2835);
    WriteU32_LE(out, 0);
    WriteU32_LE(out, 0);
    WriteU32_LE(out, 0x0000FF00);
    WriteU32_LE(out, 0x00FF0000);
    WriteU32_LE(out, 0xFF000000);
    WriteU32_LE(out, 0x000000FF);
    WriteU32_LE(out, 0x42475273);
    for (int i = 0; i < 12 * 4; ++i) out.push_back(0);

    for (int y = 0; y < height; ++y)
    {
        int flippedY = height - 1 - y;
        size_t row = static_cast<size_t>(flippedY * width * 4);
        for (int x = 0; x < width; ++x)
        {
            size_t p = row + static_cast<size_t>(x * 4);
            uint8_t r = rgba[p + 0];
            uint8_t g = rgba[p + 1];
            uint8_t b = rgba[p + 2];
            uint8_t a = rgba[p + 3];
            out.push_back(b);
            out.push_back(g);
            out.push_back(r);
            out.push_back(a);
        }
    }

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return f.good();
}

bool TextureExporter::SaveTGA32(const std::string& path,
                                const std::vector<uint8_t>& rgba,
                                int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (static_cast<size_t>(width * height * 4) > rgba.size()) return false;

    std::vector<uint8_t> header(18, 0);
    header[2] = 2;
    header[12] = static_cast<uint8_t>(width & 0xFF);
    header[13] = static_cast<uint8_t>((width >> 8) & 0xFF);
    header[14] = static_cast<uint8_t>(height & 0xFF);
    header[15] = static_cast<uint8_t>((height >> 8) & 0xFF);
    header[16] = 32;
    header[17] = 0x20;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(header.data()), 18);

    for (int y = 0; y < height; ++y)
    {
        size_t row = static_cast<size_t>(y * width * 4);
        for (int x = 0; x < width; ++x)
        {
            size_t p = row + static_cast<size_t>(x * 4);
            uint8_t bgra[4] = { rgba[p + 2], rgba[p + 1], rgba[p + 0], rgba[p + 3] };
            f.write(reinterpret_cast<const char*>(bgra), 4);
        }
    }
    return f.good();
}
