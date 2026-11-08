// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph_kinds.h"

#include <core/telemetry/frame_graph_types.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<u64> FrameGraphCompileBarrierCount(const FrameGraphCompileRuntimeStatistics& statistics)noexcept{
    u64 barrierCountTotal = statistics.transitionBarrierCount;
    const Array<u64, 4u> remainingBarrierCounts = { statistics.uavBarrierCount, statistics.ownershipReleaseBarrierCount, statistics.ownershipAcquireBarrierCount, statistics.stateExportBarrierCount, };
    for(const u64 barrierCount : remainingBarrierCounts){
        if(barrierCount > Limit<u64>::s_Max - barrierCountTotal)
            return MakeUnexpected(Failure{});
        barrierCountTotal += barrierCount;
    }
    return barrierCountTotal;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphRuntimeStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    FrameGraphCompileRuntimeStatistics compile;
    FrameGraphRecordingRuntimeStatistics recording;
    FrameGraphSubmissionRuntimeStatistics submission;
    u16 deviceGeneration = 0u;
    b8 present = false;
};

struct FrameGraphPhysicalQueueRuntimeStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    FrameGraphPhysicalQueueId queue;
    u16 deviceGeneration = 0u;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    FrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
};

struct FrameGraphPhysicalQueueRuntimeStatisticsRecord{
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    FrameGraphPhysicalQueueRuntimeStatistics statistics;
};

// Exact native-submission telemetry for one compiler-generated packet.
// Present packet tables hold every native submission per owner, even an empty table when nobody submitted.
// Packet generation is the immutable plan generation.
// Wait counts exclude backend-internal waits outside the graph.
struct FrameGraphPacketSubmissionStatisticsRecord{
    u64 packetGeneration = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 inheritedTimelineWaitElisionCount = 0u;
    f64 submissionSeconds = 0.0;
    FrameGraphPhysicalQueueId queue;
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    b8 joinsAcceptedQueueFrontier = false;
    b8 recoverySubmission = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

