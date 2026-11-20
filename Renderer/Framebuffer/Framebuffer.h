#pragma once

#include <glad/glad.h>

struct FramebufferSpec
{
    int Width;
    int Height;

    enum ColorFormat
    {
        RGBA8 = 0,
        RGBA16F = 1,
        R11G11B10F = 2,
        RGB8 = 3
    };

    enum DepthFormat
    {
        NONE = 0,
        DEPTH24 = 1,
        DEPTH32F = 2,
        DEPTH24_STENCIL8 = 3
    };

    ColorFormat Color = RGBA8;
    DepthFormat Depth = DEPTH24;
    bool HasStencil = false;
    bool GenerateMipmaps = false;
    GLenum MinFilter = GL_LINEAR;
    GLenum MagFilter = GL_LINEAR;
    GLenum WrapS = GL_CLAMP_TO_EDGE;
    GLenum WrapT = GL_CLAMP_TO_EDGE;
};

class Framebuffer
{
public:
    Framebuffer();
    ~Framebuffer();

    bool Create(
        int width,
        int height
    );

    bool Create(
        const FramebufferSpec& spec
    );

    void Resize(
        int width,
        int height
    );

    void Bind();
    void Unbind();

    unsigned int GetTexture() const;
    GLuint GetDepthTexture() const;
    GLuint GetFBO() const;
    const FramebufferSpec& GetSpec() const;
    int GetWidth() const;
    int GetHeight() const;

private:
    unsigned int m_FBO;
    unsigned int m_Texture;
    unsigned int m_RBO;
    FramebufferSpec m_Spec;
    GLuint m_DepthTexture = 0;
};
