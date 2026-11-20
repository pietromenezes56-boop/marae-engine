#pragma once

#include <memory>

struct GLFWwindow;
struct ImDrawData;

class VulkanImGuiRenderer
{
public:
    VulkanImGuiRenderer();
    ~VulkanImGuiRenderer();

    VulkanImGuiRenderer(const VulkanImGuiRenderer&) = delete;
    VulkanImGuiRenderer& operator=(const VulkanImGuiRenderer&) = delete;

    bool Initialize(GLFWwindow* window);
    bool Render(ImDrawData* drawData);
    void Shutdown();
    bool IsInitialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
