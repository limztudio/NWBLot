// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>

#include <core/telemetry/diagnostic.h>
#include <core/telemetry/frame_graph.h>
#include <core/telemetry/perf.h>
#include <core/telemetry/text_log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;
using TelemetryArena = Telemetry::TelemetryArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_TelemetryReportEventKindCount = static_cast<usize>(Telemetry::EventKind::MemoryFrame) + 1u;

struct TelemetryReportSummary{
    u64 eventCount = 0u;
    u64 eventKindCounts[s_TelemetryReportEventKindCount] = {};
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

struct TelemetryReport{
    TelemetryReportSummary summary;
    AString<TelemetryArena> json;
    AString<TelemetryArena> perfCsv;
    AString<TelemetryArena> graph;

    explicit TelemetryReport(TelemetryArena& arena)
        : json(arena)
        , perfCsv(arena)
        , graph(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const char* EventKindText(Telemetry::EventKind::Enum kind)noexcept;
[[nodiscard]] const char* PerfTimingSourceText(Telemetry::PerfTimingSource::Enum source)noexcept;
[[nodiscard]] bool BuildTelemetryReport(TelemetryArena& arena, const Telemetry::EventView& events, TelemetryReport& outReport);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

