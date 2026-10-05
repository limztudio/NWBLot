// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_metrics.h"

#include <core/perf/timing.h>


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

struct GpuTimingSinkSample{
    Perf::TimingScopeId scope;
    f64 durationSeconds = 0.0;
    u64 sourceFrameIndex = 0u;
};

using GpuTimingSinkSampleVector = Vector<GpuTimingSinkSample, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns the persistent role reservations and bounded pending-range state shared by GPU-derived timing metrics.
// Callers serialize access so query-pool synchronization remains with the recorder that feeds completed ranges.
class GpuTimingMetricCorrelator final : NoCopy{
private:
    using PacketEnvelopeScopeIndex = HashMap<Name, usize, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;


private:
    struct PendingOverlapFrame{
        u64 frameIndex = 0u;
        GpuComparableTimestampRange first;
        GpuComparableTimestampRange second;
        bool hasFirst = false;
        bool hasSecond = false;
    };

    struct OverlapRecord{
        Name firstScope = s_NameNone;
        Name secondScope = s_NameNone;
        Name outputScopeName = s_NameNone;
        Perf::TimingScopeId outputScope;
        Vector<PendingOverlapFrame, Alloc::GlobalArena> pendingFrames;

        explicit OverlapRecord(Alloc::GlobalArena& arena)
            : pendingFrames(arena)
        {}
    };

    struct PacketEnvelopeMetricScopeRecord{
        GpuComparableTimestampRange range;
        Name scopeName = s_NameNone;
        GpuPhysicalQueueId physicalQueue;
        bool received = false;
    };

    struct PacketEnvelopeMetricQueueOutputRecord{
        GpuPhysicalQueueId physicalQueue;
        Name internalIdleScopeName = s_NameNone;
        Perf::TimingScopeId internalIdleScope;
    };

    struct PacketEnvelopeMetricOutputRoleRecord{
        Name scopeName = s_NameNone;
        GpuPhysicalQueueId physicalQueue;
        bool queueInternalIdle = false;
    };


    struct PendingPacketEnvelopeMetric{
        u64 sourceFrameIndex = 0u;
        Name queueOverlapScopeName = s_NameNone;
        Perf::TimingScopeId queueOverlapScope;
        Vector<PacketEnvelopeMetricScopeRecord, Alloc::GlobalArena> scopes;
        PacketEnvelopeScopeIndex scopeIndices;
        Vector<PacketEnvelopeMetricQueueOutputRecord, Alloc::GlobalArena> queueOutputs;
        usize remainingScopeCount = 0u;

        explicit PendingPacketEnvelopeMetric(Alloc::GlobalArena& arena)
            : scopes(arena)
            , scopeIndices(arena)
            , queueOutputs(arena)
        {}
    };

    using OverlapVector = Vector<OverlapRecord, Alloc::GlobalArena>;
    using PendingPacketEnvelopeMetricVector = Vector<PendingPacketEnvelopeMetric, Alloc::GlobalArena>;
    using PacketEnvelopeMetricOutputRoleVector = Vector<PacketEnvelopeMetricOutputRoleRecord, Alloc::GlobalArena>;


public:
    GpuTimingMetricCorrelator(Alloc::GlobalArena& arena, Perf::TimingSink& timing);


public:
    [[nodiscard]] bool prepareOverlapMetric(
        const Name& firstScope,
        const Name& secondScope,
        const Name& outputScope
    );
    [[nodiscard]] bool preparePacketEnvelopeMetrics(
        u64 sourceFrameIndex,
        NotNull<const GpuPacketEnvelopeMetricScope*> scopeInputs,
        usize scopeCount,
        const Name& queueOverlapScope,
        NotNull<const GpuPacketEnvelopeMetricQueueOutput*> queueOutputInputs,
        usize queueOutputCount
    );
    // Stages completed derived samples without invoking the timing sink. The caller owns the observer-publication boundary.
    void recordTimestampRange(
        const Name& scopeName,
        u64 frameIndex,
        const GpuComparableTimestampRange& range,
        GpuTimingSinkSampleVector& performanceSamples,
        Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] bool hasOutputRole(const Name& scopeName)const;
    void discardPendingRanges()noexcept;
    void reset();


private:
    void rememberMetricOutput(const Name& name, const GpuPhysicalQueueId& queue, bool internalIdle);


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    OverlapVector m_overlapRecords;
    PendingPacketEnvelopeMetricVector m_pendingPacketEnvelopeMetrics;
    PacketEnvelopeMetricOutputRoleVector m_packetEnvelopeMetricOutputRoles;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

