// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/command.h"

#include <core/alloc/scratch.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One absolute device-timestamp range proven comparable across submissions. Queue indices may differ, but every
// range retains its logical-device generation and exact tick period so consumers can reject stale or mismatched
// data before conversion to floating-point seconds.
struct GpuComparableTimestampRange{
    u64 beginTicks = 0u;
    u64 endTicks = 0u;
    f64 secondsPerTick = 0.0;
    GpuPhysicalQueueId physicalQueue;

    [[nodiscard]] bool valid()const noexcept{
        return physicalQueue.valid() && beginTicks <= endTicks && secondsPerTick > 0.0 && IsFinite(secondsPerTick);
    }
};

// Ranges must share one calibrated logical-device epoch and exact tick period. Comparable disjoint ranges return
// zero overlap. Integer intersection preserves precision beyond f64's exact range.
[[nodiscard]] inline Expected<u64> TryComputeGpuTimestampOverlap(
    const GpuComparableTimestampRange& first,
    const GpuComparableTimestampRange& second
)noexcept{
    if(
        !first.valid()
        || !second.valid()
        || first.physicalQueue.deviceGeneration != second.physicalQueue.deviceGeneration
        || first.secondsPerTick != second.secondsPerTick
    )
        return MakeUnexpected(Failure{});

    const u64 overlapBegin = Max(first.beginTicks, second.beginTicks);
    const u64 overlapEnd = Min(first.endTicks, second.endTicks);
    return overlapEnd > overlapBegin ? overlapEnd - overlapBegin : 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuQueuePacketEnvelopeMetrics{
    GpuPhysicalQueueId physicalQueue;
    // Positive holes between accepted packet envelopes on this queue. Leading and trailing idle are unknowable.
    u64 internalIdleTicks = 0u;
};

struct GpuPacketEnvelopeMetrics{
    f64 secondsPerTick = 0.0;
    // Union length for which at least two distinct physical queues have accepted packet work in flight.
    u64 queueOverlapTicks = 0u;
};

using GpuQueuePacketEnvelopeMetricsVector = Vector<GpuQueuePacketEnvelopeMetrics, Alloc::ScratchArena>;

// Aggregates half-open packet envelopes in raw ticks (same device generation + tick period). Same-queue unioned
// before gap/concurrency measure. False resets outputs; caller allocator kept, scratch is temporary.
[[nodiscard]] Expected<GpuPacketEnvelopeMetrics> TryAggregateGpuPacketEnvelopeMetrics(
    const GpuComparableTimestampRange* packetEnvelopes,
    usize packetEnvelopeCount,
    GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

