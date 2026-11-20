#pragma once

// ---------------------------------------------------------------------------
// VulkanAccelerator
//
// Coordenador principal do sistema híbrido OpenGL/Vulkan.
// Gerencia quando ativar recursos Vulkan baseado na carga da cena.
// OpenGL continua sendo o renderizador principal do viewport.
// ---------------------------------------------------------------------------

#include "VulkanShadowRenderer.h"
#include "VulkanLightingProcessor.h"
#include "VulkanMemoryManager.h"
#include "VulkanDynamicLoader.h"
#include <memory>
#include <string>

struct VulkanAcceleratorConfig
{
    bool Enabled = true;
    
    // Configurações de sombra
    VulkanShadowConfig ShadowConfig;
    
    // Configurações de iluminação
    VulkanLightingConfig LightingConfig;
    
    // Configurações de memória
    VulkanMemoryConfig MemoryConfig;
    
    // Estatísticas
    int ObjectCount = 0;
    int LightCount = 0;
    size_t MemoryUsage = 0;
};

class VulkanAccelerator
{
public:
    VulkanAccelerator();
    ~VulkanAccelerator();

    bool Initialize(VulkanDynamicLoader* loader, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                    VkQueue graphicsQueue, VkQueue computeQueue,
                    uint32_t graphicsQueueFamilyIndex, uint32_t computeQueueFamilyIndex);
    void Shutdown();

    // Atualiza estatísticas da cena e decide se deve ativar recursos Vulkan
    void UpdateSceneStats(int objectCount, int lightCount, size_t memoryUsage);
    
    // Verifica se recursos Vulkan estão ativos
    bool IsShadowRenderingActive() const;
    bool IsLightingProcessingActive() const;
    bool IsMemoryManagementActive() const;
    
    // Renderiza sombras com Vulkan (se ativo)
    bool RenderShadows(const glm::mat4& lightViewMatrix, const glm::mat4& lightProjMatrix,
                       const std::vector<glm::mat4>& objectTransforms);
    
    // Processa iluminação com Vulkan (se ativo)
    bool ProcessLighting(const std::vector<LightData>& lights);
    
    // Gerencia memória com Vulkan (se ativo)
    void ManageMemory();
    
    // Stream texturas com Vulkan (se ativo)
    bool StreamTexture(const std::string& texturePath, int priority = 0);
    
    // Configuração
    const VulkanAcceleratorConfig& GetConfig() const { return m_Config; }
    void SetConfig(const VulkanAcceleratorConfig& config) { m_Config = config; }

private:
    void EvaluateActivation();

    VulkanDynamicLoader* m_Loader = nullptr;
    std::unique_ptr<VulkanShadowRenderer> m_ShadowRenderer;
    std::unique_ptr<VulkanLightingProcessor> m_LightingProcessor;
    std::unique_ptr<VulkanMemoryManager> m_MemoryManager;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_ComputeQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = 0;
    uint32_t m_ComputeQueueFamilyIndex = 0;

    VulkanAcceleratorConfig m_Config;
    
    bool m_Initialized = false;
};
