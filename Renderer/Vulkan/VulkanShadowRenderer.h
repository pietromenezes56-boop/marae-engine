#pragma once

// ---------------------------------------------------------------------------
// VulkanShadowRenderer
//
// Renderizador de sombras de ultra qualidade usando Vulkan.
// Ativado automaticamente quando a cena tem muitos objetos ou sombras de alta qualidade.
// O OpenGL continua sendo o renderizador principal do viewport.
// ---------------------------------------------------------------------------

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include "VulkanDynamicLoader.h"

struct VulkanShadowConfig
{
    bool Enabled = false;
    int MaxShadowCascades = 4;
    int ShadowMapResolution = 4096;  // Ultra qualidade
    float QualityThreshold = 50;     // Ativa quando tem >50 objetos
    bool UsePCF = true;
    bool UseVSM = false;  // Variance Shadow Maps (experimental)
};

class VulkanShadowRenderer
{
public:
    VulkanShadowRenderer();
    ~VulkanShadowRenderer();

    bool Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, 
                    VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex);
    void Shutdown();

    bool CanRender(const VulkanShadowConfig& config, int objectCount) const;
    bool RenderShadowMap(const glm::mat4& lightViewMatrix, const glm::mat4& lightProjMatrix,
                        const std::vector<glm::mat4>& objectTransforms,
                        const VulkanShadowConfig& config);

    VkImageView GetShadowMapView() const { return m_ShadowMapView; }
    VkSampler GetShadowMapSampler() const { return m_ShadowMapSampler; }

private:
    bool CreateShadowMapResources(const VulkanShadowConfig& config);
    bool CreateShadowPipeline();
    bool CreateCommandBuffers();

    void CleanupShadowMapResources();
    void CleanupShadowPipeline();
    void CleanupCommandBuffers();

    VulkanDynamicLoader* m_Loader = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = 0;

    // Shadow map resources
    VkImage m_ShadowMapImage = VK_NULL_HANDLE;
    VkDeviceMemory m_ShadowMapMemory = VK_NULL_HANDLE;
    VkImageView m_ShadowMapView = VK_NULL_HANDLE;
    VkSampler m_ShadowMapSampler = VK_NULL_HANDLE;
    VkFramebuffer m_ShadowFramebuffer = VK_NULL_HANDLE;

    // Pipeline
    VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_ShadowPipelineLayout = VK_NULL_HANDLE;

    // Command buffers
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

    bool m_Initialized = false;
    VulkanShadowConfig m_LastConfig;
};
