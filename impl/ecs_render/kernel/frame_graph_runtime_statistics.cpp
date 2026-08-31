// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_runtime_statistics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_runtime_statistics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool TranslateQueueClass(
    const Core::CommandQueue::Enum queueClass,
    Core::Telemetry::FrameGraphQueueClass::Enum& outQueueClass
)noexcept{
    switch(queueClass){
    case Core::CommandQueue::Graphics:
        outQueueClass = Core::Telemetry::FrameGraphQueueClass::Graphics;
        return true;
    case Core::CommandQueue::Compute:
        outQueueClass = Core::Telemetry::FrameGraphQueueClass::Compute;
        return true;
    case Core::CommandQueue::Transfer:
        outQueueClass = Core::Telemetry::FrameGraphQueueClass::Transfer;
        return true;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Telemetry::FrameGraphRuntimeStatistics ECSRenderDetail::BuildFrameGraphRuntimeStatistics(
    const Core::GpuTaskGraphRuntimeStatistics& statistics,
    const u64 captureFrameIndex,
    const u64 sourceFrameIndex
)noexcept{
    if(captureFrameIndex != sourceFrameIndex || !statistics.valid())
        return {};

    const Core::GpuTaskGraphCompileStatistics& compileStatistics = statistics.compile;
    const Core::GpuTaskGraphRecordingStatistics& recordingStatistics = statistics.recording;
    const Core::GpuTaskGraphSubmissionStatistics& submissionStatistics = statistics.submission;
    const Core::Telemetry::FrameGraphRuntimeStatistics result{
        .graphGeneration = compileStatistics.graphGeneration,
        .planGeneration = compileStatistics.planGeneration,
        .recordingAttemptGeneration = recordingStatistics.recordingAttemptGeneration,
        .deviceGeneration = compileStatistics.deviceGeneration,
        .compile = {
            .taskCount = static_cast<u64>(compileStatistics.taskCount),
            .resourceCount = static_cast<u64>(compileStatistics.resourceCount),
            .resourceVersionCount = static_cast<u64>(compileStatistics.resourceVersionCount),
            .resourceVersionEdgeCount = static_cast<u64>(compileStatistics.resourceVersionEdgeCount),
            .resourceUseCount = static_cast<u64>(compileStatistics.resourceUseCount),
            .explicitDependencyCount = static_cast<u64>(compileStatistics.explicitDependencyCount),
            .inferredDependencyCount = static_cast<u64>(compileStatistics.inferredDependencyCount),
            .packetCount = static_cast<u64>(compileStatistics.packetCount),
            .packetDependencyCount = static_cast<u64>(compileStatistics.packetDependencyCount),
            .mergedTaskCount = static_cast<u64>(compileStatistics.mergedTaskCount),
            .transitionBarrierCount = static_cast<u64>(compileStatistics.transitionBarrierCount),
            .uavBarrierCount = static_cast<u64>(compileStatistics.uavBarrierCount),
            .ownershipReleaseBarrierCount = static_cast<u64>(compileStatistics.ownershipReleaseBarrierCount),
            .ownershipAcquireBarrierCount = static_cast<u64>(compileStatistics.ownershipAcquireBarrierCount),
            .stateExportBarrierCount = static_cast<u64>(compileStatistics.stateExportBarrierCount),
            .logicalOwnershipTransferCount = static_cast<u64>(compileStatistics.logicalOwnershipTransferCount),
            .logicalOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.logicalOwnershipTransferSignatureCount
            ),
            .repeatedOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.repeatedOwnershipTransferSignatureCount
            ),
            .concurrentSharingCouldAvoidTransferCount = static_cast<u64>(
                compileStatistics.concurrentSharingCouldAvoidTransferCount
            ),
            .concurrentSharingAdviceResourceCount = static_cast<u64>(
                compileStatistics.concurrentSharingAdviceResourceCount
            ),
            .logicalOwnershipTransferInternalCount = static_cast<u64>(
                compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::Internal]
            ),
            .logicalOwnershipTransferExternalImportCount = static_cast<u64>(
                compileStatistics.logicalOwnershipTransferCountByRoute[
                    Core::GpuOwnershipTransferRoute::ExternalImport
                ]
            ),
            .logicalOwnershipTransferExternalExportCount = static_cast<u64>(
                compileStatistics.logicalOwnershipTransferCountByRoute[
                    Core::GpuOwnershipTransferRoute::ExternalExport
                ]
            ),
            .resourceSetCount = static_cast<u64>(compileStatistics.resourceSetCount),
            .resourceSetMemberCount = static_cast<u64>(compileStatistics.resourceSetMemberCount),
            .directResourceUseCount = static_cast<u64>(compileStatistics.directResourceUseCount),
            .declaredResourceSetUseCount = static_cast<u64>(compileStatistics.declaredResourceSetUseCount),
            .expandedResourceSetMemberUseCount = static_cast<u64>(
                compileStatistics.expandedResourceSetMemberUseCount
            ),
            .payloadObjectCount = static_cast<u64>(compileStatistics.payloadObjectCount),
            .payloadObjectBytes = static_cast<u64>(compileStatistics.payloadObjectBytes),
            .uploadBlobCount = static_cast<u64>(compileStatistics.uploadBlobCount),
            .uploadBlobBytes = static_cast<u64>(compileStatistics.uploadBlobBytes),
            .declarationSeconds = compileStatistics.declarationSeconds,
            .analysisSeconds = compileStatistics.analysisSeconds,
            .validationSeconds = compileStatistics.validationSeconds,
            .dependencyAnalysisSeconds = compileStatistics.dependencyAnalysisSeconds,
            .hazardAnalysisSeconds = compileStatistics.hazardAnalysisSeconds,
            .topologicalOrderSeconds = compileStatistics.topologicalOrderSeconds,
            .queueAssignmentSeconds = compileStatistics.queueAssignmentSeconds,
            .planningSeconds = compileStatistics.planningSeconds,
            .packetizationSeconds = compileStatistics.packetizationSeconds,
            .resourceStatePlanningSeconds = compileStatistics.resourceStatePlanningSeconds,
            .packetDependencyPlanningSeconds = compileStatistics.packetDependencyPlanningSeconds,
            .totalSeconds = compileStatistics.totalSeconds,
        },
        .recording = {
            .packetCount = static_cast<u64>(recordingStatistics.packetCount),
            .taskCount = static_cast<u64>(recordingStatistics.taskCount),
            .commandListCount = static_cast<u64>(recordingStatistics.commandListCount),
            .barrierCount = static_cast<u64>(recordingStatistics.barrierCount),
            .workerRoutedPacketCount = static_cast<u64>(recordingStatistics.workerRoutedPacketCount),
            .parallelPacketCount = static_cast<u64>(recordingStatistics.parallelPacketCount),
            .commandListAcquisitionSeconds = recordingStatistics.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = recordingStatistics.graphBarrierRecordingSeconds,
            .taskRecordSeconds = recordingStatistics.taskRecordSeconds,
            .recordingSeconds = recordingStatistics.recordingSeconds,
            .recordingElapsedSeconds = recordingStatistics.recordingElapsedSeconds,
            .readyFrontierElapsedSeconds = recordingStatistics.readyFrontierElapsedSeconds,
            .readyFrontierWorkerBusySeconds = recordingStatistics.readyFrontierWorkerBusySeconds,
            .readyFrontierWorkerCapacitySeconds = recordingStatistics.readyFrontierWorkerCapacitySeconds,
        },
        .submission = {
            .acceptedPacketCount = static_cast<u64>(submissionStatistics.acceptedPacketCount),
            .acceptedTaskCount = static_cast<u64>(submissionStatistics.acceptedTaskCount),
            .rejectedPacketCount = static_cast<u64>(submissionStatistics.rejectedPacketCount),
            .rejectedTaskCount = static_cast<u64>(submissionStatistics.rejectedTaskCount),
            .nativeSubmissionCount = static_cast<u64>(submissionStatistics.nativeSubmissionCount),
            .rejectedSubmissionCount = static_cast<u64>(submissionStatistics.rejectedSubmissionCount),
            .nativeCommandListCount = static_cast<u64>(submissionStatistics.nativeCommandListCount),
            .plannedWaitTokenCount = static_cast<u64>(submissionStatistics.plannedWaitTokenCount),
            .sameQueueWaitElisionCount = static_cast<u64>(submissionStatistics.sameQueueWaitElisionCount),
            .timelineWaitCount = static_cast<u64>(submissionStatistics.timelineWaitCount),
            .mergedTimelineWaitCount = static_cast<u64>(submissionStatistics.mergedTimelineWaitCount),
            .acceptedFrontierSubmissionCount = static_cast<u64>(
                submissionStatistics.acceptedFrontierSubmissionCount
            ),
            .recoverySubmissionCount = static_cast<u64>(submissionStatistics.recoverySubmissionCount),
            .submissionSeconds = submissionStatistics.submissionSeconds,
        },
        .present = true,
    };
    if(!Core::Telemetry::IsValidFrameGraphRuntimeStatistics(result))
        return {};
    return result;
}

Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord
ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(
    const Core::GpuTaskGraphPacketSubmissionStatistics& statistics,
    const u32 ownerNodeIndex
)noexcept{
    if(!statistics.valid() || ownerNodeIndex == Limit<u32>::s_Max)
        return {};

    Core::Telemetry::FrameGraphQueueClass::Enum queueClass = Core::Telemetry::FrameGraphQueueClass::Unknown;
    if(!__hidden_frame_graph_runtime_statistics::TranslateQueueClass(statistics.queueClass, queueClass))
        return {};

    const Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord result{
        .ownerNodeIndex = ownerNodeIndex,
        .packetIndex = statistics.packet.index,
        .packetGeneration = statistics.packet.generation,
        .queue = {
            .index = statistics.queue.index,
            .deviceGeneration = statistics.queue.deviceGeneration,
        },
        .queueClass = queueClass,
        .taskCount = static_cast<u64>(statistics.taskCount),
        .commandListCount = static_cast<u64>(statistics.nativeCommandListCount),
        .plannedWaitTokenCount = static_cast<u64>(statistics.plannedWaitTokenCount),
        .sameQueueWaitElisionCount = static_cast<u64>(statistics.sameQueueWaitElisionCount),
        .timelineWaitCount = static_cast<u64>(statistics.timelineWaitCount),
        .mergedTimelineWaitCount = static_cast<u64>(statistics.mergedTimelineWaitCount),
        .joinsAcceptedQueueFrontier = statistics.joinsAcceptedQueueFrontier,
        .recoverySubmission = statistics.isRecoverySubmission,
        .submissionSeconds = statistics.submissionSeconds,
    };
    if(!Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(result))
        return {};
    return result;
}

Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics
ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
    const Core::GpuTaskGraphPhysicalQueueCompileStatistics& compileStatistics,
    const Core::GpuTaskGraphPhysicalQueueRecordingStatistics& recordingStatistics,
    const Core::GpuTaskGraphPhysicalQueueSubmissionStatistics& submissionStatistics
)noexcept{
    if(
        !compileStatistics.valid()
        || !recordingStatistics.valid()
        || !submissionStatistics.valid()
        || compileStatistics.graphGeneration != recordingStatistics.graphGeneration
        || compileStatistics.graphGeneration != submissionStatistics.graphGeneration
        || compileStatistics.planGeneration != recordingStatistics.planGeneration
        || compileStatistics.planGeneration != submissionStatistics.planGeneration
        || recordingStatistics.recordingAttemptGeneration != submissionStatistics.recordingAttemptGeneration
        || compileStatistics.deviceGeneration != recordingStatistics.deviceGeneration
        || compileStatistics.deviceGeneration != submissionStatistics.deviceGeneration
        || compileStatistics.queue != recordingStatistics.queue
        || compileStatistics.queue != submissionStatistics.queue
        || compileStatistics.queueClass != recordingStatistics.queueClass
        || compileStatistics.queueClass != submissionStatistics.queueClass
    )
        return {};

    Core::Telemetry::FrameGraphQueueClass::Enum queueClass = Core::Telemetry::FrameGraphQueueClass::Unknown;
    if(!__hidden_frame_graph_runtime_statistics::TranslateQueueClass(compileStatistics.queueClass, queueClass))
        return {};

    const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics result{
        .graphGeneration = compileStatistics.graphGeneration,
        .planGeneration = compileStatistics.planGeneration,
        .recordingAttemptGeneration = recordingStatistics.recordingAttemptGeneration,
        .deviceGeneration = compileStatistics.deviceGeneration,
        .queue = {
            .index = compileStatistics.queue.index,
            .deviceGeneration = compileStatistics.queue.deviceGeneration,
        },
        .queueClass = queueClass,
        .compile = {
            .taskCount = static_cast<u64>(compileStatistics.taskCount),
            .packetCount = static_cast<u64>(compileStatistics.packetCount),
            .mergedTaskCount = static_cast<u64>(compileStatistics.mergedTaskCount),
            .prologueBarrierCount = static_cast<u64>(compileStatistics.prologueBarrierCount),
            .epilogueBarrierCount = static_cast<u64>(compileStatistics.epilogueBarrierCount),
            .ownershipReleaseBarrierCount = static_cast<u64>(compileStatistics.ownershipReleaseBarrierCount),
            .ownershipAcquireBarrierCount = static_cast<u64>(compileStatistics.ownershipAcquireBarrierCount),
            .incomingLogicalOwnershipTransferCount = static_cast<u64>(
                compileStatistics.incomingLogicalOwnershipTransferCount
            ),
            .outgoingLogicalOwnershipTransferCount = static_cast<u64>(
                compileStatistics.outgoingLogicalOwnershipTransferCount
            ),
            .incomingLogicalOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.incomingLogicalOwnershipTransferSignatureCount
            ),
            .outgoingLogicalOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.outgoingLogicalOwnershipTransferSignatureCount
            ),
            .incomingRepeatedOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.incomingRepeatedOwnershipTransferSignatureCount
            ),
            .outgoingRepeatedOwnershipTransferSignatureCount = static_cast<u64>(
                compileStatistics.outgoingRepeatedOwnershipTransferSignatureCount
            ),
            .concurrentSharingAdviceResourceCount = static_cast<u64>(
                compileStatistics.concurrentSharingAdviceResourceCount
            ),
        },
        .recording = {
            .packetCount = static_cast<u64>(recordingStatistics.packetCount),
            .taskCount = static_cast<u64>(recordingStatistics.taskCount),
            .commandListCount = static_cast<u64>(recordingStatistics.commandListCount),
            .barrierCount = static_cast<u64>(recordingStatistics.barrierCount),
            .workerRoutedPacketCount = static_cast<u64>(recordingStatistics.workerRoutedPacketCount),
            .parallelPacketCount = static_cast<u64>(recordingStatistics.parallelPacketCount),
            .commandListAcquisitionSeconds = recordingStatistics.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = recordingStatistics.graphBarrierRecordingSeconds,
            .taskRecordSeconds = recordingStatistics.taskRecordSeconds,
            .recordingSeconds = recordingStatistics.recordingSeconds,
        },
        .submission = {
            .acceptedPacketCount = static_cast<u64>(submissionStatistics.acceptedPacketCount),
            .acceptedTaskCount = static_cast<u64>(submissionStatistics.acceptedTaskCount),
            .rejectedPacketCount = static_cast<u64>(submissionStatistics.rejectedPacketCount),
            .rejectedTaskCount = static_cast<u64>(submissionStatistics.rejectedTaskCount),
            .nativeSubmissionCount = static_cast<u64>(submissionStatistics.nativeSubmissionCount),
            .rejectedSubmissionCount = static_cast<u64>(submissionStatistics.rejectedSubmissionCount),
            .nativeCommandListCount = static_cast<u64>(submissionStatistics.nativeCommandListCount),
            .plannedWaitTokenCount = static_cast<u64>(submissionStatistics.plannedWaitTokenCount),
            .sameQueueWaitElisionCount = static_cast<u64>(submissionStatistics.sameQueueWaitElisionCount),
            .timelineWaitCount = static_cast<u64>(submissionStatistics.timelineWaitCount),
            .mergedTimelineWaitCount = static_cast<u64>(submissionStatistics.mergedTimelineWaitCount),
            .acceptedFrontierSubmissionCount = static_cast<u64>(
                submissionStatistics.acceptedFrontierSubmissionCount
            ),
            .recoverySubmissionCount = static_cast<u64>(submissionStatistics.recoverySubmissionCount),
            .submissionSeconds = submissionStatistics.submissionSeconds,
        },
    };
    if(!Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(result))
        return {};
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

