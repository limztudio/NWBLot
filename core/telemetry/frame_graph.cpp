// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include <global/algorithm.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsValidStringTableText(const AStringView text)noexcept{
    return !text.empty()
        && !HasEmbeddedNull(text)
        && text.size() < static_cast<usize>(Limit<u32>::s_Max)
    ;
}

[[nodiscard]] static i32 QueueAssignmentScoreTotal(const FrameGraphQueueAssignmentScore& score)noexcept{
    const i64 total = static_cast<i64>(score.preference)
        + static_cast<i64>(score.overlap)
        - static_cast<i64>(score.queueLoad)
        - static_cast<i64>(score.incomingCrossings)
        - static_cast<i64>(score.outgoingCrossings)
        - static_cast<i64>(score.ownershipTransfers)
    ;
    if(total < static_cast<i64>(Limit<i32>::s_Min))
        return Limit<i32>::s_Min;
    if(total > static_cast<i64>(Limit<i32>::s_Max))
        return Limit<i32>::s_Max;
    return static_cast<i32>(total);
}

[[nodiscard]] static bool ValidateHeader(const u32 magic, const u16 reserved)noexcept{
    return magic == s_FrameGraphPayloadMagic
        && reserved == 0u
    ;
}

[[nodiscard]] static bool IsCanonicalQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return queue.valid()
        || (queue.index == Limit<u16>::s_Max && queue.deviceGeneration == 0u)
    ;
}

[[nodiscard]] static bool ValidateNodeInput(const FrameGraphNodeDesc& node)noexcept{
    return static_cast<bool>(node.name)
        && IsValidFrameGraphNodeKind(node.kind)
        && IsValidStringTableText(node.label)
        && (!node.queueAssignment.present || (
            node.kind == FrameGraphNodeKind::Pass
            && IsValidFrameGraphQueueAssignment(node.queueAssignment)
        ))
        && (!node.compiledTask.present || (
            node.kind == FrameGraphNodeKind::Pass
            && IsValidFrameGraphCompiledTask(node.compiledTask)
        ))
        && (!node.runtimeStatistics.present || (
            node.kind == FrameGraphNodeKind::Pass
            && IsValidFrameGraphRuntimeStatistics(node.runtimeStatistics)
        ))
    ;
}

[[nodiscard]] static bool ValidateEdgeInput(const FrameGraphEdgeDesc& edge, const usize nodeCount)noexcept{
    return IsValidFrameGraphEdgeKind(edge.kind)
        && static_cast<usize>(edge.fromNodeIndex) < nodeCount
        && static_cast<usize>(edge.toNodeIndex) < nodeCount
    ;
}

[[nodiscard]] static bool ValidateEncodedNode(const EncodedFrameGraphNode& node)noexcept{
    return node.reserved == 0u
        && !NameDetail::IsZeroHash(node.nameHash)
        && IsValidFrameGraphNodeKind(static_cast<FrameGraphNodeKind::Enum>(node.kind))
    ;
}

[[nodiscard]] static bool ValidateEncodedEdge(const EncodedFrameGraphEdge& edge, const usize nodeCount)noexcept{
    return edge.reserved == 0u
        && IsValidFrameGraphEdgeKind(static_cast<FrameGraphEdgeKind::Enum>(edge.kind))
        && static_cast<usize>(edge.fromNodeIndex) < nodeCount
        && static_cast<usize>(edge.toNodeIndex) < nodeCount
    ;
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueId EncodeQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return EncodedFrameGraphPhysicalQueueId{
        .index = queue.index,
        .deviceGeneration = queue.deviceGeneration,
    };
}

[[nodiscard]] static FrameGraphPhysicalQueueId DecodeQueue(const EncodedFrameGraphPhysicalQueueId& queue)noexcept{
    return FrameGraphPhysicalQueueId{
        .index = queue.index,
        .deviceGeneration = queue.deviceGeneration,
    };
}

[[nodiscard]] static EncodedFrameGraphQueueAssignment EncodeQueueAssignment(
    const u32 nodeIndex,
    const FrameGraphQueueAssignment& assignment
)noexcept{
    EncodedFrameGraphQueueAssignment encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.initialQueue = EncodeQueue(assignment.initialQueue);
    encoded.plannedQueue = EncodeQueue(assignment.plannedQueue);
    encoded.acceptedQueue = EncodeQueue(assignment.acceptedQueue);
    encoded.previousAcceptedQueue = EncodeQueue(assignment.previousAcceptedQueue);
    encoded.scorePreference = assignment.score.preference;
    encoded.scoreOverlap = assignment.score.overlap;
    encoded.scoreQueueLoad = assignment.score.queueLoad;
    encoded.scoreIncomingCrossings = assignment.score.incomingCrossings;
    encoded.scoreOutgoingCrossings = assignment.score.outgoingCrossings;
    encoded.scoreOwnershipTransfers = assignment.score.ownershipTransfers;
    encoded.scoreTotal = assignment.score.total;
    encoded.queueClass = assignment.queueClass;
    encoded.reason = assignment.reason;
    encoded.modifiers = assignment.modifiers;
    encoded.acceptance = assignment.acceptance;
    encoded.dedicated = assignment.dedicated ? 1u : 0u;
    return encoded;
}

[[nodiscard]] static bool DecodeQueueAssignment(
    const EncodedFrameGraphQueueAssignment& encoded,
    FrameGraphQueueAssignment& outAssignment
)noexcept{
    if(
        encoded.dedicated > 1u
        || encoded.reserved[0u] != 0u
        || encoded.reserved[1u] != 0u
        || encoded.reserved[2u] != 0u
    )
        return false;

    outAssignment.initialQueue = DecodeQueue(encoded.initialQueue);
    outAssignment.plannedQueue = DecodeQueue(encoded.plannedQueue);
    outAssignment.acceptedQueue = DecodeQueue(encoded.acceptedQueue);
    outAssignment.previousAcceptedQueue = DecodeQueue(encoded.previousAcceptedQueue);
    outAssignment.score.preference = encoded.scorePreference;
    outAssignment.score.overlap = encoded.scoreOverlap;
    outAssignment.score.queueLoad = encoded.scoreQueueLoad;
    outAssignment.score.incomingCrossings = encoded.scoreIncomingCrossings;
    outAssignment.score.outgoingCrossings = encoded.scoreOutgoingCrossings;
    outAssignment.score.ownershipTransfers = encoded.scoreOwnershipTransfers;
    outAssignment.score.total = encoded.scoreTotal;
    outAssignment.queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass);
    outAssignment.reason = static_cast<FrameGraphQueueAssignmentReason::Enum>(encoded.reason);
    outAssignment.modifiers = static_cast<FrameGraphQueueAssignmentModifier::Mask>(encoded.modifiers);
    outAssignment.acceptance = static_cast<FrameGraphQueueAssignmentAcceptance::Enum>(encoded.acceptance);
    outAssignment.dedicated = encoded.dedicated != 0u;
    outAssignment.present = true;
    return IsValidFrameGraphQueueAssignment(outAssignment);
}

[[nodiscard]] static EncodedFrameGraphCompiledTask EncodeCompiledTask(
    const u32 nodeIndex,
    const FrameGraphCompiledTask& compiledTask
)noexcept{
    EncodedFrameGraphCompiledTask encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.packetIndex = compiledTask.packetIndex;
    encoded.planGeneration = compiledTask.planGeneration;
    encoded.packetizationDecision = compiledTask.packetizationDecision;
    return encoded;
}

[[nodiscard]] static bool DecodeCompiledTask(
    const EncodedFrameGraphCompiledTask& encoded,
    FrameGraphCompiledTask& outCompiledTask
)noexcept{
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u)
        return false;

    outCompiledTask.planGeneration = encoded.planGeneration;
    outCompiledTask.packetIndex = encoded.packetIndex;
    outCompiledTask.packetizationDecision = static_cast<FrameGraphTaskPacketizationDecision::Enum>(
        encoded.packetizationDecision
    );
    outCompiledTask.present = true;
    return IsValidFrameGraphCompiledTask(outCompiledTask);
}

[[nodiscard]] static EncodedFrameGraphCompileRuntimeStatistics EncodeCompileRuntimeStatistics(
    const FrameGraphCompileRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphCompileRuntimeStatistics{
        .taskCount = statistics.taskCount,
        .resourceCount = statistics.resourceCount,
        .resourceUseCount = statistics.resourceUseCount,
        .explicitDependencyCount = statistics.explicitDependencyCount,
        .inferredDependencyCount = statistics.inferredDependencyCount,
        .packetCount = statistics.packetCount,
        .packetDependencyCount = statistics.packetDependencyCount,
        .mergedTaskCount = statistics.mergedTaskCount,
        .transitionBarrierCount = statistics.transitionBarrierCount,
        .uavBarrierCount = statistics.uavBarrierCount,
        .ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount,
        .ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount,
        .stateExportBarrierCount = statistics.stateExportBarrierCount,
        .logicalOwnershipTransferCount = statistics.logicalOwnershipTransferCount,
        .logicalOwnershipTransferSignatureCount = statistics.logicalOwnershipTransferSignatureCount,
        .repeatedOwnershipTransferSignatureCount = statistics.repeatedOwnershipTransferSignatureCount,
        .concurrentSharingCouldAvoidTransferCount = statistics.concurrentSharingCouldAvoidTransferCount,
        .concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount,
        .logicalOwnershipTransferInternalCount = statistics.logicalOwnershipTransferInternalCount,
        .logicalOwnershipTransferExternalImportCount = statistics.logicalOwnershipTransferExternalImportCount,
        .logicalOwnershipTransferExternalExportCount = statistics.logicalOwnershipTransferExternalExportCount,
        .resourceSetCount = statistics.resourceSetCount,
        .resourceSetMemberCount = statistics.resourceSetMemberCount,
        .directResourceUseCount = statistics.directResourceUseCount,
        .declaredResourceSetUseCount = statistics.declaredResourceSetUseCount,
        .expandedResourceSetMemberUseCount = statistics.expandedResourceSetMemberUseCount,
        .payloadObjectCount = statistics.payloadObjectCount,
        .payloadObjectBytes = statistics.payloadObjectBytes,
        .uploadBlobCount = statistics.uploadBlobCount,
        .uploadBlobBytes = statistics.uploadBlobBytes,
        .declarationSeconds = statistics.declarationSeconds,
        .analysisSeconds = statistics.analysisSeconds,
        .validationSeconds = statistics.validationSeconds,
        .dependencyAnalysisSeconds = statistics.dependencyAnalysisSeconds,
        .hazardAnalysisSeconds = statistics.hazardAnalysisSeconds,
        .topologicalOrderSeconds = statistics.topologicalOrderSeconds,
        .queueAssignmentSeconds = statistics.queueAssignmentSeconds,
        .planningSeconds = statistics.planningSeconds,
        .packetizationSeconds = statistics.packetizationSeconds,
        .resourceStatePlanningSeconds = statistics.resourceStatePlanningSeconds,
        .packetDependencyPlanningSeconds = statistics.packetDependencyPlanningSeconds,
        .totalSeconds = statistics.totalSeconds,
    };
}

[[nodiscard]] static FrameGraphCompileRuntimeStatistics DecodeCompileRuntimeStatistics(
    const EncodedFrameGraphCompileRuntimeStatistics& statistics
)noexcept{
    return FrameGraphCompileRuntimeStatistics{
        .taskCount = statistics.taskCount,
        .resourceCount = statistics.resourceCount,
        .resourceUseCount = statistics.resourceUseCount,
        .explicitDependencyCount = statistics.explicitDependencyCount,
        .inferredDependencyCount = statistics.inferredDependencyCount,
        .packetCount = statistics.packetCount,
        .packetDependencyCount = statistics.packetDependencyCount,
        .mergedTaskCount = statistics.mergedTaskCount,
        .transitionBarrierCount = statistics.transitionBarrierCount,
        .uavBarrierCount = statistics.uavBarrierCount,
        .ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount,
        .ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount,
        .stateExportBarrierCount = statistics.stateExportBarrierCount,
        .logicalOwnershipTransferCount = statistics.logicalOwnershipTransferCount,
        .logicalOwnershipTransferSignatureCount = statistics.logicalOwnershipTransferSignatureCount,
        .repeatedOwnershipTransferSignatureCount = statistics.repeatedOwnershipTransferSignatureCount,
        .concurrentSharingCouldAvoidTransferCount = statistics.concurrentSharingCouldAvoidTransferCount,
        .concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount,
        .logicalOwnershipTransferInternalCount = statistics.logicalOwnershipTransferInternalCount,
        .logicalOwnershipTransferExternalImportCount = statistics.logicalOwnershipTransferExternalImportCount,
        .logicalOwnershipTransferExternalExportCount = statistics.logicalOwnershipTransferExternalExportCount,
        .resourceSetCount = statistics.resourceSetCount,
        .resourceSetMemberCount = statistics.resourceSetMemberCount,
        .directResourceUseCount = statistics.directResourceUseCount,
        .declaredResourceSetUseCount = statistics.declaredResourceSetUseCount,
        .expandedResourceSetMemberUseCount = statistics.expandedResourceSetMemberUseCount,
        .payloadObjectCount = statistics.payloadObjectCount,
        .payloadObjectBytes = statistics.payloadObjectBytes,
        .uploadBlobCount = statistics.uploadBlobCount,
        .uploadBlobBytes = statistics.uploadBlobBytes,
        .declarationSeconds = statistics.declarationSeconds,
        .analysisSeconds = statistics.analysisSeconds,
        .validationSeconds = statistics.validationSeconds,
        .dependencyAnalysisSeconds = statistics.dependencyAnalysisSeconds,
        .hazardAnalysisSeconds = statistics.hazardAnalysisSeconds,
        .topologicalOrderSeconds = statistics.topologicalOrderSeconds,
        .queueAssignmentSeconds = statistics.queueAssignmentSeconds,
        .planningSeconds = statistics.planningSeconds,
        .packetizationSeconds = statistics.packetizationSeconds,
        .resourceStatePlanningSeconds = statistics.resourceStatePlanningSeconds,
        .packetDependencyPlanningSeconds = statistics.packetDependencyPlanningSeconds,
        .totalSeconds = statistics.totalSeconds,
    };
}

[[nodiscard]] static EncodedFrameGraphRecordingRuntimeStatistics EncodeRecordingRuntimeStatistics(
    const FrameGraphRecordingRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphRecordingRuntimeStatistics{
        .packetCount = statistics.packetCount,
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .barrierCount = statistics.barrierCount,
        .workerRoutedPacketCount = statistics.workerRoutedPacketCount,
        .parallelPacketCount = statistics.parallelPacketCount,
        .commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds,
        .graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds,
        .taskRecordSeconds = statistics.taskRecordSeconds,
        .recordingSeconds = statistics.recordingSeconds,
        .recordingElapsedSeconds = statistics.recordingElapsedSeconds,
        .readyFrontierElapsedSeconds = statistics.readyFrontierElapsedSeconds,
        .readyFrontierWorkerBusySeconds = statistics.readyFrontierWorkerBusySeconds,
        .readyFrontierWorkerCapacitySeconds = statistics.readyFrontierWorkerCapacitySeconds,
    };
}

[[nodiscard]] static FrameGraphRecordingRuntimeStatistics DecodeRecordingRuntimeStatistics(
    const EncodedFrameGraphRecordingRuntimeStatistics& statistics
)noexcept{
    return FrameGraphRecordingRuntimeStatistics{
        .packetCount = statistics.packetCount,
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .barrierCount = statistics.barrierCount,
        .workerRoutedPacketCount = statistics.workerRoutedPacketCount,
        .parallelPacketCount = statistics.parallelPacketCount,
        .commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds,
        .graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds,
        .taskRecordSeconds = statistics.taskRecordSeconds,
        .recordingSeconds = statistics.recordingSeconds,
        .recordingElapsedSeconds = statistics.recordingElapsedSeconds,
        .readyFrontierElapsedSeconds = statistics.readyFrontierElapsedSeconds,
        .readyFrontierWorkerBusySeconds = statistics.readyFrontierWorkerBusySeconds,
        .readyFrontierWorkerCapacitySeconds = statistics.readyFrontierWorkerCapacitySeconds,
    };
}

[[nodiscard]] static EncodedFrameGraphSubmissionRuntimeStatistics EncodeSubmissionRuntimeStatistics(
    const FrameGraphSubmissionRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphSubmissionRuntimeStatistics{
        .acceptedPacketCount = statistics.acceptedPacketCount,
        .acceptedTaskCount = statistics.acceptedTaskCount,
        .rejectedPacketCount = statistics.rejectedPacketCount,
        .rejectedTaskCount = statistics.rejectedTaskCount,
        .nativeSubmissionCount = statistics.nativeSubmissionCount,
        .rejectedSubmissionCount = statistics.rejectedSubmissionCount,
        .nativeCommandListCount = statistics.nativeCommandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static FrameGraphSubmissionRuntimeStatistics DecodeSubmissionRuntimeStatistics(
    const EncodedFrameGraphSubmissionRuntimeStatistics& statistics
)noexcept{
    return FrameGraphSubmissionRuntimeStatistics{
        .acceptedPacketCount = statistics.acceptedPacketCount,
        .acceptedTaskCount = statistics.acceptedTaskCount,
        .rejectedPacketCount = statistics.rejectedPacketCount,
        .rejectedTaskCount = statistics.rejectedTaskCount,
        .nativeSubmissionCount = statistics.nativeSubmissionCount,
        .rejectedSubmissionCount = statistics.rejectedSubmissionCount,
        .nativeCommandListCount = statistics.nativeCommandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static EncodedFrameGraphRuntimeStatistics EncodeRuntimeStatistics(
    const u32 nodeIndex,
    const FrameGraphRuntimeStatistics& statistics
)noexcept{
    EncodedFrameGraphRuntimeStatistics encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.deviceGeneration = statistics.deviceGeneration;
    encoded.graphGeneration = statistics.graphGeneration;
    encoded.planGeneration = statistics.planGeneration;
    encoded.recordingAttemptGeneration = statistics.recordingAttemptGeneration;
    encoded.compile = EncodeCompileRuntimeStatistics(statistics.compile);
    encoded.recording = EncodeRecordingRuntimeStatistics(statistics.recording);
    encoded.submission = EncodeSubmissionRuntimeStatistics(statistics.submission);
    return encoded;
}

[[nodiscard]] static bool DecodeRuntimeStatistics(
    const EncodedFrameGraphRuntimeStatistics& encoded,
    FrameGraphRuntimeStatistics& outStatistics
)noexcept{
    if(encoded.reserved != 0u)
        return false;

    outStatistics = {
        .graphGeneration = encoded.graphGeneration,
        .planGeneration = encoded.planGeneration,
        .recordingAttemptGeneration = encoded.recordingAttemptGeneration,
        .deviceGeneration = encoded.deviceGeneration,
        .compile = DecodeCompileRuntimeStatistics(encoded.compile),
        .recording = DecodeRecordingRuntimeStatistics(encoded.recording),
        .submission = DecodeSubmissionRuntimeStatistics(encoded.submission),
        .present = true,
    };
    return IsValidFrameGraphRuntimeStatistics(outStatistics);
}

[[nodiscard]] static bool PhysicalQueueRuntimeStatisticsRecordLess(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs
)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    if(lhs.statistics.queue.index != rhs.statistics.queue.index)
        return lhs.statistics.queue.index < rhs.statistics.queue.index;
    return lhs.statistics.queue.deviceGeneration < rhs.statistics.queue.deviceGeneration;
}

struct FrameGraphPhysicalQueueRuntimeStatisticsAccumulator{
    FrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    FrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
};

[[nodiscard]] static bool AccumulateBoundedCount(const u64 value, const u64 maximum, u64& total)noexcept{
    if(total > maximum || value > maximum - total)
        return false;
    total += value;
    return true;
}

[[nodiscard]] static bool FrameGraphCompileBarrierCount(
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

[[nodiscard]] static bool AccumulatePhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics,
    FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total
)noexcept{
    u64 ownerBarrierCount = 0u;
    if(!FrameGraphCompileBarrierCount(ownerStatistics.compile, ownerBarrierCount))
        return false;

    const FrameGraphPhysicalQueueCompileRuntimeStatistics& compile = statistics.compile;
    const FrameGraphCompileRuntimeStatistics& ownerCompile = ownerStatistics.compile;
    if(
        !AccumulateBoundedCount(compile.taskCount, ownerCompile.taskCount, total.compile.taskCount)
        || !AccumulateBoundedCount(compile.packetCount, ownerCompile.packetCount, total.compile.packetCount)
        || !AccumulateBoundedCount(compile.mergedTaskCount, ownerCompile.mergedTaskCount, total.compile.mergedTaskCount)
        || !AccumulateBoundedCount(
            compile.prologueBarrierCount,
            ownerBarrierCount,
            total.compile.prologueBarrierCount
        )
        || !AccumulateBoundedCount(
            compile.epilogueBarrierCount,
            ownerBarrierCount,
            total.compile.prologueBarrierCount
        )
        || !AccumulateBoundedCount(
            compile.ownershipReleaseBarrierCount,
            ownerCompile.ownershipReleaseBarrierCount,
            total.compile.ownershipReleaseBarrierCount
        )
        || !AccumulateBoundedCount(
            compile.ownershipAcquireBarrierCount,
            ownerCompile.ownershipAcquireBarrierCount,
            total.compile.ownershipAcquireBarrierCount
        )
        || !AccumulateBoundedCount(
            compile.incomingLogicalOwnershipTransferCount,
            ownerCompile.logicalOwnershipTransferCount,
            total.compile.incomingLogicalOwnershipTransferCount
        )
        || !AccumulateBoundedCount(
            compile.outgoingLogicalOwnershipTransferCount,
            ownerCompile.logicalOwnershipTransferCount,
            total.compile.outgoingLogicalOwnershipTransferCount
        )
        || !AccumulateBoundedCount(
            compile.incomingLogicalOwnershipTransferSignatureCount,
            ownerCompile.logicalOwnershipTransferSignatureCount,
            total.compile.incomingLogicalOwnershipTransferSignatureCount
        )
        || !AccumulateBoundedCount(
            compile.outgoingLogicalOwnershipTransferSignatureCount,
            ownerCompile.logicalOwnershipTransferSignatureCount,
            total.compile.outgoingLogicalOwnershipTransferSignatureCount
        )
        || !AccumulateBoundedCount(
            compile.incomingRepeatedOwnershipTransferSignatureCount,
            ownerCompile.repeatedOwnershipTransferSignatureCount,
            total.compile.incomingRepeatedOwnershipTransferSignatureCount
        )
        || !AccumulateBoundedCount(
            compile.outgoingRepeatedOwnershipTransferSignatureCount,
            ownerCompile.repeatedOwnershipTransferSignatureCount,
            total.compile.outgoingRepeatedOwnershipTransferSignatureCount
        )
    )
        return false;

    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& recording = statistics.recording;
    const FrameGraphRecordingRuntimeStatistics& ownerRecording = ownerStatistics.recording;
    if(
        !AccumulateBoundedCount(recording.packetCount, ownerRecording.packetCount, total.recording.packetCount)
        || !AccumulateBoundedCount(recording.taskCount, ownerRecording.taskCount, total.recording.taskCount)
        || !AccumulateBoundedCount(
            recording.commandListCount,
            ownerRecording.commandListCount,
            total.recording.commandListCount
        )
        || !AccumulateBoundedCount(recording.barrierCount, ownerRecording.barrierCount, total.recording.barrierCount)
        || !AccumulateBoundedCount(
            recording.workerRoutedPacketCount,
            ownerRecording.workerRoutedPacketCount,
            total.recording.workerRoutedPacketCount
        )
        || !AccumulateBoundedCount(
            recording.parallelPacketCount,
            ownerRecording.parallelPacketCount,
            total.recording.parallelPacketCount
        )
    )
        return false;
    if(
        total.recording.taskCount - total.recording.packetCount
            > ownerRecording.taskCount - ownerRecording.packetCount
        || total.recording.commandListCount - total.recording.packetCount
            > ownerRecording.commandListCount - ownerRecording.packetCount
    )
        return false;

    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& submission = statistics.submission;
    const FrameGraphSubmissionRuntimeStatistics& ownerSubmission = ownerStatistics.submission;
    if(
        !AccumulateBoundedCount(
            submission.acceptedPacketCount,
            ownerSubmission.acceptedPacketCount,
            total.submission.acceptedPacketCount
        )
        || !AccumulateBoundedCount(
            submission.acceptedTaskCount,
            ownerSubmission.acceptedTaskCount,
            total.submission.acceptedTaskCount
        )
        || !AccumulateBoundedCount(
            submission.rejectedPacketCount,
            ownerSubmission.rejectedPacketCount,
            total.submission.rejectedPacketCount
        )
        || !AccumulateBoundedCount(
            submission.rejectedTaskCount,
            ownerSubmission.rejectedTaskCount,
            total.submission.rejectedTaskCount
        )
        || !AccumulateBoundedCount(
            submission.nativeSubmissionCount,
            ownerSubmission.nativeSubmissionCount,
            total.submission.nativeSubmissionCount
        )
        || !AccumulateBoundedCount(
            submission.rejectedSubmissionCount,
            ownerSubmission.rejectedSubmissionCount,
            total.submission.rejectedSubmissionCount
        )
        || !AccumulateBoundedCount(
            submission.nativeCommandListCount,
            ownerSubmission.nativeCommandListCount,
            total.submission.nativeCommandListCount
        )
        || !AccumulateBoundedCount(
            submission.plannedWaitTokenCount,
            ownerSubmission.plannedWaitTokenCount,
            total.submission.plannedWaitTokenCount
        )
        || !AccumulateBoundedCount(
            submission.sameQueueWaitElisionCount,
            ownerSubmission.sameQueueWaitElisionCount,
            total.submission.sameQueueWaitElisionCount
        )
        || !AccumulateBoundedCount(
            submission.timelineWaitCount,
            ownerSubmission.timelineWaitCount,
            total.submission.timelineWaitCount
        )
        || !AccumulateBoundedCount(
            submission.mergedTimelineWaitCount,
            ownerSubmission.mergedTimelineWaitCount,
            total.submission.mergedTimelineWaitCount
        )
        || !AccumulateBoundedCount(
            submission.acceptedFrontierSubmissionCount,
            ownerSubmission.acceptedFrontierSubmissionCount,
            total.submission.acceptedFrontierSubmissionCount
        )
    )
        return false;

    return
        total.submission.acceptedTaskCount - total.submission.acceptedPacketCount
            <= ownerSubmission.acceptedTaskCount - ownerSubmission.acceptedPacketCount
        && total.submission.rejectedTaskCount - total.submission.rejectedPacketCount
            <= ownerSubmission.rejectedTaskCount - ownerSubmission.rejectedPacketCount
        && total.submission.nativeCommandListCount - total.submission.nativeSubmissionCount
            <= ownerSubmission.nativeCommandListCount - ownerSubmission.nativeSubmissionCount
    ;
}

[[nodiscard]] static bool ValidatePhysicalQueueRuntimeStatisticsOwner(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record,
    const usize nodeCount,
    const FrameGraphNodeKind::Enum ownerKind,
    const FrameGraphRuntimeStatistics& ownerStatistics
)noexcept{
    return static_cast<usize>(record.ownerNodeIndex) < nodeCount
        && ownerKind == FrameGraphNodeKind::Pass
        && IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(record.statistics, ownerStatistics)
    ;
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics
EncodePhysicalQueueCompileRuntimeStatistics(
    const FrameGraphPhysicalQueueCompileRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics{
        .taskCount = statistics.taskCount,
        .packetCount = statistics.packetCount,
        .mergedTaskCount = statistics.mergedTaskCount,
        .prologueBarrierCount = statistics.prologueBarrierCount,
        .epilogueBarrierCount = statistics.epilogueBarrierCount,
        .ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount,
        .ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount,
        .incomingLogicalOwnershipTransferCount = statistics.incomingLogicalOwnershipTransferCount,
        .outgoingLogicalOwnershipTransferCount = statistics.outgoingLogicalOwnershipTransferCount,
        .incomingLogicalOwnershipTransferSignatureCount = statistics.incomingLogicalOwnershipTransferSignatureCount,
        .outgoingLogicalOwnershipTransferSignatureCount = statistics.outgoingLogicalOwnershipTransferSignatureCount,
        .incomingRepeatedOwnershipTransferSignatureCount = statistics.incomingRepeatedOwnershipTransferSignatureCount,
        .outgoingRepeatedOwnershipTransferSignatureCount = statistics.outgoingRepeatedOwnershipTransferSignatureCount,
        .concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount,
    };
}

[[nodiscard]] static FrameGraphPhysicalQueueCompileRuntimeStatistics DecodePhysicalQueueCompileRuntimeStatistics(
    const EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics& statistics
)noexcept{
    return FrameGraphPhysicalQueueCompileRuntimeStatistics{
        .taskCount = statistics.taskCount,
        .packetCount = statistics.packetCount,
        .mergedTaskCount = statistics.mergedTaskCount,
        .prologueBarrierCount = statistics.prologueBarrierCount,
        .epilogueBarrierCount = statistics.epilogueBarrierCount,
        .ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount,
        .ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount,
        .incomingLogicalOwnershipTransferCount = statistics.incomingLogicalOwnershipTransferCount,
        .outgoingLogicalOwnershipTransferCount = statistics.outgoingLogicalOwnershipTransferCount,
        .incomingLogicalOwnershipTransferSignatureCount = statistics.incomingLogicalOwnershipTransferSignatureCount,
        .outgoingLogicalOwnershipTransferSignatureCount = statistics.outgoingLogicalOwnershipTransferSignatureCount,
        .incomingRepeatedOwnershipTransferSignatureCount = statistics.incomingRepeatedOwnershipTransferSignatureCount,
        .outgoingRepeatedOwnershipTransferSignatureCount = statistics.outgoingRepeatedOwnershipTransferSignatureCount,
        .concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount,
    };
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics
EncodePhysicalQueueRecordingRuntimeStatistics(
    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics{
        .packetCount = statistics.packetCount,
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .barrierCount = statistics.barrierCount,
        .workerRoutedPacketCount = statistics.workerRoutedPacketCount,
        .parallelPacketCount = statistics.parallelPacketCount,
        .commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds,
        .graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds,
        .taskRecordSeconds = statistics.taskRecordSeconds,
        .recordingSeconds = statistics.recordingSeconds,
    };
}

[[nodiscard]] static FrameGraphPhysicalQueueRecordingRuntimeStatistics
DecodePhysicalQueueRecordingRuntimeStatistics(
    const EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics& statistics
)noexcept{
    return FrameGraphPhysicalQueueRecordingRuntimeStatistics{
        .packetCount = statistics.packetCount,
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .barrierCount = statistics.barrierCount,
        .workerRoutedPacketCount = statistics.workerRoutedPacketCount,
        .parallelPacketCount = statistics.parallelPacketCount,
        .commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds,
        .graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds,
        .taskRecordSeconds = statistics.taskRecordSeconds,
        .recordingSeconds = statistics.recordingSeconds,
    };
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics
EncodePhysicalQueueSubmissionRuntimeStatistics(
    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics{
        .acceptedPacketCount = statistics.acceptedPacketCount,
        .acceptedTaskCount = statistics.acceptedTaskCount,
        .rejectedPacketCount = statistics.rejectedPacketCount,
        .rejectedTaskCount = statistics.rejectedTaskCount,
        .nativeSubmissionCount = statistics.nativeSubmissionCount,
        .rejectedSubmissionCount = statistics.rejectedSubmissionCount,
        .nativeCommandListCount = statistics.nativeCommandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static FrameGraphPhysicalQueueSubmissionRuntimeStatistics
DecodePhysicalQueueSubmissionRuntimeStatistics(
    const EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics& statistics
)noexcept{
    return FrameGraphPhysicalQueueSubmissionRuntimeStatistics{
        .acceptedPacketCount = statistics.acceptedPacketCount,
        .acceptedTaskCount = statistics.acceptedTaskCount,
        .rejectedPacketCount = statistics.rejectedPacketCount,
        .rejectedTaskCount = statistics.rejectedTaskCount,
        .nativeSubmissionCount = statistics.nativeSubmissionCount,
        .rejectedSubmissionCount = statistics.rejectedSubmissionCount,
        .nativeCommandListCount = statistics.nativeCommandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueRuntimeStatistics EncodePhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record
)noexcept{
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics = record.statistics;
    EncodedFrameGraphPhysicalQueueRuntimeStatistics encoded;
    encoded.ownerNodeIndex = record.ownerNodeIndex;
    encoded.queue = EncodeQueue(statistics.queue);
    encoded.queueClass = statistics.queueClass;
    encoded.compile = EncodePhysicalQueueCompileRuntimeStatistics(statistics.compile);
    encoded.recording = EncodePhysicalQueueRecordingRuntimeStatistics(statistics.recording);
    encoded.submission = EncodePhysicalQueueSubmissionRuntimeStatistics(statistics.submission);
    return encoded;
}

[[nodiscard]] static bool DecodePhysicalQueueRuntimeStatistics(
    const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encoded,
    const FrameGraphRuntimeStatistics& ownerStatistics,
    FrameGraphPhysicalQueueRuntimeStatisticsRecord& outRecord
)noexcept{
    if(
        encoded.reserved[0u] != 0u
        || encoded.reserved[1u] != 0u
        || encoded.reserved[2u] != 0u
        || encoded.reserved[3u] != 0u
        || encoded.reserved[4u] != 0u
        || encoded.reserved[5u] != 0u
        || encoded.reserved[6u] != 0u
    )
        return false;

    outRecord = {
        .ownerNodeIndex = encoded.ownerNodeIndex,
        .statistics = {
            .graphGeneration = ownerStatistics.graphGeneration,
            .planGeneration = ownerStatistics.planGeneration,
            .recordingAttemptGeneration = ownerStatistics.recordingAttemptGeneration,
            .deviceGeneration = ownerStatistics.deviceGeneration,
            .queue = DecodeQueue(encoded.queue),
            .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
            .compile = DecodePhysicalQueueCompileRuntimeStatistics(encoded.compile),
            .recording = DecodePhysicalQueueRecordingRuntimeStatistics(encoded.recording),
            .submission = DecodePhysicalQueueSubmissionRuntimeStatistics(encoded.submission),
        },
    };
    return IsValidFrameGraphPhysicalQueueRuntimeStatistics(outRecord.statistics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidFrameGraphNodeKind(const FrameGraphNodeKind::Enum kind)noexcept{
    switch(kind){
    case FrameGraphNodeKind::Pass:
    case FrameGraphNodeKind::Resource:
    case FrameGraphNodeKind::External:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphEdgeKind(const FrameGraphEdgeKind::Enum kind)noexcept{
    switch(kind){
    case FrameGraphEdgeKind::Reads:
    case FrameGraphEdgeKind::Writes:
    case FrameGraphEdgeKind::DependsOn:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueClass(const FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case FrameGraphQueueClass::Graphics:
    case FrameGraphQueueClass::Compute:
    case FrameGraphQueueClass::Transfer:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignmentReason(const FrameGraphQueueAssignmentReason::Enum reason)noexcept{
    switch(reason){
    case FrameGraphQueueAssignmentReason::RequiredGraphics:
    case FrameGraphQueueAssignmentReason::PreferredQueue:
    case FrameGraphQueueAssignmentReason::DedicatedCompute:
    case FrameGraphQueueAssignmentReason::DedicatedTransfer:
    case FrameGraphQueueAssignmentReason::Fallback:
    case FrameGraphQueueAssignmentReason::ConservativeAny:
    case FrameGraphQueueAssignmentReason::SameClassRouting:
    case FrameGraphQueueAssignmentReason::CompilerOverride:
    case FrameGraphQueueAssignmentReason::ScoredAny:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignmentAcceptance(
    const FrameGraphQueueAssignmentAcceptance::Enum acceptance
)noexcept{
    switch(acceptance){
    case FrameGraphQueueAssignmentAcceptance::NotAccepted:
    case FrameGraphQueueAssignmentAcceptance::First:
    case FrameGraphQueueAssignmentAcceptance::Unchanged:
    case FrameGraphQueueAssignmentAcceptance::Changed:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphTaskPacketizationDecision(
    const FrameGraphTaskPacketizationDecision::Enum decision
)noexcept{
    switch(decision){
    case FrameGraphTaskPacketizationDecision::FirstTask:
    case FrameGraphTaskPacketizationDecision::MergeNotRequested:
    case FrameGraphTaskPacketizationDecision::TaskForcesBoundary:
    case FrameGraphTaskPacketizationDecision::QueueChanged:
    case FrameGraphTaskPacketizationDecision::PrecedingTaskForcesBoundary:
    case FrameGraphTaskPacketizationDecision::ScoredMergeIneligible:
    case FrameGraphTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency:
    case FrameGraphTaskPacketizationDecision::CrossQueueConsumerFrontier:
    case FrameGraphTaskPacketizationDecision::MergedExplicit:
    case FrameGraphTaskPacketizationDecision::MergedFrontierScored:
    case FrameGraphTaskPacketizationDecision::ScoredMergeDomainMismatch:
        return true;
    default:
        return false;
    }
}

bool IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept{
    if(
        !assignment.present
        || !assignment.initialQueue.valid()
        || !assignment.plannedQueue.valid()
        || !__hidden_telemetry_frame_graph::IsCanonicalQueue(assignment.acceptedQueue)
        || !__hidden_telemetry_frame_graph::IsCanonicalQueue(assignment.previousAcceptedQueue)
        || assignment.initialQueue.deviceGeneration != assignment.plannedQueue.deviceGeneration
        || !IsValidFrameGraphQueueClass(assignment.queueClass)
        || !IsValidFrameGraphQueueAssignmentReason(assignment.reason)
        || !IsValidFrameGraphQueueAssignmentAcceptance(assignment.acceptance)
        || (
            static_cast<u8>(assignment.modifiers)
            & static_cast<u8>(~static_cast<u8>(FrameGraphQueueAssignmentModifier::All))
        ) != 0u
        || assignment.score.total != __hidden_telemetry_frame_graph::QueueAssignmentScoreTotal(assignment.score)
    )
        return false;

    const u16 deviceGeneration = assignment.plannedQueue.deviceGeneration;
    if(assignment.acceptedQueue.valid() && assignment.acceptedQueue.deviceGeneration != deviceGeneration)
        return false;
    if(assignment.previousAcceptedQueue.valid() && assignment.previousAcceptedQueue.deviceGeneration != deviceGeneration)
        return false;

    switch(assignment.acceptance){
    case FrameGraphQueueAssignmentAcceptance::NotAccepted:
        return !assignment.acceptedQueue.valid();
    case FrameGraphQueueAssignmentAcceptance::First:
        return assignment.acceptedQueue == assignment.plannedQueue && !assignment.previousAcceptedQueue.valid();
    case FrameGraphQueueAssignmentAcceptance::Unchanged:
        return assignment.acceptedQueue == assignment.plannedQueue
            && assignment.previousAcceptedQueue == assignment.acceptedQueue
        ;
    case FrameGraphQueueAssignmentAcceptance::Changed:
        return assignment.acceptedQueue == assignment.plannedQueue
            && assignment.previousAcceptedQueue.valid()
            && assignment.previousAcceptedQueue != assignment.acceptedQueue
        ;
    default:
        return false;
    }
}

bool IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept{
    return compiledTask.present
        && compiledTask.planGeneration != 0u
        && compiledTask.packetIndex != Limit<u32>::s_Max
        && IsValidFrameGraphTaskPacketizationDecision(compiledTask.packetizationDecision)
    ;
}

bool IsValidFrameGraphRuntimeStatistics(const FrameGraphRuntimeStatistics& statistics)noexcept{
    if(
        !statistics.present
        || statistics.graphGeneration == 0u
        || statistics.planGeneration == 0u
        || statistics.recordingAttemptGeneration == 0u
        || statistics.deviceGeneration == 0u
    )
        return false;

    const FrameGraphCompileRuntimeStatistics& compile = statistics.compile;
    if(
        compile.packetCount > compile.taskCount
        || compile.mergedTaskCount != compile.taskCount - compile.packetCount
        || compile.directResourceUseCount > compile.resourceUseCount
        || compile.expandedResourceSetMemberUseCount != compile.resourceUseCount - compile.directResourceUseCount
        || compile.payloadObjectCount > compile.taskCount
        || compile.logicalOwnershipTransferSignatureCount > compile.logicalOwnershipTransferCount
        || compile.repeatedOwnershipTransferSignatureCount > compile.logicalOwnershipTransferSignatureCount
        || compile.concurrentSharingCouldAvoidTransferCount > compile.logicalOwnershipTransferCount
        || compile.concurrentSharingAdviceResourceCount > compile.resourceCount
        || compile.logicalOwnershipTransferInternalCount > compile.logicalOwnershipTransferCount
    )
        return false;

    u64 remainingOwnershipTransferCount = compile.logicalOwnershipTransferCount
        - compile.logicalOwnershipTransferInternalCount
    ;
    if(compile.logicalOwnershipTransferExternalImportCount > remainingOwnershipTransferCount)
        return false;
    remainingOwnershipTransferCount -= compile.logicalOwnershipTransferExternalImportCount;
    if(compile.logicalOwnershipTransferExternalExportCount != remainingOwnershipTransferCount)
        return false;

    const FrameGraphRecordingRuntimeStatistics& recording = statistics.recording;
    if(
        recording.packetCount > compile.packetCount
        || recording.taskCount > compile.taskCount
        || recording.packetCount > recording.taskCount
        || recording.packetCount > recording.commandListCount
        || recording.workerRoutedPacketCount > recording.packetCount
        || recording.parallelPacketCount > recording.packetCount
    )
        return false;

    const FrameGraphSubmissionRuntimeStatistics& submission = statistics.submission;
    if(
        submission.acceptedPacketCount > compile.packetCount
        || submission.acceptedTaskCount > compile.taskCount
    )
        return false;
    if(
        submission.rejectedPacketCount > compile.packetCount - submission.acceptedPacketCount
        || submission.rejectedTaskCount > compile.taskCount - submission.acceptedTaskCount
        || submission.acceptedPacketCount > submission.acceptedTaskCount
        || submission.rejectedPacketCount > submission.rejectedTaskCount
        || submission.nativeSubmissionCount > submission.acceptedPacketCount
        || submission.rejectedSubmissionCount > submission.rejectedPacketCount
        || submission.acceptedFrontierSubmissionCount > submission.nativeSubmissionCount
        || submission.nativeSubmissionCount > recording.packetCount
        || submission.nativeSubmissionCount > submission.nativeCommandListCount
        || submission.nativeCommandListCount > recording.commandListCount
        || submission.sameQueueWaitElisionCount > submission.plannedWaitTokenCount
    )
        return false;

    u64 remainingWaitTokenCount = submission.plannedWaitTokenCount - submission.sameQueueWaitElisionCount;
    if(submission.mergedTimelineWaitCount > remainingWaitTokenCount)
        return false;
    remainingWaitTokenCount -= submission.mergedTimelineWaitCount;
    if(submission.timelineWaitCount != remainingWaitTokenCount)
        return false;

    const f64 durations[] = {
        statistics.compile.declarationSeconds,
        statistics.compile.analysisSeconds,
        statistics.compile.validationSeconds,
        statistics.compile.dependencyAnalysisSeconds,
        statistics.compile.hazardAnalysisSeconds,
        statistics.compile.topologicalOrderSeconds,
        statistics.compile.queueAssignmentSeconds,
        statistics.compile.planningSeconds,
        statistics.compile.packetizationSeconds,
        statistics.compile.resourceStatePlanningSeconds,
        statistics.compile.packetDependencyPlanningSeconds,
        statistics.compile.totalSeconds,
        statistics.recording.commandListAcquisitionSeconds,
        statistics.recording.graphBarrierRecordingSeconds,
        statistics.recording.taskRecordSeconds,
        statistics.recording.recordingSeconds,
        statistics.recording.recordingElapsedSeconds,
        statistics.recording.readyFrontierElapsedSeconds,
        statistics.recording.readyFrontierWorkerBusySeconds,
        statistics.recording.readyFrontierWorkerCapacitySeconds,
        statistics.submission.submissionSeconds,
    };
    for(const f64 duration : durations){
        if(duration < 0.0 || !IsFinite(duration))
            return false;
    }
    return true;
}

bool IsValidFrameGraphPhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics
)noexcept{
    if(
        statistics.graphGeneration == 0u
        || statistics.planGeneration == 0u
        || statistics.recordingAttemptGeneration == 0u
        || statistics.deviceGeneration == 0u
        || !statistics.queue.valid()
        || statistics.queue.deviceGeneration != statistics.deviceGeneration
        || !IsValidFrameGraphQueueClass(statistics.queueClass)
    )
        return false;

    const FrameGraphPhysicalQueueCompileRuntimeStatistics& compile = statistics.compile;
    if(
        compile.packetCount > compile.taskCount
        || compile.mergedTaskCount != compile.taskCount - compile.packetCount
        || (compile.packetCount == 0u && compile.taskCount != 0u)
        || compile.incomingLogicalOwnershipTransferSignatureCount
            > compile.incomingLogicalOwnershipTransferCount
        || compile.outgoingLogicalOwnershipTransferSignatureCount
            > compile.outgoingLogicalOwnershipTransferCount
        || compile.incomingRepeatedOwnershipTransferSignatureCount
            > compile.incomingLogicalOwnershipTransferSignatureCount
        || compile.outgoingRepeatedOwnershipTransferSignatureCount
            > compile.outgoingLogicalOwnershipTransferSignatureCount
    )
        return false;
    if(
        compile.concurrentSharingAdviceResourceCount
            > compile.incomingLogicalOwnershipTransferSignatureCount
        && compile.concurrentSharingAdviceResourceCount
            - compile.incomingLogicalOwnershipTransferSignatureCount
            > compile.outgoingLogicalOwnershipTransferSignatureCount
    )
        return false;

    if(compile.prologueBarrierCount > Limit<u64>::s_Max - compile.epilogueBarrierCount)
        return false;
    const u64 plannedBarrierCount = compile.prologueBarrierCount + compile.epilogueBarrierCount;
    if(
        compile.ownershipReleaseBarrierCount > plannedBarrierCount
        || compile.ownershipAcquireBarrierCount
            > plannedBarrierCount - compile.ownershipReleaseBarrierCount
        || (compile.taskCount == 0u && plannedBarrierCount != 0u)
    )
        return false;

    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& recording = statistics.recording;
    if(
        recording.packetCount > compile.packetCount
        || recording.taskCount > compile.taskCount
        || recording.packetCount > recording.taskCount
        || recording.packetCount > recording.commandListCount
        || recording.barrierCount > plannedBarrierCount
        || recording.workerRoutedPacketCount > recording.packetCount
        || recording.parallelPacketCount > recording.packetCount
    )
        return false;
    if(recording.taskCount - recording.packetCount > compile.mergedTaskCount)
        return false;
    if(
        recording.packetCount == 0u
        && (
            recording.taskCount != 0u
            || recording.commandListCount != 0u
            || recording.barrierCount != 0u
            || recording.workerRoutedPacketCount != 0u
            || recording.parallelPacketCount != 0u
            || recording.commandListAcquisitionSeconds != 0.0
            || recording.graphBarrierRecordingSeconds != 0.0
            || recording.taskRecordSeconds != 0.0
            || recording.recordingSeconds != 0.0
        )
    )
        return false;

    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& submission = statistics.submission;
    if(
        submission.acceptedPacketCount > compile.packetCount
        || submission.acceptedTaskCount > compile.taskCount
    )
        return false;
    if(
        submission.rejectedPacketCount > compile.packetCount - submission.acceptedPacketCount
        || submission.rejectedTaskCount > compile.taskCount - submission.acceptedTaskCount
        || submission.acceptedPacketCount > submission.acceptedTaskCount
        || submission.rejectedPacketCount > submission.rejectedTaskCount
        || submission.nativeSubmissionCount > submission.acceptedPacketCount
        || submission.rejectedSubmissionCount > submission.rejectedPacketCount
        || submission.acceptedFrontierSubmissionCount > submission.nativeSubmissionCount
        || submission.nativeSubmissionCount > recording.packetCount
        || submission.nativeSubmissionCount > submission.nativeCommandListCount
        || submission.nativeCommandListCount > recording.commandListCount
        || submission.sameQueueWaitElisionCount > submission.plannedWaitTokenCount
    )
        return false;

    const u64 acceptedMergedTaskCount = submission.acceptedTaskCount - submission.acceptedPacketCount;
    const u64 rejectedMergedTaskCount = submission.rejectedTaskCount - submission.rejectedPacketCount;
    if(
        acceptedMergedTaskCount > compile.mergedTaskCount
        || rejectedMergedTaskCount > compile.mergedTaskCount - acceptedMergedTaskCount
    )
        return false;
    if(submission.acceptedPacketCount == 0u && submission.acceptedTaskCount != 0u)
        return false;
    if(submission.rejectedPacketCount == 0u && submission.rejectedTaskCount != 0u)
        return false;
    if(
        submission.nativeSubmissionCount == 0u
        && (
            submission.nativeCommandListCount != 0u
            || submission.plannedWaitTokenCount != 0u
            || submission.sameQueueWaitElisionCount != 0u
            || submission.timelineWaitCount != 0u
            || submission.mergedTimelineWaitCount != 0u
            || submission.acceptedFrontierSubmissionCount != 0u
            || submission.submissionSeconds != 0.0
        )
    )
        return false;

    u64 remainingWaitTokenCount = submission.plannedWaitTokenCount - submission.sameQueueWaitElisionCount;
    if(submission.mergedTimelineWaitCount > remainingWaitTokenCount)
        return false;
    remainingWaitTokenCount -= submission.mergedTimelineWaitCount;
    if(submission.timelineWaitCount != remainingWaitTokenCount)
        return false;

    const f64 durations[] = {
        recording.commandListAcquisitionSeconds,
        recording.graphBarrierRecordingSeconds,
        recording.taskRecordSeconds,
        recording.recordingSeconds,
        submission.submissionSeconds,
    };
    for(const f64 duration : durations){
        if(duration < 0.0 || !IsFinite(duration))
            return false;
    }
    return true;
}

bool IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics
)noexcept{
    if(
        !IsValidFrameGraphRuntimeStatistics(ownerStatistics)
        || !IsValidFrameGraphPhysicalQueueRuntimeStatistics(statistics)
        || statistics.graphGeneration != ownerStatistics.graphGeneration
        || statistics.planGeneration != ownerStatistics.planGeneration
        || statistics.recordingAttemptGeneration != ownerStatistics.recordingAttemptGeneration
        || statistics.deviceGeneration != ownerStatistics.deviceGeneration
    )
        return false;

    const FrameGraphPhysicalQueueCompileRuntimeStatistics& compile = statistics.compile;
    const FrameGraphCompileRuntimeStatistics& ownerCompile = ownerStatistics.compile;
    if(
        compile.taskCount > ownerCompile.taskCount
        || compile.packetCount > ownerCompile.packetCount
        || compile.mergedTaskCount > ownerCompile.mergedTaskCount
        || compile.ownershipReleaseBarrierCount > ownerCompile.ownershipReleaseBarrierCount
        || compile.ownershipAcquireBarrierCount > ownerCompile.ownershipAcquireBarrierCount
        || compile.incomingLogicalOwnershipTransferCount > ownerCompile.logicalOwnershipTransferCount
        || compile.outgoingLogicalOwnershipTransferCount > ownerCompile.logicalOwnershipTransferCount
        || compile.incomingLogicalOwnershipTransferSignatureCount
            > ownerCompile.logicalOwnershipTransferSignatureCount
        || compile.outgoingLogicalOwnershipTransferSignatureCount
            > ownerCompile.logicalOwnershipTransferSignatureCount
        || compile.incomingRepeatedOwnershipTransferSignatureCount
            > ownerCompile.repeatedOwnershipTransferSignatureCount
        || compile.outgoingRepeatedOwnershipTransferSignatureCount
            > ownerCompile.repeatedOwnershipTransferSignatureCount
        || compile.concurrentSharingAdviceResourceCount > ownerCompile.concurrentSharingAdviceResourceCount
    )
        return false;

    u64 ownerBarrierCount = 0u;
    if(!__hidden_telemetry_frame_graph::FrameGraphCompileBarrierCount(ownerCompile, ownerBarrierCount))
        return false;
    if(
        compile.prologueBarrierCount > ownerBarrierCount
        || compile.epilogueBarrierCount > ownerBarrierCount - compile.prologueBarrierCount
    )
        return false;

    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& recording = statistics.recording;
    const FrameGraphRecordingRuntimeStatistics& ownerRecording = ownerStatistics.recording;
    if(
        recording.packetCount > ownerRecording.packetCount
        || recording.taskCount > ownerRecording.taskCount
        || recording.commandListCount > ownerRecording.commandListCount
        || recording.barrierCount > ownerRecording.barrierCount
        || recording.workerRoutedPacketCount > ownerRecording.workerRoutedPacketCount
        || recording.parallelPacketCount > ownerRecording.parallelPacketCount
        || recording.commandListAcquisitionSeconds > ownerRecording.commandListAcquisitionSeconds
        || recording.graphBarrierRecordingSeconds > ownerRecording.graphBarrierRecordingSeconds
        || recording.taskRecordSeconds > ownerRecording.taskRecordSeconds
        || recording.recordingSeconds > ownerRecording.recordingSeconds
    )
        return false;
    if(
        recording.taskCount - recording.packetCount
            > ownerRecording.taskCount - ownerRecording.packetCount
        || recording.commandListCount - recording.packetCount
            > ownerRecording.commandListCount - ownerRecording.packetCount
    )
        return false;

    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& submission = statistics.submission;
    const FrameGraphSubmissionRuntimeStatistics& ownerSubmission = ownerStatistics.submission;
    // Submission acceptance and per-queue snapshots visit packets in different orders, so their positive floating
    // duration sums can differ by one rounding bit. Counts remain the exact owner-conservation contract.
    return submission.acceptedPacketCount <= ownerSubmission.acceptedPacketCount
        && submission.acceptedTaskCount <= ownerSubmission.acceptedTaskCount
        && submission.rejectedPacketCount <= ownerSubmission.rejectedPacketCount
        && submission.rejectedTaskCount <= ownerSubmission.rejectedTaskCount
        && submission.nativeSubmissionCount <= ownerSubmission.nativeSubmissionCount
        && submission.rejectedSubmissionCount <= ownerSubmission.rejectedSubmissionCount
        && submission.nativeCommandListCount <= ownerSubmission.nativeCommandListCount
        && submission.plannedWaitTokenCount <= ownerSubmission.plannedWaitTokenCount
        && submission.sameQueueWaitElisionCount <= ownerSubmission.sameQueueWaitElisionCount
        && submission.timelineWaitCount <= ownerSubmission.timelineWaitCount
        && submission.mergedTimelineWaitCount <= ownerSubmission.mergedTimelineWaitCount
        && submission.acceptedFrontierSubmissionCount <= ownerSubmission.acceptedFrontierSubmissionCount
        && submission.acceptedTaskCount - submission.acceptedPacketCount
            <= ownerSubmission.acceptedTaskCount - ownerSubmission.acceptedPacketCount
        && submission.rejectedTaskCount - submission.rejectedPacketCount
            <= ownerSubmission.rejectedTaskCount - ownerSubmission.rejectedPacketCount
        && submission.nativeCommandListCount - submission.nativeSubmissionCount
            <= ownerSubmission.nativeCommandListCount - ownerSubmission.nativeSubmissionCount
    ;
}

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
){
    FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(arena);
    return BuildFrameGraphPayload(arena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, outPayload);
}

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(
        !FitsU32(nodes.size())
        || !FitsU32(edges.size())
        || !FitsU32(physicalQueueRuntimeStatistics.size())
    )
        return false;

    FrameGraphPhysicalQueueRuntimeStatisticsRecords orderedPhysicalQueueRuntimeStatistics(arena);
    orderedPhysicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatistics.size());
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : physicalQueueRuntimeStatistics)
        orderedPhysicalQueueRuntimeStatistics.push_back(record);
    Sort(
        orderedPhysicalQueueRuntimeStatistics.begin(),
        orderedPhysicalQueueRuntimeStatistics.end(),
        __hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess
    );

    usize stringTableBytes = 0u;
    usize queueAssignmentCount = 0u;
    usize compiledTaskCount = 0u;
    usize runtimeStatisticsCount = 0u;
    for(const FrameGraphNodeDesc& node : nodes){
        if(!__hidden_telemetry_frame_graph::ValidateNodeInput(node))
            return false;
        if(!AddStringTableTextReserveBytes(stringTableBytes, node.label))
            return false;
        if(node.queueAssignment.present)
            ++queueAssignmentCount;
        if(node.compiledTask.present)
            ++compiledTaskCount;
        if(node.runtimeStatistics.present)
            ++runtimeStatisticsCount;
    }
    if(!FitsU32(queueAssignmentCount) || !FitsU32(compiledTaskCount) || !FitsU32(runtimeStatisticsCount))
        return false;

    for(const FrameGraphEdgeDesc& edge : edges){
        if(!__hidden_telemetry_frame_graph::ValidateEdgeInput(edge, nodes.size()))
            return false;
    }

    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    __hidden_telemetry_frame_graph::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(usize statisticsIndex = 0u; statisticsIndex < orderedPhysicalQueueRuntimeStatistics.size(); ++statisticsIndex){
        const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record =
            orderedPhysicalQueueRuntimeStatistics[statisticsIndex]
        ;
        if(record.ownerNodeIndex >= nodes.size())
            return false;
        if(!__hidden_telemetry_frame_graph::ValidatePhysicalQueueRuntimeStatisticsOwner(
            record,
            nodes.size(),
            nodes[record.ownerNodeIndex].kind,
            nodes[record.ownerNodeIndex].runtimeStatistics
        ))
            return false;
        if(record.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = record.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!__hidden_telemetry_frame_graph::AccumulatePhysicalQueueRuntimeStatistics(
            record.statistics,
            nodes[record.ownerNodeIndex].runtimeStatistics,
            accumulatedStatistics
        ))
            return false;
        if(
            statisticsIndex != 0u
            && !__hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess(
                orderedPhysicalQueueRuntimeStatistics[statisticsIndex - 1u],
                record
            )
        )
            return false;
    }

    const bool hasQueueAssignments = queueAssignmentCount != 0u;
    const bool hasCompiledTasks = compiledTaskCount != 0u;
    const bool hasRuntimeStatistics = runtimeStatisticsCount != 0u;
    const bool hasPhysicalQueueRuntimeStatistics = !orderedPhysicalQueueRuntimeStatistics.empty();
    usize payloadBytes = hasPhysicalQueueRuntimeStatistics
        ? sizeof(EncodedFrameGraphPayloadHeaderV5)
        : (
            hasRuntimeStatistics
            ? sizeof(EncodedFrameGraphPayloadHeaderV4)
            : (
                hasCompiledTasks
                ? sizeof(EncodedFrameGraphPayloadHeaderV3)
                : (
                    hasQueueAssignments
                    ? sizeof(EncodedFrameGraphPayloadHeaderV2)
                    : sizeof(EncodedFrameGraphPayloadHeader)
                )
            )
        )
    ;
    if(
        !AddBinaryRepeatedReserveBytes(payloadBytes, nodes.size(), sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, edges.size(), sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            queueAssignmentCount,
            sizeof(EncodedFrameGraphQueueAssignment)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            compiledTaskCount,
            sizeof(EncodedFrameGraphCompiledTask)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            runtimeStatisticsCount,
            sizeof(EncodedFrameGraphRuntimeStatistics)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            orderedPhysicalQueueRuntimeStatistics.size(),
            sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics)
        )
        || !AddBinaryReserveBytes(payloadBytes, stringTableBytes)
    )
        return false;

    TelemetryBytes stringTable(arena);
    stringTable.reserve(stringTableBytes);
    Vector<EncodedFrameGraphNode, TelemetryArena> encodedNodes(arena);
    encodedNodes.reserve(nodes.size());

    for(const FrameGraphNodeDesc& node : nodes){
        u32 labelOffset = Limit<u32>::s_Max;
        if(!AppendStringTableText(stringTable, node.label, labelOffset))
            return false;

        EncodedFrameGraphNode encodedNode;
        encodedNode.nameHash = node.name.hash();
        encodedNode.labelOffset = labelOffset;
        encodedNode.kind = node.kind;
        encodedNode.flags = node.flags;
        encodedNodes.push_back(encodedNode);
    }

    outPayload.reserve(payloadBytes);
    if(hasPhysicalQueueRuntimeStatistics){
        EncodedFrameGraphPayloadHeaderV5 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        header.compiledTaskCount = static_cast<u32>(compiledTaskCount);
        header.runtimeStatisticsCount = static_cast<u32>(runtimeStatisticsCount);
        header.physicalQueueRuntimeStatisticsCount = static_cast<u32>(
            orderedPhysicalQueueRuntimeStatistics.size()
        );
        AppendPOD(outPayload, header);
    }
    else if(hasRuntimeStatistics){
        EncodedFrameGraphPayloadHeaderV4 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        header.compiledTaskCount = static_cast<u32>(compiledTaskCount);
        header.runtimeStatisticsCount = static_cast<u32>(runtimeStatisticsCount);
        AppendPOD(outPayload, header);
    }
    else if(hasCompiledTasks){
        EncodedFrameGraphPayloadHeaderV3 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        header.compiledTaskCount = static_cast<u32>(compiledTaskCount);
        AppendPOD(outPayload, header);
    }
    else if(hasQueueAssignments){
        EncodedFrameGraphPayloadHeaderV2 header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        header.queueAssignmentCount = static_cast<u32>(queueAssignmentCount);
        AppendPOD(outPayload, header);
    }
    else{
        EncodedFrameGraphPayloadHeader header;
        header.frameIndex = frameIndex;
        header.nodeCount = static_cast<u32>(nodes.size());
        header.edgeCount = static_cast<u32>(edges.size());
        header.stringTableBytes = static_cast<u32>(stringTable.size());
        AppendPOD(outPayload, header);
    }
    for(const EncodedFrameGraphNode& node : encodedNodes)
        AppendPOD(outPayload, node);
    for(const FrameGraphEdgeDesc& edge : edges){
        EncodedFrameGraphEdge encodedEdge;
        encodedEdge.fromNodeIndex = edge.fromNodeIndex;
        encodedEdge.toNodeIndex = edge.toNodeIndex;
        encodedEdge.kind = edge.kind;
        encodedEdge.flags = edge.flags;
        AppendPOD(outPayload, encodedEdge);
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].queueAssignment.present)
            AppendPOD(
                outPayload,
                __hidden_telemetry_frame_graph::EncodeQueueAssignment(nodeIndex, nodes[nodeIndex].queueAssignment)
            );
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].compiledTask.present)
            AppendPOD(
                outPayload,
                __hidden_telemetry_frame_graph::EncodeCompiledTask(nodeIndex, nodes[nodeIndex].compiledTask)
            );
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].runtimeStatistics.present)
            AppendPOD(
                outPayload,
                __hidden_telemetry_frame_graph::EncodeRuntimeStatistics(
                    nodeIndex,
                    nodes[nodeIndex].runtimeStatistics
                )
            );
    }
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : orderedPhysicalQueueRuntimeStatistics){
        AppendPOD(
            outPayload,
            __hidden_telemetry_frame_graph::EncodePhysicalQueueRuntimeStatistics(record)
        );
    }
    if(!stringTable.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, stringTable.data(), stringTable.size());

    return outPayload.size() == payloadBytes;
}

bool ParseFrameGraphPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    FrameGraphPayload& outPayload
){
    outPayload = FrameGraphPayload(arena);

    if(payloadBytes < sizeof(EncodedFrameGraphPayloadHeader) || !payload)
        return false;

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedFrameGraphPayloadHeader legacyHeader;
    if(!ReadPOD(encoded, cursor, legacyHeader))
        return false;
    if(!__hidden_telemetry_frame_graph::ValidateHeader(legacyHeader.magic, legacyHeader.reserved))
        return false;

    usize headerBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
    u32 physicalQueueRuntimeStatisticsCount = 0u;
    switch(legacyHeader.version){
    case s_FrameGraphLegacyPayloadVersion:
        headerBytes = sizeof(EncodedFrameGraphPayloadHeader);
        break;
    case s_FrameGraphQueueAssignmentPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV2 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV2);
        queueAssignmentCount = header.queueAssignmentCount;
        break;
    }
    case s_FrameGraphCompiledTaskPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV3 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV3);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        break;
    }
    case s_FrameGraphRuntimeStatisticsPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV4 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV4);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        runtimeStatisticsCount = header.runtimeStatisticsCount;
        break;
    }
    case s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV5 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV5);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        runtimeStatisticsCount = header.runtimeStatisticsCount;
        physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
        break;
    }
    default:
        return false;
    }
    outPayload.wireVersion = legacyHeader.version;
    if(
        queueAssignmentCount > legacyHeader.nodeCount
        || compiledTaskCount > legacyHeader.nodeCount
        || runtimeStatisticsCount > legacyHeader.nodeCount
    )
        return false;

    usize expectedBytes = headerBytes;
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, legacyHeader.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, legacyHeader.edgeCount, sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            queueAssignmentCount,
            sizeof(EncodedFrameGraphQueueAssignment)
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            compiledTaskCount,
            sizeof(EncodedFrameGraphCompiledTask)
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            runtimeStatisticsCount,
            sizeof(EncodedFrameGraphRuntimeStatistics)
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            physicalQueueRuntimeStatisticsCount,
            sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics)
        )
        || !AddBinaryReserveBytes(expectedBytes, legacyHeader.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return false;

    usize edgeOffset = headerBytes;
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, legacyHeader.nodeCount, sizeof(EncodedFrameGraphNode)))
        return false;

    usize queueAssignmentOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(queueAssignmentOffset, legacyHeader.edgeCount, sizeof(EncodedFrameGraphEdge)))
        return false;

    usize compiledTaskOffset = queueAssignmentOffset;
    if(!AddBinaryRepeatedReserveBytes(
        compiledTaskOffset,
        queueAssignmentCount,
        sizeof(EncodedFrameGraphQueueAssignment)
    ))
        return false;

    usize runtimeStatisticsOffset = compiledTaskOffset;
    if(!AddBinaryRepeatedReserveBytes(
        runtimeStatisticsOffset,
        compiledTaskCount,
        sizeof(EncodedFrameGraphCompiledTask)
    ))
        return false;

    usize physicalQueueRuntimeStatisticsOffset = runtimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(
        physicalQueueRuntimeStatisticsOffset,
        runtimeStatisticsCount,
        sizeof(EncodedFrameGraphRuntimeStatistics)
    ))
        return false;

    usize stringTableOffset = physicalQueueRuntimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        physicalQueueRuntimeStatisticsCount,
        sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics)
    ))
        return false;

    outPayload.frameIndex = legacyHeader.frameIndex;
    outPayload.nodes.reserve(legacyHeader.nodeCount);
    outPayload.edges.reserve(legacyHeader.edgeCount);
    outPayload.physicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatisticsCount);

    for(u32 nodeIndex = 0u; nodeIndex < legacyHeader.nodeCount; ++nodeIndex){
        EncodedFrameGraphNode encodedNode;
        if(!ReadPOD(encoded, cursor, encodedNode))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return false;

        AStringView labelView;
        if(!BinaryDetail::ReadStringTableTextView(
            encoded,
            stringTableOffset,
            legacyHeader.stringTableBytes,
            encodedNode.labelOffset,
            labelView
        ))
            return false;

        FrameGraphNodePayload& node = outPayload.nodes.emplace_back(arena);
        node.name = Name(encodedNode.nameHash);
        node.label.assign(labelView.data(), labelView.size());
        node.kind = static_cast<FrameGraphNodeKind::Enum>(encodedNode.kind);
        node.flags = encodedNode.flags;
    }

    for(u32 edgeIndex = 0u; edgeIndex < legacyHeader.edgeCount; ++edgeIndex){
        EncodedFrameGraphEdge encodedEdge;
        if(!ReadPOD(encoded, cursor, encodedEdge))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, legacyHeader.nodeCount))
            return false;

        FrameGraphEdgePayload& edge = outPayload.edges.emplace_back();
        edge.fromNodeIndex = encodedEdge.fromNodeIndex;
        edge.toNodeIndex = encodedEdge.toNodeIndex;
        edge.kind = static_cast<FrameGraphEdgeKind::Enum>(encodedEdge.kind);
        edge.flags = encodedEdge.flags;
    }

    u32 previousNodeIndex = 0u;
    for(u32 assignmentIndex = 0u; assignmentIndex < queueAssignmentCount; ++assignmentIndex){
        EncodedFrameGraphQueueAssignment encodedAssignment;
        if(!ReadPOD(encoded, cursor, encodedAssignment))
            return false;
        if(
            encodedAssignment.nodeIndex >= legacyHeader.nodeCount
            || (assignmentIndex != 0u && encodedAssignment.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedAssignment.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !__hidden_telemetry_frame_graph::DecodeQueueAssignment(
                encodedAssignment,
                outPayload.nodes[encodedAssignment.nodeIndex].queueAssignment
            )
        )
            return false;
        previousNodeIndex = encodedAssignment.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 compiledTaskIndex = 0u; compiledTaskIndex < compiledTaskCount; ++compiledTaskIndex){
        EncodedFrameGraphCompiledTask encodedCompiledTask;
        if(!ReadPOD(encoded, cursor, encodedCompiledTask))
            return false;
        if(
            encodedCompiledTask.nodeIndex >= legacyHeader.nodeCount
            || (compiledTaskIndex != 0u && encodedCompiledTask.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedCompiledTask.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !__hidden_telemetry_frame_graph::DecodeCompiledTask(
                encodedCompiledTask,
                outPayload.nodes[encodedCompiledTask.nodeIndex].compiledTask
            )
        )
            return false;
        previousNodeIndex = encodedCompiledTask.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 statisticsIndex = 0u; statisticsIndex < runtimeStatisticsCount; ++statisticsIndex){
        EncodedFrameGraphRuntimeStatistics encodedStatistics;
        if(!ReadPOD(encoded, cursor, encodedStatistics))
            return false;
        if(
            encodedStatistics.nodeIndex >= legacyHeader.nodeCount
            || (statisticsIndex != 0u && encodedStatistics.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedStatistics.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !__hidden_telemetry_frame_graph::DecodeRuntimeStatistics(
                encodedStatistics,
                outPayload.nodes[encodedStatistics.nodeIndex].runtimeStatistics
            )
        )
            return false;
        previousNodeIndex = encodedStatistics.nodeIndex;
    }

    FrameGraphPhysicalQueueRuntimeStatisticsRecord previousPhysicalQueueStatistics;
    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    __hidden_telemetry_frame_graph::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(u32 statisticsIndex = 0u; statisticsIndex < physicalQueueRuntimeStatisticsCount; ++statisticsIndex){
        EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
        FrameGraphPhysicalQueueRuntimeStatisticsRecord statistics;
        if(!ReadPOD(encoded, cursor, encodedStatistics))
            return false;
        if(encodedStatistics.ownerNodeIndex >= outPayload.nodes.size())
            return false;
        if(!__hidden_telemetry_frame_graph::DecodePhysicalQueueRuntimeStatistics(
            encodedStatistics,
            outPayload.nodes[encodedStatistics.ownerNodeIndex].runtimeStatistics,
            statistics
        ))
            return false;
        if(statistics.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = statistics.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!__hidden_telemetry_frame_graph::AccumulatePhysicalQueueRuntimeStatistics(
            statistics.statistics,
            outPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics,
            accumulatedStatistics
        ))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidatePhysicalQueueRuntimeStatisticsOwner(
            statistics,
            outPayload.nodes.size(),
            outPayload.nodes[statistics.ownerNodeIndex].kind,
            outPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics
        ))
            return false;
        if(
            statisticsIndex != 0u
            && !__hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess(
                previousPhysicalQueueStatistics,
                statistics
            )
        )
            return false;
        outPayload.physicalQueueRuntimeStatistics.push_back(statistics);
        previousPhysicalQueueStatistics = statistics;
    }

    return cursor == stringTableOffset;
}

bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const u32 streamId
){
    return Detail::RecordBuiltPayload(
        recorder,
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildFrameGraphPayload(arena, frameIndex, nodes, edges, payload);
        }
    );
}

bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const u32 streamId
){
    return Detail::RecordBuiltPayload(
        recorder,
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges, &physicalQueueRuntimeStatistics](
            TelemetryArena& arena,
            TelemetryBytes& payload
        ){
            return BuildFrameGraphPayload(
                arena,
                frameIndex,
                nodes,
                edges,
                physicalQueueRuntimeStatistics,
                payload
            );
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

