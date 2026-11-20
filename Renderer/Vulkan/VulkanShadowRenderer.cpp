#include "VulkanShadowRenderer.h"
#include <iostream>

VulkanShadowRenderer::VulkanShadowRenderer()
{
}

VulkanShadowRenderer::~VulkanShadowRenderer()
{
    Shutdown();
}

bool VulkanShadowRenderer::Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, 
                                       VkDevice device, VkQueue graphicsQueue, 
                                       uint32_t graphicsQueueFamilyIndex)
{
    m_Loader = loader;
    m_Instance = instance;
    m_PhysicalDevice = physicalDevice;
    m_Device = device;
    m_GraphicsQueue = graphicsQueue;
    m_GraphicsQueueFamilyIndex = graphicsQueueFamilyIndex;

    if (!m_Loader || !m_Loader->IsAvailable())
    {
        std::cout << "[VulkanShadowRenderer] Vulkan loader not available\n";
        return false;
    }

    if (!CreateCommandBuffers())
    {
        std::cout << "[VulkanShadowRenderer] Failed to create command buffers\n";
        return false;
    }

    m_Initialized = true;
    std::cout << "[VulkanShadowRenderer] Initialized successfully\n";
    return true;
}

void VulkanShadowRenderer::Shutdown()
{
    if (!m_Initialized)
        return;

    CleanupShadowPipeline();
    CleanupShadowMapResources();
    CleanupCommandBuffers();

    m_Initialized = false;
}

bool VulkanShadowRenderer::CanRender(const VulkanShadowConfig& config, int objectCount) const
{
    if (!config.Enabled)
        return false;
    if (!m_Initialized)
        return false;
    if (objectCount < config.QualityThreshold)
        return false;

    return true;
}

bool VulkanShadowRenderer::RenderShadowMap(const glm::mat4& lightViewMatrix, 
                                            const glm::mat4& lightProjMatrix,
                                            const std::vector<glm::mat4>& objectTransforms,
                                            const VulkanShadowConfig& config)
{
    if (!m_Initialized)
        return false;

    // Recreate resources if config changed
    if (!m_ShadowMapImage || config.ShadowMapResolution != m_LastConfig.ShadowMapResolution)
    {
        CleanupShadowMapResources();
        if (!CreateShadowMapResources(config))
            {
                std::cout << "[VulkanShadowRenderer] Failed to create shadow map resources\n";
                return false;
            }
        m_LastConfig = config;
    }

    // TODO: Implement actual shadow rendering
    // This would involve:
    // 1. Begin command buffer
    // 2. Begin render pass
    // 3. Bind pipeline
    // 4. Set viewport and scissor
    // 5. Set push constants (light matrices)
    // 6. Draw all objects
    // 7. End render pass
    // 8. End command buffer
    // 9. Submit to queue

    return true;
}

bool VulkanShadowRenderer::CreateShadowMapResources(const VulkanShadowConfig& config)
{
    // Create shadow map image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = config.ShadowMapResolution;
    imageInfo.extent.height = config.ShadowMapResolution;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_Loader->vkCreateImage(m_Device, &imageInfo, nullptr, &m_ShadowMapImage) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to create shadow map image\n";
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    m_Loader->vkGetImageMemoryRequirements(m_Device, m_ShadowMapImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find appropriate memory type

    if (m_Loader->vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_ShadowMapMemory) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to allocate shadow map memory\n";
        m_Loader->vkDestroyImage(m_Device, m_ShadowMapImage, nullptr);
        return false;
    }

    m_Loader->vkBindImageMemory(m_Device, m_ShadowMapImage, m_ShadowMapMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ShadowMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (m_Loader->vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ShadowMapView) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to create shadow map image view\n";
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (m_Loader->vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_ShadowMapSampler) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to create shadow map sampler\n";
        return false;
    }

    std::cout << "[VulkanShadowRenderer] Shadow map resources created (" 
              << config.ShadowMapResolution << "x" << config.ShadowMapResolution << ")\n";
    return true;
}

bool VulkanShadowRenderer::CreateShadowPipeline()
{
    // TODO: Create render pass, pipeline layout, and graphics pipeline
    // This requires SPIR-V shaders for shadow pass
    return true;
}

bool VulkanShadowRenderer::CreateCommandBuffers()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsQueueFamilyIndex;

    if (m_Loader->vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to create command pool\n";
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (m_Loader->vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS)
    {
        std::cout << "[VulkanShadowRenderer] Failed to allocate command buffer\n";
        return false;
    }

    return true;
}

void VulkanShadowRenderer::CleanupShadowMapResources()
{
    if (m_ShadowMapSampler)
        m_Loader->vkDestroySampler(m_Device, m_ShadowMapSampler, nullptr);
    if (m_ShadowMapView)
        m_Loader->vkDestroyImageView(m_Device, m_ShadowMapView, nullptr);
    if (m_ShadowMapImage)
        m_Loader->vkDestroyImage(m_Device, m_ShadowMapImage, nullptr);
    if (m_ShadowMapMemory)
        m_Loader->vkFreeMemory(m_Device, m_ShadowMapMemory, nullptr);
    if (m_ShadowFramebuffer)
        m_Loader->vkDestroyFramebuffer(m_Device, m_ShadowFramebuffer, nullptr);

    m_ShadowMapSampler = VK_NULL_HANDLE;
    m_ShadowMapView = VK_NULL_HANDLE;
    m_ShadowMapImage = VK_NULL_HANDLE;
    m_ShadowMapMemory = VK_NULL_HANDLE;
    m_ShadowFramebuffer = VK_NULL_HANDLE;
}

void VulkanShadowRenderer::CleanupShadowPipeline()
{
    if (m_ShadowPipeline)
        m_Loader->vkDestroyPipeline(m_Device, m_ShadowPipeline, nullptr);
    if (m_ShadowPipelineLayout)
        m_Loader->vkDestroyPipelineLayout(m_Device, m_ShadowPipelineLayout, nullptr);
    if (m_ShadowRenderPass)
        m_Loader->vkDestroyRenderPass(m_Device, m_ShadowRenderPass, nullptr);

    m_ShadowPipeline = VK_NULL_HANDLE;
    m_ShadowPipelineLayout = VK_NULL_HANDLE;
    m_ShadowRenderPass = VK_NULL_HANDLE;
}

void VulkanShadowRenderer::CleanupCommandBuffers()
{
    if (m_CommandBuffer)
        m_Loader->vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &m_CommandBuffer);
    if (m_CommandPool)
        m_Loader->vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    m_CommandBuffer = VK_NULL_HANDLE;
    m_CommandPool = VK_NULL_HANDLE;
}
