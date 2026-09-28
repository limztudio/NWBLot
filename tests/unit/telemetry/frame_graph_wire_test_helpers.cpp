// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved){
    return Telemetry::EncodedFrameGraphRuntimeStatistics{
        .nodeIndex = nodeIndex,
        .deviceGeneration = statistics.deviceGeneration,
        .reserved = reserved,
        .graphGeneration = statistics.graphGeneration,
        .planGeneration = statistics.planGeneration,
        .recordingAttemptGeneration = statistics.recordingAttemptGeneration,
        .compile = {
            .taskCount = statistics.compile.taskCount,
            .resourceCount = statistics.compile.resourceCount,
            .resourceUseCount = statistics.compile.resourceUseCount,
            .explicitDependencyCount = statistics.compile.explicitDependencyCount,
            .inferredDependencyCount = statistics.compile.inferredDependencyCount,
            .packetCount = statistics.compile.packetCount,
            .packetDependencyCount = statistics.compile.packetDependencyCount,
            .mergedTaskCount = statistics.compile.mergedTaskCount,
            .transitionBarrierCount = statistics.compile.transitionBarrierCount,
            .uavBarrierCount = statistics.compile.uavBarrierCount,
            .ownershipReleaseBarrierCount = statistics.compile.ownershipReleaseBarrierCount,
            .ownershipAcquireBarrierCount = statistics.compile.ownershipAcquireBarrierCount,
            .stateExportBarrierCount = statistics.compile.stateExportBarrierCount,
            .logicalOwnershipTransferCount = statistics.compile.logicalOwnershipTransferCount,
            .logicalOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferSignatureCount,
            .repeatedOwnershipTransferSignatureCount = statistics.compile.repeatedOwnershipTransferSignatureCount,
            .concurrentSharingCouldAvoidTransferCount = statistics.compile.concurrentSharingCouldAvoidTransferCount,
            .concurrentSharingAdviceResourceCount = statistics.compile.concurrentSharingAdviceResourceCount,
            .logicalOwnershipTransferInternalCount = statistics.compile.logicalOwnershipTransferInternalCount,
            .logicalOwnershipTransferExternalImportCount = statistics.compile.logicalOwnershipTransferExternalImportCount,
            .logicalOwnershipTransferExternalExportCount = statistics.compile.logicalOwnershipTransferExternalExportCount,
            .resourceSetCount = statistics.compile.resourceSetCount,
            .resourceSetMemberCount = statistics.compile.resourceSetMemberCount,
            .directResourceUseCount = statistics.compile.directResourceUseCount,
            .declaredResourceSetUseCount = statistics.compile.declaredResourceSetUseCount,
            .expandedResourceSetMemberUseCount = statistics.compile.expandedResourceSetMemberUseCount,
            .payloadObjectCount = statistics.compile.payloadObjectCount,
            .payloadObjectBytes = statistics.compile.payloadObjectBytes,
            .uploadBlobCount = statistics.compile.uploadBlobCount,
            .uploadBlobBytes = statistics.compile.uploadBlobBytes,
            .declarationSeconds = statistics.compile.declarationSeconds,
            .analysisSeconds = statistics.compile.analysisSeconds,
            .validationSeconds = statistics.compile.validationSeconds,
            .dependencyAnalysisSeconds = statistics.compile.dependencyAnalysisSeconds,
            .hazardAnalysisSeconds = statistics.compile.hazardAnalysisSeconds,
            .topologicalOrderSeconds = statistics.compile.topologicalOrderSeconds,
            .queueAssignmentSeconds = statistics.compile.queueAssignmentSeconds,
            .planningSeconds = statistics.compile.planningSeconds,
            .packetizationSeconds = statistics.compile.packetizationSeconds,
            .resourceStatePlanningSeconds = statistics.compile.resourceStatePlanningSeconds,
            .packetDependencyPlanningSeconds = statistics.compile.packetDependencyPlanningSeconds,
            .totalSeconds = statistics.compile.totalSeconds,
            .resourceVersionCount = statistics.compile.resourceVersionCount,
            .resourceVersionEdgeCount = statistics.compile.resourceVersionEdgeCount,
        },
        .recording = {
            .packetCount = statistics.recording.packetCount,
            .taskCount = statistics.recording.taskCount,
            .commandListCount = statistics.recording.commandListCount,
            .barrierCount = statistics.recording.barrierCount,
            .workerRoutedPacketCount = statistics.recording.workerRoutedPacketCount,
            .parallelPacketCount = statistics.recording.parallelPacketCount,
            .commandListAcquisitionSeconds = statistics.recording.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = statistics.recording.graphBarrierRecordingSeconds,
            .taskRecordSeconds = statistics.recording.taskRecordSeconds,
            .recordingSeconds = statistics.recording.recordingSeconds,
            .recordingElapsedSeconds = statistics.recording.recordingElapsedSeconds,
            .readyFrontierElapsedSeconds = statistics.recording.readyFrontierElapsedSeconds,
            .readyFrontierWorkerBusySeconds = statistics.recording.readyFrontierWorkerBusySeconds,
            .readyFrontierWorkerCapacitySeconds = statistics.recording.readyFrontierWorkerCapacitySeconds,
        },
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
            .recoverySubmissionCount = statistics.submission.recoverySubmissionCount,
        },
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

