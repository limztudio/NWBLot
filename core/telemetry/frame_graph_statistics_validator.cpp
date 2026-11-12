// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_statistics_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FrameGraphStatisticsValidator::PacketSubmissionStatisticsRecordLess(const FrameGraphPacketSubmissionStatisticsRecord& lhs, const FrameGraphPacketSubmissionStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    return lhs.packetIndex < rhs.packetIndex;
}

b8 FrameGraphStatisticsValidator::PhysicalQueueRuntimeStatisticsRecordLess(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& lhs, const FrameGraphPhysicalQueueRuntimeStatisticsRecord& rhs)noexcept{
    if(lhs.ownerNodeIndex != rhs.ownerNodeIndex)
        return lhs.ownerNodeIndex < rhs.ownerNodeIndex;
    if(lhs.statistics.queue.index != rhs.statistics.queue.index)
        return lhs.statistics.queue.index < rhs.statistics.queue.index;
    return lhs.statistics.queue.deviceGeneration < rhs.statistics.queue.deviceGeneration;
}

b8 FrameGraphStatisticsValidator::AccumulatePhysicalQueueRuntimeStatistics(const FrameGraphPhysicalQueueRuntimeStatistics& statistics, const FrameGraphRuntimeStatistics& ownerStatistics, FrameGraphPhysicalQueueRuntimeStatisticsAccumulator& total)noexcept{
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

b8 FrameGraphStatisticsValidator::ValidatePhysicalQueueRuntimeStatisticsOwner(const FrameGraphPhysicalQueueRuntimeStatisticsRecord& record, const usize nodeCount, const FrameGraphNodeKind::Enum ownerKind, const FrameGraphRuntimeStatistics& ownerStatistics)noexcept{
    return static_cast<usize>(record.ownerNodeIndex) < nodeCount && ownerKind == FrameGraphNodeKind::Pass && IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(record.statistics, ownerStatistics);
}

b8 FrameGraphStatisticsValidator::AccumulatePacketSubmissionStatistics(const FrameGraphPacketSubmissionStatisticsRecord& statistics, const FrameGraphSubmissionRuntimeStatistics& ownerStatistics, FrameGraphPacketSubmissionStatisticsAccumulator& total)noexcept{
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

b8 FrameGraphStatisticsValidator::PacketSubmissionDurationSumsMatch(const f64 lhs, const f64 rhs, const u64 submissionCount)noexcept{
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


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

