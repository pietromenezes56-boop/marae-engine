#include "PingPongFramebuffer.h"

PingPongFramebuffer::PingPongFramebuffer()
    : m_A(),
    m_B(),
    m_Current(&m_A),
    m_Next(&m_B)
{
}

bool PingPongFramebuffer::Create(
    const FramebufferSpec& spec)
{
    bool okA = m_A.Create(
        spec
    );

    bool okB = m_B.Create(
        spec
    );

    return okA && okB;
}

void PingPongFramebuffer::Resize(
    int width,
    int height)
{
    m_A.Resize(
        width,
        height
    );

    m_B.Resize(
        width,
        height
    );
}

void PingPongFramebuffer::Swap()
{
    Framebuffer* temp = m_Current;
    m_Current = m_Next;
    m_Next = temp;
}

Framebuffer& PingPongFramebuffer::GetCurrent() const
{
    return *m_Current;
}

Framebuffer& PingPongFramebuffer::GetNext() const
{
    return *m_Next;
}

GLuint PingPongFramebuffer::GetCurrentTex() const
{
    return m_Current->GetTexture();
}

GLuint PingPongFramebuffer::GetNextTex() const
{
    return m_Next->GetTexture();
}
