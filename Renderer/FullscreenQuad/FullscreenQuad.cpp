#include "FullscreenQuad.h"

#include <glad/glad.h>

FullscreenQuad& FullscreenQuad::Instance()
{
    static FullscreenQuad instance;
    return instance;
}

FullscreenQuad::FullscreenQuad()
    : m_VAO(0),
    m_VBO(0)
{
}

FullscreenQuad::~FullscreenQuad()
{
}

void FullscreenQuad::Init()
{
    float vertices[] =
    {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(
        1,
        &m_VAO
    );

    glGenBuffers(
        1,
        &m_VBO
    );

    glBindVertexArray(
        m_VAO
    );

    glBindBuffer(
        GL_ARRAY_BUFFER,
        m_VBO
    );

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(
        0
    );

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        (void*)0
    );

    glEnableVertexAttribArray(
        1
    );

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        (void*)(3 * sizeof(float))
    );

    glBindVertexArray(
        0
    );
}

void FullscreenQuad::Destroy()
{
    if (m_VAO)
    {
        glDeleteVertexArrays(
            1,
            &m_VAO
        );
    }

    if (m_VBO)
    {
        glDeleteBuffers(
            1,
            &m_VBO
        );
    }

    m_VAO = 0;
    m_VBO = 0;
}

void FullscreenQuad::Draw()
{
    glBindVertexArray(
        m_VAO
    );

    glDrawArrays(
        GL_TRIANGLE_STRIP,
        0,
        4
    );

    glBindVertexArray(
        0
    );
}
