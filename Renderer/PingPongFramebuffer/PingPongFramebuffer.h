#pragma once

#include "../Framebuffer/Framebuffer.h"

class PingPongFramebuffer
{
public:
    PingPongFramebuffer();

    bool Create(
        const FramebufferSpec& spec
    );

    void Resize(
        int width,
        int height
    );

    void Swap();

    Framebuffer& GetCurrent() const;
    Framebuffer& GetNext() const;

    GLuint GetCurrentTex() const;
    GLuint GetNextTex() const;

private:
    Framebuffer m_A;
    Framebuffer m_B;
    Framebuffer* m_Current = &m_A;
    Framebuffer* m_Next = &m_B;
};
