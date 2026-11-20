#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "VulkanSmokeWindow.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ostream>
#include <thread>
#include <vector>

namespace
{
    template<typename Function>
    Function LoadInstanceFunction(VkInstance instance, const char* name)
    {
        return reinterpret_cast<Function>(glfwGetInstanceProcAddress(instance, name));
    }

    template<typename Function>
    Function LoadDeviceFunction(VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr,
        const char* name)
    {
        return reinterpret_cast<Function>(getDeviceProcAddr(device, name));
    }

    struct VulkanSmokeContext
    {
        GLFWwindow* window = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        std::vector<VkFramebuffer> framebuffers;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence frameFence = VK_NULL_HANDLE;

        PFN_vkDestroyInstance destroyInstance = nullptr;
        PFN_vkDestroySurfaceKHR destroySurface = nullptr;
        PFN_vkDestroyDevice destroyDevice = nullptr;
        PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
        PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
        PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
        PFN_vkDestroyImageView destroyImageView = nullptr;
        PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
        PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
        PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
        PFN_vkDestroySemaphore destroySemaphore = nullptr;
        PFN_vkDestroyFence destroyFence = nullptr;

        ~VulkanSmokeContext()
        {
            if (device && deviceWaitIdle)
                deviceWaitIdle(device);

            if (device)
            {
                if (frameFence && destroyFence) destroyFence(device, frameFence, nullptr);
                if (renderFinished && destroySemaphore) destroySemaphore(device, renderFinished, nullptr);
                if (imageAvailable && destroySemaphore) destroySemaphore(device, imageAvailable, nullptr);
                if (commandPool && destroyCommandPool) destroyCommandPool(device, commandPool, nullptr);
                for (VkFramebuffer framebuffer : framebuffers)
                    if (framebuffer && destroyFramebuffer) destroyFramebuffer(device, framebuffer, nullptr);
                if (renderPass && destroyRenderPass) destroyRenderPass(device, renderPass, nullptr);
                for (VkImageView imageView : imageViews)
                    if (imageView && destroyImageView) destroyImageView(device, imageView, nullptr);
                if (swapchain && destroySwapchain) destroySwapchain(device, swapchain, nullptr);
                if (destroyDevice) destroyDevice(device, nullptr);
            }
            if (surface && instance && destroySurface)
                destroySurface(instance, surface, nullptr);
            if (instance && destroyInstance)
                destroyInstance(instance, nullptr);
            if (window)
                glfwDestroyWindow(window);
        }
    };

    bool LoadInstanceFunctions(VulkanSmokeContext& context, std::ostream& output)
    {
        context.destroyInstance = LoadInstanceFunction<PFN_vkDestroyInstance>(context.instance, "vkDestroyInstance");
        const auto enumeratePhysicalDevices = LoadInstanceFunction<PFN_vkEnumeratePhysicalDevices>(context.instance, "vkEnumeratePhysicalDevices");
        const auto getQueueFamilyProperties = LoadInstanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(context.instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        const auto getSurfaceSupport = LoadInstanceFunction<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(context.instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        const auto enumerateDeviceExtensions = LoadInstanceFunction<PFN_vkEnumerateDeviceExtensionProperties>(context.instance, "vkEnumerateDeviceExtensionProperties");
        const auto createDevice = LoadInstanceFunction<PFN_vkCreateDevice>(context.instance, "vkCreateDevice");
        const auto getDeviceProcAddr = LoadInstanceFunction<PFN_vkGetDeviceProcAddr>(context.instance, "vkGetDeviceProcAddr");
        context.destroySurface = LoadInstanceFunction<PFN_vkDestroySurfaceKHR>(context.instance, "vkDestroySurfaceKHR");

        if (!context.destroyInstance || !enumeratePhysicalDevices || !getQueueFamilyProperties ||
            !getSurfaceSupport || !enumerateDeviceExtensions || !createDevice || !getDeviceProcAddr ||
            !context.destroySurface)
        {
            output << "[Vulkan] The loader is missing an instance/surface function required by the smoke test.\n";
            return false;
        }

        uint32_t deviceCount = 0;
        VkResult result = enumeratePhysicalDevices(context.instance, &deviceCount, nullptr);
        if (result != VK_SUCCESS || deviceCount == 0)
        {
            output << "[Vulkan] No physical device is available for presentation (VkResult "
                << static_cast<int>(result) << ").\n";
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount, VK_NULL_HANDLE);
        result = enumeratePhysicalDevices(context.instance, &deviceCount, devices.data());
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Physical-device enumeration failed (VkResult "
                << static_cast<int>(result) << ").\n";
            return false;
        }

        for (VkPhysicalDevice candidate : devices)
        {
            uint32_t familyCount = 0;
            getQueueFamilyProperties(candidate, &familyCount, nullptr);
            if (familyCount == 0)
                continue;

            std::vector<VkQueueFamilyProperties> families(familyCount);
            getQueueFamilyProperties(candidate, &familyCount, families.data());
            uint32_t graphics = UINT32_MAX;
            uint32_t present = UINT32_MAX;
            for (uint32_t i = 0; i < familyCount; ++i)
            {
                if (families[i].queueCount == 0)
                    continue;
                if (graphics == UINT32_MAX && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                    graphics = i;
                VkBool32 supportsPresent = VK_FALSE;
                if (getSurfaceSupport(candidate, i, context.surface, &supportsPresent) == VK_SUCCESS && supportsPresent)
                    present = i;
                if (graphics == i && present == i)
                    break;
            }
            if (graphics == UINT32_MAX || present == UINT32_MAX)
                continue;

            uint32_t extensionCount = 0;
            result = enumerateDeviceExtensions(candidate, nullptr, &extensionCount, nullptr);
            if (result != VK_SUCCESS)
                continue;
            std::vector<VkExtensionProperties> extensions(extensionCount);
            result = enumerateDeviceExtensions(candidate, nullptr, &extensionCount, extensions.data());
            if (result != VK_SUCCESS)
                continue;
            const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(), [](const auto& extension)
            {
                return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
            if (!hasSwapchain)
                continue;

            context.physicalDevice = candidate;
            context.graphicsFamily = graphics;
            context.presentFamily = present;
            break;
        }

        if (!context.physicalDevice)
        {
            output << "[Vulkan] No GPU has both graphics/presentation queues and VK_KHR_swapchain.\n";
            return false;
        }

        std::array<uint32_t, 2> queueFamilies{ context.graphicsFamily, context.presentFamily };
        const uint32_t queueCount = queueFamilies[0] == queueFamilies[1] ? 1u : 2u;
        const float queuePriority = 1.0f;
        std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};
        for (uint32_t i = 0; i < queueCount; ++i)
        {
            queueInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfos[i].queueFamilyIndex = queueFamilies[i];
            queueInfos[i].queueCount = 1;
            queueInfos[i].pQueuePriorities = &queuePriority;
        }

        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = queueCount;
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;

        result = createDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Logical-device creation failed (VkResult "
                << static_cast<int>(result) << ").\n";
            return false;
        }

        context.destroyDevice = LoadDeviceFunction<PFN_vkDestroyDevice>(context.device, getDeviceProcAddr, "vkDestroyDevice");
        context.deviceWaitIdle = LoadDeviceFunction<PFN_vkDeviceWaitIdle>(context.device, getDeviceProcAddr, "vkDeviceWaitIdle");
        context.queueWaitIdle = LoadDeviceFunction<PFN_vkQueueWaitIdle>(context.device, getDeviceProcAddr, "vkQueueWaitIdle");
        const auto getDeviceQueue = LoadDeviceFunction<PFN_vkGetDeviceQueue>(context.device, getDeviceProcAddr, "vkGetDeviceQueue");
        if (!context.destroyDevice || !context.deviceWaitIdle || !context.queueWaitIdle || !getDeviceQueue)
        {
            output << "[Vulkan] The loader is missing a device-management function.\n";
            return false;
        }

        getDeviceQueue(context.device, context.graphicsFamily, 0, &context.graphicsQueue);
        getDeviceQueue(context.device, context.presentFamily, 0, &context.presentQueue);
        return context.graphicsQueue && context.presentQueue;
    }

    bool CreateSwapchainAndClearResources(VulkanSmokeContext& context, std::ostream& output)
    {
        const auto getSurfaceCapabilities = LoadInstanceFunction<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(context.instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        const auto getSurfaceFormats = LoadInstanceFunction<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(context.instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        const auto getPresentModes = LoadInstanceFunction<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(context.instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
        const auto getDeviceProcAddr = LoadInstanceFunction<PFN_vkGetDeviceProcAddr>(context.instance, "vkGetDeviceProcAddr");
        if (!getSurfaceCapabilities || !getSurfaceFormats || !getPresentModes || !getDeviceProcAddr)
        {
            output << "[Vulkan] Surface capability functions are unavailable.\n";
            return false;
        }

        const auto createSwapchain = LoadDeviceFunction<PFN_vkCreateSwapchainKHR>(context.device, getDeviceProcAddr, "vkCreateSwapchainKHR");
        context.destroySwapchain = LoadDeviceFunction<PFN_vkDestroySwapchainKHR>(context.device, getDeviceProcAddr, "vkDestroySwapchainKHR");
        const auto getSwapchainImages = LoadDeviceFunction<PFN_vkGetSwapchainImagesKHR>(context.device, getDeviceProcAddr, "vkGetSwapchainImagesKHR");
        const auto createImageView = LoadDeviceFunction<PFN_vkCreateImageView>(context.device, getDeviceProcAddr, "vkCreateImageView");
        context.destroyImageView = LoadDeviceFunction<PFN_vkDestroyImageView>(context.device, getDeviceProcAddr, "vkDestroyImageView");
        const auto createRenderPass = LoadDeviceFunction<PFN_vkCreateRenderPass>(context.device, getDeviceProcAddr, "vkCreateRenderPass");
        context.destroyRenderPass = LoadDeviceFunction<PFN_vkDestroyRenderPass>(context.device, getDeviceProcAddr, "vkDestroyRenderPass");
        const auto createFramebuffer = LoadDeviceFunction<PFN_vkCreateFramebuffer>(context.device, getDeviceProcAddr, "vkCreateFramebuffer");
        context.destroyFramebuffer = LoadDeviceFunction<PFN_vkDestroyFramebuffer>(context.device, getDeviceProcAddr, "vkDestroyFramebuffer");
        const auto createCommandPool = LoadDeviceFunction<PFN_vkCreateCommandPool>(context.device, getDeviceProcAddr, "vkCreateCommandPool");
        context.destroyCommandPool = LoadDeviceFunction<PFN_vkDestroyCommandPool>(context.device, getDeviceProcAddr, "vkDestroyCommandPool");
        const auto allocateCommandBuffers = LoadDeviceFunction<PFN_vkAllocateCommandBuffers>(context.device, getDeviceProcAddr, "vkAllocateCommandBuffers");
        const auto beginCommandBuffer = LoadDeviceFunction<PFN_vkBeginCommandBuffer>(context.device, getDeviceProcAddr, "vkBeginCommandBuffer");
        const auto cmdBeginRenderPass = LoadDeviceFunction<PFN_vkCmdBeginRenderPass>(context.device, getDeviceProcAddr, "vkCmdBeginRenderPass");
        const auto cmdEndRenderPass = LoadDeviceFunction<PFN_vkCmdEndRenderPass>(context.device, getDeviceProcAddr, "vkCmdEndRenderPass");
        const auto endCommandBuffer = LoadDeviceFunction<PFN_vkEndCommandBuffer>(context.device, getDeviceProcAddr, "vkEndCommandBuffer");
        const auto createSemaphore = LoadDeviceFunction<PFN_vkCreateSemaphore>(context.device, getDeviceProcAddr, "vkCreateSemaphore");
        context.destroySemaphore = LoadDeviceFunction<PFN_vkDestroySemaphore>(context.device, getDeviceProcAddr, "vkDestroySemaphore");
        const auto createFence = LoadDeviceFunction<PFN_vkCreateFence>(context.device, getDeviceProcAddr, "vkCreateFence");
        context.destroyFence = LoadDeviceFunction<PFN_vkDestroyFence>(context.device, getDeviceProcAddr, "vkDestroyFence");
        const auto acquireNextImage = LoadDeviceFunction<PFN_vkAcquireNextImageKHR>(context.device, getDeviceProcAddr, "vkAcquireNextImageKHR");
        const auto queueSubmit = LoadDeviceFunction<PFN_vkQueueSubmit>(context.device, getDeviceProcAddr, "vkQueueSubmit");
        const auto queuePresent = LoadDeviceFunction<PFN_vkQueuePresentKHR>(context.device, getDeviceProcAddr, "vkQueuePresentKHR");
        const auto waitForFences = LoadDeviceFunction<PFN_vkWaitForFences>(context.device, getDeviceProcAddr, "vkWaitForFences");

        if (!createSwapchain || !context.destroySwapchain || !getSwapchainImages || !createImageView ||
            !context.destroyImageView || !createRenderPass || !context.destroyRenderPass || !createFramebuffer ||
            !context.destroyFramebuffer || !createCommandPool || !context.destroyCommandPool ||
            !allocateCommandBuffers || !beginCommandBuffer || !cmdBeginRenderPass || !cmdEndRenderPass ||
            !endCommandBuffer || !createSemaphore || !context.destroySemaphore || !createFence ||
            !context.destroyFence || !acquireNextImage || !queueSubmit || !queuePresent || !waitForFences)
        {
            output << "[Vulkan] The loader is missing a swapchain/render function required by the smoke test.\n";
            return false;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = getSurfaceCapabilities(context.physicalDevice, context.surface, &capabilities);
        if (result != VK_SUCCESS || !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {
            output << "[Vulkan] The window surface cannot be used as a color attachment (VkResult "
                << static_cast<int>(result) << ").\n";
            return false;
        }

        uint32_t formatCount = 0;
        result = getSurfaceFormats(context.physicalDevice, context.surface, &formatCount, nullptr);
        if (result != VK_SUCCESS || formatCount == 0)
        {
            output << "[Vulkan] The surface exposes no presentable image formats.\n";
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        result = getSurfaceFormats(context.physicalDevice, context.surface, &formatCount, formats.data());
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Surface-format query failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkSurfaceFormatKHR chosenFormat = formats.front();
        for (const VkSurfaceFormatKHR& format : formats)
        {
            if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosenFormat = format;
                break;
            }
        }
        if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
            chosenFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

        uint32_t presentModeCount = 0;
        result = getPresentModes(context.physicalDevice, context.surface, &presentModeCount, nullptr);
        if (result != VK_SUCCESS || presentModeCount == 0)
        {
            output << "[Vulkan] The surface exposes no presentation modes.\n";
            return false;
        }
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        result = getPresentModes(context.physicalDevice, context.surface, &presentModeCount, presentModes.data());
        if (result != VK_SUCCESS || std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_FIFO_KHR) == presentModes.end())
        {
            output << "[Vulkan] The required FIFO presentation mode is unavailable.\n";
            return false;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(context.window, &width, &height);
        if (width <= 0 || height <= 0)
        {
            output << "[Vulkan] The smoke-test window has no drawable framebuffer.\n";
            return false;
        }

        context.extent = capabilities.currentExtent;
        if (context.extent.width == UINT32_MAX)
        {
            context.extent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            context.extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0)
            imageCount = std::min(imageCount, capabilities.maxImageCount);

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = context.surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = chosenFormat.format;
        swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
        swapchainInfo.imageExtent = context.extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (context.graphicsFamily != context.presentFamily)
        {
            const uint32_t families[] = { context.graphicsFamily, context.presentFamily };
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2;
            swapchainInfo.pQueueFamilyIndices = families;
        }
        else
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        swapchainInfo.preTransform = capabilities.currentTransform;
        const VkCompositeAlphaFlagBitsKHR alphaModes[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        };
        for (VkCompositeAlphaFlagBitsKHR mode : alphaModes)
        {
            if (capabilities.supportedCompositeAlpha & mode)
            {
                swapchainInfo.compositeAlpha = mode;
                break;
            }
        }
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;

        result = createSwapchain(context.device, &swapchainInfo, nullptr, &context.swapchain);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Swapchain creation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        uint32_t swapchainImageCount = 0;
        result = getSwapchainImages(context.device, context.swapchain, &swapchainImageCount, nullptr);
        if (result != VK_SUCCESS || swapchainImageCount == 0)
        {
            output << "[Vulkan] The created swapchain has no images.\n";
            return false;
        }
        context.images.resize(swapchainImageCount);
        result = getSwapchainImages(context.device, context.swapchain, &swapchainImageCount, context.images.data());
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Swapchain image retrieval failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkAttachmentDescription attachment{};
        attachment.format = chosenFormat.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        result = createRenderPass(context.device, &renderPassInfo, nullptr, &context.renderPass);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Render-pass creation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        context.imageViews.resize(context.images.size(), VK_NULL_HANDLE);
        context.framebuffers.resize(context.images.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < context.images.size(); ++i)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = context.images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = chosenFormat.format;
            viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            result = createImageView(context.device, &viewInfo, nullptr, &context.imageViews[i]);
            if (result != VK_SUCCESS)
            {
                output << "[Vulkan] Swapchain image-view creation failed (VkResult " << static_cast<int>(result) << ").\n";
                return false;
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = context.renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &context.imageViews[i];
            framebufferInfo.width = context.extent.width;
            framebufferInfo.height = context.extent.height;
            framebufferInfo.layers = 1;
            result = createFramebuffer(context.device, &framebufferInfo, nullptr, &context.framebuffers[i]);
            if (result != VK_SUCCESS)
            {
                output << "[Vulkan] Framebuffer creation failed (VkResult " << static_cast<int>(result) << ").\n";
                return false;
            }
        }

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.queueFamilyIndex = context.graphicsFamily;
        result = createCommandPool(context.device, &commandPoolInfo, nullptr, &context.commandPool);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Command-pool creation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }
        context.commandBuffers.resize(context.images.size(), VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferInfo.commandPool = context.commandPool;
        commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferInfo.commandBufferCount = static_cast<uint32_t>(context.commandBuffers.size());
        result = allocateCommandBuffers(context.device, &commandBufferInfo, context.commandBuffers.data());
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Command-buffer allocation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = createSemaphore(context.device, &semaphoreInfo, nullptr, &context.imageAvailable);
        if (result == VK_SUCCESS)
            result = createSemaphore(context.device, &semaphoreInfo, nullptr, &context.renderFinished);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Frame semaphore creation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = createFence(context.device, &fenceInfo, nullptr, &context.frameFence);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Frame fence creation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        uint32_t imageIndex = 0;
        result = acquireNextImage(context.device, context.swapchain, UINT64_MAX,
            context.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            output << "[Vulkan] Swapchain image acquisition failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VkCommandBuffer commandBuffer = context.commandBuffers[imageIndex];
        result = beginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Command-buffer begin failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }
        VkClearValue clearValue{};
        clearValue.color = { { 0.035f, 0.085f, 0.19f, 1.0f } };
        VkRenderPassBeginInfo renderBegin{};
        renderBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderBegin.renderPass = context.renderPass;
        renderBegin.framebuffer = context.framebuffers[imageIndex];
        renderBegin.renderArea.extent = context.extent;
        renderBegin.clearValueCount = 1;
        renderBegin.pClearValues = &clearValue;
        cmdBeginRenderPass(commandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
        cmdEndRenderPass(commandBuffer);
        result = endCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Command-buffer end failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &context.imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &context.renderFinished;
        result = queueSubmit(context.graphicsQueue, 1, &submitInfo, context.frameFence);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Graphics queue submission failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &context.renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &context.swapchain;
        presentInfo.pImageIndices = &imageIndex;
        result = queuePresent(context.presentQueue, &presentInfo);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            output << "[Vulkan] Frame presentation failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }
        result = waitForFences(context.device, 1, &context.frameFence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] GPU completion wait failed (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }
        result = context.queueWaitIdle(context.presentQueue);
        if (result != VK_SUCCESS)
        {
            output << "[Vulkan] Presentation queue did not become idle (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        output << "[Vulkan] Presented one clear frame (" << context.extent.width << "x"
            << context.extent.height << ").\n";
        return true;
    }
}

bool VulkanSmokeWindow::PresentOneFrame(std::ostream& output)
{
    if (!glfwVulkanSupported())
    {
        output << "[Vulkan] GLFW cannot find a usable Vulkan loader on this system.\n";
        return false;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    VulkanSmokeContext context;
    context.window = glfwCreateWindow(960, 540, "Marae Engine - Vulkan experimental", nullptr, nullptr);
    if (!context.window)
    {
        const char* description = nullptr;
        const int code = glfwGetError(&description);
        output << "[Vulkan] GLFW could not create the no-API window (" << code << "): "
            << (description ? description : "unknown error") << ".\n";
        return false;
    }

    uint32_t requiredExtensionCount = 0;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
    if (!requiredExtensions || requiredExtensionCount == 0)
    {
        output << "[Vulkan] GLFW did not provide the instance extensions required for a window surface.\n";
        return false;
    }

    const auto createInstance = LoadInstanceFunction<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
    const auto enumerateInstanceExtensions = LoadInstanceFunction<PFN_vkEnumerateInstanceExtensionProperties>(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!createInstance || !enumerateInstanceExtensions)
    {
        output << "[Vulkan] The Vulkan loader is missing a global instance function.\n";
        return false;
    }

    uint32_t availableExtensionCount = 0;
    VkResult result = enumerateInstanceExtensions(nullptr, &availableExtensionCount, nullptr);
    if (result != VK_SUCCESS)
    {
        output << "[Vulkan] Could not enumerate instance extensions (VkResult " << static_cast<int>(result) << ").\n";
        return false;
    }
    std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
    result = enumerateInstanceExtensions(nullptr, &availableExtensionCount, availableExtensions.data());
    if (result != VK_SUCCESS)
    {
        output << "[Vulkan] Instance-extension query failed (VkResult " << static_cast<int>(result) << ").\n";
        return false;
    }
    for (uint32_t i = 0; i < requiredExtensionCount; ++i)
    {
        const bool available = std::any_of(availableExtensions.begin(), availableExtensions.end(),
            [requiredExtensions, i](const auto& extension)
            {
                return std::strcmp(extension.extensionName, requiredExtensions[i]) == 0;
            });
        if (!available)
        {
            output << "[Vulkan] Required window extension is unavailable: " << requiredExtensions[i] << ".\n";
            return false;
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Marae Engine Vulkan smoke test";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.pEngineName = "Marae Engine";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = requiredExtensionCount;
    instanceInfo.ppEnabledExtensionNames = requiredExtensions;
    result = createInstance(&instanceInfo, nullptr, &context.instance);
    if (result != VK_SUCCESS)
    {
        output << "[Vulkan] Window instance creation failed (VkResult " << static_cast<int>(result) << ").\n";
        return false;
    }

    context.destroyInstance = LoadInstanceFunction<PFN_vkDestroyInstance>(context.instance, "vkDestroyInstance");
    context.destroySurface = LoadInstanceFunction<PFN_vkDestroySurfaceKHR>(context.instance, "vkDestroySurfaceKHR");
    const auto createWindowSurface = glfwCreateWindowSurface;
    if (!context.destroyInstance || !context.destroySurface ||
        createWindowSurface(context.instance, context.window, nullptr, &context.surface) != VK_SUCCESS)
    {
        output << "[Vulkan] Could not create the GLFW Vulkan surface.\n";
        return false;
    }

    if (!LoadInstanceFunctions(context, output))
        return false;

    if (!CreateSwapchainAndClearResources(context, output))
        return false;

    output << "[Vulkan] The experimental window will close automatically in 5 seconds.\n";
    const auto closeAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!glfwWindowShouldClose(context.window) && std::chrono::steady_clock::now() < closeAt)
    {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    return true;
}
