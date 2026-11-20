#include "GpuMemoryTracker.h"

namespace
{
    std::atomic<int64_t> s_AllocatedBytes{ 0 };
}

void GpuMemoryTracker::Add(int64_t bytes)
{
    if (bytes > 0)
        s_AllocatedBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void GpuMemoryTracker::Remove(int64_t bytes)
{
    if (bytes > 0)
        s_AllocatedBytes.fetch_sub(bytes, std::memory_order_relaxed);
}

int64_t GpuMemoryTracker::GetAllocatedBytes()
{
    return s_AllocatedBytes.load(std::memory_order_relaxed);
}
