#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "VulkanRuntimeProbe.h"

#include <iterator>
#include <ostream>
#include <string>
#include <vector>
#include <cstring>

namespace
{
    std::string ApiVersionString(uint32_t version)
    {
        return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
            std::to_string(VK_API_VERSION_MINOR(version)) + "." +
            std::to_string(VK_API_VERSION_PATCH(version));
    }

    const char* DeviceTypeName(VkPhysicalDeviceType type)
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU Vulkan device";
        default:                                     return "other Vulkan device";
        }
    }

    template<typename Function>
    Function LoadInstanceFunction(PFN_vkGetInstanceProcAddr getProcAddr,
        VkInstance instance, const char* name)
    {
        return reinterpret_cast<Function>(getProcAddr(instance, name));
    }
}

bool VulkanRuntimeProbe::RunDiagnostics(std::ostream& output)
{
    wchar_t systemDirectory[MAX_PATH + 1]{};
    const UINT systemDirectoryLength = GetSystemDirectoryW(
        systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
    if (systemDirectoryLength == 0 || systemDirectoryLength >= std::size(systemDirectory))
    {
        output << "[Vulkan] Could not resolve the Windows system directory.\n";
        return false;
    }

    std::wstring loaderPath(systemDirectory, systemDirectoryLength);
    loaderPath += L"\\vulkan-1.dll";
    HMODULE loader = LoadLibraryW(loaderPath.c_str());
    if (!loader)
    {
        output << "[Vulkan] System Vulkan loader vulkan-1.dll was not found.\n";
        return false;
    }

    auto getProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(loader, "vkGetInstanceProcAddr"));
    if (!getProcAddr)
    {
        output << "[Vulkan] Loader is present but vkGetInstanceProcAddr is missing.\n";
        FreeLibrary(loader);
        return false;
    }

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    const auto enumerateInstanceVersion = LoadInstanceFunction<PFN_vkEnumerateInstanceVersion>(
        getProcAddr, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateInstanceVersion)
    {
        const VkResult versionResult = enumerateInstanceVersion(&loaderVersion);
        if (versionResult != VK_SUCCESS)
        {
            output << "[Vulkan] Could not query loader API version (VkResult "
                << static_cast<int>(versionResult) << ").\n";
            FreeLibrary(loader);
            return false;
        }
    }

    const auto createInstance = LoadInstanceFunction<PFN_vkCreateInstance>(
        getProcAddr, VK_NULL_HANDLE, "vkCreateInstance");
    if (!createInstance)
    {
        output << "[Vulkan] Loader does not expose vkCreateInstance.\n";
        FreeLibrary(loader);
        return false;
    }

    const auto enumerateInstanceLayerProperties = LoadInstanceFunction<PFN_vkEnumerateInstanceLayerProperties>(
        getProcAddr, VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");

    // Check for validation layers (may help with Intel UHD driver bug)
    std::vector<const char*> validationLayers;
    bool hasValidationLayers = false;
    if (enumerateInstanceLayerProperties)
    {
        uint32_t layerCount = 0;
        enumerateInstanceLayerProperties(&layerCount, nullptr);
        if (layerCount > 0)
        {
            std::vector<VkLayerProperties> availableLayers(layerCount);
            enumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            output << "[Vulkan] Available validation layers:\n";
            for (const auto& layer : availableLayers)
            {
                output << "  - " << layer.layerName << "\n";
            }

            // Try to enable standard validation layers
            const char* standardLayers[] = {
                "VK_LAYER_KHRONOS_validation",
                "VK_LAYER_LUNARG_standard_validation"
            };

            for (const char* layerName : standardLayers)
            {
                bool found = false;
                for (const auto& layer : availableLayers)
                {
                    if (strcmp(layer.layerName, layerName) == 0)
                    {
                        validationLayers.push_back(layerName);
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    hasValidationLayers = true;
                    output << "[Vulkan] Enabling validation layer: " << layerName << "\n";
                }
            }
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Marae Engine Vulkan Probe";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.pEngineName = "Marae Engine";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    // Request the baseline API so the probe can run on older Vulkan drivers too.
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instanceInfo.ppEnabledLayerNames = validationLayers.data();

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult createResult = createInstance(&instanceInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS || instance == VK_NULL_HANDLE)
    {
        output << "[Vulkan] Instance creation failed (VkResult "
            << static_cast<int>(createResult) << ").\n";
        FreeLibrary(loader);
        return false;
    }

    const auto destroyInstance = LoadInstanceFunction<PFN_vkDestroyInstance>(
        getProcAddr, instance, "vkDestroyInstance");
    const auto enumeratePhysicalDevices = LoadInstanceFunction<PFN_vkEnumeratePhysicalDevices>(
        getProcAddr, instance, "vkEnumeratePhysicalDevices");
    const auto getPhysicalDeviceProperties = LoadInstanceFunction<PFN_vkGetPhysicalDeviceProperties>(
        getProcAddr, instance, "vkGetPhysicalDeviceProperties");

    bool succeeded = false;
    bool hasIntelUHD = false;
    if (!destroyInstance || !enumeratePhysicalDevices || !getPhysicalDeviceProperties)
    {
        output << "[Vulkan] Required device-enumeration functions are unavailable.\n";
    }
    else
    {
        uint32_t deviceCount = 0;
        VkResult result = enumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (result == VK_SUCCESS && deviceCount > 0)
        {
            std::vector<VkPhysicalDevice> devices(deviceCount, VK_NULL_HANDLE);
            result = enumeratePhysicalDevices(instance, &deviceCount, devices.data());
            if (result == VK_SUCCESS)
            {
                output << "[Vulkan] Loader API " << ApiVersionString(loaderVersion)
                    << "; found " << deviceCount << " device(s):\n";
                for (uint32_t i = 0; i < deviceCount; ++i)
                {
                    VkPhysicalDeviceProperties properties{};
                    getPhysicalDeviceProperties(devices[i], &properties);
                    std::string deviceName(properties.deviceName);

                    // Detect Intel UHD Graphics (pre-Arc)
                    bool isIntelUHD = (deviceName.find("Intel(R) UHD") != std::string::npos) ||
                                     (deviceName.find("Intel HD Graphics") != std::string::npos);
                    if (isIntelUHD)
                    {
                        hasIntelUHD = true;
                        output << "  - " << deviceName << " ("
                            << DeviceTypeName(properties.deviceType) << ", API "
                            << ApiVersionString(properties.apiVersion) << ") [INCOMPATIBLE - driver bug]\n";
                    }
                    else
                    {
                        output << "  - " << deviceName << " ("
                            << DeviceTypeName(properties.deviceType) << ", API "
                            << ApiVersionString(properties.apiVersion) << ") [COMPATIBLE]\n";
                    }
                }

                if (hasIntelUHD)
                {
                    output << "[Vulkan] WARNING: Intel UHD Graphics detected - driver has critical bug\n";
                    output << "[Vulkan] Vulkan acceleration is NOT recommended on this GPU\n";
                    output << "[Vulkan] Consider using a dedicated GPU (NVIDIA/AMD) for Vulkan acceleration\n";
                }

                succeeded = true;
            }
            else
            {
                output << "[Vulkan] Device enumeration failed (VkResult "
                    << static_cast<int>(result) << ").\n";
            }
        }
        else if (result == VK_SUCCESS)
        {
            output << "[Vulkan] Loader works, but no Vulkan physical device was found.\n";
        }
        else
        {
            output << "[Vulkan] Could not query physical devices (VkResult "
                << static_cast<int>(result) << ").\n";
        }
    }

    if (destroyInstance)
        destroyInstance(instance, nullptr);
    FreeLibrary(loader);
    return succeeded;
}
