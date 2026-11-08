// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_metrics.h"
#include "gpu_timing_metric_sample.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


struct GpuPacketEnvelopeMetricScope;
struct GpuPacketEnvelopeMetricQueueOutput;


class GpuOverlapMetricTracker final : NoCopy{
private:
    struct PendingOverlapFrame{
        u64 frameIndex = 0u;
        GpuComparableTimestampRange first;
        GpuComparableTimestampRange second;
        b8 hasFirst = false;
        b8 hasSecond = false;
    };

    struct OverlapRecord{
        Name firstScope = s_NameNone;
        Name secondScope = s_NameNone;
        Name outputScopeName = s_NameNone;
        Perf::TimingScopeId outputScope;
        Vector<PendingOverlapFrame, Alloc::GlobalArena> pendingFrames;


    public:
        explicit OverlapRecord(Alloc::GlobalArena& arena)noexcept
            : pendingFrames(arena)
        {}
    };


public:
    GpuOverlapMetricTracker(Alloc::GlobalArena& arena, Perf::TimingSink& timing)noexcept
        : m_arena(arena), m_timing(timing), m_overlapRecords(arena){}
    [[nodiscard]] b8 prepare(const Name& firstScope, const Name& secondScope, const Name& outputScope);
    [[nodiscard]] b8 conflicts(Span<const GpuPacketEnvelopeMetricScope> scopeInputs, const Name& queueOverlapScope, Span<const GpuPacketEnvelopeMetricQueueOutput> queueOutputInputs)const noexcept;
    void record(const Name& scopeName, u64 frameIndex, const GpuComparableTimestampRange& range, u64 retentionFrames, GpuTimingSinkSampleVector& performanceSamples);
    [[nodiscard]] b8 hasOutputRole(const Name& scopeName)const noexcept;
    void discardPendingRanges()noexcept;
    void reset()noexcept;

private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    Vector<OverlapRecord, Alloc::GlobalArena> m_overlapRecords;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
