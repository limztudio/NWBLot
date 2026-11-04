// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_statistics_codec_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsCodecDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    noexcept(ConvertPhysicalQueueRecordingRuntimeStatistics<OutputT>(DeclVal<const InputT&>()))
    && noexcept(DeclVal<void(*)(OutputT)noexcept>()(DeclVal<OutputT&&>()))
    && noexcept(DeclVal<OutputT&>().recordingElapsedSeconds = DeclVal<const InputT&>().recordingElapsedSeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierElapsedSeconds = DeclVal<const InputT&>().readyFrontierElapsedSeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierWorkerBusySeconds = DeclVal<const InputT&>().readyFrontierWorkerBusySeconds)
    && noexcept(DeclVal<OutputT&>().readyFrontierWorkerCapacitySeconds = DeclVal<const InputT&>().readyFrontierWorkerCapacitySeconds)
){
    OutputT converted = ConvertPhysicalQueueRecordingRuntimeStatistics<OutputT>(statistics);
    converted.recordingElapsedSeconds = statistics.recordingElapsedSeconds;
    converted.readyFrontierElapsedSeconds = statistics.readyFrontierElapsedSeconds;
    converted.readyFrontierWorkerBusySeconds = statistics.readyFrontierWorkerBusySeconds;
    converted.readyFrontierWorkerCapacitySeconds = statistics.readyFrontierWorkerCapacitySeconds;
    return converted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EncodedFrameGraphPhysicalQueueRuntimeStatistics EncodePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record)noexcept{
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics = record.statistics;
    EncodedFrameGraphPhysicalQueueRuntimeStatistics encoded;
    encoded.ownerNodeIndex = record.ownerNodeIndex;
    encoded.queue = EncodeQueue(statistics.queue);
    encoded.queueClass = statistics.queueClass;
    encoded.compile = ConvertPhysicalQueueCompileRuntimeStatistics<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>(statistics.compile);
    encoded.recording = ConvertPhysicalQueueRecordingRuntimeStatistics<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>(statistics.recording);
    encoded.submission = ConvertSubmissionRuntimeStatistics<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>(statistics.submission);
    return encoded;
}

Expected<FrameGraphPhysicalQueueRuntimeStatisticsRecord> DecodePhysicalQueueRuntimeStatistics(const EncodedFrameGraphPhysicalQueueRuntimeStatistics& encoded, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    FrameGraphPhysicalQueueRuntimeStatisticsRecord decoded;
    if(encoded.reserved[0u] != 0u || encoded.reserved[1u] != 0u || encoded.reserved[2u] != 0u || encoded.reserved[3u] != 0u || encoded.reserved[4u] != 0u || encoded.reserved[5u] != 0u || encoded.reserved[6u] != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
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
    if(!IsValidFrameGraphPhysicalQueueRuntimeStatistics(decoded.statistics))
        return MakeUnexpected(Failure{});
    return decoded;
}

b8 PacketSubmissionStatisticsRecordLess(const FrameGraphPacketSubmissionStatisticsRecord& lhs, const FrameGraphPacketSubmissionStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    return lhs.packetIndex < rhs.packetIndex;
}

EncodedFrameGraphPacketSubmissionStatistics EncodePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics)noexcept{
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

Expected<FrameGraphPacketSubmissionStatisticsRecord> DecodePacketSubmissionStatistics(const EncodedFrameGraphPacketSubmissionStatistics& encoded)noexcept{
    FrameGraphPacketSubmissionStatisticsRecord decoded;
    if(encoded.joinsAcceptedQueueFrontier > 1u || encoded.recoverySubmission > 1u || encoded.reserved != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
        .packetGeneration = encoded.packetGeneration,
        .taskCount = encoded.taskCount,
        .commandListCount = encoded.commandListCount,
        .plannedWaitTokenCount = encoded.plannedWaitTokenCount,
        .sameQueueWaitElisionCount = encoded.sameQueueWaitElisionCount,
        .timelineWaitCount = encoded.timelineWaitCount,
        .mergedTimelineWaitCount = encoded.mergedTimelineWaitCount,
        .inheritedTimelineWaitElisionCount = encoded.inheritedTimelineWaitElisionCount,
        .submissionSeconds = encoded.submissionSeconds,
        .queue = DecodeQueue(encoded.queue),
        .ownerNodeIndex = encoded.ownerNodeIndex,
        .packetIndex = encoded.packetIndex,
        .queueClass = static_cast<FrameGraphQueueClass::Enum>(encoded.queueClass),
        .joinsAcceptedQueueFrontier = encoded.joinsAcceptedQueueFrontier != 0u,
        .recoverySubmission = encoded.recoverySubmission != 0u,
    };
    if(!IsValidFrameGraphPacketSubmissionStatistics(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}

EncodedFrameGraphRuntimeStatistics EncodeRuntimeStatistics(const u32 nodeIndex, const FrameGraphRuntimeStatistics& statistics)noexcept{
    EncodedFrameGraphRuntimeStatistics encoded;
    encoded.nodeIndex = nodeIndex;
    encoded.deviceGeneration = statistics.deviceGeneration;
    encoded.graphGeneration = statistics.graphGeneration;
    encoded.planGeneration = statistics.planGeneration;
    encoded.recordingAttemptGeneration = statistics.recordingAttemptGeneration;
    encoded.compile = ConvertCompileRuntimeStatistics<EncodedFrameGraphCompileRuntimeStatistics>(statistics.compile);
    encoded.recording = ConvertRecordingRuntimeStatistics<EncodedFrameGraphRecordingRuntimeStatistics>(statistics.recording);
    encoded.submission = ConvertSubmissionRuntimeStatistics<EncodedFrameGraphSubmissionRuntimeStatistics>(statistics.submission);
    return encoded;
}

Expected<FrameGraphRuntimeStatistics> DecodeRuntimeStatistics(const EncodedFrameGraphRuntimeStatistics& encoded)noexcept{
    FrameGraphRuntimeStatistics decoded;
    if(encoded.reserved != 0u)
        return MakeUnexpected(Failure{});

    decoded = {
        .graphGeneration = encoded.graphGeneration,
        .planGeneration = encoded.planGeneration,
        .recordingAttemptGeneration = encoded.recordingAttemptGeneration,
        .compile = ConvertCompileRuntimeStatistics<FrameGraphCompileRuntimeStatistics>(encoded.compile),
        .recording = ConvertRecordingRuntimeStatistics<FrameGraphRecordingRuntimeStatistics>(encoded.recording),
        .submission = ConvertSubmissionRuntimeStatistics<FrameGraphSubmissionRuntimeStatistics>(encoded.submission),
        .deviceGeneration = encoded.deviceGeneration,
        .present = true,
    };
    if(!IsValidFrameGraphRuntimeStatistics(decoded))
        return MakeUnexpected(Failure{});
    return decoded;
}

b8 PhysicalQueueRuntimeStatisticsRecordLess(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs, const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    if(lhs.statistics.queue.index != rhs.statistics.queue.index)
        return lhs.statistics.queue.index < rhs.statistics.queue.index;
    return lhs.statistics.queue.deviceGeneration < rhs.statistics.queue.deviceGeneration;
}

b8 AccumulatePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatistics& statistics, const FrameGraphRuntimeStatistics& ownerStatistics, FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total)noexcept{
    const auto barrierCount = FrameGraphStatisticsDetail::FrameGraphCompileBarrierCount(ownerStatistics.compile);
    if(!barrierCount)
        return false;
    const u64 ownerBarrierCount = *barrierCount;

    const FrameGraphPhysicalQueueCompileRuntimeStatistics& compile = statistics.compile;
    const FrameGraphCompileRuntimeStatistics& ownerCompile = ownerStatistics.compile;
    if(
        !AccumulateBoundedCount(compile.taskCount, ownerCompile.taskCount, total.compile.taskCount)
        || !AccumulateBoundedCount(compile.packetCount, ownerCompile.packetCount, total.compile.packetCount)
        || !AccumulateBoundedCount(compile.mergedTaskCount, ownerCompile.mergedTaskCount, total.compile.mergedTaskCount)
        || !AccumulateBoundedCount(compile.prologueBarrierCount, ownerBarrierCount, total.barrierCount)
        || !AccumulateBoundedCount(compile.epilogueBarrierCount, ownerBarrierCount, total.barrierCount)
        || !AccumulateBoundedCount(compile.ownershipReleaseBarrierCount, ownerCompile.ownershipReleaseBarrierCount, total.compile.ownershipReleaseBarrierCount)
        || !AccumulateBoundedCount(compile.ownershipAcquireBarrierCount, ownerCompile.ownershipAcquireBarrierCount, total.compile.ownershipAcquireBarrierCount)
        || !AccumulateBoundedCount(compile.incomingLogicalOwnershipTransferCount, ownerCompile.logicalOwnershipTransferCount, total.compile.incomingLogicalOwnershipTransferCount)
        || !AccumulateBoundedCount(compile.outgoingLogicalOwnershipTransferCount, ownerCompile.logicalOwnershipTransferCount, total.compile.outgoingLogicalOwnershipTransferCount)
        || !AccumulateBoundedCount(compile.incomingLogicalOwnershipTransferSignatureCount, ownerCompile.logicalOwnershipTransferSignatureCount, total.compile.incomingLogicalOwnershipTransferSignatureCount)
        || !AccumulateBoundedCount(compile.outgoingLogicalOwnershipTransferSignatureCount, ownerCompile.logicalOwnershipTransferSignatureCount, total.compile.outgoingLogicalOwnershipTransferSignatureCount)
        || !AccumulateBoundedCount(compile.incomingRepeatedOwnershipTransferSignatureCount, ownerCompile.repeatedOwnershipTransferSignatureCount, total.compile.incomingRepeatedOwnershipTransferSignatureCount)
        || !AccumulateBoundedCount(compile.outgoingRepeatedOwnershipTransferSignatureCount, ownerCompile.repeatedOwnershipTransferSignatureCount, total.compile.outgoingRepeatedOwnershipTransferSignatureCount)
    )
        return false;

    const FrameGraphPhysicalQueueRecordingRuntimeStatistics& recording = statistics.recording;
    const FrameGraphRecordingRuntimeStatistics& ownerRecording = ownerStatistics.recording;
    if(
        !AccumulateBoundedCount(recording.packetCount, ownerRecording.packetCount, total.recording.packetCount)
        || !AccumulateBoundedCount(recording.taskCount, ownerRecording.taskCount, total.recording.taskCount)
        || !AccumulateBoundedCount(recording.commandListCount, ownerRecording.commandListCount, total.recording.commandListCount)
        || !AccumulateBoundedCount(recording.barrierCount, ownerRecording.barrierCount, total.recording.barrierCount)
        || !AccumulateBoundedCount(recording.workerRoutedPacketCount, ownerRecording.workerRoutedPacketCount, total.recording.workerRoutedPacketCount)
        || !AccumulateBoundedCount(recording.parallelPacketCount, ownerRecording.parallelPacketCount, total.recording.parallelPacketCount)
    )
        return false;
    if(total.recording.taskCount - total.recording.packetCount > ownerRecording.taskCount - ownerRecording.packetCount || total.recording.commandListCount - total.recording.packetCount > ownerRecording.commandListCount - ownerRecording.packetCount)
        return false;

    const FrameGraphPhysicalQueueSubmissionRuntimeStatistics& submission = statistics.submission;
    const FrameGraphSubmissionRuntimeStatistics& ownerSubmission = ownerStatistics.submission;
    if(
        !AccumulateBoundedCount(submission.acceptedPacketCount, ownerSubmission.acceptedPacketCount, total.submission.acceptedPacketCount)
        || !AccumulateBoundedCount(submission.acceptedTaskCount, ownerSubmission.acceptedTaskCount, total.submission.acceptedTaskCount)
        || !AccumulateBoundedCount(submission.rejectedPacketCount, ownerSubmission.rejectedPacketCount, total.submission.rejectedPacketCount)
        || !AccumulateBoundedCount(submission.rejectedTaskCount, ownerSubmission.rejectedTaskCount, total.submission.rejectedTaskCount)
        || !AccumulateBoundedCount(submission.nativeSubmissionCount, ownerSubmission.nativeSubmissionCount, total.submission.nativeSubmissionCount)
        || !AccumulateBoundedCount(submission.rejectedSubmissionCount, ownerSubmission.rejectedSubmissionCount, total.submission.rejectedSubmissionCount)
        || !AccumulateBoundedCount(submission.nativeCommandListCount, ownerSubmission.nativeCommandListCount, total.submission.nativeCommandListCount)
        || !AccumulateBoundedCount(submission.plannedWaitTokenCount, ownerSubmission.plannedWaitTokenCount, total.submission.plannedWaitTokenCount)
        || !AccumulateBoundedCount(submission.sameQueueWaitElisionCount, ownerSubmission.sameQueueWaitElisionCount, total.submission.sameQueueWaitElisionCount)
        || !AccumulateBoundedCount(submission.timelineWaitCount, ownerSubmission.timelineWaitCount, total.submission.timelineWaitCount)
        || !AccumulateBoundedCount(submission.mergedTimelineWaitCount, ownerSubmission.mergedTimelineWaitCount, total.submission.mergedTimelineWaitCount)
        || !AccumulateBoundedCount(submission.inheritedTimelineWaitElisionCount, ownerSubmission.inheritedTimelineWaitElisionCount, total.submission.inheritedTimelineWaitElisionCount)
        || !AccumulateBoundedCount(submission.acceptedFrontierSubmissionCount, ownerSubmission.acceptedFrontierSubmissionCount, total.submission.acceptedFrontierSubmissionCount)
        || !AccumulateBoundedCount(submission.recoverySubmissionCount, ownerSubmission.recoverySubmissionCount, total.submission.recoverySubmissionCount)
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

b8 ValidatePhysicalQueueRuntimeStatisticsOwner(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record, const usize nodeCount, const FrameGraphNodeKind::Enum ownerKind, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    return static_cast<usize>(record.ownerNodeIndex) < nodeCount && ownerKind == FrameGraphNodeKind::Pass && IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(record.statistics, ownerStatistics);
}

b8 AccumulatePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphSubmissionRuntimeStatistics& ownerStatistics, FrameGraphPacketSubmissionStatisticsAccumulator& total)noexcept{
    if(!(
        AccumulateBoundedCount(1u, ownerStatistics.nativeSubmissionCount, total.nativeSubmissionCount)
        && AccumulateBoundedCount(statistics.taskCount, ownerStatistics.acceptedTaskCount, total.taskCount)
        && AccumulateBoundedCount(statistics.commandListCount, ownerStatistics.nativeCommandListCount, total.commandListCount)
        && AccumulateBoundedCount(statistics.plannedWaitTokenCount, ownerStatistics.plannedWaitTokenCount, total.plannedWaitTokenCount)
        && AccumulateBoundedCount(statistics.sameQueueWaitElisionCount, ownerStatistics.sameQueueWaitElisionCount, total.sameQueueWaitElisionCount)
        && AccumulateBoundedCount(statistics.timelineWaitCount, ownerStatistics.timelineWaitCount, total.timelineWaitCount)
        && AccumulateBoundedCount(statistics.mergedTimelineWaitCount, ownerStatistics.mergedTimelineWaitCount, total.mergedTimelineWaitCount)
        && AccumulateBoundedCount(statistics.inheritedTimelineWaitElisionCount, ownerStatistics.inheritedTimelineWaitElisionCount, total.inheritedTimelineWaitElisionCount)
        && AccumulateBoundedCount(statistics.joinsAcceptedQueueFrontier ? 1u : 0u, ownerStatistics.acceptedFrontierSubmissionCount, total.acceptedFrontierSubmissionCount)
        && AccumulateBoundedCount(statistics.recoverySubmission ? 1u : 0u, ownerStatistics.recoverySubmissionCount, total.recoverySubmissionCount)
    ))
        return false;

    total.submissionSeconds += statistics.submissionSeconds;
    return IsFinite(total.submissionSeconds);
}

b8 PacketSubmissionDurationSumsMatch(const f64 lhs, const f64 rhs, const u64 submissionCount)noexcept{
    if(lhs == rhs)
        return true;
    if(submissionCount == 0u)
        return false;

    const f64 roundingFactor = static_cast<f64>(submissionCount) * s_DoublePrecisionEpsilon;
    if(roundingFactor >= s_SubmissionRoundingLimit)
        return false;
    const f64 magnitude = Max(lhs, rhs);
    const f64 difference = magnitude - Min(rhs, lhs);
    return (difference / magnitude) <= ((s_SubmissionRoundingScale * roundingFactor) / (1.0 - roundingFactor));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

