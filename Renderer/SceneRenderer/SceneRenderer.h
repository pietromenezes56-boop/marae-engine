#pragma once

#include "../../Scene/Scene/Scene.h"
#include "../Shader/Shader.h"
#include "../Mesh/Mesh.h"
#include "../TransformRenderer/TransformRenderer.h"
#include "../InstancedMesh/InstancedRenderer.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../Engine/Lighting/LightingManager.h"
#include "../GridRenderer/GridRenderer.h"
#include "../../Hub/ProjectManager.h"

class SceneRenderer
{
public:
    struct RenderStats
    {
        unsigned int DrawCalls = 0;
        unsigned int Triangles = 0;
        unsigned int VisibleEntities = 0;
        unsigned int FrustumCulledEntities = 0;
        unsigned int ShadowDrawCalls = 0;
        unsigned int ShadowTriangles = 0;
        double RenderTimeMs = 0.0;
        double ShadowTimeMs = 0.0;
        double GpuTimeMs = 0.0;
        bool GpuTimeAvailable = false;
    };
    SceneRenderer();
    static int RunFrustumCullingRegressionTests();

    ~SceneRenderer();

    void SetQuality(GraphicsQuality quality);

    // ── Reflexo Planar (Mirror) ───────────────────────────────────────────
    // Chamado pelo Renderer antes de Render() quando há entidades Mirror na
    // cena. Produz a textura de reflexo no m_ReflectionFBO.
    void RenderReflection(
        Scene& scene,
        Shader& shader,
        LightingManager& lighting,
        const glm::mat4& mirrorModelMatrix,    // transform da entidade espelho
        const glm::vec3& mirrorNormalWorld,    // normal do espelho em world space
        const glm::mat4& cameraView,
        const glm::mat4& projection,
        int  reflectionResolution,             // pixels (ex: 256, 512, 1024)
        unsigned int whiteTextureID = 0,
        uint64_t mirrorEntityID = 0
    );

    // Textura GPU gerada pelo último RenderReflection()
    unsigned int GetReflectionTextureID() const;

    void Render(
        Scene& scene,
        Shader& shader,
        LightingManager& lighting,
        int selectedEntity,
        const std::vector<int>& multiSelectedEntities,
        GLuint whiteTextureID,
        const glm::mat4& view,
        const glm::mat4& projection,
        bool showAxis = true,
        bool showGrid = true,
        float gridRenderDistance = 500.0f,
        bool editor2DMode = false
    );

    const RenderStats& GetLastStats() const { return m_LastStats; }

    // ── Informações de sombra (usadas pelo Profiler da Cena) ──────────────
    unsigned int GetShadowMapSize() const { return m_ShadowMapSize; }
    unsigned int GetPointShadowMapSize() const { return m_PointShadowMapSize; }
    int GetPointShadowMapCount() const { return m_PointShadowMapCount; }
    int GetActiveCascadeCount() const { return m_ActiveCascadeCount; }
    bool AreShadowsEnabled() const { return m_UseShadows; }

    // Instanced rendering control
    void SetInstancedRenderingEnabled(bool enabled) { m_InstancedRenderingEnabled = enabled; }
    bool IsInstancedRenderingEnabled() const { return m_InstancedRenderingEnabled; }
    InstancedRenderer& GetInstancedRenderer() { return m_InstancedRenderer; }

private:
    void InitShadowMap();
    void InitPointShadowMaps(int requiredMapCount);

    TransformRenderer m_TransformRenderer;
    std::array<Mesh, 6> m_ParticleShapeMeshes;
    std::unordered_map<std::string, std::shared_ptr<class Texture>> m_ParticleTextures;
    GLuint m_ShadowFBO[Shader::MAX_SHADOW_CASCADES] = { 0, 0, 0, 0 };
    GLuint m_ShadowMap[Shader::MAX_SHADOW_CASCADES] = { 0, 0, 0, 0 };
    GLuint m_ShadowOpacityMap[Shader::MAX_SHADOW_CASCADES] = { 0, 0, 0, 0 };
    GLuint m_PointShadowFBO = 0;
    GLuint m_PointShadowMaps[Shader::MAX_LOCAL_SHADOWS] = { 0, 0, 0, 0 };
    GLuint m_PointShadowOpacityMaps[Shader::MAX_LOCAL_SHADOWS] = { 0, 0, 0, 0 };
    unsigned int m_ShadowMapSize = 2048;
    unsigned int m_RequestedShadowMapSize = 2048;
    unsigned int m_EffectiveShadowMapSize = 2048;
    int m_ActiveCascadeCount = Shader::MAX_SHADOW_CASCADES;
    unsigned int m_PointShadowMapSize = 0;
    int m_PointShadowMapCount = 0;
    unsigned int m_RequestedPointShadowMapSize = 1024;
    unsigned int m_EffectivePointShadowMapSize = 1024;
    std::array<std::size_t, Shader::MAX_LOCAL_SHADOWS> m_LocalShadowSourceSignatures{};
    std::array<bool, Shader::MAX_LOCAL_SHADOWS> m_LocalShadowSourceCacheValid{};
    std::size_t m_DirectionalShadowSignature = 0;
    bool m_LocalShadowCacheValid = false;
    bool m_DirectionalShadowCacheValid = false;
    std::array<glm::mat4, Shader::MAX_SHADOW_CASCADES> m_CachedCascadeMatrices{};
    // Alcance em unidades de mundo do depth ortográfico de cada cascata
    // (farPlane - nearPlane). O shader usa para converter o vão
    // oclusor→receptor em penumbra física.
    std::array<float, Shader::MAX_SHADOW_CASCADES> m_CachedCascadeDepthRanges{};
    bool m_UseShadows = true;
    GraphicsQuality m_Quality = GraphicsQuality::Balanced;
    GridRenderer m_GridRenderer;
    InstancedRenderer m_InstancedRenderer;
    bool m_InstancedRenderingEnabled = true;
    RenderStats m_LastStats{};

    // Buffers scratch reused per frame to avoid sorting/selection/tile heap
    // allocations for every viewport render.
    std::vector<int> m_DrawOrderScratch;
    std::vector<unsigned char> m_MultiSelectionFlagsScratch;
    std::vector<glm::mat4> m_TileModelsScratch;
    std::vector<glm::mat4> m_EntityModelsScratch;
    std::vector<std::size_t> m_EntityFirstChildScratch;
    std::vector<std::size_t> m_EntityNextSiblingScratch;
    std::vector<float> m_EntityShadowOpacityScratch;
    std::array<std::vector<std::size_t>, Shader::MAX_LOCAL_SHADOWS> m_LocalShadowCasterScratch;

    // ── Reflexo Planar ────────────────────────────────────────────────────
    GLuint m_ReflectionFBO       = 0;
    GLuint m_ReflectionTexture   = 0;
    GLuint m_ReflectionDepthRBO  = 0;
    int    m_ReflectionRes       = 0;
    bool   m_IsRenderingReflection = false;
    uint64_t m_ActiveMirrorEntityID  = 0;

    void InitReflectionFBO(int resolution);
    void DestroyReflectionFBO();

    GLuint m_GpuTimerQuery = 0;
    bool m_GpuQueryInFlight = false;
};
