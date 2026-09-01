// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_metrics.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuPacketEnvelopeMetricScope{
    Name scopeName = NAME_NONE;
    GpuPhysicalQueueId physicalQueue;
};

struct GpuPacketEnvelopeMetricQueueOutput{
    GpuPhysicalQueueId physicalQueue;
    Name internalIdleScopeName = NAME_NONE;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns the persistent role reservations and bounded pending-range state shared by GPU-derived timing metrics.
// Callers serialize access so query-pool synchronization remains with the recorder that feeds completed ranges.
class GpuTimingMetricCorrelator final : NoCopy{
private:
    struct PendingOverlapFrame{
        u64 frameIndex = 0u;
        GpuComparableTimestampRange first;
        GpuComparableTimestampRange second;
        bool hasFirst = false;
        bool hasSecond = false;
    };

    struct OverlapRecord{
        Name firstScope = NAME_NONE;
        Name secondScope = NAME_NONE;
        Name outputScopeName = NAME_NONE;
        Perf::TimingScopeId outputScope;
        Vector<PendingOverlapFrame, Alloc::GlobalArena> pendingFrames;

        explicit OverlapRecord(Alloc::GlobalArena& arena)
            : pendingFrames(arena)
        {}
    };

    struct PacketEnvelopeMetricScopeRecord{
        Name scopeName = NAME_NONE;
        GpuPhysicalQueueId physicalQueue;
        GpuComparableTimestampRange range;
        bool received = false;
    };

    struct PacketEnvelopeMetricQueueOutputRecord{
        GpuPhysicalQueueId physicalQueue;
        Name internalIdleScopeName = NAME_NONE;
        Perf::TimingScopeId internalIdleScope;
    };

    struct PacketEnvelopeMetricOutputRoleRecord{
        Name scopeName = NAME_NONE;
        GpuPhysicalQueueId physicalQueue;
        bool queueInternalIdle = false;
    };

    struct PendingPacketEnvelopeMetric{
        u64 sourceFrameIndex = 0u;
        Name queueOverlapScopeName = NAME_NONE;
        Perf::TimingScopeId queueOverlapScope;
        Vector<PacketEnvelopeMetricScopeRecord, Alloc::GlobalArena> scopes;
        Vector<PacketEnvelopeMetricQueueOutputRecord, Alloc::GlobalArena> queueOutputs;

        explicit PendingPacketEnvelopeMetric(Alloc::GlobalArena& arena)
            : scopes(arena)
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
    void recordTimestampRange(
        const Name& scopeName,
        u64 frameIndex,
        const GpuComparableTimestampRange& range,
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

