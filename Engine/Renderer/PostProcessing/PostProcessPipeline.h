#pragma once

#include "PostProcessEffect.h"
#include "../../../../Renderer/PingPongFramebuffer/PingPongFramebuffer.h"
#include <vector>
#include <memory>

class PostProcessPipeline
{
public:
    PostProcessPipeline();
    ~PostProcessPipeline();

    bool Init();
    void Destroy();

    void SetQuality(GraphicsQuality q);
    void Resize(int w, int h);

    void AddEffect(std::unique_ptr<PostProcessEffect> e);
    void ReorderPasses(const std::vector<size_t>& newOrder);

    size_t EffectCount() const { return OrderedEffects.size(); }
    PostProcessEffect* GetEffect(size_t i) { return i < OrderedEffects.size() ? OrderedEffects[i].get() : nullptr; }

    struct Stats { int PassesRun = 0; int DrawCalls = 0; };
    Stats GetLastStats() const { return m_LastStats; }

    Framebuffer& Execute(Framebuffer& hdrSrc, const BlendedSettings& settings, int viewportW, int viewportH);

private:
    std::vector<std::unique_ptr<PostProcessEffect>> OrderedEffects;
    PingPongFramebuffer m_PingPong;
    FramebufferSpec m_LastSpec;
    Stats m_LastStats{};
    bool m_Inited = false;
};
