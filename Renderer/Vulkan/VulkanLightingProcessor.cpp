#include "VulkanLightingProcessor.h"
#include <iostream>

VulkanLightingProcessor::VulkanLightingProcessor()
{
}

VulkanLightingProcessor::~VulkanLightingProcessor()
{
    Shutdown();
}

bool VulkanLightingProcessor::Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice,
                                         VkDevice device, VkQueue graphicsQueue, VkQueue computeQueue,
                                         uint32_t graphicsQueueFamilyIndex, uint32_t computeQueueFamilyIndex)
{
    m_Loader = loader;
    m_Instance = instance;
    m_PhysicalDevice = physicalDevice;
    m_Device = device;
    m_GraphicsQueue = graphicsQueue;
    m_ComputeQueue = computeQueue;
    m_GraphicsQueueFamilyIndex = graphicsQueueFamilyIndex;
    m_ComputeQueueFamilyIndex = computeQueueFamilyIndex;

    if (!m_Loader || !m_Loader->IsAvailable())
    {
        std::cout << "[VulkanLightingProcessor] Vulkan loader not available\n";
        return false;
    }

    if (!CreateCommandBuffers())
    {
        std::cout << "[VulkanLightingProcessor] Failed to create command buffers\n";
        return false;
    }

    m_Initialized = true;
    std::cout << "[VulkanLightingProcessor] Initialized successfully\n";
    return true;
}

void VulkanLightingProcessor::Shutdown()
{
    if (!m_Initialized)
        return;

    CleanupComputePipeline();
    CleanupLightBuffers();
    CleanupCommandBuffers();

    m_Initialized = false;
}

bool VulkanLightingProcessor::CanProcess(const VulkanLightingConfig& config, int lightCount) const
{
    if (!config.Enabled)
        return false;
    if (!m_Initialized)
        return false;
    if (lightCount < config.LightCountThreshold)
        return false;

    return true;
}

bool VulkanLightingProcessor::ProcessLighting(const std::vector<LightData>& lights, 
                                                 const VulkanLightingConfig& config)
{
    if (!m_Initialized)
        return false;

    // Recreate buffers if config changed
    if (!m_LightDataBuffer || config.MaxLights != m_LastConfig.MaxLights)
    {
        CleanupLightBuffers();
        if (!CreateLightBuffers(config))
        {
            std::cout << "[VulkanLightingProcessor] Failed to create light buffers\n";
            return false;
        }
        m_LastConfig = config;
    }

    // TODO: Implement actual lighting processing
    // This would involve:
    // 1. Upload light data to GPU buffer
    // 2. Run compute shader to calculate lighting contributions
    // 3. Retrieve results for OpenGL to use
    // 4. Sync between Vulkan and OpenGL via shared memory or texture

    return true;
}

bool VulkanLightingProcessor::CreateLightBuffers(const VulkanLightingConfig& config)
{
    // Create light data buffer (input)
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(LightData) * config.MaxLights;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_Loader->vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_LightDataBuffer) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to create light data buffer\n";
        return false;
    }

    VkMemoryRequirements memRequirements;
    m_Loader->vkGetBufferMemoryRequirements(m_Device, m_LightDataBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find appropriate memory type

    if (m_Loader->vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_LightDataMemory) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to allocate light data memory\n";
        m_Loader->vkDestroyBuffer(m_Device, m_LightDataBuffer, nullptr);
        return false;
    }

    m_Loader->vkBindBufferMemory(m_Device, m_LightDataBuffer, m_LightDataMemory, 0);

    // Create result buffer (output)
    bufferInfo.size = sizeof(glm::vec4) * config.MaxLights;  // Lighting result per light
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    if (m_Loader->vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_LightResultBuffer) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to create light result buffer\n";
        return false;
    }

    m_Loader->vkGetBufferMemoryRequirements(m_Device, m_LightResultBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;

    if (m_Loader->vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_LightResultMemory) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to allocate light result memory\n";
        m_Loader->vkDestroyBuffer(m_Device, m_LightResultBuffer, nullptr);
        return false;
    }

    m_Loader->vkBindBufferMemory(m_Device, m_LightResultBuffer, m_LightResultMemory, 0);

    std::cout << "[VulkanLightingProcessor] Light buffers created (max " 
              << config.MaxLights << " lights)\n";
    return true;
}

bool VulkanLightingProcessor::CreateComputePipeline()
{
    // TODO: Create compute pipeline for lighting calculations
    // This requires SPIR-V compute shader
    return true;
}

bool VulkanLightingProcessor::CreateCommandBuffers()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_ComputeQueueFamilyIndex;

    if (m_Loader->vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to create command pool\n";
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (m_Loader->vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS)
    {
        std::cout << "[VulkanLightingProcessor] Failed to allocate command buffer\n";
        return false;
    }

    return true;
}

void VulkanLightingProcessor::CleanupLightBuffers()
{
    if (m_LightResultMemory)
        m_Loader->vkFreeMemory(m_Device, m_LightResultMemory, nullptr);
    if (m_LightResultBuffer)
        m_Loader->vkDestroyBuffer(m_Device, m_LightResultBuffer, nullptr);
    if (m_LightDataMemory)
        m_Loader->vkFreeMemory(m_Device, m_LightDataMemory, nullptr);
    if (m_LightDataBuffer)
        m_Loader->vkDestroyBuffer(m_Device, m_LightDataBuffer, nullptr);

    m_LightResultMemory = VK_NULL_HANDLE;
    m_LightResultBuffer = VK_NULL_HANDLE;
    m_LightDataMemory = VK_NULL_HANDLE;
    m_LightDataBuffer = VK_NULL_HANDLE;
}

void VulkanLightingProcessor::CleanupComputePipeline()
{
    if (m_DescriptorSet)
        m_Loader->vkFreeDescriptorSets(m_Device, m_DescriptorPool, 1, &m_DescriptorSet);
    if (m_DescriptorPool)
        m_Loader->vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
    if (m_DescriptorSetLayout)
        m_Loader->vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
    if (m_ComputePipeline)
        m_Loader->vkDestroyPipeline(m_Device, m_ComputePipeline, nullptr);
    if (m_ComputePipelineLayout)
        m_Loader->vkDestroyPipelineLayout(m_Device, m_ComputePipelineLayout, nullptr);

    m_DescriptorSet = VK_NULL_HANDLE;
    m_DescriptorPool = VK_NULL_HANDLE;
    m_DescriptorSetLayout = VK_NULL_HANDLE;
    m_ComputePipeline = VK_NULL_HANDLE;
    m_ComputePipelineLayout = VK_NULL_HANDLE;
}

void VulkanLightingProcessor::CleanupCommandBuffers()
{
    if (m_CommandBuffer)
        m_Loader->vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &m_CommandBuffer);
    if (m_CommandPool)
        m_Loader->vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    m_CommandBuffer = VK_NULL_HANDLE;
    m_CommandPool = VK_NULL_HANDLE;
}
