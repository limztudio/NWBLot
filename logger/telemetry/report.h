// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/global.h>

#include <core/alloc/scratch.h>
#include <core/telemetry/perf.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_TelemetryReportEventKindCount = static_cast<usize>(Core::Telemetry::EventKind::MemoryFrame) + 1u;
inline constexpr usize s_TelemetryMemorySourceCount = static_cast<usize>(Core::Perf::MemorySource::HeapBacking) + 1u;

struct TelemetryMemorySummary{
    u64 eventCount = 0u;
    u64 maxUsedBytes = 0u;
    u64 maxPeakUsedBytes = 0u;
    i64 totalUsedDeltaBytes = 0;
    // An unrepresentable sum stays invalid for this report; individual records retain their original deltas.
    b8 totalUsedDeltaValid = true;
};

struct TelemetryReportSummary{
    u64 eventCount = 0u;
    Array<u64, s_TelemetryReportEventKindCount> eventKindCounts{};
    u64 parseFailureCount = 0u;

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
    Array<TelemetryMemorySummary, s_TelemetryMemorySourceCount> memorySources;

    u64 frameGraphFrameCount = 0u;
    u64 frameGraphNodeCount = 0u;
    u64 frameGraphEdgeCount = 0u;
    u32 maxFrameGraphNodeCount = 0u;
    u32 maxFrameGraphEdgeCount = 0u;
    b8 gpuTimingSecondsValid = true;
    b8 cpuTimingSecondsValid = true;
    b8 hasFrameRange = false;
};

struct TelemetryReport{
    TelemetryReportSummary summary;
    AString<Core::Telemetry::TelemetryArena> json;
    AString<Core::Telemetry::TelemetryArena> perfCsv;
    AString<Core::Telemetry::TelemetryArena> graph;


public:
    explicit TelemetryReport(Core::Telemetry::TelemetryArena& arena)noexcept
        : json(arena)
        , perfCsv(arena)
        , graph(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryReportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AStringView FormatNameIdentityText(const Name& name, Array<char, NameDetail::s_DebugHashTextLength + 1u>& outIdentityText);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TelemetryReport> BuildTelemetryReport(const Core::Telemetry::EventView& events, Core::Telemetry::TelemetryArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

