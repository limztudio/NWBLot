// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include <core/alloc/scratch.h>
#include <global/algorithm.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_validators{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static bool IsCanonicalQueue(const FrameGraphPhysicalQueueId& queue)noexcept{
    return queue.valid()
        || (queue.index == Limit<u16>::s_Max && queue.deviceGeneration == 0u)
    ;
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
        || !__hidden_frame_graph_validators::IsCanonicalQueue(assignment.acceptedQueue)
        || !__hidden_frame_graph_validators::IsCanonicalQueue(assignment.previousAcceptedQueue)
        || assignment.initialQueue.deviceGeneration != assignment.plannedQueue.deviceGeneration
        || !IsValidFrameGraphQueueClass(assignment.queueClass)
        || !IsValidFrameGraphQueueAssignmentReason(assignment.reason)
        || !IsValidFrameGraphQueueAssignmentAcceptance(assignment.acceptance)
        || (
            static_cast<u8>(assignment.modifiers)
            & static_cast<u8>(~static_cast<u8>(FrameGraphQueueAssignmentModifier::All))
        ) != 0u
        || assignment.score.total != __hidden_frame_graph_validators::QueueAssignmentScoreTotal(assignment.score)
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
        || (compile.resourceVersionCount != 0u && compile.resourceCount == 0u)
        || (compile.resourceVersionEdgeCount != 0u && compile.resourceVersionCount == 0u)
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
        || submission.recoverySubmissionCount > submission.acceptedFrontierSubmissionCount
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
        || submission.recoverySubmissionCount > submission.acceptedFrontierSubmissionCount
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
            || submission.recoverySubmissionCount != 0u
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
    if(!__hidden_frame_graph_validators::FrameGraphCompileBarrierCount(ownerCompile, ownerBarrierCount))
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
        && submission.recoverySubmissionCount <= ownerSubmission.recoverySubmissionCount
        && submission.acceptedTaskCount - submission.acceptedPacketCount
            <= ownerSubmission.acceptedTaskCount - ownerSubmission.acceptedPacketCount
        && submission.rejectedTaskCount - submission.rejectedPacketCount
            <= ownerSubmission.rejectedTaskCount - ownerSubmission.rejectedPacketCount
        && submission.nativeCommandListCount - submission.nativeSubmissionCount
            <= ownerSubmission.nativeCommandListCount - ownerSubmission.nativeSubmissionCount
    ;
}

bool IsValidFrameGraphPacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics
)noexcept{
    if(
        statistics.ownerNodeIndex == Limit<u32>::s_Max
        || statistics.packetIndex == Limit<u32>::s_Max
        || statistics.packetGeneration == 0u
        || !statistics.queue.valid()
        || !IsValidFrameGraphQueueClass(statistics.queueClass)
        || statistics.taskCount == 0u
        || statistics.commandListCount == 0u
        || (statistics.recoverySubmission && !statistics.joinsAcceptedQueueFrontier)
        || statistics.sameQueueWaitElisionCount > statistics.plannedWaitTokenCount
        || statistics.submissionSeconds < 0.0
        || !IsFinite(statistics.submissionSeconds)
    )
        return false;

    u64 remainingWaitTokenCount = statistics.plannedWaitTokenCount
        - statistics.sameQueueWaitElisionCount
    ;
    if(statistics.mergedTimelineWaitCount > remainingWaitTokenCount)
        return false;
    remainingWaitTokenCount -= statistics.mergedTimelineWaitCount;
    return statistics.timelineWaitCount == remainingWaitTokenCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

