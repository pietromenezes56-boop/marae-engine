#include "PixelCanvas.h"
#include "../../../Renderer/Texture/Texture.h"

#include <glad/glad.h>
#include <cmath>
#include <cstring>
#include <iostream>

PixelCanvas::PixelCanvas(int width, int height, const glm::vec4& clearColor)
    : m_Width(width), m_Height(height), m_Dirty(false)
{
    if (m_Width < 1) m_Width = 1;
    if (m_Height < 1) m_Height = 1;
    m_Pixels.resize(static_cast<size_t>(m_Width * m_Height * 4), 0);
    Clear(clearColor);
    m_Dirty = false;
    InitTexture();
}

PixelCanvas::~PixelCanvas()
{
    DestroyTexture();
}

void PixelCanvas::DestroyTexture()
{
    m_Texture = nullptr;
    if (m_TextureID != 0)
    {
        glDeleteTextures(1, &m_TextureID);
        m_TextureID = 0;
    }
}

void PixelCanvas::InitTexture()
{
    if (m_TextureID != 0) DestroyTexture();

    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_Pixels.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    m_Texture = std::make_shared<Texture>();
    m_Texture->ID = m_TextureID;
    m_Texture->Width = m_Width;
    m_Texture->Height = m_Height;
    m_Texture->Channels = 4;
    m_Texture->Loaded = true;
}

void PixelCanvas::MarkDirtyFull()
{
    m_Dirty = true;
    m_DirtyMinX = 0; m_DirtyMinY = 0;
    m_DirtyMaxX = m_Width; m_DirtyMaxY = m_Height;
}

void PixelCanvas::MarkDirty(int x, int y)
{
    if (!m_Dirty)
    {
        m_Dirty = true;
        m_DirtyMinX = x; m_DirtyMaxX = x + 1;
        m_DirtyMinY = y; m_DirtyMaxY = y + 1;
    }
    else
    {
        if (x < m_DirtyMinX) m_DirtyMinX = x;
        if (y < m_DirtyMinY) m_DirtyMinY = y;
        if (x + 1 > m_DirtyMaxX) m_DirtyMaxX = x + 1;
        if (y + 1 > m_DirtyMaxY) m_DirtyMaxY = y + 1;
    }
}

void PixelCanvas::Clear(const glm::vec4& color)
{
    uint8_t r = static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);

    for (size_t i = 0; i < m_Pixels.size(); i += 4)
    {
        m_Pixels[i + 0] = r;
        m_Pixels[i + 1] = g;
        m_Pixels[i + 2] = b;
        m_Pixels[i + 3] = a;
    }
    MarkDirtyFull();
}

void PixelCanvas::Resize(int nw, int nh, const glm::vec4& fillColor)
{
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (nw == m_Width && nh == m_Height) return;

    std::vector<uint8_t> newBuf(static_cast<size_t>(nw * nh * 4), 0);

    uint8_t fr = static_cast<uint8_t>(std::clamp(fillColor.r, 0.0f, 1.0f) * 255.0f);
    uint8_t fg = static_cast<uint8_t>(std::clamp(fillColor.g, 0.0f, 1.0f) * 255.0f);
    uint8_t fb = static_cast<uint8_t>(std::clamp(fillColor.b, 0.0f, 1.0f) * 255.0f);
    uint8_t fa = static_cast<uint8_t>(std::clamp(fillColor.a, 0.0f, 1.0f) * 255.0f);
    for (size_t i = 0; i < newBuf.size(); i += 4)
    {
        newBuf[i + 0] = fr; newBuf[i + 1] = fg;
        newBuf[i + 2] = fb; newBuf[i + 3] = fa;
    }

    int copyW = std::min(m_Width, nw);
    int copyH = std::min(m_Height, nh);
    for (int y = 0; y < copyH; ++y)
    {
        for (int x = 0; x < copyW; ++x)
        {
            size_t src = PixIdx(m_Width, x, y);
            size_t dst = PixIdx(nw, x, y);
            newBuf[dst + 0] = m_Pixels[src + 0];
            newBuf[dst + 1] = m_Pixels[src + 1];
            newBuf[dst + 2] = m_Pixels[src + 2];
            newBuf[dst + 3] = m_Pixels[src + 3];
        }
    }

    m_Pixels = std::move(newBuf);
    m_Width = nw;
    m_Height = nh;
    InitTexture();
    MarkDirtyFull();
}

void PixelCanvas::SetPixel(int x, int y, const glm::vec4& color, float blendFactor)
{
    if (x < 0 || x >= m_Width || y < 0 || y >= m_Height) return;

    size_t index = PixIdx(m_Width, x, y);
    float alpha = std::clamp(blendFactor * color.a, 0.0f, 1.0f);

    float srcR = color.r * 255.0f;
    float srcG = color.g * 255.0f;
    float srcB = color.b * 255.0f;
    float srcA = color.a * 255.0f;

    float dstR = static_cast<float>(m_Pixels[index + 0]);
    float dstG = static_cast<float>(m_Pixels[index + 1]);
    float dstB = static_cast<float>(m_Pixels[index + 2]);
    float dstA = static_cast<float>(m_Pixels[index + 3]);

    if (blendFactor >= 0.999f && color.a >= 0.999f)
    {
        m_Pixels[index + 0] = static_cast<uint8_t>(std::clamp(srcR, 0.0f, 255.0f));
        m_Pixels[index + 1] = static_cast<uint8_t>(std::clamp(srcG, 0.0f, 255.0f));
        m_Pixels[index + 2] = static_cast<uint8_t>(std::clamp(srcB, 0.0f, 255.0f));
        m_Pixels[index + 3] = static_cast<uint8_t>(std::clamp(srcA, 0.0f, 255.0f));
    }
    else
    {
        float outA = dstA / 255.0f + alpha * (1.0f - dstA / 255.0f);
        float invOutA = (outA > 0.0001f) ? (1.0f / outA) : 0.0f;
        float outR = (srcR / 255.0f * alpha + dstR / 255.0f * (dstA / 255.0f) * (1.0f - alpha)) * invOutA;
        float outG = (srcG / 255.0f * alpha + dstG / 255.0f * (dstA / 255.0f) * (1.0f - alpha)) * invOutA;
        float outB = (srcB / 255.0f * alpha + dstB / 255.0f * (dstA / 255.0f) * (1.0f - alpha)) * invOutA;
        m_Pixels[index + 0] = static_cast<uint8_t>(std::clamp(outR * 255.0f, 0.0f, 255.0f));
        m_Pixels[index + 1] = static_cast<uint8_t>(std::clamp(outG * 255.0f, 0.0f, 255.0f));
        m_Pixels[index + 2] = static_cast<uint8_t>(std::clamp(outB * 255.0f, 0.0f, 255.0f));
        m_Pixels[index + 3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
    }

    MarkDirty(x, y);
}

glm::vec4 PixelCanvas::GetPixel(int x, int y) const
{
    if (x < 0 || x >= m_Width || y < 0 || y >= m_Height)
        return glm::vec4(0.0f);
    size_t i = PixIdx(m_Width, x, y);
    return glm::vec4(
        m_Pixels[i + 0] / 255.0f,
        m_Pixels[i + 1] / 255.0f,
        m_Pixels[i + 2] / 255.0f,
        m_Pixels[i + 3] / 255.0f);
}

void PixelCanvas::UpdateGPU()
{
    if (!m_Dirty || m_TextureID == 0) return;

    int x0 = std::max(0, m_DirtyMinX);
    int y0 = std::max(0, m_DirtyMinY);
    int x1 = std::min(m_Width, m_DirtyMaxX);
    int y1 = std::min(m_Height, m_DirtyMaxY);
    if (x0 >= x1 || y0 >= y1) return;

    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_Width);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    x0, y0, x1 - x0, y1 - y0,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_Pixels.data() + PixIdx(m_Width, x0, y0));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_Dirty = false;
}

void PixelCanvas::PushUndoSnapshot()
{
    m_UndoStack.push_back(m_Pixels);
    if (m_UndoStack.size() > MAX_HISTORY) m_UndoStack.pop_front();
    m_RedoStack.clear();
}

bool PixelCanvas::Undo()
{
    if (m_UndoStack.empty()) return false;
    m_RedoStack.push_back(m_Pixels);
    if (m_RedoStack.size() > MAX_HISTORY) m_RedoStack.pop_front();
    m_Pixels = m_UndoStack.back();
    m_UndoStack.pop_back();
    MarkDirtyFull();
    return true;
}

bool PixelCanvas::Redo()
{
    if (m_RedoStack.empty()) return false;
    m_UndoStack.push_back(m_Pixels);
    if (m_UndoStack.size() > MAX_HISTORY) m_UndoStack.pop_front();
    m_Pixels = m_RedoStack.back();
    m_RedoStack.pop_back();
    MarkDirtyFull();
    return true;
}

void PixelCanvas::FlipHorizontal()
{
    for (int y = 0; y < m_Height; ++y)
    {
        for (int x = 0; x < m_Width / 2; ++x)
        {
            size_t a = PixIdx(m_Width, x, y);
            size_t b = PixIdx(m_Width, m_Width - 1 - x, y);
            for (int c = 0; c < 4; ++c) std::swap(m_Pixels[a + c], m_Pixels[b + c]);
        }
    }
    MarkDirtyFull();
}

void PixelCanvas::FlipVertical()
{
    for (int y = 0; y < m_Height / 2; ++y)
    {
        for (int x = 0; x < m_Width; ++x)
        {
            size_t a = PixIdx(m_Width, x, y);
            size_t b = PixIdx(m_Width, x, m_Height - 1 - y);
            for (int c = 0; c < 4; ++c) std::swap(m_Pixels[a + c], m_Pixels[b + c]);
        }
    }
    MarkDirtyFull();
}

void PixelCanvas::Rotate90CW()
{
    int nw = m_Height;
    int nh = m_Width;
    std::vector<uint8_t> newBuf(static_cast<size_t>(nw * nh * 4), 0);
    for (int y = 0; y < m_Height; ++y)
    {
        for (int x = 0; x < m_Width; ++x)
        {
            int nx = m_Height - 1 - y;
            int ny = x;
            size_t src = PixIdx(m_Width, x, y);
            size_t dst = PixIdx(nw, nx, ny);
            for (int c = 0; c < 4; ++c) newBuf[dst + c] = m_Pixels[src + c];
        }
    }
    m_Pixels = std::move(newBuf);
    m_Width = nw;
    m_Height = nh;
    InitTexture();
    MarkDirtyFull();
}
