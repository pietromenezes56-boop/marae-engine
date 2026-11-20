#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include "VulkanImGuiRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>

struct VulkanImGuiRenderer::Impl
{
    GLFWwindow* Window = nullptr;
    VkInstance Instance = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    uint32_t QueueFamily = UINT32_MAX;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window MainWindow;
    bool BackendInitialized = false;

    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkDestroySurfaceKHR DestroySurface = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImage = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;
    PFN_vkResetCommandPool ResetCommandPool = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;
    PFN_vkQueuePresentKHR QueuePresent = nullptr;

    template<typename T>
    T LoadInstance(const char* name) const
    {
        return reinterpret_cast<T>(glfwGetInstanceProcAddress(Instance, name));
    }

    template<typename T>
    T LoadDevice(const char* name) const
    {
        return GetDeviceProcAddr
            ? reinterpret_cast<T>(GetDeviceProcAddr(Device, name))
            : nullptr;
    }

    static PFN_vkVoidFunction ImGuiFunctionLoader(const char* name, void* userData)
    {
        auto* self = static_cast<Impl*>(userData);
        PFN_vkVoidFunction function = glfwGetInstanceProcAddress(self->Instance, name);
        if (!function && self->GetDeviceProcAddr && self->Device)
            function = self->GetDeviceProcAddr(self->Device, name);
        return function;
    }

    bool CreateInstanceAndSurface()
    {
        if (!glfwVulkanSupported())
        {
            std::cerr << "[Vulkan] O loader Vulkan não está disponível neste computador.\n";
            return false;
        }

        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions || extensionCount == 0)
        {
            std::cerr << "[Vulkan] GLFW não forneceu as extensões de superfície.\n";
            return false;
        }

        const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
            glfwGetInstanceProcAddress(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!createInstance)
        {
            std::cerr << "[Vulkan] vkCreateInstance não foi encontrado no loader.\n";
            return false;
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Marae Engine Editor";
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
        applicationInfo.pEngineName = "Marae Engine";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;
        const VkResult createResult = createInstance(&createInfo, nullptr, &Instance);
        if (createResult != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Falha ao criar instância (VkResult "
                      << static_cast<int>(createResult) << ").\n";
            return false;
        }

        DestroyInstance = LoadInstance<PFN_vkDestroyInstance>("vkDestroyInstance");
        DestroySurface = LoadInstance<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
        if (!DestroyInstance || !DestroySurface ||
            glfwCreateWindowSurface(Instance, Window, nullptr, &Surface) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Não foi possível criar a superfície da janela.\n";
            return false;
        }
        return true;
    }

    bool SelectDeviceAndCreateLogicalDevice()
    {
        const auto enumerateDevices = LoadInstance<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        const auto getQueueFamilies = LoadInstance<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        const auto getSurfaceSupport = LoadInstance<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR");
        const auto enumerateExtensions = LoadInstance<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        const auto createDevice = LoadInstance<PFN_vkCreateDevice>("vkCreateDevice");
        GetDeviceProcAddr = LoadInstance<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        if (!enumerateDevices || !getQueueFamilies || !getSurfaceSupport ||
            !enumerateExtensions || !createDevice || !GetDeviceProcAddr)
        {
            std::cerr << "[Vulkan] O loader não expõe as funções necessárias para escolher a GPU.\n";
            return false;
        }

        uint32_t deviceCount = 0;
        if (enumerateDevices(Instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
        {
            std::cerr << "[Vulkan] Nenhuma GPU Vulkan foi encontrada.\n";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (enumerateDevices(Instance, &deviceCount, devices.data()) != VK_SUCCESS)
            return false;

        for (VkPhysicalDevice candidate : devices)
        {
            uint32_t familyCount = 0;
            getQueueFamilies(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            getQueueFamilies(candidate, &familyCount, families.data());

            uint32_t candidateFamily = UINT32_MAX;
            for (uint32_t index = 0; index < familyCount; ++index)
            {
                VkBool32 canPresent = VK_FALSE;
                if (families[index].queueCount > 0 &&
                    (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    getSurfaceSupport(candidate, index, Surface, &canPresent) == VK_SUCCESS &&
                    canPresent == VK_TRUE)
                {
                    candidateFamily = index;
                    break;
                }
            }
            if (candidateFamily == UINT32_MAX)
                continue;

            uint32_t extensionCount = 0;
            if (enumerateExtensions(candidate, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
                continue;
            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            if (enumerateExtensions(candidate, nullptr, &extensionCount, availableExtensions.data()) != VK_SUCCESS)
                continue;
            const bool supportsSwapchain = std::any_of(availableExtensions.begin(), availableExtensions.end(),
                [](const VkExtensionProperties& extension)
                {
                    return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                });
            if (!supportsSwapchain)
                continue;

            PhysicalDevice = candidate;
            QueueFamily = candidateFamily;
            break;
        }
        if (!PhysicalDevice)
        {
            std::cerr << "[Vulkan] Não há GPU com fila gráfica/apresentação e suporte a swapchain.\n";
            return false;
        }

        constexpr float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = QueueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        if (createDevice(PhysicalDevice, &deviceInfo, nullptr, &Device) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Falha ao criar o dispositivo lógico.\n";
            return false;
        }

        DestroyDevice = LoadDevice<PFN_vkDestroyDevice>("vkDestroyDevice");
        DeviceWaitIdle = LoadDevice<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
        GetDeviceQueue = LoadDevice<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
        if (!DestroyDevice || !DeviceWaitIdle || !GetDeviceQueue)
            return false;
        GetDeviceQueue(Device, QueueFamily, 0, &Queue);
        return Queue != VK_NULL_HANDLE;
    }

    bool CreatePresentationResources()
    {
        CreateDescriptorPool = LoadDevice<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool");
        DestroyDescriptorPool = LoadDevice<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
        AcquireNextImage = LoadDevice<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
        WaitForFences = LoadDevice<PFN_vkWaitForFences>("vkWaitForFences");
        ResetFences = LoadDevice<PFN_vkResetFences>("vkResetFences");
        ResetCommandPool = LoadDevice<PFN_vkResetCommandPool>("vkResetCommandPool");
        BeginCommandBuffer = LoadDevice<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
        CmdBeginRenderPass = LoadDevice<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass");
        CmdEndRenderPass = LoadDevice<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass");
        EndCommandBuffer = LoadDevice<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
        QueueSubmit = LoadDevice<PFN_vkQueueSubmit>("vkQueueSubmit");
        QueuePresent = LoadDevice<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
        if (!CreateDescriptorPool || !DestroyDescriptorPool || !AcquireNextImage ||
            !WaitForFences || !ResetFences || !ResetCommandPool || !BeginCommandBuffer ||
            !CmdBeginRenderPass || !CmdEndRenderPass || !EndCommandBuffer || !QueueSubmit || !QueuePresent)
        {
            std::cerr << "[Vulkan] O loader não expõe todas as funções para apresentar os painéis.\n";
            return false;
        }

        // The bundled backend helpers share the same function table; populate
        // it before selecting formats/creating the helper-managed swapchain.
        if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, ImGuiFunctionLoader, this))
        {
            std::cerr << "[Vulkan] O backend do ImGui não conseguiu carregar suas funções Vulkan.\n";
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 16 }
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 80;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;
        if (CreateDescriptorPool(Device, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
            return false;

        const VkFormat formats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,
            VK_FORMAT_R8G8B8_UNORM
        };
        MainWindow.Surface = Surface;
        MainWindow.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            PhysicalDevice, Surface, formats, std::size(formats), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        const VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
        MainWindow.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            PhysicalDevice, Surface, presentModes, std::size(presentModes));
        MainWindow.ClearValue.color = { { 0.045f, 0.055f, 0.075f, 1.0f } };

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(Window, &width, &height);
        if (width <= 0 || height <= 0)
        {
            std::cerr << "[Vulkan] A superfície não tem tamanho de framebuffer válido.\n";
            return false;
        }
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            Instance, PhysicalDevice, Device, &MainWindow, QueueFamily, nullptr,
            width, height, 2, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

        return InitializeImGuiBackend();
    }

    bool InitializeImGuiBackend()
    {
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_0;
        initInfo.Instance = Instance;
        initInfo.PhysicalDevice = PhysicalDevice;
        initInfo.Device = Device;
        initInfo.QueueFamily = QueueFamily;
        initInfo.Queue = Queue;
        initInfo.DescriptorPool = DescriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = MainWindow.ImageCount;
        initInfo.PipelineInfoMain.RenderPass = MainWindow.RenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.CheckVkResultFn = [](VkResult result)
        {
            if (result != VK_SUCCESS)
                std::cerr << "[Vulkan] ImGui backend VkResult " << static_cast<int>(result) << ".\n";
        };
        if (!ImGui_ImplVulkan_Init(&initInfo))
            return false;
        BackendInitialized = true;
        return true;
    }

    bool RecreateSwapchain()
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(Window, &width, &height);
        if (width <= 0 || height <= 0)
            return false;
        if (DeviceWaitIdle(Device) != VK_SUCCESS)
            return false;
        // Pipelines target the current swapchain render pass. Recreate the
        // ImGui Vulkan backend whenever that render pass/swapchain is rebuilt.
        if (BackendInitialized)
        {
            ImGui_ImplVulkan_Shutdown();
            BackendInitialized = false;
        }
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            Instance, PhysicalDevice, Device, &MainWindow, QueueFamily, nullptr,
            width, height, 2, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        return MainWindow.ImageCount >= 2 && InitializeImGuiBackend();
    }

    bool RenderDrawData(ImDrawData* drawData)
    {
        if (!drawData || !Window || !Device)
            return false;

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(Window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
            return true;
        if (MainWindow.Width != framebufferWidth || MainWindow.Height != framebufferHeight)
        {
            if (!RecreateSwapchain())
                return false;
        }

        ImGui_ImplVulkanH_Window* windowData = &MainWindow;
        const uint32_t semaphoreIndex = windowData->SemaphoreIndex;
        const VkSemaphore imageAcquired = windowData->FrameSemaphores[semaphoreIndex].ImageAcquiredSemaphore;
        const VkSemaphore renderComplete = windowData->FrameSemaphores[semaphoreIndex].RenderCompleteSemaphore;
        VkResult result = AcquireNextImage(Device, windowData->Swapchain, UINT64_MAX,
            imageAcquired, VK_NULL_HANDLE, &windowData->FrameIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            return true;
        }
        const bool acquiredSuboptimal = result == VK_SUBOPTIMAL_KHR;
        if (result != VK_SUCCESS && !acquiredSuboptimal)
        {
            std::cerr << "[Vulkan] Aquisição do frame falhou (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        ImGui_ImplVulkanH_Frame* frame = &windowData->Frames[windowData->FrameIndex];
        result = WaitForFences(Device, 1, &frame->Fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS || ResetFences(Device, 1, &frame->Fence) != VK_SUCCESS ||
            ResetCommandPool(Device, frame->CommandPool, 0) != VK_SUCCESS)
            return false;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (BeginCommandBuffer(frame->CommandBuffer, &beginInfo) != VK_SUCCESS)
            return false;

        VkRenderPassBeginInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderInfo.renderPass = windowData->RenderPass;
        renderInfo.framebuffer = frame->Framebuffer;
        renderInfo.renderArea.extent = { static_cast<uint32_t>(windowData->Width),
                                         static_cast<uint32_t>(windowData->Height) };
        renderInfo.clearValueCount = 1;
        renderInfo.pClearValues = &windowData->ClearValue;
        CmdBeginRenderPass(frame->CommandBuffer, &renderInfo, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(drawData, frame->CommandBuffer);
        CmdEndRenderPass(frame->CommandBuffer);
        if (EndCommandBuffer(frame->CommandBuffer) != VK_SUCCESS)
            return false;

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAcquired;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame->CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderComplete;
        result = QueueSubmit(Queue, 1, &submitInfo, frame->Fence);
        if (result != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Submissão do frame falhou (VkResult " << static_cast<int>(result) << ").\n";
            return false;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderComplete;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &windowData->Swapchain;
        presentInfo.pImageIndices = &windowData->FrameIndex;
        result = QueuePresent(Queue, &presentInfo);
        windowData->SemaphoreIndex = (windowData->SemaphoreIndex + 1) % windowData->SemaphoreCount;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquiredSuboptimal)
            return RecreateSwapchain();
        if (result != VK_SUCCESS)
            std::cerr << "[Vulkan] Apresentação do frame falhou (VkResult " << static_cast<int>(result) << ").\n";
        return result == VK_SUCCESS;
    }
};

VulkanImGuiRenderer::VulkanImGuiRenderer() : m_Impl(std::make_unique<Impl>()) {}
VulkanImGuiRenderer::~VulkanImGuiRenderer() { Shutdown(); }

bool VulkanImGuiRenderer::Initialize(GLFWwindow* window)
{
    Shutdown();
    m_Impl = std::make_unique<Impl>();
    m_Impl->Window = window;
    if (!window || !m_Impl->CreateInstanceAndSurface())
    {
        Shutdown();
        return false;
    }
    if (!m_Impl->SelectDeviceAndCreateLogicalDevice())
    {
        Shutdown();
        return false;
    }
    if (!m_Impl->CreatePresentationResources())
    {
        Shutdown();
        return false;
    }
    std::cout << "[Vulkan] Editor UI ativo via Vulkan/ImGui. A viewport de cena ainda usa OpenGL.\n";
    return true;
}

bool VulkanImGuiRenderer::Render(ImDrawData* drawData)
{
    return m_Impl && m_Impl->BackendInitialized && m_Impl->RenderDrawData(drawData);
}

bool VulkanImGuiRenderer::IsInitialized() const
{
    return m_Impl && m_Impl->BackendInitialized;
}

void VulkanImGuiRenderer::Shutdown()
{
    if (!m_Impl)
        return;
    if (m_Impl->Device && m_Impl->DeviceWaitIdle)
        m_Impl->DeviceWaitIdle(m_Impl->Device);
    if (m_Impl->BackendInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        m_Impl->BackendInitialized = false;
    }
    if (m_Impl->Device && m_Impl->MainWindow.Swapchain)
        ImGui_ImplVulkanH_DestroyWindow(m_Impl->Instance, m_Impl->Device, &m_Impl->MainWindow, nullptr);
    if (m_Impl->DescriptorPool && m_Impl->DestroyDescriptorPool)
        m_Impl->DestroyDescriptorPool(m_Impl->Device, m_Impl->DescriptorPool, nullptr);
    if (m_Impl->Device && m_Impl->DestroyDevice)
        m_Impl->DestroyDevice(m_Impl->Device, nullptr);
    if (m_Impl->Surface && m_Impl->Instance && m_Impl->DestroySurface)
        m_Impl->DestroySurface(m_Impl->Instance, m_Impl->Surface, nullptr);
    if (m_Impl->Instance && m_Impl->DestroyInstance)
        m_Impl->DestroyInstance(m_Impl->Instance, nullptr);
    m_Impl.reset();
}
