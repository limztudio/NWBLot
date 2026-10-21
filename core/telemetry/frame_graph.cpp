// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include "frame_graph_queue_codec_internal.h"
#include "frame_graph_statistics_codec_internal.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool BuildFrameGraphPayloadImpl(
    Alloc::ScratchArena& scratchArena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords* packetSubmissionStatistics,
    TelemetryBytes& outPayload
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsValidStringTableText(const AStringView text)noexcept{
    return !text.empty() && !HasEmbeddedNull(text) && text.size() < static_cast<usize>(Limit<u32>::s_Max);
}


[[nodiscard]] static bool ValidateHeader(const u32 magic, const u16 reserved)noexcept{
    return magic == s_FrameGraphPayloadMagic && reserved == 0u;
}


[[nodiscard]] static bool ValidateNodeInput(const FrameGraphNodeDesc& node)noexcept{
    return
        static_cast<bool>(node.name)
        && IsValidFrameGraphNodeKind(node.kind)
        && IsValidStringTableText(node.label)
        && (!node.queueAssignment.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphQueueAssignment(node.queueAssignment)))
        && (!node.compiledTask.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphCompiledTask(node.compiledTask)))
        && (!node.runtimeStatistics.present || (node.kind == FrameGraphNodeKind::Pass && IsValidFrameGraphRuntimeStatistics(node.runtimeStatistics)))
    ;
}

[[nodiscard]] static bool ValidateEdgeInput(const FrameGraphEdgeDesc& edge, const usize nodeCount)noexcept{
    return IsValidFrameGraphEdgeKind(edge.kind) && static_cast<usize>(edge.fromNodeIndex) < nodeCount && static_cast<usize>(edge.toNodeIndex) < nodeCount;
}

[[nodiscard]] static bool ValidateEncodedNode(const EncodedFrameGraphNode& node)noexcept{
    return node.reserved == 0u && !NameDetail::IsZeroHash(node.nameHash) && IsValidFrameGraphNodeKind(static_cast<FrameGraphNodeKind::Enum>(node.kind));
}

[[nodiscard]] static bool ValidateEncodedEdge(const EncodedFrameGraphEdge& edge, const usize nodeCount)noexcept{
    return edge.reserved == 0u && IsValidFrameGraphEdgeKind(static_cast<FrameGraphEdgeKind::Enum>(edge.kind)) && static_cast<usize>(edge.fromNodeIndex) < nodeCount && static_cast<usize>(edge.toNodeIndex) < nodeCount;
}


[[nodiscard]] static EncodedFrameGraphCompiledTask EncodeCompiledTask(const u32 nodeIndex, const FrameGraphCompiledTask& compiledTask)noexcept{
    EncodedFrameGraphCompiledTask encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.packetIndex = compiledTask.packetIndex;
    encoded.planGeneration = compiledTask.planGeneration;
    encoded.packetizationDecision = compiledTask.packetizationDecision;
    return encoded;
}

[[nodiscard]] static Expected<FrameGraphCompiledTask> DecodeCompiledTask(const EncodedFrameGraphCompiledTask& encoded)noexcept{
    FrameGraphCompiledTask decoded;
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u)
        return MakeUnexpected(Failure{});

    decoded.planGeneration = encoded.planGeneration;
    decoded.packetIndex = encoded.packetIndex;
    decoded.packetizationDecision = static_cast<FrameGraphTaskPacketizationDecision::Enum>(encoded.packetizationDecision);
    decoded.present = true;
    if(!IsValidFrameGraphCompiledTask(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}

template<typename OutputT, typename InputT>
[[nodiscard]] static OutputT ConvertCompileRuntimeStatistics(const InputT& statistics)noexcept(
    noexcept(OutputT{})
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().taskCount = DeclVal<const InputT&>().taskCount)
    && noexcept(DeclVal<OutputT&>().resourceCount = DeclVal<const InputT&>().resourceCount)
    && noexcept(DeclVal<OutputT&>().resourceUseCount = DeclVal<const InputT&>().resourceUseCount)
    && noexcept(DeclVal<OutputT&>().explicitDependencyCount = DeclVal<const InputT&>().explicitDependencyCount)
    && noexcept(DeclVal<OutputT&>().inferredDependencyCount = DeclVal<const InputT&>().inferredDependencyCount)
    && noexcept(DeclVal<OutputT&>().packetCount = DeclVal<const InputT&>().packetCount)
    && noexcept(DeclVal<OutputT&>().packetDependencyCount = DeclVal<const InputT&>().packetDependencyCount)
    && noexcept(DeclVal<OutputT&>().mergedTaskCount = DeclVal<const InputT&>().mergedTaskCount)
    && noexcept(DeclVal<OutputT&>().transitionBarrierCount = DeclVal<const InputT&>().transitionBarrierCount)
    && noexcept(DeclVal<OutputT&>().uavBarrierCount = DeclVal<const InputT&>().uavBarrierCount)
    && noexcept(DeclVal<OutputT&>().ownershipReleaseBarrierCount = DeclVal<const InputT&>().ownershipReleaseBarrierCount)
    && noexcept(DeclVal<OutputT&>().ownershipAcquireBarrierCount = DeclVal<const InputT&>().ownershipAcquireBarrierCount)
    && noexcept(DeclVal<OutputT&>().stateExportBarrierCount = DeclVal<const InputT&>().stateExportBarrierCount)
    && noexcept(DeclVal<OutputT&>().logicalOwnershipTransferCount = DeclVal<const InputT&>().logicalOwnershipTransferCount)
    && noexcept(DeclVal<OutputT&>().logicalOwnershipTransferSignatureCount = DeclVal<const InputT&>().logicalOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().repeatedOwnershipTransferSignatureCount = DeclVal<const InputT&>().repeatedOwnershipTransferSignatureCount)
    && noexcept(DeclVal<OutputT&>().concurrentSharingCouldAvoidTransferCount = DeclVal<const InputT&>().concurrentSharingCouldAvoidTransferCount)
    && noexcept(DeclVal<OutputT&>().concurrentSharingAdviceResourceCount = DeclVal<const InputT&>().concurrentSharingAdviceResourceCount)
    && noexcept(DeclVal<OutputT&>().logicalOwnershipTransferInternalCount = DeclVal<const InputT&>().logicalOwnershipTransferInternalCount)
    && noexcept(DeclVal<OutputT&>().logicalOwnershipTransferExternalImportCount = DeclVal<const InputT&>().logicalOwnershipTransferExternalImportCount)
    && noexcept(DeclVal<OutputT&>().logicalOwnershipTransferExternalExportCount = DeclVal<const InputT&>().logicalOwnershipTransferExternalExportCount)
    && noexcept(DeclVal<OutputT&>().resourceSetCount = DeclVal<const InputT&>().resourceSetCount)
    && noexcept(DeclVal<OutputT&>().resourceSetMemberCount = DeclVal<const InputT&>().resourceSetMemberCount)
    && noexcept(DeclVal<OutputT&>().directResourceUseCount = DeclVal<const InputT&>().directResourceUseCount)
    && noexcept(DeclVal<OutputT&>().declaredResourceSetUseCount = DeclVal<const InputT&>().declaredResourceSetUseCount)
    && noexcept(DeclVal<OutputT&>().expandedResourceSetMemberUseCount = DeclVal<const InputT&>().expandedResourceSetMemberUseCount)
    && noexcept(DeclVal<OutputT&>().payloadObjectCount = DeclVal<const InputT&>().payloadObjectCount)
    && noexcept(DeclVal<OutputT&>().payloadObjectBytes = DeclVal<const InputT&>().payloadObjectBytes)
    && noexcept(DeclVal<OutputT&>().uploadBlobCount = DeclVal<const InputT&>().uploadBlobCount)
    && noexcept(DeclVal<OutputT&>().uploadBlobBytes = DeclVal<const InputT&>().uploadBlobBytes)
    && noexcept(DeclVal<OutputT&>().declarationSeconds = DeclVal<const InputT&>().declarationSeconds)
    && noexcept(DeclVal<OutputT&>().analysisSeconds = DeclVal<const InputT&>().analysisSeconds)
    && noexcept(DeclVal<OutputT&>().validationSeconds = DeclVal<const InputT&>().validationSeconds)
    && noexcept(DeclVal<OutputT&>().dependencyAnalysisSeconds = DeclVal<const InputT&>().dependencyAnalysisSeconds)
    && noexcept(DeclVal<OutputT&>().hazardAnalysisSeconds = DeclVal<const InputT&>().hazardAnalysisSeconds)
    && noexcept(DeclVal<OutputT&>().topologicalOrderSeconds = DeclVal<const InputT&>().topologicalOrderSeconds)
    && noexcept(DeclVal<OutputT&>().queueAssignmentSeconds = DeclVal<const InputT&>().queueAssignmentSeconds)
    && noexcept(DeclVal<OutputT&>().planningSeconds = DeclVal<const InputT&>().planningSeconds)
    && noexcept(DeclVal<OutputT&>().packetizationSeconds = DeclVal<const InputT&>().packetizationSeconds)
    && noexcept(DeclVal<OutputT&>().resourceStatePlanningSeconds = DeclVal<const InputT&>().resourceStatePlanningSeconds)
    && noexcept(DeclVal<OutputT&>().packetDependencyPlanningSeconds = DeclVal<const InputT&>().packetDependencyPlanningSeconds)
    && noexcept(DeclVal<OutputT&>().totalSeconds = DeclVal<const InputT&>().totalSeconds)
    && noexcept(DeclVal<OutputT&>().resourceVersionCount = DeclVal<const InputT&>().resourceVersionCount)
    && noexcept(DeclVal<OutputT&>().resourceVersionEdgeCount = DeclVal<const InputT&>().resourceVersionEdgeCount)
){
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
[[nodiscard]] static OutputT ConvertRecordingRuntimeStatistics(const InputT& statistics)noexcept(
    noexcept(OutputT{})
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().packetCount = DeclVal<const InputT&>().packetCount)
    && noexcept(DeclVal<OutputT&>().taskCount = DeclVal<const InputT&>().taskCount)
    && noexcept(DeclVal<OutputT&>().commandListCount = DeclVal<const InputT&>().commandListCount)
    && noexcept(DeclVal<OutputT&>().barrierCount = DeclVal<const InputT&>().barrierCount)
    && noexcept(DeclVal<OutputT&>().workerRoutedPacketCount = DeclVal<const InputT&>().workerRoutedPacketCount)
    && noexcept(DeclVal<OutputT&>().parallelPacketCount = DeclVal<const InputT&>().parallelPacketCount)
    && noexcept(DeclVal<OutputT&>().commandListAcquisitionSeconds = DeclVal<const InputT&>().commandListAcquisitionSeconds)
    && noexcept(DeclVal<OutputT&>().graphBarrierRecordingSeconds = DeclVal<const InputT&>().graphBarrierRecordingSeconds)
    && noexcept(DeclVal<OutputT&>().taskRecordSeconds = DeclVal<const InputT&>().taskRecordSeconds)
    && noexcept(DeclVal<OutputT&>().recordingSeconds = DeclVal<const InputT&>().recordingSeconds)
    && noexcept(DeclVal<OutputT&>().recordingElapsedSeconds = DeclVal<const InputT&>().recordingElapsedSeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierElapsedSeconds = DeclVal<const InputT&>().readyFrontierElapsedSeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierWorkerBusySeconds = DeclVal<const InputT&>().readyFrontierWorkerBusySeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierWorkerCapacitySeconds = DeclVal<const InputT&>().readyFrontierWorkerCapacitySeconds)
){
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


[[nodiscard]] static EncodedFrameGraphRuntimeStatistics EncodeRuntimeStatistics(const u32 nodeIndex, const FrameGraphRuntimeStatistics& statistics)noexcept{
    EncodedFrameGraphRuntimeStatistics encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.deviceGeneration = statistics.deviceGeneration;
    encoded.graphGeneration = statistics.graphGeneration;
    encoded.planGeneration = statistics.planGeneration;
    encoded.recordingAttemptGeneration = statistics.recordingAttemptGeneration;
    encoded.compile = ConvertCompileRuntimeStatistics<EncodedFrameGraphCompileRuntimeStatistics>(statistics.compile);
    encoded.recording = ConvertRecordingRuntimeStatistics<EncodedFrameGraphRecordingRuntimeStatistics>(statistics.recording);
    encoded.submission = FrameGraphStatisticsCodecDetail::ConvertSubmissionRuntimeStatistics<EncodedFrameGraphSubmissionRuntimeStatistics>(statistics.submission);
    return encoded;
}

[[nodiscard]] static Expected<FrameGraphRuntimeStatistics> DecodeRuntimeStatistics(const EncodedFrameGraphRuntimeStatistics& encoded)noexcept{
    FrameGraphRuntimeStatistics decoded;
    if(encoded.reserved != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
        .graphGeneration = encoded.graphGeneration,
        .planGeneration = encoded.planGeneration,
        .recordingAttemptGeneration = encoded.recordingAttemptGeneration,
        .compile = ConvertCompileRuntimeStatistics<FrameGraphCompileRuntimeStatistics>(encoded.compile),
        .recording = ConvertRecordingRuntimeStatistics<FrameGraphRecordingRuntimeStatistics>(encoded.recording),
        .submission = FrameGraphStatisticsCodecDetail::ConvertSubmissionRuntimeStatistics<FrameGraphSubmissionRuntimeStatistics>(encoded.submission),
        .deviceGeneration = encoded.deviceGeneration,
        .present = true,
    };
    if(!IsValidFrameGraphRuntimeStatistics(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}

[[nodiscard]] static bool PhysicalQueueRuntimeStatisticsRecordLess(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs, const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs)noexcept{
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


[[nodiscard]] static bool AccumulatePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatistics& statistics, const FrameGraphRuntimeStatistics& ownerStatistics, FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total)noexcept{
    const auto barrierCount = FrameGraphStatisticsDetail::FrameGraphCompileBarrierCount(ownerStatistics.compile);
    if(!barrierCount)
        return false;
    const u64 ownerBarrierCount = *barrierCount;

    const FrameGraphPhysicalQueueCompileRuntimeStatistics& compile = statistics.compile;
    const FrameGraphCompileRuntimeStatistics& ownerCompile = ownerStatistics.compile;
    if(
        !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.taskCount, ownerCompile.taskCount, total.compile.taskCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.packetCount, ownerCompile.packetCount, total.compile.packetCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.mergedTaskCount, ownerCompile.mergedTaskCount, total.compile.mergedTaskCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.prologueBarrierCount, ownerBarrierCount, total.compile.prologueBarrierCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.epilogueBarrierCount, ownerBarrierCount, total.compile.prologueBarrierCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.ownershipReleaseBarrierCount, ownerCompile.ownershipReleaseBarrierCount, total.compile.ownershipReleaseBarrierCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.ownershipAcquireBarrierCount, ownerCompile.ownershipAcquireBarrierCount, total.compile.ownershipAcquireBarrierCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.incomingLogicalOwnershipTransferCount, ownerCompile.logicalOwnershipTransferCount, total.compile.incomingLogicalOwnershipTransferCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.outgoingLogicalOwnershipTransferCount, ownerCompile.logicalOwnershipTransferCount, total.compile.outgoingLogicalOwnershipTransferCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.incomingLogicalOwnershipTransferSignatureCount, ownerCompile.logicalOwnershipTransferSignatureCount, total.compile.incomingLogicalOwnershipTransferSignatureCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.outgoingLogicalOwnershipTransferSignatureCount, ownerCompile.logicalOwnershipTransferSignatureCount, total.compile.outgoingLogicalOwnershipTransferSignatureCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.incomingRepeatedOwnershipTransferSignatureCount, ownerCompile.repeatedOwnershipTransferSignatureCount, total.compile.incomingRepeatedOwnershipTransferSignatureCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(compile.outgoingRepeatedOwnershipTransferSignatureCount, ownerCompile.repeatedOwnershipTransferSignatureCount, total.compile.outgoingRepeatedOwnershipTransferSignatureCount)
    )
        return false;

    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& recording = statistics.recording;
    const FrameGraphRecordingRuntimeStatistics& ownerRecording = ownerStatistics.recording;
    if(
        !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.packetCount, ownerRecording.packetCount, total.recording.packetCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.taskCount, ownerRecording.taskCount, total.recording.taskCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.commandListCount, ownerRecording.commandListCount, total.recording.commandListCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.barrierCount, ownerRecording.barrierCount, total.recording.barrierCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.workerRoutedPacketCount, ownerRecording.workerRoutedPacketCount, total.recording.workerRoutedPacketCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(recording.parallelPacketCount, ownerRecording.parallelPacketCount, total.recording.parallelPacketCount)
    )
        return false;
    if(total.recording.taskCount - total.recording.packetCount > ownerRecording.taskCount - ownerRecording.packetCount || total.recording.commandListCount - total.recording.packetCount > ownerRecording.commandListCount - ownerRecording.packetCount)
        return false;

    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& submission = statistics.submission;
    const FrameGraphSubmissionRuntimeStatistics& ownerSubmission = ownerStatistics.submission;
    if(
        !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.acceptedPacketCount, ownerSubmission.acceptedPacketCount, total.submission.acceptedPacketCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.acceptedTaskCount, ownerSubmission.acceptedTaskCount, total.submission.acceptedTaskCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.rejectedPacketCount, ownerSubmission.rejectedPacketCount, total.submission.rejectedPacketCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.rejectedTaskCount, ownerSubmission.rejectedTaskCount, total.submission.rejectedTaskCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.nativeSubmissionCount, ownerSubmission.nativeSubmissionCount, total.submission.nativeSubmissionCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.rejectedSubmissionCount, ownerSubmission.rejectedSubmissionCount, total.submission.rejectedSubmissionCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.nativeCommandListCount, ownerSubmission.nativeCommandListCount, total.submission.nativeCommandListCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.plannedWaitTokenCount, ownerSubmission.plannedWaitTokenCount, total.submission.plannedWaitTokenCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.sameQueueWaitElisionCount, ownerSubmission.sameQueueWaitElisionCount, total.submission.sameQueueWaitElisionCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.timelineWaitCount, ownerSubmission.timelineWaitCount, total.submission.timelineWaitCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.mergedTimelineWaitCount, ownerSubmission.mergedTimelineWaitCount, total.submission.mergedTimelineWaitCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.inheritedTimelineWaitElisionCount, ownerSubmission.inheritedTimelineWaitElisionCount, total.submission.inheritedTimelineWaitElisionCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.acceptedFrontierSubmissionCount, ownerSubmission.acceptedFrontierSubmissionCount, total.submission.acceptedFrontierSubmissionCount)
        || !FrameGraphStatisticsCodecDetail::AccumulateBoundedCount(submission.recoverySubmissionCount, ownerSubmission.recoverySubmissionCount, total.submission.recoverySubmissionCount)
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

[[nodiscard]] static bool ValidatePhysicalQueueRuntimeStatisticsOwner(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record, const usize nodeCount, const FrameGraphNodeKind::Enum ownerKind, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    return static_cast<usize>(record.ownerNodeIndex) < nodeCount && ownerKind == FrameGraphNodeKind::Pass && IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(record.statistics, ownerStatistics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildFrameGraphPayload(Alloc::ScratchArena& scratchArena, const u64 frameIndex, const FrameGraphNodeDescs& nodes, const FrameGraphEdgeDescs& edges, TelemetryBytes& outPayload){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord>{}, nullptr, outPayload);
}

bool BuildFrameGraphPayload(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload
){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, nullptr, outPayload);
}

bool BuildFrameGraphPayload(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    return __hidden_telemetry_frame_graph::BuildFrameGraphPayloadImpl(scratchArena, frameIndex, nodes, edges, physicalQueueRuntimeStatistics, &packetSubmissionStatistics, outPayload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildFrameGraphPayloadImpl(
    Alloc::ScratchArena& scratchArena,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords* const packetSubmissionStatistics,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!FitsU32(nodes.size()) || !FitsU32(edges.size()) || !FitsU32(physicalQueueRuntimeStatistics.size()) || (packetSubmissionStatistics && !FitsU32(packetSubmissionStatistics->size())))
        return false;

    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, Alloc::ScratchArena> orderedPhysicalQueueRuntimeStatistics(scratchArena);
    orderedPhysicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatistics.size());
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : physicalQueueRuntimeStatistics)
        orderedPhysicalQueueRuntimeStatistics.push_back(record);
    Sort(orderedPhysicalQueueRuntimeStatistics.begin(), orderedPhysicalQueueRuntimeStatistics.end(), __hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess);

    Vector<FrameGraphPacketSubmissionStatisticsRecord, Alloc::ScratchArena> orderedPacketSubmissionStatistics(scratchArena);
    if(packetSubmissionStatistics){
        orderedPacketSubmissionStatistics.reserve(packetSubmissionStatistics->size());
        for(const FrameGraphPacketSubmissionStatisticsRecord& record : *packetSubmissionStatistics)
            orderedPacketSubmissionStatistics.push_back(record);
        Sort(orderedPacketSubmissionStatistics.begin(), orderedPacketSubmissionStatistics.end(), FrameGraphStatisticsCodecDetail::PacketSubmissionStatisticsRecordLess);
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
        const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record = orderedPhysicalQueueRuntimeStatistics[statisticsIndex];
        if(record.ownerNodeIndex >= nodes.size())
            return false;
        if(!__hidden_telemetry_frame_graph::ValidatePhysicalQueueRuntimeStatisticsOwner(record, nodes.size(), nodes[record.ownerNodeIndex].kind, nodes[record.ownerNodeIndex].runtimeStatistics))
            return false;
        if(record.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = record.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!__hidden_telemetry_frame_graph::AccumulatePhysicalQueueRuntimeStatistics(record.statistics, nodes[record.ownerNodeIndex].runtimeStatistics, accumulatedStatistics))
            return false;
        if(statisticsIndex != 0u && !__hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess(orderedPhysicalQueueRuntimeStatistics[statisticsIndex - 1u], record))
            return false;
    }

    const bool hasPacketSubmissionStatistics = packetSubmissionStatistics != nullptr;
    if(hasPacketSubmissionStatistics){
        if(!FrameGraphStatisticsCodecDetail::ValidatePacketSubmissionStatisticsTable(scratchArena, nodes, orderedPhysicalQueueRuntimeStatistics, orderedPacketSubmissionStatistics))
            return false;
    }

    usize payloadBytes = sizeof(EncodedFrameGraphPayloadHeader);
    if(
        !AddBinaryRepeatedReserveBytes(payloadBytes, nodes.size(), sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, edges.size(), sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, orderedPhysicalQueueRuntimeStatistics.size(), sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, orderedPacketSubmissionStatistics.size(), sizeof(EncodedFrameGraphPacketSubmissionStatistics))
        || !AddBinaryReserveBytes(payloadBytes, stringTableBytes)
    )
        return false;

    Vector<u8, Alloc::ScratchArena> stringTable(scratchArena);
    stringTable.reserve(stringTableBytes);
    Vector<EncodedFrameGraphNode, Alloc::ScratchArena> encodedNodes(scratchArena);
    encodedNodes.reserve(nodes.size());

    for(const FrameGraphNodeDesc& node : nodes){
        const auto labelOffset = AppendStringTableText(stringTable, node.label);
        if(!labelOffset)
            return false;

        EncodedFrameGraphNode encodedNode;
        encodedNode.nameHash = node.name.hash();
        encodedNode.labelOffset = *labelOffset;
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
    header.physicalQueueRuntimeStatisticsCount = static_cast<u32>(orderedPhysicalQueueRuntimeStatistics.size());
    header.packetSubmissionStatisticsCount = static_cast<u32>(orderedPacketSubmissionStatistics.size());
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
            AppendPOD(outPayload, FrameGraphQueueCodecDetail::EncodeQueueAssignment(nodeIndex, nodes[nodeIndex].queueAssignment));
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].compiledTask.present)
            AppendPOD(outPayload, __hidden_telemetry_frame_graph::EncodeCompiledTask(nodeIndex, nodes[nodeIndex].compiledTask));
    }
    for(u32 nodeIndex = 0u; nodeIndex < static_cast<u32>(nodes.size()); ++nodeIndex){
        if(nodes[nodeIndex].runtimeStatistics.present)
            AppendPOD(outPayload, __hidden_telemetry_frame_graph::EncodeRuntimeStatistics(nodeIndex, nodes[nodeIndex].runtimeStatistics));
    }
    for(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record : orderedPhysicalQueueRuntimeStatistics){
        AppendPOD(outPayload, FrameGraphStatisticsCodecDetail::EncodePhysicalQueueRuntimeStatistics(record));
    }
    for(const FrameGraphPacketSubmissionStatisticsRecord& statistics : orderedPacketSubmissionStatistics)
        AppendPOD(outPayload, FrameGraphStatisticsCodecDetail::EncodePacketSubmissionStatistics(statistics));
    if(!stringTable.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, stringTable.data(), stringTable.size());

    return outPayload.size() == payloadBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphPayload> ParseFrameGraphPayload(TelemetryArena& arena, Alloc::ScratchArena& scratchArena, const void* const payload, const usize payloadBytes){
    FrameGraphPayload parsedPayload(arena);

    if(payloadBytes < sizeof(EncodedFrameGraphPayloadHeader) || !payload)
        return MakeUnexpected(Failure{});

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    const auto decodedHeader = ReadPOD<EncodedFrameGraphPayloadHeader>(encoded, cursor);
    if(!decodedHeader)
        return MakeUnexpected(Failure{});
    const EncodedFrameGraphPayloadHeader& header = *decodedHeader;
    if(
        header.version != s_FrameGraphPayloadVersion
        || !__hidden_telemetry_frame_graph::ValidateHeader(header.magic, header.reserved)
        || header.packetSubmissionStatisticsPresent > 1u
        || header.reservedTail[0u] != 0u
        || header.reservedTail[1u] != 0u
        || header.reservedTail[2u] != 0u
        || (header.packetSubmissionStatisticsPresent == 0u && header.packetSubmissionStatisticsCount != 0u)
    )
        return MakeUnexpected(Failure{});

    const usize headerBytes = sizeof(EncodedFrameGraphPayloadHeader);
    const u32 queueAssignmentCount = header.queueAssignmentCount;
    const u32 compiledTaskCount = header.compiledTaskCount;
    const u32 runtimeStatisticsCount = header.runtimeStatisticsCount;
    const u32 physicalQueueRuntimeStatisticsCount = header.physicalQueueRuntimeStatisticsCount;
    const u32 packetSubmissionStatisticsCount = header.packetSubmissionStatisticsCount;
    const bool packetSubmissionStatisticsPresent = header.packetSubmissionStatisticsPresent != 0u;
    parsedPayload.packetSubmissionStatisticsPresent = packetSubmissionStatisticsPresent;
    if(queueAssignmentCount > header.nodeCount || compiledTaskCount > header.nodeCount || runtimeStatisticsCount > header.nodeCount)
        return MakeUnexpected(Failure{});

    usize expectedBytes = headerBytes;
    if(
        !AddBinaryRepeatedReserveBytes(expectedBytes, header.nodeCount, sizeof(EncodedFrameGraphNode))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, header.edgeCount, sizeof(EncodedFrameGraphEdge))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, physicalQueueRuntimeStatisticsCount, sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics))
        || !AddBinaryRepeatedReserveBytes(expectedBytes, packetSubmissionStatisticsCount, sizeof(EncodedFrameGraphPacketSubmissionStatistics))
        || !AddBinaryReserveBytes(expectedBytes, header.stringTableBytes)
        || expectedBytes != payloadBytes
    )
        return MakeUnexpected(Failure{});

    usize edgeOffset = headerBytes;
    if(!AddBinaryRepeatedReserveBytes(edgeOffset, header.nodeCount, sizeof(EncodedFrameGraphNode)))
        return MakeUnexpected(Failure{});

    usize queueAssignmentOffset = edgeOffset;
    if(!AddBinaryRepeatedReserveBytes(queueAssignmentOffset, header.edgeCount, sizeof(EncodedFrameGraphEdge)))
        return MakeUnexpected(Failure{});

    usize compiledTaskOffset = queueAssignmentOffset;
    if(!AddBinaryRepeatedReserveBytes(compiledTaskOffset, queueAssignmentCount, sizeof(EncodedFrameGraphQueueAssignment)))
        return MakeUnexpected(Failure{});

    usize runtimeStatisticsOffset = compiledTaskOffset;
    if(!AddBinaryRepeatedReserveBytes(runtimeStatisticsOffset, compiledTaskCount, sizeof(EncodedFrameGraphCompiledTask)))
        return MakeUnexpected(Failure{});

    usize physicalQueueRuntimeStatisticsOffset = runtimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(physicalQueueRuntimeStatisticsOffset, runtimeStatisticsCount, sizeof(EncodedFrameGraphRuntimeStatistics)))
        return MakeUnexpected(Failure{});

    usize stringTableOffset = physicalQueueRuntimeStatisticsOffset;
    if(!AddBinaryRepeatedReserveBytes(stringTableOffset, physicalQueueRuntimeStatisticsCount, sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics)))
        return MakeUnexpected(Failure{});

    if(!AddBinaryRepeatedReserveBytes(stringTableOffset, packetSubmissionStatisticsCount, sizeof(EncodedFrameGraphPacketSubmissionStatistics)))
        return MakeUnexpected(Failure{});

    parsedPayload.frameIndex = header.frameIndex;
    parsedPayload.nodes.reserve(header.nodeCount);
    parsedPayload.edges.reserve(header.edgeCount);
    parsedPayload.physicalQueueRuntimeStatistics.reserve(physicalQueueRuntimeStatisticsCount);
    parsedPayload.packetSubmissionStatistics.reserve(packetSubmissionStatisticsCount);

    for(u32 nodeIndex = 0u; nodeIndex < header.nodeCount; ++nodeIndex){
        const auto decodedEncodedNode = ReadPOD<EncodedFrameGraphNode>(encoded, cursor);
        if(!decodedEncodedNode)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphNode& encodedNode = *decodedEncodedNode;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedNode(encodedNode))
            return MakeUnexpected(Failure{});

        const auto labelView = BinaryDetail::ReadStringTableTextView(encoded, stringTableOffset, header.stringTableBytes, encodedNode.labelOffset);
        if(!labelView)
            return MakeUnexpected(Failure{});

        FrameGraphNodePayload& node = parsedPayload.nodes.emplace_back();
        node.name = Name(encodedNode.nameHash);
        node.label = *labelView;
        node.kind = static_cast<FrameGraphNodeKind::Enum>(encodedNode.kind);
        node.flags = encodedNode.flags;
    }

    for(u32 edgeIndex = 0u; edgeIndex < header.edgeCount; ++edgeIndex){
        const auto decodedEncodedEdge = ReadPOD<EncodedFrameGraphEdge>(encoded, cursor);
        if(!decodedEncodedEdge)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphEdge& encodedEdge = *decodedEncodedEdge;
        if(!__hidden_telemetry_frame_graph::ValidateEncodedEdge(encodedEdge, header.nodeCount))
            return MakeUnexpected(Failure{});

        FrameGraphEdgePayload& edge = parsedPayload.edges.emplace_back();
        edge.fromNodeIndex = encodedEdge.fromNodeIndex;
        edge.toNodeIndex = encodedEdge.toNodeIndex;
        edge.kind = static_cast<FrameGraphEdgeKind::Enum>(encodedEdge.kind);
        edge.flags = encodedEdge.flags;
    }

    u32 previousNodeIndex = 0u;
    for(u32 assignmentIndex = 0u; assignmentIndex < queueAssignmentCount; ++assignmentIndex){
        const auto decodedEncodedAssignment = ReadPOD<EncodedFrameGraphQueueAssignment>(encoded, cursor);
        if(!decodedEncodedAssignment)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphQueueAssignment& encodedAssignment = *decodedEncodedAssignment;
        if(encodedAssignment.nodeIndex >= header.nodeCount || (assignmentIndex != 0u && encodedAssignment.nodeIndex <= previousNodeIndex) || parsedPayload.nodes[encodedAssignment.nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        const auto assignment = FrameGraphQueueCodecDetail::DecodeQueueAssignment(encodedAssignment);
        if(!assignment)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[encodedAssignment.nodeIndex].queueAssignment = *assignment;
        previousNodeIndex = encodedAssignment.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 compiledTaskIndex = 0u; compiledTaskIndex < compiledTaskCount; ++compiledTaskIndex){
        const auto decodedEncodedCompiledTask = ReadPOD<EncodedFrameGraphCompiledTask>(encoded, cursor);
        if(!decodedEncodedCompiledTask)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphCompiledTask& encodedCompiledTask = *decodedEncodedCompiledTask;
        if(encodedCompiledTask.nodeIndex >= header.nodeCount || (compiledTaskIndex != 0u && encodedCompiledTask.nodeIndex <= previousNodeIndex) || parsedPayload.nodes[encodedCompiledTask.nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        const auto compiledTask = __hidden_telemetry_frame_graph::DecodeCompiledTask(encodedCompiledTask);
        if(!compiledTask)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[encodedCompiledTask.nodeIndex].compiledTask = *compiledTask;
        previousNodeIndex = encodedCompiledTask.nodeIndex;
    }

    previousNodeIndex = 0u;
    for(u32 statisticsIndex = 0u; statisticsIndex < runtimeStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphRuntimeStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphRuntimeStatistics& encodedStatistics = *decodedEncodedStatistics;
        const u32 nodeIndex = encodedStatistics.nodeIndex;
        const auto statistics = __hidden_telemetry_frame_graph::DecodeRuntimeStatistics(encodedStatistics);
        if(!statistics)
            return MakeUnexpected(Failure{});
        if(nodeIndex >= header.nodeCount || (statisticsIndex != 0u && nodeIndex <= previousNodeIndex) || parsedPayload.nodes[nodeIndex].kind != FrameGraphNodeKind::Pass)
            return MakeUnexpected(Failure{});
        parsedPayload.nodes[nodeIndex].runtimeStatistics = *statistics;
        previousNodeIndex = nodeIndex;
    }

    FrameGraphPhysicalQueueRuntimeStatisticsRecord previousPhysicalQueueStatistics;
    u32 accumulatedOwnerNodeIndex = Limit<u32>::s_Max;
    __hidden_telemetry_frame_graph::FrameGraphPhysicalQueueRuntimeStatisticsAccumulator accumulatedStatistics;
    for(u32 statisticsIndex = 0u; statisticsIndex < physicalQueueRuntimeStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphPhysicalQueueRuntimeStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encodedStatistics = *decodedEncodedStatistics;
        if(encodedStatistics.ownerNodeIndex >= parsedPayload.nodes.size())
            return MakeUnexpected(Failure{});
        const auto decodedStatistics = FrameGraphStatisticsCodecDetail::DecodePhysicalQueueRuntimeStatistics(encodedStatistics, parsedPayload.nodes[encodedStatistics.ownerNodeIndex].runtimeStatistics);
        if(!decodedStatistics)
            return MakeUnexpected(Failure{});
        const FrameGraphPhysicalQueueRuntimeStatisticsRecord& statistics = *decodedStatistics;
        if(statistics.ownerNodeIndex != accumulatedOwnerNodeIndex){
            accumulatedOwnerNodeIndex = statistics.ownerNodeIndex;
            accumulatedStatistics = {};
        }
        if(!__hidden_telemetry_frame_graph::AccumulatePhysicalQueueRuntimeStatistics(statistics.statistics, parsedPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics, accumulatedStatistics))
            return MakeUnexpected(Failure{});
        if(!__hidden_telemetry_frame_graph::ValidatePhysicalQueueRuntimeStatisticsOwner(statistics, parsedPayload.nodes.size(), parsedPayload.nodes[statistics.ownerNodeIndex].kind, parsedPayload.nodes[statistics.ownerNodeIndex].runtimeStatistics))
            return MakeUnexpected(Failure{});
        if(statisticsIndex != 0u && !__hidden_telemetry_frame_graph::PhysicalQueueRuntimeStatisticsRecordLess(previousPhysicalQueueStatistics, statistics))
            return MakeUnexpected(Failure{});
        parsedPayload.physicalQueueRuntimeStatistics.push_back(statistics);
        previousPhysicalQueueStatistics = statistics;
    }

    for(u32 statisticsIndex = 0u; statisticsIndex < packetSubmissionStatisticsCount; ++statisticsIndex){
        const auto decodedEncodedStatistics = ReadPOD<EncodedFrameGraphPacketSubmissionStatistics>(encoded, cursor);
        if(!decodedEncodedStatistics)
            return MakeUnexpected(Failure{});
        const EncodedFrameGraphPacketSubmissionStatistics& encodedStatistics = *decodedEncodedStatistics;

        const auto statistics = FrameGraphStatisticsCodecDetail::DecodePacketSubmissionStatistics(encodedStatistics);
        if(!statistics)
            return MakeUnexpected(Failure{});
        parsedPayload.packetSubmissionStatistics.push_back(*statistics);
    }
    if(parsedPayload.packetSubmissionStatisticsPresent){
        if(!FrameGraphStatisticsCodecDetail::ValidatePacketSubmissionStatisticsTable(scratchArena, parsedPayload.nodes, parsedPayload.physicalQueueRuntimeStatistics, parsedPayload.packetSubmissionStatistics))
            return MakeUnexpected(Failure{});
    }

    if(cursor != stringTableOffset)
        return MakeUnexpected(Failure{});
    return parsedPayload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

