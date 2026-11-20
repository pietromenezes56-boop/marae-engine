#include "VulkanMemoryManager.h"
#include <iostream>

VulkanMemoryManager::VulkanMemoryManager()
{
}

VulkanMemoryManager::~VulkanMemoryManager()
{
    Shutdown();
}

bool VulkanMemoryManager::Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
{
    m_Loader = loader;
    m_Instance = instance;
    m_PhysicalDevice = physicalDevice;
    m_Device = device;

    if (!m_Loader || !m_Loader->IsAvailable())
    {
        std::cout << "[VulkanMemoryManager] Vulkan loader not available\n";
        return false;
    }

    m_Loader->vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_MemoryProperties);

    m_Initialized = true;
    std::cout << "[VulkanMemoryManager] Initialized successfully\n";
    std::cout << "[VulkanMemoryManager] Found " << m_MemoryProperties.memoryHeapCount << " memory heaps\n";
    std::cout << "[VulkanMemoryManager] Found " << m_MemoryProperties.memoryTypeCount << " memory types\n";
    
    return true;
}

void VulkanMemoryManager::Shutdown()
{
    if (!m_Initialized)
        return;

    CleanupMemoryPools();
    
    m_Initialized = false;
}

bool VulkanMemoryManager::ShouldActivate(const VulkanMemoryConfig& config, int objectCount, 
                                         size_t currentMemoryUsage) const
{
    if (!config.Enabled)
        return false;
    if (!m_Initialized)
        return false;
    if (objectCount < config.ObjectCountThreshold)
        return false;
    if (currentMemoryUsage < config.MaxMemoryBudget * 0.7)  // Ativa em 70% do budget
        return false;

    return true;
}

void* VulkanMemoryManager::AllocateMemory(VkDeviceSize size, VkDeviceSize alignment, 
                                          VkMemoryPropertyFlags properties)
{
    // TODO: Implement allocation from memory pools
    // For now, return nullptr to indicate not implemented
    return nullptr;
}

void VulkanMemoryManager::FreeMemory(void* pointer)
{
    // TODO: Implement free from memory pools
}

bool VulkanMemoryManager::StreamTexture(const std::string& texturePath, int priority)
{
    if (!m_Initialized)
        return false;

    StreamingRequest request;
    request.path = texturePath;
    request.Priority = priority;
    request.Loaded = false;
    
    m_StreamingQueue.push_back(request);
    
    std::cout << "[VulkanMemoryManager] Queued texture for streaming: " << texturePath 
              << " (priority " << priority << ")\n";
    return true;
}

void VulkanMemoryManager::UpdateStreaming()
{
    if (!m_Initialized)
        return;

    // TODO: Process streaming queue
    // Load textures with highest priority first
    // Upload to GPU using Vulkan
    // Update streaming status
}

VkBuffer VulkanMemoryManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                                           VkMemoryPropertyFlags properties)
{
    VkBuffer buffer = VK_NULL_HANDLE;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_Loader->vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        std::cout << "[VulkanMemoryManager] Failed to create buffer\n";
        return VK_NULL_HANDLE;
    }

    VkMemoryRequirements memRequirements;
    m_Loader->vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    uint32_t memoryTypeIndex = 0;
    if (!FindMemoryType(memRequirements.memoryTypeBits, properties, memoryTypeIndex))
    {
        std::cout << "[VulkanMemoryManager] Failed to find suitable memory type\n";
        m_Loader->vkDestroyBuffer(m_Device, buffer, nullptr);
        return VK_NULL_HANDLE;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (m_Loader->vkAllocateMemory(m_Device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
        std::cout << "[VulkanMemoryManager] Failed to allocate buffer memory\n";
        m_Loader->vkDestroyBuffer(m_Device, buffer, nullptr);
        return VK_NULL_HANDLE;
    }

    m_Loader->vkBindBufferMemory(m_Device, buffer, memory, 0);

    m_TotalAllocated += memRequirements.size;
    
    std::cout << "[VulkanMemoryManager] Created buffer (" << size << " bytes)\n";
    return buffer;
}

void VulkanMemoryManager::DestroyBuffer(VkBuffer buffer)
{
    if (buffer != VK_NULL_HANDLE)
    {
        // TODO: Track and free associated memory
        m_Loader->vkDestroyBuffer(m_Device, buffer, nullptr);
    }
}

bool VulkanMemoryManager::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, 
                                           uint32_t& typeIndex)
{
    for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (m_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            typeIndex = i;
            return true;
        }
    }
    return false;
}

bool VulkanMemoryManager::CreateMemoryPools(const VulkanMemoryConfig& config)
{
    // TODO: Create memory pools for different memory types
    // This would pre-allocate large chunks of memory for better allocation performance
    return true;
}

void VulkanMemoryManager::CleanupMemoryPools()
{
    // TODO: Free all memory pools
    for (auto& pool : m_MemoryPools)
    {
        if (pool.Memory != VK_NULL_HANDLE)
        {
            m_Loader->vkFreeMemory(m_Device, pool.Memory, nullptr);
        }
    }
    m_MemoryPools.clear();
}
