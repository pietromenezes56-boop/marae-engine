#pragma once

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class PerformanceMonitor
{
public:
    struct ScriptTiming
    {
        std::string Name;
        double FrameTimeMs = 0.0;
        unsigned int Calls = 0;
        uint64_t EntityID = 0;
    };

    struct RenderFrameSample
    {
        unsigned long long Frame = 0;
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

    // Seções de subsistema medidas por frame (ms). Os valores são acumulados
    // durante o frame e fechados na próxima chamada de Update().
    enum class FrameSection : int
    {
        DemoGames = 0,
        Scripting,
        Physics,
        AI,
        Animation,
        Audio,
        UIInteraction,
        SceneRender,
        Count
    };

    static void Initialize();
    static void Update();
    static void BeginFrameSections();
    static void RecordFrameSection(FrameSection section, double elapsedMs);
    static const char* GetFrameSectionName(FrameSection section);
    // Copia os tempos do último frame completo e a média móvel para arrays
    // de capacidade >= (size_t)FrameSection::Count.
    static void GetFrameSectionTimes(double* lastFrameMs, double* averageMs, size_t capacity);
    // Estatísticas do GC gerenciado, reportadas pelo runtime C# quando o
    // bridge GetGCStats está disponível. As taxas são coleções por segundo.
    static void RecordGCStats(int gen0Collections, int gen1Collections,
                              int gen2Collections, long long managedHeapBytes);
    static void GetGCStats(double* gen0PerSecond, double* gen1PerSecond,
                           double* gen2PerSecond, double* managedHeapMB);
    // VRAM estimada pela engine (texturas + buffers de mesh registrados no
    // GpuMemoryTracker). Não inclui overhead do driver nem framebuffers.
    static double GetEstimatedVRAMMB();
    static void SetScriptProfilingEnabled(bool enabled);
    static bool IsScriptProfilingEnabled();
    static void BeginScriptProfileFrame();
    static void RecordScriptTime(const std::string& name, double elapsedMs, uint64_t entityId = 0);
    static double GetScriptTimeForEntity(uint64_t entityId);
    static void FinalizeScriptProfileFrame();
    static const std::vector<ScriptTiming>& GetScriptTimings();
    static void SetFrameCaptureEnabled(bool enabled);
    static bool IsFrameCaptureEnabled();
    static void ClearCapturedFrames();
    static void RecordRenderFrame(const RenderFrameSample& sample);
    static const std::vector<RenderFrameSample>& GetCapturedFrames();
    static int RunFrameCaptureRegressionTests();

    static float GetFPS();
    static float GetCPUUsage();
    static float GetWorkingSetMB();
    static float GetPrivateUsageMB();
    static double GetDeltaSeconds();
    static void GetFrameTimeHistory(float* output, size_t capacity);
    static float GetAverageFrameTimeMs();
    static float GetWorstFrameTimeMs();
    static double GetLastMeshCreateTimeMs();
    static void SetLastMeshCreateTimeMs(double timeMs);

private:
    static std::chrono::steady_clock::time_point s_LastFrameTime;
    static std::chrono::steady_clock::time_point s_LastCPUCheck;
    static std::chrono::steady_clock::time_point s_LastMemoryCheck;

    static unsigned long long s_LastKernelTime;
    static unsigned long long s_LastUserTime;

    static float s_FPS;
    static float s_CPUUsage;
    static float s_WorkingSetMB;
    static float s_PrivateUsageMB;

    static double s_LastMeshCreateTimeMs;

    static int s_FrameCount;
    static double s_FPSAccumulator;
    static std::array<float, 240> s_FrameTimeHistory;
    static size_t s_FrameTimeWriteIndex;
    static size_t s_FrameTimeSampleCount;
    static bool s_ScriptProfilingEnabled;
    static std::vector<ScriptTiming> s_ScriptTimings;
    static bool s_FrameCaptureEnabled;
    static unsigned long long s_CapturedFrameNumber;
    static std::vector<RenderFrameSample> s_CapturedFrames;

    static constexpr size_t kSectionCount =
        static_cast<size_t>(FrameSection::Count);
    static std::array<double, kSectionCount> s_SectionPendingMs;
    static std::array<double, kSectionCount> s_SectionLastMs;
    static std::array<double, kSectionCount> s_SectionAvgMs;

    static int s_GCGen0Collections;
    static int s_GCGen1Collections;
    static int s_GCGen2Collections;
    static long long s_GCManagedHeapBytes;
    static bool s_GCStatsValid;
    static std::chrono::steady_clock::time_point s_GCWindowStart;
    static int s_GCPrevGen0;
    static int s_GCPrevGen1;
    static int s_GCPrevGen2;
    static double s_GCGen0Rate;
    static double s_GCGen1Rate;
    static double s_GCGen2Rate;
};
