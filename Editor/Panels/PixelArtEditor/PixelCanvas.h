#pragma once

#include <vector>
#include <memory>
#include <string>
#include <deque>
#include <algorithm>
#include <glm/glm.hpp>

class Texture;

class PixelCanvas
{
public:
    PixelCanvas(int width = 32, int height = 32, const glm::vec4& clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    ~PixelCanvas();

    PixelCanvas(const PixelCanvas&) = delete;
    PixelCanvas& operator=(const PixelCanvas&) = delete;

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    unsigned int GetTextureID() const { return m_TextureID; }
    const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }
    std::shared_ptr<Texture> GetTexture() const { return m_Texture; }

    void Clear(const glm::vec4& color);
    void Resize(int newWidth, int newHeight, const glm::vec4& fillColor = glm::vec4(0.0f));

    void SetPixel(int x, int y, const glm::vec4& color, float blendFactor = 1.0f);
    glm::vec4 GetPixel(int x, int y) const;

    void MarkDirtyFull();
    void MarkDirty(int x, int y);
    void UpdateGPU();

    void PushUndoSnapshot();
    bool Undo();
    bool Redo();

    void FlipHorizontal();
    void FlipVertical();
    void Rotate90CW();

private:
    void InitTexture();
    void DestroyTexture();
    static size_t PixIdx(int w, int x, int y) { return static_cast<size_t>((y * w + x) * 4); }

    int m_Width;
    int m_Height;
    unsigned int m_TextureID = 0;
    std::vector<uint8_t> m_Pixels;
    std::shared_ptr<Texture> m_Texture;

    bool m_Dirty = false;
    int m_DirtyMinX = 0, m_DirtyMinY = 0;
    int m_DirtyMaxX = 0, m_DirtyMaxY = 0;

    static constexpr size_t MAX_HISTORY = 64;
    std::deque<std::vector<uint8_t>> m_UndoStack;
    std::deque<std::vector<uint8_t>> m_RedoStack;
};
