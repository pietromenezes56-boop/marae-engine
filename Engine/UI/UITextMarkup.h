#pragma once

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cfloat>
#include <string>
#include <vector>

namespace MaraeUI
{
    struct StyledGlyph
    {
        std::string Text;
        ImU32 Color = IM_COL32_WHITE;
        float Size = 14.0f;
        bool Bold = false;
    };

    inline bool ParseColorTag(const std::string& tag, ImVec4& out)
    {
        if (tag.size() != 7 && tag.size() != 9) return false;
        if (tag[0] != '#') return false;
        unsigned values[4] = { 0, 0, 0, 255 };
        for (size_t channel = 0; channel < (tag.size() == 7 ? 3u : 4u); ++channel)
        {
            const size_t offset = 1 + channel * 2;
            char* end = nullptr;
            values[channel] = static_cast<unsigned>(std::strtoul(tag.substr(offset, 2).c_str(), &end, 16));
            if (!end || *end != '\0') return false;
        }
        out = ImVec4(values[0] / 255.0f, values[1] / 255.0f,
                     values[2] / 255.0f, values[3] / 255.0f);
        return true;
    }

    inline void DrawRichText(ImDrawList* drawList, ImFont* font, const char* text,
                             ImVec2 position, ImU32 baseColor, float baseSize,
                             float wrapWidth, int alignment)
    {
        if (!drawList || !font || !text) return;
        std::vector<StyledGlyph> glyphs;
        glyphs.reserve(std::char_traits<char>::length(text));
        ImVec4 currentColor = ImGui::ColorConvertU32ToFloat4(baseColor);
        float currentSize = std::clamp(baseSize, 6.0f, 256.0f);
        bool bold = false;
        std::vector<ImVec4> colorStack;
        std::vector<float> sizeStack;
        std::vector<bool> boldStack;
        const std::string input(text);
        size_t i = 0;
        while (i < input.size())
        {
            if (input[i] == '<')
            {
                const size_t close = input.find('>', i + 1);
                if (close != std::string::npos && close - i <= 32)
                {
                    const std::string tag = input.substr(i + 1, close - i - 1);
                    if (tag.rfind("color=#", 0) == 0)
                    {
                        ImVec4 parsed;
                        if (ParseColorTag(tag.substr(6), parsed))
                        { colorStack.push_back(currentColor); currentColor = parsed; i = close + 1; continue; }
                    }
                    else if (tag == "/color" && !colorStack.empty())
                    { currentColor = colorStack.back(); colorStack.pop_back(); i = close + 1; continue; }
                    else if (tag.rfind("size=", 0) == 0)
                    {
                        char* end = nullptr;
                        const float parsed = std::strtof(tag.c_str() + 5, &end);
                        if (end && end != tag.c_str() + 5 && std::isfinite(parsed))
                        { sizeStack.push_back(currentSize); currentSize = std::clamp(parsed, 6.0f, 256.0f); i = close + 1; continue; }
                    }
                    else if (tag == "/size" && !sizeStack.empty())
                    { currentSize = sizeStack.back(); sizeStack.pop_back(); i = close + 1; continue; }
                    else if (tag == "b")
                    { boldStack.push_back(bold); bold = true; i = close + 1; continue; }
                    else if (tag == "/b" && !boldStack.empty())
                    { bold = boldStack.back(); boldStack.pop_back(); i = close + 1; continue; }
                    else if (tag == "br/" || tag == "br")
                    { glyphs.push_back({ "\n", ImGui::ColorConvertFloat4ToU32(currentColor), currentSize, bold }); i = close + 1; continue; }
                }
            }

            const unsigned char lead = static_cast<unsigned char>(input[i]);
            size_t bytes = lead < 0x80 ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : ((lead & 0xF0) == 0xE0 ? 3 : ((lead & 0xF8) == 0xF0 ? 4 : 1)));
            bytes = std::min(bytes, input.size() - i);
            glyphs.push_back({ input.substr(i, bytes), ImGui::ColorConvertFloat4ToU32(currentColor), currentSize, bold });
            i += bytes;
        }

        struct Line { std::vector<StyledGlyph> Glyphs; float Width = 0.0f; float Height = 14.0f; };
        std::vector<Line> lines(1);
        lines.front().Height = baseSize;
        wrapWidth = std::max(1.0f, wrapWidth);
        for (const StyledGlyph& glyph : glyphs)
        {
            if (glyph.Text == "\n") { lines.emplace_back(); continue; }
            const ImVec2 extent = font->CalcTextSizeA(glyph.Size, FLT_MAX, 0.0f, glyph.Text.c_str());
            Line& line = lines.back();
            if (line.Width > 0.0f && line.Width + extent.x > wrapWidth)
                lines.emplace_back();
            Line& destination = lines.back();
            destination.Glyphs.push_back(glyph);
            destination.Width += extent.x;
            destination.Height = std::max(destination.Height, glyph.Size);
        }

        float y = position.y;
        for (const Line& line : lines)
        {
            float x = position.x;
            if (alignment == 1) x += std::max(0.0f, (wrapWidth - line.Width) * 0.5f);
            else if (alignment == 2) x += std::max(0.0f, wrapWidth - line.Width);
            for (const StyledGlyph& glyph : line.Glyphs)
            {
                drawList->AddText(font, glyph.Size, ImVec2(x, y), glyph.Color, glyph.Text.c_str());
                const ImVec2 extent = font->CalcTextSizeA(glyph.Size, FLT_MAX, 0.0f, glyph.Text.c_str());
                if (glyph.Bold)
                    drawList->AddText(font, glyph.Size, ImVec2(x + 0.45f, y), glyph.Color, glyph.Text.c_str());
                x += extent.x;
            }
            y += std::max(1.0f, line.Height);
        }
    }
}
