// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_metrics.h"
#include "gpu_overlap_metric_tracker.h"

#include <core/perf/timing.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuPacketEnvelopeMetricScope{
    Name scopeName = s_NameNone;
    GpuPhysicalQueueId physicalQueue;
};

struct GpuPacketEnvelopeMetricQueueOutput{
    GpuPhysicalQueueId physicalQueue;
    Name internalIdleScopeName = s_NameNone;
};




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns the persistent role reservations and bounded pending-range state shared by GPU-derived timing metrics.
// Callers serialize access so query-pool synchronization remains with the recorder that feeds completed ranges.
class GpuTimingMetricCorrelator final : NoCopy{
private:
    using PacketEnvelopeScopeIndex = HashMap<Name, usize, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;


private:
    struct PacketEnvelopeMetricScopeRecord{
        GpuComparableTimestampRange range;
        Name scopeName = s_NameNone;
        GpuPhysicalQueueId physicalQueue;
        b8 received = false;
    };

    struct PacketEnvelopeMetricQueueOutputRecord{
        GpuPhysicalQueueId physicalQueue;
        Name internalIdleScopeName = s_NameNone;
        Perf::TimingScopeId internalIdleScope;
    };

    struct PacketEnvelopeMetricOutputRoleRecord{
        Name scopeName = s_NameNone;
        GpuPhysicalQueueId physicalQueue;
        b8 queueInternalIdle = false;
    };


    struct PendingPacketEnvelopeMetric{
        u64 sourceFrameIndex = 0u;
        Name queueOverlapScopeName = s_NameNone;
        Perf::TimingScopeId queueOverlapScope;
        Vector<PacketEnvelopeMetricScopeRecord, Alloc::GlobalArena> scopes;
        PacketEnvelopeScopeIndex scopeIndices;
        Vector<PacketEnvelopeMetricQueueOutputRecord, Alloc::GlobalArena> queueOutputs;
        usize remainingScopeCount = 0u;


    public:
        explicit PendingPacketEnvelopeMetric(Alloc::GlobalArena& arena)
            : scopes(arena)
            , scopeIndices(arena)
            , queueOutputs(arena)
        {}
    };


private:
    using PendingPacketEnvelopeMetricVector = Vector<PendingPacketEnvelopeMetric, Alloc::GlobalArena>;
    using PacketEnvelopeMetricOutputRoleVector = Vector<PacketEnvelopeMetricOutputRoleRecord, Alloc::GlobalArena>;


public:
    GpuTimingMetricCorrelator(Alloc::GlobalArena& arena, Perf::TimingSink& timing)noexcept;


public:
    [[nodiscard]] b8 prepareOverlapMetric(const Name& firstScope, const Name& secondScope, const Name& outputScope);
    [[nodiscard]] b8 preparePacketEnvelopeMetrics(
        u64 sourceFrameIndex,
        Span<const GpuPacketEnvelopeMetricScope> scopeInputs,
        const Name& queueOverlapScope,
        Span<const GpuPacketEnvelopeMetricQueueOutput> queueOutputInputs
    );
    // Stages completed derived samples without invoking the timing sink. The caller owns the observer-publication boundary.
    void recordTimestampRange(const Name& scopeName, u64 frameIndex, const GpuComparableTimestampRange& range, Alloc::ScratchArena& scratchArena, GpuTimingSinkSampleVector& performanceSamples);
    [[nodiscard]] b8 hasOutputRole(const Name& scopeName)const noexcept;
    void discardPendingRanges()noexcept;
    void reset()noexcept;


private:
    void rememberMetricOutput(const Name& name, const GpuPhysicalQueueId& queue, b8 internalIdle);


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    GpuOverlapMetricTracker m_overlapMetrics;
    PendingPacketEnvelopeMetricVector m_pendingPacketEnvelopeMetrics;
    PacketEnvelopeMetricOutputRoleVector m_packetEnvelopeMetricOutputRoles;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

