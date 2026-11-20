#pragma once

#include <glad/glad.h>
#include <vector>
#include <cstddef>

// Persistent mapped buffer para upload de dados extremamente rápido
// Usa glMapBufferRange com GL_MAP_PERSISTENT_BIT e GL_MAP_COHERENT_BIT
// Reduz latência de CPU-GPU em até 50%
class PersistentBuffer
{
public:
    PersistentBuffer();
    ~PersistentBuffer();

    // Cria buffer com tamanho fixo
    bool Create(size_t size, GLenum usage = GL_DYNAMIC_DRAW);

    // Mapeia buffer para escrita (chamado uma vez após Create)
    bool Map();

    // Desmapeia buffer
    void Unmap();

    // Escreve dados no buffer (não precisa de glBindBuffer)
    // offset: posição em bytes
    // data: ponteiro para dados
    // size: tamanho em bytes
    void Write(size_t offset, const void* data, size_t size);

    // Sincroniza intervalo específico (para writes não-coerentes)
    void FlushRange(size_t offset, size_t size);

    // Limpa buffer com zeros
    void Clear();

    // Obtém ponteiro mapeado
    void* GetMappedPtr() const { return m_MappedPtr; }

    // Obtém tamanho total
    size_t GetSize() const { return m_Size; }

    // Obtém ID do buffer OpenGL
    GLuint GetBufferID() const { return m_BufferID; }

    // Verifica se está mapeado
    bool IsMapped() const { return m_MappedPtr != nullptr; }

    // Verifica se o hardware suporta persistent mapping
    static bool IsSupported();

private:
    GLuint m_BufferID = 0;
    void* m_MappedPtr = nullptr;
    size_t m_Size = 0;
    size_t m_WriteOffset = 0; // Para escrita sequencial
    bool m_Coherent = false; // Se usa GL_MAP_COHERENT_BIT

    static bool s_Supported;
    static bool s_SupportChecked;
};
