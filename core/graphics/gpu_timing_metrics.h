// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/command.h"

#include <core/alloc/scratch.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuComparableTimestampRange{
    u64 beginTicks = 0u;
    u64 endTicks = 0u;
    f64 secondsPerTick = 0.0;
    GpuPhysicalQueueId physicalQueue;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return physicalQueue.valid() && beginTicks <= endTicks && secondsPerTick > 0.0 && VectorDoubleIsFinite(VectorDoubleReplicate(secondsPerTick), VectorDoubleComponentMask::s_DoubleXYZW);
    }
};

[[nodiscard]] Expected<u64> TryComputeGpuTimestampOverlap(const GpuComparableTimestampRange& first, const GpuComparableTimestampRange& second)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuQueuePacketEnvelopeMetrics{
    GpuPhysicalQueueId physicalQueue;
    u64 internalIdleTicks = 0u;
};

struct GpuPacketEnvelopeMetrics{
    f64 secondsPerTick = 0.0;
    u64 queueOverlapTicks = 0u;
};

using GpuQueuePacketEnvelopeMetricsVector = Vector<GpuQueuePacketEnvelopeMetrics, Alloc::ScratchArena>;

[[nodiscard]] Expected<GpuPacketEnvelopeMetrics> TryAggregateGpuPacketEnvelopeMetrics(Span<const GpuComparableTimestampRange> packetEnvelopes, Alloc::ScratchArena& scratchArena, GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

