// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "diagnostic.h"
#include "frame_graph.h"
#include "perf.h"
#include "text_log.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_ArchiveReportEventKindCount = static_cast<usize>(EventKind::MemoryFrame) + 1u;

struct ArchiveReportSummary{
    u64 eventCount = 0u;
    u64 eventKindCounts[s_ArchiveReportEventKindCount] = {};
    u64 parseFailureCount = 0u;

    bool hasFrameRange = false;
    u64 minFrameIndex = 0u;
    u64 maxFrameIndex = 0u;

    u64 cpuTimingEventCount = 0u;
    u64 cpuTimingSampleCount = 0u;
    f64 cpuTimingSeconds = 0.0;
    f64 maxCpuTimingSeconds = 0.0;

    u64 gpuTimingEventCount = 0u;
    u64 gpuTimingSampleCount = 0u;
    f64 gpuTimingSeconds = 0.0;
    f64 maxGpuTimingSeconds = 0.0;

    u64 memoryEventCount = 0u;
    u64 maxMemoryUsedBytes = 0u;
    u64 maxMemoryPeakUsedBytes = 0u;
    i64 totalMemoryUsedDeltaBytes = 0;

    u64 frameGraphFrameCount = 0u;
    u64 frameGraphNodeCount = 0u;
    u64 frameGraphEdgeCount = 0u;
    u32 maxFrameGraphNodeCount = 0u;
    u32 maxFrameGraphEdgeCount = 0u;
};

struct ArchiveReport{
    ArchiveReportSummary summary;
    AString<TelemetryArena> json;
    AString<TelemetryArena> perfCsv;

    explicit ArchiveReport(TelemetryArena& arena)
        : json(arena)
        , perfCsv(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* EventKindText(EventKind::Enum kind)noexcept;
[[nodiscard]] const char* PerfTimingSourceText(PerfTimingSource::Enum source)noexcept;
[[nodiscard]] bool BuildArchiveReport(TelemetryArena& arena, const EventView& events, ArchiveReport& outReport);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
