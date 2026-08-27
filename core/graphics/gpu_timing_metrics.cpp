// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing_metrics.h"

#include <global/algorithm.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metrics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TimestampEvent{
    u64 ticks = 0u;
    bool begins = false;
};

using TimestampRangeVector = Vector<GpuComparableTimestampRange, Alloc::ScratchArena>;
using TimestampEventVector = Vector<TimestampEvent, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool LessTimestampRange(
    const GpuComparableTimestampRange& lhs,
    const GpuComparableTimestampRange& rhs
){
    if(lhs.physicalQueue.deviceGeneration != rhs.physicalQueue.deviceGeneration)
        return lhs.physicalQueue.deviceGeneration < rhs.physicalQueue.deviceGeneration;
    if(lhs.physicalQueue.index != rhs.physicalQueue.index)
        return lhs.physicalQueue.index < rhs.physicalQueue.index;
    if(lhs.beginTicks != rhs.beginTicks)
        return lhs.beginTicks < rhs.beginTicks;
    return lhs.endTicks < rhs.endTicks;
}

[[nodiscard]] static bool LessTimestampEvent(const TimestampEvent& lhs, const TimestampEvent& rhs){
    return lhs.ticks < rhs.ticks;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TryAggregateGpuPacketEnvelopeMetrics(
    const GpuComparableTimestampRange* const packetEnvelopes,
    const usize packetEnvelopeCount,
    GpuPacketEnvelopeMetrics& outMetrics,
    GpuQueuePacketEnvelopeMetricsVector& outQueueMetrics,
    Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_gpu_timing_metrics;

    const auto reject = [&](){
        outMetrics = {};
        outQueueMetrics.clear();
        return false;
    };
    outMetrics = {};
    outQueueMetrics.clear();

    usize eventCapacity = 0u;
    if(!TryMultiply<usize>(packetEnvelopeCount, usize{ 2u }, eventCapacity))
        return false;
    if(packetEnvelopeCount == 0u || !packetEnvelopes)
        return false;

    const usize queueCapacity = Min(packetEnvelopeCount, static_cast<usize>(Limit<u16>::s_Max));
    TimestampRangeVector sortedRanges{scratchArena};
    TimestampEventVector events{scratchArena};
    if(
        packetEnvelopeCount > sortedRanges.max_size()
        || queueCapacity > outQueueMetrics.max_size()
        || eventCapacity > events.max_size()
    )
        return false;

    // Reserve escaping output first. When all vectors share one LIFO scratch arena, the temporary buffers can then
    // pop cleanly in reverse construction order instead of remaining pinned below the caller-owned output.
    outQueueMetrics.reserve(queueCapacity);
    sortedRanges.reserve(packetEnvelopeCount);
    events.reserve(eventCapacity);

    const f64 secondsPerTick = packetEnvelopes[0u].secondsPerTick;
    const u16 deviceGeneration = packetEnvelopes[0u].physicalQueue.deviceGeneration;
    for(usize envelopeIndex = 0u; envelopeIndex < packetEnvelopeCount; ++envelopeIndex){
        const GpuComparableTimestampRange& range = packetEnvelopes[envelopeIndex];
        if(
            !range.valid()
            || range.secondsPerTick != secondsPerTick
            || range.physicalQueue.deviceGeneration != deviceGeneration
        )
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
            if(AddOverflows<u64>(queueMetrics.internalIdleTicks, idleTicks))
                return reject();
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
        if(activeQueueCount >= 2u){
            const u64 intervalTicks = eventTicks - previousTicks;
            if(AddOverflows<u64>(overlapTicks, intervalTicks))
                return reject();
            overlapTicks += intervalTicks;
        }

        usize beginCount = 0u;
        usize endCount = 0u;
        while(eventIndex < events.size() && events[eventIndex].ticks == eventTicks){
            if(events[eventIndex].begins){
                if(AddOverflows<usize>(beginCount, usize{ 1u }))
                    return reject();
                ++beginCount;
            }
            else{
                if(AddOverflows<usize>(endCount, usize{ 1u }))
                    return reject();
                ++endCount;
            }
            ++eventIndex;
        }

        if(endCount > activeQueueCount)
            return reject();
        activeQueueCount -= endCount;
        if(AddOverflows<usize>(activeQueueCount, beginCount))
            return reject();
        activeQueueCount += beginCount;
        previousTicks = eventTicks;
    }
    if(activeQueueCount != 0u)
        return reject();

    outMetrics.secondsPerTick = secondsPerTick;
    outMetrics.queueOverlapTicks = overlapTicks;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

