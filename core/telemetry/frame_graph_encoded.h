// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph_statistics.h"

#include <core/telemetry/frame_graph_wire_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(EncodedFrameGraphPayloadHeader) == s_EncodedFrameGraphPayloadHeaderByteSize, "EncodedFrameGraphPayloadHeader wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeader) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeader, magic) == s_EncodedFrameGraphPayloadHeaderMagicOffset
    && offsetof(EncodedFrameGraphPayloadHeader, version) == s_EncodedFrameGraphPayloadHeaderVersionOffset
    && offsetof(EncodedFrameGraphPayloadHeader, reserved) == s_EncodedFrameGraphPayloadHeaderReservedOffset
    && offsetof(EncodedFrameGraphPayloadHeader, frameIndex) == s_EncodedFrameGraphPayloadHeaderFrameIndexOffset
    && offsetof(EncodedFrameGraphPayloadHeader, nodeCount) == s_EncodedFrameGraphPayloadHeaderNodeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, edgeCount) == s_EncodedFrameGraphPayloadHeaderEdgeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, stringTableBytes) == s_EncodedFrameGraphPayloadHeaderStringTableBytesOffset
    && offsetof(EncodedFrameGraphPayloadHeader, queueAssignmentCount) == s_EncodedFrameGraphPayloadHeaderQueueAssignmentCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, compiledTaskCount) == s_EncodedFrameGraphPayloadHeaderCompiledTaskCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, runtimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderRuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, physicalQueueRuntimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderPhysicalQueueRuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, packetSubmissionStatisticsCount) == s_EncodedFrameGraphPayloadHeaderPacketSubmissionStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeader, packetSubmissionStatisticsPresent) == s_EncodedFrameGraphPayloadHeaderPacketSubmissionStatisticsPresentOffset
    && offsetof(EncodedFrameGraphPayloadHeader, reservedTail) == s_EncodedFrameGraphPayloadHeaderReservedTailOffset,
    "EncodedFrameGraphPayloadHeader field order drifted"
);
static_assert(sizeof(EncodedFrameGraphNode) == s_EncodedFrameGraphNodeByteSize, "EncodedFrameGraphNode wire layout drifted");
static_assert(alignof(EncodedFrameGraphNode) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphNode must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphEdge) == s_EncodedFrameGraphEdgeByteSize, "EncodedFrameGraphEdge wire layout drifted");
static_assert(alignof(EncodedFrameGraphEdge) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphEdge must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPhysicalQueueId) == s_EncodedFrameGraphPhysicalQueueIdByteSize, "EncodedFrameGraphPhysicalQueueId wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueId) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueId must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphQueueAssignment) == s_EncodedFrameGraphQueueAssignmentByteSize, "EncodedFrameGraphQueueAssignment wire layout drifted");
static_assert(alignof(EncodedFrameGraphQueueAssignment) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphQueueAssignment must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphCompiledTask) == s_EncodedFrameGraphCompiledTaskByteSize, "EncodedFrameGraphCompiledTask wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompiledTask) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphCompiledTask must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphCompileRuntimeStatistics) == s_EncodedFrameGraphCompileRuntimeStatisticsByteSize, "EncodedFrameGraphCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompileRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphCompileRuntimeStatistics, taskCount) == s_EncodedFrameGraphCompileRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, explicitDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsExplicitDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, inferredDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsInferredDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, mergedTaskCount) == s_EncodedFrameGraphCompileRuntimeStatisticsMergedTaskCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, transitionBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsTransitionBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uavBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsUavBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipReleaseBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipAcquireBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, stateExportBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsStateExportBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, repeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphCompileRuntimeStatisticsRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingCouldAvoidTransferCount) == s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingCouldAvoidTransferCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferInternalCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferInternalCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalImportCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalImportCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalExportCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalExportCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetMemberCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetMemberCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, directResourceUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsDirectResourceUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declaredResourceSetUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsDeclaredResourceSetUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, expandedResourceSetMemberUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsExpandedResourceSetMemberUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectBytes) == s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectBytesOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobCount) == s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobBytes) == s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobBytesOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declarationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsDeclarationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, analysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, validationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsValidationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, dependencyAnalysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsDependencyAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, hazardAnalysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsHazardAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, topologicalOrderSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsTopologicalOrderSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, queueAssignmentSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsQueueAssignmentSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, planningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetizationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketizationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceStatePlanningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceStatePlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyPlanningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyPlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, totalSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsTotalSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceVersionCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceVersionCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceVersionEdgeCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceVersionEdgeCountOffset,
    "EncodedFrameGraphCompileRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRecordingRuntimeStatistics) == s_EncodedFrameGraphRecordingRuntimeStatisticsByteSize, "EncodedFrameGraphRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRecordingRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRecordingRuntimeStatistics, packetCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, barrierCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsBarrierCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, workerRoutedPacketCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, parallelPacketCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsParallelPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListAcquisitionSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskRecordSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsTaskRecordSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingElapsedSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingElapsedSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierElapsedSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierElapsedSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerBusySeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerBusySecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerCapacitySeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerCapacitySecondsOffset,
    "EncodedFrameGraphRecordingRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphSubmissionRuntimeStatistics) == s_EncodedFrameGraphSubmissionRuntimeStatisticsByteSize, "EncodedFrameGraphSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphSubmissionRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeCommandListCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeCommandListCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, timelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, inheritedTimelineWaitElisionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsInheritedTimelineWaitElisionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphSubmissionRuntimeStatisticsSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, recoverySubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRecoverySubmissionCountOffset,
    "EncodedFrameGraphSubmissionRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRuntimeStatistics) == s_EncodedFrameGraphRuntimeStatisticsByteSize, "EncodedFrameGraphRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatistics, nodeIndex) == s_EncodedFrameGraphRuntimeStatisticsNodeIndexOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, deviceGeneration) == s_EncodedFrameGraphRuntimeStatisticsDeviceGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, reserved) == s_EncodedFrameGraphRuntimeStatisticsReservedOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, graphGeneration) == s_EncodedFrameGraphRuntimeStatisticsGraphGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, planGeneration) == s_EncodedFrameGraphRuntimeStatisticsPlanGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, recordingAttemptGeneration) == s_EncodedFrameGraphRuntimeStatisticsRecordingAttemptGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, compile) == s_EncodedFrameGraphRuntimeStatisticsCompileOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, recording) == s_EncodedFrameGraphRuntimeStatisticsRecordingOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, submission) == s_EncodedFrameGraphRuntimeStatisticsSubmissionOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphRuntimeStatisticsNestedSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, recoverySubmissionCount) == s_EncodedFrameGraphRuntimeStatisticsNestedRecoverySubmissionCountOffset,
    "EncodedFrameGraphRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, taskCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, packetCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, mergedTaskCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsMergedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, prologueBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPrologueBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, epilogueBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsEpilogueBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipReleaseBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipAcquireBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingRepeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingRepeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset,
    "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, packetCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, barrierCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, workerRoutedPacketCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, parallelPacketCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsParallelPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListAcquisitionSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskRecordSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskRecordSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, recordingSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsRecordingSecondsOffset,
    "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeCommandListCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeCommandListCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, timelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, inheritedTimelineWaitElisionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsInheritedTimelineWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, recoverySubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRecoverySubmissionCountOffset,
    "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, ownerNodeIndex) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsOwnerNodeIndexOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queue) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queueClass) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueClassOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, reserved) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsReservedOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, compile) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsCompileOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, recording) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsRecordingOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, submission) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsSubmissionOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsNestedSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, recoverySubmissionCount) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsNestedRecoverySubmissionCountOffset,
    "EncodedFrameGraphPhysicalQueueRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPacketSubmissionStatistics) == s_EncodedFrameGraphPacketSubmissionStatisticsByteSize, "EncodedFrameGraphPacketSubmissionStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPacketSubmissionStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPacketSubmissionStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPacketSubmissionStatistics, ownerNodeIndex) == s_EncodedFrameGraphPacketSubmissionStatisticsOwnerNodeIndexOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetIndex) == s_EncodedFrameGraphPacketSubmissionStatisticsPacketIndexOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetGeneration) == s_EncodedFrameGraphPacketSubmissionStatisticsPacketGenerationOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queue) == s_EncodedFrameGraphPacketSubmissionStatisticsQueueOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queueClass) == s_EncodedFrameGraphPacketSubmissionStatisticsQueueClassOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, joinsAcceptedQueueFrontier) == s_EncodedFrameGraphPacketSubmissionStatisticsJoinsAcceptedQueueFrontierOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, recoverySubmission) == s_EncodedFrameGraphPacketSubmissionStatisticsRecoverySubmissionOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, reserved) == s_EncodedFrameGraphPacketSubmissionStatisticsReservedOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, taskCount) == s_EncodedFrameGraphPacketSubmissionStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, commandListCount) == s_EncodedFrameGraphPacketSubmissionStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphPacketSubmissionStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphPacketSubmissionStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, timelineWaitCount) == s_EncodedFrameGraphPacketSubmissionStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphPacketSubmissionStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, inheritedTimelineWaitElisionCount) == s_EncodedFrameGraphPacketSubmissionStatisticsInheritedTimelineWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, submissionSeconds) == s_EncodedFrameGraphPacketSubmissionStatisticsSubmissionSecondsOffset,
    "EncodedFrameGraphPacketSubmissionStatistics field order drifted"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

