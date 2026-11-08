// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/command.h"

#include <core/alloc/scratch.h>
#include <core/graphics/gpu_timing_metrics.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One absolute device-timestamp range proven comparable across submissions.
// Ranges keep device generation and tick period, so consumers reject stale data before converting to seconds.
struct GpuComparableTimestampRange{
    u64 beginTicks = 0u;
    u64 endTicks = 0u;
    f64 secondsPerTick = 0.0;
    GpuPhysicalQueueId physicalQueue;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return physicalQueue.valid() && beginTicks <= endTicks && secondsPerTick > 0.0 && IsFinite(secondsPerTick);
    }
};

// Ranges must share one calibrated logical-device epoch and exact tick period.
// Comparable disjoint ranges return zero overlap.
// Integer intersection preserves precision beyond f64's exact range.
[[nodiscard]] Expected<u64> TryComputeGpuTimestampOverlap(const GpuComparableTimestampRange& first, const GpuComparableTimestampRange& second)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuQueuePacketEnvelopeMetrics{
    GpuPhysicalQueueId physicalQueue;
    // Positive holes between accepted packet envelopes on this queue. Leading and trailing idle are unknowable.
    u64 internalIdleTicks = 0u;
};

using GpuQueuePacketEnvelopeMetricsVector = Vector<GpuQueuePacketEnvelopeMetrics, Alloc::ScratchArena>;

// Aggregates half-open packet envelopes in raw ticks (same device generation + tick period).
// Same-queue unioned before gap/concurrency measure.
// False resets outputs; caller allocator kept, scratch is temporary.
// Never dereferences packet data; the caller keeps the span storage live through the call.
[[nodiscard]] Expected<GpuPacketEnvelopeMetrics> TryAggregateGpuPacketEnvelopeMetrics(Span<const GpuComparableTimestampRange> packetEnvelopes, Alloc::ScratchArena& scratchArena, GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

