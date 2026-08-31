// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/command.h"

#include <core/alloc/scratch.h>


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

    [[nodiscard]] bool valid()const{
        return physicalQueue.valid() && beginTicks <= endTicks && secondsPerTick > 0.0 && IsFinite(secondsPerTick);
    }
};

// Returns false when the ranges do not share one calibrated logical-device epoch and exact tick period. Comparable
// disjoint ranges return true with zero overlap. Integer intersection preserves precision beyond f64's exact range.
[[nodiscard]] inline bool TryComputeGpuTimestampOverlap(
    const GpuComparableTimestampRange& first,
    const GpuComparableTimestampRange& second,
    u64& outOverlapTicks
){
    outOverlapTicks = 0u;
    if(
        !first.valid()
        || !second.valid()
        || first.physicalQueue.deviceGeneration != second.physicalQueue.deviceGeneration
        || first.secondsPerTick != second.secondsPerTick
    )
        return false;

    const u64 overlapBegin = Max(first.beginTicks, second.beginTicks);
    const u64 overlapEnd = Min(first.endTicks, second.endTicks);
    if(overlapEnd > overlapBegin)
        outOverlapTicks = overlapEnd - overlapBegin;
    return true;
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

// Aggregates half-open packet envelopes without converting their raw ticks to floating point. Every range must
// belong to the same logical-device generation and use the same exact tick period. Same-queue ranges are unioned
// before gaps and cross-queue concurrency are measured. False always resets both outputs. outQueueMetrics keeps its
// caller-selected allocator; scratchArena owns only temporary sorting and sweep storage.
[[nodiscard]] bool TryAggregateGpuPacketEnvelopeMetrics(
    const GpuComparableTimestampRange* packetEnvelopes,
    usize packetEnvelopeCount,
    GpuPacketEnvelopeMetrics& outMetrics,
    GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

