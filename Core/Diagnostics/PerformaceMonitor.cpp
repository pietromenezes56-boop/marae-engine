#include "PerformanceMonitor.h"
#include "GpuMemoryTracker.h"

#include <Windows.h>
#include <Psapi.h>

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "Psapi.lib")

std::chrono::steady_clock::time_point
PerformanceMonitor::s_LastFrameTime;

std::chrono::steady_clock::time_point
PerformanceMonitor::s_LastCPUCheck;

std::chrono::steady_clock::time_point
PerformanceMonitor::s_LastMemoryCheck;

unsigned long long
PerformanceMonitor::s_LastKernelTime = 0;

unsigned long long
PerformanceMonitor::s_LastUserTime = 0;

float PerformanceMonitor::s_FPS = 0.0f;
float PerformanceMonitor::s_CPUUsage = 0.0f;
float PerformanceMonitor::s_WorkingSetMB = 0.0f;
float PerformanceMonitor::s_PrivateUsageMB = 0.0f;

double PerformanceMonitor::s_LastMeshCreateTimeMs = 0.0;

int PerformanceMonitor::s_FrameCount = 0;
double PerformanceMonitor::s_FPSAccumulator = 0.0;
std::array<float, 240> PerformanceMonitor::s_FrameTimeHistory{};
size_t PerformanceMonitor::s_FrameTimeWriteIndex = 0;
size_t PerformanceMonitor::s_FrameTimeSampleCount = 0;
bool PerformanceMonitor::s_ScriptProfilingEnabled = false;
std::vector<PerformanceMonitor::ScriptTiming> PerformanceMonitor::s_ScriptTimings;
bool PerformanceMonitor::s_FrameCaptureEnabled = false;
unsigned long long PerformanceMonitor::s_CapturedFrameNumber = 0;
std::vector<PerformanceMonitor::RenderFrameSample> PerformanceMonitor::s_CapturedFrames;

constexpr size_t PerformanceMonitor::kSectionCount;
std::array<double, PerformanceMonitor::kSectionCount> PerformanceMonitor::s_SectionPendingMs{};
std::array<double, PerformanceMonitor::kSectionCount> PerformanceMonitor::s_SectionLastMs{};
std::array<double, PerformanceMonitor::kSectionCount> PerformanceMonitor::s_SectionAvgMs{};

int PerformanceMonitor::s_GCGen0Collections = 0;
int PerformanceMonitor::s_GCGen1Collections = 0;
int PerformanceMonitor::s_GCGen2Collections = 0;
long long PerformanceMonitor::s_GCManagedHeapBytes = 0;
bool PerformanceMonitor::s_GCStatsValid = false;
std::chrono::steady_clock::time_point PerformanceMonitor::s_GCWindowStart;
int PerformanceMonitor::s_GCPrevGen0 = 0;
int PerformanceMonitor::s_GCPrevGen1 = 0;
int PerformanceMonitor::s_GCPrevGen2 = 0;
double PerformanceMonitor::s_GCGen0Rate = 0.0;
double PerformanceMonitor::s_GCGen1Rate = 0.0;
double PerformanceMonitor::s_GCGen2Rate = 0.0;


// ------------------------------------------------------------
// FILETIME -> uint64
// ------------------------------------------------------------

static unsigned long long FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value;

    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;

    return value.QuadPart;
}


// ------------------------------------------------------------
// Initialize
// ------------------------------------------------------------

void PerformanceMonitor::Initialize()
{
    s_LastFrameTime =
        std::chrono::steady_clock::now();

    s_LastCPUCheck =
        std::chrono::steady_clock::now();

    s_LastMemoryCheck =
        std::chrono::steady_clock::now();

    HANDLE process = GetCurrentProcess();

    FILETIME creationTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;

    if (GetProcessTimes(
        process,
        &creationTime,
        &exitTime,
        &kernelTime,
        &userTime))
    {
        s_LastKernelTime =
            FileTimeToUInt64(kernelTime);

        s_LastUserTime =
            FileTimeToUInt64(userTime);
    }

    s_FPS = 0.0f;
    s_CPUUsage = 0.0f;
    s_WorkingSetMB = 0.0f;
    s_PrivateUsageMB = 0.0f;

    s_FrameCount = 0;
    s_FPSAccumulator = 0.0;

    s_LastMeshCreateTimeMs = 0.0;
    s_FrameTimeHistory.fill(0.0f);
    s_FrameTimeWriteIndex = 0;
    s_FrameTimeSampleCount = 0;
    s_ScriptProfilingEnabled = false;
    s_ScriptTimings.clear();
    s_FrameCaptureEnabled = false;
    s_CapturedFrameNumber = 0;
    s_CapturedFrames.clear();
    s_SectionPendingMs.fill(0.0);
    s_SectionLastMs.fill(0.0);
    s_SectionAvgMs.fill(0.0);
    s_GCGen0Collections = 0;
    s_GCGen1Collections = 0;
    s_GCGen2Collections = 0;
    s_GCManagedHeapBytes = 0;
    s_GCStatsValid = false;
    s_GCWindowStart = std::chrono::steady_clock::now();
    s_GCPrevGen0 = 0;
    s_GCPrevGen1 = 0;
    s_GCPrevGen2 = 0;
    s_GCGen0Rate = 0.0;
    s_GCGen1Rate = 0.0;
    s_GCGen2Rate = 0.0;
}

void PerformanceMonitor::SetScriptProfilingEnabled(bool enabled)
{
    s_ScriptProfilingEnabled = enabled;
    s_ScriptTimings.clear();
}

bool PerformanceMonitor::IsScriptProfilingEnabled()
{
    return s_ScriptProfilingEnabled;
}

void PerformanceMonitor::BeginScriptProfileFrame()
{
    if (s_ScriptProfilingEnabled)
        s_ScriptTimings.clear();
}

void PerformanceMonitor::RecordScriptTime(const std::string& name, double elapsedMs, uint64_t entityId)
{
    if (!s_ScriptProfilingEnabled || name.empty() || !std::isfinite(elapsedMs) || elapsedMs < 0.0)
        return;

    auto it = std::find_if(s_ScriptTimings.begin(), s_ScriptTimings.end(),
        [&](const ScriptTiming& item) { return item.Name == name && item.EntityID == entityId; });
    if (it == s_ScriptTimings.end())
    {
        s_ScriptTimings.push_back({ name, elapsedMs, 1, entityId });
        return;
    }
    it->FrameTimeMs += elapsedMs;
    ++it->Calls;
}

double PerformanceMonitor::GetScriptTimeForEntity(uint64_t entityId)
{
    if (!s_ScriptProfilingEnabled || entityId == 0) return 0.0;
    double totalMs = 0.0;
    for (const ScriptTiming& timing : s_ScriptTimings)
        if (timing.EntityID == entityId)
            totalMs += timing.FrameTimeMs;
    return totalMs;
}

void PerformanceMonitor::FinalizeScriptProfileFrame()
{
    if (!s_ScriptProfilingEnabled) return;
    std::sort(s_ScriptTimings.begin(), s_ScriptTimings.end(),
        [](const ScriptTiming& a, const ScriptTiming& b) { return a.FrameTimeMs > b.FrameTimeMs; });
}

const std::vector<PerformanceMonitor::ScriptTiming>& PerformanceMonitor::GetScriptTimings()
{
    return s_ScriptTimings;
}

void PerformanceMonitor::SetFrameCaptureEnabled(bool enabled)
{
    s_FrameCaptureEnabled = enabled;
    if (enabled)
        ClearCapturedFrames();
}

bool PerformanceMonitor::IsFrameCaptureEnabled()
{
    return s_FrameCaptureEnabled;
}

void PerformanceMonitor::ClearCapturedFrames()
{
    s_CapturedFrameNumber = 0;
    s_CapturedFrames.clear();
}

void PerformanceMonitor::RecordRenderFrame(const RenderFrameSample& sample)
{
    if (!s_FrameCaptureEnabled)
        return;

    RenderFrameSample captured = sample;
    captured.Frame = ++s_CapturedFrameNumber;
    constexpr size_t maxCapturedFrames = 120;
    if (s_CapturedFrames.size() == maxCapturedFrames)
        s_CapturedFrames.erase(s_CapturedFrames.begin());
    s_CapturedFrames.push_back(captured);
}

const std::vector<PerformanceMonitor::RenderFrameSample>& PerformanceMonitor::GetCapturedFrames()
{
    return s_CapturedFrames;
}

int PerformanceMonitor::RunFrameCaptureRegressionTests()
{
    int failures = 0;
    auto check = [&](const char* name, bool ok)
    {
        std::cout << "[FrameCaptureTest] " << (ok ? "PASS: " : "FAIL: ") << name << '\n';
        if (!ok) ++failures;
    };

    SetFrameCaptureEnabled(false);
    ClearCapturedFrames();
    RenderFrameSample sample;
    sample.DrawCalls = 7;
    sample.Triangles = 42;
    RecordRenderFrame(sample);
    check("captura desativada não guarda quadros", GetCapturedFrames().empty());

    SetFrameCaptureEnabled(true);
    for (unsigned int i = 0; i < 125; ++i)
    {
        sample.DrawCalls = i;
        RecordRenderFrame(sample);
    }
    const auto& frames = GetCapturedFrames();
    check("histórico limitado a 120 quadros", frames.size() == 120);
    check("quadro mantém sequência e métricas", frames.size() == 120 &&
        frames.front().Frame == 6 && frames.back().Frame == 125 &&
        frames.back().DrawCalls == 124 && frames.back().Triangles == 42);

    ClearCapturedFrames();
    check("limpar remove histórico e reinicia sequência", GetCapturedFrames().empty());
    SetFrameCaptureEnabled(false);
    std::cout << "[FrameCaptureTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))\n";
    return failures == 0 ? 0 : 1;
}


// ------------------------------------------------------------
// Update
// ------------------------------------------------------------

void PerformanceMonitor::Update()
{
    // Fecha as seções de subsistema acumuladas no frame anterior.
    BeginFrameSections();

    auto now =
        std::chrono::steady_clock::now();

    std::chrono::duration<double> frameDelta =
        now - s_LastFrameTime;

    s_LastFrameTime = now;

    double deltaSeconds =
        frameDelta.count();

    if (deltaSeconds > 0.0)
    {
        s_FrameTimeHistory[s_FrameTimeWriteIndex] = static_cast<float>(deltaSeconds * 1000.0);
        s_FrameTimeWriteIndex = (s_FrameTimeWriteIndex + 1) % s_FrameTimeHistory.size();
        s_FrameTimeSampleCount = (std::min)(s_FrameTimeSampleCount + 1, s_FrameTimeHistory.size());
        s_FPSAccumulator += deltaSeconds;
        s_FrameCount++;
    }

    // Atualiza FPS aproximadamente uma vez por segundo
    if (s_FPSAccumulator >= 1.0)
    {
        s_FPS =
            static_cast<float>(
                static_cast<double>(s_FrameCount)
                / s_FPSAccumulator
                );

        s_FrameCount = 0;
        s_FPSAccumulator = 0.0;
    }


    // --------------------------------------------------------
    // MEMÓRIA — amostrada a cada 500 ms para não custar um syscall por frame
    // --------------------------------------------------------

    HANDLE process = GetCurrentProcess();
    std::chrono::duration<double> memoryDelta =
        now - s_LastMemoryCheck;

    if (memoryDelta.count() >= 0.5)
    {

    PROCESS_MEMORY_COUNTERS_EX memoryCounters;

    if (GetProcessMemoryInfo(
        process,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(
            &memoryCounters),
        sizeof(memoryCounters)))
    {
        s_WorkingSetMB =
            static_cast<float>(
                memoryCounters.WorkingSetSize
                / (1024.0 * 1024.0)
                );

        s_PrivateUsageMB =
            static_cast<float>(
                memoryCounters.PrivateUsage
                / (1024.0 * 1024.0)
                );
        }
        s_LastMemoryCheck = now;
    }


    // --------------------------------------------------------
    // CPU
    // --------------------------------------------------------

    std::chrono::duration<double> cpuDelta =
        now - s_LastCPUCheck;

    if (cpuDelta.count() >= 1.0)
    {
        FILETIME creationTime;
        FILETIME exitTime;
        FILETIME kernelTime;
        FILETIME userTime;

        if (GetProcessTimes(
            process,
            &creationTime,
            &exitTime,
            &kernelTime,
            &userTime))
        {
            unsigned long long kernel =
                FileTimeToUInt64(kernelTime);

            unsigned long long user =
                FileTimeToUInt64(userTime);

            unsigned long long kernelDelta =
                kernel - s_LastKernelTime;

            unsigned long long userDelta =
                user - s_LastUserTime;

            unsigned long long totalProcessTime =
                kernelDelta + userDelta;

            SYSTEM_INFO systemInfo;

            GetSystemInfo(&systemInfo);

            unsigned int cpuCount =
                systemInfo.dwNumberOfProcessors;

            // FILETIME utiliza unidades de 100 ns.
            double processSeconds =
                static_cast<double>(totalProcessTime)
                / 10000000.0;

            double realSeconds =
                cpuDelta.count();

            if (realSeconds > 0.0 && cpuCount > 0)
            {
                s_CPUUsage =
                    static_cast<float>(
                        (processSeconds
                            / realSeconds)
                        * 100.0
                        / cpuCount
                        );
            }

            s_LastKernelTime = kernel;
            s_LastUserTime = user;
        }

        s_LastCPUCheck = now;
    }

    // --------------------------------------------------------
    // GC gerenciado — taxas de coleta por segundo sobre janela de ~1 s
    // --------------------------------------------------------

    if (s_GCStatsValid)
    {
        const double windowSeconds =
            std::chrono::duration<double>(now - s_GCWindowStart).count();
        if (windowSeconds >= 1.0)
        {
            s_GCGen0Rate = (s_GCGen0Collections - s_GCPrevGen0) / windowSeconds;
            s_GCGen1Rate = (s_GCGen1Collections - s_GCPrevGen1) / windowSeconds;
            s_GCGen2Rate = (s_GCGen2Collections - s_GCPrevGen2) / windowSeconds;
            s_GCPrevGen0 = s_GCGen0Collections;
            s_GCPrevGen1 = s_GCGen1Collections;
            s_GCPrevGen2 = s_GCGen2Collections;
            s_GCWindowStart = now;
        }
    }
}


// ------------------------------------------------------------
// Getters
// ------------------------------------------------------------

float PerformanceMonitor::GetFPS()
{
    return s_FPS;
}

float PerformanceMonitor::GetCPUUsage()
{
    return s_CPUUsage;
}

float PerformanceMonitor::GetWorkingSetMB()
{
    return s_WorkingSetMB;
}

float PerformanceMonitor::GetPrivateUsageMB()
{
    return s_PrivateUsageMB;
}

double PerformanceMonitor::GetLastMeshCreateTimeMs()
{
    return s_LastMeshCreateTimeMs;
}

void PerformanceMonitor::SetLastMeshCreateTimeMs(double timeMs)
{
    s_LastMeshCreateTimeMs = timeMs;
}

double PerformanceMonitor::GetDeltaSeconds()
{
    auto now = std::chrono::steady_clock::now();

    std::chrono::duration<double> delta =
        now - s_LastFrameTime;

    return delta.count();
}

void PerformanceMonitor::GetFrameTimeHistory(float* output, size_t capacity)
{
    if (!output || capacity == 0)
        return;

    const size_t count = (std::min)((std::min)(capacity, s_FrameTimeSampleCount), s_FrameTimeHistory.size());
    const size_t oldest = (s_FrameTimeWriteIndex + s_FrameTimeHistory.size() - count) % s_FrameTimeHistory.size();
    for (size_t i = 0; i < count; ++i)
        output[i] = s_FrameTimeHistory[(oldest + i) % s_FrameTimeHistory.size()];
    for (size_t i = count; i < capacity; ++i)
        output[i] = 0.0f;
}

float PerformanceMonitor::GetAverageFrameTimeMs()
{
    if (s_FrameTimeSampleCount == 0)
        return 0.0f;
    double total = 0.0;
    const size_t oldest = (s_FrameTimeWriteIndex + s_FrameTimeHistory.size() - s_FrameTimeSampleCount) % s_FrameTimeHistory.size();
    for (size_t i = 0; i < s_FrameTimeSampleCount; ++i)
        total += s_FrameTimeHistory[(oldest + i) % s_FrameTimeHistory.size()];
    return static_cast<float>(total / static_cast<double>(s_FrameTimeSampleCount));
}

float PerformanceMonitor::GetWorstFrameTimeMs()
{
    float worst = 0.0f;
    const size_t oldest = (s_FrameTimeWriteIndex + s_FrameTimeHistory.size() - s_FrameTimeSampleCount) % s_FrameTimeHistory.size();
    for (size_t i = 0; i < s_FrameTimeSampleCount; ++i)
        worst = (std::max)(worst, s_FrameTimeHistory[(oldest + i) % s_FrameTimeHistory.size()]);
    return worst;
}


// ------------------------------------------------------------
// Tempos por subsistema
// ------------------------------------------------------------

void PerformanceMonitor::BeginFrameSections()
{
    // Fecha o frame anterior: o acumulado vira "último" e alimenta a média.
    for (size_t i = 0; i < kSectionCount; ++i)
    {
        const double frameMs = s_SectionPendingMs[i];
        s_SectionLastMs[i] = frameMs;
        s_SectionAvgMs[i] = (s_SectionAvgMs[i] == 0.0)
            ? frameMs
            : s_SectionAvgMs[i] * 0.9 + frameMs * 0.1;
        s_SectionPendingMs[i] = 0.0;
    }
}

void PerformanceMonitor::RecordFrameSection(FrameSection section, double elapsedMs)
{
    const size_t index = static_cast<size_t>(section);
    if (index >= kSectionCount || !std::isfinite(elapsedMs) || elapsedMs < 0.0)
        return;
    s_SectionPendingMs[index] += elapsedMs;
}

const char* PerformanceMonitor::GetFrameSectionName(FrameSection section)
{
    static const char* names[] = {
        "Jogos demo", "Scripts", "Física", "IA",
        "Animação", "Áudio", "UI 2D", "Render"
    };
    const size_t index = static_cast<size_t>(section);
    return index < kSectionCount ? names[index] : "?";
}

void PerformanceMonitor::GetFrameSectionTimes(double* lastFrameMs, double* averageMs, size_t capacity)
{
    const size_t count = (std::min)(capacity, kSectionCount);
    for (size_t i = 0; i < count; ++i)
    {
        if (lastFrameMs) lastFrameMs[i] = s_SectionLastMs[i];
        if (averageMs) averageMs[i] = s_SectionAvgMs[i];
    }
}


// ------------------------------------------------------------
// GC gerenciado e VRAM estimada
// ------------------------------------------------------------

void PerformanceMonitor::RecordGCStats(int gen0Collections, int gen1Collections,
                                       int gen2Collections, long long managedHeapBytes)
{
    s_GCGen0Collections = gen0Collections;
    s_GCGen1Collections = gen1Collections;
    s_GCGen2Collections = gen2Collections;
    s_GCManagedHeapBytes = managedHeapBytes;
    s_GCStatsValid = true;
}

void PerformanceMonitor::GetGCStats(double* gen0PerSecond, double* gen1PerSecond,
                                    double* gen2PerSecond, double* managedHeapMB)
{
    if (gen0PerSecond) *gen0PerSecond = s_GCGen0Rate;
    if (gen1PerSecond) *gen1PerSecond = s_GCGen1Rate;
    if (gen2PerSecond) *gen2PerSecond = s_GCGen2Rate;
    if (managedHeapMB)
        *managedHeapMB = s_GCStatsValid
            ? static_cast<double>(s_GCManagedHeapBytes) / (1024.0 * 1024.0)
            : 0.0;
}

double PerformanceMonitor::GetEstimatedVRAMMB()
{
    return static_cast<double>(GpuMemoryTracker::GetAllocatedBytes()) /
        (1024.0 * 1024.0);
}

