// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing_metrics.h"

#include <global/algorithm.h>
#include <global/assert.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metrics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TimestampEventsPerEnvelope = 2u;
static constexpr usize s_MinOverlappingQueueCount = 2u;

struct TimestampEvent{
    u64 ticks = 0u;
    bool begins = false;
};

using TimestampRangeVector = Vector<GpuComparableTimestampRange, Alloc::ScratchArena>;
using TimestampEventVector = Vector<TimestampEvent, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool LessTimestampRange(const GpuComparableTimestampRange& lhs, const GpuComparableTimestampRange& rhs)noexcept{
    if(lhs.physicalQueue.deviceGeneration != rhs.physicalQueue.deviceGeneration)
        return lhs.physicalQueue.deviceGeneration < rhs.physicalQueue.deviceGeneration;
    if(lhs.physicalQueue.index != rhs.physicalQueue.index)
        return lhs.physicalQueue.index < rhs.physicalQueue.index;
    if(lhs.beginTicks != rhs.beginTicks)
        return lhs.beginTicks < rhs.beginTicks;
    return lhs.endTicks < rhs.endTicks;
}

[[nodiscard]] static bool LessTimestampEvent(const TimestampEvent& lhs, const TimestampEvent& rhs)noexcept{
    return lhs.ticks < rhs.ticks;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuPacketEnvelopeMetrics> TryAggregateGpuPacketEnvelopeMetrics(const GpuComparableTimestampRange* const packetEnvelopes, const usize packetEnvelopeCount, Alloc::ScratchArena& scratchArena, GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics){
    using namespace __hidden_gpu_timing_metrics;

    const auto reject = [&]()noexcept{
        outQueueMetrics.clear();
        return MakeUnexpected(Failure{});
    };
    GpuPacketEnvelopeMetrics metrics;
    outQueueMetrics.clear();

    const auto eventCapacity = TryMultiply<usize>(packetEnvelopeCount, s_TimestampEventsPerEnvelope);
    if(!eventCapacity)
        return MakeUnexpected(Failure{});
    if(packetEnvelopeCount == 0u || !packetEnvelopes)
        return MakeUnexpected(Failure{});

    const usize queueCapacity = Min(packetEnvelopeCount, static_cast<usize>(Limit<u16>::s_Max));
    TimestampRangeVector sortedRanges{scratchArena};
    TimestampEventVector events{scratchArena};
    if(packetEnvelopeCount > sortedRanges.max_size() || queueCapacity > outQueueMetrics.max_size() || *eventCapacity > events.max_size())
        return MakeUnexpected(Failure{});

    // Reserve escaping output first so shared-arena temporaries pop cleanly instead of pinning below the caller-owned output.
    outQueueMetrics.reserve(queueCapacity);
    sortedRanges.reserve(packetEnvelopeCount);
    events.reserve(*eventCapacity);

    const f64 secondsPerTick = packetEnvelopes[0u].secondsPerTick;
    const u16 deviceGeneration = packetEnvelopes[0u].physicalQueue.deviceGeneration;
    for(usize envelopeIndex = 0u; envelopeIndex < packetEnvelopeCount; ++envelopeIndex){
        const GpuComparableTimestampRange& range = packetEnvelopes[envelopeIndex];
        if(!range.valid() || range.secondsPerTick != secondsPerTick || range.physicalQueue.deviceGeneration != deviceGeneration)
            return reject();
        sortedRanges.push_back(range);
    }
    Sort(sortedRanges.begin(), sortedRanges.end(), LessTimestampRange);

    usize rangeIndex = 0u;
    while(rangeIndex < sortedRanges.size()){
        const GpuPhysicalQueueId physicalQueue = sortedRanges[rangeIndex].physicalQueue;
        GpuQueuePacketEnvelopeMetrics queueMetrics{ .physicalQueue = physicalQueue };
        bool hasMergedRange = false;
        u64 mergedBegin = 0u;
        u64 mergedEnd = 0u;

        while(rangeIndex < sortedRanges.size() && sortedRanges[rangeIndex].physicalQueue == physicalQueue){
            const GpuComparableTimestampRange& range = sortedRanges[rangeIndex];
            ++rangeIndex;
            if(range.beginTicks == range.endTicks)
                continue;

            if(!hasMergedRange){
                mergedBegin = range.beginTicks;
                mergedEnd = range.endTicks;
                hasMergedRange = true;
                continue;
            }
            if(range.beginTicks <= mergedEnd){
                mergedEnd = Max(mergedEnd, range.endTicks);
                continue;
            }

            const u64 idleTicks = range.beginTicks - mergedEnd;
            NWB_ASSERT(!AddOverflows<u64>(queueMetrics.internalIdleTicks, idleTicks));
            queueMetrics.internalIdleTicks += idleTicks;
            events.push_back(TimestampEvent{ .ticks = mergedBegin, .begins = true });
            events.push_back(TimestampEvent{ .ticks = mergedEnd, .begins = false });
            mergedBegin = range.beginTicks;
            mergedEnd = range.endTicks;
        }

        if(hasMergedRange){
            events.push_back(TimestampEvent{ .ticks = mergedBegin, .begins = true });
            events.push_back(TimestampEvent{ .ticks = mergedEnd, .begins = false });
        }
        outQueueMetrics.push_back(queueMetrics);
    }

    Sort(events.begin(), events.end(), LessTimestampEvent);
    u64 overlapTicks = 0u;
    u64 previousTicks = events.empty() ? 0u : events.front().ticks;
    usize activeQueueCount = 0u;
    usize eventIndex = 0u;
    while(eventIndex < events.size()){
        const u64 eventTicks = events[eventIndex].ticks;
        if(activeQueueCount >= s_MinOverlappingQueueCount){
            const u64 intervalTicks = eventTicks - previousTicks;
            NWB_ASSERT(!AddOverflows<u64>(overlapTicks, intervalTicks));
            overlapTicks += intervalTicks;
        }

        usize beginCount = 0u;
        usize endCount = 0u;
        while(eventIndex < events.size() && events[eventIndex].ticks == eventTicks){
            if(events[eventIndex].begins){
                NWB_ASSERT(!AddOverflows<usize>(beginCount, usize{ 1u }));
                ++beginCount;
            }
            else{
                NWB_ASSERT(!AddOverflows<usize>(endCount, usize{ 1u }));
                ++endCount;
            }
            ++eventIndex;
        }

        NWB_ASSERT(endCount <= activeQueueCount);
        activeQueueCount -= endCount;
        NWB_ASSERT(!AddOverflows<usize>(activeQueueCount, beginCount));
        activeQueueCount += beginCount;
        previousTicks = eventTicks;
    }
    NWB_ASSERT(activeQueueCount == 0u);

    metrics.secondsPerTick = secondsPerTick;
    metrics.queueOverlapTicks = overlapTicks;
    return metrics;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

