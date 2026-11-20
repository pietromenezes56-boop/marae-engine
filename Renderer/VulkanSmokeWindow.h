#pragma once

#include <iosfwd>

namespace VulkanSmokeWindow
{
    // Opens an isolated, non-resizable GLFW Vulkan window, presents one clear
    // frame, then closes automatically. GLFW must already be initialized.
    bool PresentOneFrame(std::ostream& output);
}
