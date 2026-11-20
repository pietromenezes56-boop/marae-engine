#include "PostProcessingPipeline.h"
#include "../../../Renderer/FullscreenQuad/FullscreenQuad.h"
#include <glad/glad.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>

namespace
{
    GLuint CreateHDRTexture(int w, int h)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    }

    GLuint CreateDepthTexture(int w, int h)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    }

    GLuint CreateHalfSizeHDR(int w, int h)
    {
        int hw = std::max(1, w / 2);
        int hh = std::max(1, h / 2);
        return CreateHDRTexture(hw, hh);
    }
}

PostProcessingPipeline::PostProcessingPipeline() = default;

PostProcessingPipeline::~PostProcessingPipeline()
{
    Destroy();
}

void PostProcessingPipeline::SetQuality(QualityLevel q)
{
    m_Quality = q;
}

bool PostProcessingPipeline::Init(int width, int height)
{
    Destroy();
    if (!m_Shader.Create())
    {
        std::cerr << "[PostProcessingPipeline] Falha ao compilar shaders de post-processamento." << std::endl;
        return false;
    }
    const bool ok = CreateBuffers(width, height);
    FullscreenQuad::Instance().Init();
    m_Valid = ok;
    return ok;
}

void PostProcessingPipeline::Destroy()
{
    m_Shader.Destroy();
    DestroyBuffers();
    m_Width = 0;
    m_Height = 0;
    m_Valid = false;
}

void PostProcessingPipeline::DestroyBuffers()
{
    auto delTex = [](GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    auto delFBO = [](GLuint& f) { if (f) { glDeleteFramebuffers(1, &f); f = 0; } };

    delFBO(m_SceneFBO);    delTex(m_SceneHDR);    delTex(m_SceneDepth);
    delFBO(m_TAAHistoryFBO); delTex(m_TAAHistoryColor);
    delFBO(m_TAAHistoryDepthFBO); delTex(m_TAAHistoryDepth);
    m_TAAHistoryValid = false;

    for (int i = 0; i < 2; ++i) { delFBO(m_PingPongFBO[i]); delTex(m_PingPongTex[i]); }
    delFBO(m_BloomExtractFBO); delTex(m_BloomExtractTex);
    for (int i = 0; i < 2; ++i) { delFBO(m_BloomFBO[i]); delTex(m_BloomTex[i]); }
}

bool PostProcessingPipeline::CreateBuffers(int width, int height)
{
    DestroyBuffers();
    m_Width  = std::max(1, width);
    m_Height = std::max(1, height);

    // 1. Scene HDR target RGBA16F + Depth32F
    {
        m_SceneHDR = CreateHDRTexture(m_Width, m_Height);
        m_SceneDepth = CreateDepthTexture(m_Width, m_Height);
        glGenFramebuffers(1, &m_SceneFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneHDR, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, m_SceneDepth, 0);
        const GLenum db = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &db);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            std::cerr << "[PostProcessingPipeline] Scene FBO incomplete." << std::endl;
            return false;
        }
    }

    // 2. Ping-pong RGBA16F (full-size) for intermediate passes
    for (int i = 0; i < 2; ++i)
    {
        m_PingPongTex[i] = CreateHDRTexture(m_Width, m_Height);
        glGenFramebuffers(1, &m_PingPongFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_PingPongFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PingPongTex[i], 0);
        const GLenum db = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &db);
    }

    // 3. Bloom downsampled targets
    {
        m_BloomExtractTex = CreateHalfSizeHDR(m_Width, m_Height);
        glGenFramebuffers(1, &m_BloomExtractFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomExtractFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BloomExtractTex, 0);
        const GLenum db = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &db);

        for (int i = 0; i < 2; ++i)
        {
            m_BloomTex[i] = CreateHalfSizeHDR(m_Width, m_Height);
            glGenFramebuffers(1, &m_BloomFBO[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, m_BloomFBO[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BloomTex[i], 0);
            const GLenum db2 = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &db2);
        }
        for (int m = 0; m < kBloomMips; ++m)
        {
            int div = 1 << (m + 1);
            m_BloomMipWidth[m]  = std::max(1, m_Width  / div);
            m_BloomMipHeight[m] = std::max(1, m_Height / div);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool PostProcessingPipeline::EnsureTemporalBuffers()
{
    if (m_TAAHistoryFBO != 0 && m_TAAHistoryDepthFBO != 0 &&
        m_TAAHistoryColor != 0 && m_TAAHistoryDepth != 0)
        return true;

    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    m_TAAHistoryColor = CreateHDRTexture(m_Width, m_Height);
    glGenFramebuffers(1, &m_TAAHistoryFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_TAAHistoryFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_TAAHistoryColor, 0);
    const GLenum historyColorBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &historyColorBuffer);
    const bool historyColorComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    m_TAAHistoryDepth = CreateDepthTexture(m_Width, m_Height);
    glGenFramebuffers(1, &m_TAAHistoryDepthFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_TAAHistoryDepthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_TAAHistoryDepth, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool historyDepthComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    if (!historyColorComplete || !historyDepthComplete)
    {
        std::cerr << "[PostProcessingPipeline] TAA history framebuffer unavailable; temporal AA will be skipped." << std::endl;
        ReleaseTemporalHistory();
        return false;
    }
    m_TAAHistoryValid = false;
    return true;
}

void PostProcessingPipeline::ReleaseTemporalHistory()
{
    if (m_TAAHistoryFBO != 0) glDeleteFramebuffers(1, &m_TAAHistoryFBO);
    if (m_TAAHistoryColor != 0) glDeleteTextures(1, &m_TAAHistoryColor);
    if (m_TAAHistoryDepthFBO != 0) glDeleteFramebuffers(1, &m_TAAHistoryDepthFBO);
    if (m_TAAHistoryDepth != 0) glDeleteTextures(1, &m_TAAHistoryDepth);
    m_TAAHistoryFBO = 0;
    m_TAAHistoryColor = 0;
    m_TAAHistoryDepthFBO = 0;
    m_TAAHistoryDepth = 0;
    m_TAAHistoryValid = false;
}

void PostProcessingPipeline::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (width == m_Width && height == m_Height) return;
    CreateBuffers(width, height);
}

void PostProcessingPipeline::SetLUTTexture(GLuint texID)
{
    m_LUTTex = texID;
}

void PostProcessingPipeline::DrawFullscreen()
{
    FullscreenQuad::Instance().Draw();
}

void PostProcessingPipeline::BlitToTarget(GLuint sourceTex, GLuint targetFBO)
{
    GLint prevFBO = 0;
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, vp);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, m_Width, m_Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Usar tone map + gamma fallback simples via shader dedicado
    m_Shader.UseToneMapOnlyPass();
    // Settings são passados diretamente? No fallback, faz blit neutral:
    // Usar composite pass como 'blit neutro' é mais seguro — porém para
    // um blit simples quando pp está desligado, usamos um tone map only
    // com tone desabilitado e gamma 1.0 se não houver configuração.
    // Em vez disso, faremos blit de textura via FBO.

    // Revert: blit de framebuffer clona o buffer sem filtragem apropriada;
    // melhor usar shader fullscreen para garantir conversão correta.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    DrawFullscreen();
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void PostProcessingPipeline::Execute(const BlendedSettings& settings,
                                    const glm::mat4& view,
                                    const glm::mat4& projection,
                                    const glm::vec3& cameraPosition,
                                    GLuint targetFBO,
                                    GLuint sceneInputTexture)
{
    if (!m_Valid) return;

    GLuint compositeInput = sceneInputTexture != 0 ? sceneInputTexture : m_SceneHDR;
    BlendedSettings compositeSettings = settings;
    if (settings.Global.TemporalAAEnabled && EnsureTemporalBuffers())
    {
        // Temporal accumulation must happen in scene-linear color before tone
        // mapping and grading. Keep its result in ping-pong target 1, then run
        // the visual filters over that resolved image exactly once.
        ExecuteFogOnly(settings.Global, view, projection, cameraPosition, m_PingPongFBO[1]);
        compositeInput = m_PingPongTex[1];
        compositeSettings.Global.FogEnabled = false;
        compositeSettings.Global.ScreenSpaceAOEnabled = false;
        compositeSettings.Global.ScreenSpaceReflectionsEnabled = false;
        compositeSettings.Global.TemporalAAEnabled = false;
    }

    GLint prevFBO = 0;
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float vw = (float)m_Width;
    const float vh = (float)m_Height;

    // Determine se PP completo é necessário
    bool hasBloom = settings.Bloom.Enabled && settings.Bloom.Intensity > 0.0001f;

    // ---- PASSO 1: BLOOM EXTRACT + BLUR ----
    GLuint bloomResultTex = 0;
    if (hasBloom)
    {
        int hw = std::max(1, m_Width  / 2);
        int hh = std::max(1, m_Height / 2);

        // 1a. Extrair bright pass -> ExtractTex
        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomExtractFBO);
        glViewport(0, 0, hw, hh);
        m_Shader.UseBloomExtractPass();
        m_Shader.SetBloom(settings.Bloom);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, compositeInput);
        DrawFullscreen();

        // 1b. Blur gaussiano separável em pares (H + V) por mip
        GLuint srcTex  = m_BloomExtractTex;
        GLuint srcW = hw, srcH = hh;
        int passes = 2;
        switch (m_Quality)
        {
            case QualityLevel::Low:    passes = 1; break;
            case QualityLevel::Medium: passes = 2; break;
            case QualityLevel::High:   passes = 3; break;
            case QualityLevel::Ultra:  passes = 4; break;
        }
        GLuint readTex = srcTex;
        for (int p = 0; p < passes; ++p)
        {
            // Horizontal -> BloomFBO[0]
            glBindFramebuffer(GL_FRAMEBUFFER, m_BloomFBO[0]);
            glViewport(0, 0, srcW, srcH);
            m_Shader.UseBloomBlurPass(true);
            m_Shader.SetViewportSize((float)srcW, (float)srcH);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, readTex);
            DrawFullscreen();

            // Vertical -> BloomFBO[1]
            glBindFramebuffer(GL_FRAMEBUFFER, m_BloomFBO[1]);
            glViewport(0, 0, srcW, srcH);
            m_Shader.UseBloomBlurPass(false);
            m_Shader.SetViewportSize((float)srcW, (float)srcH);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_BloomTex[0]);
            DrawFullscreen();

            readTex = m_BloomTex[1];
        }
        bloomResultTex = readTex;
    }

    // ---- PASSO 2: COMPOSITE FINAL (ColorGrading, LUT, TM, Vignette, Grain, Chromatic, DoF, Sharpen, LensDistort) ----
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, m_Width, m_Height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_Shader.UseCompositePass();
    m_Shader.SetFogOnlyPass(false);
    m_Shader.SetViewportSize(vw, vh);
    m_Shader.SetElapsedTime(m_ElapsedTime);
    m_Shader.SetFogCamera(glm::inverse(projection), glm::inverse(view), projection, cameraPosition);
    m_Shader.SetTemporalHistoryValid(false);
    m_Shader.SetTemporalHistoryUVOffset(m_TAAHistoryUVOffset);
    m_Shader.SetAdvancedEffectQuality(static_cast<int>(m_Quality));

    glm::vec3 sunDir(compositeSettings.Global.SunDirection.X, compositeSettings.Global.SunDirection.Y, compositeSettings.Global.SunDirection.Z);
    if (glm::length(sunDir) > 0.0001f)
        sunDir = glm::normalize(sunDir);
    else
        sunDir = glm::vec3(-0.4f, -1.0f, -0.4f);

    glm::vec3 sunWorldPos = cameraPosition - sunDir * 1000.0f;
    glm::vec4 clipPos = projection * view * glm::vec4(sunWorldPos, 1.0f);

    glm::vec2 sunScreenPos(0.5f, 0.5f);
    bool sunInFront = (clipPos.w > 0.001f);
    if (sunInFront)
    {
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        sunScreenPos = glm::vec2(ndc.x, ndc.y) * 0.5f + 0.5f;
    }
    m_Shader.SetGodRaysSunScreen(sunScreenPos, sunInFront);

    // Calcula altitude do sol: dot com vetor "cima" negado (SunDirection aponta para baixo)
    // SunDirection = direcao em que o sol ilumina (aponta DA fonte para a cena).
    // altitude = quanto o sol esta acima do horizonte: 0 = horizonte, 1 = zenite.
    if (compositeSettings.Global.GodRaysEnabled)
    {
        // O sol fica na direcao oposta a SunDirection (que aponta para a cena)
        const glm::vec3 sunUp = -sunDir; // vetor apontando para a fonte luminosa
        float sunAltitude = glm::clamp(sunUp.y, 0.0f, 1.0f); // componente Y normalizada
        BlendedSettings& mutableSettings = const_cast<BlendedSettings&>(compositeSettings);
        mutableSettings.Global.GodRaysSunAltitude = sunAltitude;
    }

    m_Shader.SetGlobalLighting(compositeSettings.Global);
    m_Shader.SetColorGrading(compositeSettings.Color);
    m_Shader.SetToneMapping(compositeSettings.Tone);
    m_Shader.SetLUT(compositeSettings.Lut);
    m_Shader.SetBloom(compositeSettings.Bloom);
    m_Shader.SetVignette(compositeSettings.Vignette);
    m_Shader.SetChromatic(compositeSettings.Chromatic);
    m_Shader.SetSharpen(compositeSettings.Sharpen);
    m_Shader.SetLensDistort(compositeSettings.LensDistortion);
    m_Shader.SetFilmGrain(compositeSettings.FilmGrain);
    m_Shader.SetDoF(compositeSettings.DoF);
    m_Shader.SetMotionBlur(compositeSettings.MotionBlur);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, compositeInput);

    if (hasBloom && bloomResultTex != 0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomResultTex);
    }
    else
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (m_LUTTex != 0 && settings.Lut.Enabled)
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_LUTTex);
    }
    else
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_SceneDepth);

    glActiveTexture(GL_TEXTURE0);
    DrawFullscreen();

    // Limpeza de estado
    for (int i = 0; i < 4; ++i)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Restauro de estado
    if (depthTest) glEnable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);
    if (cull)      glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void PostProcessingPipeline::ExecuteFogOnly(const GlobalLightingSettings& lighting,
                                           const glm::mat4& view,
                                           const glm::mat4& projection,
                                           const glm::vec3& cameraPosition,
                                           GLuint targetFBO)
{
    const bool anySceneEffect = lighting.FogEnabled || lighting.ScreenSpaceAOEnabled ||
        lighting.ScreenSpaceReflectionsEnabled || lighting.TemporalAAEnabled;
    if (!m_Valid || !anySceneEffect || m_SceneHDR == 0 || m_SceneDepth == 0)
        return;

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    const bool useTAA = lighting.TemporalAAEnabled && EnsureTemporalBuffers();
    const GLuint compositeFBO = useTAA ? m_PingPongFBO[0] : targetFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, compositeFBO);
    glViewport(0, 0, m_Width, m_Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_Shader.UseCompositePass();
    m_Shader.SetFogOnlyPass(true);
    m_Shader.SetViewportSize(static_cast<float>(m_Width), static_cast<float>(m_Height));
    m_Shader.SetGlobalLighting(lighting);
    m_Shader.SetFogCamera(glm::inverse(projection), glm::inverse(view), projection, cameraPosition);
    m_Shader.SetTemporalHistoryValid(useTAA && m_TAAHistoryValid);
    m_Shader.SetTemporalHistoryUVOffset(m_TAAHistoryUVOffset);
    m_Shader.SetAdvancedEffectQuality(static_cast<int>(m_Quality));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_SceneHDR);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_SceneDepth);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_TAAHistoryColor);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_TAAHistoryDepth);
    glActiveTexture(GL_TEXTURE0);
    DrawFullscreen();

    if (useTAA)
    {
        // Publish the filtered image, then preserve its color and current depth
        // for next frame. Depth rejection limits ghosting without motion vectors.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, compositeFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
        glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_TAAHistoryFBO);
        glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_SceneFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_TAAHistoryDepthFBO);
        glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        m_TAAHistoryValid = true;
    }
    else
    {
        m_TAAHistoryValid = false;
    }

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}
