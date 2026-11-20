#include "SceneRenderer.h"
#include "../../Core/Diagnostics/PerformanceMonitor.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>
#include <chrono>
#include "../../Assets/MaterialDatabase/MaterialDatabase.h"
#include "../MeshFactory/MeshFactory.h"
#include "../Mesh/MeshCache.h"
#include "../../Engine/Lighting/PointLight.h"
#include "../../Engine/Lighting/LightingSystem.h"
#include "../../Engine/Lighting/ShadowSystem.h"
#include "../../Scene/Components/LightComponent.h"
#include "../../Scene/Components/LocalLightComponent.h"
#include "../../Scene/Components/GlobalLightComponent.h"
#include "../../Scene/Entity/Entity.h"
#include "../Texture/TextureCache.h"
#include "../../Editor/InspectorPanel/InspectorPanel.h"

namespace
{
    constexpr float kShadowPadding = 3.1f;
    constexpr float kShadowDepthPadding = 60.0f;
    constexpr float kLightDistance = 120.0f;

    void GetMaterialLighting(MaterialType type,
                              float& metallic,
                              float& roughness,
                              float& specular,
                              float& emissionStrength)
    {
        switch (type)
        {
        case MaterialType::Metal:
            metallic = 1.0f;
            roughness = 0.24f;
            specular = 0.95f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Wood:
            metallic = 0.0f;
            roughness = 0.72f;
            specular = 0.28f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Neon:
            metallic = 0.0f;
            roughness = 0.18f;
            specular = 0.72f;
            emissionStrength = 0.85f;
            break;
        case MaterialType::Glass:
            metallic = 0.0f;
            roughness = 0.04f;
            specular = 1.0f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Rubber:
            metallic = 0.0f;
            roughness = 0.88f;
            specular = 0.22f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Ceramic:
            metallic = 0.0f;
            roughness = 0.20f;
            specular = 0.78f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Fabric:
            metallic = 0.0f;
            roughness = 0.96f;
            specular = 0.12f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Mirror:
            // Espelho perfeito: totalmente metálico, sem rugosidade
            metallic = 1.0f;
            roughness = 0.001f;
            specular = 1.0f;
            emissionStrength = 0.0f;
            break;
        case MaterialType::Plastic:
        default:
            metallic = 0.0f;
            roughness = 0.42f;
            specular = 0.52f;
            emissionStrength = 0.0f;
            break;
        }
    }

    glm::mat4 BuildStableDirectionalShadowMatrix(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& lightRayDirection,
        unsigned int shadowMapSize,
        float sliceNear,
        float sliceFar,
        float maxShadowCasterY,
        float maxShadowDistance,
        float* outDepthNear = nullptr,
        float* outDepthFar = nullptr)
    {
        const glm::mat4 inverseCamera = glm::inverse(projection * view);
        std::array<glm::vec3, 8> frustumCorners{};

        // Converte a distância em view-space para o NDC correto da projeção.
        // O cálculo anterior avançava ao longo do raio de cada canto e
        // produzia cascatas com tamanhos diferentes quando a câmera girava.
        sliceNear = std::max(0.1f, sliceNear);
        sliceFar = std::max(sliceNear + 1.0f, sliceFar);
        auto ndcDepthForDistance = [&](float distance)
        {
            const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
            return clip.z / std::max(std::abs(clip.w), 0.0001f);
        };
        const float nearDepth = ndcDepthForDistance(sliceNear);
        const float farDepth = ndcDepthForDistance(sliceFar);
        int cornerIndex = 0;
        for (float x : { -1.0f, 1.0f })
        {
            for (float y : { -1.0f, 1.0f })
            {
                glm::vec4 nearCorner = inverseCamera * glm::vec4(x, y, nearDepth, 1.0f);
                glm::vec4 farCorner = inverseCamera * glm::vec4(x, y, farDepth, 1.0f);
                nearCorner /= nearCorner.w;
                farCorner /= farCorner.w;
                frustumCorners[cornerIndex++] = glm::vec3(nearCorner);
                frustumCorners[cornerIndex++] = glm::vec3(farCorner);
            }
        }

        glm::vec3 frustumCenter(0.0f);
        for (const glm::vec3& corner : frustumCorners)
        {
            frustumCenter += corner;
        }
        frustumCenter /= static_cast<float>(frustumCorners.size());

        // O raio da esfera do frustum não depende da orientação da câmera.
        // Usá-lo para estimar a altura receptora evita que girar a câmera
        // altere a extensão das cascatas e faça a sombra mudar de escala.
        float frustumRadius = 0.0f;
        for (const glm::vec3& corner : frustumCorners)
            frustumRadius = std::max(frustumRadius, glm::length(corner - frustumCenter));
        const float minimumReceiverY = frustumCenter.y - frustumRadius;

        const glm::vec3 lightDirection = glm::length(lightRayDirection) > 0.0001f
            ? glm::normalize(lightRayDirection)
            : glm::normalize(glm::vec3(-0.4f, -1.0f, -0.4f));
        const glm::vec3 lightUpReference = glm::abs(glm::dot(lightDirection, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);

        // Inclui casters fora do recorte da câmera que ainda podem projetar
        // sombra para dentro dele. O limite vem da altura real dos casters e
        // da inclinação do raio solar: comprimento = altura / |direção vertical|.
        // ShadowDistance limita o caminho quando o Sol está quase no horizonte.
        const float casterHeight = std::max(0.0f, maxShadowCasterY - minimumReceiverY);
        const float minVerticalRay = std::sin(glm::radians(1.0f));
        const float rayDistance = std::min(
            std::max(0.0f, maxShadowDistance),
            casterHeight / std::max(std::abs(lightDirection.y), minVerticalRay));
        const glm::vec3 upstreamOffset = -lightDirection * rayDistance;
        frustumCenter += upstreamOffset * 0.5f;

        // A square footprint avoids resolution changes when the camera rotates.
        float halfExtent = 0.0f;
        for (const glm::vec3& corner : frustumCorners)
        {
            halfExtent = std::max(halfExtent, glm::length(corner - frustumCenter));
            halfExtent = std::max(
                halfExtent, glm::length(corner + upstreamOffset - frustumCenter));
        }
        halfExtent += kShadowPadding;

        const float lightDistance = std::max(100.0f, halfExtent * 2.0f + 60.0f);

        // A orientação da luz precisa ficar independente da posição do
        // frustum. Aplicar o snap numa view já centralizada transforma o XY
        // do centro em zero e deixa a matriz seguir continuamente a câmera.
        const glm::mat4 lightOrientation = glm::lookAt(
            glm::vec3(0.0f), lightDirection, lightUpReference);
        const float worldUnitsPerTexel = (halfExtent * 2.0f) /
            static_cast<float>(std::max(1u, shadowMapSize));
        glm::vec4 centerInLightSpace = lightOrientation * glm::vec4(frustumCenter, 1.0f);
        centerInLightSpace.x = std::round(centerInLightSpace.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        centerInLightSpace.y = std::round(centerInLightSpace.y / worldUnitsPerTexel) * worldUnitsPerTexel;
        const glm::vec3 snappedCenter = glm::vec3(
            glm::inverse(lightOrientation) * centerInLightSpace);
        const glm::vec3 lightPosition = snappedCenter - lightDirection * lightDistance;
        const glm::mat4 lightView = glm::lookAt(lightPosition, snappedCenter, lightUpReference);

        float minimumDepth = std::numeric_limits<float>::max();
        float maximumDepth = std::numeric_limits<float>::lowest();
        for (const glm::vec3& corner : frustumCorners)
        {
            const float lightSpaceZ = (lightView * glm::vec4(corner, 1.0f)).z;
            minimumDepth = std::min(minimumDepth, lightSpaceZ);
            maximumDepth = std::max(maximumDepth, lightSpaceZ);
            const float casterSpaceZ =
                (lightView * glm::vec4(corner + upstreamOffset, 1.0f)).z;
            minimumDepth = std::min(minimumDepth, casterSpaceZ);
            maximumDepth = std::max(maximumDepth, casterSpaceZ);
        }

        // In light view space, visible geometry sits at negative Z. glm::ortho
        // receives positive near/far distances, hence the sign inversion here.
        const float nearPlane = std::max(0.1f, -maximumDepth - kShadowDepthPadding);
        const float farPlane = std::max(nearPlane + 1.0f, -minimumDepth + kShadowDepthPadding);
        if (outDepthNear) *outDepthNear = nearPlane;
        if (outDepthFar) *outDepthFar = farPlane;
        const glm::mat4 lightProjection = glm::ortho(
            -halfExtent,
            halfExtent,
            -halfExtent,
            halfExtent,
            nearPlane,
            farPlane
        );

        return lightProjection * lightView;
    }

    struct FrustumPlanes
    {
        glm::vec4 planes[6];

        void FromMatrix(const glm::mat4& viewProjection)
        {
            // GLM indexes matrices as [column][row]. Clip planes come from
            // rows of the combined matrix (row 3 +/- row 0/1/2). Combining
            // whole columns produced invalid planes and caused meshes and
            // shadow casters to pop as the camera moved.
            const glm::vec4 raw[6] = {
                glm::vec4(viewProjection[0][3] + viewProjection[0][0], viewProjection[1][3] + viewProjection[1][0], viewProjection[2][3] + viewProjection[2][0], viewProjection[3][3] + viewProjection[3][0]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][0], viewProjection[1][3] - viewProjection[1][0], viewProjection[2][3] - viewProjection[2][0], viewProjection[3][3] - viewProjection[3][0]),
                glm::vec4(viewProjection[0][3] + viewProjection[0][1], viewProjection[1][3] + viewProjection[1][1], viewProjection[2][3] + viewProjection[2][1], viewProjection[3][3] + viewProjection[3][1]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][1], viewProjection[1][3] - viewProjection[1][1], viewProjection[2][3] - viewProjection[2][1], viewProjection[3][3] - viewProjection[3][1]),
                glm::vec4(viewProjection[0][3] + viewProjection[0][2], viewProjection[1][3] + viewProjection[1][2], viewProjection[2][3] + viewProjection[2][2], viewProjection[3][3] + viewProjection[3][2]),
                glm::vec4(viewProjection[0][3] - viewProjection[0][2], viewProjection[1][3] - viewProjection[1][2], viewProjection[2][3] - viewProjection[2][2], viewProjection[3][3] - viewProjection[3][2])
            };
            for (int i = 0; i < 6; ++i)
            {
                const float length = glm::length(glm::vec3(raw[i]));
                if (length > 0.0001f)
                    planes[i] = raw[i] / length;
                else
                    planes[i] = raw[i];
            }
        }

        bool IsSphereInside(const glm::vec3& center, float radius) const
        {
            for (int i = 0; i < 6; ++i)
            {
                if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius)
                {
                    return false;
                }
            }
            return true;
        }
    };

    bool IsSphereInsideFrustum(
        const glm::mat4& viewProjection,
        const glm::vec3& center,
        float radius)
    {
        FrustumPlanes fp;
        fp.FromMatrix(viewProjection);
        return fp.IsSphereInside(center, radius);
    }

    std::array<glm::mat4, 6> BuildPointShadowMatrices(
        const glm::vec3& position,
        float farPlane)
    {
        const glm::mat4 projection = glm::perspective(
            glm::radians(90.0f), 1.0f, 0.05f, std::max(farPlane, 0.1f));
        const glm::vec3 directions[6] = {
            glm::vec3( 1.0f,  0.0f,  0.0f),
            glm::vec3(-1.0f,  0.0f,  0.0f),
            glm::vec3( 0.0f,  1.0f,  0.0f),
            glm::vec3( 0.0f, -1.0f,  0.0f),
            glm::vec3( 0.0f,  0.0f,  1.0f),
            glm::vec3( 0.0f,  0.0f,-1.0f)
        };
        const glm::vec3 ups[6] = {
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f,  0.0f, 1.0f),
            glm::vec3(0.0f,  0.0f,-1.0f),
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, -1.0f, 0.0f)
        };

        std::array<glm::mat4, 6> matrices{};
        for (int face = 0; face < 6; ++face)
        {
            matrices[face] = projection * glm::lookAt(
                position,
                position + directions[face],
                ups[face]
            );
        }
        return matrices;
    }
}



int SceneRenderer::RunFrustumCullingRegressionTests()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        std::cout << (condition ? "[FrustumTest] PASS: " : "[FrustumTest] FAIL: ")
                  << name << '\n';
        if (!condition) ++failures;
    };

    const glm::mat4 projection = glm::perspective(glm::radians(65.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::mat4 initialView = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                               glm::vec3(0.0f, 1.0f, 0.0f));
    check(IsSphereInsideFrustum(projection * initialView, glm::vec3(0.0f, 0.0f, -5.0f), 0.5f),
          "objeto à frente da câmera permanece dentro do frustum");
    check(!IsSphereInsideFrustum(projection * initialView, glm::vec3(0.0f, 0.0f, 5.0f), 0.5f),
          "objeto atrás da câmera é descartado");
    check(!IsSphereInsideFrustum(projection * initialView, glm::vec3(0.0f, 0.0f, -600.0f), 1.0f),
          "objeto além do plano distante é descartado");
    check(IsSphereInsideFrustum(projection * initialView, glm::vec3(0.0f, 0.0f, -0.05f), 0.1f),
          "mesh que cruza o plano próximo não desaparece por completo");

    const glm::vec3 movedCamera(25.0f, 3.0f, -7.0f);
    const glm::mat4 movedView = glm::lookAt(movedCamera, movedCamera + glm::vec3(1.0f, 0.0f, 0.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    check(IsSphereInsideFrustum(projection * movedView, movedCamera + glm::vec3(8.0f, 0.0f, 0.0f), 0.5f),
          "frustum continua correto após mover e girar a câmera");
    check(!IsSphereInsideFrustum(projection * movedView, movedCamera + glm::vec3(-8.0f, 0.0f, 0.0f), 0.5f),
          "frustum rotacionado descarta o que ficou atrás da câmera");

    std::cout << "[FrustumTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))\n";
    return failures;
}

SceneRenderer::SceneRenderer()
{
    m_GridRenderer.Create();
    m_InstancedRenderer.SetMaxInstancesPerMesh(1000);
    std::cout << "[SceneRenderer] Instanced rendering initialized (max 1000 instances per mesh)" << std::endl;
    constexpr float pi = 3.14159265358979323846f;
    const auto makeShape = [&](int shape)
    {
        const int sides = shape == 2 ? 3 : (shape == 4 ? 5 : (shape == 5 ? 10 : (shape == 0 ? 28 : 4)));
        const bool star = shape == 5;
        const float radius = shape == 1 ? 0.70710678f : 0.5f;
        const float startAngle = shape == 1 ? pi * 0.25f : pi * 0.5f;
        std::vector<Vertex> vertices;
        vertices.reserve(static_cast<size_t>(sides) * 3);
        for (int i = 0; i < sides; ++i)
        {
            const auto point = [&](int index)
            {
                const float angle = startAngle + glm::two_pi<float>() *
                    static_cast<float>(index) / static_cast<float>(sides);
                const float pointRadius = radius * (star && (index % 2 == 1) ? 0.42f : 1.0f);
                return glm::vec2(std::cos(angle), std::sin(angle)) * pointRadius;
            };
            const glm::vec2 p0 = point(i);
            const glm::vec2 p1 = point(i + 1);
            vertices.emplace_back(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f);
            vertices.emplace_back(p0.x, p0.y, 0.0f, 0.0f, 0.0f, 1.0f,
                                  p0.x + 0.5f, p0.y + 0.5f);
            vertices.emplace_back(p1.x, p1.y, 0.0f, 0.0f, 0.0f, 1.0f,
                                  p1.x + 0.5f, p1.y + 0.5f);
        }
        m_ParticleShapeMeshes[static_cast<size_t>(shape)].Create(
            vertices, GL_TRIANGLES, "SceneRenderer::ParticleShape");
    };
    for (int shape = 0; shape < static_cast<int>(m_ParticleShapeMeshes.size()); ++shape)
        makeShape(shape);
}

SceneRenderer::~SceneRenderer()
{
    glDeleteFramebuffers(Shader::MAX_SHADOW_CASCADES, m_ShadowFBO);
    glDeleteTextures(Shader::MAX_SHADOW_CASCADES, m_ShadowMap);
    glDeleteTextures(Shader::MAX_SHADOW_CASCADES, m_ShadowOpacityMap);
    for (int i = 0; i < Shader::MAX_SHADOW_CASCADES; ++i)
    {
        m_ShadowFBO[i] = 0;
        m_ShadowMap[i] = 0;
        m_ShadowOpacityMap[i] = 0;
    }
    if (m_PointShadowFBO != 0)
    {
        glDeleteFramebuffers(1, &m_PointShadowFBO);
        m_PointShadowFBO = 0;
    }
    glDeleteTextures(Shader::MAX_LOCAL_SHADOWS, m_PointShadowMaps);
    for (GLuint& map : m_PointShadowMaps)
        map = 0;
    glDeleteTextures(Shader::MAX_LOCAL_SHADOWS, m_PointShadowOpacityMaps);
    for (GLuint& map : m_PointShadowOpacityMaps)
        map = 0;
    if (m_GpuTimerQuery != 0)
    {
        glDeleteQueries(1, &m_GpuTimerQuery);
        m_GpuTimerQuery = 0;
    }
    m_GridRenderer.Destroy();
    DestroyReflectionFBO();
}

// ---------------------------------------------------------------------------
// Reflexo Planar — FBO helpers
// ---------------------------------------------------------------------------
void SceneRenderer::InitReflectionFBO(int resolution)
{
    if (m_ReflectionRes == resolution && m_ReflectionFBO != 0) return;
    DestroyReflectionFBO();

    m_ReflectionRes = resolution;

    glGenFramebuffers(1, &m_ReflectionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);

    glGenTextures(1, &m_ReflectionTexture);
    glBindTexture(GL_TEXTURE_2D, m_ReflectionTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, resolution, resolution, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_ReflectionTexture, 0);

    glGenRenderbuffers(1, &m_ReflectionDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_ReflectionDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          resolution, resolution);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_ReflectionDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRenderer::DestroyReflectionFBO()
{
    if (m_ReflectionFBO != 0)      { glDeleteFramebuffers(1, &m_ReflectionFBO); m_ReflectionFBO = 0; }
    if (m_ReflectionTexture != 0)  { glDeleteTextures(1, &m_ReflectionTexture); m_ReflectionTexture = 0; }
    if (m_ReflectionDepthRBO != 0) { glDeleteRenderbuffers(1, &m_ReflectionDepthRBO); m_ReflectionDepthRBO = 0; }
    m_ReflectionRes = 0;
}

unsigned int SceneRenderer::GetReflectionTextureID() const
{
    return m_ReflectionTexture;
}

// ---------------------------------------------------------------------------
// RenderReflection — renderiza a cena do ponto de vista refletido
// ---------------------------------------------------------------------------
void SceneRenderer::RenderReflection(
    Scene& scene,
    Shader& shader,
    LightingManager& lighting,
    const glm::mat4& mirrorModelMatrix,
    const glm::vec3& mirrorNormalWorld,
    const glm::mat4& cameraView,
    const glm::mat4& projection,
    int  reflectionResolution,
    unsigned int whiteTextureID,
    uint64_t mirrorEntityID)
{
    if (reflectionResolution <= 0) return;
    InitReflectionFBO(reflectionResolution);
    if (m_ReflectionFBO == 0) return;

    // Salva FBO e Viewport atuais para restaurá-los com perfeição
    GLint savedFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
    GLint savedViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, savedViewport);

    m_IsRenderingReflection = true;
    m_ActiveMirrorEntityID = mirrorEntityID;

    // ---- Calcula a câmera refletida ----------------------------------------
    glm::vec3 mirrorPos = glm::vec3(mirrorModelMatrix[3]);
    glm::vec3 N = glm::normalize(mirrorNormalWorld);

    glm::mat4 invView = glm::inverse(cameraView);
    glm::vec3 camPos  = glm::vec3(invView[3]);
    glm::vec3 camFwd  = -glm::vec3(invView[2]);

    glm::vec3 camToMirror = camPos - mirrorPos;
    glm::vec3 reflPos = camPos - 2.0f * glm::dot(camToMirror, N) * N;
    glm::vec3 reflFwd = camFwd - 2.0f * glm::dot(camFwd, N) * N;

    glm::vec3 camUp  = glm::vec3(invView[1]);
    glm::vec3 reflUp = camUp - 2.0f * glm::dot(camUp, N) * N;

    glm::mat4 reflView = glm::lookAt(reflPos, reflPos + reflFwd, reflUp);

    // ---- Render no FBO de reflexo -----------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);
    glViewport(0, 0, reflectionResolution, reflectionResolution);
    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Render(scene, shader, lighting,
           /*selectedEntity=*/-1, /*multiSelected=*/{},
           whiteTextureID,
           reflView, projection,
           /*showAxis=*/false, /*showGrid=*/false,
           /*gridRenderDistance=*/500.0f, /*editor2D=*/false);

    // Restaura o framebuffer e viewport anteriores para que a cena continue renderizando no targetFBO!
    glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
    glFrontFace(GL_CCW);

    m_IsRenderingReflection = false;
    m_ActiveMirrorEntityID = 0;
}

void SceneRenderer::SetQuality(GraphicsQuality quality)
{
    m_DirectionalShadowCacheValid = false;
    m_LocalShadowCacheValid = false;
    switch (quality)
    {
        case GraphicsQuality::Performance:
            // Mesmo no preset leve, sombras locais de Point/Spot continuam
            // ativas em baixa resolução. Desligá-las aqui fazia a luz
            // artificial iluminar sem nunca projetar sombra.
            m_UseShadows = true;
            m_RequestedShadowMapSize = 1024;
            m_RequestedPointShadowMapSize = 512;
            m_ActiveCascadeCount = 1;
            break;
        case GraphicsQuality::UltraPerformance:
            // Preset de compatibilidade máxima: sem sombras dinâmicas, nuvens
            // nem sombras locais; cena continua usando iluminação direta simples.
            m_UseShadows = false;
            m_RequestedShadowMapSize = 512;
            m_RequestedPointShadowMapSize = 256;
            m_ActiveCascadeCount = 1;
            break;
        case GraphicsQuality::Balanced:
            m_UseShadows = true;
            // Três cascatas 2K mantêm boa definição perto da câmera sem
            // multiplicar o custo de quatro mapas de alta resolução.
            m_RequestedShadowMapSize = 2048;
            m_RequestedPointShadowMapSize = 512;
            m_ActiveCascadeCount = 3;
            break;
        case GraphicsQuality::HighFidelity:
            m_UseShadows = true;
            // Ultra usa quatro cascatas 3K: a primeira fica nítida no entorno
            // do objeto e as demais cobrem o horizonte.
            m_RequestedShadowMapSize = 3072;
            m_RequestedPointShadowMapSize = 1024;
            m_ActiveCascadeCount = Shader::MAX_SHADOW_CASCADES;
            break;
        case GraphicsQuality::Ultra:
            m_UseShadows = true;
            m_RequestedShadowMapSize = 4096;
            m_RequestedPointShadowMapSize = 2048;
            m_ActiveCascadeCount = Shader::MAX_SHADOW_CASCADES;
            break;
    }
}

void SceneRenderer::InitPointShadowMaps(int requiredMapCount)
{
    requiredMapCount = std::clamp(requiredMapCount, 1, Shader::MAX_LOCAL_SHADOWS);
    if (m_PointShadowFBO != 0 &&
        m_PointShadowMapSize == m_EffectivePointShadowMapSize &&
        m_PointShadowMapCount >= requiredMapCount)
    {
        return;
    }

    if (m_PointShadowFBO != 0)
    {
        glDeleteFramebuffers(1, &m_PointShadowFBO);
        m_PointShadowFBO = 0;
    }
    glDeleteTextures(Shader::MAX_LOCAL_SHADOWS, m_PointShadowMaps);
    for (GLuint& map : m_PointShadowMaps)
        map = 0;
    glDeleteTextures(Shader::MAX_LOCAL_SHADOWS, m_PointShadowOpacityMaps);
    for (GLuint& map : m_PointShadowOpacityMaps)
        map = 0;
    m_PointShadowMapCount = 0;
    m_LocalShadowCacheValid = false;

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_PointShadowMapSize = std::min(
        m_EffectivePointShadowMapSize,
        static_cast<unsigned int>(std::max(256, maxTextureSize)));

    glGenFramebuffers(1, &m_PointShadowFBO);
    glGenTextures(requiredMapCount, m_PointShadowMaps);
    glGenTextures(requiredMapCount, m_PointShadowOpacityMaps);
    for (int mapIndex = 0; mapIndex < requiredMapCount; ++mapIndex)
    {
        const GLuint map = m_PointShadowMaps[mapIndex];
        glBindTexture(GL_TEXTURE_CUBE_MAP, map);
        for (int face = 0; face < 6; ++face)
        {
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_DEPTH_COMPONENT32F,
                static_cast<GLsizei>(m_PointShadowMapSize),
                static_cast<GLsizei>(m_PointShadowMapSize),
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                nullptr
            );
        }
        // A filtragem é feita manualmente no shader; interpolar a profundidade
        // aqui pode clarear demais a borda e fazer a luz "vazar".
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        const GLuint opacityMap = m_PointShadowOpacityMaps[mapIndex];
        glBindTexture(GL_TEXTURE_CUBE_MAP, opacityMap);
        for (int face = 0; face < 6; ++face)
        {
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_R16F,
                static_cast<GLsizei>(m_PointShadowMapSize),
                static_cast<GLsizei>(m_PointShadowMapSize),
                0,
                GL_RED,
                GL_HALF_FLOAT,
                nullptr
            );
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_PointShadowFBO);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        m_PointShadowMaps[0],
        0
    );
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        m_PointShadowOpacityMaps[0],
        0
    );
    const GLenum pointDrawBuffers[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, pointDrawBuffers);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[SceneRenderer] Point shadow framebuffer is incomplete." << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_PointShadowMapCount = requiredMapCount;
}

void SceneRenderer::InitShadowMap()
{
    if (m_ShadowFBO[0] != 0 && m_ShadowMapSize == m_EffectiveShadowMapSize)
        return;

    glDeleteFramebuffers(Shader::MAX_SHADOW_CASCADES, m_ShadowFBO);
    glDeleteTextures(Shader::MAX_SHADOW_CASCADES, m_ShadowMap);
    glDeleteTextures(Shader::MAX_SHADOW_CASCADES, m_ShadowOpacityMap);
    for (int i = 0; i < Shader::MAX_SHADOW_CASCADES; ++i)
    {
        m_ShadowFBO[i] = 0;
        m_ShadowMap[i] = 0;
        m_ShadowOpacityMap[i] = 0;
    }
    GLint maxTextureSize = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_ShadowMapSize = std::min(
        m_EffectiveShadowMapSize,
        static_cast<unsigned int>(std::max(1024, maxTextureSize))
    );
    m_DirectionalShadowCacheValid = false;

    glGenFramebuffers(Shader::MAX_SHADOW_CASCADES, m_ShadowFBO);
    glGenTextures(Shader::MAX_SHADOW_CASCADES, m_ShadowMap);
    // A textura de opacidade é uma 2D-array: uma única unidade de sampler
    // atende todas as cascatas e evita estourar o limite mínimo do OpenGL 3.3.
    glGenTextures(1, &m_ShadowOpacityMap[0]);
    const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const GLenum shadowDrawBuffers[] = { GL_COLOR_ATTACHMENT0 };
    for (int cascade = 0; cascade < Shader::MAX_SHADOW_CASCADES; ++cascade)
    {
        glBindTexture(GL_TEXTURE_2D, m_ShadowMap[cascade]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
            static_cast<GLsizei>(m_ShadowMapSize), static_cast<GLsizei>(m_ShadowMapSize),
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO[cascade]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D, m_ShadowMap[cascade], 0);
        glDrawBuffers(1, shadowDrawBuffers);
        glReadBuffer(GL_NONE);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_ShadowOpacityMap[0]);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_R32F,
        static_cast<GLsizei>(m_ShadowMapSize),
        static_cast<GLsizei>(m_ShadowMapSize),
        Shader::MAX_SHADOW_CASCADES,
        0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);

    for (int cascade = 0; cascade < Shader::MAX_SHADOW_CASCADES; ++cascade)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO[cascade]);
        glFramebufferTextureLayer(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            m_ShadowOpacityMap[0], 0, cascade);
        glDrawBuffers(1, shadowDrawBuffers);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "[SceneRenderer] Shadow cascade framebuffer is incomplete." << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRenderer::Render(
    Scene& scene,
    Shader& shader,
    LightingManager& lighting,
    int selectedEntity,
    const std::vector<int>& multiSelectedEntities,
    GLuint whiteTextureID,
    const glm::mat4& view,
    const glm::mat4& projection,
    bool showAxis,
    bool showGrid,
    float gridRenderDistance,
    bool editor2DMode)
{
    const auto renderStart = std::chrono::steady_clock::now();
    m_LastStats = RenderStats{};

    // O editor pode alterar Position/Rotation/Scale diretamente pelo
    // Inspector ou pelo gizmo. Garanta que o passe de renderização nunca use
    // uma WorldMatrix de um frame anterior.
    scene.UpdateTransforms();
    if (m_GpuTimerQuery == 0)
        glGenQueries(1, &m_GpuTimerQuery);
    if (m_GpuTimerQuery != 0)
    {
        if (m_GpuQueryInFlight)
        {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(m_GpuTimerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE)
            {
                GLuint64 elapsedNanoseconds = 0;
                glGetQueryObjectui64v(
                    m_GpuTimerQuery, GL_QUERY_RESULT, &elapsedNanoseconds);
                m_LastStats.GpuTimeMs = static_cast<double>(elapsedNanoseconds) / 1000000.0;
                m_LastStats.GpuTimeAvailable = true;
                m_GpuQueryInFlight = false;
            }
        }
        if (!m_GpuQueryInFlight)
        {
            glBeginQuery(GL_TIME_ELAPSED, m_GpuTimerQuery);
            m_GpuQueryInFlight = true;
        }
    }
    const auto& entities = scene.GetEntities();
    std::unordered_map<uint64_t, size_t> entityIndexByID;
    entityIndexByID.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i)
        entityIndexByID.emplace(entities[i].GetID(), i);
    const size_t noEntityIndex = std::numeric_limits<size_t>::max();
    m_EntityFirstChildScratch.resize(entities.size(), noEntityIndex);
    m_EntityNextSiblingScratch.resize(entities.size(), noEntityIndex);
    std::fill(m_EntityFirstChildScratch.begin(), m_EntityFirstChildScratch.end(), noEntityIndex);
    std::fill(m_EntityNextSiblingScratch.begin(), m_EntityNextSiblingScratch.end(), noEntityIndex);
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const uint64_t parentID = entities[i].GetParentID();
        const auto parentIt = entityIndexByID.find(parentID);
        if (parentID != 0 && parentIt != entityIndexByID.end())
        {
            m_EntityNextSiblingScratch[i] = m_EntityFirstChildScratch[parentIt->second];
            m_EntityFirstChildScratch[parentIt->second] = i;
        }
    }

    // Resolve hierarchy activity once. The previous light scan walked the
    // full entity array for every ancestor of every light (O(N²) in large
    // scenes), and shadow passes did not consistently respect inactive parents.
    std::vector<unsigned char> activeHierarchyState(entities.size(), 0);
    std::vector<size_t> hierarchyChain;
    hierarchyChain.reserve(32);
    for (size_t start = 0; start < entities.size(); ++start)
    {
        if (activeHierarchyState[start] == 1 || activeHierarchyState[start] == 2)
            continue;
        hierarchyChain.clear();
        size_t current = start;
        bool parentActive = false;
        while (true)
        {
            const unsigned char state = activeHierarchyState[current];
            if (state == 1)
            {
                parentActive = true;
                break;
            }
            if (state == 2)
            {
                parentActive = false;
                break;
            }
            if (state == 3)
            {
                // A malformed parent cycle is treated as inactive instead of
                // looping forever during rendering.
                parentActive = false;
                break;
            }
            if (!entities[current].Active)
            {
                activeHierarchyState[current] = 2;
                parentActive = false;
                break;
            }

            activeHierarchyState[current] = 3;
            hierarchyChain.push_back(current);
            const uint64_t parentID = entities[current].GetParentID();
            if (parentID == 0)
            {
                parentActive = true;
                break;
            }
            const auto parentIt = entityIndexByID.find(parentID);
            if (parentIt == entityIndexByID.end())
            {
                parentActive = true;
                break;
            }
            current = parentIt->second;
        }
        while (!hierarchyChain.empty())
        {
            const size_t index = hierarchyChain.back();
            hierarchyChain.pop_back();
            const bool active = entities[index].Active && parentActive;
            activeHierarchyState[index] = active ? 1 : 2;
            parentActive = active;
        }
    }
    auto isEntityActiveInHierarchy = [&](const Entity& e) -> bool
    {
        const auto it = entityIndexByID.find(e.GetID());
        return it != entityIndexByID.end() && activeHierarchyState[it->second] == 1;
    };
    auto entityShadowOpacity = [&](Entity& entity) -> float
    {
        float opacity = std::clamp(entity.Opacity, 0.0f, 1.0f);
        MaterialComponent& component = entity.GetMaterialComponent();
        Material* shadowMaterial = nullptr;
        if (!component.MaterialGUID.empty())
            shadowMaterial = MaterialDatabase::GetMaterialByGUID(component.MaterialGUID);
        if (!shadowMaterial)
            shadowMaterial = &component.MaterialData;
        if (shadowMaterial->PBRParametersEnabled)
            opacity *= std::clamp(shadowMaterial->Alpha, 0.0f, 1.0f);
        return opacity;
    };
    LightingSettings lightingSettings = lighting.Settings;
    lightingSettings.Clamp();
    const bool advancedLightingEnabled = !editor2DMode && LightingSystem::IsEnabled(lightingSettings);
    const bool useShadows = !editor2DMode && ShadowSystem::ShouldRender(lightingSettings, m_UseShadows);
    // Sombras de Point/Spot/Area são uma propriedade explícita da luz. Elas
    // não devem desaparecer só porque o pacote opcional de iluminação global
    // (sol, PBR e ciclo dia/noite) está desligado. O toggle global de sombras
    // do renderer continua sendo respeitado, e DynamicShadows controla as
    // sombras quando o sistema avançado está ativo.
    const bool localShadowsEnabled = !editor2DMode && m_UseShadows &&
        (!advancedLightingEnabled || lightingSettings.DynamicShadows);
    m_EffectiveShadowMapSize = ShadowSystem::ResolveDirectionalResolution(
        lightingSettings, m_RequestedShadowMapSize);
    m_EffectivePointShadowMapSize = ShadowSystem::ResolvePointResolution(
        lightingSettings, m_RequestedPointShadowMapSize);

    // Uma única construção de transform evita que o mesh visível e o caster
    // do shadow map usem WorldMatrix e TRS local diferentes. Isso é importante
    // para entidades raiz quando o Inspector/gizmo acabou de alterar o TRS.
    auto buildEntityModel = [&](const Entity& entity)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        glm::mat4 model = m_TransformRenderer.GetModelMatrix(transform);
        if (entity.GetParentID() == 0)
        {
            model = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
            model *= glm::mat4_cast(transform.Rotation);
            model = glm::scale(
                model,
                glm::vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z));
        }

        // Uma entidade de personagem pode conter simultaneamente o mesh e a
        // câmera. Nesse caso o pitch aplicado à câmera não deve inclinar o
        // corpo visual: a câmera usa a rotação completa do Transform, mas o
        // mesh deve acompanhar apenas o yaw (eixo Y). Entidades com câmera sem
        // mesh, ou meshes sem câmera, seguem o comportamento normal.
        if (entity.HasCamera() && const_cast<Entity&>(entity).GetMeshComponent().MeshData)
        {
            const glm::vec3 worldPosition = glm::vec3(model[3]);
            const glm::vec3 worldX = glm::vec3(model[0]);
            const glm::vec3 worldY = glm::vec3(model[1]);
            const glm::vec3 worldZ = glm::vec3(model[2]);
            const glm::vec3 forward = glm::length(worldZ) > 0.0001f
                ? glm::normalize(worldZ)
                : glm::vec3(0.0f, 0.0f, 1.0f);
            const float yaw = std::atan2(forward.x, forward.z);
            const glm::vec3 scale(
                std::max(glm::length(worldX), 0.0001f),
                std::max(glm::length(worldY), 0.0001f),
                std::max(glm::length(worldZ), 0.0001f));
            model = glm::translate(glm::mat4(1.0f), worldPosition);
            model *= glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, scale);
        }
        return model;
    };

    m_EntityModelsScratch.resize(entities.size());
    m_EntityShadowOpacityScratch.resize(entities.size(), 1.0f);
    for (size_t entIdx = 0; entIdx < entities.size(); ++entIdx)
    {
        m_EntityModelsScratch[entIdx] = buildEntityModel(entities[entIdx]);
        if (useShadows || localShadowsEnabled)
            m_EntityShadowOpacityScratch[entIdx] = entityShadowOpacity(
                const_cast<Entity&>(entities[entIdx]));
    }

    // Os passes de sombra usam culling de faces para reduzir o custo, mas o
    // viewport precisa mostrar o mesh de qualquer lado da câmera. Alguns
    // meshes antigos têm winding misto; por isso garantimos o estado aqui e
    // desligamos o culling somente antes do passe visual principal.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Os shadow passes desenham em FBOs próprios. Normalize estados globais
    // antes deles: rasterizer discard pode eliminar todos os fragments mesmo
    // com FBO completo e draw calls; scissor/stencil herdados também podem
    // restringir os mapas ao estado do viewport anterior.
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    // -------------------------------------------------------------
    // CALCULATE A STABLE, CAMERA-FITTED DIRECTIONAL SHADOW MATRIX.
    // -------------------------------------------------------------
    glm::vec3 lightDirection(
        lighting.Sun.Direction.X,
        lighting.Sun.Direction.Y,
        lighting.Sun.Direction.Z
    );
    glm::vec3 directionalColor(
        lighting.Sun.Color.X,
        lighting.Sun.Color.Y,
        lighting.Sun.Color.Z
    );
    float directionalIntensity = std::max(0.0f, lighting.Sun.Intensity);
    if (lighting.Sun.UseTemperature)
        directionalColor *= LightingSystem::ColorTemperatureToRGB(lighting.Sun.TemperatureKelvin);

    if (editor2DMode)
    {
        directionalIntensity = 0.0f;
        directionalColor = glm::vec3(0.0f);
    }

    // Uma LightComponent Directional pode substituir o sol global sem criar
    // um segundo caminho de renderização. O primeiro componente ativo é a
    // fonte direcional da cena, mantendo compatibilidade com LightingManager::Sun.
    for (const Entity& sourceEntity : entities)
    {
        if (!advancedLightingEnabled)
            break;
        Entity& source = const_cast<Entity&>(sourceEntity);
        if (!source.Active || !source.HasLight())
            continue;
        const LightComponent& component = source.GetLightComponent();
        if (!component.Enabled || component.Type != LightType::Directional)
            continue;
        const glm::mat4 model = m_TransformRenderer.GetModelMatrix(source.GetTransform());
        glm::vec3 localDirection(
            component.DirectionLocal[0], component.DirectionLocal[1], component.DirectionLocal[2]);
        if (glm::length(localDirection) < 0.0001f)
            localDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        lightDirection = glm::normalize(glm::vec3(model * glm::vec4(localDirection, 0.0f)));
        directionalColor = glm::vec3(component.Color[0], component.Color[1], component.Color[2]);
        if (component.UseTemperature)
            directionalColor *= LightingSystem::ColorTemperatureToRGB(component.TemperatureKelvin);
        directionalIntensity = std::max(0.0f, component.Intensity);
        break;
    }
    if (advancedLightingEnabled && lightingSettings.DayNightCycleEnabled)
    {
        const DayNightSample solar = LightingSystem::EvaluateDayNightCycle(lightingSettings);
        if (solar.Daylight > 0.08f)
        {
            lightDirection = solar.RayDirection;
            directionalColor = solar.Color;
            directionalIntensity = solar.Intensity;
        }
        else
        {
            // À noite a lua se torna a fonte direcional efetiva. Assim o
            // mesmo shadow map/cascatas continuam funcionando sem duplicar
            // o renderer de sombras.
            lightDirection = -solar.RayDirection;
            directionalColor = glm::vec3(
                lighting.Moon.Color.X, lighting.Moon.Color.Y, lighting.Moon.Color.Z);
            if (lighting.Moon.UseTemperature)
                directionalColor *= LightingSystem::ColorTemperatureToRGB(lighting.Moon.TemperatureKelvin);
            directionalIntensity = std::max(0.0f, lighting.Moon.Intensity) * solar.Night * 0.12f;
        }
    }

    if (!useShadows)
    {
        // Ao reativar a iluminação, reconstrói cada cache uma vez em vez de
        // reutilizar sombras que possam ter sido invalidadas durante a pausa.
        m_DirectionalShadowCacheValid = false;
        if (!localShadowsEnabled)
            m_LocalShadowCacheValid = false;
    }
    const int qualityCascadeCap[] = { 1, 2, 3, 4 };
    const int qualityIndex = std::clamp(static_cast<int>(lightingSettings.Quality), 0, 3);
    const int activeCascadeCount = std::clamp(
        useShadows ? std::min(m_ActiveCascadeCount, qualityCascadeCap[qualityIndex]) : 1,
        1,
        Shader::MAX_SHADOW_CASCADES);
    const float shadowDistance = ShadowSystem::ResolveShadowDistance(
        lightingSettings, 220.0f);
    const std::array<float, Shader::MAX_SHADOW_CASCADES> cascadeFarPlanes =
        ShadowSystem::ResolveCascadeFarPlanes(shadowDistance, activeCascadeCount);
    std::array<float, Shader::MAX_SHADOW_CASCADES> cascadeNearPlanes{};
    cascadeNearPlanes[0] = 0.1f;
    for (int cascade = 1; cascade < Shader::MAX_SHADOW_CASCADES; ++cascade)
        cascadeNearPlanes[static_cast<size_t>(cascade)] =
            cascadeFarPlanes[static_cast<size_t>(cascade - 1)];
    std::array<glm::mat4, Shader::MAX_SHADOW_CASCADES> cascadeMatrices{};
    for (glm::mat4& matrix : cascadeMatrices)
        matrix = glm::mat4(1.0f);
    std::array<float, Shader::MAX_SHADOW_CASCADES> cascadeDepthRanges{};

    std::size_t directionalShadowSignature = 1469598103934665603ull;
    auto mixDirectional = [&](std::size_t value)
    {
        directionalShadowSignature ^= value + 0x9e3779b97f4a7c15ull +
            (directionalShadowSignature << 6) + (directionalShadowSignature >> 2);
    };
    auto mixDirectionalFloat = [&](float value)
    {
        mixDirectional(std::hash<float>{}(value));
    };
    float maxShadowCasterY = -std::numeric_limits<float>::max();
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            mixDirectionalFloat(view[column][row]);
            mixDirectionalFloat(projection[column][row]);
        }
    }
    mixDirectionalFloat(lightDirection.x);
    mixDirectionalFloat(lightDirection.y);
    mixDirectionalFloat(lightDirection.z);
    mixDirectionalFloat(shadowDistance);
    mixDirectional(m_EffectiveShadowMapSize);
    mixDirectional(static_cast<std::size_t>(activeCascadeCount));
    for (int cascade = 0; cascade < activeCascadeCount; ++cascade)
        mixDirectionalFloat(cascadeFarPlanes[static_cast<size_t>(cascade)]);
    if (useShadows)
    {
        for (const Entity& entity : entities)
        {
            const std::string& entityName = entity.GetName();
            if (entity.IsEditorOnly() || entity.HasUI() ||
                !isEntityActiveInHierarchy(entity) || entityName == "Grid" ||
                entityName == "Axis" || entityName == "Axis X" ||
                entityName == "Axis Y" || entityName == "Axis Z")
                continue;
            const size_t entityIdx = static_cast<size_t>(&entity - entities.data());
            const float shadowOpacity = m_EntityShadowOpacityScratch[entityIdx];
            if (shadowOpacity <= 0.001f)
                continue;
            mixDirectional(static_cast<std::size_t>(entity.GetID()));
            mixDirectional(entity.CastShadows ? 1u : 0u);
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            mixDirectional(std::hash<const void*>{}(mesh.get()));
            mixDirectional(mesh ? static_cast<std::size_t>(mesh->GetRevision()) : 0);
            const glm::mat4& worldModel = (entityIdx < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[entityIdx] : buildEntityModel(entity);
            if (mesh && mesh->IsValid())
            {
                const glm::vec3 meshCenter = glm::vec3(
                    worldModel * glm::vec4(mesh->GetBoundingCenter(), 1.0f));
                const float worldScale = std::max({
                    glm::length(glm::vec3(worldModel[0])),
                    glm::length(glm::vec3(worldModel[1])),
                    glm::length(glm::vec3(worldModel[2])),
                    0.001f });
                maxShadowCasterY = std::max(maxShadowCasterY,
                    meshCenter.y + mesh->GetBoundingRadius() * worldScale);
            }
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    mixDirectionalFloat(worldModel[column][row]);
            mixDirectionalFloat(entity.CastShadows ? shadowOpacity : 0.0f);
        }
    }
    if (!std::isfinite(maxShadowCasterY))
        maxShadowCasterY = 0.0f;
    mixDirectionalFloat(maxShadowCasterY);
    mixDirectionalFloat(lightingSettings.DirectionalSourceAngularDiameterDegrees);
    const bool directionalShadowDirty = useShadows &&
        (!m_DirectionalShadowCacheValid ||
         directionalShadowSignature != m_DirectionalShadowSignature);
    if (!directionalShadowDirty)
    {
        cascadeMatrices = m_CachedCascadeMatrices;
        cascadeDepthRanges = m_CachedCascadeDepthRanges;
    }

    struct RenderPointLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity = 0.0f;
        float Radius = 1.0f;
        float SourceRadius = 0.15f;
        float ShadowSoftness = 0.25f;
        float ShadowStrength = 1.0f;
        bool DistanceSoftening = true;
        bool IndirectContribution = true;
        int ShadowIndex = -1;
    };

    struct RenderSpotLight
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
        glm::vec3 Color{ 1.0f };
        float Intensity = 0.0f;
        float Range = 1.0f;
        float InnerCos = 1.0f;
        float OuterCos = 0.0f;
        float SourceRadius = 0.15f;
        float ShadowSoftness = 0.25f;
        float ShadowStrength = 1.0f;
        bool DistanceSoftening = true;
        bool IndirectContribution = true;
        int ShadowIndex = -1;
    };

    struct LocalShadowSource
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
        float FarPlane = 10.0f;
        float OuterCos = -1.0f;
        uint64_t EmitterEntityID = 0;
        bool IsSpot = false;
    };

    std::array<RenderPointLight, Shader::MAX_POINT_LIGHTS> pointLights{};
    std::array<RenderSpotLight, Shader::MAX_SPOT_LIGHTS> spotLights{};
    std::array<LocalShadowSource, Shader::MAX_LOCAL_SHADOWS> localShadowSources{};
    int pointLightCount = 0;
    int spotLightCount = 0;
    int localShadowCount = 0;
    const int maxPointLights = editor2DMode ? 0 : (advancedLightingEnabled
        ? std::min(lightingSettings.MaxPointLights, Shader::MAX_POINT_LIGHTS)
        : std::min(4, Shader::MAX_POINT_LIGHTS));
    const int maxSpotLights = editor2DMode ? 0 : (advancedLightingEnabled
        ? std::min(lightingSettings.MaxSpotLights, Shader::MAX_SPOT_LIGHTS)
        : std::min(4, Shader::MAX_SPOT_LIGHTS));
    // Cenas criadas antes do limite de sombras locais existir foram salvas
    // com MaxShadowedLocalLights=0. Esse valor não deve fazer a opção
    // "Projetar sombras" de toda luz artificial parecer quebrada: quando a
    // iluminação avançada está ativa, zero significa orçamento automático.
    // O usuário ainda pode desligar todas as sombras pelo DynamicShadows ou
    // pelo toggle geral do renderer.
    // Sombras de Point/Spot são um recurso padrão da luz artificial. Elas
    // não podem depender do modo "Iluminação avançada": isso fazia a luz
    // aparecer, mas descartava todos os shadow maps em projetos leves.
    const int maxShadowedLocalLights = std::clamp(
        lightingSettings.MaxShadowedLocalLights > 0
            ? lightingSettings.MaxShadowedLocalLights
            : 1,
        1,
        Shader::MAX_LOCAL_SHADOWS);
    const glm::mat4 lightCullingViewProjection = projection * view;

    auto allocateLocalShadow = [&](const glm::vec3& position, float range,
                                   uint64_t emitterEntityID = 0,
                                   const glm::vec3* spotDirection = nullptr,
                                   float spotOuterCos = -1.0f) -> int
    {
        if (!localShadowsEnabled || localShadowCount >= maxShadowedLocalLights)
            return -1;
        const int index = localShadowCount++;
        localShadowSources[index].Position = position;
        localShadowSources[index].EmitterEntityID = emitterEntityID;
        localShadowSources[index].IsSpot = spotDirection != nullptr;
        if (spotDirection)
            localShadowSources[index].Direction = glm::normalize(*spotDirection);
        localShadowSources[index].OuterCos = spotOuterCos;
        // O shadow map precisa usar o mesmo alcance da luz. O limite anterior
        // de 1000 m fazia a iluminação continuar, mas a sombra desaparecer
        // antes do fim configurado pelo usuário.
        localShadowSources[index].FarPlane = std::clamp(range, 1.0f, 10000.0f);
        return index;
    };

    // Monta uma lista única para que a iluminação e o shadow map usem
    // exatamente as mesmas posições, alcances e índices.
    for (size_t li = 0;
         li < lighting.PointLights.size() && pointLightCount < maxPointLights;
         ++li)
    {
        const PointLight& source = lighting.PointLights[li];
        RenderPointLight& target = pointLights[pointLightCount];
        target.Position = glm::vec3(source.Position.X, source.Position.Y, source.Position.Z);
        target.Radius = std::clamp(source.Radius, 0.1f, 10000.0f);
        // A fonte pode estar fora da câmera e ainda iluminar receptores
        // visíveis; o shadow map local não deve ser descartado pelo frustum.
        target.Color = glm::vec3(source.Color.X, source.Color.Y, source.Color.Z);
        if (source.UseTemperature)
            target.Color *= LightingSystem::ColorTemperatureToRGB(source.TemperatureKelvin);
        target.Color = glm::max(target.Color, glm::vec3(0.0f));
        target.Intensity = std::max(0.0f, source.Intensity);
        if (target.Intensity <= 0.0001f || glm::dot(target.Color, target.Color) <= 0.000001f)
            continue;
        target.SourceRadius = std::max(0.025f, source.SourceRadius);
        target.ShadowSoftness = std::clamp(source.ShadowSoftness +
            (lightingSettings.ContactShadows
                ? source.ContactShadowStrength * lightingSettings.ContactShadowStrength
                : 0.0f), 0.0f, 1.0f);
        target.DistanceSoftening = true;
        target.ShadowStrength = 1.0f;
        target.ShadowIndex = target.Intensity > 0.0001f && glm::dot(target.Color, target.Color) > 0.000001f
            ? allocateLocalShadow(target.Position, target.Radius) : -1;
        ++pointLightCount;
    }

    for (size_t li = 0;
         li < lighting.SpotLights.size() && spotLightCount < maxSpotLights;
         ++li)
    {
        const SpotLight& source = lighting.SpotLights[li];
        RenderSpotLight& target = spotLights[spotLightCount];
        target.Position = glm::vec3(source.Position.X, source.Position.Y, source.Position.Z);
        target.Range = std::clamp(source.Radius, 0.1f, 10000.0f);
        // O cone pode projetar sombra dentro da câmera mesmo com a origem
        // fora do frustum atual.
        const glm::vec3 sourceDirection(
            source.Direction.X, source.Direction.Y, source.Direction.Z);
        target.Direction = glm::length(sourceDirection) > 0.001f
            ? glm::normalize(sourceDirection)
            : glm::vec3(0.0f, -1.0f, 0.0f);
        target.Color = glm::max(
            glm::vec3(source.Color.X, source.Color.Y, source.Color.Z), glm::vec3(0.0f));
        if (source.UseTemperature)
            target.Color *= LightingSystem::ColorTemperatureToRGB(source.TemperatureKelvin);
        target.Intensity = std::max(0.0f, source.Intensity);
        if (target.Intensity <= 0.0001f || glm::dot(target.Color, target.Color) <= 0.000001f)
            continue;
        target.SourceRadius = std::max(0.025f, source.SourceRadius);
        target.ShadowSoftness = std::clamp(source.ShadowSoftness +
            (lightingSettings.ContactShadows
                ? source.ContactShadowStrength * lightingSettings.ContactShadowStrength
                : 0.0f), 0.0f, 1.0f);
        target.DistanceSoftening = true;
        target.ShadowStrength = 1.0f;
        const float innerHalf = std::clamp(source.Cutoff * 0.5f, 0.5f, 44.5f);
        const float outerHalf = std::clamp(innerHalf + 4.0f, innerHalf + 0.5f, 44.9f);
        target.InnerCos = std::cos(glm::radians(innerHalf));
        target.OuterCos = std::cos(glm::radians(outerHalf));
        target.ShadowIndex = target.Intensity > 0.0001f && glm::dot(target.Color, target.Color) > 0.000001f
            ? allocateLocalShadow(target.Position, target.Range, 0,
                &target.Direction, target.OuterCos) : -1;
        ++spotLightCount;
    }

    for (size_t ei = 0;
         ei < entities.size() && pointLightCount < maxPointLights;
         ++ei)
    {
        Entity& ent = const_cast<Entity&>(entities[ei]);
        if (!isEntityActiveInHierarchy(ent) || !ent.HasLight()) continue;
        const LightComponent& light = ent.GetLightComponent();
        if (!light.Enabled || (light.Type != LightType::Point && light.Type != LightType::Area)) continue;

        const Transform& transform = ent.GetTransform();
        const glm::mat4& model = (ei < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[ei] : buildEntityModel(ent);
        RenderPointLight& target = pointLights[pointLightCount];
        // A luz segue a posicao mundial da entidade, inclusive quando esta
        // ligada a um pai. Usar apenas Transform::Position fazia o shadow map
        // ser renderizado em um lugar e a iluminacao ser aplicada em outro.
        target.Position = glm::vec3(model[3]);
        // Desloca luz de area fina (fixture no teto) para evitar auto-sombra do proprio teto.
        if (light.Type == LightType::Area ||
            (light.Type == LightType::Point && transform.Scale.Y < 0.35f))
            target.Position.y -= 0.08f;
        target.Radius = std::clamp(light.Range, 0.1f, 10000.0f);
        // A posicao da luz nao precisa estar visivel para sua sombra existir.
        target.Color = glm::vec3(light.Color[0], light.Color[1], light.Color[2]);
        if (light.UseTemperature)
            target.Color *= LightingSystem::ColorTemperatureToRGB(light.TemperatureKelvin);
        target.Color = glm::max(target.Color, glm::vec3(0.0f));
        target.Intensity = std::max(0.0f, light.Intensity);
        if (target.Intensity <= 0.0001f || glm::dot(target.Color, target.Color) <= 0.000001f)
            continue;
        target.IndirectContribution = light.IndirectContribution;
        const float areaHalfExtent = 0.5f * std::max({
            std::abs(transform.Scale.X), std::abs(transform.Scale.Y),
            std::abs(transform.Scale.Z), 0.0f });
        target.SourceRadius = std::max(
            0.025f,
            light.Type == LightType::Area
                ? std::max(light.SourceRadius, areaHalfExtent)
                : light.SourceRadius);
        target.ShadowSoftness = std::clamp(light.ShadowSoftness +
            (lightingSettings.ContactShadows
                ? light.ContactShadowStrength * lightingSettings.ContactShadowStrength
                : 0.0f), 0.0f, 1.0f);
        target.DistanceSoftening = light.DistanceSoftening;
        target.ShadowStrength = std::clamp(light.ShadowStrength, 0.0f, 1.0f);
        target.ShadowIndex = light.CastShadows && target.Intensity > 0.0001f &&
            glm::dot(target.Color, target.Color) > 0.000001f
            ? allocateLocalShadow(target.Position, target.Radius, ent.GetID())
            : -1;
        ++pointLightCount;
    }

    for (size_t ei = 0;
         ei < entities.size() && spotLightCount < maxSpotLights;
         ++ei)
    {
        Entity& ent = const_cast<Entity&>(entities[ei]);
        if (!isEntityActiveInHierarchy(ent) || !ent.HasLight()) continue;
        const LightComponent& light = ent.GetLightComponent();
        if (!light.Enabled || light.Type != LightType::Spot) continue;

        const Transform& transform = ent.GetTransform();
        const glm::mat4& model = (ei < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[ei] : buildEntityModel(ent);
        glm::vec3 localDirection(
            light.DirectionLocal[0], light.DirectionLocal[1], light.DirectionLocal[2]);
        if (glm::length(localDirection) < 0.001f)
            localDirection = glm::vec3(0.0f, -1.0f, 0.0f);

        RenderSpotLight& target = spotLights[spotLightCount];
        // A posicao da fonte precisa estar no mesmo espaco mundial usado pelo
        // WorldPos do shader e pelo cubemap de sombra.
        target.Position = glm::vec3(model[3]);
        target.Range = std::clamp(light.Range, 0.1f, 10000.0f);
        // Gere o cubemap tambem para luzes fora da camera; os receptores sao
        // filtrados naturalmente no passe principal.
        // FIXED: Usar apenas a rotação para transformar a direção, não a matriz model completa
        glm::mat4 rotationOnly = glm::mat4(glm::mat3(model));  // Remove translation and scale
        target.Direction = glm::normalize(glm::vec3(rotationOnly * glm::vec4(localDirection, 0.0f)));
        target.Color = glm::max(glm::vec3(light.Color[0], light.Color[1], light.Color[2]), glm::vec3(0.0f));
        if (light.UseTemperature)
            target.Color *= LightingSystem::ColorTemperatureToRGB(light.TemperatureKelvin);
        target.Intensity = std::max(0.0f, light.Intensity);
        if (target.Intensity <= 0.0001f || glm::dot(target.Color, target.Color) <= 0.000001f)
            continue;
        target.IndirectContribution = light.IndirectContribution;
        target.SourceRadius = std::max(0.025f, light.SourceRadius);
        const float innerHalf = std::clamp(light.InnerAngleDeg * 0.5f, 0.5f, 44.5f);
        const float outerHalf = std::clamp(
            std::max(light.OuterAngleDeg * 0.5f, innerHalf + 0.25f),
            innerHalf + 0.25f, 44.9f);
        target.InnerCos = std::cos(glm::radians(innerHalf));
        target.OuterCos = std::cos(glm::radians(outerHalf));
        target.ShadowSoftness = std::clamp(light.ShadowSoftness +
            (lightingSettings.ContactShadows
                ? light.ContactShadowStrength * lightingSettings.ContactShadowStrength
                : 0.0f), 0.0f, 1.0f);
        target.DistanceSoftening = light.DistanceSoftening;
        target.ShadowStrength = std::clamp(light.ShadowStrength, 0.0f, 1.0f);
        target.ShadowIndex = light.CastShadows && target.Intensity > 0.0001f &&
            glm::dot(target.Color, target.Color) > 0.000001f
            ? allocateLocalShadow(target.Position, target.Range, ent.GetID(),
                &target.Direction, target.OuterCos)
            : -1;
        ++spotLightCount;
    }

    // Cada shadow map local tem sua própria assinatura e lista de casters.
    // Mover um objeto fora do alcance de uma luz não invalida aquela projeção;
    // as listas também evitam percorrer todos os meshes nas seis faces do cubemap.
    std::array<std::size_t, Shader::MAX_LOCAL_SHADOWS> localShadowSignatures{};
    std::array<bool, Shader::MAX_LOCAL_SHADOWS> localShadowDirtyBySource{};
    bool localShadowDirty = false;
    auto mixLocalValue = [](std::size_t& signature, std::size_t value)
    {
        signature ^= value + 0x9e3779b97f4a7c15ull +
            (signature << 6) + (signature >> 2);
    };
    auto isShadowHelperEntity = [](const Entity& entity)
    {
        const std::string& name = entity.GetName();
        return name == "Grid" || name == "Axis" || name == "Axis X" ||
            name == "Axis Y" || name == "Axis Z";
    };
    const auto localShadowPrepareStart = std::chrono::steady_clock::now();
    if (localShadowsEnabled)
    {
        for (int sourceIndex = 0; sourceIndex < localShadowCount; ++sourceIndex)
        {
            const LocalShadowSource& source = localShadowSources[sourceIndex];
            std::size_t signature = 1469598103934665603ull;
            mixLocalValue(signature, static_cast<std::size_t>(localShadowCount));
            mixLocalValue(signature, static_cast<std::size_t>(pointLightCount));
            mixLocalValue(signature, static_cast<std::size_t>(spotLightCount));
            mixLocalValue(signature, static_cast<std::size_t>(m_EffectivePointShadowMapSize));
            mixLocalValue(signature, static_cast<std::size_t>(source.EmitterEntityID));
            mixLocalValue(signature, source.IsSpot ? 1u : 0u);
            for (int axis = 0; axis < 3; ++axis)
            {
                mixLocalValue(signature, std::hash<float>{}(source.Position[axis]));
                if (source.IsSpot)
                    mixLocalValue(signature, std::hash<float>{}(source.Direction[axis]));
            }
            mixLocalValue(signature, std::hash<float>{}(source.FarPlane));
            mixLocalValue(signature, std::hash<float>{}(source.OuterCos));

            std::vector<size_t>& casters = m_LocalShadowCasterScratch[sourceIndex];
            casters.clear();
            for (size_t entityIdx = 0; entityIdx < entities.size(); ++entityIdx)
            {
                Entity& ent = const_cast<Entity&>(entities[entityIdx]);
                if (ent.IsEditorOnly() || ent.HasUI() || ent.HasCamera() ||
                    !isEntityActiveInHierarchy(ent) || isShadowHelperEntity(ent) ||
                    m_EntityShadowOpacityScratch[entityIdx] <= 0.001f)
                    continue;

                auto& meshComp = ent.GetMeshComponent();
                if (!meshComp.MeshData || !meshComp.MeshData->IsValid())
                {
                    if (ent.IsGlobalLight() || ent.IsGlobalScript() || ent.HasLight() ||
                        ent.HasLocalLight() || ent.HasAudio() || ent.HasCamera() || ent.IsFolder ||
                        meshComp.PrimitiveType == "None" || !meshComp.MeshPath.empty() ||
                        meshComp.PrimitiveType != "Cube")
                        continue;
                    meshComp.MeshData = MeshCache::CreateCubeInstance();
                    if (!meshComp.MeshData || !meshComp.MeshData->IsValid())
                        continue;
                }

                const glm::mat4& model = entityIdx < m_EntityModelsScratch.size()
                    ? m_EntityModelsScratch[entityIdx] : buildEntityModel(ent);
                const Transform& transform = ent.GetTransform();
                const float maxScale = std::max({
                    std::abs(transform.Scale.X), std::abs(transform.Scale.Y),
                    std::abs(transform.Scale.Z), 1.0f });
                const float radius = std::max(1.0f,
                    std::max(meshComp.MeshData->GetBoundingRadius(), ent.PickingRadius)) * maxScale * 2.0f;
                const glm::vec3 center = glm::vec3(
                    model * glm::vec4(meshComp.MeshData->GetBoundingCenter(), 1.0f));
                const glm::vec3 toCaster = center - source.Position;
                const float casterDistance = glm::length(toCaster);
                if (casterDistance > source.FarPlane + radius)
                    continue;
                if (source.IsSpot && casterDistance > radius + 0.001f)
                {
                    const float casterAngularRadius = std::asin(std::clamp(
                        radius / casterDistance, 0.0f, 1.0f));
                    const float maxCasterAngle = std::min(glm::pi<float>(),
                        std::acos(std::clamp(source.OuterCos, -1.0f, 1.0f)) +
                        casterAngularRadius + glm::radians(2.0f));
                    if (glm::dot(source.Direction, toCaster / casterDistance) <
                        std::cos(maxCasterAngle))
                        continue;
                }

                casters.push_back(entityIdx);
                mixLocalValue(signature, static_cast<std::size_t>(ent.GetID()));
                mixLocalValue(signature, std::hash<const void*>{}(meshComp.MeshData.get()));
                mixLocalValue(signature, static_cast<std::size_t>(meshComp.MeshData->GetRevision()));
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        mixLocalValue(signature, std::hash<float>{}(model[column][row]));
                mixLocalValue(signature, ent.CastShadows ? 1u : 0u);
                mixLocalValue(signature, std::hash<float>{}(
                    ent.CastShadows ? m_EntityShadowOpacityScratch[entityIdx] : 0.0f));
            }

            localShadowSignatures[sourceIndex] = signature;
            localShadowDirtyBySource[sourceIndex] =
                !m_LocalShadowCacheValid || !m_LocalShadowSourceCacheValid[sourceIndex] ||
                signature != m_LocalShadowSourceSignatures[sourceIndex];
            localShadowDirty = localShadowDirty || localShadowDirtyBySource[sourceIndex];
        }
    }
    m_LastStats.ShadowTimeMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - localShadowPrepareStart).count();

    // -------------------------------------------------------------
    // 1. SHADOW MAP PASS (Render Depth from Sun Viewpoint)
    // -------------------------------------------------------------
    if (useShadows && directionalShadowDirty)
    {
        const auto shadowStart = std::chrono::steady_clock::now();
        InitShadowMap();
        for (int cascade = 0; cascade < activeCascadeCount; ++cascade)
        {
            float cascadeDepthNear = 0.0f;
            float cascadeDepthFar = 0.0f;
            cascadeMatrices[cascade] = BuildStableDirectionalShadowMatrix(
                view,
                projection,
                lightDirection,
                m_ShadowMapSize,
                cascadeNearPlanes[cascade],
                cascadeFarPlanes[cascade],
                maxShadowCasterY,
                shadowDistance,
                &cascadeDepthNear,
                &cascadeDepthFar
            );
            cascadeDepthRanges[cascade] = cascadeDepthFar - cascadeDepthNear;
        }

        FrustumPlanes cascadeFrustums[Shader::MAX_SHADOW_CASCADES];
        for (int cascade = 0; cascade < activeCascadeCount; ++cascade)
        {
            cascadeFrustums[cascade].FromMatrix(cascadeMatrices[cascade]);
        }

        // Cada cascata recebe seu proprio depth map. As primeiras cobrem a
        // area perto da camera com muito mais texels; as ultimas preservam o
        // alcance do sol sem estourar a resolucao em toda a cena.
        GLint previousFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glViewport(0, 0, static_cast<GLsizei>(m_ShadowMapSize), static_cast<GLsizei>(m_ShadowMapSize));
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_CULL_FACE);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        for (int cascade = 0; cascade < activeCascadeCount; ++cascade)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO[cascade]);
            const float clearOpacity[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            glClearBufferfv(GL_COLOR, 0, clearOpacity);
            glClear(GL_DEPTH_BUFFER_BIT);

            shader.UseShadowShader();
            shader.SetShadowLightSpaceMatrix(cascadeMatrices[cascade]);

            const float cascadePadding = std::min(kShadowDepthPadding, std::max(12.0f, cascadeFarPlanes[cascade] * 0.75f));

            for (size_t i = 0; i < entities.size(); i++)
            {
                Entity& ent = const_cast<Entity&>(entities[i]);

                if (ent.IsEditorOnly() || ent.HasUI() || ent.HasCamera() ||
                    !isEntityActiveInHierarchy(ent) || m_EntityShadowOpacityScratch[i] <= 0.001f ||
                    ent.GetName() == "Grid" || ent.GetName() == "Axis" ||
                    ent.GetName() == "Axis X" || ent.GetName() == "Axis Y" || ent.GetName() == "Axis Z")
                {
                    continue;
                }

                auto& meshComp = ent.GetMeshComponent();
                if (!meshComp.MeshData || !meshComp.MeshData->IsValid())
                {
                    if (ent.IsGlobalLight() || ent.IsGlobalScript() || ent.HasLight() || ent.HasLocalLight() ||
                        ent.HasAudio() || ent.HasCamera() || ent.IsFolder ||
                        meshComp.PrimitiveType == "None" || !meshComp.MeshPath.empty() ||
                        meshComp.PrimitiveType != "Cube")
                    {
                        continue;
                    }
                    meshComp.MeshData = MeshCache::CreateCubeInstance();
                    if (!meshComp.MeshData || !meshComp.MeshData->IsValid())
                        continue;
                }

                const glm::mat4& model = (i < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[i] : buildEntityModel(ent);
                const Transform& transform = ent.GetTransform();
                const float maxScale = std::max({
                    std::abs(transform.Scale.X),
                    std::abs(transform.Scale.Y),
                    std::abs(transform.Scale.Z),
                    0.001f
                });
                const float worldRadius = std::max(
                    0.001f, meshComp.MeshData->GetBoundingRadius() * maxScale);
                const glm::vec3 shadowCenter = glm::vec3(
                    model * glm::vec4(meshComp.MeshData->GetBoundingCenter(), 1.0f));
                // A caster pode estar fora do volume visivel e ainda assim
                // projetar sombra dentro dele. A margem evita cortes secos
                // nas bordas das cascatas.
                if (!cascadeFrustums[cascade].IsSphereInside(
                        shadowCenter,
                        worldRadius + cascadePadding))
                {
                    continue;
                }
                shader.SetShadowModelMatrix(model);
                // Mesmo sem projetar a propria sombra, a geometria opaca
                // continua escrevendo profundidade para bloquear casters atras.
                shader.SetShadowOpacity(ent.CastShadows ? m_EntityShadowOpacityScratch[i] : 0.0f);
                meshComp.MeshData->Draw();
                ++m_LastStats.ShadowDrawCalls;
                m_LastStats.ShadowTriangles += meshComp.MeshData->GetVertexCount() / 3;
            }
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc(GL_LESS);
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        m_LastStats.ShadowTimeMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - shadowStart).count();
        m_CachedCascadeMatrices = cascadeMatrices;
        m_CachedCascadeDepthRanges = cascadeDepthRanges;
        m_DirectionalShadowSignature = directionalShadowSignature;
        m_DirectionalShadowCacheValid = true;
    }

    // -------------------------------------------------------------
    // 1B. LOCAL SHADOW MAP PASS (Point/Spot lights)
    // -------------------------------------------------------------
    // Cada luz artificial usa um cubemap de profundidade. Assim a sombra
    // acompanha a luz em qualquer direção, em vez de ser tratada apenas como
    // uma contribuição de cor sem oclusão.
    if (localShadowsEnabled && localShadowCount > 0 && localShadowDirty)
    {
        const auto shadowStart = std::chrono::steady_clock::now();
        InitPointShadowMaps(localShadowCount);

        GLint previousFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glViewport(
            0, 0,
            static_cast<GLsizei>(m_PointShadowMapSize),
            static_cast<GLsizei>(m_PointShadowMapSize));
        glBindFramebuffer(GL_FRAMEBUFFER, m_PointShadowFBO);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_CULL_FACE);
        glDepthFunc(GL_LESS);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);

        for (int sourceIndex = 0; sourceIndex < localShadowCount; ++sourceIndex)
        {
            if (!localShadowDirtyBySource[sourceIndex])
                continue;
            const LocalShadowSource& source = localShadowSources[sourceIndex];
            const auto faceMatrices = BuildPointShadowMatrices(
                source.Position, source.FarPlane);
            static const std::array<glm::vec3, 6> kPointShadowFaceDirections = {
                glm::vec3( 1.0f,  0.0f,  0.0f),
                glm::vec3(-1.0f,  0.0f,  0.0f),
                glm::vec3( 0.0f,  1.0f,  0.0f),
                glm::vec3( 0.0f, -1.0f,  0.0f),
                glm::vec3( 0.0f,  0.0f,  1.0f),
                glm::vec3( 0.0f,  0.0f, -1.0f)
            };
            // Uma face cubemap cobre no máximo 54,74 graus a partir do centro.
            // Se nem essa margem alcançar o cone do Spot, a face é limpa mas
            // seus meshes não são desenhados. Isso mantém o mapa coerente e
            // reduz draw calls sem mudar o sampler/resultado da sombra.
            constexpr float kCubeFaceHalfAngleRadians = 0.9553166181f;
            const float spotHalfAngle = source.IsSpot
                ? std::acos(std::clamp(source.OuterCos, -1.0f, 1.0f))
                : glm::pi<float>();
            const float maxSpotFaceAngle = std::min(
                glm::pi<float>(), spotHalfAngle + kCubeFaceHalfAngleRadians +
                    glm::radians(2.0f));
            const float minSpotFaceDot = std::cos(maxSpotFaceAngle);

            // Coleta todas as entidades da família/luminária da luz (a própria luz, pai, ancestrais,
            // irmãos e filhos diretos) para que o corpo da luminária NUNCA bloqueie a sua própria luz.
            std::unordered_set<uint64_t> lightRigEntities;
            if (source.EmitterEntityID != 0)
            {
                lightRigEntities.insert(source.EmitterEntityID);

                // 1. Ancestrais da luz (pai, avô, etc.): o pai é o corpo da luz e não deve fazer sombra nela
                uint64_t curr = source.EmitterEntityID;
                while (curr != 0)
                {
                    uint64_t parent = 0;
                    const auto currentIt = entityIndexByID.find(curr);
                    if (currentIt != entityIndexByID.end())
                        parent = entities[currentIt->second].GetParentID();
                    if (parent != 0)
                    {
                        lightRigEntities.insert(parent);
                        // Irmãos diretos da luz que compartilham o mesmo pai (peças da luminária)
                        const auto parentIt = entityIndexByID.find(parent);
                        if (parentIt != entityIndexByID.end())
                            for (size_t siblingIndex = m_EntityFirstChildScratch[parentIt->second];
                                 siblingIndex != noEntityIndex;
                                 siblingIndex = m_EntityNextSiblingScratch[siblingIndex])
                                lightRigEntities.insert(entities[siblingIndex].GetID());
                    }
                    curr = parent;
                }

                // 2. Descendentes da luz (filhos, netos)
                std::vector<uint64_t> rigStack = { source.EmitterEntityID };
                while (!rigStack.empty())
                {
                    uint64_t p = rigStack.back();
                    rigStack.pop_back();
                    const auto parentIt = entityIndexByID.find(p);
                    if (parentIt == entityIndexByID.end())
                        continue;
                    for (size_t childIndex = m_EntityFirstChildScratch[parentIt->second];
                         childIndex != noEntityIndex;
                         childIndex = m_EntityNextSiblingScratch[childIndex])
                    {
                        const uint64_t childID = entities[childIndex].GetID();
                        if (lightRigEntities.insert(childID).second)
                            rigStack.push_back(childID);
                    }
                }
            }

            shader.UsePointShadowShader();
            shader.SetPointShadowLightPosition(
                source.Position.x, source.Position.y, source.Position.z);
            shader.SetPointShadowFarPlane(source.FarPlane);

            for (int face = 0; face < 6; ++face)
            {
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER,
                    GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                    m_PointShadowMaps[sourceIndex],
                    0
                );
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER,
                    GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                    m_PointShadowOpacityMaps[sourceIndex],
                    0
                );
                const GLenum pointDrawBuffers[] = { GL_COLOR_ATTACHMENT0 };
                glDrawBuffers(1, pointDrawBuffers);
                const float clearOpacity[] = { 1.0f, 1.0f, 1.0f, 1.0f };
                glClearBufferfv(GL_COLOR, 0, clearOpacity);
                glClear(GL_DEPTH_BUFFER_BIT);
                const bool faceIntersectsSpotCone = !source.IsSpot ||
                    glm::dot(source.Direction, kPointShadowFaceDirections[face]) >= minSpotFaceDot;
                if (!faceIntersectsSpotCone)
                    continue;
                shader.SetPointShadowLightSpaceMatrix(faceMatrices[face]);

                FrustumPlanes faceFrustum;
                faceFrustum.FromMatrix(faceMatrices[face]);

                for (const size_t entIdx : m_LocalShadowCasterScratch[sourceIndex])
                {
                    const Entity& sourceEntity = entities[entIdx];
                    Entity& ent = const_cast<Entity&>(sourceEntity);
                    if (source.EmitterEntityID != 0 && lightRigEntities.count(ent.GetID()) > 0)
                    {
                        continue;
                    }

                    auto& meshComp = ent.GetMeshComponent();
                    if (!meshComp.MeshData || !meshComp.MeshData->IsValid())
                        continue;

                    const Transform& transform = ent.GetTransform();
                    const glm::mat4& model = (entIdx < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[entIdx] : buildEntityModel(ent);
                    const glm::vec3 center = glm::vec3(
                        model * glm::vec4(meshComp.MeshData->GetBoundingCenter(), 1.0f));
                    const float maxScale = std::max({
                        std::abs(transform.Scale.X),
                        std::abs(transform.Scale.Y),
                        std::abs(transform.Scale.Z),
                        1.0f
                    });
                    const float meshRadius = meshComp.MeshData->GetBoundingRadius();
                    const float radius = std::max(
                        1.0f,
                        std::max(meshRadius, ent.PickingRadius)) * maxScale * 2.0f;
                    if (glm::length(center - source.Position) > source.FarPlane + radius)
                        continue;

                    if (!faceFrustum.IsSphereInside(center, radius))
                        continue;

                    shader.SetPointShadowModelMatrix(model);
                    // Profundidade preserva o bloqueio da luz; opacidade zero
                    // impede que este objeto gere uma sombra propria.
                    shader.SetPointShadowOpacity(ent.CastShadows ? m_EntityShadowOpacityScratch[entIdx] : 0.0f);
                     meshComp.MeshData->Draw();
                     ++m_LastStats.ShadowDrawCalls;
                     m_LastStats.ShadowTriangles += meshComp.MeshData->GetVertexCount() / 3;
                }
            }
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc(GL_LESS);
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        m_LastStats.ShadowTimeMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - shadowStart).count();
        for (int sourceIndex = 0; sourceIndex < localShadowCount; ++sourceIndex)
        {
            m_LocalShadowSourceSignatures[sourceIndex] = localShadowSignatures[sourceIndex];
            m_LocalShadowSourceCacheValid[sourceIndex] = true;
        }
        m_LocalShadowCacheValid = true;
    }

    // -------------------------------------------------------------
    // 2. MAIN PASS: SETUP SHADER & SHADOW MAP TEXTURE
    // -------------------------------------------------------------
    // A cena é desenhada primeiro. O grid/axis entra depois, usando o depth
    // buffer pronto, para nunca interferir no estado ou na visibilidade dos
    // meshes quando uma das opções de visualização é desligada.
    shader.Use();
    shader.SetViewMatrix(view);
    shader.SetProjectionMatrix(projection);

    // O céu é desenhado sem depth write, mas alguns drivers preservam o
    // estado do depth buffer entre passes. Começamos a geometria da cena com
    // depth limpo para que nenhum mesh seja descartado silenciosamente.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glClear(GL_DEPTH_BUFFER_BIT);
    shader.SetLightSpaceMatrix(cascadeMatrices[0]);
    for (int cascade = 0; cascade < activeCascadeCount; ++cascade)
        shader.SetShadowCascadeMatrix(cascade, cascadeMatrices[cascade]);
    shader.SetShadowCascadeFarPlanes(cascadeFarPlanes.data(), activeCascadeCount);
    shader.SetShadowCascadeDepthRanges(cascadeDepthRanges.data(), activeCascadeCount);
    std::array<glm::vec2, Shader::MAX_SHADOW_CASCADES> cascadeWorldTexelSizes{};
    const float directionalResolution = static_cast<float>(std::max(1u, m_ShadowMapSize));
    for (int cascade = 0; cascade < Shader::MAX_SHADOW_CASCADES; ++cascade)
    {
        const glm::mat4& matrix = cascadeMatrices[cascade];
        const glm::vec3 rowX(matrix[0][0], matrix[1][0], matrix[2][0]);
        const glm::vec3 rowY(matrix[0][1], matrix[1][1], matrix[2][1]);
        cascadeWorldTexelSizes[cascade] = glm::vec2(
            2.0f / std::max(glm::length(rowX) * directionalResolution, 0.00001f),
            2.0f / std::max(glm::length(rowY) * directionalResolution, 0.00001f));
    }
    shader.SetShadowCascadeWorldTexelSizes(
        cascadeWorldTexelSizes.data(), activeCascadeCount);
    shader.SetDirectionalSourceAngularRadius(glm::radians(
        lightingSettings.DirectionalSourceAngularDiameterDegrees * 0.5f));
    const unsigned int localResolution = std::max(
        1u, m_PointShadowMapSize != 0
            ? m_PointShadowMapSize : m_EffectivePointShadowMapSize);
    shader.SetPointShadowAngularTexelSize(2.0f / static_cast<float>(localResolution));

    // Cascatas do sol ocupam as unidades 1..4.
    for (int cascade = 0; cascade < Shader::MAX_SHADOW_CASCADES; ++cascade)
    {
        glActiveTexture(GL_TEXTURE1 + cascade);
        glBindTexture(
            GL_TEXTURE_2D,
            (useShadows && cascade < activeCascadeCount)
                ? m_ShadowMap[cascade]
                : 0);
    }
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(
        GL_TEXTURE_2D_ARRAY,
        useShadows ? m_ShadowOpacityMap[0] : 0);
    for (int i = 0; i < Shader::MAX_LOCAL_SHADOWS; ++i)
    {
        glActiveTexture(GL_TEXTURE5 + i);
        glBindTexture(
            GL_TEXTURE_CUBE_MAP,
            (i < localShadowCount && localShadowsEnabled) ? m_PointShadowMaps[i] : 0);
        glActiveTexture(GL_TEXTURE10 + i);
        glBindTexture(
            GL_TEXTURE_CUBE_MAP,
            (i < localShadowCount && localShadowsEnabled) ? m_PointShadowOpacityMaps[i] : 0);
        shader.SetPointShadowMap(i, 5 + i);
        shader.SetPointShadowEnabled(i, i < localShadowCount && localShadowsEnabled);
        shader.SetPointShadowFarPlane(
            i,
            i < localShadowCount ? localShadowSources[i].FarPlane : 10.0f);
    }
    shader.SetShadowOpacityArray(14);
    glActiveTexture(GL_TEXTURE0);

    // Reestabelece o estado mínimo do passe da cena mesmo quando o grid/axis
    // está desligado. Isso evita que um passe anterior deixe depth, máscara
    // ou rasterização desativados e faça os meshes parecerem desaparecer.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glLineWidth(1.0f);
    GLuint lastBoundTexture = 0;

    // O editor precisa continuar mostrando a geometria quando a câmera está
    // dentro de uma sala/mesh ou quando um arquivo importado tem winding
    // inconsistente entre as faces. O culling global fazia paredes e meshes
    // desaparecerem dependendo do lado da câmera; a iluminação continua
    // usando as normais, então isto não altera a seleção nem o gizmo.
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto buildEntityTileMatrices = [&](const Entity& entity, const glm::mat4& baseModel, const Material* mat,
                                       std::vector<glm::mat4>& result)
    {
        result.clear();
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();

        if (!mat || mat->ScaleMode != UVScaleMode::TileMesh)
        {
            result.push_back(baseModel);
            return;
        }

        float tileSize = mat->TileSize > 0.05f ? mat->TileSize : 1.0f;
        int countX = std::clamp(static_cast<int>(std::round(std::abs(transform.Scale.X) / tileSize)), 1, 10);
        int countY = std::clamp(static_cast<int>(std::round(std::abs(transform.Scale.Y) / tileSize)), 1, 10);
        int countZ = std::clamp(static_cast<int>(std::round(std::abs(transform.Scale.Z) / tileSize)), 1, 10);

        if (countX * countY * countZ > 125)
        {
            countX = std::min(countX, 5);
            countY = std::min(countY, 5);
            countZ = std::min(countZ, 5);
        }

        if (countX == 1 && countY == 1 && countZ == 1)
        {
            result.push_back(baseModel);
            return;
        }

        float signX = transform.Scale.X >= 0.0f ? 1.0f : -1.0f;
        float signY = transform.Scale.Y >= 0.0f ? 1.0f : -1.0f;
        float signZ = transform.Scale.Z >= 0.0f ? 1.0f : -1.0f;

        glm::mat4 transRot = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z))
            * glm::mat4_cast(transform.Rotation);

        glm::mat4 baseScale = glm::scale(glm::mat4(1.0f), glm::vec3(tileSize * signX, tileSize * signY, tileSize * signZ));

        for (int x = 0; x < countX; ++x)
        {
            float ox = (static_cast<float>(x) - static_cast<float>(countX - 1) * 0.5f) * tileSize;
            for (int y = 0; y < countY; ++y)
            {
                float oy = (static_cast<float>(y) - static_cast<float>(countY - 1) * 0.5f) * tileSize;
                for (int z = 0; z < countZ; ++z)
                {
                    float oz = (static_cast<float>(z) - static_cast<float>(countZ - 1) * 0.5f) * tileSize;
                    glm::mat4 tileModel = transRot * glm::translate(glm::mat4(1.0f), glm::vec3(ox, oy, oz)) * baseScale;
                    result.push_back(tileModel);
                }
            }
        }
    };

    auto drawSelectionOutline = [&](Entity& entity, const glm::mat4& model)
    {
        auto& mesh = entity.GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid() || entity.IsGlobalLight() || entity.IsGlobalScript())
            return;

        // Se o objeto estiver no Modo Modelador, o ViewportPanel desenha o overlay com precisão (evita o corte diagonal)
        if (InspectorPanel::IsModelingMode() && entity.GetID() == InspectorPanel::GetModelingEntityID())
            return;

        static std::shared_ptr<Mesh> s_SelectionBoxMesh = nullptr;
        if (!s_SelectionBoxMesh)
        {
            static const glm::vec3 corners[8] = {
                { -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
                {  0.5f, -0.5f,  0.5f }, { -0.5f, -0.5f,  0.5f },
                { -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
                {  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f }
            };
            static const int edgeIndices[24] = {
                0,1, 1,2, 2,3, 3,0, // base
                4,5, 5,6, 6,7, 7,4, // topo
                0,4, 1,5, 2,6, 3,7  // colunas verticais
            };
            std::vector<Vertex> lineVertices;
            lineVertices.reserve(24);
            for (int idx = 0; idx < 24; ++idx)
            {
                Vertex v{};
                const glm::vec3& p = corners[edgeIndices[idx]];
                v.X = p.x; v.Y = p.y; v.Z = p.z;
                v.NX = 0.0f; v.NY = 1.0f; v.NZ = 0.0f;
                lineVertices.push_back(v);
            }
            s_SelectionBoxMesh = std::make_shared<Mesh>();
            s_SelectionBoxMesh->Create(lineVertices, GL_LINES, "SelectionBoxOutline");
        }

        glm::vec3 center = mesh->GetBoundingCenter();
        glm::vec3 halfExtents = mesh->GetBoundingHalfExtents();
        glm::vec3 scale = (halfExtents * 2.0f) * 1.002f;

        glm::mat4 boxModel = model *
            glm::translate(glm::mat4(1.0f), center) *
            glm::scale(glm::mat4(1.0f), scale);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTextureID);
        shader.SetModelMatrix(boxModel);
        shader.SetColor(1.0f, 0.88f, 0.10f);
        shader.SetMaterialProperties(0.0f, 0.45f, 0.5f, 0.0f);
        shader.SetUseShadows(false);
        glUniform1i(glGetUniformLocation(shader.GetProgramID(), "u_EditorSelectionUnlit"), 1);
        shader.SetOpacity(1.0f);

        glLineWidth(2.0f);
        s_SelectionBoxMesh->Draw();
        glUniform1i(glGetUniformLocation(shader.GetProgramID(), "u_EditorSelectionUnlit"), 0);
        glLineWidth(1.0f);
    };

    // -------------------------------------------------------------
    // 3. SET SCENE LIGHTING PARAMETERS & RENDER SCENE ENTITIES WITH SHADOWS
    // -------------------------------------------------------------
    shader.SetLightDirection(
        lightDirection.x,
        lightDirection.y,
        lightDirection.z
    );

    shader.SetLightColor(
        directionalColor.x,
        directionalColor.y,
        directionalColor.z
    );

    shader.SetLightIntensity(
        directionalIntensity
    );
    shader.SetAdvancedLightingEnabled(advancedLightingEnabled);
    shader.SetDirectionalShadowsEnabled(useShadows);
    shader.SetDirectionalShadowDistanceSoftening(
        lightingSettings.DirectionalDistanceSoftening);
    shader.SetDirectionalShadowSubTexelFilter(
        lightingSettings.Quality == LightingQuality::Ultra);
    // Usa 1.0 como padrão para força de sombra global quando não há Global Light
    float globalShadowStrength = 1.0f;
    for (Entity& ent : scene.GetEntities())
    {
        if (ent.HasComponent<GlobalLightComponent>())
        {
            globalShadowStrength = ent.GetComponent<GlobalLightComponent>()->ShadowStrength;
            break;
        }
    }
    shader.SetGlobalShadowStrength(globalShadowStrength);

    for (int i = 0; i < pointLightCount; ++i)
    {
        const RenderPointLight& light = pointLights[i];
        shader.SetPointLight(
            i,
            light.Position.x, light.Position.y, light.Position.z,
            light.Color.r, light.Color.g, light.Color.b,
            light.Intensity, light.Radius, light.ShadowIndex,
            light.SourceRadius, light.ShadowSoftness,
            light.ShadowStrength,
            light.IndirectContribution, light.DistanceSoftening);
    }
    shader.SetPointLightCount(pointLightCount);

    for (int i = 0; i < spotLightCount; ++i)
    {
        const RenderSpotLight& light = spotLights[i];
        shader.SetSpotLight(
            i,
            light.Position.x, light.Position.y, light.Position.z,
            light.Direction.x, light.Direction.y, light.Direction.z,
            light.Color.r, light.Color.g, light.Color.b,
            light.Intensity, light.Range,
            light.InnerCos, light.OuterCos, light.ShadowIndex,
            light.SourceRadius, light.ShadowSoftness,
            light.ShadowStrength,
            light.IndirectContribution, light.DistanceSoftening);
    }
    shader.SetSpotLightCount(spotLightCount);

    auto& drawOrder = m_DrawOrderScratch;
    drawOrder.resize(entities.size());
    std::iota(drawOrder.begin(), drawOrder.end(), 0);
    auto drawOrderLess = [&](int a, int b)
    {
        const int layerA = entities[static_cast<size_t>(a)].Layer;
        const int layerB = entities[static_cast<size_t>(b)].Layer;
        return layerA != layerB ? layerA < layerB : a < b;
    };
    // Most 2D scenes keep objects in creation order on one layer; avoid an
    // O(n log n) sort each frame unless the current order actually needs it.
    if (!editor2DMode || !std::is_sorted(drawOrder.begin(), drawOrder.end(), drawOrderLess))
        std::sort(drawOrder.begin(), drawOrder.end(), drawOrderLess);

    auto& selectedFlags = m_MultiSelectionFlagsScratch;
    selectedFlags.assign(entities.size(), 0);
    for (const int selectedIndex : multiSelectedEntities)
    {
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < selectedFlags.size())
            selectedFlags[static_cast<size_t>(selectedIndex)] = 1;
    }
    const GLint editorSelectionUnlitLocation =
        glGetUniformLocation(shader.GetProgramID(), "u_EditorSelectionUnlit");

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (editor2DMode)
    {
        glDisable(GL_DEPTH_TEST);
    }

    FrustumPlanes cameraFrustum;
    cameraFrustum.FromMatrix(projection * view);
    bool currentCullEnabled = false;
    GLuint lastBoundNormal = 0;
    GLuint lastBoundEmission = 0;

    for (int orderIdx = 0; orderIdx < static_cast<int>(drawOrder.size()); ++orderIdx)
    {
        const size_t i = static_cast<size_t>(drawOrder[orderIdx]);
        Entity& currentEntity = const_cast<Entity&>(entities[i]);

        // Câmeras de cena são componentes do editor/runtime, não meshes
        // cúbicos. O ícone orientado no viewport é desenhado pelo overlay.
        if (currentEntity.HasCamera() || currentEntity.HasUI())
            continue;

        // Pasta e Modelo são contêineres editoriais. Eles não possuem corpo,
        // mesh, colisão ou representação visual; somente seus filhos devem ser
        // renderizados pelo loop normal de entidades.
        if (currentEntity.IsFolder || currentEntity.IsModel)
            continue;

        // Global Light e Global Script são entidades lógicas de configuração,
        // não possuem mesh, colisão ou representação visual no mundo 3D.
        if (currentEntity.IsGlobalLight() || currentEntity.IsGlobalScript())
            continue;

        const bool isActiveSelection = static_cast<int>(i) == selectedEntity;
        const bool isMultiSelection = selectedFlags[i] != 0;

        if (!currentEntity.Active || currentEntity.Opacity <= 0.001f)
        {
            // Opacidade controla apenas o mesh. A marcação do editor precisa
            // continuar visível para que um objeto invisível possa ser
            // localizado e editado pelo Inspector.
            auto& hiddenMesh = currentEntity.GetMeshComponent();
            if (!hiddenMesh.MeshData || !hiddenMesh.MeshData->IsValid())
            {
                if (currentEntity.IsGlobalLight() || currentEntity.IsGlobalScript() || currentEntity.HasLight() || currentEntity.HasLocalLight() ||
                    currentEntity.HasAudio() || currentEntity.HasCamera() || currentEntity.IsFolder ||
                    hiddenMesh.PrimitiveType == "None")
                {
                    continue;
                }
                if (hiddenMesh.MeshPath.empty() && hiddenMesh.PrimitiveType == "Cube")
                    hiddenMesh.MeshData = MeshCache::CreateCubeInstance();
            }
            if ((isActiveSelection || isMultiSelection) && hiddenMesh.MeshData &&
                hiddenMesh.MeshData->IsValid())
            {
                drawSelectionOutline(currentEntity, buildEntityModel(currentEntity));
            }
            continue;
        }

        auto& meshComponent = currentEntity.GetMeshComponent();
        if (!meshComponent.MeshData || !meshComponent.MeshData->IsValid())
        {
            if (currentEntity.IsGlobalLight() || currentEntity.HasLight() || currentEntity.HasLocalLight() ||
                currentEntity.HasAudio() || currentEntity.HasCamera() || currentEntity.IsFolder ||
                meshComponent.PrimitiveType == "None")
            {
                continue;
            }
            if (meshComponent.MeshPath.empty() && meshComponent.PrimitiveType == "Cube")
                meshComponent.MeshData = MeshCache::CreateCubeInstance();
            if (!meshComponent.MeshData || !meshComponent.MeshData->IsValid())
                continue;
        }
        // A matriz vazia desativa o skinning para meshes estáticos e evita que
        // uniformes de um personagem anterior contaminem o próximo draw call.
        shader.SetSkinMatrices(currentEntity.GetAnimationComponent().SkinMatrices);

        const Transform& transform = currentEntity.GetTransform();
        glm::mat4 model = (i < m_EntityModelsScratch.size()) ? m_EntityModelsScratch[i] : buildEntityModel(currentEntity);

        // Escala negativa inverte o determinante do modelo e também inverte
        // as faces externas. Trocar o winding preserva o objeto visível sem
        // desativar o culling global.
        if (!editor2DMode)
        {
            const glm::vec3 modelX(model[0]);
            const glm::vec3 modelY(model[1]);
            const glm::vec3 modelZ(model[2]);
            const float modelDeterminant = glm::dot(modelX, glm::cross(modelY, modelZ));
            if (m_IsRenderingReflection)
                glFrontFace(modelDeterminant < 0.0f ? GL_CCW : GL_CW);
            else
                glFrontFace(modelDeterminant < 0.0f ? GL_CW : GL_CCW);
        }

                const bool isGrid = (currentEntity.GetName() == "Grid");
        const bool isAxis = (currentEntity.GetName() == "Axis" ||
                             currentEntity.GetName() == "Axis X" ||
                             currentEntity.GetName() == "Axis Y" ||
                             currentEntity.GetName() == "Axis Z");

        // Frustum culling da camera: descarta entidades estaticas fora da visao
        // com margem de seguranca. Objetos com fisica ficam fora deste descarte:
        // o solver pode atualizar a transform entre a construcao do volume e o
        // passe visual, causando o desaparecimento intermitente de cubos e
        // outros corpos dinamicos quando a camera se move.
        // Grid, eixos e entidades selecionadas nunca sao descartados para manter
        // a edicao perfeita.
        if (m_IsRenderingReflection && currentEntity.GetID() == m_ActiveMirrorEntityID)
            continue; // Nao renderiza o espelho dentro da propria reflexao

        if (!editor2DMode && !currentEntity.PhysicsEnabled &&
            !isActiveSelection && !isMultiSelection && !isGrid && !isAxis)
        {
            const float maxScale = std::max({
                std::abs(transform.Scale.X),
                std::abs(transform.Scale.Y),
                std::abs(transform.Scale.Z),
                0.001f
            });
            // Otimizacao: margem reduzida de 5.0f para 1.0f para culling mais preciso
            const float worldRadius = std::max(0.001f, meshComponent.MeshData->GetBoundingRadius() * maxScale) * 2.0f + 1.0f;
            const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(meshComponent.MeshData->GetBoundingCenter(), 1.0f));
            if (!cameraFrustum.IsSphereInside(worldCenter, worldRadius))
            {
                ++m_LastStats.FrustumCulledEntities;
                continue;
            }
        }

        ++m_LastStats.VisibleEntities;

        MaterialComponent& materialComponent = currentEntity.GetMaterialComponent();
        Material* material = nullptr;
        if (!materialComponent.MaterialGUID.empty())
        {
            material = MaterialDatabase::GetMaterialByGUID(materialComponent.MaterialGUID);
        }

        if (!material)
        {
            material = &materialComponent.MaterialData;
        }

        if (material && !material->TexturePath.empty() &&
            material->TexturePath != "[PaintCanvas]" && !material->DiffuseTexture)
            material->DiffuseTexture = TextureCache::Load(material->TexturePath);

        int uvMode = 0;
        glm::vec4 uvExtraParams(0.0f, 1.0f, 0.25f, 0.0f);
        if (material)
        {
            if (material->ScaleMode == UVScaleMode::FixedDrawing)
                uvMode = 1;
            else if (material->ScaleMode == UVScaleMode::TileMesh)
                uvMode = 2;
            else if (material->ScaleMode == UVScaleMode::WorldSpace)
                uvMode = 3;
            else if (material->ScaleMode == UVScaleMode::AutoTileFace)
                uvMode = 4;
            else if (material->ScaleMode == UVScaleMode::NineSlice)
                uvMode = 5;

            uvExtraParams = glm::vec4(
                static_cast<float>(material->FixedAlignment),
                material->FaceTileRepeat > 0.001f ? material->FaceTileRepeat : 1.0f,
                std::clamp(material->NineSliceBorder, 0.01f, 0.45f),
                0.0f
            );
        }

        glm::vec2 atlasGrid(1.0f, 1.0f);
        if (meshComponent.MeshData && meshComponent.MeshData->GetVertexCount() == 36 &&
            material && (material->TexturePath == "[PaintCanvas]" || (material->DiffuseTexture && material->DiffuseTexture->Loaded)))
        {
            atlasGrid = glm::vec2(3.0f, 2.0f);
        }
        if (materialComponent.TextureMode == FaceTextureMode::IndividualFaces)
            atlasGrid = glm::vec2(1.0f, 1.0f);

        if (editor2DMode)
        {
            // Dedicated 2D draw path: retain layer order, base color,
            // opacity and diffuse texture while skipping 3D PBR/shadow setup.
            const Vector3& color = currentEntity.GetColorComponent().Color;
            const glm::vec4 tint(color.X, color.Y, color.Z, 1.0f);
            const glm::vec3 scale(std::abs(transform.Scale.X),
                                  std::abs(transform.Scale.Y),
                                  std::abs(transform.Scale.Z));
            shader.SetUVMode(uvMode, material ? material->UVWorldScale : 1.0f, scale, atlasGrid, tint, uvExtraParams);
            shader.SetModelMatrix(model);
            const GLuint texture = material->DiffuseTexture && material->DiffuseTexture->Loaded
                ? material->DiffuseTexture->ID : whiteTextureID;
            if (texture != lastBoundTexture)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                lastBoundTexture = texture;
            }
            shader.SetColor(color.X, color.Y, color.Z);
            const float materialAlpha = material->PBRParametersEnabled
                ? std::clamp(material->Alpha, 0.0f, 1.0f)
                : (material->Type == MaterialType::Glass ? 0.35f : 1.0f);
            const float opacity = currentEntity.Opacity * materialAlpha;
            shader.SetOpacity(opacity);
            shader.SetUseShadows(false);
            if (editorSelectionUnlitLocation >= 0)
                glUniform1i(editorSelectionUnlitLocation, 1);

            const bool transparent = opacity < 0.999f;
            if (transparent)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            glDisable(GL_CULL_FACE);
            meshComponent.MeshData->Draw();
            ++m_LastStats.DrawCalls;
            m_LastStats.Triangles += meshComponent.MeshData->GetVertexCount() / 3;
            if (isActiveSelection || isMultiSelection)
                drawSelectionOutline(currentEntity, model);
            if (transparent)
            {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
            continue;
        }

        const Vector3& entColor = currentEntity.GetColorComponent().Color;
        glm::vec4 bgColor(entColor.X, entColor.Y, entColor.Z, 1.0f);
        glm::vec3 objScale(std::abs(transform.Scale.X), std::abs(transform.Scale.Y), std::abs(transform.Scale.Z));
        shader.SetUVMode(uvMode, material ? material->UVWorldScale : 1.0f, objScale, atlasGrid, bgColor, uvExtraParams);

        auto& tileModels = m_TileModelsScratch;
        buildEntityTileMatrices(currentEntity, model, material, tileModels);
        shader.SetModelMatrix(tileModels.empty() ? model : tileModels[0]);

        // Bind material texture on Texture Unit 0
        GLuint tex = (material->DiffuseTexture && material->DiffuseTexture->Loaded) 
                     ? material->DiffuseTexture->ID 
                     : whiteTextureID;

        if (tex != lastBoundTexture)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            lastBoundTexture = tex;
        }

        // Normal/emission maps são opcionais e só são carregados quando o
        // material realmente os referencia. Assim o caminho padrão continua
        // sem custo de textura extra quando a iluminação avançada está fora.
        if (advancedLightingEnabled && !material->NormalMapPath.empty() && !material->NormalTexture)
            material->NormalTexture = TextureCache::Load(material->NormalMapPath, false);
        if (advancedLightingEnabled && !material->EmissionTexturePath.empty() && !material->EmissionTexture)
            material->EmissionTexture = TextureCache::Load(material->EmissionTexturePath);
        const bool hasNormalMap = advancedLightingEnabled && material->PBRParametersEnabled &&
            material->NormalTexture && material->NormalTexture->Loaded;
        const bool hasEmissionMap = advancedLightingEnabled && material->PBRParametersEnabled &&
            material->EmissionTexture && material->EmissionTexture->Loaded;
        if (advancedLightingEnabled)
        {
            const GLuint normalTex = hasNormalMap ? material->NormalTexture->ID : 0;
            if (normalTex != lastBoundNormal)
            {
                glActiveTexture(GL_TEXTURE15);
                glBindTexture(GL_TEXTURE_2D, normalTex);
                lastBoundNormal = normalTex;
            }
            const GLuint emissionTex = hasEmissionMap ? material->EmissionTexture->ID : 0;
            if (emissionTex != lastBoundEmission)
            {
                glActiveTexture(GL_TEXTURE16);
                glBindTexture(GL_TEXTURE_2D, emissionTex);
                lastBoundEmission = emissionTex;
            }
            if (lastBoundNormal != 0 || lastBoundEmission != 0)
            {
                glActiveTexture(GL_TEXTURE0);
            }
            shader.SetMaterialTextures(15, hasNormalMap, material->NormalStrength,
                                       16, hasEmissionMap);
        }

        // A cor pertence ao objeto/mesh. O material altera somente a resposta
        // à iluminação: rugosidade, metalness, brilho e emissão.
        const Vector3& meshColor = currentEntity.GetColorComponent().Color;
        shader.SetColor(meshColor.X, meshColor.Y, meshColor.Z);
        const float presetAlpha = material->Type == MaterialType::Glass ? 0.35f : 1.0f;
        shader.SetOpacity(currentEntity.Opacity *
            (material->PBRParametersEnabled ? std::clamp(material->Alpha, 0.0f, 1.0f) : presetAlpha));

        float metallic = 0.0f;
        float roughness = 0.45f;
        float specular = 0.5f;
        float emissionStrength = 0.0f;
        GetMaterialLighting(material->Type, metallic, roughness, specular, emissionStrength);
        if (material->PBRParametersEnabled)
        {
            metallic = std::clamp(material->Metallic, 0.0f, 1.0f);
            roughness = std::clamp(material->Roughness, 0.02f, 1.0f);
            specular = std::clamp(material->Specular, 0.0f, 1.0f);
            emissionStrength = std::max(0.0f, material->EmissionStrength);
        }
        // Envia as propriedades de iluminação do material garantindo que nunca vazem para outros objetos
        shader.SetMaterialProperties(metallic, roughness, specular, emissionStrength);

        const bool isMirror = (material->Type == MaterialType::Mirror);
        if (!m_IsRenderingReflection && isMirror && m_ReflectionTexture != 0)
        {
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, m_ReflectionTexture);
            glActiveTexture(GL_TEXTURE0);
            shader.SetMirrorReflection(true, 7,
                std::clamp(material->MirrorIntensity, 0.0f, 1.0f),
                std::clamp(material->MirrorBlur,      0.0f, 1.0f));
            shader.SetMirrorMode(1);
        }
        else if (isMirror)
        {
            shader.SetMirrorReflection(false, 7, 1.0f, 0.0f);
            shader.SetMirrorMode(2); // Ultra Performance / Chrome mode apenas para o espelho
        }
        else
        {
            shader.SetMirrorReflection(false, 7, 0.0f, 0.0f);
            shader.SetMirrorMode(0); // Desativado para qualquer outro objeto!
        }

        // Aplicar Local Light se a entidade tiver
        if (currentEntity.HasLocalLight())
        {
            const LocalLightComponent& local = currentEntity.GetLocalLightComponent();
            shader.SetLocalLightEnabled(local.Enabled);
            shader.SetLocalLightInfluence(
                local.SunLightInfluence,
                local.ArtificialLightInfluence,
                local.AmbientLightInfluence
            );
            shader.SetLocalLightShadow(
                local.ReceiveShadows,
                local.ShadowStrength,
                local.ShadowBias
            );
            shader.SetLocalLightReflection(
                local.EnableReflections,
                local.ReflectionStrength
            );
            shader.SetLocalLightAO(
                local.EnableAO,
                local.AOStrength
            );
        }
        else
        {
            // Valores padrão quando não tem Local Light
            shader.SetLocalLightEnabled(false);
            shader.SetLocalLightShadow(true, 1.0f, 0.005f);
        }

        // Shadows apply somente a objetos reais da cena.
        shader.SetUseShadows(useShadows && !isAxis);
        // Sprites e formas 2D são desenhados como cores sólidas. Não devem
        // depender de luz, ambiente ou sombra para permanecerem visíveis.
        if (editorSelectionUnlitLocation >= 0)
            glUniform1i(editorSelectionUnlitLocation, 0);

        if (isAxis)
        {
            glLineWidth(2.0f);
        }
        else
        {
            glLineWidth(1.0f);
        }

        // Skip drawing editor axis/grid entities because GridRenderer handles them (to avoid visual duplication)
        if (isAxis || isGrid)
        {
            continue;
        }

        // Alpha de entidade e de material PBR compartilha o caminho de
        // transparência existente. Não escreve depth durante o blend para não
        // esconder meshes opacos que já foram desenhados.
        const float materialAlpha = material->PBRParametersEnabled
            ? std::clamp(material->Alpha, 0.0f, 1.0f)
            : (material->Type == MaterialType::Glass ? 0.35f : 1.0f);
        const bool transparentEntity = currentEntity.Opacity * materialAlpha < 0.999f;
        if (transparentEntity)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }

        const bool is2DQuad = (meshComponent.MeshData->GetVertexCount() <= 6);
        const bool shouldCull = !editor2DMode && !is2DQuad && !transparentEntity;
        if (shouldCull != currentCullEnabled)
        {
            if (shouldCull)
            {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }
            currentCullEnabled = shouldCull;
        }

        // Draw all mesh tiles with proper material & texture
        for (const auto& tileMat : tileModels)
        {
            shader.SetModelMatrix(tileMat);
            const auto& faceRanges = meshComponent.MeshData->GetFaceRanges();
            if (materialComponent.TextureMode == FaceTextureMode::IndividualFaces && !faceRanges.empty())
            {
                unsigned int runStart = faceRanges.front().FirstVertex;
                unsigned int runCount = 0;
                GLuint runTexture = 0;
                bool haveRun = false;
                auto flushFaceRun = [&]() {
                    if (haveRun && runCount > 0)
                    {
                        if (runTexture != lastBoundTexture)
                        {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, runTexture);
                            lastBoundTexture = runTexture;
                        }
                        meshComponent.MeshData->DrawRange(runStart, runCount);
                        ++m_LastStats.DrawCalls;
                        m_LastStats.Triangles += runCount / 3;
                    }
                };

                for (size_t faceIndex = 0; faceIndex < faceRanges.size(); ++faceIndex)
                {
                    GLuint faceTexture = whiteTextureID;
                    const auto path = materialComponent.FaceTexturePaths.find(static_cast<uint32_t>(faceIndex));
                    if (path != materialComponent.FaceTexturePaths.end() && !path->second.empty())
                    {
                        auto& cachedTexture = materialComponent.FaceTextures[static_cast<uint32_t>(faceIndex)];
                        if (!cachedTexture)
                            cachedTexture = TextureCache::Load(path->second);
                        if (cachedTexture && cachedTexture->Loaded)
                            faceTexture = cachedTexture->ID;
                    }

                    const auto& face = faceRanges[faceIndex];
                    if (!haveRun)
                    {
                        runStart = face.FirstVertex;
                        runCount = face.VertexCount;
                        runTexture = faceTexture;
                        haveRun = true;
                    }
                    else if (faceTexture == runTexture &&
                        face.FirstVertex == runStart + runCount)
                    {
                        runCount += face.VertexCount;
                    }
                    else
                    {
                        flushFaceRun();
                        runStart = face.FirstVertex;
                        runCount = face.VertexCount;
                        runTexture = faceTexture;
                    }
                }
                flushFaceRun();
            }
            else
            {
                meshComponent.MeshData->Draw();
                ++m_LastStats.DrawCalls;
                m_LastStats.Triangles += meshComponent.MeshData->GetVertexCount() / 3;
            }
        }
        if (editor2DMode)
            glUniform1i(glGetUniformLocation(shader.GetProgramID(), "u_EditorSelectionUnlit"), 0);

        if (isAxis)
        {
            glLineWidth(1.0f);
        }

        // Draw selection outline AFTER all tiles are drawn (avoids turning tiles yellow)
        if ((isActiveSelection || isMultiSelection) && !isGrid && !isAxis)
        {
            drawSelectionOutline(currentEntity, model);
            lastBoundTexture = whiteTextureID;
        }
        if (transparentEntity)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

    }

    if (currentCullEnabled)
    {
        glDisable(GL_CULL_FACE);
    }

    // Partículas 2D em mundo 3D: sprites circulares billboard que acompanham
    // a câmera, simulados deterministicamente a partir do tempo do runtime.
    // O mesh é compartilhado e o limite por emissor mantém o custo previsível.
    if (!editor2DMode)
    {
        const glm::mat4 inverseView = glm::inverse(view);
        const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView[0]));
        const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView[1]));
        const glm::vec3 cameraForward = glm::normalize(glm::vec3(inverseView[2]));
        const double clockTime = glfwGetTime();
        const float elapsed = static_cast<float>(std::max(0.0, clockTime));
        glActiveTexture(GL_TEXTURE0);
        shader.SetUVMode(0, 1.0f, glm::vec3(1.0f), glm::vec2(1.0f), glm::vec4(1.0f));
        shader.SetMaterialProperties(0.0f, 1.0f, 0.0f, 0.0f);
        shader.SetUseShadows(false);
        const GLint particleUnlitLoc = glGetUniformLocation(shader.GetProgramID(), "u_EditorSelectionUnlit");
        if (particleUnlitLoc >= 0)
            glUniform1i(particleUnlitLoc, 1);
        const GLint particleTextureLoc = glGetUniformLocation(shader.GetProgramID(), "u_ParticleTextured");
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        size_t renderedParticleCount = 0;
        constexpr size_t kSceneParticleDrawBudget = 4096;
        for (Entity& emitter : const_cast<std::vector<Entity>&>(entities))
        {
            if (!emitter.Active || !emitter.HasParticleSystem())
                continue;
            ParticleSystemComponent& system = emitter.GetParticleSystem();
            system.Clamp();
            if (!system.Enabled)
                continue;

            const glm::mat4 emitterWorld = emitter.GetTransform().WorldMatrix;
            const glm::vec3 origin = glm::vec3(emitterWorld * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const glm::quat emitterRotation = glm::quat_cast(glm::mat3(emitterWorld));
            const float rate = std::max(0.1f, system.EmissionRate);
            const int burstCount = std::max(1, system.BurstCount);
            const float cycle = std::max(1.0f, std::ceil(static_cast<float>(system.MaxParticles) /
                static_cast<float>(burstCount))) / rate;
            const float interval = 1.0f / rate;
            const float lifetime = std::max(0.05f, system.Lifetime);
            const float seed = static_cast<float>(emitter.GetID() % 1000003u);
            GLuint particleTexture = whiteTextureID;
            if (!system.TexturePath.empty())
            {
                auto& texture = m_ParticleTextures[system.TexturePath];
                if (!texture || !texture->Loaded)
                    texture = TextureCache::Load(system.TexturePath);
                if (texture && texture->Loaded)
                    particleTexture = texture->ID;
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, particleTexture);
            if (particleUnlitLoc >= 0)
                glUniform1i(particleUnlitLoc, system.Lit ? 0 : 1);
            shader.SetMaterialProperties(0.0f, 0.85f, 0.0f, 0.0f);
            if (particleTextureLoc >= 0)
                glUniform1i(particleTextureLoc, system.TexturePath.empty() ? 0 : 1);
            glBlendFunc(GL_SRC_ALPHA, system.Glow > 0.001f ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            Mesh& shapeMesh = m_ParticleShapeMeshes[static_cast<size_t>(system.Shape)];

            for (int slot = 0; slot < system.MaxParticles; ++slot)
            {
                if (renderedParticleCount >= kSceneParticleDrawBudget)
                    break;
                const int burstIndex = slot / burstCount;
                float age = std::fmod(elapsed - static_cast<float>(burstIndex) * interval, cycle);
                if (age < 0.0f) age += cycle;
                const auto random01 = [&](float salt)
                {
                    const float value = std::sin((static_cast<float>(slot) + 1.0f) * salt + seed) * 43758.5453f;
                    return value - std::floor(value);
                };
                const float particleLifetime = std::max(0.05f, lifetime *
                    (1.0f + (random01(51.731f) * 2.0f - 1.0f) * system.LifetimeVariation));
                if (age >= particleLifetime) continue;
                const float t = std::clamp(age / particleLifetime, 0.0f, 1.0f);
                const float rx = random01(12.9898f) * 2.0f - 1.0f;
                const float rz = random01(78.233f) * 2.0f - 1.0f;
                glm::vec3 localDirection(rx * system.Spread, 1.0f,
                                         rz * system.Spread);
                localDirection = glm::normalize(localDirection);
                const float speed = system.Speed * (1.0f + (random01(39.425f) * 2.0f - 1.0f) * system.SpeedVariation);
                const glm::vec3 velocity = emitterRotation * localDirection * speed;
                glm::vec3 spawnOffset(0.0f);
                if (system.EmissionShape == 1)
                {
                    const float radius = std::cbrt(random01(17.219f));
                    const float angle = random01(31.117f) * 6.2831853f;
                    const float y = random01(47.113f) * 2.0f - 1.0f;
                    const float radial = std::sqrt(std::max(0.0f, 1.0f - y * y));
                    spawnOffset = emitterRotation * (glm::vec3(std::cos(angle) * radial, y,
                        std::sin(angle) * radial) * radius * system.StartSize);
                }
                else if (system.EmissionShape == 2)
                {
                    spawnOffset = emitterRotation * glm::vec3(rx, random01(83.73f) * 2.0f - 1.0f, rz) * system.StartSize;
                }
                const glm::vec3 acceleration = system.Gravity + system.Wind;
                glm::vec3 travel;
                if (system.Drag > 0.0001f)
                {
                    const float decay = std::exp(-system.Drag * age);
                    travel = velocity * ((1.0f - decay) / system.Drag) +
                        acceleration * (age / system.Drag - (1.0f - decay) / (system.Drag * system.Drag));
                }
                else
                    travel = velocity * age + 0.5f * acceleration * age * age;
                const glm::vec3 turbulence(std::sin(age * 8.1f + seed + slot) * system.Turbulence,
                    std::sin(age * 6.7f + seed * 0.3f + slot * 1.7f) * system.Turbulence,
                    std::cos(age * 7.3f + seed * 0.7f + slot) * system.Turbulence);
                const glm::vec3 particlePosition = origin + spawnOffset + travel +
                    turbulence * (age * age * 0.5f);
                float size;
                if (system.MidSize > 0.0f)
                    size = t < 0.5f ? glm::mix(system.StartSize, system.MidSize, t * 2.0f)
                                    : glm::mix(system.MidSize, system.EndSize, (t - 0.5f) * 2.0f);
                else
                    size = glm::mix(system.StartSize, system.EndSize, t);
                const glm::vec4 color = system.UseMidColor
                    ? (t < 0.5f
                        ? glm::mix(system.StartColor, system.MidColor, t * 2.0f)
                        : glm::mix(system.MidColor, system.EndColor, (t - 0.5f) * 2.0f))
                    : glm::mix(system.StartColor, system.EndColor, t);
                const float angle = system.StartRotation + system.RotationSpeed * age;
                const glm::vec3 rotatedRight = cameraRight * std::cos(angle) + cameraUp * std::sin(angle);
                const glm::vec3 rotatedUp = cameraUp * std::cos(angle) - cameraRight * std::sin(angle);

                glm::mat4 model(1.0f);
                model[0] = glm::vec4(rotatedRight * size, 0.0f);
                model[1] = glm::vec4(rotatedUp * size, 0.0f);
                model[2] = glm::vec4(cameraForward, 0.0f);
                model[3] = glm::vec4(particlePosition, 1.0f);
                shader.SetModelMatrix(model);
                const float brightness = 1.0f + system.Glow;
                shader.SetColor(color.r * brightness, color.g * brightness, color.b * brightness);
                shader.SetOpacity(color.a);
                shapeMesh.Draw();
                ++m_LastStats.DrawCalls;
                m_LastStats.Triangles += shapeMesh.GetVertexCount() / 3;
                ++renderedParticleCount;
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (particleUnlitLoc >= 0)
            glUniform1i(particleUnlitLoc, 0);
        if (particleTextureLoc >= 0)
            glUniform1i(particleTextureLoc, 0);
    }

    if (editor2DMode && depthWasEnabled)
    {
        glEnable(GL_DEPTH_TEST);
    }

    // O grid e os eixos são uma sobreposição de editor, não entidades da cena.
    // Desenhá-los após os meshes mantém a cena visível mesmo com ambos
    // desligados e deixa o depth test decidir corretamente o que fica à frente.
    if (showGrid || showAxis)
    {
        const glm::mat4 inverseView = glm::inverse(view);
        const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
        m_GridRenderer.Render(
            view,
            projection,
            cameraPosition,
            showAxis,
            showGrid,
            gridRenderDistance);
    }

    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    m_LastStats.RenderTimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - renderStart).count();
    if (m_GpuTimerQuery != 0 && m_GpuQueryInFlight)
        glEndQuery(GL_TIME_ELAPSED);

    if (PerformanceMonitor::IsFrameCaptureEnabled())
    {
        PerformanceMonitor::RenderFrameSample capturedFrame;
        capturedFrame.DrawCalls = m_LastStats.DrawCalls;
        capturedFrame.Triangles = m_LastStats.Triangles;
        capturedFrame.VisibleEntities = m_LastStats.VisibleEntities;
        capturedFrame.FrustumCulledEntities = m_LastStats.FrustumCulledEntities;
        capturedFrame.ShadowDrawCalls = m_LastStats.ShadowDrawCalls;
        capturedFrame.ShadowTriangles = m_LastStats.ShadowTriangles;
        capturedFrame.RenderTimeMs = m_LastStats.RenderTimeMs;
        capturedFrame.ShadowTimeMs = m_LastStats.ShadowTimeMs;
        capturedFrame.GpuTimeMs = m_LastStats.GpuTimeMs;
        capturedFrame.GpuTimeAvailable = m_LastStats.GpuTimeAvailable;
        PerformanceMonitor::RecordRenderFrame(capturedFrame);
    }
}

