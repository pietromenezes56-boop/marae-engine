#pragma once

// ---------------------------------------------------------------------------
// VulkanLightingProcessor
//
// Processador de iluminação artificial pesada usando Vulkan.
// Gerencia cálculos de iluminação para muitas luzes Point/Spot/Area.
// Ativado automaticamente quando há muitas luzes na cena.
// ---------------------------------------------------------------------------

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include "VulkanDynamicLoader.h"

struct LightData
{
    glm::vec3 Position;
    glm::vec3 Color;
    float Intensity;
    float Radius;
    int Type;  // 0=Point, 1=Spot, 2=Area
};

struct VulkanLightingConfig
{
    bool Enabled = false;
    int MaxLights = 256;  // Muito mais que o limite padrão do OpenGL
    int LightCountThreshold = 32;  // Ativa quando tem >32 luzes
    bool UseComputeShaders = true;  // Usar compute shaders para cálculo de iluminação
};

class VulkanLightingProcessor
{
public:
    VulkanLightingProcessor();
    ~VulkanLightingProcessor();

    bool Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    VkQueue graphicsQueue, VkQueue computeQueue,
                    uint32_t graphicsQueueFamilyIndex, uint32_t computeQueueFamilyIndex);
    void Shutdown();

    bool CanProcess(const VulkanLightingConfig& config, int lightCount) const;
    bool ProcessLighting(const std::vector<LightData>& lights, const VulkanLightingConfig& config);

    VkBuffer GetLightDataBuffer() const { return m_LightDataBuffer; }
    VkDeviceMemory GetLightDataMemory() const { return m_LightDataMemory; }

private:
    bool CreateLightBuffers(const VulkanLightingConfig& config);
    bool CreateComputePipeline();
    bool CreateCommandBuffers();

    void CleanupLightBuffers();
    void CleanupComputePipeline();
    void CleanupCommandBuffers();

    VulkanDynamicLoader* m_Loader = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_ComputeQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = 0;
    uint32_t m_ComputeQueueFamilyIndex = 0;

    // Light data buffers
    VkBuffer m_LightDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_LightDataMemory = VK_NULL_HANDLE;
    VkBuffer m_LightResultBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_LightResultMemory = VK_NULL_HANDLE;

    // Compute pipeline
    VkPipeline m_ComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_ComputePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

    // Command buffers
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

    bool m_Initialized = false;
    VulkanLightingConfig m_LastConfig;
};
