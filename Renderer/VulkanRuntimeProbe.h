#pragma once

#include <iosfwd>

namespace VulkanRuntimeProbe
{
    // First Vulkan integration milestone: validate the system loader, create a
    // Vulkan 1.0-compatible instance, and enumerate physical devices. This is
    // intentionally independent from the active OpenGL renderer.
    bool RunDiagnostics(std::ostream& output);
}
