#include "PixelArtTools.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <stack>

void PixelArtTools::PlotSize(PixelCanvas& c, int cx, int cy,
                             const glm::vec4& color, int size, float alpha,
                             bool circular)
{
    size = std::clamp(size, 1, 256);
    const int minOffset = -(size - 1) / 2;
    const int maxOffset = size / 2;
    const float centerOffset = (minOffset + maxOffset) * 0.5f;
    const float radius = std::max(0.5f, size * 0.5f);
    for (int dy = minOffset; dy <= maxOffset; ++dy)
        for (int dx = minOffset; dx <= maxOffset; ++dx)
        {
            const float centeredX = dx - centerOffset;
            const float centeredY = dy - centerOffset;
            if (circular && centeredX * centeredX + centeredY * centeredY > radius * radius)
                continue;
            c.SetPixel(cx + dx, cy + dy, color, alpha);
        }
}

bool PixelArtTools::ColorsEqual(const glm::vec4& a, const glm::vec4& b)
{
    return (std::abs(a.r - b.r) < 0.005f &&
            std::abs(a.g - b.g) < 0.005f &&
            std::abs(a.b - b.b) < 0.005f &&
            std::abs(a.a - b.a) < 0.005f);
}

void PixelArtTools::PencilStroke(PixelCanvas& c, int x0, int y0, int x1, int y1,
                                 const glm::vec4& color, int size, float alpha,
                                 bool circular)
{
    Line(c, x0, y0, x1, y1, color, size, alpha, circular);
}

void PixelArtTools::EraseStroke(PixelCanvas& c, int x0, int y0, int x1, int y1,
                                int size, bool circular)
{
    Line(c, x0, y0, x1, y1, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), size, 1.0f, circular);
}

void PixelArtTools::Line(PixelCanvas& c, int x0, int y0, int x1, int y1,
                         const glm::vec4& color, int size, float alpha,
                         bool circular)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;

    const int maxSteps = 1 + std::abs(x1 - x0) + std::abs(y1 - y0);
    for (int i = 0; i < maxSteps; ++i)
    {
        PlotSize(c, x, y, color, size, alpha, circular);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void PixelArtTools::FloodFill(PixelCanvas& c, int x, int y, const glm::vec4& newColor)
{
    int w = c.GetWidth(), h = c.GetHeight();
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    glm::vec4 target = c.GetPixel(x, y);
    if (ColorsEqual(target, newColor)) return;

    std::stack<std::pair<int, int>> stk;
    stk.push({ x, y });

    const int maxIter = w * h * 4;
    int iter = 0;
    while (!stk.empty() && iter < maxIter)
    {
        iter++;
        auto [cx, cy] = stk.top();
        stk.pop();
        while (cy >= 0 && ColorsEqual(c.GetPixel(cx, cy), target)) cy--;
        cy++;
        bool spanLeft = false, spanRight = false;
        while (cy < h && ColorsEqual(c.GetPixel(cx, cy), target))
        {
            c.SetPixel(cx, cy, newColor, 1.0f);
            if (cx > 0)
            {
                if (ColorsEqual(c.GetPixel(cx - 1, cy), target))
                {
                    if (!spanLeft) { stk.push({ cx - 1, cy }); spanLeft = true; }
                }
                else spanLeft = false;
            }
            if (cx < w - 1)
            {
                if (ColorsEqual(c.GetPixel(cx + 1, cy), target))
                {
                    if (!spanRight) { stk.push({ cx + 1, cy }); spanRight = true; }
                }
                else spanRight = false;
            }
            cy++;
        }
    }
}

glm::vec4 PixelArtTools::Eyedropper(PixelCanvas& c, int x, int y)
{
    return c.GetPixel(x, y);
}

void PixelArtTools::ReplaceColor(PixelCanvas& c, const glm::vec4& oldColor,
                                 const glm::vec4& newColor)
{
    if (ColorsEqual(oldColor, newColor))
        return;

    for (int y = 0; y < c.GetHeight(); ++y)
    {
        for (int x = 0; x < c.GetWidth(); ++x)
        {
            if (ColorsEqual(c.GetPixel(x, y), oldColor))
                c.SetPixel(x, y, newColor);
        }
    }
}

void PixelArtTools::RectOutline(PixelCanvas& c, int x0, int y0, int x1, int y1,
                                const glm::vec4& color, int size, float alpha,
                                bool circular)
{
    int lx = std::min(x0, x1), rx = std::max(x0, x1);
    int ty = std::min(y0, y1), by = std::max(y0, y1);
    Line(c, lx, ty, rx, ty, color, size, alpha, circular);
    Line(c, rx, ty, rx, by, color, size, alpha, circular);
    Line(c, rx, by, lx, by, color, size, alpha, circular);
    Line(c, lx, by, lx, ty, color, size, alpha, circular);
}

void PixelArtTools::RectFilled(PixelCanvas& c, int x0, int y0, int x1, int y1,
                               const glm::vec4& color, float alpha)
{
    int lx = std::min(x0, x1), rx = std::max(x0, x1);
    int ty = std::min(y0, y1), by = std::max(y0, y1);
    for (int yy = ty; yy <= by; ++yy)
        for (int xx = lx; xx <= rx; ++xx)
            c.SetPixel(xx, yy, color, alpha);
}

void PixelArtTools::CircleOutline(PixelCanvas& c, int cx, int cy, int r,
                                  const glm::vec4& color, int size, float alpha,
                                  bool circular)
{
    int x = 0, y = r;
    int d = 1 - r;
    auto plot8 = [&](int xi, int yi)
    {
        PlotSize(c, cx + xi, cy + yi, color, size, alpha, circular);
        PlotSize(c, cx - xi, cy + yi, color, size, alpha, circular);
        PlotSize(c, cx + xi, cy - yi, color, size, alpha, circular);
        PlotSize(c, cx - xi, cy - yi, color, size, alpha, circular);
        PlotSize(c, cx + yi, cy + xi, color, size, alpha, circular);
        PlotSize(c, cx - yi, cy + xi, color, size, alpha, circular);
        PlotSize(c, cx + yi, cy - xi, color, size, alpha, circular);
        PlotSize(c, cx - yi, cy - xi, color, size, alpha, circular);
    };
    plot8(x, y);
    while (x < y)
    {
        if (d < 0) d += 2 * x + 3;
        else { d += 2 * (x - y) + 5; y--; }
        x++;
        plot8(x, y);
    }
}

void PixelArtTools::CircleFilled(PixelCanvas& c, int cx, int cy, int r,
                                 const glm::vec4& color, float alpha)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r + r)
                c.SetPixel(cx + x, cy + y, color, alpha);
}
