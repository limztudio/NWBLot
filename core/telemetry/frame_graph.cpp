// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include <core/alloc/scratch.h>
#include <global/algorithm.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool BuildFrameGraphPayloadImpl(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords* packetSubmissionStatistics,
    TelemetryBytes& outPayload
);

static constexpr f64 s_DoublePrecisionEpsilon = 2.2204460492503130808472633361816e-16;


[[nodiscard]] static bool IsValidStringTableText(const AStringView text)noexcept{
    return !text.empty()
        && !HasEmbeddedNull(text)
        && text.size() < static_cast<usize>(Limit<u32>::s_Max)
    ;
}


[[nodiscard]] static bool ValidateHeader(const u32 magic, const u16 reserved)noexcept{
    return magic == s_FrameGraphPayloadMagic
        && reserved == 0u
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

[[nodiscard]] static EncodedFrameGraphCompileRuntimeStatisticsV8 EncodeCompileRuntimeStatistics(
    const FrameGraphCompileRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphCompileRuntimeStatisticsV8{
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
        .resourceVersionCount = statistics.resourceVersionCount,
        .resourceVersionEdgeCount = statistics.resourceVersionEdgeCount,
    };
}

template<typename EncodedStatisticsT>
[[nodiscard]] static FrameGraphCompileRuntimeStatistics DecodeCompileRuntimeStatistics(
    const EncodedStatisticsT& statistics
)noexcept{
    FrameGraphCompileRuntimeStatistics decoded{
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
    if constexpr(IsSame_V<EncodedStatisticsT, EncodedFrameGraphCompileRuntimeStatisticsV8>){
        decoded.resourceVersionCount = statistics.resourceVersionCount;
        decoded.resourceVersionEdgeCount = statistics.resourceVersionEdgeCount;
    }
    return decoded;
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

[[nodiscard]] static EncodedFrameGraphSubmissionRuntimeStatisticsV6 EncodeSubmissionRuntimeStatistics(
    const FrameGraphSubmissionRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphSubmissionRuntimeStatisticsV6{
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
        .recoverySubmissionCount = statistics.recoverySubmissionCount,
    };
}

template<typename EncodedStatisticsT>
[[nodiscard]] static FrameGraphSubmissionRuntimeStatistics DecodeSubmissionRuntimeStatistics(
    const EncodedStatisticsT& statistics
)noexcept{
    FrameGraphSubmissionRuntimeStatistics decoded{
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
    if constexpr(IsSame_V<EncodedStatisticsT, EncodedFrameGraphSubmissionRuntimeStatisticsV6>)
        decoded.recoverySubmissionCount = statistics.recoverySubmissionCount;
    return decoded;
}

[[nodiscard]] static EncodedFrameGraphRuntimeStatisticsV8 EncodeRuntimeStatistics(
    const u32 nodeIndex,
    const FrameGraphRuntimeStatistics& statistics
)noexcept{
    EncodedFrameGraphRuntimeStatisticsV8 encoded;
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

template<typename EncodedStatisticsT>
[[nodiscard]] static bool DecodeRuntimeStatistics(
    const EncodedStatisticsT& encoded,
    FrameGraphRuntimeStatistics& outStatistics
)noexcept{
    if(encoded.reserved != 0u)
        return false;

    outStatistics = {
        .graphGeneration = encoded.graphGeneration,
        .planGeneration = encoded.planGeneration,
        .recordingAttemptGeneration = encoded.recordingAttemptGeneration,
        .compile = DecodeCompileRuntimeStatistics(encoded.compile),
        .recording = DecodeRecordingRuntimeStatistics(encoded.recording),
        .submission = DecodeSubmissionRuntimeStatistics(encoded.submission),
        .deviceGeneration = encoded.deviceGeneration,
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
        || !AccumulateBoundedCount(
            submission.recoverySubmissionCount,
            ownerSubmission.recoverySubmissionCount,
            total.submission.recoverySubmissionCount
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

[[nodiscard]] static EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6
EncodePhysicalQueueSubmissionRuntimeStatistics(
    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& statistics
)noexcept{
    return EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6{
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
        .recoverySubmissionCount = statistics.recoverySubmissionCount,
    };
}

template<typename EncodedStatisticsT>
[[nodiscard]] static FrameGraphPhysicalQueueSubmissionRuntimeStatistics
DecodePhysicalQueueSubmissionRuntimeStatistics(
    const EncodedStatisticsT& statistics
)noexcept{
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics decoded{
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
    if constexpr(IsSame_V<EncodedStatisticsT, EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6>)
        decoded.recoverySubmissionCount = statistics.recoverySubmissionCount;
    return decoded;
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 EncodePhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record
)noexcept{
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics = record.statistics;
    EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 encoded;
    encoded.ownerNodeIndex = record.ownerNodeIndex;
    encoded.queue = EncodeQueue(statistics.queue);
    encoded.queueClass = statistics.queueClass;
    encoded.compile = EncodePhysicalQueueCompileRuntimeStatistics(statistics.compile);
    encoded.recording = EncodePhysicalQueueRecordingRuntimeStatistics(statistics.recording);
    encoded.submission = EncodePhysicalQueueSubmissionRuntimeStatistics(statistics.submission);
    return encoded;
}

template<typename EncodedStatisticsT>
[[nodiscard]] static bool DecodePhysicalQueueRuntimeStatistics(
    const EncodedStatisticsT& encoded,
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
            .queue = DecodeQueue(encoded.queue),
            .deviceGeneration = ownerStatistics.deviceGeneration,
            .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
            .compile = DecodePhysicalQueueCompileRuntimeStatistics(encoded.compile),
            .recording = DecodePhysicalQueueRecordingRuntimeStatistics(encoded.recording),
            .submission = DecodePhysicalQueueSubmissionRuntimeStatistics(encoded.submission),
        },
    };
    return IsValidFrameGraphPhysicalQueueRuntimeStatistics(outRecord.statistics);
}

[[nodiscard]] static bool PacketSubmissionStatisticsRecordLess(
    const FrameGraphPacketSubmissionStatisticsRecord& lhs,
    const FrameGraphPacketSubmissionStatisticsRecord& rhs
)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    return lhs.packetIndex < rhs.packetIndex;
}

[[nodiscard]] static EncodedFrameGraphPacketSubmissionStatistics EncodePacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics
)noexcept{
    return EncodedFrameGraphPacketSubmissionStatistics{
        .ownerNodeIndex = statistics.ownerNodeIndex,
        .packetIndex = statistics.packetIndex,
        .packetGeneration = statistics.packetGeneration,
        .queue = EncodeQueue(statistics.queue),
        .queueClass = statistics.queueClass,
        .joinsAcceptedQueueFrontier = static_cast<u8>(statistics.joinsAcceptedQueueFrontier ? 1u : 0u),
        .recoverySubmission = static_cast<u8>(statistics.recoverySubmission ? 1u : 0u),
        .taskCount = statistics.taskCount,
        .commandListCount = statistics.commandListCount,
        .plannedWaitTokenCount = statistics.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount,
        .timelineWaitCount = statistics.timelineWaitCount,
        .mergedTimelineWaitCount = statistics.mergedTimelineWaitCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static bool DecodePacketSubmissionStatistics(
    const EncodedFrameGraphPacketSubmissionStatistics& encoded,
    FrameGraphPacketSubmissionStatisticsRecord& outStatistics
)noexcept{
    if(
        encoded.joinsAcceptedQueueFrontier > 1u
        || encoded.recoverySubmission > 1u
        || encoded.reserved != 0u
    )
        return false;

    outStatistics = {
        .packetGeneration = encoded.packetGeneration,
        .taskCount = encoded.taskCount,
        .commandListCount = encoded.commandListCount,
        .ownerNodeIndex = encoded.ownerNodeIndex,
        .packetIndex = encoded.packetIndex,
        .queue = DecodeQueue(encoded.queue),
        .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
        .plannedWaitTokenCount = encoded.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = encoded.sameQueueWaitElisionCount,
        .timelineWaitCount = encoded.timelineWaitCount,
        .mergedTimelineWaitCount = encoded.mergedTimelineWaitCount,
        .submissionSeconds = encoded.submissionSeconds,
        .joinsAcceptedQueueFrontier = encoded.joinsAcceptedQueueFrontier != 0u,
        .recoverySubmission = encoded.recoverySubmission != 0u,
    };
    return IsValidFrameGraphPacketSubmissionStatistics(outStatistics);
}

struct FrameGraphPacketSubmissionStatisticsAccumulator{
    u64 nativeSubmissionCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

[[nodiscard]] static bool ValidatePacketSubmissionStatisticsOwner(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics
)noexcept{
    return IsValidFrameGraphRuntimeStatistics(ownerStatistics)
        && statistics.packetGeneration == ownerStatistics.planGeneration
        && static_cast<u64>(statistics.packetIndex) < ownerStatistics.compile.packetCount
        && statistics.queue.deviceGeneration == ownerStatistics.deviceGeneration
        && statistics.taskCount <= ownerStatistics.compile.taskCount
        && statistics.commandListCount <= ownerStatistics.recording.commandListCount
    ;
}

[[nodiscard]] static bool AccumulatePacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics,
    const FrameGraphSubmissionRuntimeStatistics& ownerStatistics,
    FrameGraphPacketSubmissionStatisticsAccumulator& total
)noexcept{
    if(!(
        AccumulateBoundedCount(1u, ownerStatistics.nativeSubmissionCount, total.nativeSubmissionCount)
        && AccumulateBoundedCount(statistics.taskCount, ownerStatistics.acceptedTaskCount, total.taskCount)
        && AccumulateBoundedCount(
            statistics.commandListCount,
            ownerStatistics.nativeCommandListCount,
            total.commandListCount
        )
        && AccumulateBoundedCount(
            statistics.plannedWaitTokenCount,
            ownerStatistics.plannedWaitTokenCount,
            total.plannedWaitTokenCount
        )
        && AccumulateBoundedCount(
            statistics.sameQueueWaitElisionCount,
            ownerStatistics.sameQueueWaitElisionCount,
            total.sameQueueWaitElisionCount
        )
        && AccumulateBoundedCount(
            statistics.timelineWaitCount,
            ownerStatistics.timelineWaitCount,
            total.timelineWaitCount
        )
        && AccumulateBoundedCount(
            statistics.mergedTimelineWaitCount,
            ownerStatistics.mergedTimelineWaitCount,
            total.mergedTimelineWaitCount
        )
        && AccumulateBoundedCount(
            statistics.joinsAcceptedQueueFrontier ? 1u : 0u,
            ownerStatistics.acceptedFrontierSubmissionCount,
            total.acceptedFrontierSubmissionCount
        )
        && AccumulateBoundedCount(
            statistics.recoverySubmission ? 1u : 0u,
            ownerStatistics.recoverySubmissionCount,
            total.recoverySubmissionCount
        )
    ))
        return false;

    total.submissionSeconds += statistics.submissionSeconds;
    return IsFinite(total.submissionSeconds);
}

[[nodiscard]] static bool PacketSubmissionDurationSumsMatch(
    const f64 lhs,
    const f64 rhs,
    const u64 submissionCount
)noexcept{
    if(lhs == rhs)
        return true;
    if(submissionCount == 0u)
        return false;

    const f64 roundingFactor = static_cast<f64>(submissionCount) * s_DoublePrecisionEpsilon;
    if(roundingFactor >= 0.5)
        return false;
    const f64 magnitude = lhs > rhs ? lhs : rhs;
    const f64 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference / magnitude <= (2.0 * roundingFactor) / (1.0 - roundingFactor);
}

template<typename SubmissionStatistics>
[[nodiscard]] static bool PacketSubmissionStatisticsAccumulatorMatches(
    const FrameGraphPacketSubmissionStatisticsAccumulator& total,
    const SubmissionStatistics& statistics
)noexcept{
    if(
        total.nativeSubmissionCount != statistics.nativeSubmissionCount
        || total.commandListCount != statistics.nativeCommandListCount
        || total.plannedWaitTokenCount != statistics.plannedWaitTokenCount
        || total.sameQueueWaitElisionCount != statistics.sameQueueWaitElisionCount
        || total.timelineWaitCount != statistics.timelineWaitCount
        || total.mergedTimelineWaitCount != statistics.mergedTimelineWaitCount
        || total.acceptedFrontierSubmissionCount != statistics.acceptedFrontierSubmissionCount
        || total.recoverySubmissionCount != statistics.recoverySubmissionCount
        || total.taskCount > statistics.acceptedTaskCount
        || !PacketSubmissionDurationSumsMatch(
            total.submissionSeconds,
            statistics.submissionSeconds,
            total.nativeSubmissionCount
        )
    )
        return false;

    const u64 manualAcceptedPacketCount = statistics.acceptedPacketCount - statistics.nativeSubmissionCount;
    return manualAcceptedPacketCount <= statistics.acceptedTaskCount - total.taskCount;
}

template<typename NodeContainer>
[[nodiscard]] static bool ValidatePacketSubmissionStatisticsTable(
    Alloc::ScratchArena& scratchArena,
    const NodeContainer& nodes,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics
){
    for(usize statisticsIndex = 0u; statisticsIndex < packetSubmissionStatistics.size(); ++statisticsIndex){
        const FrameGraphPacketSubmissionStatisticsRecord& statistics = packetSubmissionStatistics[statisticsIndex];
        if(
            statistics.ownerNodeIndex >= nodes.size()
            || nodes[statistics.ownerNodeIndex].kind != FrameGraphNodeKind::Pass
            || !ValidatePacketSubmissionStatisticsOwner(
                statistics,
                nodes[statistics.ownerNodeIndex].runtimeStatistics
            )
            || (
                statisticsIndex != 0u
                && !PacketSubmissionStatisticsRecordLess(
                    packetSubmissionStatistics[statisticsIndex - 1u],
                    statistics
                )
            )
        )
            return false;
    }

    Vector<FrameGraphPacketSubmissionStatisticsAccumulator, Alloc::ScratchArena> queueTotals(scratchArena);
    queueTotals.resize(physicalQueueRuntimeStatistics.size());
    usize statisticsIndex = 0u;
    usize queueBegin = 0u;
    for(usize nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex){
        usize queueEnd = queueBegin;
        while(
            queueEnd < physicalQueueRuntimeStatistics.size()
            && physicalQueueRuntimeStatistics[queueEnd].ownerNodeIndex == nodeIndex
        )
            ++queueEnd;

        FrameGraphPacketSubmissionStatisticsAccumulator total;
        while(
            statisticsIndex < packetSubmissionStatistics.size()
            && packetSubmissionStatistics[statisticsIndex].ownerNodeIndex == nodeIndex
        ){
            const auto& statistics = packetSubmissionStatistics[statisticsIndex];
            const auto& ownerSubmission = nodes[nodeIndex].runtimeStatistics.submission;
            if(!AccumulatePacketSubmissionStatistics(statistics, ownerSubmission, total))
                return false;

            usize first = queueBegin;
            usize last = queueEnd;
            while(first < last){
                const usize middle = first + (last - first) / 2u;
                const auto queue = physicalQueueRuntimeStatistics[middle].statistics.queue;
                if(
                    queue.index < statistics.queue.index
                    || (queue.index == statistics.queue.index && queue.deviceGeneration < statistics.queue.deviceGeneration)
                )
                    first = middle + 1u;
                else
                    last = middle;
            }
            if(first < queueEnd && physicalQueueRuntimeStatistics[first].statistics.queue == statistics.queue){
                if(
                    physicalQueueRuntimeStatistics[first].statistics.queueClass != statistics.queueClass
                    || !AccumulatePacketSubmissionStatistics(statistics, ownerSubmission, queueTotals[first])
                )
                    return false;
            }
            ++statisticsIndex;
        }
        if(
            nodes[nodeIndex].runtimeStatistics.present
            && !PacketSubmissionStatisticsAccumulatorMatches(
                total,
                nodes[nodeIndex].runtimeStatistics.submission
            )
        )
            return false;
        queueBegin = queueEnd;
    }
    if(statisticsIndex != packetSubmissionStatistics.size())
        return false;

    for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){
        if(!PacketSubmissionStatisticsAccumulatorMatches(
            queueTotals[queueIndex],
            physicalQueueRuntimeStatistics[queueIndex].statistics.submission
        ))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(
        arena,
        frameIndex,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        nullptr,
        outPayload
    );
}

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(
        arena,
        frameIndex,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        &packetSubmissionStatistics,
        outPayload
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildFrameGraphPayloadImpl(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords* const packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(
        !FitsU32(nodes.size())
        || !FitsU32(edges.size())
        || !FitsU32(physicalQueueRuntimeStatistics.size())
        || (packetSubmissionStatistics && !FitsU32(packetSubmissionStatistics->size()))
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

    FrameGraphPacketSubmissionStatisticsRecords orderedPacketSubmissionStatistics(arena);
    if(packetSubmissionStatistics){
        orderedPacketSubmissionStatistics.reserve(packetSubmissionStatistics->size());
        for(const FrameGraphPacketSubmissionStatisticsRecord& record : *packetSubmissionStatistics)
            orderedPacketSubmissionStatistics.push_back(record);
        Sort(
            orderedPacketSubmissionStatistics.begin(),
            orderedPacketSubmissionStatistics.end(),
            __hidden_telemetry_frame_graph::PacketSubmissionStatisticsRecordLess
        );
    }

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

    const bool hasPacketSubmissionStatistics = packetSubmissionStatistics != nullptr;
    if(hasPacketSubmissionStatistics){
        Alloc::ScratchArena scratchArena(Name("Telemetry/PacketStatisticsValidation"));
        if(!__hidden_telemetry_frame_graph::ValidatePacketSubmissionStatisticsTable(
            scratchArena,
            nodes,
            orderedPhysicalQueueRuntimeStatistics,
            orderedPacketSubmissionStatistics
        ))
            return false;
    }

    const bool hasQueueAssignments = queueAssignmentCount != 0u;
    const bool hasCompiledTasks = compiledTaskCount != 0u;
    const bool hasRuntimeStatistics = runtimeStatisticsCount != 0u;
    const bool hasCurrentStatisticsPayload = hasRuntimeStatistics || hasPacketSubmissionStatistics;
    usize payloadBytes = hasCurrentStatisticsPayload
        ? sizeof(EncodedFrameGraphPayloadHeaderV8)
        : (
            hasCompiledTasks
            ? sizeof(EncodedFrameGraphPayloadHeaderV3)
            : (
                hasQueueAssignments
                ? sizeof(EncodedFrameGraphPayloadHeaderV2)
                : sizeof(EncodedFrameGraphPayloadHeader)
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
            sizeof(EncodedFrameGraphRuntimeStatisticsV8)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            orderedPhysicalQueueRuntimeStatistics.size(),
            sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6)
        )
        || !AddBinaryRepeatedReserveBytes(
            payloadBytes,
            orderedPacketSubmissionStatistics.size(),
            sizeof(EncodedFrameGraphPacketSubmissionStatistics)
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
    if(hasCurrentStatisticsPayload){
        EncodedFrameGraphPayloadHeaderV8 header;
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
        header.packetSubmissionStatisticsCount = static_cast<u32>(
            orderedPacketSubmissionStatistics.size()
        );
        header.packetSubmissionStatisticsPresent = hasPacketSubmissionStatistics ? 1u : 0u;
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
    for(const FrameGraphPacketSubmissionStatisticsRecord& statistics : orderedPacketSubmissionStatistics)
        AppendPOD(outPayload, __hidden_telemetry_frame_graph::EncodePacketSubmissionStatistics(statistics));
    if(!stringTable.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, stringTable.data(), stringTable.size());

    return outPayload.size() == payloadBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    u32 packetSubmissionStatisticsCount = 0u;
    bool packetSubmissionStatisticsPresent = false;
    usize runtimeStatisticsRecordBytes = sizeof(EncodedFrameGraphRuntimeStatistics);
    usize physicalQueueRuntimeStatisticsRecordBytes = sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics);
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
    case s_FrameGraphRecoverySubmissionCountPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV6 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV6);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        runtimeStatisticsCount = header.runtimeStatisticsCount;
        physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
        runtimeStatisticsRecordBytes = sizeof(EncodedFrameGraphRuntimeStatisticsV6);
        physicalQueueRuntimeStatisticsRecordBytes = sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6);
        break;
    }
    case s_FrameGraphPacketSubmissionStatisticsPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV7 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved))
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV7);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        runtimeStatisticsCount = header.runtimeStatisticsCount;
        physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
        packetSubmissionStatisticsCount = header.packetSubmissionStatisticsCount;
        runtimeStatisticsRecordBytes = sizeof(EncodedFrameGraphRuntimeStatisticsV6);
        physicalQueueRuntimeStatisticsRecordBytes = sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6);
        packetSubmissionStatisticsPresent = true;
        break;
    }
    case s_FrameGraphResourceVersionStatisticsPayloadVersion: {
        cursor = 0u;
        EncodedFrameGraphPayloadHeaderV8 header;
        if(!ReadPOD(encoded, cursor, header))
            return false;
        if(
            !__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved)
            || header.packetSubmissionStatisticsPresent > 1u
            || header.reservedTail[0u] != 0u
            || header.reservedTail[1u] != 0u
            || header.reservedTail[2u] != 0u
            || (header.packetSubmissionStatisticsPresent == 0u && header.packetSubmissionStatisticsCount != 0u)
        )
            return false;
        legacyHeader.frameIndex = header.frameIndex;
        legacyHeader.nodeCount = header.nodeCount;
        legacyHeader.edgeCount = header.edgeCount;
        legacyHeader.stringTableBytes = header.stringTableBytes;
        headerBytes = sizeof(EncodedFrameGraphPayloadHeaderV8);
        queueAssignmentCount = header.queueAssignmentCount;
        compiledTaskCount = header.compiledTaskCount;
        runtimeStatisticsCount = header.runtimeStatisticsCount;
        physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
        packetSubmissionStatisticsCount = header.packetSubmissionStatisticsCount;
        runtimeStatisticsRecordBytes = sizeof(EncodedFrameGraphRuntimeStatisticsV8);
        physicalQueueRuntimeStatisticsRecordBytes = sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6);
        packetSubmissionStatisticsPresent = header.packetSubmissionStatisticsPresent != 0u;
        break;
    }
    default:
        return false;
    }
    outPayload.wireVersion = legacyHeader.version;
    outPayload.physicalQueueRuntimeStatisticsPresent = legacyHeader.version
        == s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion
        || (
            legacyHeader.version == s_FrameGraphRecoverySubmissionCountPayloadVersion
            && physicalQueueRuntimeStatisticsCount != 0u
        )
        || (
            legacyHeader.version == s_FrameGraphPacketSubmissionStatisticsPayloadVersion
            && physicalQueueRuntimeStatisticsCount != 0u
        )
        || (
            legacyHeader.version == s_FrameGraphResourceVersionStatisticsPayloadVersion
            && physicalQueueRuntimeStatisticsCount != 0u
        )
    ;
    outPayload.packetSubmissionStatisticsPresent = packetSubmissionStatisticsPresent;
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
            runtimeStatisticsRecordBytes
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            physicalQueueRuntimeStatisticsCount,
            physicalQueueRuntimeStatisticsRecordBytes
        )
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            packetSubmissionStatisticsCount,
            sizeof(EncodedFrameGraphPacketSubmissionStatistics)
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
        runtimeStatisticsRecordBytes
    ))
        return false;

    usize stringTableOffset = physicalQueueRuntimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        physicalQueueRuntimeStatisticsCount,
        physicalQueueRuntimeStatisticsRecordBytes
    ))
        return false;

    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        packetSubmissionStatisticsCount,
        sizeof(EncodedFrameGraphPacketSubmissionStatistics)
    ))
        return false;

    outPayload.frameIndex = legacyHeader.frameIndex;
    outPayload.nodes.reserve(legacyHeader.nodeCount);
    outPayload.edges.reserve(legacyHeader.edgeCount);
    outPayload.physicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatisticsCount);
    outPayload.packetSubmissionStatistics.reserve(packetSubmissionStatisticsCount);

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
        u32 nodeIndex = Limit<u32>::s_Max;
        FrameGraphRuntimeStatistics statistics;
        if(legacyHeader.version == s_FrameGraphResourceVersionStatisticsPayloadVersion){
            EncodedFrameGraphRuntimeStatisticsV8 encodedStatistics;
            if(!ReadPOD(encoded, cursor, encodedStatistics))
                return false;
            nodeIndex = encodedStatistics.nodeIndex;
            if(!__hidden_telemetry_frame_graph::DecodeRuntimeStatistics(encodedStatistics, statistics))
                return false;
        }
        else if(
            legacyHeader.version == s_FrameGraphRecoverySubmissionCountPayloadVersion
            || legacyHeader.version == s_FrameGraphPacketSubmissionStatisticsPayloadVersion
        ){
            EncodedFrameGraphRuntimeStatisticsV6 encodedStatistics;
            if(!ReadPOD(encoded, cursor, encodedStatistics))
                return false;
            nodeIndex = encodedStatistics.nodeIndex;
            if(!__hidden_telemetry_frame_graph::DecodeRuntimeStatistics(encodedStatistics, statistics))
                return false;
        }
        else{
            EncodedFrameGraphRuntimeStatistics encodedStatistics;
            if(!ReadPOD(encoded, cursor, encodedStatistics))
                return false;
            nodeIndex = encodedStatistics.nodeIndex;
            if(!__hidden_telemetry_frame_graph::DecodeRuntimeStatistics(encodedStatistics, statistics))
                return false;
        }
        if(
            nodeIndex >= legacyHeader.nodeCount
            || (statisticsIndex != 0u && nodeIndex <= previousNodeIndex)
            || outPayload.nodes[nodeIndex].kind != FrameGraphNodeKind::Pass
        )
            return false;
        outPayload.nodes[nodeIndex].runtimeStatistics = statistics;
        previousNodeIndex = nodeIndex;
    }

    FrameGraphPhysicalQueueRuntimeStatisticsRecord previousPhysicalQueueStatistics;
    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    __hidden_telemetry_frame_graph::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(u32 statisticsIndex = 0u; statisticsIndex < physicalQueueRuntimeStatisticsCount; ++statisticsIndex){
        FrameGraphPhysicalQueueRuntimeStatisticsRecord statistics;
        if(
            legacyHeader.version == s_FrameGraphRecoverySubmissionCountPayloadVersion
            || legacyHeader.version == s_FrameGraphPacketSubmissionStatisticsPayloadVersion
            || legacyHeader.version == s_FrameGraphResourceVersionStatisticsPayloadVersion
        ){
            EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 encodedStatistics;
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
        }
        else{
            EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
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
        }
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

    for(u32 statisticsIndex = 0u; statisticsIndex < packetSubmissionStatisticsCount; ++statisticsIndex){
        EncodedFrameGraphPacketSubmissionStatistics encodedStatistics;
        if(!ReadPOD(encoded, cursor, encodedStatistics))
            return false;

        FrameGraphPacketSubmissionStatisticsRecord statistics;
        if(!__hidden_telemetry_frame_graph::DecodePacketSubmissionStatistics(encodedStatistics, statistics))
            return false;
        outPayload.packetSubmissionStatistics.push_back(statistics);
    }
    if(outPayload.packetSubmissionStatisticsPresent){
        Alloc::ScratchArena scratchArena(Name("Telemetry/PacketStatisticsValidation"));
        if(!__hidden_telemetry_frame_graph::ValidatePacketSubmissionStatisticsTable(
            scratchArena,
            outPayload.nodes,
            outPayload.physicalQueueRuntimeStatistics,
            outPayload.packetSubmissionStatistics
        ))
            return false;
    }

    return cursor == stringTableOffset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

