#pragma once

#include "PixelCanvas.h"
#include <glm/glm.hpp>

class PixelArtTools
{
public:
    static void PencilStroke(PixelCanvas& c, int x0, int y0, int x1, int y1,
                             const glm::vec4& color, int size = 1, float alpha = 1.0f,
                             bool circular = false);
    static void EraseStroke(PixelCanvas& c, int x0, int y0, int x1, int y1,
                            int size = 1, bool circular = false);

    static void FloodFill(PixelCanvas& c, int x, int y, const glm::vec4& color);

    static glm::vec4 Eyedropper(PixelCanvas& c, int x, int y);

    static void ReplaceColor(PixelCanvas& c, const glm::vec4& oldColor,
                             const glm::vec4& newColor);

    static void Line(PixelCanvas& c, int x0, int y0, int x1, int y1,
                     const glm::vec4& color, int size = 1, float alpha = 1.0f,
                     bool circular = false);

    static void RectOutline(PixelCanvas& c, int x0, int y0, int x1, int y1,
                            const glm::vec4& color, int size = 1, float alpha = 1.0f,
                            bool circular = false);
    static void RectFilled(PixelCanvas& c, int x0, int y0, int x1, int y1,
                           const glm::vec4& color, float alpha = 1.0f);

    static void CircleOutline(PixelCanvas& c, int cx, int cy, int r,
                              const glm::vec4& color, int size = 1, float alpha = 1.0f,
                              bool circular = false);
    static void CircleFilled(PixelCanvas& c, int cx, int cy, int r,
                             const glm::vec4& color, float alpha = 1.0f);

private:
    static void PlotSize(PixelCanvas& c, int cx, int cy,
                         const glm::vec4& color, int size, float alpha,
                         bool circular = false);
    static bool ColorsEqual(const glm::vec4& a, const glm::vec4& b);
};
