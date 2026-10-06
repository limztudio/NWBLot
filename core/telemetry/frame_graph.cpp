// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include "frame_graph_queue_codec_internal.h"

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
static constexpr f64 s_SubmissionRoundingLimit = 0.5;
static constexpr f64 s_SubmissionRoundingScale = 2.0;
inline constexpr Name s_PacketStatisticsValidationScratch("Telemetry/PacketStatisticsValidation");


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

[[nodiscard]] static EncodedFrameGraphCompiledTask EncodeCompiledTask(
    const u32 nodeIndex,
    const FrameGraphCompiledTask& compiledTask)noexcept{
    EncodedFrameGraphCompiledTask encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.packetIndex = compiledTask.packetIndex;
    encoded.planGeneration = compiledTask.planGeneration;
    encoded.packetizationDecision = compiledTask.packetizationDecision;
    return encoded;
}

[[nodiscard]] static bool DecodeCompiledTask(
    const EncodedFrameGraphCompiledTask& encoded,
    FrameGraphCompiledTask& outCompiledTask)noexcept{
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

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertCompileRuntimeStatistics(const InputT& statistics)noexcept{
    OutputT converted;
    converted.taskCount = statistics.taskCount;
    converted.resourceCount = statistics.resourceCount;
    converted.resourceUseCount = statistics.resourceUseCount;
    converted.explicitDependencyCount = statistics.explicitDependencyCount;
    converted.inferredDependencyCount = statistics.inferredDependencyCount;
    converted.packetCount = statistics.packetCount;
    converted.packetDependencyCount = statistics.packetDependencyCount;
    converted.mergedTaskCount = statistics.mergedTaskCount;
    converted.transitionBarrierCount = statistics.transitionBarrierCount;
    converted.uavBarrierCount = statistics.uavBarrierCount;
    converted.ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount;
    converted.ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount;
    converted.stateExportBarrierCount = statistics.stateExportBarrierCount;
    converted.logicalOwnershipTransferCount = statistics.logicalOwnershipTransferCount;
    converted.logicalOwnershipTransferSignatureCount = statistics.logicalOwnershipTransferSignatureCount;
    converted.repeatedOwnershipTransferSignatureCount = statistics.repeatedOwnershipTransferSignatureCount;
    converted.concurrentSharingCouldAvoidTransferCount = statistics.concurrentSharingCouldAvoidTransferCount;
    converted.concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount;
    converted.logicalOwnershipTransferInternalCount = statistics.logicalOwnershipTransferInternalCount;
    converted.logicalOwnershipTransferExternalImportCount = statistics.logicalOwnershipTransferExternalImportCount;
    converted.logicalOwnershipTransferExternalExportCount = statistics.logicalOwnershipTransferExternalExportCount;
    converted.resourceSetCount = statistics.resourceSetCount;
    converted.resourceSetMemberCount = statistics.resourceSetMemberCount;
    converted.directResourceUseCount = statistics.directResourceUseCount;
    converted.declaredResourceSetUseCount = statistics.declaredResourceSetUseCount;
    converted.expandedResourceSetMemberUseCount = statistics.expandedResourceSetMemberUseCount;
    converted.payloadObjectCount = statistics.payloadObjectCount;
    converted.payloadObjectBytes = statistics.payloadObjectBytes;
    converted.uploadBlobCount = statistics.uploadBlobCount;
    converted.uploadBlobBytes = statistics.uploadBlobBytes;
    converted.declarationSeconds = statistics.declarationSeconds;
    converted.analysisSeconds = statistics.analysisSeconds;
    converted.validationSeconds = statistics.validationSeconds;
    converted.dependencyAnalysisSeconds = statistics.dependencyAnalysisSeconds;
    converted.hazardAnalysisSeconds = statistics.hazardAnalysisSeconds;
    converted.topologicalOrderSeconds = statistics.topologicalOrderSeconds;
    converted.queueAssignmentSeconds = statistics.queueAssignmentSeconds;
    converted.planningSeconds = statistics.planningSeconds;
    converted.packetizationSeconds = statistics.packetizationSeconds;
    converted.resourceStatePlanningSeconds = statistics.resourceStatePlanningSeconds;
    converted.packetDependencyPlanningSeconds = statistics.packetDependencyPlanningSeconds;
    converted.totalSeconds = statistics.totalSeconds;
    converted.resourceVersionCount = statistics.resourceVersionCount;
    converted.resourceVersionEdgeCount = statistics.resourceVersionEdgeCount;
    return converted;
}

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertRecordingRuntimeStatistics(const InputT& statistics)noexcept{
    OutputT converted;
    converted.packetCount = statistics.packetCount;
    converted.taskCount = statistics.taskCount;
    converted.commandListCount = statistics.commandListCount;
    converted.barrierCount = statistics.barrierCount;
    converted.workerRoutedPacketCount = statistics.workerRoutedPacketCount;
    converted.parallelPacketCount = statistics.parallelPacketCount;
    converted.commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds;
    converted.graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds;
    converted.taskRecordSeconds = statistics.taskRecordSeconds;
    converted.recordingSeconds = statistics.recordingSeconds;
    converted.recordingElapsedSeconds = statistics.recordingElapsedSeconds;
    converted.readyFrontierElapsedSeconds = statistics.readyFrontierElapsedSeconds;
    converted.readyFrontierWorkerBusySeconds = statistics.readyFrontierWorkerBusySeconds;
    converted.readyFrontierWorkerCapacitySeconds = statistics.readyFrontierWorkerCapacitySeconds;
    return converted;
}

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertSubmissionRuntimeStatistics(const InputT& statistics)noexcept{
    OutputT converted;
    converted.acceptedPacketCount = statistics.acceptedPacketCount;
    converted.acceptedTaskCount = statistics.acceptedTaskCount;
    converted.rejectedPacketCount = statistics.rejectedPacketCount;
    converted.rejectedTaskCount = statistics.rejectedTaskCount;
    converted.nativeSubmissionCount = statistics.nativeSubmissionCount;
    converted.rejectedSubmissionCount = statistics.rejectedSubmissionCount;
    converted.nativeCommandListCount = statistics.nativeCommandListCount;
    converted.plannedWaitTokenCount = statistics.plannedWaitTokenCount;
    converted.sameQueueWaitElisionCount = statistics.sameQueueWaitElisionCount;
    converted.timelineWaitCount = statistics.timelineWaitCount;
    converted.mergedTimelineWaitCount = statistics.mergedTimelineWaitCount;
    converted.inheritedTimelineWaitElisionCount = statistics.inheritedTimelineWaitElisionCount;
    converted.acceptedFrontierSubmissionCount = statistics.acceptedFrontierSubmissionCount;
    converted.submissionSeconds = statistics.submissionSeconds;
    converted.recoverySubmissionCount = statistics.recoverySubmissionCount;
    return converted;
}

[[nodiscard]] static EncodedFrameGraphRuntimeStatistics EncodeRuntimeStatistics(
    const u32 nodeIndex,
    const FrameGraphRuntimeStatistics& statistics)noexcept{
    EncodedFrameGraphRuntimeStatistics encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.deviceGeneration = statistics.deviceGeneration;
    encoded.graphGeneration = statistics.graphGeneration;
    encoded.planGeneration = statistics.planGeneration;
    encoded.recordingAttemptGeneration = statistics.recordingAttemptGeneration;
    encoded.compile = ConvertCompileRuntimeStatistics<EncodedFrameGraphCompileRuntimeStatistics>(statistics.compile);
    encoded.recording = ConvertRecordingRuntimeStatistics<EncodedFrameGraphRecordingRuntimeStatistics>(statistics.recording);
    encoded.submission = ConvertSubmissionRuntimeStatistics<EncodedFrameGraphSubmissionRuntimeStatistics>(
        statistics.submission
    );
    return encoded;
}

[[nodiscard]] static bool DecodeRuntimeStatistics(
    const EncodedFrameGraphRuntimeStatistics& encoded,
    FrameGraphRuntimeStatistics& outStatistics)noexcept{
    if(encoded.reserved != 0u)
        return false;

    outStatistics = {
        .graphGeneration = encoded.graphGeneration,
        .planGeneration = encoded.planGeneration,
        .recordingAttemptGeneration = encoded.recordingAttemptGeneration,
        .compile = ConvertCompileRuntimeStatistics<FrameGraphCompileRuntimeStatistics>(encoded.compile),
        .recording = ConvertRecordingRuntimeStatistics<FrameGraphRecordingRuntimeStatistics>(encoded.recording),
        .submission = ConvertSubmissionRuntimeStatistics<FrameGraphSubmissionRuntimeStatistics>(encoded.submission),
        .deviceGeneration = encoded.deviceGeneration,
        .present = true,
    };
    return IsValidFrameGraphRuntimeStatistics(outStatistics);
}

[[nodiscard]] static bool PhysicalQueueRuntimeStatisticsRecordLess(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs)noexcept{
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

[[nodiscard]] static bool AccumulatePhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics,
    FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total)noexcept{
    u64 ownerBarrierCount = 0u;
    if(!FrameGraphStatisticsDetail::FrameGraphCompileBarrierCount(ownerStatistics.compile, ownerBarrierCount))
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
            submission.inheritedTimelineWaitElisionCount,
            ownerSubmission.inheritedTimelineWaitElisionCount,
            total.submission.inheritedTimelineWaitElisionCount
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
    const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    return static_cast<usize>(record.ownerNodeIndex) < nodeCount
        && ownerKind == FrameGraphNodeKind::Pass
        && IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(record.statistics, ownerStatistics)
    ;
}

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertPhysicalQueueCompileRuntimeStatistics(const InputT& statistics)noexcept{
    OutputT converted;
    converted.taskCount = statistics.taskCount;
    converted.packetCount = statistics.packetCount;
    converted.mergedTaskCount = statistics.mergedTaskCount;
    converted.prologueBarrierCount = statistics.prologueBarrierCount;
    converted.epilogueBarrierCount = statistics.epilogueBarrierCount;
    converted.ownershipReleaseBarrierCount = statistics.ownershipReleaseBarrierCount;
    converted.ownershipAcquireBarrierCount = statistics.ownershipAcquireBarrierCount;
    converted.incomingLogicalOwnershipTransferCount = statistics.incomingLogicalOwnershipTransferCount;
    converted.outgoingLogicalOwnershipTransferCount = statistics.outgoingLogicalOwnershipTransferCount;
    converted.incomingLogicalOwnershipTransferSignatureCount = statistics.incomingLogicalOwnershipTransferSignatureCount;
    converted.outgoingLogicalOwnershipTransferSignatureCount = statistics.outgoingLogicalOwnershipTransferSignatureCount;
    converted.incomingRepeatedOwnershipTransferSignatureCount = statistics.incomingRepeatedOwnershipTransferSignatureCount;
    converted.outgoingRepeatedOwnershipTransferSignatureCount = statistics.outgoingRepeatedOwnershipTransferSignatureCount;
    converted.concurrentSharingAdviceResourceCount = statistics.concurrentSharingAdviceResourceCount;
    return converted;
}

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertPhysicalQueueRecordingRuntimeStatistics(const InputT& statistics)noexcept{
    OutputT converted;
    converted.packetCount = statistics.packetCount;
    converted.taskCount = statistics.taskCount;
    converted.commandListCount = statistics.commandListCount;
    converted.barrierCount = statistics.barrierCount;
    converted.workerRoutedPacketCount = statistics.workerRoutedPacketCount;
    converted.parallelPacketCount = statistics.parallelPacketCount;
    converted.commandListAcquisitionSeconds = statistics.commandListAcquisitionSeconds;
    converted.graphBarrierRecordingSeconds = statistics.graphBarrierRecordingSeconds;
    converted.taskRecordSeconds = statistics.taskRecordSeconds;
    converted.recordingSeconds = statistics.recordingSeconds;
    return converted;
}

[[nodiscard]] static EncodedFrameGraphPhysicalQueueRuntimeStatistics EncodePhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record)noexcept{
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics = record.statistics;
    EncodedFrameGraphPhysicalQueueRuntimeStatistics encoded;
    encoded.ownerNodeIndex = record.ownerNodeIndex;
    encoded.queue = EncodeQueue(statistics.queue);
    encoded.queueClass = statistics.queueClass;
    encoded.compile = ConvertPhysicalQueueCompileRuntimeStatistics<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>(
        statistics.compile
    );
    encoded.recording = ConvertPhysicalQueueRecordingRuntimeStatistics<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>(
        statistics.recording
    );
    encoded.submission = ConvertSubmissionRuntimeStatistics<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>(
        statistics.submission
    );
    return encoded;
}

[[nodiscard]] static bool DecodePhysicalQueueRuntimeStatistics(
    const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encoded,
    const FrameGraphRuntimeStatistics& ownerStatistics,
    FrameGraphPhysicalQueueRuntimeStatisticsRecord& outRecord)noexcept{
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
            .compile = ConvertPhysicalQueueCompileRuntimeStatistics<FrameGraphPhysicalQueueCompileRuntimeStatistics>(encoded.compile),
            .recording = ConvertPhysicalQueueRecordingRuntimeStatistics<FrameGraphPhysicalQueueRecordingRuntimeStatistics>(encoded.recording),
            .submission = ConvertSubmissionRuntimeStatistics<FrameGraphPhysicalQueueSubmissionRuntimeStatistics>(encoded.submission),
        },
    };
    return IsValidFrameGraphPhysicalQueueRuntimeStatistics(outRecord.statistics);
}

[[nodiscard]] static bool PacketSubmissionStatisticsRecordLess(
    const FrameGraphPacketSubmissionStatisticsRecord& lhs,
    const FrameGraphPacketSubmissionStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    return lhs.packetIndex < rhs.packetIndex;
}

[[nodiscard]] static EncodedFrameGraphPacketSubmissionStatistics EncodePacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics)noexcept{
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
        .inheritedTimelineWaitElisionCount = statistics.inheritedTimelineWaitElisionCount,
        .submissionSeconds = statistics.submissionSeconds,
    };
}

[[nodiscard]] static bool DecodePacketSubmissionStatistics(
    const EncodedFrameGraphPacketSubmissionStatistics& encoded,
    FrameGraphPacketSubmissionStatisticsRecord& outStatistics)noexcept{
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
        .joinsAcceptedQueueFrontier = encoded.joinsAcceptedQueueFrontier != 0u,
        .recoverySubmission = encoded.recoverySubmission != 0u,
        .plannedWaitTokenCount = encoded.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = encoded.sameQueueWaitElisionCount,
        .timelineWaitCount = encoded.timelineWaitCount,
        .mergedTimelineWaitCount = encoded.mergedTimelineWaitCount,
        .inheritedTimelineWaitElisionCount = encoded.inheritedTimelineWaitElisionCount,
        .submissionSeconds = encoded.submissionSeconds,
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
    u64 inheritedTimelineWaitElisionCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

[[nodiscard]] static bool ValidatePacketSubmissionStatisticsOwner(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
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
    FrameGraphPacketSubmissionStatisticsAccumulator& total)noexcept{
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
            statistics.inheritedTimelineWaitElisionCount,
            ownerStatistics.inheritedTimelineWaitElisionCount,
            total.inheritedTimelineWaitElisionCount
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
    const u64 submissionCount)noexcept{
    if(lhs == rhs)
        return true;
    if(submissionCount == 0u)
        return false;

    const f64 roundingFactor = static_cast<f64>(submissionCount) * s_DoublePrecisionEpsilon;
    if(roundingFactor >= s_SubmissionRoundingLimit)
        return false;
    const f64 magnitude = lhs > rhs ? lhs : rhs;
    const f64 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference / magnitude <= (s_SubmissionRoundingScale * roundingFactor) / (1.0 - roundingFactor);
}

template<typename SubmissionStatistics>
[[nodiscard]] static bool PacketSubmissionStatisticsAccumulatorMatches(
    const FrameGraphPacketSubmissionStatisticsAccumulator& total,
    const SubmissionStatistics& statistics)noexcept{
    if(
        total.nativeSubmissionCount != statistics.nativeSubmissionCount
        || total.commandListCount != statistics.nativeCommandListCount
        || total.plannedWaitTokenCount != statistics.plannedWaitTokenCount
        || total.sameQueueWaitElisionCount != statistics.sameQueueWaitElisionCount
        || total.timelineWaitCount != statistics.timelineWaitCount
        || total.mergedTimelineWaitCount != statistics.mergedTimelineWaitCount
        || total.inheritedTimelineWaitElisionCount != statistics.inheritedTimelineWaitElisionCount
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
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics){
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
    TelemetryBytes& outPayload){
    FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(arena);
    return BuildFrameGraphPayload(arena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, outPayload);
}

bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload){
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
    TelemetryBytes& outPayload){
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
    TelemetryBytes& outPayload){
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
        Alloc::ScratchArena scratchArena(__hidden_telemetry_frame_graph::s_PacketStatisticsValidationScratch);
        if(!__hidden_telemetry_frame_graph::ValidatePacketSubmissionStatisticsTable(
            scratchArena,
            nodes,
            orderedPhysicalQueueRuntimeStatistics,
            orderedPacketSubmissionStatistics
        ))
            return false;
    }

    usize payloadBytes = sizeof(EncodedFrameGraphPayloadHeader);
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
    EncodedFrameGraphPayloadHeader header;
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
                FrameGraphQueueCodecDetail::EncodeQueueAssignment(nodeIndex, nodes[nodeIndex].queueAssignment)
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
    FrameGraphPayload& outPayload){
    outPayload = FrameGraphPayload(arena);

    if(payloadBytes < sizeof(EncodedFrameGraphPayloadHeader) || !payload)
        return false;

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedFrameGraphPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(
        header.version != s_FrameGraphPayloadVersion
        || !__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved)
        || header.packetSubmissionStatisticsPresent > 1u
        || header.reservedTail[0u] != 0u
        || header.reservedTail[1u] != 0u
        || header.reservedTail[2u] != 0u
        || (header.packetSubmissionStatisticsPresent == 0u && header.packetSubmissionStatisticsCount != 0u)
    )
        return false;

    const usize headerBytes = sizeof(EncodedFrameGraphPayloadHeader);
    const u32 queueAssignmentCount = header.queueAssignmentCount;
    const u32 compiledTaskCount = header.compiledTaskCount;
    const u32 runtimeStatisticsCount = header.runtimeStatisticsCount;
    const u32 physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
    const u32 packetSubmissionStatisticsCount = header.packetSubmissionStatisticsCount;
    const bool packetSubmissionStatisticsPresent = header.packetSubmissionStatisticsPresent != 0u;
    outPayload.packetSubmissionStatisticsPresent = packetSubmissionStatisticsPresent;
    if(
        queueAssignmentCount > header.nodeCount
        || compiledTaskCount > header.nodeCount
        || runtimeStatisticsCount > header.nodeCount
    )
        return false;

    usize expectedBytes = headerBytes;
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, header.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, header.edgeCount, sizeof(EncodedFrameGraphEdge))
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
        || !AddBinaryRepeatedReserveBytes(
            expectedBytes,
            packetSubmissionStatisticsCount,
            sizeof(EncodedFrameGraphPacketSubmissionStatistics)
        )
        || !AddBinaryReserveBytes(expectedBytes, header.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return false;

    usize edgeOffset = headerBytes;
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, header.nodeCount, sizeof(EncodedFrameGraphNode)))
        return false;

    usize queueAssignmentOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(queueAssignmentOffset, header.edgeCount, sizeof(EncodedFrameGraphEdge)))
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

    if(!AddBinaryRepeatedReserveBytes(
        stringTableOffset,
        packetSubmissionStatisticsCount,
        sizeof(EncodedFrameGraphPacketSubmissionStatistics)
    ))
        return false;

    outPayload.frameIndex = header.frameIndex;
    outPayload.nodes.reserve(header.nodeCount);
    outPayload.edges.reserve(header.edgeCount);
    outPayload.physicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatisticsCount);
    outPayload.packetSubmissionStatistics.reserve(packetSubmissionStatisticsCount);

    for(u32 nodeIndex = 0u; nodeIndex < header.nodeCount; ++nodeIndex){
        EncodedFrameGraphNode encodedNode;
        if(!ReadPOD(encoded, cursor, encodedNode))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return false;

        AStringView labelView;
        if(!BinaryDetail::ReadStringTableTextView(
            encoded,
            stringTableOffset,
            header.stringTableBytes,
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

    for(u32 edgeIndex = 0u; edgeIndex < header.edgeCount; ++edgeIndex){
        EncodedFrameGraphEdge encodedEdge;
        if(!ReadPOD(encoded, cursor, encodedEdge))
            return false;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, header.nodeCount))
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
            encodedAssignment.nodeIndex >= header.nodeCount
            || (assignmentIndex != 0u && encodedAssignment.nodeIndex <= previousNodeIndex)
            || outPayload.nodes[encodedAssignment.nodeIndex].kind != FrameGraphNodeKind::Pass
            || !FrameGraphQueueCodecDetail::DecodeQueueAssignment(
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
            encodedCompiledTask.nodeIndex >= header.nodeCount
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
        const u32 nodeIndex = encodedStatistics.nodeIndex;
        FrameGraphRuntimeStatistics statistics;
        if(!__hidden_telemetry_frame_graph::DecodeRuntimeStatistics(encodedStatistics, statistics))
            return false;
        if(
            nodeIndex >= header.nodeCount
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
        EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
        if(!ReadPOD(encoded, cursor, encodedStatistics))
            return false;
        if(encodedStatistics.ownerNodeIndex >= outPayload.nodes.size())
            return false;
        FrameGraphPhysicalQueueRuntimeStatisticsRecord statistics;
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
        Alloc::ScratchArena scratchArena(__hidden_telemetry_frame_graph::s_PacketStatisticsValidationScratch);
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

