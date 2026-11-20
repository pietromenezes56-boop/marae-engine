#include "PostProcessPipeline.h"
#include "../../../../Renderer/FullscreenQuad/FullscreenQuad.h"
#include <algorithm>

PostProcessPipeline::PostProcessPipeline()
{
}

PostProcessPipeline::~PostProcessPipeline()
{
    Destroy();
}

bool PostProcessPipeline::Init()
{
    if (m_Inited) return true;

    FramebufferSpec spec;
    spec.Width = 1;
    spec.Height = 1;
    spec.Color = FramebufferSpec::RGBA16F;
    spec.Depth = FramebufferSpec::NONE;
    spec.HasStencil = false;
    spec.GenerateMipmaps = false;
    spec.MinFilter = GL_LINEAR;
    spec.MagFilter = GL_LINEAR;
    spec.WrapS = GL_CLAMP_TO_EDGE;
    spec.WrapT = GL_CLAMP_TO_EDGE;

    m_LastSpec = spec;

    if (!m_PingPong.Create(spec))
        return false;

    for (auto& e : OrderedEffects)
    {
        if (!e->Init(nullptr))
            return false;
    }

    m_Inited = true;
    return true;
}

void PostProcessPipeline::Destroy()
{
    OrderedEffects.clear();
    m_Inited = false;
}

void PostProcessPipeline::SetQuality(GraphicsQuality q)
{
    for (auto& e : OrderedEffects)
        e->SetQuality(q);
}

void PostProcessPipeline::Resize(int w, int h)
{
    m_LastSpec.Width = w;
    m_LastSpec.Height = h;
    m_PingPong.Resize(w, h);
    for (auto& e : OrderedEffects)
        e->OnResize(w, h);
}

void PostProcessPipeline::AddEffect(std::unique_ptr<PostProcessEffect> e)
{
    OrderedEffects.push_back(std::move(e));
}

void PostProcessPipeline::ReorderPasses(const std::vector<size_t>& newOrder)
{
    std::vector<std::unique_ptr<PostProcessEffect>> reordered;
    reordered.reserve(OrderedEffects.size());
    for (size_t idx : newOrder)
    {
        if (idx < OrderedEffects.size())
            reordered.push_back(std::move(OrderedEffects[idx]));
    }
    OrderedEffects = std::move(reordered);
}

Framebuffer& PostProcessPipeline::Execute(Framebuffer& hdrSrc, const BlendedSettings& settings, int viewportW, int viewportH)
{
    if (!m_Inited)
    {
        if (!Init())
            return hdrSrc;
    }

    if (m_LastSpec.Width != viewportW || m_LastSpec.Height != viewportH)
    {
        Resize(viewportW, viewportH);
    }

    m_LastStats = Stats{};

    GLuint prevTex = hdrSrc.GetTexture();
    bool firstPass = true;
    bool anyPassRan = false;

    for (auto& effect : OrderedEffects)
    {
        if (!effect->IsEnabled(settings))
            continue;

        Framebuffer& target = firstPass ? m_PingPong.GetNext() : m_PingPong.GetCurrent();

        effect->Render(prevTex, target, settings, viewportW, viewportH);

        prevTex = target.GetTexture();
        anyPassRan = true;
        m_LastStats.PassesRun++;
        m_LastStats.DrawCalls++;

        if (firstPass)
        {
            m_PingPong.Swap();
            firstPass = false;
        }
    }

    if (!anyPassRan)
    {
        Framebuffer& dst = m_PingPong.GetNext();
        GLuint srcFBO = hdrSrc.GetFBO();
        GLuint dstFBO = dst.GetFBO();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
        glBlitFramebuffer(
            0, 0, viewportW, viewportH,
            0, 0, viewportW, viewportH,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
        );
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        m_PingPong.Swap();
        prevTex = m_PingPong.GetCurrent().GetTexture();
    }

    return m_PingPong.GetCurrent();
}
