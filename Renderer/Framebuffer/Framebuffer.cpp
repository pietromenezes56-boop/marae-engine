#include "Framebuffer.h"

#include <glad/glad.h>

Framebuffer::Framebuffer()
    : m_FBO(0),
    m_Texture(0),
    m_RBO(0),
    m_Spec(),
    m_DepthTexture(0)
{
}

Framebuffer::~Framebuffer()
{
    if (m_FBO)
    {
        glDeleteFramebuffers(
            1,
            &m_FBO
        );
    }

    if (m_Texture)
    {
        glDeleteTextures(
            1,
            &m_Texture
        );
    }

    if (m_RBO)
    {
        glDeleteRenderbuffers(
            1,
            &m_RBO
        );
    }

    if (m_DepthTexture)
    {
        glDeleteTextures(
            1,
            &m_DepthTexture
        );
    }
}

bool Framebuffer::Create(
    int width,
    int height)
{
    m_Spec.Width = width;
    m_Spec.Height = height;
    m_Spec.Color = FramebufferSpec::RGB8;
    m_Spec.Depth = FramebufferSpec::DEPTH24_STENCIL8;
    m_Spec.HasStencil = true;
    m_Spec.GenerateMipmaps = false;
    m_Spec.MinFilter = GL_LINEAR;
    m_Spec.MagFilter = GL_LINEAR;
    m_Spec.WrapS = GL_CLAMP_TO_EDGE;
    m_Spec.WrapT = GL_CLAMP_TO_EDGE;

    glGenFramebuffers(
        1,
        &m_FBO
    );

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        m_FBO
    );

    glGenTextures(
        1,
        &m_Texture
    );

    glBindTexture(
        GL_TEXTURE_2D,
        m_Texture
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        width,
        height,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR
    );

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_Texture,
        0
    );

    glGenRenderbuffers(
        1,
        &m_RBO
    );

    glBindRenderbuffer(
        GL_RENDERBUFFER,
        m_RBO
    );

    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        width,
        height
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        m_RBO
    );

    bool complete =
        glCheckFramebufferStatus(
            GL_FRAMEBUFFER
        ) == GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        0
    );

    return complete;
}

bool Framebuffer::Create(
    const FramebufferSpec& spec)
{
    m_Spec = spec;

    GLint internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;

    switch (spec.Color)
    {
    case FramebufferSpec::RGBA8:
        internalFormat = GL_RGBA8;
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
    case FramebufferSpec::RGBA16F:
        internalFormat = GL_RGBA16F;
        format = GL_RGBA;
        type = GL_HALF_FLOAT;
        break;
    case FramebufferSpec::R11G11B10F:
        internalFormat = GL_R11F_G11F_B10F;
        format = GL_RGB;
        type = GL_HALF_FLOAT;
        break;
    case FramebufferSpec::RGB8:
        internalFormat = GL_RGB8;
        format = GL_RGB;
        type = GL_UNSIGNED_BYTE;
        break;
    }

    glGenFramebuffers(
        1,
        &m_FBO
    );

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        m_FBO
    );

    glGenTextures(
        1,
        &m_Texture
    );

    glBindTexture(
        GL_TEXTURE_2D,
        m_Texture
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internalFormat,
        spec.Width,
        spec.Height,
        0,
        format,
        type,
        nullptr
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        spec.MinFilter
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        spec.MagFilter
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        spec.WrapS
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        spec.WrapT
    );

    if (spec.GenerateMipmaps)
    {
        glGenerateMipmap(
            GL_TEXTURE_2D
        );
    }

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_Texture,
        0
    );

    if (spec.Depth != FramebufferSpec::NONE)
    {
        if (spec.Depth == FramebufferSpec::DEPTH24_STENCIL8)
        {
            glGenRenderbuffers(
                1,
                &m_RBO
            );

            glBindRenderbuffer(
                GL_RENDERBUFFER,
                m_RBO
            );

            glRenderbufferStorage(
                GL_RENDERBUFFER,
                GL_DEPTH24_STENCIL8,
                spec.Width,
                spec.Height
            );

            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,
                m_RBO
            );
        }
        else
        {
            GLenum depthInternal = GL_DEPTH_COMPONENT24;
            GLenum depthFormat = GL_DEPTH_COMPONENT;
            GLenum depthType = GL_UNSIGNED_INT;

            switch (spec.Depth)
            {
            case FramebufferSpec::DEPTH24:
                depthInternal = GL_DEPTH_COMPONENT24;
                depthFormat = GL_DEPTH_COMPONENT;
                depthType = GL_UNSIGNED_INT;
                break;
            case FramebufferSpec::DEPTH32F:
                depthInternal = GL_DEPTH_COMPONENT32F;
                depthFormat = GL_DEPTH_COMPONENT;
                depthType = GL_FLOAT;
                break;
            default:
                break;
            }

            glGenTextures(
                1,
                &m_DepthTexture
            );

            glBindTexture(
                GL_TEXTURE_2D,
                m_DepthTexture
            );

            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                depthInternal,
                spec.Width,
                spec.Height,
                0,
                depthFormat,
                depthType,
                nullptr
            );

            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER,
                spec.MinFilter
            );

            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                spec.MagFilter
            );

            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_S,
                spec.WrapS
            );

            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_T,
                spec.WrapT
            );

            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D,
                m_DepthTexture,
                0
            );
        }
    }

    bool complete =
        glCheckFramebufferStatus(
            GL_FRAMEBUFFER
        ) == GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        0
    );

    return complete;
}

void Framebuffer::Resize(
    int width,
    int height)
{
    if (width <= 0 ||
        height <= 0)
    {
        return;
    }

    m_Spec.Width = width;
    m_Spec.Height = height;

    GLint internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;

    switch (m_Spec.Color)
    {
    case FramebufferSpec::RGBA8:
        internalFormat = GL_RGBA8;
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
    case FramebufferSpec::RGBA16F:
        internalFormat = GL_RGBA16F;
        format = GL_RGBA;
        type = GL_HALF_FLOAT;
        break;
    case FramebufferSpec::R11G11B10F:
        internalFormat = GL_R11F_G11F_B10F;
        format = GL_RGB;
        type = GL_HALF_FLOAT;
        break;
    case FramebufferSpec::RGB8:
        internalFormat = GL_RGB8;
        format = GL_RGB;
        type = GL_UNSIGNED_BYTE;
        break;
    }

    glBindTexture(
        GL_TEXTURE_2D,
        m_Texture
    );

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internalFormat,
        width,
        height,
        0,
        format,
        type,
        nullptr
    );

    if (m_Spec.GenerateMipmaps)
    {
        glGenerateMipmap(
            GL_TEXTURE_2D
        );
    }

    if (m_Spec.Depth != FramebufferSpec::NONE)
    {
        if (m_Spec.Depth == FramebufferSpec::DEPTH24_STENCIL8)
        {
            glBindRenderbuffer(
                GL_RENDERBUFFER,
                m_RBO
            );

            glRenderbufferStorage(
                GL_RENDERBUFFER,
                GL_DEPTH24_STENCIL8,
                width,
                height
            );
        }
        else
        {
            GLenum depthInternal = GL_DEPTH_COMPONENT24;
            GLenum depthFormat = GL_DEPTH_COMPONENT;
            GLenum depthType = GL_UNSIGNED_INT;

            switch (m_Spec.Depth)
            {
            case FramebufferSpec::DEPTH24:
                depthInternal = GL_DEPTH_COMPONENT24;
                depthFormat = GL_DEPTH_COMPONENT;
                depthType = GL_UNSIGNED_INT;
                break;
            case FramebufferSpec::DEPTH32F:
                depthInternal = GL_DEPTH_COMPONENT32F;
                depthFormat = GL_DEPTH_COMPONENT;
                depthType = GL_FLOAT;
                break;
            default:
                break;
            }

            glBindTexture(
                GL_TEXTURE_2D,
                m_DepthTexture
            );

            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                depthInternal,
                width,
                height,
                0,
                depthFormat,
                depthType,
                nullptr
            );
        }
    }

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        m_FBO
    );

    glCheckFramebufferStatus(
        GL_FRAMEBUFFER
    );

    glBindFramebuffer(
        GL_FRAMEBUFFER,
        0
    );
}

void Framebuffer::Bind()
{
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        m_FBO
    );
}

void Framebuffer::Unbind()
{
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        0
    );
}

unsigned int Framebuffer::GetTexture() const
{
    return m_Texture;
}

GLuint Framebuffer::GetDepthTexture() const
{
    return m_DepthTexture;
}

GLuint Framebuffer::GetFBO() const
{
    return m_FBO;
}

const FramebufferSpec& Framebuffer::GetSpec() const
{
    return m_Spec;
}

int Framebuffer::GetWidth() const
{
    return m_Spec.Width;
}

int Framebuffer::GetHeight() const
{
    return m_Spec.Height;
}
