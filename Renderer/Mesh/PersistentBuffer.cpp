#include "PersistentBuffer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <cstdio>

#define _CRT_SECURE_NO_WARNINGS

bool PersistentBuffer::s_Supported = false;
bool PersistentBuffer::s_SupportChecked = false;

PersistentBuffer::PersistentBuffer()
    : m_BufferID(0)
    , m_MappedPtr(nullptr)
    , m_Size(0)
    , m_WriteOffset(0)
    , m_Coherent(false)
{
}

PersistentBuffer::~PersistentBuffer()
{
    if (m_MappedPtr)
    {
        Unmap();
    }

    if (m_BufferID != 0)
    {
        glDeleteBuffers(1, &m_BufferID);
        m_BufferID = 0;
    }
}

bool PersistentBuffer::IsSupported()
{
    if (s_SupportChecked)
        return s_Supported;

    s_SupportChecked = true;

    // Verifica se OpenGL 4.4+ está disponível (persistent mapping é 4.4)
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version)
    {
        int major = 0, minor = 0;
        if (sscanf_s(version, "%d.%d", &major, &minor) == 2)
        {
            s_Supported = (major > 4) || (major == 4 && minor >= 4);
        }
    }

    // Também verifica extensão ARB_buffer_storage
    if (!s_Supported)
    {
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions)
        {
            std::string extStr(extensions);
            s_Supported = extStr.find("GL_ARB_buffer_storage") != std::string::npos;
        }
    }

    // Verifica se glBufferStorage está disponível via glfwGetProcAddress
    if (s_Supported)
    {
        typedef void (*PFNGLBUFFERSTORAGEPROC)(GLenum, GLsizeiptr, const void*, GLbitfield);
        PFNGLBUFFERSTORAGEPROC glBufferStorageFunc = (PFNGLBUFFERSTORAGEPROC)glfwGetProcAddress("glBufferStorage");
        if (glBufferStorageFunc == nullptr)
        {
            s_Supported = false;
            std::cout << "[PersistentBuffer] glBufferStorage não disponível via glfwGetProcAddress, usando fallback\n";
        }
    }

    if (s_Supported)
    {
        std::cout << "[PersistentBuffer] Hardware suporta persistent mapping\n";
    }
    else
    {
        std::cout << "[PersistentBuffer] Hardware NÃO suporta persistent mapping, usando fallback\n";
    }

    return s_Supported;
}

bool PersistentBuffer::Create(size_t size, GLenum usage)
{
    if (m_BufferID != 0)
    {
        glDeleteBuffers(1, &m_BufferID);
        m_BufferID = 0;
    }

    if (m_MappedPtr)
    {
        Unmap();
    }

    m_Size = size;
    m_WriteOffset = 0;

    glGenBuffers(1, &m_BufferID);
    glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);

    // Verifica se glBufferStorage está disponível em runtime
    typedef void (*PFNGLBUFFERSTORAGEPROC)(GLenum, GLsizeiptr, const void*, GLbitfield);
    PFNGLBUFFERSTORAGEPROC glBufferStorageFunc = (PFNGLBUFFERSTORAGEPROC)glfwGetProcAddress("glBufferStorage");

    if (IsSupported() && glBufferStorageFunc != nullptr)
    {
        // Usa glBufferStorage para persistent mapping (OpenGL 4.4+)
        GLbitfield flags = GL_MAP_WRITE_BIT;
        flags |= 0x2000; // GL_MAP_PERSISTENT_BIT
        flags |= 0x0080; // GL_MAP_COHERENT_BIT

        glBufferStorageFunc(GL_COPY_WRITE_BUFFER, size, nullptr, flags);
        m_Coherent = true;
    }
    else
    {
        // Fallback para glBufferData tradicional
        glBufferData(GL_COPY_WRITE_BUFFER, size, nullptr, usage);
        m_Coherent = false;
    }

    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    return true;
}

bool PersistentBuffer::Map()
{
    if (m_BufferID == 0 || m_Size == 0)
        return false;

    if (m_MappedPtr != nullptr)
        return true; // Já mapeado

    glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);

    if (IsSupported())
    {
        GLbitfield access = GL_MAP_WRITE_BIT;
        access |= 0x2000; // GL_MAP_PERSISTENT_BIT
        access |= 0x0080; // GL_MAP_COHERENT_BIT

        m_MappedPtr = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, m_Size, access);
    }
    else
    {
        // Fallback: mapeia temporariamente para cada write
        m_MappedPtr = nullptr;
    }

    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    if (!m_MappedPtr && IsSupported())
    {
        std::cerr << "[PersistentBuffer] Falha ao mapear buffer\n";
        return false;
    }

    return true;
}

void PersistentBuffer::Unmap()
{
    if (m_MappedPtr == nullptr)
        return;

    glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);
    glUnmapBuffer(GL_COPY_WRITE_BUFFER);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    m_MappedPtr = nullptr;
}

void PersistentBuffer::Write(size_t offset, const void* data, size_t size)
{
    if (m_BufferID == 0 || size == 0 || data == nullptr)
        return;

    if (offset + size > m_Size)
    {
        std::cerr << "[PersistentBuffer] Write fora dos limites do buffer\n";
        return;
    }

    if (IsSupported() && m_MappedPtr)
    {
        // Write direto na memória mapeada (extremamente rápido)
        memcpy(static_cast<char*>(m_MappedPtr) + offset, data, size);
    }
    else
    {
        // Fallback: mapeia, escreve, desmapeia
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);
        void* ptr = glMapBufferRange(GL_COPY_WRITE_BUFFER, offset, size,
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if (ptr)
        {
            memcpy(ptr, data, size);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }

    m_WriteOffset = offset + size;
}

void PersistentBuffer::FlushRange(size_t offset, size_t size)
{
    if (!IsSupported() || m_Coherent || m_MappedPtr == nullptr)
        return; // Coherent buffers não precisam de flush

    glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);
    glFlushMappedBufferRange(GL_COPY_WRITE_BUFFER, offset, size);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void PersistentBuffer::Clear()
{
    if (m_MappedPtr)
    {
        memset(m_MappedPtr, 0, m_Size);
    }
    else if (m_BufferID != 0)
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, m_BufferID);
        void* ptr = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, m_Size,
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        if (ptr)
        {
            memset(ptr, 0, m_Size);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    m_WriteOffset = 0;
}
