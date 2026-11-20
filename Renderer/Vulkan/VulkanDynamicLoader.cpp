#include "VulkanDynamicLoader.h"
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

VulkanDynamicLoader::VulkanDynamicLoader()
{
}

VulkanDynamicLoader::~VulkanDynamicLoader()
{
    Shutdown();
}

bool VulkanDynamicLoader::Initialize()
{
    if (!LoadVulkanLoader())
    {
        std::cout << "[VulkanDynamicLoader] Failed to load Vulkan loader\n";
        return false;
    }

    if (!LoadInstanceFunctions())
    {
        std::cout << "[VulkanDynamicLoader] Failed to load instance functions\n";
        Shutdown();
        return false;
    }

    m_Available = true;
    std::cout << "[VulkanDynamicLoader] Initialized successfully\n";
    return true;
}

void VulkanDynamicLoader::Shutdown()
{
    if (m_VulkanDLL)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(m_VulkanDLL));
#else
        dlclose(m_VulkanDLL);
#endif
        m_VulkanDLL = nullptr;
    }

    m_GetInstanceProcAddr = nullptr;
    m_Available = false;
    std::cout << "[VulkanDynamicLoader] Shutdown complete\n";
}

bool VulkanDynamicLoader::LoadVulkanLoader()
{
#ifdef _WIN32
    wchar_t systemDirectory[MAX_PATH + 1]{};
    const UINT systemDirectoryLength = GetSystemDirectoryW(
        systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
    if (systemDirectoryLength == 0 || systemDirectoryLength >= std::size(systemDirectory))
    {
        std::cout << "[VulkanDynamicLoader] Could not resolve system directory\n";
        return false;
    }

    std::wstring loaderPath(systemDirectory, systemDirectoryLength);
    loaderPath += L"\\vulkan-1.dll";
    m_VulkanDLL = LoadLibraryW(loaderPath.c_str());
#else
    m_VulkanDLL = dlopen("libvulkan.so.1", RTLD_LAZY);
#endif

    if (!m_VulkanDLL)
    {
        std::cout << "[VulkanDynamicLoader] Vulkan loader not found\n";
        return false;
    }

#ifdef _WIN32
    m_GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(static_cast<HMODULE>(m_VulkanDLL), "vkGetInstanceProcAddr"));
#else
    m_GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(m_VulkanDLL, "vkGetInstanceProcAddr"));
#endif

    if (!m_GetInstanceProcAddr)
    {
        std::cout << "[VulkanDynamicLoader] vkGetInstanceProcAddr not found\n";
        Shutdown();
        return false;
    }

    std::cout << "[VulkanDynamicLoader] Vulkan loader loaded successfully\n";
    return true;
}

bool VulkanDynamicLoader::LoadInstanceFunctions()
{
    std::cout << "[VulkanDynamicLoader] Starting to load instance functions...\n";

    // Check if GetInstanceProcAddr is valid
    if (!m_GetInstanceProcAddr)
    {
        std::cout << "[VulkanDynamicLoader] ERROR: m_GetInstanceProcAddr is null!\n";
        return false;
    }

    // Load instance-level functions (can be loaded with vkGetInstanceProcAddr)
    auto loadFunc = [this](const char* name, auto& funcPtr) -> bool {
        std::cout << "[VulkanDynamicLoader] Loading: " << name << "\n";
        funcPtr = reinterpret_cast<decltype(funcPtr)>(m_GetInstanceProcAddr(VK_NULL_HANDLE, name));
        if (!funcPtr)
        {
            std::cout << "[VulkanDynamicLoader] Failed to load " << name << "\n";
            return false;
        }
        return true;
    };

    std::cout << "[VulkanDynamicLoader] Loading core instance functions...\n";
    if (!loadFunc("vkCreateInstance", vkCreateInstance)) return false;
    if (!loadFunc("vkDestroyInstance", vkDestroyInstance)) return false;
    if (!loadFunc("vkEnumerateInstanceVersion", vkEnumerateInstanceVersion)) return false;
    if (!loadFunc("vkEnumerateInstanceLayerProperties", vkEnumerateInstanceLayerProperties)) return false;

    std::cout << "[VulkanDynamicLoader] Loading physical device functions...\n";
    if (!loadFunc("vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices)) return false;
    if (!loadFunc("vkGetPhysicalDeviceProperties", vkGetPhysicalDeviceProperties)) return false;
    if (!loadFunc("vkGetPhysicalDeviceMemoryProperties", vkGetPhysicalDeviceMemoryProperties)) return false;
    if (!loadFunc("vkGetPhysicalDeviceQueueFamilyProperties", vkGetPhysicalDeviceQueueFamilyProperties)) return false;

    std::cout << "[VulkanDynamicLoader] Loading device functions...\n";
    if (!loadFunc("vkCreateDevice", vkCreateDevice)) return false;
    if (!loadFunc("vkDestroyDevice", vkDestroyDevice)) return false;
    if (!loadFunc("vkGetDeviceProcAddr", vkGetDeviceProcAddr)) return false;

    std::cout << "[VulkanDynamicLoader] Instance functions loaded successfully\n";
    return true;
}

bool VulkanDynamicLoader::LoadDeviceFunctions(VkDevice device)
{
    if (!device)
    {
        std::cout << "[VulkanDynamicLoader] Invalid device\n";
        return false;
    }
    
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = vkGetDeviceProcAddr;
    if (!getDeviceProcAddr)
    {
        std::cout << "[VulkanDynamicLoader] getDeviceProcAddr not available\n";
        return false;
    }

    auto loadFunc = [getDeviceProcAddr, device](const char* name, auto& funcPtr) -> bool {
        funcPtr = reinterpret_cast<decltype(funcPtr)>(getDeviceProcAddr(device, name));
        if (!funcPtr)
        {
            std::cout << "[VulkanDynamicLoader] Failed to load " << name << "\n";
            return false;
        }
        return true;
    };

    // Queue functions
    if (!loadFunc("vkGetDeviceQueue", vkGetDeviceQueue)) return false;
    if (!loadFunc("vkQueueSubmit", vkQueueSubmit)) return false;
    if (!loadFunc("vkQueueWaitIdle", vkQueueWaitIdle)) return false;
    if (!loadFunc("vkDeviceWaitIdle", vkDeviceWaitIdle)) return false;

    // Command buffer functions
    if (!loadFunc("vkAllocateCommandBuffers", vkAllocateCommandBuffers)) return false;
    if (!loadFunc("vkFreeCommandBuffers", vkFreeCommandBuffers)) return false;
    if (!loadFunc("vkBeginCommandBuffer", vkBeginCommandBuffer)) return false;
    if (!loadFunc("vkEndCommandBuffer", vkEndCommandBuffer)) return false;
    if (!loadFunc("vkResetCommandBuffer", vkResetCommandBuffer)) return false;
    if (!loadFunc("vkCreateCommandPool", vkCreateCommandPool)) return false;
    if (!loadFunc("vkDestroyCommandPool", vkDestroyCommandPool)) return false;

    // Memory functions
    if (!loadFunc("vkAllocateMemory", vkAllocateMemory)) return false;
    if (!loadFunc("vkFreeMemory", vkFreeMemory)) return false;
    if (!loadFunc("vkMapMemory", vkMapMemory)) return false;
    if (!loadFunc("vkUnmapMemory", vkUnmapMemory)) return false;
    if (!loadFunc("vkFlushMappedMemoryRanges", vkFlushMappedMemoryRanges)) return false;
    if (!loadFunc("vkInvalidateMappedMemoryRanges", vkInvalidateMappedMemoryRanges)) return false;
    if (!loadFunc("vkBindBufferMemory", vkBindBufferMemory)) return false;
    if (!loadFunc("vkBindImageMemory", vkBindImageMemory)) return false;
    if (!loadFunc("vkGetBufferMemoryRequirements", vkGetBufferMemoryRequirements)) return false;
    if (!loadFunc("vkGetImageMemoryRequirements", vkGetImageMemoryRequirements)) return false;

    // Buffer functions
    if (!loadFunc("vkCreateBuffer", vkCreateBuffer)) return false;
    if (!loadFunc("vkDestroyBuffer", vkDestroyBuffer)) return false;

    // Image functions
    if (!loadFunc("vkCreateImage", vkCreateImage)) return false;
    if (!loadFunc("vkDestroyImage", vkDestroyImage)) return false;
    if (!loadFunc("vkCreateImageView", vkCreateImageView)) return false;
    if (!loadFunc("vkDestroyImageView", vkDestroyImageView)) return false;
    if (!loadFunc("vkCreateSampler", vkCreateSampler)) return false;
    if (!loadFunc("vkDestroySampler", vkDestroySampler)) return false;

    // Render pass and framebuffer functions
    if (!loadFunc("vkCreateRenderPass", vkCreateRenderPass)) return false;
    if (!loadFunc("vkDestroyRenderPass", vkDestroyRenderPass)) return false;
    if (!loadFunc("vkCreateFramebuffer", vkCreateFramebuffer)) return false;
    if (!loadFunc("vkDestroyFramebuffer", vkDestroyFramebuffer)) return false;

    // Pipeline functions
    if (!loadFunc("vkCreatePipelineLayout", vkCreatePipelineLayout)) return false;
    if (!loadFunc("vkDestroyPipelineLayout", vkDestroyPipelineLayout)) return false;
    if (!loadFunc("vkCreateComputePipelines", vkCreateComputePipelines)) return false;
    if (!loadFunc("vkCreateGraphicsPipelines", vkCreateGraphicsPipelines)) return false;
    if (!loadFunc("vkDestroyPipeline", vkDestroyPipeline)) return false;

    // Descriptor set functions
    if (!loadFunc("vkCreateDescriptorSetLayout", vkCreateDescriptorSetLayout)) return false;
    if (!loadFunc("vkDestroyDescriptorSetLayout", vkDestroyDescriptorSetLayout)) return false;
    if (!loadFunc("vkCreateDescriptorPool", vkCreateDescriptorPool)) return false;
    if (!loadFunc("vkDestroyDescriptorPool", vkDestroyDescriptorPool)) return false;
    if (!loadFunc("vkAllocateDescriptorSets", vkAllocateDescriptorSets)) return false;
    if (!loadFunc("vkFreeDescriptorSets", vkFreeDescriptorSets)) return false;
    if (!loadFunc("vkUpdateDescriptorSets", vkUpdateDescriptorSets)) return false;

    // Synchronization functions
    if (!loadFunc("vkCreateFence", vkCreateFence)) return false;
    if (!loadFunc("vkDestroyFence", vkDestroyFence)) return false;
    if (!loadFunc("vkWaitForFences", vkWaitForFences)) return false;
    if (!loadFunc("vkResetFences", vkResetFences)) return false;
    if (!loadFunc("vkCreateSemaphore", vkCreateSemaphore)) return false;
    if (!loadFunc("vkDestroySemaphore", vkDestroySemaphore)) return false;

    std::cout << "[VulkanDynamicLoader] Device functions loaded\n";
    return true;
}
