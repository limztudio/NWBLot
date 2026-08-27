// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_runtime_statistics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
            .submissionSeconds = submissionStatistics.submissionSeconds,
        },
        .present = true,
    };
    if(!Core::Telemetry::IsValidFrameGraphRuntimeStatistics(result))
        return {};
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

