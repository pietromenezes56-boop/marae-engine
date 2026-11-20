#pragma once

#include <glad/glad.h>
#include "PostProcessingShader.h"
#include "CommonStructs.h"
#include "../../../Math/Vector3.h"
#include <cstdint>
#include <glm/glm.hpp>

class PostProcessingPipeline
{
public:
    enum class QualityLevel : uint8_t
    {
        Low = 0,
        Medium = 1,
        High = 2,
        Ultra = 3
    };

    PostProcessingPipeline();
    ~PostProcessingPipeline();

    bool Init(int width, int height);
    void Destroy();
    void Resize(int width, int height);
    void SetQuality(QualityLevel q);
    void InvalidateTemporalHistory() { m_TAAHistoryValid = false; }
    void ReleaseTemporalHistory();
    void SetTemporalHistoryUVOffset(const glm::vec2& offset) { m_TAAHistoryUVOffset = offset; }

    GLuint GetSceneFBO()    const { return m_SceneFBO; }
    GLuint GetSceneTex()    const { return m_SceneHDR; }
    GLuint GetSceneDepth()  const { return m_SceneDepth; }
    bool IsValid() const { return m_Valid; }
    int GetWidth()  const { return m_Width; }
    int GetHeight() const { return m_Height; }

    void SetLUTTexture(GLuint texID);
    void SetElapsedTime(float t) { m_ElapsedTime = t; }

    void Execute(const BlendedSettings& settings,
                 const glm::mat4& view,
                 const glm::mat4& projection,
                 const glm::vec3& cameraPosition,
                 GLuint targetFBO = 0,
                 GLuint sceneInputTexture = 0);

    void ExecuteFogOnly(const GlobalLightingSettings& lighting,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const glm::vec3& cameraPosition,
                        GLuint targetFBO = 0);

    PostProcessingShader& Shader() { return m_Shader; }

private:
    bool CreateBuffers(int width, int height);
    bool EnsureTemporalBuffers();
    void DestroyBuffers();

    void BlitToTarget(GLuint sourceTex, GLuint targetFBO);
    void DrawFullscreen();

    int m_Width = 0;
    int m_Height = 0;
    QualityLevel m_Quality = QualityLevel::High;
    float m_ElapsedTime = 0.0f;

    GLuint m_SceneFBO    = 0;
    GLuint m_SceneHDR    = 0;
    GLuint m_SceneDepth  = 0;

    // Temporal history used only when TAA is enabled; invalidated on resize or toggle-off.
    GLuint m_TAAHistoryFBO = 0;
    GLuint m_TAAHistoryColor = 0;
    GLuint m_TAAHistoryDepthFBO = 0;
    GLuint m_TAAHistoryDepth = 0;
    bool m_TAAHistoryValid = false;
    glm::vec2 m_TAAHistoryUVOffset = glm::vec2(0.0f);

    GLuint m_PingPongFBO[2] = { 0, 0 };
    GLuint m_PingPongTex[2] = { 0, 0 };

    static constexpr int kBloomMips = 6;
    GLuint m_BloomExtractFBO = 0;
    GLuint m_BloomExtractTex = 0;
    GLuint m_BloomFBO[2]     = { 0, 0 };
    GLuint m_BloomTex[2]     = { 0, 0 };
    int     m_BloomMipWidth[kBloomMips]  = {};
    int     m_BloomMipHeight[kBloomMips] = {};

    GLuint m_LUTTex = 0;

    PostProcessingShader m_Shader;
    bool m_Valid = false;
};
