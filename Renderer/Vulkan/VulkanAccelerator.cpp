#include "VulkanAccelerator.h"
#include <iostream>

VulkanAccelerator::VulkanAccelerator()
    : m_ShadowRenderer(std::make_unique<VulkanShadowRenderer>())
    , m_LightingProcessor(std::make_unique<VulkanLightingProcessor>())
    , m_MemoryManager(std::make_unique<VulkanMemoryManager>())
{
}

VulkanAccelerator::~VulkanAccelerator()
{
    Shutdown();
}

bool VulkanAccelerator::Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                    VkQueue graphicsQueue, VkQueue computeQueue,
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
        std::cout << "[VulkanAccelerator] Vulkan loader not available\n";
        return false;
    }

    // Inicializa gerenciador de memória (sempre ativo se Vulkan estiver disponível)
    if (!m_MemoryManager->Initialize(m_Loader, instance, physicalDevice, device))
    {
        std::cout << "[VulkanAccelerator] Memory manager initialization failed, disabling Vulkan memory management\n";
        m_Config.MemoryConfig.Enabled = false;
    }
    else
    {
        std::cout << "[VulkanAccelerator] Memory manager initialized\n";
    }

    // Inicializa renderizador de sombras (opcional - pode falhar sem shaders)
    if (!m_ShadowRenderer->Initialize(m_Loader, instance, physicalDevice, device, graphicsQueue, graphicsQueueFamilyIndex))
    {
        std::cout << "[VulkanAccelerator] Shadow renderer initialization failed (missing shaders?), disabling Vulkan shadows\n";
        m_Config.ShadowConfig.Enabled = false;
    }
    else
    {
        std::cout << "[VulkanAccelerator] Shadow renderer initialized\n";
    }

    // Inicializa processador de iluminação (opcional - pode falhar sem shaders)
    if (!m_LightingProcessor->Initialize(m_Loader, instance, physicalDevice, device, graphicsQueue, computeQueue,
                                          graphicsQueueFamilyIndex, computeQueueFamilyIndex))
    {
        std::cout << "[VulkanAccelerator] Lighting processor initialization failed (missing shaders?), disabling Vulkan lighting\n";
        m_Config.LightingConfig.Enabled = false;
    }
    else
    {
        std::cout << "[VulkanAccelerator] Lighting processor initialized\n";
    }

    m_Initialized = true;
    std::cout << "[VulkanAccelerator] Initialized successfully (OpenGL remains primary renderer)\n";
    std::cout << "[VulkanAccelerator] Vulkan instance/device ready for future shader integration\n";
    return true;
}

void VulkanAccelerator::Shutdown()
{
    if (!m_Initialized)
        return;

    m_ShadowRenderer->Shutdown();
    m_LightingProcessor->Shutdown();
    m_MemoryManager->Shutdown();

    m_Initialized = false;
    std::cout << "[VulkanAccelerator] Shutdown complete\n";
}

void VulkanAccelerator::UpdateSceneStats(int objectCount, int lightCount, size_t memoryUsage)
{
    m_Config.ObjectCount = objectCount;
    m_Config.LightCount = lightCount;
    m_Config.MemoryUsage = memoryUsage;

    EvaluateActivation();
}

void VulkanAccelerator::EvaluateActivation()
{
    // Avalia se deve ativar/desativar recursos Vulkan baseado na carga
    
    // Sombras
    if (m_ShadowRenderer->CanRender(m_Config.ShadowConfig, m_Config.ObjectCount))
    {
        if (!m_Config.ShadowConfig.Enabled)
        {
            m_Config.ShadowConfig.Enabled = true;
            std::cout << "[VulkanAccelerator] Vulkan shadow rendering ACTIVATED ("
                      << m_Config.ObjectCount << " objects)\n";
        }
    }
    else
    {
        if (m_Config.ShadowConfig.Enabled)
        {
            m_Config.ShadowConfig.Enabled = false;
            std::cout << "[VulkanAccelerator] Vulkan shadow rendering DEACTIVATED\n";
        }
    }

    // Iluminação
    if (m_LightingProcessor->CanProcess(m_Config.LightingConfig, m_Config.LightCount))
    {
        if (!m_Config.LightingConfig.Enabled)
        {
            m_Config.LightingConfig.Enabled = true;
            std::cout << "[VulkanAccelerator] Vulkan lighting processing ACTIVATED ("
                      << m_Config.LightCount << " lights)\n";
        }
    }
    else
    {
        if (m_Config.LightingConfig.Enabled)
        {
            m_Config.LightingConfig.Enabled = false;
            std::cout << "[VulkanAccelerator] Vulkan lighting processing DEACTIVATED\n";
        }
    }

    // Memória
    if (m_MemoryManager->ShouldActivate(m_Config.MemoryConfig, m_Config.ObjectCount, m_Config.MemoryUsage))
    {
        if (!m_Config.MemoryConfig.Enabled)
        {
            m_Config.MemoryConfig.Enabled = true;
            std::cout << "[VulkanAccelerator] Vulkan memory management ACTIVATED ("
                      << (m_Config.MemoryUsage / (1024 * 1024)) << " MB used)\n";
        }
    }
    else
    {
        if (m_Config.MemoryConfig.Enabled)
        {
            m_Config.MemoryConfig.Enabled = false;
            std::cout << "[VulkanAccelerator] Vulkan memory management DEACTIVATED\n";
        }
    }
}

bool VulkanAccelerator::IsShadowRenderingActive() const
{
    return m_Config.ShadowConfig.Enabled;
}

bool VulkanAccelerator::IsLightingProcessingActive() const
{
    return m_Config.LightingConfig.Enabled;
}

bool VulkanAccelerator::IsMemoryManagementActive() const
{
    return m_Config.MemoryConfig.Enabled;
}

bool VulkanAccelerator::RenderShadows(const glm::mat4& lightViewMatrix, const glm::mat4& lightProjMatrix,
                                       const std::vector<glm::mat4>& objectTransforms)
{
    if (!m_Config.ShadowConfig.Enabled)
        return false;

    return m_ShadowRenderer->RenderShadowMap(lightViewMatrix, lightProjMatrix, objectTransforms, 
                                               m_Config.ShadowConfig);
}

bool VulkanAccelerator::ProcessLighting(const std::vector<LightData>& lights)
{
    if (!m_Config.LightingConfig.Enabled)
        return false;

    return m_LightingProcessor->ProcessLighting(lights, m_Config.LightingConfig);
}

void VulkanAccelerator::ManageMemory()
{
    if (!m_Config.MemoryConfig.Enabled)
        return;

    m_MemoryManager->UpdateStreaming();
}

bool VulkanAccelerator::StreamTexture(const std::string& texturePath, int priority)
{
    if (!m_Config.MemoryConfig.Enabled)
        return false;

    return m_MemoryManager->StreamTexture(texturePath, priority);
}
