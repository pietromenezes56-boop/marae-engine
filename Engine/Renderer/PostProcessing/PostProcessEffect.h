#pragma once

#include "../../../../Renderer/Framebuffer/Framebuffer.h"
#include "CommonStructs.h"
#include <glad/glad.h>

class PostProcessEffect
{
public:
    virtual ~PostProcessEffect() = default;
    virtual const char* Name() const = 0;
    virtual bool IsEnabled(const BlendedSettings& s) const = 0;
    virtual void SetQuality(GraphicsQuality q) { (void)q; }
    virtual bool Init(class ShaderLoader* helper) { (void)helper; return true; }
    virtual void OnResize(int w, int h) { (void)w; (void)h; }
    virtual void Render(GLuint srcTex, Framebuffer& dst, const BlendedSettings& settings, int viewW, int viewH) = 0;

protected:
    PostProcessEffect() = default;
};
