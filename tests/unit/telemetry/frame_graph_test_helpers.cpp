// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


void BuildTestFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    nodes = Telemetry::FrameGraphNodeDescs(arena);
    edges = Telemetry::FrameGraphEdgeDescs(arena);

    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("gbuffer"),
        .label = AStringView("GBuffer Pass"),
        .kind = Telemetry::FrameGraphNodeKind::Pass,
        .flags = 1u,
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("albedo"),
        .label = AStringView("Albedo Texture"),
        .kind = Telemetry::FrameGraphNodeKind::Resource,
        .flags = 0u,
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
    });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{
        .name = Name("lighting"),
        .label = AStringView("Lighting Pass"),
        .kind = Telemetry::FrameGraphNodeKind::Pass,
        .flags = 0u,
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = {},
    });

    edges.push_back(Telemetry::FrameGraphEdgeDesc{
        .fromNodeIndex = 0u,
        .toNodeIndex = 1u,
        .kind = Telemetry::FrameGraphEdgeKind::Writes,
    });
    edges.push_back(Telemetry::FrameGraphEdgeDesc{
        .fromNodeIndex = 1u,
        .toNodeIndex = 2u,
        .kind = Telemetry::FrameGraphEdgeKind::Reads,
        .flags = 2u,
    });
}

Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment(){
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 1u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 3u, .deviceGeneration = 17u };
    assignment.acceptedQueue = assignment.plannedQueue;
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = {
        .preference = 11,
        .overlap = 7,
        .queueLoad = 3,
        .incomingCrossings = 2,
        .outgoingCrossings = 1,
        .ownershipTransfers = 4,
        .total = 8,
    };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Compute;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::Fallback;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::All;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::Changed;
    assignment.dedicated = true;
    assignment.present = true;
    return assignment;
}

Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment(){
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 4u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 5u, .deviceGeneration = 17u };
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = {
        .preference = 5,
        .overlap = 6,
        .queueLoad = 1,
        .incomingCrossings = 2,
        .outgoingCrossings = 3,
        .ownershipTransfers = 4,
        .total = 1,
    };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Transfer;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::ScoredAny;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted;
    assignment.present = true;
    return assignment;
}

Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(
    const u64 planGeneration,
    const u32 packetIndex,
    const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision
){
    return Telemetry::FrameGraphCompiledTask{
        .planGeneration = planGeneration,
        .packetIndex = packetIndex,
        .packetizationDecision = packetizationDecision,
        .present = true,
    };
}

Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics(){
    return Telemetry::FrameGraphRuntimeStatistics{
        .graphGeneration = 51u,
        .planGeneration = 52u,
        .recordingAttemptGeneration = 53u,
        .deviceGeneration = 17u,
        .compile = {
            .taskCount = 78u,
            .resourceCount = 2u,
            .resourceVersionCount = 3u,
            .resourceVersionEdgeCount = 6u,
            .resourceUseCount = 50u,
            .explicitDependencyCount = 4u,
            .inferredDependencyCount = 5u,
            .packetCount = 76u,
            .packetDependencyCount = 7u,
            .mergedTaskCount = 2u,
            .transitionBarrierCount = 9u,
            .uavBarrierCount = 10u,
            .ownershipReleaseBarrierCount = 11u,
            .ownershipAcquireBarrierCount = 12u,
            .stateExportBarrierCount = 13u,
            .logicalOwnershipTransferCount = 60u,
            .logicalOwnershipTransferSignatureCount = 15u,
            .repeatedOwnershipTransferSignatureCount = 14u,
            .concurrentSharingCouldAvoidTransferCount = 17u,
            .concurrentSharingAdviceResourceCount = 2u,
            .logicalOwnershipTransferInternalCount = 19u,
            .logicalOwnershipTransferExternalImportCount = 20u,
            .logicalOwnershipTransferExternalExportCount = 21u,
            .resourceSetCount = 22u,
            .resourceSetMemberCount = 23u,
            .directResourceUseCount = 24u,
            .declaredResourceSetUseCount = 25u,
            .expandedResourceSetMemberUseCount = 26u,
            .payloadObjectCount = 27u,
            .payloadObjectBytes = 28u,
            .uploadBlobCount = 29u,
            .uploadBlobBytes = 30u,
            .declarationSeconds = 0.001,
            .analysisSeconds = 0.002,
            .validationSeconds = 0.003,
            .dependencyAnalysisSeconds = 0.004,
            .hazardAnalysisSeconds = 0.005,
            .topologicalOrderSeconds = 0.006,
            .queueAssignmentSeconds = 0.007,
            .planningSeconds = 0.008,
            .packetizationSeconds = 0.009,
            .resourceStatePlanningSeconds = 0.010,
            .packetDependencyPlanningSeconds = 0.011,
            .totalSeconds = 0.012,
        },
        .recording = {
            .packetCount = 31u,
            .taskCount = 32u,
            .commandListCount = 33u,
            .barrierCount = 34u,
            .workerRoutedPacketCount = 30u,
            .parallelPacketCount = 29u,
            .commandListAcquisitionSeconds = 0.013,
            .graphBarrierRecordingSeconds = 0.014,
            .taskRecordSeconds = 0.015,
            .recordingSeconds = 0.016,
            .recordingElapsedSeconds = 0.017,
            .readyFrontierElapsedSeconds = 0.018,
            .readyFrontierWorkerBusySeconds = 0.019,
            .readyFrontierWorkerCapacitySeconds = 0.020,
        },
        .submission = {
            .acceptedPacketCount = 37u,
            .acceptedTaskCount = 38u,
            .rejectedPacketCount = 39u,
            .rejectedTaskCount = 40u,
            .nativeSubmissionCount = 30u,
            .rejectedSubmissionCount = 38u,
            .nativeCommandListCount = 32u,
            .plannedWaitTokenCount = 44u,
            .sameQueueWaitElisionCount = 12u,
            .timelineWaitCount = 14u,
            .mergedTimelineWaitCount = 18u,
            .acceptedFrontierSubmissionCount = 28u,
            .recoverySubmissionCount = 8u,
            .submissionSeconds = 0.021,
        },
        .present = true,
    };
}

Telemetry::FrameGraphPhysicalQueueRuntimeStatistics MakeFrameGraphPhysicalQueueRuntimeStatistics(
    const u16 queueIndex
){
    if(queueIndex == 1u){
        return Telemetry::FrameGraphPhysicalQueueRuntimeStatistics{
            .graphGeneration = 51u,
            .planGeneration = 52u,
            .recordingAttemptGeneration = 53u,
            .deviceGeneration = 17u,
            .queue = { .index = 1u, .deviceGeneration = 17u },
            .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
            .compile = {
                .taskCount = 50u,
                .packetCount = 49u,
                .mergedTaskCount = 1u,
                .prologueBarrierCount = 11u,
                .epilogueBarrierCount = 12u,
                .ownershipReleaseBarrierCount = 6u,
                .ownershipAcquireBarrierCount = 7u,
                .incomingLogicalOwnershipTransferCount = 30u,
                .outgoingLogicalOwnershipTransferCount = 31u,
                .incomingLogicalOwnershipTransferSignatureCount = 8u,
                .outgoingLogicalOwnershipTransferSignatureCount = 9u,
                .incomingRepeatedOwnershipTransferSignatureCount = 7u,
                .outgoingRepeatedOwnershipTransferSignatureCount = 8u,
                .concurrentSharingAdviceResourceCount = 1u,
            },
            .recording = {
                .packetCount = 20u,
                .taskCount = 21u,
                .commandListCount = 21u,
                .barrierCount = 23u,
                .workerRoutedPacketCount = 19u,
                .parallelPacketCount = 18u,
                .commandListAcquisitionSeconds = 0.005,
                .graphBarrierRecordingSeconds = 0.006,
                .taskRecordSeconds = 0.007,
                .recordingSeconds = 0.008,
            },
            .submission = {
                .acceptedPacketCount = 25u,
                .acceptedTaskCount = 26u,
                .rejectedPacketCount = 24u,
                .rejectedTaskCount = 24u,
                .nativeSubmissionCount = 19u,
                .rejectedSubmissionCount = 23u,
                .nativeCommandListCount = 20u,
                .plannedWaitTokenCount = 20u,
                .sameQueueWaitElisionCount = 5u,
                .timelineWaitCount = 8u,
                .mergedTimelineWaitCount = 7u,
                .acceptedFrontierSubmissionCount = 18u,
                .recoverySubmissionCount = 5u,
                .submissionSeconds = 0.011,
            },
        };
    }

    return Telemetry::FrameGraphPhysicalQueueRuntimeStatistics{
        .graphGeneration = 51u,
        .planGeneration = 52u,
        .recordingAttemptGeneration = 53u,
        .deviceGeneration = 17u,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .compile = {
            .taskCount = 28u,
            .packetCount = 27u,
            .mergedTaskCount = 1u,
            .prologueBarrierCount = 5u,
            .epilogueBarrierCount = 6u,
            .ownershipReleaseBarrierCount = 5u,
            .ownershipAcquireBarrierCount = 5u,
            .incomingLogicalOwnershipTransferCount = 30u,
            .outgoingLogicalOwnershipTransferCount = 29u,
            .incomingLogicalOwnershipTransferSignatureCount = 7u,
            .outgoingLogicalOwnershipTransferSignatureCount = 6u,
            .incomingRepeatedOwnershipTransferSignatureCount = 7u,
            .outgoingRepeatedOwnershipTransferSignatureCount = 6u,
            .concurrentSharingAdviceResourceCount = 1u,
        },
        .recording = {
            .packetCount = 11u,
            .taskCount = 11u,
            .commandListCount = 12u,
            .barrierCount = 11u,
            .workerRoutedPacketCount = 11u,
            .parallelPacketCount = 11u,
            .commandListAcquisitionSeconds = 0.008,
            .graphBarrierRecordingSeconds = 0.008,
            .taskRecordSeconds = 0.008,
            .recordingSeconds = 0.008,
        },
        .submission = {
            .acceptedPacketCount = 12u,
            .acceptedTaskCount = 12u,
            .rejectedPacketCount = 15u,
            .rejectedTaskCount = 16u,
            .nativeSubmissionCount = 11u,
            .rejectedSubmissionCount = 15u,
            .nativeCommandListCount = 12u,
            .plannedWaitTokenCount = 24u,
            .sameQueueWaitElisionCount = 7u,
            .timelineWaitCount = 6u,
            .mergedTimelineWaitCount = 11u,
            .acceptedFrontierSubmissionCount = 10u,
            .recoverySubmissionCount = 3u,
            .submissionSeconds = 0.010,
        },
    };
}

void BuildTestPhysicalQueueRuntimeStatistics(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& records
){
    records = Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords(arena);
    records.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{
        .ownerNodeIndex = 0u,
        .statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u),
    });
    records.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{
        .ownerNodeIndex = 0u,
        .statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u),
    });
}

void BuildTestAssignedFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestFrameGraph(arena, nodes, edges);
    nodes[0u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    nodes[2u].queueAssignment = MakeNotAcceptedFrameGraphQueueAssignment();
}

void BuildTestCompiledFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestAssignedFrameGraph(arena, nodes, edges);
    nodes[0u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    nodes[2u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit
    );
}

void BuildTestRuntimeFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
){
    BuildTestCompiledFrameGraph(arena, nodes, edges);
    nodes[0u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    nodes[2u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    nodes[2u].runtimeStatistics.graphGeneration = 61u;
    nodes[2u].runtimeStatistics.planGeneration = 62u;
    nodes[2u].runtimeStatistics.recordingAttemptGeneration = 63u;
}

void BuildTestPacketSubmissionFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges,
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics
){
    nodes = Telemetry::FrameGraphNodeDescs(arena);
    edges = Telemetry::FrameGraphEdgeDescs(arena);
    physicalQueueRuntimeStatistics = Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords(arena);
    packetSubmissionStatistics = Telemetry::FrameGraphPacketSubmissionStatisticsRecords(arena);

    Telemetry::FrameGraphRuntimeStatistics runtimeStatistics;
    runtimeStatistics.graphGeneration = 71u;
    runtimeStatistics.planGeneration = 72u;
    runtimeStatistics.recordingAttemptGeneration = 73u;
    runtimeStatistics.deviceGeneration = 17u;
    runtimeStatistics.compile.taskCount = 5u;
    runtimeStatistics.compile.packetCount = 3u;
    runtimeStatistics.compile.mergedTaskCount = 2u;
    runtimeStatistics.recording.packetCount = 3u;
    runtimeStatistics.recording.taskCount = 5u;
    runtimeStatistics.recording.commandListCount = 4u;
    runtimeStatistics.submission.acceptedPacketCount = 3u;
    runtimeStatistics.submission.acceptedTaskCount = 5u;
    runtimeStatistics.submission.nativeSubmissionCount = 3u;
    runtimeStatistics.submission.nativeCommandListCount = 4u;
    runtimeStatistics.submission.plannedWaitTokenCount = 6u;
    runtimeStatistics.submission.sameQueueWaitElisionCount = 2u;
    runtimeStatistics.submission.timelineWaitCount = 2u;
    runtimeStatistics.submission.mergedTimelineWaitCount = 2u;
    runtimeStatistics.submission.acceptedFrontierSubmissionCount = 2u;
    runtimeStatistics.submission.recoverySubmissionCount = 1u;
    runtimeStatistics.submission.submissionSeconds = 0.75;
    runtimeStatistics.present = true;

    Telemetry::FrameGraphNodeDesc owner;
    owner.name = Name("packet_submission_pass");
    owner.label = "Packet Submission Pass";
    owner.kind = Telemetry::FrameGraphNodeKind::Pass;
    owner.runtimeStatistics = runtimeStatistics;
    nodes.push_back(owner);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics graphicsStatistics;
    graphicsStatistics.graphGeneration = 71u;
    graphicsStatistics.planGeneration = 72u;
    graphicsStatistics.recordingAttemptGeneration = 73u;
    graphicsStatistics.deviceGeneration = 17u;
    graphicsStatistics.queue = { .index = 1u, .deviceGeneration = 17u };
    graphicsStatistics.queueClass = Telemetry::FrameGraphQueueClass::Graphics;
    graphicsStatistics.compile.taskCount = 4u;
    graphicsStatistics.compile.packetCount = 2u;
    graphicsStatistics.compile.mergedTaskCount = 2u;
    graphicsStatistics.recording.packetCount = 2u;
    graphicsStatistics.recording.taskCount = 4u;
    graphicsStatistics.recording.commandListCount = 2u;
    graphicsStatistics.submission.acceptedPacketCount = 2u;
    graphicsStatistics.submission.acceptedTaskCount = 4u;
    graphicsStatistics.submission.nativeSubmissionCount = 2u;
    graphicsStatistics.submission.nativeCommandListCount = 2u;
    graphicsStatistics.submission.plannedWaitTokenCount = 3u;
    graphicsStatistics.submission.sameQueueWaitElisionCount = 2u;
    graphicsStatistics.submission.timelineWaitCount = 1u;
    graphicsStatistics.submission.acceptedFrontierSubmissionCount = 1u;
    graphicsStatistics.submission.recoverySubmissionCount = 1u;
    graphicsStatistics.submission.submissionSeconds = 0.5;

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics computeStatistics;
    computeStatistics.graphGeneration = 71u;
    computeStatistics.planGeneration = 72u;
    computeStatistics.recordingAttemptGeneration = 73u;
    computeStatistics.deviceGeneration = 17u;
    computeStatistics.queue = { .index = 3u, .deviceGeneration = 17u };
    computeStatistics.queueClass = Telemetry::FrameGraphQueueClass::Compute;
    computeStatistics.compile.taskCount = 1u;
    computeStatistics.compile.packetCount = 1u;
    computeStatistics.recording.packetCount = 1u;
    computeStatistics.recording.taskCount = 1u;
    computeStatistics.recording.commandListCount = 2u;
    computeStatistics.submission.acceptedPacketCount = 1u;
    computeStatistics.submission.acceptedTaskCount = 1u;
    computeStatistics.submission.nativeSubmissionCount = 1u;
    computeStatistics.submission.nativeCommandListCount = 2u;
    computeStatistics.submission.plannedWaitTokenCount = 3u;
    computeStatistics.submission.timelineWaitCount = 1u;
    computeStatistics.submission.mergedTimelineWaitCount = 2u;
    computeStatistics.submission.acceptedFrontierSubmissionCount = 1u;
    computeStatistics.submission.submissionSeconds = 0.25;

    physicalQueueRuntimeStatistics.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{
        .ownerNodeIndex = 0u,
        .statistics = computeStatistics,
    });
    physicalQueueRuntimeStatistics.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{
        .ownerNodeIndex = 0u,
        .statistics = graphicsStatistics,
    });

    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .ownerNodeIndex = 0u,
        .packetIndex = 2u,
        .packetGeneration = 72u,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
        .taskCount = 2u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 1u,
        .sameQueueWaitElisionCount = 1u,
        .joinsAcceptedQueueFrontier = true,
        .recoverySubmission = true,
        .submissionSeconds = 0.375,
    });
    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .ownerNodeIndex = 0u,
        .packetIndex = 0u,
        .packetGeneration = 72u,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
        .taskCount = 2u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 2u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .submissionSeconds = 0.125,
    });
    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .ownerNodeIndex = 0u,
        .packetIndex = 1u,
        .packetGeneration = 72u,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .taskCount = 1u,
        .commandListCount = 2u,
        .plannedWaitTokenCount = 3u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 2u,
        .joinsAcceptedQueueFrontier = true,
        .submissionSeconds = 0.25,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

