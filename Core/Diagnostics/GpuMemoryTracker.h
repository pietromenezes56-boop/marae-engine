#pragma once
// ---------------------------------------------------------------------------
// GpuMemoryTracker — estimativa de VRAM alocada pela engine.
//
// Drivers OpenGL não expõem um contador confiável de memória de vídeo em
// todos os adaptadores. Este tracker soma os bytes que a engine efetivamente
// sobe para a GPU (texturas, buffers de mesh) no momento do upload e subtrai
// no delete/rebuild. É uma estimativa do lado da engine — não inclui overhead
// do driver, mipmaps gerados internamente nem alvos de framebuffer.
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>

namespace GpuMemoryTracker
{
    void Add(int64_t bytes);
    void Remove(int64_t bytes);
    int64_t GetAllocatedBytes();
}
