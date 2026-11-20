#pragma once

#include <glad/glad.h>

class FullscreenQuad
{
public:
    static FullscreenQuad& Instance();

    void Init();
    void Destroy();
    void Draw();

private:
    FullscreenQuad();
    ~FullscreenQuad();

    FullscreenQuad(
        const FullscreenQuad&
    ) = delete;

    FullscreenQuad& operator=(
        const FullscreenQuad&
    ) = delete;

    GLuint m_VAO;
    GLuint m_VBO;
};
