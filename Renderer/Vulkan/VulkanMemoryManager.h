#pragma once

// ---------------------------------------------------------------------------
// VulkanMemoryManager
//
// Gerenciador de memória Vulkan para otimizar uso de RAM.
// Ativado automaticamente quando a cena tem muitos objetos.
// Gerencia alocação de memória, streaming de texturas e buffers.
// ---------------------------------------------------------------------------

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include "VulkanDynamicLoader.h"

struct VulkanMemoryConfig
{
    bool Enabled = false;
    int ObjectCountThreshold = 50;                // Ativa quando tem >50 objetos (reduzido de 100)
    size_t MaxMemoryBudget = 512ULL * 1024 * 1024;  // 512MB (reduzido de 2GB)
    bool UseMemoryPools = true;
    bool EnableStreaming = true;
    size_t StreamingChunkSize = 16 * 1024 * 1024;  // 16MB chunks (reduzido de 64MB)
};

struct MemoryPool
{
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    VkDeviceSize Used = 0;
    VkDeviceSize Offset = 0;
};

class VulkanMemoryManager
{
public:
    VulkanMemoryManager();
    ~VulkanMemoryManager();

    bool Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    void Shutdown();

    bool ShouldActivate(const VulkanMemoryConfig& config, int objectCount, size_t currentMemoryUsage) const;
    
    // Memory allocation
    void* AllocateMemory(VkDeviceSize size, VkDeviceSize alignment, VkMemoryPropertyFlags properties);
    void FreeMemory(void* pointer);
    
    // Memory statistics
    size_t GetTotalAllocated() const { return m_TotalAllocated; }
    size_t GetTotalFreed() const { return m_TotalFreed; }
    size_t GetCurrentUsage() const { return m_TotalAllocated - m_TotalFreed; }
    
    // Texture streaming
    bool StreamTexture(const std::string& texturePath, int priority = 0);
    void UpdateStreaming();
    
    // Buffer management
    VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void DestroyBuffer(VkBuffer buffer);

private:
    bool FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t& typeIndex);
    bool CreateMemoryPools(const VulkanMemoryConfig& config);
    void CleanupMemoryPools();

    VulkanDynamicLoader* m_Loader = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    VkPhysicalDeviceMemoryProperties m_MemoryProperties;
    
    std::vector<MemoryPool> m_MemoryPools;
    std::unordered_map<void*, VkDeviceMemory> m_AllocationMap;
    
    size_t m_TotalAllocated = 0;
    size_t m_TotalFreed = 0;
    
    struct StreamingRequest
    {
        std::string path;
        int Priority;
        bool Loaded;
    };
    std::vector<StreamingRequest> m_StreamingQueue;
    
    bool m_Initialized = false;
};
