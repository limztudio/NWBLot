// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Generation-scoped CPU telemetry; fixed-width counts (no host-width leak). Wire records: packed fixed-size, codec-native byte order (no cross-endian format).
// Durations are seconds.
struct FrameGraphCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 resourceCount = 0u;
    u64 resourceVersionCount = 0u;
    u64 resourceVersionEdgeCount = 0u;
    u64 resourceUseCount = 0u;
    u64 explicitDependencyCount = 0u;
    u64 inferredDependencyCount = 0u;
    u64 packetCount = 0u;
    u64 packetDependencyCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 transitionBarrierCount = 0u;
    u64 uavBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 stateExportBarrierCount = 0u;
    u64 logicalOwnershipTransferCount = 0u;
    u64 logicalOwnershipTransferSignatureCount = 0u;
    u64 repeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingCouldAvoidTransferCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
    u64 logicalOwnershipTransferInternalCount = 0u;
    u64 logicalOwnershipTransferExternalImportCount = 0u;
    u64 logicalOwnershipTransferExternalExportCount = 0u;
    u64 resourceSetCount = 0u;
    u64 resourceSetMemberCount = 0u;
    u64 directResourceUseCount = 0u;
    u64 declaredResourceSetUseCount = 0u;
    u64 expandedResourceSetMemberUseCount = 0u;
    u64 payloadObjectCount = 0u;
    u64 payloadObjectBytes = 0u;
    u64 uploadBlobCount = 0u;
    u64 uploadBlobBytes = 0u;
    f64 declarationSeconds = 0.0;
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    f64 totalSeconds = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool FrameGraphCompileBarrierCount(
    const FrameGraphCompileRuntimeStatistics& statistics,
    u64& outBarrierCount
)noexcept{
    outBarrierCount = statistics.transitionBarrierCount;
    const u64 remainingBarrierCounts[] = {
        statistics.uavBarrierCount,
        statistics.ownershipReleaseBarrierCount,
        statistics.ownershipAcquireBarrierCount,
        statistics.stateExportBarrierCount,
    };
    for(const u64 barrierCount : remainingBarrierCounts){
        if(barrierCount > Limit<u64>::s_Max - outBarrierCount)
            return false;
        outBarrierCount += barrierCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;
};

struct FrameGraphSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 inheritedTimelineWaitElisionCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

struct FrameGraphRuntimeStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    FrameGraphCompileRuntimeStatistics compile;
    FrameGraphRecordingRuntimeStatistics recording;
    FrameGraphSubmissionRuntimeStatistics submission;
    u16 deviceGeneration = 0u;
    bool present = false;
};

// Exact CPU telemetry per physical queue; absent rows are unknown, not zero.
struct FrameGraphPhysicalQueueCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 packetCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 prologueBarrierCount = 0u;
    u64 epilogueBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 incomingLogicalOwnershipTransferCount = 0u;
    u64 outgoingLogicalOwnershipTransferCount = 0u;
    u64 incomingLogicalOwnershipTransferSignatureCount = 0u;
    u64 outgoingLogicalOwnershipTransferSignatureCount = 0u;
    u64 incomingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
};

struct FrameGraphPhysicalQueueRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
};

struct FrameGraphPhysicalQueueSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 inheritedTimelineWaitElisionCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
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

// Exact native-submission telemetry for one compiler-generated packet. Payloads whose packet table is marked present contain every native submission for every runtime-statistics owner, including an exact empty table when no owner submitted native work.
// Packet generation is the immutable plan generation. Wait counts exclude backend-internal waits outside the graph.
struct FrameGraphPacketSubmissionStatisticsRecord{
    u64 packetGeneration = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphPhysicalQueueId queue;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    bool joinsAcceptedQueueFrontier = false;
    bool recoverySubmission = false;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 inheritedTimelineWaitElisionCount = 0u;
    f64 submissionSeconds = 0.0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

