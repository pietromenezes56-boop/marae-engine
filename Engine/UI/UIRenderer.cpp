#include "UIRenderer.h"
#include "UILayout.h"
#include "UIFontLibrary.h"

#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Components/UIComponent.h"
#include "../../Renderer/Texture/TextureCache.h"
#include "../../Renderer/Texture/Texture.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr float kReferenceWidth = 960.0f;
    constexpr float kReferenceHeight = 540.0f;

    const char* kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aMode;
layout(location = 4) in vec4 aRect;    // x=cornerRadius, y=borderWidth, z=softness
layout(location = 5) in vec4 aBorder;  // cor da borda
layout(location = 6) in vec2 aCenter;
layout(location = 7) in vec2 aHalf;

uniform mat4 uProj;

out vec2 vUV;
out vec4 vColor;
out float vMode;
out vec4 vRect;
out vec4 vBorder;
out vec2 vLocal;
out vec2 vHalf;

void main()
{
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vMode = aMode;
    vRect = aRect;
    vBorder = aBorder;
    vLocal = aPos - aCenter;
    vHalf = aHalf;
}
)GLSL";

    const char* kFragmentShader = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vMode;
in vec4 vRect;
in vec4 vBorder;
in vec2 vLocal;
in vec2 vHalf;

uniform sampler2D uTexture;

out vec4 FragColor;

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    if (vMode > 1.5)
    {
        // Glifo: o atlas guarda cobertura no canal alpha.
        float a = texture(uTexture, vUV).a;
        FragColor = vec4(vColor.rgb, vColor.a * a);
        return;
    }
    if (vMode > 0.5)
    {
        FragColor = texture(uTexture, vUV) * vColor;
        return;
    }

    vec2 halfSize = max(vHalf, vec2(0.0001));
    float radius = min(vRect.x, min(halfSize.x, halfSize.y));
    float d = sdRoundBox(vLocal, halfSize, radius);

    float aa = max(vRect.z, 0.75);
    float outer = 1.0 - smoothstep(-aa, aa, d);
    float inner = (vRect.y > 0.0) ? (1.0 - smoothstep(-aa, aa, d + vRect.y)) : 1.0;

    vec4 col = mix(vBorder, vColor, inner);
    col.a *= outer;
    FragColor = col;
}
)GLSL";

    GLuint CompileShader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    struct NativeTextToken
    {
        ImWchar Codepoint = 0;
        float Size = 14.0f;
        float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool Bold = false;
        bool NewLine = false;
    };

    ImWchar ReadCodepoint(const std::string& text, size_t& index)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index++]);
        if (lead < 0x80)
            return static_cast<ImWchar>(lead);

        int continuationCount = 0;
        uint32_t codepoint = 0;
        if ((lead & 0xE0) == 0xC0) { continuationCount = 1; codepoint = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { continuationCount = 2; codepoint = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { continuationCount = 3; codepoint = lead & 0x07; }
        else return static_cast<ImWchar>(0xFFFD);

        for (int i = 0; i < continuationCount; ++i)
        {
            if (index >= text.size())
                return static_cast<ImWchar>(0xFFFD);
            const unsigned char next = static_cast<unsigned char>(text[index]);
            if ((next & 0xC0) != 0x80)
                return static_cast<ImWchar>(0xFFFD);
            ++index;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            codepoint = 0xFFFD;
        return static_cast<ImWchar>(codepoint);
    }

    bool ParseMarkupColor(const std::string& value, const float baseColor[4], float out[4])
    {
        if ((value.size() != 7 && value.size() != 9) || value[0] != '#')
            return false;
        unsigned channels[4] = { 0, 0, 0, 255 };
        for (size_t i = 0; i < (value.size() == 7 ? 3u : 4u); ++i)
        {
            char* end = nullptr;
            const std::string part = value.substr(1 + i * 2, 2);
            channels[i] = static_cast<unsigned>(std::strtoul(part.c_str(), &end, 16));
            if (!end || *end != '\0')
                return false;
        }
        for (int i = 0; i < 3; ++i)
            out[i] = static_cast<float>(channels[i]) / 255.0f;
        out[3] = (static_cast<float>(channels[3]) / 255.0f) * baseColor[3];
        return true;
    }
}

void UIRenderer::Init()
{
    if (m_Program != 0)
        return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs == 0 || fs == 0)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }

    m_Program = glCreateProgram();
    glAttachShader(m_Program, vs);
    glAttachShader(m_Program, fs);
    glLinkProgram(m_Program);

    GLint linked = GL_FALSE;
    glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!linked)
    {
        glDeleteProgram(m_Program);
        m_Program = 0;
        return;
    }

    m_LocProj = glGetUniformLocation(m_Program, "uProj");
    m_LocTexture = glGetUniformLocation(m_Program, "uTexture");

    EnsureBuffers();

    // Textura branca 1x1 usada pelos retângulos sólidos, para que tudo passe
    // pelo mesmo shader com amostragem de textura.
    glGenTextures(1, &m_WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);
    const unsigned char white[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_BoundTexture = m_WhiteTexture;
}

void UIRenderer::EnsureBuffers()
{
    if (m_VAO == 0)
        glGenVertexArrays(1, &m_VAO);
    if (m_VBO == 0)
        glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    auto attrib = [stride](GLuint index, GLint size, size_t offset)
    {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offset));
    };
    attrib(0, 2, offsetof(Vertex, x));
    attrib(1, 2, offsetof(Vertex, u));
    attrib(2, 4, offsetof(Vertex, r));
    attrib(3, 1, offsetof(Vertex, mode));
    attrib(4, 4, offsetof(Vertex, cornerRadius));
    attrib(5, 4, offsetof(Vertex, br));
    attrib(6, 2, offsetof(Vertex, cx));
    attrib(7, 2, offsetof(Vertex, hx));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UIRenderer::Shutdown()
{
    if (m_WhiteTexture)
    {
        glDeleteTextures(1, &m_WhiteTexture);
        m_WhiteTexture = 0;
    }
    if (m_VBO)
    {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    if (m_VAO)
    {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_Program)
    {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }
    m_ImageCache.clear();
}

void UIRenderer::Flush()
{
    if (m_Vertices.empty() || m_Program == 0)
    {
        m_Vertices.clear();
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_Vertices.size() * sizeof(Vertex)),
                 m_Vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_BoundTexture);
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_Vertices.size()));
    glBindVertexArray(0);

    m_VertexCount += static_cast<GLsizei>(m_Vertices.size());
    m_Vertices.clear();
}

void UIRenderer::PushQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d)
{
    m_Vertices.push_back(a);
    m_Vertices.push_back(b);
    m_Vertices.push_back(c);
    m_Vertices.push_back(a);
    m_Vertices.push_back(c);
    m_Vertices.push_back(d);
}

void UIRenderer::AddRect(float x, float y, float w, float h, const float color[4],
                         float cornerRadius, float borderWidth, const float borderColor[4])
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    if (m_BoundTexture != m_WhiteTexture)
    {
        Flush();
        m_BoundTexture = m_WhiteTexture;
    }

    // cornerRadius/borderWidth chegam já em pixels do alvo.
    Vertex v{};
    v.mode = 0.0f;
    v.u = 0.5f; v.v = 0.5f;
    v.cornerRadius = cornerRadius;
    v.borderWidth = borderWidth;
    v.softness = 1.0f;
    v.cx = x + w * 0.5f;
    v.cy = y + h * 0.5f;
    v.hx = w * 0.5f;
    v.hy = h * 0.5f;
    v.br = borderColor[0]; v.bg = borderColor[1]; v.bb = borderColor[2]; v.ba = borderColor[3];

    Vertex tl = v, tr = v, br = v, bl = v;
    tl.x = x;         tl.y = y;
    tr.x = x + w;     tr.y = y;
    br.x = x + w;     br.y = y + h;
    bl.x = x;         bl.y = y + h;
    for (Vertex* vert : { &tl, &tr, &br, &bl })
    {
        vert->r = color[0]; vert->g = color[1]; vert->b = color[2]; vert->a = color[3];
    }
    PushQuad(tl, tr, br, bl);
}

void UIRenderer::AddImage(GLuint texture, float x, float y, float w, float h, const float color[4])
{
    if (texture == 0 || w <= 0.0f || h <= 0.0f)
        return;
    if (m_BoundTexture != texture)
    {
        Flush();
        m_BoundTexture = texture;
    }

    Vertex v{};
    v.mode = 1.0f;
    v.r = color[0]; v.g = color[1]; v.b = color[2]; v.a = color[3];
    v.cx = x + w * 0.5f; v.cy = y + h * 0.5f;
    v.hx = w * 0.5f; v.hy = h * 0.5f;

    Vertex tl = v, tr = v, br = v, bl = v;
    tl.x = x;     tl.y = y;     tl.u = 0.0f; tl.v = 0.0f;
    tr.x = x + w; tr.y = y;     tr.u = 1.0f; tr.v = 0.0f;
    br.x = x + w; br.y = y + h; br.u = 1.0f; br.v = 1.0f;
    bl.x = x;     bl.y = y + h; bl.u = 0.0f; bl.v = 1.0f;
    PushQuad(tl, tr, br, bl);
}

float UIRenderer::MeasureTextWidth(void* imFont, const char* text, float fontSize) const
{
    if (imFont == nullptr || text == nullptr || text[0] == '\0' || fontSize <= 0.0f)
        return 0.0f;
    ImFont* font = static_cast<ImFont*>(imFont);
    // ImGui 1.92+: os glifos são rasterizados sob demanda por tamanho; as métricas
    // devolvidas por FindGlyph já estão na escala pedida (fontSize).
    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (baked == nullptr)
        return 0.0f;
    const std::string value(text);
    float width = 0.0f;
    for (size_t i = 0; i < value.size();)
    {
        const ImWchar codepoint = ReadCodepoint(value, i);
        const ImFontGlyph* glyph = baked->FindGlyph(codepoint);
        if (glyph)
            width += glyph->AdvanceX;
    }
    return width;
}

void UIRenderer::AddImage9Slice(GLuint texture, int texWidth, int texHeight,
                                float x, float y, float w, float h,
                                float left, float top, float right, float bottom,
                                const float color[4])
{
    if (texture == 0 || texWidth <= 0 || texHeight <= 0 || w <= 0.0f || h <= 0.0f)
        return;
    // Garante que as bordas não ultrapassem a região nem a textura.
    left = std::clamp(left, 0.0f, static_cast<float>(texWidth));
    right = std::clamp(right, 0.0f, static_cast<float>(texWidth));
    top = std::clamp(top, 0.0f, static_cast<float>(texHeight));
    bottom = std::clamp(bottom, 0.0f, static_cast<float>(texHeight));
    if (left + right > w) { const float f = w / (left + right + 0.0001f); left *= f; right *= f; }
    if (top + bottom > h) { const float f = h / (top + bottom + 0.0001f); top *= f; bottom *= f; }

    if (m_BoundTexture != texture)
    {
        Flush();
        m_BoundTexture = texture;
    }

    const float invW = 1.0f / static_cast<float>(texWidth);
    const float invH = 1.0f / static_cast<float>(texHeight);

    // Colunas/linhas em espaço de tela e em espaço de textura (UV).
    const float xs[4] = { x, x + left, x + w - right, x + w };
    const float ys[4] = { y, y + top, y + h - bottom, y + h };
    const float us[4] = { 0.0f, left * invW, 1.0f - right * invW, 1.0f };
    const float vs[4] = { 0.0f, top * invH, 1.0f - bottom * invH, 1.0f };

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const float x0 = xs[col], x1 = xs[col + 1];
            const float y0 = ys[row], y1 = ys[row + 1];
            if (x1 - x0 <= 0.0f || y1 - y0 <= 0.0f)
                continue;

            Vertex v{};
            v.mode = 1.0f;
            v.r = color[0]; v.g = color[1]; v.b = color[2]; v.a = color[3];
            v.cx = (x0 + x1) * 0.5f; v.cy = (y0 + y1) * 0.5f;
            v.hx = (x1 - x0) * 0.5f; v.hy = (y1 - y0) * 0.5f;

            Vertex tl = v, tr = v, br = v, bl = v;
            tl.x = x0; tl.y = y0; tl.u = us[col];     tl.v = vs[row];
            tr.x = x1; tr.y = y0; tr.u = us[col + 1]; tr.v = vs[row];
            br.x = x1; br.y = y1; br.u = us[col + 1]; br.v = vs[row + 1];
            bl.x = x0; bl.y = y1; bl.u = us[col];     bl.v = vs[row + 1];
            PushQuad(tl, tr, br, bl);
        }
    }
}

void UIRenderer::AddText(GLuint atlasTexture, void* imFont, const char* text,
                         float x, float y, float boxWidth, float boxHeight,
                         float fontSize, const float color[4], int align,
                         bool richText, float markupScale)
{
    if (atlasTexture == 0 || imFont == nullptr || text == nullptr || text[0] == '\0')
        return;

    ImFont* font = static_cast<ImFont*>(imFont);
    // ImGui 1.92+: obtém os dados rasterizados para o tamanho pedido. As métricas
    // do glifo (AdvanceX, X0..Y1) já estão na escala de 'fontSize'.
    ImFontBaked* baked = (fontSize > 0.0f) ? font->GetFontBaked(fontSize) : nullptr;
    if (baked == nullptr)
        return;

    std::vector<NativeTextToken> tokens;
    tokens.reserve(std::strlen(text));
    const std::string input(text);
    float currentColor[4] = { color[0], color[1], color[2], color[3] };
    float currentSize = fontSize;
    bool bold = false;
    std::vector<std::array<float, 4>> colorStack;
    std::vector<float> sizeStack;
    std::vector<bool> boldStack;

    for (size_t i = 0; i < input.size();)
    {
        if (richText && input[i] == '<')
        {
            const size_t close = input.find('>', i + 1);
            if (close != std::string::npos && close - i <= 32)
            {
                const std::string tag = input.substr(i + 1, close - i - 1);
                if (tag.rfind("color=#", 0) == 0)
                {
                    float parsed[4];
                    if (ParseMarkupColor(tag.substr(6), color, parsed))
                    {
                        colorStack.push_back({ currentColor[0], currentColor[1], currentColor[2], currentColor[3] });
                        std::copy(parsed, parsed + 4, currentColor);
                        i = close + 1;
                        continue;
                    }
                }
                else if (tag == "/color" && !colorStack.empty())
                {
                    std::copy(colorStack.back().begin(), colorStack.back().end(), currentColor);
                    colorStack.pop_back();
                    i = close + 1;
                    continue;
                }
                else if (tag.rfind("size=", 0) == 0)
                {
                    char* end = nullptr;
                    const float parsed = std::strtof(tag.c_str() + 5, &end);
                    if (end && end != tag.c_str() + 5 && *end == '\0' && std::isfinite(parsed))
                    {
                        sizeStack.push_back(currentSize);
                        currentSize = std::clamp(parsed * std::max(0.01f, markupScale), 6.0f, 256.0f);
                        i = close + 1;
                        continue;
                    }
                }
                else if (tag == "/size" && !sizeStack.empty())
                {
                    currentSize = sizeStack.back();
                    sizeStack.pop_back();
                    i = close + 1;
                    continue;
                }
                else if (tag == "b")
                {
                    boldStack.push_back(bold);
                    bold = true;
                    i = close + 1;
                    continue;
                }
                else if (tag == "/b" && !boldStack.empty())
                {
                    bold = boldStack.back();
                    boldStack.pop_back();
                    i = close + 1;
                    continue;
                }
                else if (tag == "br" || tag == "br/")
                {
                    NativeTextToken token;
                    token.NewLine = true;
                    tokens.push_back(token);
                    i = close + 1;
                    continue;
                }
            }
        }

        NativeTextToken token;
        token.Size = currentSize;
        std::copy(currentColor, currentColor + 4, token.Color);
        token.Bold = bold;
        if (input[i] == '\r' || input[i] == '\n')
        {
            token.NewLine = true;
            const char newline = input[i++];
            if (newline == '\r' && i < input.size() && input[i] == '\n')
                ++i;
        }
        else
        {
            token.Codepoint = ReadCodepoint(input, i);
        }
        tokens.push_back(token);
    }

    struct TextLine
    {
        std::vector<NativeTextToken> Tokens;
        float Width = 0.0f;
        float Height = 0.0f;
    };
    std::vector<TextLine> lines(1);
    for (const NativeTextToken& token : tokens)
    {
        if (token.NewLine)
        {
            lines.emplace_back();
            continue;
        }
        ImFontBaked* tokenFont = font->GetFontBaked(token.Size);
        const ImFontGlyph* glyph = tokenFont ? tokenFont->FindGlyph(token.Codepoint) : nullptr;
        if (!glyph)
            continue;
        TextLine& line = lines.back();
        if (line.Width > 0.0f && line.Width + glyph->AdvanceX > boxWidth)
            lines.emplace_back();
        TextLine& destination = lines.back();
        destination.Tokens.push_back(token);
        destination.Width += glyph->AdvanceX;
        const float lineHeight = tokenFont->Ascent - tokenFont->Descent;
        destination.Height = std::max(destination.Height, std::max(lineHeight, token.Size));
    }

    float totalHeight = 0.0f;
    for (const TextLine& line : lines)
        totalHeight += std::max(line.Height, fontSize);
    float lineTop = y + std::max(0.0f, (boxHeight - totalHeight) * 0.5f);

    if (m_BoundTexture != atlasTexture)
    {
        Flush();
        m_BoundTexture = atlasTexture;
    }

    for (const TextLine& line : lines)
    {
        float penX = x;
        if (align == 1)
            penX += std::max(0.0f, (boxWidth - line.Width) * 0.5f);
        else if (align == 2)
            penX += std::max(0.0f, boxWidth - line.Width);

        const float lineHeight = std::max(line.Height, fontSize);
        for (const NativeTextToken& token : line.Tokens)
        {
            ImFontBaked* tokenFont = font->GetFontBaked(token.Size);
            const ImFontGlyph* glyph = tokenFont ? tokenFont->FindGlyph(token.Codepoint) : nullptr;
            if (!glyph)
                continue;

            const float fontLineHeight = tokenFont->Ascent - tokenFont->Descent;
            // ImFontGlyph::Y0/Y1 are relative to ImGui's text-origin (top),
            // not to the typographic baseline.
            const float glyphOriginY = lineTop + (lineHeight - fontLineHeight) * 0.5f;
            const float x0 = penX + glyph->X0;
            const float y0 = glyphOriginY + glyph->Y0;
            const float x1 = penX + glyph->X1;
            const float y1 = glyphOriginY + glyph->Y1;

            if (glyph->Visible && x1 > x0 && y1 > y0)
            {
                Vertex v{};
                v.mode = 2.0f;
                v.r = token.Color[0]; v.g = token.Color[1]; v.b = token.Color[2]; v.a = token.Color[3];
                v.cx = (x0 + x1) * 0.5f; v.cy = (y0 + y1) * 0.5f;
                v.hx = (x1 - x0) * 0.5f; v.hy = (y1 - y0) * 0.5f;

                Vertex tl = v, tr = v, br = v, bl = v;
                tl.x = x0; tl.y = y0; tl.u = glyph->U0; tl.v = glyph->V0;
                tr.x = x1; tr.y = y0; tr.u = glyph->U1; tr.v = glyph->V0;
                br.x = x1; br.y = y1; br.u = glyph->U1; br.v = glyph->V1;
                bl.x = x0; bl.y = y1; bl.u = glyph->U0; bl.v = glyph->V1;
                PushQuad(tl, tr, br, bl);

                if (token.Bold)
                {
                    tl.x += 0.45f; tr.x += 0.45f; br.x += 0.45f; bl.x += 0.45f;
                    tl.cx += 0.45f; tr.cx += 0.45f; br.cx += 0.45f; bl.cx += 0.45f;
                    PushQuad(tl, tr, br, bl);
                }
            }

            penX += glyph->AdvanceX;
        }
        lineTop += lineHeight;
    }
}

void UIRenderer::ApplyScissor(int x, int y, int w, int h)
{
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    // Converte do sistema com origem no topo para o sistema do OpenGL.
    const int glY = m_CanvasHeight - (y + h);
    glScissor(x, glY, w, h);
}

void UIRenderer::Render(Scene& scene, int canvasWidth, int canvasHeight)
{
    if (!IsReady() || canvasWidth <= 0 || canvasHeight <= 0)
        return;

    std::vector<Entity>& entities = scene.GetEntities();
    if (entities.empty())
        return;

    struct Entry { Entity* Object; int ZIndex; };
    std::vector<Entry> drawList;
    std::unordered_map<uint64_t, Entity*> byId;

    for (Entity& entity : entities)
        byId.emplace(entity.GetID(), &entity);
    const auto hierarchyDepths = MaraeUI::GetUIHierarchyDepths(entities);

    for (Entity& entity : entities)
    {
        if (!entity.HasUI())
            continue;
        const UIComponent& ui = entity.GetUIComponent();
        if (!ui.Enabled || ui.Type == UIElementType::ScreenGui ||
            !MaraeUI::IsUIHierarchyEnabled(entity, byId))
            continue;
        drawList.push_back({ &entity, ui.ZIndex });
    }
    if (drawList.empty())
        return;

    std::stable_sort(drawList.begin(), drawList.end(),
        [&hierarchyDepths](const Entry& a, const Entry& b)
        {
            if (a.ZIndex != b.ZIndex)
                return a.ZIndex < b.ZIndex;
            return hierarchyDepths.at(a.Object->GetID()) <
                   hierarchyDepths.at(b.Object->GetID());
        });

    const MaraeUI::ResolvedUIMap resolved =
        MaraeUI::ResolveLayouts(entities, kReferenceWidth, kReferenceHeight);

    m_CanvasWidth = canvasWidth;
    m_CanvasHeight = canvasHeight;
    m_ScaleX = static_cast<float>(canvasWidth) / kReferenceWidth;
    m_ScaleY = static_cast<float>(canvasHeight) / kReferenceHeight;

    // ── Salva estado GL que vamos alterar ────────────────────────────────
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint prevProgram = 0, prevVao = 0, prevTexture = 0;
    GLint prevScissor[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissor);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    glUseProgram(m_Program);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(canvasWidth),
                                            static_cast<float>(canvasHeight), 0.0f,
                                            -1.0f, 1.0f);
    glUniformMatrix4fv(m_LocProj, 1, GL_FALSE, &projection[0][0]);
    if (m_LocTexture >= 0)
        glUniform1i(m_LocTexture, 0);

    m_VertexCount = 0;
    m_Vertices.clear();
    m_BoundTexture = m_WhiteTexture;

    ImFont* font = nullptr;
    GLuint atlasTexture = 0;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().Fonts != nullptr)
    {
        // ImGui 1.92+: o atlas expõe ImTextureRef; convertemos para o id nativo
        // (GLuint) do backend OpenGL atual.
        const ImTextureID texId = ImGui::GetIO().Fonts->TexRef.GetTexID();
        atlasTexture = static_cast<GLuint>(static_cast<intptr_t>(texId));
        font = ImGui::GetFont();
    }

    const float fullClip[4] = { 0.0f, 0.0f, static_cast<float>(canvasWidth),
                                static_cast<float>(canvasHeight) };

    // Cache de imagens por caminho; o weak_ptr do TextureCache pode expirar,
    // por isso mantemos uma referência forte aqui enquanto a UI é desenhada.
    auto loadImage = [this](const std::string& path) -> Texture*
    {
        auto cached = m_ImageCache.find(path);
        if (cached == m_ImageCache.end())
        {
            std::shared_ptr<Texture> texture = TextureCache::Load(path, true);
            cached = m_ImageCache.emplace(path, texture).first;
        }
        return cached->second ? cached->second.get() : nullptr;
    };

    for (const Entry& entry : drawList)
    {
        Entity& entity = *entry.Object;
        UIComponent& ui = entity.GetUIComponent();
        ImFont* elementFont = MaraeUI::GetUIFont(ui);
        if (elementFont == nullptr)
            elementFont = font;

        UIComponent::ResolvedRect rect = ui.ResolveRect(kReferenceWidth, kReferenceHeight);
        const auto resolvedIt = resolved.find(entity.GetID());
        if (resolvedIt != resolved.end())
            rect = resolvedIt->second;

        // Escala da referência (960x540) para a resolução real do alvo.
        const float px = rect.X * m_ScaleX;
        const float py = rect.Y * m_ScaleY;
        const float pw = rect.Width * m_ScaleX;
        const float ph = rect.Height * m_ScaleY;

        // Recorte herdado de ancestrais com ClipsContent/ScrollViewEnabled.
        float clipX0 = fullClip[0], clipY0 = fullClip[1];
        float clipX1 = fullClip[2], clipY1 = fullClip[3];
        uint64_t parentId = entity.GetParentID();
        int guard = 0;
        while (parentId != 0 && guard++ < 64)
        {
            const auto parentIt = byId.find(parentId);
            if (parentIt == byId.end())
                break;
            Entity* parent = parentIt->second;
            if (parent->HasUI())
            {
                const UIComponent& parentUI = parent->GetUIComponent();
                if (parentUI.ClipsContent || parentUI.ScrollViewEnabled)
                {
                    UIComponent::ResolvedRect parentRect =
                        parentUI.ResolveRect(kReferenceWidth, kReferenceHeight);
                    const auto parentResolvedIt = resolved.find(parent->GetID());
                    if (parentResolvedIt != resolved.end())
                        parentRect = parentResolvedIt->second;
                    const float ax = parentRect.X * m_ScaleX;
                    const float ay = parentRect.Y * m_ScaleY;
                    const float ax1 = (parentRect.X + parentRect.Width) * m_ScaleX;
                    const float ay1 = (parentRect.Y + parentRect.Height) * m_ScaleY;
                    clipX0 = std::max(clipX0, ax);
                    clipY0 = std::max(clipY0, ay);
                    clipX1 = std::min(clipX1, ax1);
                    clipY1 = std::min(clipY1, ay1);
                }
            }
            parentId = parent->GetParentID();
        }

        const int scissorX = static_cast<int>(clipX0);
        const int scissorY = static_cast<int>(clipY0);
        const int scissorW = static_cast<int>(std::max(0.0f, clipX1 - clipX0));
        const int scissorH = static_cast<int>(std::max(0.0f, clipY1 - clipY0));
        ApplyScissor(scissorX, scissorY, scissorW, scissorH);
        if (scissorW <= 0 || scissorH <= 0)
            continue;

        float background[4], border[4], text[4];
        ui.ResolveThemeColors(background, border, text);
        const float alphaScale = 1.0f - std::clamp(ui.Transparency, 0.0f, 1.0f);
        for (float* c : { background, border, text })
            c[3] *= alphaScale;

        auto themeColor = [alphaScale](const float* src, float* dst)
        {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3] * alphaScale;
        };
        float fill[4], hover[4], pressed[4], track[4];
        themeColor(ui.FillColor, fill);
        themeColor(ui.HoverColor, hover);
        themeColor(ui.PressedColor, pressed);
        themeColor(ui.TrackColor, track);

        const UIElementType type = ui.Type;
        const float uiScale = std::min(m_ScaleX, m_ScaleY);
        const float fontSize = std::max(6.0f, ui.FontSize * m_ScaleY);
        const int align = static_cast<int>(ui.TextAlignment);
        const float radius = std::max(0.0f, ui.CornerRadius * uiScale);
        const float borderW = std::max(0.0f, ui.BorderThickness * uiScale);

        // Estado visual (hover/pressionado) para botões e dropdown.
        float bg[4] = { background[0], background[1], background[2], background[3] };
        if (UIElementIsButton(type) || type == UIElementType::Dropdown)
        {
            if (ui.IsPressed) { bg[0] = pressed[0]; bg[1] = pressed[1]; bg[2] = pressed[2]; bg[3] = pressed[3]; }
            else if (ui.IsHovered) { bg[0] = hover[0]; bg[1] = hover[1]; bg[2] = hover[2]; bg[3] = hover[3]; }
        }

        if (UIElementHasBackground(type))
            AddRect(px, py, pw, ph, bg, radius, borderW, border);

        if (UIElementHasImage(type) && !ui.ImagePath.empty())
        {
            Texture* tex = loadImage(ui.ImagePath);
            if (tex && tex->ID != 0)
            {
                if (ui.NineSlice)
                    AddImage9Slice(tex->ID, tex->Width, tex->Height, px, py, pw, ph,
                                   ui.SliceLeft * uiScale, ui.SliceTop * uiScale,
                                   ui.SliceRight * uiScale, ui.SliceBottom * uiScale, text);
                else
                    AddImage(tex->ID, px, py, pw, ph, text);
            }
        }

        switch (type)
        {
        case UIElementType::Slider:
        {
            const float trackH = std::max(4.0f, ph * 0.35f);
            const float trackY = py + (ph - trackH) * 0.5f;
            AddRect(px, trackY, pw, trackH, track, trackH * 0.5f, 0.0f, border);
            const float t = ui.NormalizedValue();
            if (t > 0.0f)
                AddRect(px, trackY, pw * t, trackH, fill, trackH * 0.5f, 0.0f, border);
            const float knob = std::min(std::max(6.0f, ph), pw);
            const float knobX = px + (pw - knob) * t;
            AddRect(knobX, py, knob, ph, fill,
                    std::min(radius > 0.0f ? radius : knob * 0.5f, knob * 0.5f), borderW, border);
            break;
        }
        case UIElementType::Checkbox:
        {
            const float box = std::min(ph, 22.0f * uiScale);
            const float boxY = py + (ph - box) * 0.5f;
            AddRect(px, boxY, box, box, bg, radius, std::max(borderW, 1.0f), border);
            if (ui.Checked)
                AddRect(px + box * 0.22f, boxY + box * 0.22f, box * 0.56f, box * 0.56f,
                        fill, box * 0.18f, 0.0f, border);
            if (elementFont != nullptr && !ui.Text.empty())
                AddText(atlasTexture, elementFont, ui.Text.c_str(), px + box + 8.0f * uiScale, py,
                        std::max(0.0f, pw - box - 8.0f * uiScale), ph, fontSize, text, 0,
                        ui.RichTextEnabled, m_ScaleY);
            break;
        }
        case UIElementType::ProgressBar:
        {
            AddRect(px, py, pw, ph, track, radius, borderW, border);
            const float t = ui.NormalizedValue();
            if (t > 0.0f)
                AddRect(px, py, pw * t, ph, fill, radius, 0.0f, border);
            if (elementFont != nullptr && !ui.Text.empty())
                AddText(atlasTexture, elementFont, ui.Text.c_str(), px, py, pw, ph, fontSize, text, 1,
                        ui.RichTextEnabled, m_ScaleY);
            break;
        }
        case UIElementType::Dropdown:
        {
            const std::string label =
                (ui.SelectedIndex >= 0 && ui.SelectedIndex < static_cast<int>(ui.Options.size()))
                ? ui.Options[static_cast<size_t>(ui.SelectedIndex)] : ui.Text;
            if (elementFont != nullptr && !label.empty())
                AddText(atlasTexture, elementFont, label.c_str(), px + 8.0f * uiScale, py,
                        std::max(0.0f, pw - 24.0f * uiScale), ph, fontSize, text, 0,
                        ui.RichTextEnabled, m_ScaleY);
            const float arrow = std::max(6.0f, 8.0f * uiScale);
            AddRect(px + pw - arrow - 8.0f * uiScale, py + (ph - arrow) * 0.5f, arrow, arrow,
                    text, 0.0f, 0.0f, border);
            if (ui.DropdownOpen)
            {
                for (size_t i = 0; i < ui.Options.size(); ++i)
                {
                    const float rowY = py + ph * static_cast<float>(i + 1);
                    const bool hoveredOption = static_cast<int>(i) == ui.HoverOptionIndex;
                    const bool selected = static_cast<int>(i) == ui.SelectedIndex;
                    const float* rowColor = hoveredOption ? hover : (selected ? fill : background);
                    AddRect(px, rowY, pw, ph, rowColor, 0.0f, 0.0f, border);
                    if (elementFont != nullptr)
                        AddText(atlasTexture, elementFont, ui.Options[i].c_str(), px + 8.0f * uiScale, rowY,
                                std::max(0.0f, pw - 16.0f * uiScale), ph, fontSize, text, 0,
                                ui.RichTextEnabled, m_ScaleY);
                }
            }
            break;
        }
        case UIElementType::ScrollingFrame:
        {
            const float maxScroll = MaraeUI::GetMaxScrollOffset(
                entity, entities, kReferenceWidth, kReferenceHeight, &resolved);
            if (maxScroll > 0.0f)
            {
                const float barW = std::max(4.0f, 6.0f * uiScale);
                const float contentH = ph + maxScroll;
                float thumbH = std::max(24.0f * uiScale, ph * (ph / contentH));
                thumbH = std::min(thumbH, ph);
                const float scroll01 = std::clamp(ui.ScrollOffsetY, 0.0f, maxScroll) / maxScroll;
                const float thumbY = py + (ph - thumbH) * scroll01;
                const float barX = px + pw - barW;
                AddRect(barX, py, barW, ph, track, barW * 0.5f, 0.0f, border);
                AddRect(barX, thumbY, barW, thumbH, fill, barW * 0.5f, 0.0f, border);
            }
            break;
        }
        case UIElementType::Frame:
        case UIElementType::TextButton:
        case UIElementType::ImageButton:
        case UIElementType::TextBox:
        {
            if (elementFont != nullptr && !ui.Text.empty())
                AddText(atlasTexture, elementFont, ui.Text.c_str(), px + 8.0f * uiScale, py,
                        std::max(0.0f, pw - 16.0f * uiScale), ph, fontSize, text, align,
                        ui.RichTextEnabled, m_ScaleY);
            if (type == UIElementType::TextBox && ui.IsFocused && elementFont != nullptr)
            {
                const int caret = std::clamp(ui.CaretIndex, 0, static_cast<int>(ui.Text.size()));
                const std::string before = ui.Text.substr(0, static_cast<size_t>(caret));
                const float caretX = px + 8.0f * uiScale +
                    MeasureTextWidth(elementFont, before.c_str(), fontSize);
                bool visible = true;
                if (ImGui::GetCurrentContext() != nullptr)
                    visible = std::fmod(static_cast<float>(ImGui::GetTime()), 1.0f) < 0.5f;
                if (visible)
                    AddRect(caretX, py + ph * 0.22f, std::max(1.0f, 1.5f * uiScale), ph * 0.56f,
                            text, 0.0f, 0.0f, border);
            }
            break;
        }
        case UIElementType::TextLabel:
        {
            if (elementFont != nullptr && !ui.Text.empty())
                AddText(atlasTexture, elementFont, ui.Text.c_str(), px + 6.0f * uiScale, py,
                        std::max(0.0f, pw - 12.0f * uiScale), ph, fontSize, text, align,
                        ui.RichTextEnabled, m_ScaleY);
            break;
        }
        default:
            break;
        }
    }

    Flush();

    glUseProgram(static_cast<GLuint>(prevProgram));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));
    glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}
