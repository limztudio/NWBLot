// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <logger/telemetry/report.h>

#include <core/alloc/scratch.h>
#include <core/telemetry/codec.h>
#include <core/telemetry/diagnostic.h>
#include <core/telemetry/event.h>
#include <core/telemetry/frame_graph.h>
#include <core/telemetry/perf.h>
#include <core/telemetry/recorder.h>
#include <core/telemetry/session.h>
#include <core/telemetry/text_log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

inline constexpr Name s_TelemetryTestScratch = NWB::Tests::TelemetryCases::s_TelemetryScratch;

struct TestArena : NWB::Tests::TestArena<struct TelemetryTestsTag>{
    NWB::Core::Alloc::ScratchArena scratchArena{ s_TelemetryTestScratch };
};
namespace Telemetry = NWB::Core::Telemetry;
namespace Log = NWB::Log;

NWB::Core::Perf::TimingStats MakeTestTimingStats()noexcept;

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName)noexcept;

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(const i64 deallocationCount = 0)noexcept;

Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(const Telemetry::FrameGraphRuntimeStatistics& statistics, const u32 nodeIndex, const u16 reserved)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildTestFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment()noexcept;

Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment()noexcept;

Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(const u64 planGeneration, const u32 packetIndex, const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision)noexcept;

Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics()noexcept;

[[nodiscard]] Telemetry::FrameGraphRuntimeStatistics OwnerStatistics(const u32 packetCount)noexcept;

Telemetry::FrameGraphPhysicalQueueRuntimeStatistics MakeFrameGraphPhysicalQueueRuntimeStatistics(const u16 queueIndex)noexcept;

void BuildTestPhysicalQueueRuntimeStatistics(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& records);

void BuildTestAssignedFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

void BuildTestCompiledFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

void BuildTestRuntimeFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

void BuildTestPacketSubmissionFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges,
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

void BuildTestFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges){
    nodes = Telemetry::FrameGraphNodeDescs(arena);
    edges = Telemetry::FrameGraphEdgeDescs(arena);

    nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name(TestCases::s_Gbuffer), .label = AStringView("GBuffer Pass"), .kind = Telemetry::FrameGraphNodeKind::Pass, .flags = 1u, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = {}, });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name(TestCases::s_Albedo), .label = AStringView("Albedo Texture"), .kind = Telemetry::FrameGraphNodeKind::Resource, .flags = 0u, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = {}, });
    nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name(TestCases::s_Lighting), .label = AStringView("Lighting Pass"), .kind = Telemetry::FrameGraphNodeKind::Pass, .flags = 0u, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = {}, });

    edges.push_back(Telemetry::FrameGraphEdgeDesc{ .fromNodeIndex = 0u, .toNodeIndex = 1u, .kind = Telemetry::FrameGraphEdgeKind::Writes, });
    edges.push_back(Telemetry::FrameGraphEdgeDesc{ .fromNodeIndex = 1u, .toNodeIndex = 2u, .kind = Telemetry::FrameGraphEdgeKind::Reads, .flags = 2u, });
}

Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment()noexcept{
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 1u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 3u, .deviceGeneration = 17u };
    assignment.acceptedQueue = assignment.plannedQueue;
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = { .overlap = 7, .queueLoad = 3, .incomingCrossings = 2, .outgoingCrossings = 1, .ownershipTransfers = 4, .total = -3, };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Compute;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::Scored;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::All;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::Changed;
    assignment.dedicated = true;
    assignment.present = true;
    return assignment;
}

Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment()noexcept{
    Telemetry::FrameGraphQueueAssignment assignment;
    assignment.initialQueue = { .index = 4u, .deviceGeneration = 17u };
    assignment.plannedQueue = { .index = 5u, .deviceGeneration = 17u };
    assignment.previousAcceptedQueue = { .index = 2u, .deviceGeneration = 17u };
    assignment.score = { .overlap = 6, .queueLoad = 1, .incomingCrossings = 2, .outgoingCrossings = 3, .ownershipTransfers = 4, .total = -4, };
    assignment.queueClass = Telemetry::FrameGraphQueueClass::Transfer;
    assignment.reason = Telemetry::FrameGraphQueueAssignmentReason::Scored;
    assignment.modifiers = Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    assignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted;
    assignment.present = true;
    return assignment;
}

Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(const u64 planGeneration, const u32 packetIndex, const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision)noexcept{
    return Telemetry::FrameGraphCompiledTask{ .planGeneration = planGeneration, .packetIndex = packetIndex, .packetizationDecision = packetizationDecision, .present = true, };
}

Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics()noexcept{
    return Telemetry::FrameGraphRuntimeStatistics{
        .graphGeneration = 51u,
        .planGeneration = 52u,
        .recordingAttemptGeneration = 53u,
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
        .deviceGeneration = 17u,
        .present = true,
    };
}

[[nodiscard]] Telemetry::FrameGraphRuntimeStatistics OwnerStatistics(const u32 packetCount)noexcept{
    Telemetry::FrameGraphRuntimeStatistics statistics;
    statistics.graphGeneration = 71u;
    statistics.planGeneration = 72u;
    statistics.recordingAttemptGeneration = 73u;
    statistics.deviceGeneration = 17u;
    statistics.compile.taskCount = packetCount;
    statistics.compile.packetCount = packetCount;
    statistics.recording.packetCount = packetCount;
    statistics.recording.taskCount = packetCount;
    statistics.recording.commandListCount = packetCount;
    statistics.submission.acceptedPacketCount = packetCount;
    statistics.submission.acceptedTaskCount = packetCount;
    statistics.submission.nativeSubmissionCount = packetCount;
    statistics.submission.nativeCommandListCount = packetCount;
    statistics.present = true;
    return statistics;
}

Telemetry::FrameGraphPhysicalQueueRuntimeStatistics MakeFrameGraphPhysicalQueueRuntimeStatistics(const u16 queueIndex)noexcept{
    if(queueIndex == 1u){
        return Telemetry::FrameGraphPhysicalQueueRuntimeStatistics{
            .graphGeneration = 51u,
            .planGeneration = 52u,
            .recordingAttemptGeneration = 53u,
            .queue = { .index = 1u, .deviceGeneration = 17u },
            .deviceGeneration = 17u,
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
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .deviceGeneration = 17u,
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

void BuildTestPhysicalQueueRuntimeStatistics(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& records){
    records = Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords(arena);
    records.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{ .ownerNodeIndex = 0u, .statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u), });
    records.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{ .ownerNodeIndex = 0u, .statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u), });
}

void BuildTestAssignedFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges){
    BuildTestFrameGraph(arena, nodes, edges);
    nodes[0u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    nodes[2u].queueAssignment = MakeNotAcceptedFrameGraphQueueAssignment();
}

void BuildTestCompiledFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges){
    BuildTestAssignedFrameGraph(arena, nodes, edges);
    nodes[0u].compiledTask = MakeFrameGraphCompiledTask(41u, 7u, Telemetry::FrameGraphTaskPacketizationDecision::FirstTask);
    nodes[2u].compiledTask = MakeFrameGraphCompiledTask(41u, 7u, Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit);
}

void BuildTestRuntimeFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges){
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
    owner.name = Name(TestCases::s_TelemetryCasePacketSubmissionPass);
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

    physicalQueueRuntimeStatistics.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{ .ownerNodeIndex = 0u, .statistics = computeStatistics, });
    physicalQueueRuntimeStatistics.push_back(Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord{ .ownerNodeIndex = 0u, .statistics = graphicsStatistics, });

    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .packetGeneration = 72u,
        .taskCount = 2u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 1u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 0u,
        .mergedTimelineWaitCount = 0u,
        .inheritedTimelineWaitElisionCount = 0u,
        .submissionSeconds = 0.375,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .ownerNodeIndex = 0u,
        .packetIndex = 2u,
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
        .joinsAcceptedQueueFrontier = true,
        .recoverySubmission = true,
    });
    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .packetGeneration = 72u,
        .taskCount = 2u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 2u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 0u,
        .inheritedTimelineWaitElisionCount = 0u,
        .submissionSeconds = 0.125,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .ownerNodeIndex = 0u,
        .packetIndex = 0u,
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
    });
    packetSubmissionStatistics.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
        .packetGeneration = 72u,
        .taskCount = 1u,
        .commandListCount = 2u,
        .plannedWaitTokenCount = 3u,
        .sameQueueWaitElisionCount = 0u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 2u,
        .inheritedTimelineWaitElisionCount = 0u,
        .submissionSeconds = 0.25,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .ownerNodeIndex = 0u,
        .packetIndex = 1u,
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .joinsAcceptedQueueFrontier = true,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Core::Perf::TimingStats MakeTestTimingStats()noexcept{
    NWB::Core::Perf::TimingStats stats;
    stats.seconds = 0.125;
    stats.sampleCount = 3u;
    stats.publishFrameIndex = 77u;
    stats.firstSampleFrameIndex = 70u;
    stats.lastSampleFrameIndex = 76u;
    return stats;
}

NWB::Core::Perf::MemorySnapshot MakeTestMemorySnapshot(const Name& scopeName)noexcept{
    NWB::Core::Perf::MemorySnapshot snapshot;
    snapshot.scopeName = scopeName;
    snapshot.frameIndex = 88u;
    snapshot.reservedBytes = 4096u;
    snapshot.usedBytes = 1536u;
    snapshot.peakUsedBytes = 2048u;
    snapshot.allocationCount = 7u;
    snapshot.reallocationCount = 2u;
    snapshot.deallocationCount = 1u;
    return snapshot;
}

NWB::Core::Perf::MemoryDelta MakeTestMemoryDelta(const i64 deallocationCount)noexcept{
    NWB::Core::Perf::MemoryDelta delta;
    delta.previousFrameIndex = 87u;
    delta.currentFrameIndex = 88u;
    delta.reservedBytes = 512;
    delta.usedBytes = -128;
    delta.peakUsedBytes = 256;
    delta.allocationCount = 2;
    delta.reallocationCount = 1;
    delta.deallocationCount = deallocationCount;
    delta.hasSamples = true;
    return delta;
}

Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(const Telemetry::FrameGraphRuntimeStatistics& statistics, const u32 nodeIndex, const u16 reserved)noexcept{
    return Telemetry::EncodedFrameGraphRuntimeStatistics{
        .nodeIndex = nodeIndex,
        .deviceGeneration = statistics.deviceGeneration,
        .reserved = reserved,
        .graphGeneration = statistics.graphGeneration,
        .planGeneration = statistics.planGeneration,
        .recordingAttemptGeneration = statistics.recordingAttemptGeneration,
        .compile = {
            .taskCount = statistics.compile.taskCount,
            .resourceCount = statistics.compile.resourceCount,
            .resourceUseCount = statistics.compile.resourceUseCount,
            .explicitDependencyCount = statistics.compile.explicitDependencyCount,
            .inferredDependencyCount = statistics.compile.inferredDependencyCount,
            .packetCount = statistics.compile.packetCount,
            .packetDependencyCount = statistics.compile.packetDependencyCount,
            .mergedTaskCount = statistics.compile.mergedTaskCount,
            .transitionBarrierCount = statistics.compile.transitionBarrierCount,
            .uavBarrierCount = statistics.compile.uavBarrierCount,
            .ownershipReleaseBarrierCount = statistics.compile.ownershipReleaseBarrierCount,
            .ownershipAcquireBarrierCount = statistics.compile.ownershipAcquireBarrierCount,
            .stateExportBarrierCount = statistics.compile.stateExportBarrierCount,
            .logicalOwnershipTransferCount = statistics.compile.logicalOwnershipTransferCount,
            .logicalOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferSignatureCount,
            .repeatedOwnershipTransferSignatureCount = statistics.compile.repeatedOwnershipTransferSignatureCount,
            .concurrentSharingCouldAvoidTransferCount = statistics.compile.concurrentSharingCouldAvoidTransferCount,
            .concurrentSharingAdviceResourceCount = statistics.compile.concurrentSharingAdviceResourceCount,
            .logicalOwnershipTransferInternalCount = statistics.compile.logicalOwnershipTransferInternalCount,
            .logicalOwnershipTransferExternalImportCount = statistics.compile.logicalOwnershipTransferExternalImportCount,
            .logicalOwnershipTransferExternalExportCount = statistics.compile.logicalOwnershipTransferExternalExportCount,
            .resourceSetCount = statistics.compile.resourceSetCount,
            .resourceSetMemberCount = statistics.compile.resourceSetMemberCount,
            .directResourceUseCount = statistics.compile.directResourceUseCount,
            .declaredResourceSetUseCount = statistics.compile.declaredResourceSetUseCount,
            .expandedResourceSetMemberUseCount = statistics.compile.expandedResourceSetMemberUseCount,
            .payloadObjectCount = statistics.compile.payloadObjectCount,
            .payloadObjectBytes = statistics.compile.payloadObjectBytes,
            .uploadBlobCount = statistics.compile.uploadBlobCount,
            .uploadBlobBytes = statistics.compile.uploadBlobBytes,
            .declarationSeconds = statistics.compile.declarationSeconds,
            .analysisSeconds = statistics.compile.analysisSeconds,
            .validationSeconds = statistics.compile.validationSeconds,
            .dependencyAnalysisSeconds = statistics.compile.dependencyAnalysisSeconds,
            .hazardAnalysisSeconds = statistics.compile.hazardAnalysisSeconds,
            .topologicalOrderSeconds = statistics.compile.topologicalOrderSeconds,
            .queueAssignmentSeconds = statistics.compile.queueAssignmentSeconds,
            .planningSeconds = statistics.compile.planningSeconds,
            .packetizationSeconds = statistics.compile.packetizationSeconds,
            .resourceStatePlanningSeconds = statistics.compile.resourceStatePlanningSeconds,
            .packetDependencyPlanningSeconds = statistics.compile.packetDependencyPlanningSeconds,
            .totalSeconds = statistics.compile.totalSeconds,
            .resourceVersionCount = statistics.compile.resourceVersionCount,
            .resourceVersionEdgeCount = statistics.compile.resourceVersionEdgeCount,
        },
        .recording = {
            .packetCount = statistics.recording.packetCount,
            .taskCount = statistics.recording.taskCount,
            .commandListCount = statistics.recording.commandListCount,
            .barrierCount = statistics.recording.barrierCount,
            .workerRoutedPacketCount = statistics.recording.workerRoutedPacketCount,
            .parallelPacketCount = statistics.recording.parallelPacketCount,
            .commandListAcquisitionSeconds = statistics.recording.commandListAcquisitionSeconds,
            .graphBarrierRecordingSeconds = statistics.recording.graphBarrierRecordingSeconds,
            .taskRecordSeconds = statistics.recording.taskRecordSeconds,
            .recordingSeconds = statistics.recording.recordingSeconds,
            .recordingElapsedSeconds = statistics.recording.recordingElapsedSeconds,
            .readyFrontierElapsedSeconds = statistics.recording.readyFrontierElapsedSeconds,
            .readyFrontierWorkerBusySeconds = statistics.recording.readyFrontierWorkerBusySeconds,
            .readyFrontierWorkerCapacitySeconds = statistics.recording.readyFrontierWorkerCapacitySeconds,
        },
        .submission = {
            .acceptedPacketCount = statistics.submission.acceptedPacketCount,
            .acceptedTaskCount = statistics.submission.acceptedTaskCount,
            .rejectedPacketCount = statistics.submission.rejectedPacketCount,
            .rejectedTaskCount = statistics.submission.rejectedTaskCount,
            .nativeSubmissionCount = statistics.submission.nativeSubmissionCount,
            .rejectedSubmissionCount = statistics.submission.rejectedSubmissionCount,
            .nativeCommandListCount = statistics.submission.nativeCommandListCount,
            .plannedWaitTokenCount = statistics.submission.plannedWaitTokenCount,
            .sameQueueWaitElisionCount = statistics.submission.sameQueueWaitElisionCount,
            .timelineWaitCount = statistics.submission.timelineWaitCount,
            .mergedTimelineWaitCount = statistics.submission.mergedTimelineWaitCount,
            .inheritedTimelineWaitElisionCount = statistics.submission.inheritedTimelineWaitElisionCount,
            .acceptedFrontierSubmissionCount = statistics.submission.acceptedFrontierSubmissionCount,
            .submissionSeconds = statistics.submission.submissionSeconds,
            .recoverySubmissionCount = statistics.submission.recoverySubmissionCount,
        },
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <logger/telemetry/report.h>

#include <core/alloc/scratch.h>
#include <core/common/name_symbols.h>
#include <core/perf/session.h>
#include <core/telemetry/session.h>

#include <global/name_hash_text.h>
#include <global/atomic.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocation_owner_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

constexpr AStringView s_ArenaSourceRecord = "\"source\": \"arena\"";
constexpr AStringView s_ReservedBytesField = "reservedBytes";
constexpr AStringView s_UsedBytesField = "usedBytes";
constexpr AStringView s_PeakUsedBytesField = "peakUsedBytes";
constexpr AStringView s_AllocationCountField = "allocationCount";
constexpr AStringView s_ReallocationCountField = "reallocationCount";
constexpr AStringView s_DeallocationCountField = "deallocationCount";
constexpr AStringView s_HeapBackingSourceRecord = "\"source\": \"heapBacking\"";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<struct AllocationOwnerTelemetryTestsTag>;
namespace Core = NWB::Core;
namespace Perf = Core::Perf;
namespace Telemetry = Core::Telemetry;
namespace Log = NWB::Log;
static Atomic<u64> s_OwnerSequence{ 0u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OwnedAllocation{
    Core::Alloc::GlobalArena& arena;
    void* pointer = nullptr;
    usize requestedBytes = 0u;


public:
    OwnedAllocation(Core::Alloc::GlobalArena& owner, const usize size)noexcept
        : arena(owner)
        , pointer(owner.allocate(1u, size))
        , requestedBytes(size)
    {}
    ~OwnedAllocation()noexcept{
        if(pointer)
            arena.deallocate(pointer, 1u, requestedBytes);
    }
};

[[nodiscard]] static Perf::MemorySnapshot MakeSnapshot(const Name& name, const Perf::MemorySource::Enum source)noexcept{
    Perf::MemorySnapshot snapshot;
    snapshot.scopeName = name;
    snapshot.source = source;
    snapshot.frameIndex = 88u;
    snapshot.reservedBytes = 4096u;
    snapshot.usedBytes = 1536u;
    snapshot.peakUsedBytes = 2048u;
    snapshot.allocationCount = 7u;
    snapshot.reallocationCount = 2u;
    snapshot.deallocationCount = 1u;
    return snapshot;
}

static void ExpectSnapshot(const Perf::MemorySnapshot& actual, const Perf::MemorySnapshot& expected){
    EXPECT_EQ(actual.scopeName, expected.scopeName);
    EXPECT_EQ(actual.source, expected.source);
    EXPECT_EQ(actual.frameIndex, expected.frameIndex);
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

static void ExpectDelta(const Perf::MemoryDelta& actual, const Perf::MemoryDelta& expected){
    EXPECT_EQ(actual.hasSamples, expected.hasSamples);
    EXPECT_EQ(actual.previousFrameIndex, expected.previousFrameIndex);
    EXPECT_EQ(actual.currentFrameIndex, expected.currentFrameIndex);
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}

[[nodiscard]] static const Telemetry::EventRecord* FindOwnerEvent(const Telemetry::EventView& events, const Name& owner, const Perf::MemorySource::Enum source)noexcept{
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        if(!event || event->header.kind != Telemetry::EventKind::MemoryFrame)
            continue;
        const auto parsed = Telemetry::ParsePerfMemoryPayload(event->payload.data(), event->payload.size());
        if(parsed && parsed->scopeName == owner && parsed->snapshot.source == source)
            return event;
    }
    return nullptr;
}

[[nodiscard]] static AStringView FindOwnerJsonRecord(const AStringView json, const Name& owner, const AStringView sourceField){
    Array<char, NameHashTextCodec::s_TextLength + 1u> identityText = {};
    NameHashTextCodec::Format(identityText.data(), sizeof(identityText), owner.hash());
    usize cursor = json.find("\"memoryRecords\": [");
    while(cursor != AStringView::npos && cursor < json.size()){
        const usize nextLine = json.find('\n', cursor);
        const AStringView line = json.substr(cursor, nextLine == AStringView::npos ? json.size() - cursor : nextLine - cursor);
        if(line.find(identityText.data()) != AStringView::npos && line.find(sourceField) != AStringView::npos)
            return line;
        if(line.find("    ]") != AStringView::npos || nextLine == AStringView::npos)
            break;
        cursor = nextLine + 1u;
    }
    return {};
}

template<typename Number>
static void ExpectJsonNumber(Core::Alloc::ScratchArena& scratchArena, const AStringView record, const AStringView field, const Number value){
    AString<Core::Alloc::ScratchArena> expected(scratchArena);
    expected.reserve(field.size() + 32u);
    StringAppendFormat(expected, "\"{}\": {}", field, value);
    const usize position = record.find(AStringView(expected.data(), expected.size()));
    ASSERT_NE(position, AStringView::npos);
    const usize end = position + expected.size();
    EXPECT_TRUE(end == record.size() || record[end] == ',' || record[end] == '}' || record[end] == ' ');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AllocationOwnerTelemetry, RejectsMalformedMemoryPayloadsWithoutInvalidatingPriorValue){
    TestArena testArena;
    const Name owner = NWB::Tests::TelemetryCases::s_TelemetryPayloadValidationOwner;
    const Perf::MemorySnapshot snapshot = MakeSnapshot(owner, Perf::MemorySource::Arena);
    const Perf::MemoryDelta delta = TelemetryTestDetail::MakeTestMemoryDelta(-1);
    Telemetry::TelemetryBytes bytes(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(owner, "Payload Validation Owner", snapshot, delta, bytes));
    Telemetry::EncodedPerfMemoryPayloadHeader validHeader;
    NWB_MEMCPY(&validHeader, sizeof(validHeader), bytes.data(), sizeof(validHeader));
    auto parsed = Telemetry::ParsePerfMemoryPayload(bytes.data(), bytes.size());
    ASSERT_TRUE(parsed);
    for(u32 invalidCase = 0u; invalidCase < 9u; ++invalidCase){
        NWB_MEMCPY(bytes.data(), bytes.size(), &validHeader, sizeof(validHeader));
        parsed = Telemetry::ParsePerfMemoryPayload(bytes.data(), bytes.size());
        ASSERT_TRUE(parsed);
        Telemetry::EncodedPerfMemoryPayloadHeader malformed = validHeader;
        switch(invalidCase){
        case 0u: malformed.version = 0u; break;
        case 1u: malformed.version = 2u; break;
        case 2u: malformed.version = Limit<u16>::s_Max; break;
        case 3u: malformed.source = 3u; break;
        case 4u: malformed.source = Limit<u32>::s_Max; break;
        case 5u: malformed.flags = 0x8000u; break;
        case 6u: malformed.magic = 0u; break;
        case 7u: malformed.scopeHash = {}; break;
        case 8u: ++malformed.scopeNameBytes; break;
        }
        NWB_MEMCPY(bytes.data(), bytes.size(), &malformed, sizeof(malformed));
        EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(bytes.data(), bytes.size()));
        ExpectSnapshot(parsed->snapshot, snapshot);
        EXPECT_EQ(parsed->scopeName, owner);
    }
    for(const usize truncatedSize : { sizeof(validHeader) - 1u, bytes.size() - 1u }){
        NWB_MEMCPY(bytes.data(), bytes.size(), &validHeader, sizeof(validHeader));
        parsed = Telemetry::ParsePerfMemoryPayload(bytes.data(), bytes.size());
        ASSERT_TRUE(parsed);
        EXPECT_FALSE(Telemetry::ParsePerfMemoryPayload(bytes.data(), truncatedSize));
        ExpectSnapshot(parsed->snapshot, snapshot);
        EXPECT_EQ(parsed->scopeName, owner);
    }

    Perf::MemorySnapshot invalidSnapshot = snapshot;
    invalidSnapshot.source = static_cast<Perf::MemorySource::Enum>(255u);
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(owner, "Invalid Source", invalidSnapshot, delta, bytes));
    EXPECT_TRUE(bytes.empty());
}

TEST(AllocationOwnerTelemetry, SeparatesRecorderSourcesAndKeepsPreviousFrameDuringReplacement){
    TestArena testArena;
    Perf::MemoryRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    const Perf::MemoryView view(recorder);
    const Name owner = NWB::Tests::TelemetryCases::s_TelemetryRecorderSourceCollision;
    const Array<Perf::MemorySource::Enum, 3> sources = { Perf::MemorySource::ExplicitScope, Perf::MemorySource::Arena, Perf::MemorySource::HeapBacking };
    Array<Perf::MemoryScopeId, LengthOf(sources)> scopes;
    Array<Perf::MemorySnapshot, LengthOf(sources)> previous;
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        const auto scopeResult = recorder.registerScope(owner, sources[sourceIndex]);
        ASSERT_TRUE(scopeResult);
        scopes[sourceIndex] = *scopeResult;
        ASSERT_TRUE(scopes[sourceIndex].valid());
        for(usize earlier = 0u; earlier < sourceIndex; ++earlier)
            EXPECT_NE(scopes[earlier].index, scopes[sourceIndex].index);
        ArenaMemoryStats stats;
        stats.reservedBytes = 1000u * (sourceIndex + 1u);
        stats.usedBytes = 100u * (sourceIndex + 1u);
        stats.peakUsedBytes = stats.usedBytes;
        stats.allocationCount = sourceIndex + 1u;
        recorder.recordSnapshot(owner, stats, 10u, sources[sourceIndex]);
        previous[sourceIndex] = view.snapshot(owner, sources[sourceIndex]);
        EXPECT_EQ(previous[sourceIndex].source, sources[sourceIndex]);
        EXPECT_EQ(previous[sourceIndex].usedBytes, stats.usedBytes);
        EXPECT_FALSE(view.delta(owner, sources[sourceIndex]).hasSamples);
        stats.usedBytes += 25u;
        stats.peakUsedBytes = stats.usedBytes;
        recorder.recordSnapshot(scopes[sourceIndex], stats, 11u);
        stats.usedBytes += 75u;
        stats.peakUsedBytes = stats.usedBytes;
        ++stats.reallocationCount;
        recorder.recordSnapshot(owner, stats, 11u, sources[sourceIndex]);
        const Perf::MemorySnapshot replacement = view.snapshot(scopes[sourceIndex]);
        EXPECT_EQ(replacement.usedBytes, stats.usedBytes);
        EXPECT_EQ(replacement.source, sources[sourceIndex]);
        ExpectSnapshot(view.snapshot(owner, sources[sourceIndex]), replacement);
        const Perf::MemoryDelta expectedDelta = Perf::Difference(replacement, previous[sourceIndex]);
        ExpectDelta(view.delta(scopes[sourceIndex]), expectedDelta);
        ExpectDelta(view.delta(owner, sources[sourceIndex]), expectedDelta);
        EXPECT_EQ(expectedDelta.previousFrameIndex, 10u);
        EXPECT_EQ(expectedDelta.currentFrameIndex, 11u);
        EXPECT_EQ(expectedDelta.usedBytes, 100);
    }
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        const Perf::MemorySnapshot& snapshot = view.snapshot(owner, sources[sourceIndex]);
        EXPECT_EQ(snapshot.source, sources[sourceIndex]);
        EXPECT_EQ(snapshot.usedBytes, previous[sourceIndex].usedBytes + 100u);
    }
    ExpectSnapshot(view.snapshot(owner), view.snapshot(scopes[0u]));
    ExpectDelta(view.delta(owner), view.delta(scopes[0u]));
}

TEST(AllocationOwnerTelemetry, CapturesAutomaticAllocationOwnersAcrossEnableReallocationAndDestruction){
    TestArena testArena;
    Array<char, 32u> sequenceText = {};
    const AStringView sequence = FormatDecimal(s_OwnerSequence.fetch_add(1u, MemoryOrder::relaxed), sequenceText);
    const auto ownerNameResult = DeriveName(Name(TestCases::s_TelemetryCaseAutomaticOwner), sequence);
    ASSERT_TRUE(ownerNameResult);
    const Name ownerName = *ownerNameResult;
    const auto scratchNameResult = DeriveName(Name(TestCases::s_TelemetryCaseRetiredScratch), sequence);
    ASSERT_TRUE(scratchNameResult);
    const Name scratchName = *scratchNameResult;
    Core::Alloc::GlobalArena owner(ownerName);
    OwnedAllocation allocation(owner, 53u);
    ASSERT_NE(allocation.pointer, nullptr);
    const u64 firstBytes = static_cast<u64>(Core::Alloc::CoreMsize(allocation.pointer));
    EXPECT_GE(firstBytes, allocation.requestedBytes);
    ArenaMemoryStats scratchLiveStats;
    {
        Core::Alloc::ScratchArena temporary(scratchName);
        ASSERT_NE(temporary.allocate(1u, 97u), nullptr);
        scratchLiveStats = temporary.memoryStats();
        EXPECT_GT(scratchLiveStats.usedBytes, 0u);
    }

    Perf::Session perfSession(testArena.arena);
    Telemetry::CaptureSession capture(testArena.arena);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    perfSession.beginFrame(40u);
    perfSession.publishFrame();
    const Telemetry::PerfSessionRecordResult disabled = capture.recordPerfReport(perfSession.report(), 23u);
    EXPECT_TRUE(disabled.ok());
    EXPECT_EQ(disabled.memoryEvents, 0u);
    EXPECT_EQ(capture.eventCount(), 0u);
    EXPECT_FALSE(perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena).valid());

    perfSession.setCaptureOptions(Perf::CaptureOptions{ .enabled = true, .memory = true });
    perfSession.beginFrame(41u);
    ASSERT_TRUE(perfSession.recordMemorySnapshot(ownerName, owner));
    perfSession.publishFrame();
    const Perf::MemorySnapshot first = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    ASSERT_TRUE(first.valid());
    EXPECT_EQ(first.source, Perf::MemorySource::Arena);
    EXPECT_EQ(first.frameIndex, 41u);
    EXPECT_EQ(first.usedBytes, firstBytes);
    EXPECT_EQ(first.reservedBytes, firstBytes);
    EXPECT_EQ(first.peakUsedBytes, firstBytes);
    EXPECT_EQ(first.allocationCount, 1u);
    EXPECT_EQ(first.reallocationCount, 0u);
    EXPECT_EQ(first.deallocationCount, 0u);
    EXPECT_FALSE(perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena).hasSamples);
    const Perf::MemorySnapshot explicitAlias = perfSession.memoryView().snapshot(ownerName);
    ASSERT_TRUE(explicitAlias.valid());
    EXPECT_EQ(explicitAlias.source, Perf::MemorySource::ExplicitScope);
    EXPECT_EQ(explicitAlias.usedBytes, first.usedBytes);
    EXPECT_EQ(explicitAlias.frameIndex, first.frameIndex);
    const Perf::MemorySnapshot scratch = perfSession.memoryView().snapshot(scratchName, Perf::MemorySource::Arena);
    ASSERT_TRUE(scratch.valid());
    EXPECT_EQ(scratch.reservedBytes, 0u);
    EXPECT_EQ(scratch.usedBytes, 0u);
    EXPECT_EQ(scratch.peakUsedBytes, scratchLiveStats.peakUsedBytes);
    EXPECT_EQ(scratch.allocationCount, scratchLiveStats.allocationCount);
    EXPECT_EQ(scratch.reallocationCount, scratchLiveStats.reallocationCount);
    EXPECT_EQ(scratch.deallocationCount, scratchLiveStats.allocationCount);
    capture.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    const Telemetry::PerfSessionRecordResult filtered = capture.recordPerfReport(perfSession.report(), 23u);
    EXPECT_FALSE(filtered.ok());
    EXPECT_EQ(filtered.memoryEvents, 0u);
    EXPECT_EQ(capture.eventCount(), 0u);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Telemetry::PerfSessionRecordResult firstRecorded = capture.recordPerfReport(perfSession.report(), 23u);
    ASSERT_TRUE(firstRecorded.ok());
    EXPECT_GT(firstRecorded.memoryEvents, 0u);
    const Telemetry::EventRecord* const firstEvent = FindOwnerEvent(capture.view(), ownerName, Perf::MemorySource::Arena);
    ASSERT_NE(firstEvent, nullptr);
    EXPECT_NE(FindOwnerEvent(capture.view(), ownerName, Perf::MemorySource::ExplicitScope), nullptr);
    EXPECT_EQ(firstEvent->header.frameIndex, 41u);
    auto parsed = Telemetry::ParsePerfMemoryPayload(firstEvent->payload.data(), firstEvent->payload.size());
    ASSERT_TRUE(parsed);
    ExpectSnapshot(parsed->snapshot, first);
    EXPECT_FALSE(parsed->delta.hasSamples);

    void* const resized = owner.reallocate(allocation.pointer, 1u, 101u);
    ASSERT_NE(resized, nullptr);
    allocation.pointer = resized;
    allocation.requestedBytes = 101u;
    const u64 resizedBytes = static_cast<u64>(Core::Alloc::CoreMsize(resized));
    perfSession.beginFrame(42u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot second = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    EXPECT_EQ(second.usedBytes, resizedBytes);
    EXPECT_EQ(second.reservedBytes, resizedBytes);
    EXPECT_EQ(second.peakUsedBytes, Max(firstBytes, resizedBytes));
    EXPECT_EQ(second.allocationCount, 1u);
    EXPECT_EQ(second.reallocationCount, 1u);
    EXPECT_EQ(second.deallocationCount, 0u);
    ExpectDelta(perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena), Perf::Difference(second, first));

    owner.deallocate(allocation.pointer, 1u, allocation.requestedBytes);
    allocation.pointer = nullptr;
    perfSession.beginFrame(43u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot retired = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    const Perf::MemoryDelta retiredDelta = perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena);
    EXPECT_EQ(retired.usedBytes, 0u);
    EXPECT_EQ(retired.reservedBytes, 0u);
    EXPECT_EQ(retired.peakUsedBytes, second.peakUsedBytes);
    EXPECT_EQ(retired.allocationCount, 1u);
    EXPECT_EQ(retired.reallocationCount, 1u);
    EXPECT_EQ(retired.deallocationCount, 1u);
    ExpectDelta(retiredDelta, Perf::Difference(retired, second));
    EXPECT_EQ(retiredDelta.usedBytes, -static_cast<i64>(resizedBytes));
    capture.clear();
    const Telemetry::PerfSessionRecordResult retiredRecorded = capture.recordPerfReport(perfSession.report(), 23u);
    ASSERT_TRUE(retiredRecorded.ok());
    const Telemetry::EventRecord* const retiredEvent = FindOwnerEvent(capture.view(), ownerName, Perf::MemorySource::Arena);
    ASSERT_NE(retiredEvent, nullptr);
    parsed = Telemetry::ParsePerfMemoryPayload(retiredEvent->payload.data(), retiredEvent->payload.size());
    ASSERT_TRUE(parsed);
    ExpectSnapshot(parsed->snapshot, retired);
    ExpectDelta(parsed->delta, retiredDelta);
    EXPECT_NE(FindOwnerEvent(capture.view(), scratchName, Perf::MemorySource::Arena), nullptr);
    const Name heapName = NWB::Tests::TelemetryCases::s_GlobalArenaMemoryHeapBacking;
    EXPECT_NE(FindOwnerEvent(capture.view(), heapName, Perf::MemorySource::HeapBacking), nullptr);

    Log::TelemetryReport report(testArena.arena);
    Core::Alloc::ScratchArena scratchArena{Name(TestCases::s_TelemetryCaseJsonChecks)};
    auto reportResult1 = Log::BuildTelemetryReport(capture.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult1);
    report = Move(*reportResult1);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView ownerRecord = FindOwnerJsonRecord(json, ownerName, s_ArenaSourceRecord);
    ASSERT_FALSE(ownerRecord.empty());
    EXPECT_NE(ownerRecord.find(ownerName.resolvedText()), AStringView::npos);
    const usize deltaBegin = ownerRecord.find("\"delta\": {");
    ASSERT_NE(deltaBegin, AStringView::npos);
    const AStringView ownerFields = ownerRecord.substr(0u, deltaBegin);
    const AStringView deltaFields = ownerRecord.substr(deltaBegin);
    ExpectJsonNumber(scratchArena, ownerFields, "frameIndex", 43u);
    ExpectJsonNumber(scratchArena, ownerFields, s_ReservedBytesField, retired.reservedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, s_UsedBytesField, retired.usedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, s_PeakUsedBytesField, retired.peakUsedBytes);
    ExpectJsonNumber(scratchArena, ownerFields, s_AllocationCountField, retired.allocationCount);
    ExpectJsonNumber(scratchArena, ownerFields, s_ReallocationCountField, retired.reallocationCount);
    ExpectJsonNumber(scratchArena, ownerFields, s_DeallocationCountField, retired.deallocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, "previousFrameIndex", 42u);
    ExpectJsonNumber(scratchArena, deltaFields, s_ReservedBytesField, retiredDelta.reservedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, s_UsedBytesField, retiredDelta.usedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, s_PeakUsedBytesField, retiredDelta.peakUsedBytes);
    ExpectJsonNumber(scratchArena, deltaFields, s_AllocationCountField, retiredDelta.allocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, s_ReallocationCountField, retiredDelta.reallocationCount);
    ExpectJsonNumber(scratchArena, deltaFields, s_DeallocationCountField, retiredDelta.deallocationCount);
    EXPECT_FALSE(FindOwnerJsonRecord(json, scratchName, "\"source\": \"arena\"").empty());
    EXPECT_FALSE(FindOwnerJsonRecord(json, heapName, "\"source\": \"heapBacking\"").empty());
}

TEST(AllocationOwnerTelemetry, SharedNameSumsLiveUsageAndPreservesLargestIndividualArenaPeak){
    TestArena testArena;
    Array<char, 32u> sequenceText = {};
    const auto ownerNameResult = DeriveName(Name(NWB::Tests::TelemetryCases::s_TelemetryCaseTestsTelemetrySharedArenaPeak), FormatDecimal(s_OwnerSequence.fetch_add(1u, MemoryOrder::relaxed), sequenceText));
    ASSERT_TRUE(ownerNameResult);
    const Name ownerName = *ownerNameResult;
    Perf::Session perfSession(testArena.arena);
    perfSession.setCaptureOptions(Perf::CaptureOptions{ .enabled = true, .memory = true });
    Core::Alloc::GlobalArena firstArena(ownerName);
    OwnedAllocation firstAllocation(firstArena, 53u);
    ASSERT_NE(firstAllocation.pointer, nullptr);
    const u64 firstBytes = static_cast<u64>(Core::Alloc::CoreMsize(firstAllocation.pointer));
    u64 secondBytes = 0u;
    {
        Core::Alloc::GlobalArena secondArena(ownerName);
        OwnedAllocation secondAllocation(secondArena, 101u);
        ASSERT_NE(secondAllocation.pointer, nullptr);
        secondBytes = static_cast<u64>(Core::Alloc::CoreMsize(secondAllocation.pointer));
        perfSession.beginFrame(71u);
        perfSession.publishFrame();
        const Perf::MemorySnapshot both = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
        ASSERT_TRUE(both.valid());
        EXPECT_EQ(both.usedBytes, firstBytes + secondBytes);
        EXPECT_EQ(both.reservedBytes, firstBytes + secondBytes);
        EXPECT_EQ(both.peakUsedBytes, Max(firstBytes, secondBytes));
        EXPECT_LT(both.peakUsedBytes, both.usedBytes);
        EXPECT_EQ(both.allocationCount, 2u);
        EXPECT_EQ(both.deallocationCount, 0u);
    }

    perfSession.beginFrame(72u);
    perfSession.publishFrame();
    const Perf::MemorySnapshot surviving = perfSession.memoryView().snapshot(ownerName, Perf::MemorySource::Arena);
    ASSERT_TRUE(surviving.valid());
    EXPECT_EQ(surviving.usedBytes, firstBytes);
    EXPECT_EQ(surviving.reservedBytes, firstBytes);
    EXPECT_EQ(surviving.peakUsedBytes, Max(firstBytes, secondBytes));
    EXPECT_EQ(surviving.allocationCount, 2u);
    EXPECT_EQ(surviving.deallocationCount, 1u);
    const Perf::MemoryDelta delta = perfSession.memoryView().delta(ownerName, Perf::MemorySource::Arena);
    EXPECT_TRUE(delta.hasSamples);
    EXPECT_EQ(delta.usedBytes, -static_cast<i64>(secondBytes));
    EXPECT_EQ(delta.peakUsedBytes, 0);

    Telemetry::CaptureSession capture(testArena.arena);
    capture.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Telemetry::PerfSessionRecordResult recorded = capture.recordPerfReport(perfSession.report(), 17u);
    ASSERT_TRUE(recorded.ok());
    Log::TelemetryReport report(testArena.arena);
    Core::Alloc::ScratchArena scratchArena{Name(NWB::Tests::TelemetryCases::s_TelemetryCaseTestsTelemetrySharedArenaPeakJsonChecks)};
    auto reportResult2 = Log::BuildTelemetryReport(capture.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult2);
    report = Move(*reportResult2);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView record = FindOwnerJsonRecord(json, ownerName, s_ArenaSourceRecord);
    ASSERT_FALSE(record.empty());
    EXPECT_NE(record.find("\"peakBasis\": \"largestArena\""), AStringView::npos);
    ExpectJsonNumber(scratchArena, record, s_UsedBytesField, firstBytes);
    ExpectJsonNumber(scratchArena, record, s_PeakUsedBytesField, Max(firstBytes, secondBytes));
}

TEST(AllocationOwnerTelemetry, KeepsHeapBackingRecordsWithoutCountingTheirUsageAgain){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Name owner = NWB::Tests::TelemetryCases::s_TelemetrySharedSourceIdentity;
    const Array<Perf::MemorySource::Enum, 3> sources = { Perf::MemorySource::ExplicitScope, Perf::MemorySource::Arena, Perf::MemorySource::HeapBacking };
    for(usize sourceIndex = 0u; sourceIndex < LengthOf(sources); ++sourceIndex){
        Perf::MemorySnapshot snapshot = MakeSnapshot(owner, sources[sourceIndex]);
        // Keep explicit scope counters separate from the aliased arena summary.
        snapshot.usedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 300u : 100u;
        snapshot.peakUsedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 3000u : 1000u;
        Perf::MemoryDelta delta = TelemetryTestDetail::MakeTestMemoryDelta(-1);
        delta.usedBytes = sources[sourceIndex] == Perf::MemorySource::HeapBacking ? 30 : 10;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, owner, "Shared Owner", snapshot, delta, 7u));
    }
    Log::TelemetryReport report(testArena.arena);
    Core::Alloc::ScratchArena scratchArena{Name(TestCases::s_TelemetryCaseHeapJsonChecks)};
    auto reportResult3 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult3);
    report = Move(*reportResult3);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    EXPECT_EQ(report.summary.memoryEventCount, 3u);
    for(const Perf::MemorySource::Enum source : sources){
        const Log::TelemetryMemorySummary& summary = report.summary.memorySources[source];
        EXPECT_EQ(summary.eventCount, 1u);
        EXPECT_EQ(summary.maxUsedBytes, source == Perf::MemorySource::HeapBacking ? 300u : 100u);
        EXPECT_EQ(summary.maxPeakUsedBytes, source == Perf::MemorySource::HeapBacking ? 3000u : 1000u);
        EXPECT_EQ(summary.totalUsedDeltaBytes, source == Perf::MemorySource::HeapBacking ? 30 : 10);
    }
    const AStringView json(report.json.data(), report.json.size());
    EXPECT_NE(json.find("\"memorySources\": {"), AStringView::npos);
    EXPECT_EQ(json.find("\"maxMemoryUsedBytes\":"), AStringView::npos);
    EXPECT_EQ(json.find("\"maxMemoryPeakUsedBytes\":"), AStringView::npos);
    EXPECT_EQ(json.find("\"totalMemoryUsedDeltaBytes\":"), AStringView::npos);
    EXPECT_FALSE(FindOwnerJsonRecord(json, owner, "\"source\": \"explicitScope\"").empty());
    EXPECT_FALSE(FindOwnerJsonRecord(json, owner, "\"source\": \"arena\"").empty());
    const AStringView heapRecord = FindOwnerJsonRecord(json, owner, s_HeapBackingSourceRecord);
    ASSERT_FALSE(heapRecord.empty());
    ExpectJsonNumber(scratchArena, heapRecord, s_UsedBytesField, 300u);
    ExpectJsonNumber(scratchArena, heapRecord, s_PeakUsedBytesField, 3000u);
}

TEST(AllocationOwnerTelemetry, ResolvesLoadedOwnerSymbolsByFullIdentityAndPreservesExplicitText){
    TestArena testArena;
    namespace NameSymbols = Core::Common::NameSymbols;
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    NameHash firstHash{};
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane)
        firstHash.qwords[lane] = 0xBD876543210ACDEFuLL + lane;
    NameHash secondHash = firstHash;
    ++secondHash.qwords[s_NameHashLaneCount - 1u];
    NameHash unknownHash = secondHash;
    ++unknownHash.qwords[s_NameHashLaneCount - 1u];
    const Name firstOwner(firstHash);
    const Name secondOwner(secondHash);
    const Name unknownOwner(unknownHash);
    Array<char, NameHashTextCodec::s_TextLength + 1u> firstHashText = {};
    Array<char, NameHashTextCodec::s_TextLength + 1u> secondHashText = {};
    Array<char, NameHashTextCodec::s_TextLength + 1u> unknownHashText = {};
    NameHashTextCodec::Format(firstHashText.data(), sizeof(firstHashText), firstHash);
    NameHashTextCodec::Format(secondHashText.data(), sizeof(secondHashText), secondHash);
    NameHashTextCodec::Format(unknownHashText.data(), sizeof(unknownHashText), unknownHash);

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    // Exact hash fallback text reproduces an opt/fin producer that has no local symbol sidecar loaded.
    ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, firstOwner, firstHashText.data(), MakeSnapshot(firstOwner, Perf::MemorySource::Arena), TelemetryTestDetail::MakeTestMemoryDelta(-1), 91u));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, secondOwner, secondHashText.data(), MakeSnapshot(secondOwner, Perf::MemorySource::Arena), TelemetryTestDetail::MakeTestMemoryDelta(-1), 92u));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, firstOwner, "Producer Display Name", MakeSnapshot(firstOwner, Perf::MemorySource::ExplicitScope), TelemetryTestDetail::MakeTestMemoryDelta(-1), 93u));
    ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, unknownOwner, unknownHashText.data(), MakeSnapshot(unknownOwner, Perf::MemorySource::HeapBacking), TelemetryTestDetail::MakeTestMemoryDelta(-1), 94u));
    Log::TelemetryReport report(testArena.arena);
    Core::Alloc::ScratchArena scratchArena{Name(NWB::Tests::TelemetryCases::s_TelemetryCaseTestsTelemetryReportWork)};
    auto reportResult4 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult4);
    report = Move(*reportResult4);
    const AStringView beforeJson(report.json.data(), report.json.size());
    const AStringView beforeRecord = FindOwnerJsonRecord(beforeJson, firstOwner, s_ArenaSourceRecord);
    ASSERT_FALSE(beforeRecord.empty());
    EXPECT_EQ(NWB::Tests::CountText(beforeRecord, firstHashText.data()), 2u);

    AString<Core::Alloc::GlobalArena> namesymText(testArena.arena);
    StringAppendFormat(namesymText, "{}\tproducer=runtime\n{}\truntime\tOwners/Loaded First\n{}\truntime\tOwners/Loaded Second\n", NameSymbols::s_FileHeader, firstHashText.data(), secondHashText.data());
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(namesymText.data(), namesymText.size()), scratchArena));
    auto reportResult5 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult5);
    report = Move(*reportResult5);
    EXPECT_EQ(report.summary.parseFailureCount, 0u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView firstRecord = FindOwnerJsonRecord(json, firstOwner, s_ArenaSourceRecord);
    const AStringView secondRecord = FindOwnerJsonRecord(json, secondOwner, s_ArenaSourceRecord);
    const AStringView explicitRecord = FindOwnerJsonRecord(json, firstOwner, "\"source\": \"explicitScope\"");
    const AStringView unknownRecord = FindOwnerJsonRecord(json, unknownOwner, s_HeapBackingSourceRecord);
    ASSERT_FALSE(firstRecord.empty());
    ASSERT_FALSE(secondRecord.empty());
    ASSERT_FALSE(explicitRecord.empty());
    ASSERT_FALSE(unknownRecord.empty());
    EXPECT_NE(firstRecord.find("\"scope\": \"owners/loaded first\""), AStringView::npos);
    EXPECT_NE(secondRecord.find("\"scope\": \"owners/loaded second\""), AStringView::npos);
    EXPECT_EQ(NWB::Tests::CountText(firstRecord, firstHashText.data()), 1u);
    EXPECT_EQ(NWB::Tests::CountText(secondRecord, secondHashText.data()), 1u);
    EXPECT_NE(explicitRecord.find("\"scope\": \"Producer Display Name\""), AStringView::npos);
    EXPECT_EQ(NWB::Tests::CountText(unknownRecord, unknownHashText.data()), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/scope_exit.h>
#include <global/thread.h>

#include <tests/common/capturing_logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ScopeDiagnosticCategory = "scope_diagnostic";
constexpr AStringView s_TelemetryGuardCategory = "telemetry_guard";
constexpr AStringView s_ErrorEvent = "error";



using namespace TelemetryTestDetail;

static u32 s_ExistingDiagnosticCallbackCount = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExistingDiagnosticCallback(const DiagnosticEventRecord&)noexcept{
    ++s_ExistingDiagnosticCallbackCount;
}


TEST(Telemetry, CaptureScopeRestoresLoggerAndStopsDiagnosticsAfterDestruction){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Tests::CapturingLogger previousLogger;
    {
        NWB::Core::Common::LoggerRegistrationGuard previousRegistration(previousLogger);
        {
            Telemetry::CaptureSessionCaptureScope captureScope(session);

            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("scope text"));
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_ScopeDiagnosticCategory, .message = "scope diagnostic", .file = "scope.cpp", .line = 67u, });
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("after scope"));
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_ScopeDiagnosticCategory, .message = "ignored after scope", });

    EXPECT_EQ(previousLogger.messageCount(), 2u);
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("scope text")));
    EXPECT_TRUE(previousLogger.sawMessageContaining(NWB_TEXT("after scope")));
    EXPECT_EQ(session.eventCount(), 2u);
}

TEST(Telemetry, TextCaptureCountsRejectedPayloadsWithoutCountingFilteredEventsAndStillForwards){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    NWB::Tests::CapturingLogger forwardLogger;
    Telemetry::TextLogCaptureLogger capture(recorder, &forwardLogger);
    NWB::Core::Common::LogString message(NWB_TEXT("capture admission"), testArena.arena);
    const auto invalidType = static_cast<NWB::Core::Common::LogType::Enum>(0xffu);
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 0u);
    EXPECT_EQ(recorder.eventCount(), 0u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
    capture.enqueue(message, NWB::Core::Common::LogType::Info);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 1u);

    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    capture.enqueue(message, invalidType);
    EXPECT_EQ(capture.droppedTextLogCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(forwardLogger.messageCount(), 4u);
    EXPECT_TRUE(forwardLogger.sawMessageContaining(NWB_TEXT("capture admission")));
}

TEST(Telemetry, DiagnosticPayloadPreservesBoundedTextAndRejectsCorruptedHeader){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);

    constexpr Array<char, 3> s_Message = { 'd', 0, 'm' };
    const DiagnosticEventRecord source{
        .event = AStringView("error.trailing").substr(0u, 5u),
        .category = "unit_category",
        .expression = "value != nullptr",
        .message = AStringView(s_Message.data(), s_Message.size()),
        .file = "diagnostic_test.cpp",
        .instructionPointer = 0x1234u,
        .line = 77u,
        .terminatesProcess = true,
    };

    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(source, testArena.arena, payload));

    Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(payload.data(), payload.size())));
    EXPECT_EQ(AStringView(parsed->event), source.event);
    EXPECT_EQ(AStringView(parsed->message), source.message);

    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParseDiagnosticPayload(payload.data(), payload.size())));
}

TEST(Telemetry, DiagnosticPayloadPreservesAliasedTextFields){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr AStringView s_Text = "event category expression message file";
    payload.reserve(512u);
    payload.resize(s_Text.size());
    NWB_MEMCPY(payload.data(), payload.size(), s_Text.data(), s_Text.size());
    const AStringView text(reinterpret_cast<const char*>(payload.data()), payload.size());
    const DiagnosticEventRecord record{
        .event = text.substr(0u, 5u),
        .category = text.substr(6u, 8u),
        .expression = text.substr(15u, 10u),
        .message = text.substr(26u, 7u),
        .file = text.substr(34u, 4u),
    };
    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(record, testArena.arena, payload));
    const auto parsed = Telemetry::ParseDiagnosticPayload(payload.data(), payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->event, "event");
    EXPECT_EQ(parsed->category, "category");
    EXPECT_EQ(parsed->expression, "expression");
    EXPECT_EQ(parsed->message, "message");
    EXPECT_EQ(parsed->file, "file");
    const DiagnosticEventRecord roundtrip{
        .event = parsed->event,
        .category = parsed->category,
        .expression = parsed->expression,
        .message = parsed->message,
        .file = parsed->file,
    };
    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(roundtrip, testArena.arena, payload));
    const auto rebuilt = Telemetry::ParseDiagnosticPayload(payload.data(), payload.size());
    ASSERT_TRUE(rebuilt);
    EXPECT_EQ(rebuilt->message, "message");
    EXPECT_EQ(rebuilt->file, "file");
}

TEST(Telemetry, DiagnosticCaptureGuardDoesNotReplaceExistingCallback){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    s_ExistingDiagnosticCallbackCount = 0u;
    SetDiagnosticEventCallback(ExistingDiagnosticCallback);
    {
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        EXPECT_FALSE(guard.installed());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .message = "existing callback should keep ownership", });
    }
    ClearDiagnosticEventCallback(ExistingDiagnosticCallback);

    EXPECT_EQ(s_ExistingDiagnosticCallbackCount, 1u);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

TEST(Telemetry, DiagnosticObserverReusesPreparedStorageAndCountsPoolOverflow){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    const DiagnosticEventRecord source{ .event = s_ErrorEvent, .message = "bounded observer", };
    const auto prepared = testArena.arena.memoryStats();
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
        Telemetry::DiagnosticCaptureGuard guard(recorder);
        ASSERT_TRUE(guard.installed());
        static_assert(noexcept(guard.capture(source)));
        guard.setFrameIndex(iteration);
        guard.setStreamId(9u);
        for(usize eventIndex = 0u; eventIndex < Telemetry::s_DiagnosticCaptureEventCapacity; ++eventIndex)
            guard.capture(source);
        guard.capture(source);
        EXPECT_EQ(recorder.eventCount(), Telemetry::s_DiagnosticCaptureEventCapacity);
        EXPECT_EQ(recorder.droppedDiagnosticCount(), iteration + 1u);
        const auto* const first = recorder.view().eventAt(0u);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(first->header.frameIndex, iteration);
        EXPECT_EQ(first->header.streamId, 9u);
        recorder.clear();
    }
    const auto reused = testArena.arena.memoryStats();
    EXPECT_EQ(reused.allocationCount, prepared.allocationCount);
    EXPECT_EQ(reused.reallocationCount, prepared.reallocationCount);
    EXPECT_EQ(reused.deallocationCount, prepared.deallocationCount);
}

TEST(Telemetry, DiagnosticObserverRejectsOversizeWithoutTruncatingDirectDiagnostics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    AString<Telemetry::TelemetryArena> message(testArena.arena);
    constexpr usize s_MessageCapacity = Telemetry::s_DiagnosticCapturePayloadCapacity - sizeof(Telemetry::EncodedDiagnosticPayloadHeader) - 5u;
    message.resize(s_MessageCapacity + 1u, 'x');
    DiagnosticEventRecord source{ .event = s_ErrorEvent, .message = AStringView(message.data(), s_MessageCapacity), };
    const auto prepared = testArena.arena.memoryStats();
    guard.capture(source);
    ASSERT_EQ(recorder.eventCount(), 1u);
    const auto* const boundedEvent = recorder.view().eventAt(0u);
    ASSERT_NE(boundedEvent, nullptr);
    EXPECT_EQ(boundedEvent->payload.size(), Telemetry::s_DiagnosticCapturePayloadCapacity);
    source.message = AStringView(message);
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
    EXPECT_EQ(recorder.eventCount(), 1u);
    const auto captured = testArena.arena.memoryStats();
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);

    ASSERT_TRUE(Telemetry::RecordDiagnostic(recorder, source));
    const auto* const directEvent = recorder.view().eventAt(1u);
    ASSERT_NE(directEvent, nullptr);
    const auto parsed = Telemetry::ParseDiagnosticPayload(directEvent->payload.data(), directEvent->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(AStringView(parsed->message), AStringView(message));
    EXPECT_EQ(boundedEvent->payload.size(), Telemetry::s_DiagnosticCapturePayloadCapacity);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverCapturesDuringUnwindWithoutAllocating){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    const auto prepared = testArena.arena.memoryStats();
    struct UnwindMarker{};
    b8 caught = false;
    try{
        ScopeExit capture([]()noexcept{
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "unwind", .message = "cleanup failed", });
        });
        throw UnwindMarker{};
    }
    catch(const UnwindMarker&){
        caught = true;
    }
    const auto captured = testArena.arena.memoryStats();
    EXPECT_TRUE(caught);
    EXPECT_EQ(recorder.eventCount(), 1u);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);
    const auto* const event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseDiagnosticPayload(event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->category, "unwind");
}

TEST(Telemetry, DiagnosticObserverPreservesPublishedRecordsAcrossFlagTogglesAndFiltersDisabledCapture){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    const DiagnosticEventRecord source{ .event = s_ErrorEvent, .message = "retained", };
    guard.capture(source);
    ASSERT_EQ(recorder.eventCount(), 1u);
    const auto* const retained = recorder.view().eventAt(0u);
    ASSERT_NE(retained, nullptr);
    const auto prepared = testArena.arena.memoryStats();
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        guard.capture(source);
        EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
        EXPECT_EQ(recorder.view().eventAt(0u), retained);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
        guard.capture(source);
        EXPECT_EQ(recorder.eventCount(), iteration + 2u);
    }
    const auto toggled = testArena.arena.memoryStats();
    EXPECT_EQ(toggled.allocationCount, prepared.allocationCount);
    EXPECT_EQ(toggled.reallocationCount, prepared.reallocationCount);
    EXPECT_EQ(toggled.deallocationCount, prepared.deallocationCount);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 0u);
    EXPECT_LT(testArena.arena.memoryStats().usedBytes, prepared.usedBytes);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverDoesNotGrowPublicationStorageWhenOrdinaryEventsFillIt){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    for(usize eventIndex = 0u; eventIndex < Telemetry::s_DiagnosticCaptureEventCapacity; ++eventIndex)
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 0u, nullptr, 0u));
    const auto filled = testArena.arena.memoryStats();
    const DiagnosticEventRecord source{ .event = s_ErrorEvent, .message = "publication full", };
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), 1u);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, filled.allocationCount);
    EXPECT_EQ(testArena.arena.memoryStats().reallocationCount, filled.reallocationCount);
    recorder.clear();
    guard.capture(source);
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, DiagnosticObserverPreservesOrderingAndPayloadLifetimeDuringBuilderReentry){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    b8 captureDidNotAllocate = false;
    ASSERT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 7u, 3u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
        const auto before = testArena.arena.memoryStats();
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_ErrorEvent, .message = "builder callback", });
        const auto after = testArena.arena.memoryStats();
        captureDidNotAllocate = before.allocationCount == after.allocationCount && before.reallocationCount == after.reallocationCount;
        payload.push_back(17u);
        return recorder.recordBinary(Telemetry::EventKind::MemoryFrame, 8u, nullptr, 0u);
    }));
    ASSERT_TRUE(captureDidNotAllocate);
    ASSERT_EQ(recorder.eventCount(), 3u);
    const auto events = recorder.view();
    EXPECT_EQ(events.eventAt(0u)->header.kind, Telemetry::EventKind::Diagnostic);
    EXPECT_EQ(events.eventAt(1u)->header.kind, Telemetry::EventKind::MemoryFrame);
    EXPECT_EQ(events.eventAt(2u)->header.kind, Telemetry::EventKind::PerfFrame);
    const auto* const diagnostic = events.eventAt(0u);
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 9u, nullptr, 0u));
    EXPECT_EQ(events.eventAt(0u), diagnostic);
    const auto parsed = Telemetry::ParseDiagnosticPayload(diagnostic->payload.data(), diagnostic->payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->message, "builder callback");
}

TEST(Telemetry, ConcurrentDiagnosticObserversAccountForEveryAdmittedAttempt){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    Telemetry::DiagnosticCaptureGuard guard(recorder);
    ASSERT_TRUE(guard.installed());
    constexpr usize s_AttemptsPerThread = 256u;
    const DiagnosticEventRecord source{ .event = s_ErrorEvent, .message = "concurrent bounded observer", };
    AtomicFlag start;
    const auto capture = [&]()noexcept{
        while(!start.test(MemoryOrder::acquire))
            start.wait(false, MemoryOrder::acquire);
        for(usize attempt = 0u; attempt < s_AttemptsPerThread; ++attempt)
            guard.capture(source);
    };
    const auto prepared = testArena.arena.memoryStats();
    Thread first(capture);
    Thread second(capture);
    start.testAndSet(MemoryOrder::release);
    start.notifyAll();
    first.join();
    second.join();
    const auto captured = testArena.arena.memoryStats();
    EXPECT_GT(recorder.eventCount(), 0u);
    EXPECT_LE(recorder.eventCount(), Telemetry::s_DiagnosticCaptureEventCapacity);
    EXPECT_EQ(recorder.eventCount() + recorder.droppedDiagnosticCount(), s_AttemptsPerThread * 2u);
    EXPECT_EQ(captured.allocationCount, prepared.allocationCount);
    EXPECT_EQ(captured.reallocationCount, prepared.reallocationCount);
    const u64 dropped = recorder.droppedDiagnosticCount();
    recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
    guard.capture(source);
    EXPECT_EQ(recorder.droppedDiagnosticCount(), dropped);
    EXPECT_EQ(recorder.eventCount(), 0u);
}

TEST(Telemetry, DiagnosticCaptureGuardConcurrentLifetimeStress){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    AtomicFlag captureThreadStarted;
    AtomicFlag stopCaptureThread;
    Thread captureThread([&captureThreadStarted, &stopCaptureThread]()noexcept{
        captureThreadStarted.testAndSet(MemoryOrder::release);
        captureThreadStarted.notifyAll();
        while(!stopCaptureThread.test(MemoryOrder::acquire)){
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_TelemetryGuardCategory, .message = "capture during destruction", });
            YieldThread();
        }
    });

    while(!captureThreadStarted.test(MemoryOrder::acquire))
        captureThreadStarted.wait(false, MemoryOrder::acquire);

    constexpr u32 s_IterationCount = 64u;
    b8 allGuardsInstalled = true;
    b8 allIterationsCaptured = true;
    b8 dropsMonotonic = true;
    usize observedEventCount = 0u;
    u64 previousDroppedCount = 0u;
    for(u32 iteration = 0u; iteration < s_IterationCount; ++iteration){
        recorder.clear();
        {
            Telemetry::DiagnosticCaptureGuard guard(recorder);
            if(!guard.installed()){
                allGuardsInstalled = false;
                break;
            }
            constexpr i32 s_CaptureDeadlineMS = 5000;
            const auto deadline = TimerAddMS(TimerNow(), s_CaptureDeadlineMS);
            while(recorder.eventCount() == 0u && TimerNow() < deadline)
                YieldThread();
        }
        const usize captured = recorder.eventCount();
        if(captured == 0u){
            allIterationsCaptured = false;
            break;
        }
        observedEventCount += captured;
        const u64 dropped = recorder.droppedDiagnosticCount();
        dropsMonotonic = dropsMonotonic && dropped >= previousDroppedCount;
        previousDroppedCount = dropped;
    }

    stopCaptureThread.testAndSet(MemoryOrder::release);
    captureThread.join();
    EXPECT_TRUE(allGuardsInstalled);
    EXPECT_TRUE(allIterationsCaptured);
    EXPECT_TRUE(dropsMonotonic);
    EXPECT_GE(observedEventCount, s_IterationCount);

    {
        Telemetry::DiagnosticCaptureGuard finalGuard(recorder);
        EXPECT_TRUE(finalGuard.installed());
    }

    const usize eventCountAfterFinalDestruction = recorder.eventCount();
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_TelemetryGuardCategory, .message = "ignored after destruction", });
    EXPECT_EQ(recorder.eventCount(), eventCountAfterFinalDestruction);

    const Telemetry::EventView events = recorder.view();
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        ASSERT_NE(event, nullptr);

        Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(event->payload.data(), event->payload.size())));
        EXPECT_EQ(parsed->event, DiagnosticEventName::s_Error);
        EXPECT_EQ(parsed->category, "telemetry_guard");
        EXPECT_EQ(parsed->message, "capture during destruction");
    }
}

TEST(Telemetry, CaptureScopePreservesForwardedLogsAndMetadataUnderConcurrentChurn){
    constexpr u32 s_WriterCount = 4u;
    constexpr u32 s_MessagesPerWriter = 384u;
    constexpr u32 s_ExpectedForwardCount = s_WriterCount * s_MessagesPerWriter;
    constexpr u32 s_BaseStreamId = 70u;

    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(1u);
    session.setStreamId(s_BaseStreamId + 1u);

    NWB::Tests::CapturingLogger forwardLogger;
    NWB::Core::Common::LoggerRegistrationGuard baseRegistration(forwardLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

    Latch startGate(s_WriterCount + 1u);
    Atomic<u32> activeWriters{ s_WriterCount };
    Array<Thread, s_WriterCount> writers;
    for(u32 writerIndex = 0u; writerIndex < s_WriterCount; ++writerIndex){
        writers[writerIndex] = Thread([&startGate, &activeWriters](){
            startGate.arrive_and_wait();
            for(u32 messageIndex = 0u; messageIndex < s_MessagesPerWriter; ++messageIndex){
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("concurrent capture scope text {}"), messageIndex);
                if((messageIndex & 7u) == 0u)
                    YieldThread();
            }
            activeWriters.fetch_sub(1u, MemoryOrder::release);
        });
    }

    startGate.arrive_and_wait();
    u64 nextFrameIndex = 1u;
    while(activeWriters.load(MemoryOrder::acquire) != 0u){
        session.setFrameIndex(nextFrameIndex);
        session.setStreamId(s_BaseStreamId + static_cast<u32>(nextFrameIndex & 7u));
        {
            Telemetry::CaptureSessionCaptureScope scope(session);
            ++nextFrameIndex;
            session.setFrameIndex(nextFrameIndex);
            session.setStreamId(s_BaseStreamId + static_cast<u32>(nextFrameIndex & 7u));
            YieldThread();
        }
        ++nextFrameIndex;
    }

    for(Thread& writer : writers)
        writer.join();

    EXPECT_EQ(forwardLogger.messageCount(), s_ExpectedForwardCount);
    EXPECT_GT(session.eventCount(), 0u);

    const Telemetry::EventView events = session.view();
    for(usize eventIndex = 0u; eventIndex < events.eventCount(); ++eventIndex){
        const Telemetry::EventRecord* const event = events.eventAt(eventIndex);
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->header.kind, Telemetry::EventKind::TextLog);
        EXPECT_GE(event->header.frameIndex, 1u);
        EXPECT_LE(event->header.frameIndex, nextFrameIndex);
        EXPECT_GE(event->header.streamId, s_BaseStreamId);
        EXPECT_LE(event->header.streamId, s_BaseStreamId + 7u);

        const auto parsed = Telemetry::ParseTextLogPayload(event->payload.data(), event->payload.size());
        ASSERT_TRUE(parsed);
        EXPECT_EQ(parsed->type, NWB::Core::Common::LogType::EssentialInfo);
        EXPECT_NE(parsed->messageUtf8.find("concurrent capture scope text"), AStringView::npos);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/perf/cpu_task_profile.h>
#include <core/perf/session.h>
#include <core/task/cpu/scheduler.h>
#include <core/telemetry/session.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_profile_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using TestArena = NWB::Tests::TestArena<struct CpuTaskProfileIntegrationTag>;


class OwnerThreadTimingSink final : public Perf::TimingSink{
public:
    OwnerThreadTimingSink(CpuTaskScheduler& scheduler, Perf::TimingRecorder& recorder, const b8 injectTasks = false)noexcept
        : m_scheduler(scheduler)
        , m_recorder(recorder)
        , m_owner(QueryCurrentThreadId())
        , m_injectTasks(injectTasks)
    {}


public:
    [[nodiscard]] virtual b8 enabled()const noexcept override{ return m_recorder.enabled(); }

    [[nodiscard]] virtual Expected<Perf::TimingScopeId> registerScope(const Name& name)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        return m_recorder.registerScope(name);
    }

    virtual void recordSample(const Perf::TimingScopeId scope, const f64 seconds, const u64 frameIndex)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        ++recordedSamples;
        m_recorder.recordSample(scope, seconds, frameIndex);
        if(m_injectTasks && m_injectionBudget != 0u){
            --m_injectionBudget;
            const auto taskAdmission = m_scheduler.submit([]()noexcept{});
            ASSERT_TRUE(taskAdmission);
            const CpuTaskHandle task = *taskAdmission;
            ASSERT_TRUE(task.valid());
            m_scheduler.wait(task);
        }
    }

    virtual void publishFrame(const u64 frameIndex)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        m_recorder.publishFrame(frameIndex);
    }


public:
    usize recordedSamples = 0u;


private:
    CpuTaskScheduler& m_scheduler;
    Perf::TimingRecorder& m_recorder;
    ThreadId m_owner;
    b8 m_injectTasks;
    usize m_injectionBudget = 64u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskProfileIntegration, DelayedTaskTimingPreservesExecutionFrameAtLaterPublication){
    using namespace __hidden_cpu_task_profile_tests;
    TestArena arena;
    Perf::Session perf(arena.arena);
    perf.setCaptureOptions({ .enabled = true, .cpuTiming = true });
    perf.beginFrame(42u);
    CpuTaskScheduler scheduler(0u);
    const Name taskName = NWB::Tests::TelemetryCases::s_CpuTaskTestsExplicit;
    const Name scopeName = NWB::Tests::TelemetryCases::s_CpuTaskTestsScope;
    const CpuTaskProfileLabel taskLabel = scheduler.registerProfileLabel(taskName);
    const CpuTaskProfileLabel scopeLabel = scheduler.registerProfileLabel(scopeName);
    ASSERT_TRUE(taskLabel.valid());
    ASSERT_TRUE(scopeLabel.valid());
    CpuTaskScope scope(scheduler, scopeLabel);
    scheduler.setProfiling(true, 41u);

    const auto taskAdmission = scope.submit([]()noexcept{}, { .profileLabel = taskLabel });
    ASSERT_TRUE(taskAdmission);
    const CpuTaskHandle task = *taskAdmission;
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
    scope.wait();
    ASSERT_GT(scheduler.statistics().profilePendingEvents, 0u);
    Perf::CollectCpuTaskProfile(scheduler, perf.cpuTimingSink());
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 0u);
    perf.publishFrame();

    const Perf::TimingView timing = perf.cpuTimingView();
    EXPECT_EQ(timing.stats(taskName).firstSampleFrameIndex, 41u);
    EXPECT_EQ(timing.stats(taskName).lastSampleFrameIndex, 41u);
    EXPECT_EQ(timing.stats(taskName).publishFrameIndex, 42u);

    Telemetry::CaptureSession telemetry(arena.arena);
    telemetry.setCaptureOptions(Telemetry::CaptureOptions::All());
    const Telemetry::PerfSessionRecordResult result = telemetry.recordPerfReport(perf.report());
    ASSERT_TRUE(result.ok());
    b8 foundNamedTask = false;
    const Telemetry::EventView events = telemetry.view();
    for(usize index = 0u; index < events.eventCount(); ++index){
        const Telemetry::EventRecord* event = events.eventAt(index);
        ASSERT_NE(event, nullptr);
        Expected<Telemetry::PerfTimingPayload> payload = MakeUnexpected(Failure{});
        ASSERT_TRUE((payload = Telemetry::ParsePerfTimingPayload(event->payload.data(), event->payload.size())));
        if(payload->scopeName == taskName){
            foundNamedTask = true;
            EXPECT_EQ(payload->stats.firstSampleFrameIndex, 41u);
            EXPECT_EQ(payload->stats.publishFrameIndex, 42u);
        }
    }
    EXPECT_TRUE(foundNamedTask);
}

TEST(CpuTaskProfileIntegration, DisabledTimingSinkDiscardsBufferedSamples){
    using namespace __hidden_cpu_task_profile_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    CpuTaskScheduler scheduler(0u);
    scheduler.setProfiling(true, 7u);
    const auto cpuTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    scheduler.wait();
    ASSERT_GT(scheduler.statistics().profilePendingEvents, 0u);
    Perf::CollectCpuTaskProfile(scheduler, timing);
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 0u);
    EXPECT_EQ(timing.scopeCount(), 0u);
    timing.setEnabled(true);
    Perf::CollectCpuTaskProfile(scheduler, timing);
    timing.publishFrame(8u);
    EXPECT_EQ(timing.scopeCount(), 0u);
}

TEST(CpuTaskProfileIntegration, CollectorConsumesOnlyItsInitialPendingSnapshot){
    using namespace __hidden_cpu_task_profile_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    timing.setEnabled(true);
    CpuTaskScheduler scheduler(0u);
    scheduler.setProfiling(true, 11u);
    const auto cpuTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    scheduler.wait();
    const usize pendingBefore = scheduler.statistics().profilePendingEvents;
    ASSERT_GT(pendingBefore, 0u);
    OwnerThreadTimingSink sink(scheduler, timing, true);
    Perf::CollectCpuTaskProfile(scheduler, sink);
    EXPECT_EQ(sink.recordedSamples, pendingBefore);
    EXPECT_GT(scheduler.statistics().profilePendingEvents, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskProfileIntegration, NativeWorkerSamplesAreRecordedOnTheCollectorThread){
    using namespace __hidden_cpu_task_profile_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    timing.setEnabled(true);
    const ThreadId owner = QueryCurrentThreadId();
    ThreadId executionThread;
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    const Name taskName = NWB::Tests::TelemetryCases::s_CpuTaskTestsWorker;
    const CpuTaskProfileLabel label = scheduler.registerProfileLabel(taskName);
    ASSERT_TRUE(label.valid());
    scheduler.setProfiling(true, 19u);
    const auto taskAdmission = scheduler.submit([&]()noexcept{ executionThread = QueryCurrentThreadId(); }, { .profileLabel = label });
    ASSERT_TRUE(taskAdmission);
    const CpuTaskHandle task = *taskAdmission;
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
    EXPECT_NE(executionThread, owner);
    OwnerThreadTimingSink sink(scheduler, timing);
    Perf::CollectCpuTaskProfile(scheduler, sink);
    timing.publishFrame(20u);
    EXPECT_GT(sink.recordedSamples, 0u);
    EXPECT_EQ(timing.stats(taskName).sampleCount, 1u);
    EXPECT_EQ(timing.stats(taskName).firstSampleFrameIndex, 19u);
    EXPECT_EQ(timing.stats(taskName).publishFrameIndex, 20u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, FrameGraphPayloadPreservesBorrowedLabelsWhenRebuiltInPlace){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(905u, nodes, edges, testArena.scratchArena, payload));
    const auto first = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size());
    ASSERT_TRUE(first);
    nodes[0u].label = first->nodes[0u].label;
    nodes[1u].label = first->nodes[1u].label;
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(906u, nodes, edges, testArena.scratchArena, payload));
    const auto rebuilt = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size());
    ASSERT_TRUE(rebuilt);
    EXPECT_EQ(rebuilt->frameIndex, 906u);
    EXPECT_EQ(rebuilt->nodes[0u].label, "GBuffer Pass");
    EXPECT_EQ(rebuilt->nodes[1u].label, "Albedo Texture");
    nodes[0u].label = rebuilt->nodes[0u].label;
    nodes[1u].label = rebuilt->nodes[1u].label;
    nodes[0u].kind = Telemetry::FrameGraphNodeKind::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(907u, nodes, edges, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, FrameGraphPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(905u, nodes, edges, testArena.scratchArena, payload));

    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
}

TEST(Telemetry, FrameGraphPayloadRejectsNonCurrentVersions){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(907u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ASSERT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);
    ASSERT_GT(header.nodeCount, 0u);
    ASSERT_GT(header.runtimeStatisticsCount, 0u);
    ASSERT_GT(header.physicalQueueRuntimeStatisticsCount, 0u);
    ASSERT_GT(header.packetSubmissionStatisticsCount, 0u);

    const Array<u16, 12> unsupportedVersions = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, static_cast<u16>(Telemetry::s_FrameGraphPayloadVersion + 1u), Limit<u16>::s_Max, };
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = Telemetry::s_FrameGraphPayloadVersion;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
        ASSERT_FALSE(parsed->nodes.empty());
        ASSERT_FALSE(parsed->physicalQueueRuntimeStatistics.empty());
        ASSERT_FALSE(parsed->packetSubmissionStatistics.empty());

        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    }
}

TEST(Telemetry, FrameGraphPayloadRejectsHistoricalEmptyPayloads){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(907u, nodes, edges, testArena.scratchArena, payload));
    ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader));

    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    EXPECT_EQ(parsed->frameIndex, 907u);

    const Array<usize, 9> historicalHeaderBytes = { 28u, 32u, 36u, 40u, 44u, 44u, 48u, 52u, 52u };
    for(usize versionIndex = 0u; versionIndex < LengthOf(historicalHeaderBytes); ++versionIndex){
        header.version = static_cast<u16>(versionIndex + 1u);
        SCOPED_TRACE(header.version);
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), historicalHeaderBytes[versionIndex])));
    }
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    const usize assignmentOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader) + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size() + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size();
    Telemetry::EncodedFrameGraphQueueAssignment first;
    Telemetry::EncodedFrameGraphQueueAssignment second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(&second, sizeof(second), payload.data() + assignmentOffset + sizeof(first), sizeof(second));

    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(payload.data() + assignmentOffset + sizeof(first), payload.size() - assignmentOffset - sizeof(first), &second, sizeof(second));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(&second, sizeof(second), payload.data() + assignmentOffset + sizeof(first), sizeof(second));
    first.nodeIndex = 2u;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    NWB_MEMCPY(payload.data() + assignmentOffset + sizeof(first), payload.size() - assignmentOffset - sizeof(first), &second, sizeof(second));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.modifiers = static_cast<u8>(1u << 7u);
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    ++first.scoreTotal;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.acceptedQueue.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(908u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.previousAcceptedQueue.index = Limit<u16>::s_Max;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    const usize compiledTaskOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
    ;
    Telemetry::EncodedFrameGraphCompiledTask first;
    Telemetry::EncodedFrameGraphCompiledTask second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    NWB_MEMCPY(&second, sizeof(second), payload.data() + compiledTaskOffset + sizeof(first), sizeof(second));

    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(payload.data() + compiledTaskOffset + sizeof(first), payload.size() - compiledTaskOffset - sizeof(first), &second, sizeof(second));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetIndex = Limit<u32>::s_Max;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::Unknown;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::Count;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(909u, nodes, edges, testArena.scratchArena, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.reserved[0u] = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simd/vector.h>

#include <gtest/gtest.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_packet_submission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, PacketSubmissionStatisticsRejectInconsistentCountsAndNonfiniteTime){
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
        .packetGeneration = 72u,
        .taskCount = 2u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 4u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 1u,
        .inheritedTimelineWaitElisionCount = 1u,
        .submissionSeconds = 0.125,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .ownerNodeIndex = 0u,
        .packetIndex = 1u,
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .joinsAcceptedQueueFrontier = true,
        .recoverySubmission = true,
    };
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));

    statistics.joinsAcceptedQueueFrontier = false;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    statistics.joinsAcceptedQueueFrontier = true;
    ++statistics.timelineWaitCount;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    --statistics.timelineWaitCount;
    ++statistics.inheritedTimelineWaitElisionCount;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    statistics.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    statistics.inheritedTimelineWaitElisionCount = 1u;
    statistics.commandListCount = 0u;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    statistics.commandListCount = 1u;
    statistics.submissionSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
}

TEST(Telemetry, InheritedWaitStatisticsRejectMalformedCountsAndMismatchedTotals){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    ++nodes[0u].runtimeStatistics.submission.plannedWaitTokenCount;
    ++nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.plannedWaitTokenCount;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.inheritedTimelineWaitElisionCount;
    ++packetSubmissionStatistics[2u].plannedWaitTokenCount;
    ++packetSubmissionStatistics[2u].inheritedTimelineWaitElisionCount;

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    EXPECT_EQ(parsed->nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount, 1u);
    EXPECT_EQ(parsed->physicalQueueRuntimeStatistics[1u].statistics.submission.inheritedTimelineWaitElisionCount, 1u);
    EXPECT_EQ(parsed->packetSubmissionStatistics[1u].inheritedTimelineWaitElisionCount, 1u);

    const usize computePacketOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics) * 2u
        + sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics)
    ;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics encoded;
    NWB_MEMCPY(&encoded, sizeof(encoded), payload.data() + computePacketOffset, sizeof(encoded));
    encoded.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    NWB_MEMCPY(payload.data() + computePacketOffset, payload.size() - computePacketOffset, &encoded, sizeof(encoded));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount = 0u;
    ++nodes[0u].runtimeStatistics.submission.timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphRuntimeStatistics(nodes[0u].runtimeStatistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());

    nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount = 1u;
    --nodes[0u].runtimeStatistics.submission.timelineWaitCount;
    physicalQueueRuntimeStatistics[0u].statistics.submission.inheritedTimelineWaitElisionCount = 0u;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(physicalQueueRuntimeStatistics[0u].statistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());

    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics[2u].inheritedTimelineWaitElisionCount = 0u;
    ++packetSubmissionStatistics[2u].timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(packetSubmissionStatistics[2u]));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, PacketSubmissionEncodingIsIndependentOfInputOrder){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord firstPhysicalQueue = physicalQueueRuntimeStatistics[0u];
    physicalQueueRuntimeStatistics[0u] = physicalQueueRuntimeStatistics[1u];
    physicalQueueRuntimeStatistics[1u] = firstPhysicalQueue;
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetZero = packetSubmissionStatistics[1u];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetOne = packetSubmissionStatistics[2u];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetTwo = packetSubmissionStatistics[0u];
    packetSubmissionStatistics[0u] = packetZero;
    packetSubmissionStatistics[1u] = packetOne;
    packetSubmissionStatistics[2u] = packetTwo;

    Telemetry::TelemetryBytes reorderedPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, reorderedPayload));
    ASSERT_EQ(payload.size(), reorderedPayload.size());
    EXPECT_EQ(NWB_MEMCMP(payload.data(), reorderedPayload.data(), payload.size()), 0);
}

TEST(Telemetry, FrameGraphPacketSubmissionStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);

    packetSubmissionStatistics[0u].joinsAcceptedQueueFrontier = false;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    packetSubmissionStatistics[1u].packetGeneration = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    packetSubmissionStatistics[1u].packetIndex = packetSubmissionStatistics[2u].packetIndex;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    packetSubmissionStatistics[2u].commandListCount = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    packetSubmissionStatistics[1u].submissionSeconds += 0.01;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    packetSubmissionStatistics[1u].queue = { .index = 3u, .deviceGeneration = 17u };
    packetSubmissionStatistics[1u].queueClass = Telemetry::FrameGraphQueueClass::Compute;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));

    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    const usize packetSubmissionStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics) * 2u
    ;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics encodedStatistics;
    NWB_MEMCPY(&encodedStatistics, sizeof(encodedStatistics), payload.data() + packetSubmissionStatisticsOffset, sizeof(encodedStatistics));
    encodedStatistics.reserved = 1u;
    NWB_MEMCPY(payload.data() + packetSubmissionStatisticsOffset, payload.size() - packetSubmissionStatisticsOffset, &encodedStatistics, sizeof(encodedStatistics));
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    NWB_MEMCPY(&encodedStatistics, sizeof(encodedStatistics), payload.data() + packetSubmissionStatisticsOffset, sizeof(encodedStatistics));
    encodedStatistics.submissionSeconds += 0.01;
    NWB_MEMCPY(payload.data() + packetSubmissionStatisticsOffset, payload.size() - packetSubmissionStatisticsOffset, &encodedStatistics, sizeof(encodedStatistics));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    const usize secondPacketSubmissionStatisticsOffset = packetSubmissionStatisticsOffset + sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics);
    NWB_MEMCPY(&encodedStatistics, sizeof(encodedStatistics), payload.data() + secondPacketSubmissionStatisticsOffset, sizeof(encodedStatistics));
    encodedStatistics.packetIndex = 0u;
    NWB_MEMCPY(payload.data() + secondPacketSubmissionStatisticsOffset, payload.size() - secondPacketSubmissionStatisticsOffset, &encodedStatistics, sizeof(encodedStatistics));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ++header.packetSubmissionStatisticsCount;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.packetSubmissionStatisticsPresent = 0u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.reservedTail[1u] = 1u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(920u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, testArena.scratchArena, payload));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/not_null.h>

#include <gtest/gtest.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_physical_queue_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

using EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation = void(*)(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics&);

static constexpr Array<NotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>, 22> s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations = {
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.reserved[6u] = 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.ownerNodeIndex = 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.queue.deviceGeneration = 18u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.queueClass = Telemetry::FrameGraphQueueClass::Unknown;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.recordingSeconds = -1.0;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        --statistics.submission.timelineWaitCount;
        ++statistics.submission.inheritedTimelineWaitElisionCount;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission = {};
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.inheritedTimelineWaitElisionCount = 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.recoverySubmissionCount = statistics.submission.acceptedFrontierSubmissionCount + 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.taskCount = 79u;
        statistics.compile.packetCount = 78u;
        statistics.compile.mergedTaskCount = 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.taskCount = 51u;
        statistics.compile.packetCount = 50u;
        statistics.compile.mergedTaskCount = 1u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.prologueBarrierCount = 0u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.barrierCount = 24u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = 22u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = 23u;
        statistics.submission.rejectedTaskCount = 24u;
        statistics.submission.rejectedSubmissionCount = 23u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.incomingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.incomingRepeatedOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording = {};
        statistics.recording.commandListCount = 1u;
        statistics.submission = {};
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeSubmissionCount = 0u;
        statistics.submission.nativeCommandListCount = 0u;
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.sameQueueWaitElisionCount = 0u;
        statistics.submission.timelineWaitCount = 1u;
        statistics.submission.mergedTimelineWaitCount = 0u;
        statistics.submission.acceptedFrontierSubmissionCount = 0u;
        statistics.submission.recoverySubmissionCount = 0u;
        statistics.submission.submissionSeconds = 0.0;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedPacketCount = 0u;
        statistics.submission.acceptedTaskCount = 1u;
        statistics.submission.nativeSubmissionCount = 0u;
        statistics.submission.nativeCommandListCount = 0u;
        statistics.submission.plannedWaitTokenCount = 0u;
        statistics.submission.sameQueueWaitElisionCount = 0u;
        statistics.submission.timelineWaitCount = 0u;
        statistics.submission.mergedTimelineWaitCount = 0u;
        statistics.submission.acceptedFrontierSubmissionCount = 0u;
        statistics.submission.recoverySubmissionCount = 0u;
        statistics.submission.submissionSeconds = 0.0;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = 0u;
        statistics.submission.rejectedTaskCount = 1u;
        statistics.submission.rejectedSubmissionCount = 0u;
    }),
    MakeNotNull<EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation>([](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.taskCount = 0u;
        statistics.compile.packetCount = 0u;
        statistics.compile.mergedTaskCount = 0u;
        statistics.compile.prologueBarrierCount = 1u;
        statistics.compile.epilogueBarrierCount = 0u;
        statistics.compile.ownershipReleaseBarrierCount = 0u;
        statistics.compile.ownershipAcquireBarrierCount = 0u;
        statistics.recording = {};
        statistics.submission = {};
    }),
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PhysicalQueueDecodeShrinkingInputClearsPreviousRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    Telemetry::TelemetryBytes payload(testArena.arena);
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(916u, nodes, edges, records, testArena.scratchArena, payload));
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    ASSERT_EQ(parsed->physicalQueueRuntimeStatistics.size(), 2u);

    records.resize(1u);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(916u, nodes, edges, records, testArena.scratchArena, payload));
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    ASSERT_EQ(parsed->physicalQueueRuntimeStatistics.size(), 1u);
    EXPECT_EQ(parsed->physicalQueueRuntimeStatistics[0u].statistics.queue.index, 3u);
}

TEST(Telemetry, FrameGraphPhysicalQueueRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    Telemetry::TelemetryBytes payload(testArena.arena);

    records[0u].statistics.graphGeneration = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].ownerNodeIndex = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.queue.deviceGeneration = 18u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.queueClass = Telemetry::FrameGraphQueueClass::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.recordingSeconds = -1.0;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.recoverySubmissionCount = records[0u].statistics.submission.acceptedFrontierSubmissionCount + 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.recoverySubmissionCount;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(records[0u].statistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.compile.mergedTaskCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.timelineWaitCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.taskCount = 79u;
    records[0u].statistics.compile.packetCount = 78u;
    records[0u].statistics.compile.mergedTaskCount = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.recordingSeconds = 0.017;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.taskCount = 29u;
    records[0u].statistics.compile.mergedTaskCount = 2u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.plannedWaitTokenCount;
    ++records[0u].statistics.submission.timelineWaitCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.epilogueBarrierCount = 4u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.barrierCount = 12u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.taskCount = 13u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.acceptedTaskCount = 13u;
    records[0u].statistics.submission.rejectedPacketCount = 14u;
    records[0u].statistics.submission.rejectedTaskCount = 15u;
    records[0u].statistics.submission.rejectedSubmissionCount = 14u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.incomingLogicalOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.outgoingLogicalOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.incomingRepeatedOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records.push_back(records[0u]);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));

    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    const usize statisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics) * 2u
    ;
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
        Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
        NWB_MEMCPY(&encodedStatistics, sizeof(encodedStatistics), payload.data() + statisticsOffset, sizeof(encodedStatistics));
        s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations[mutationIndex].get()(encodedStatistics);
        NWB_MEMCPY(payload.data() + statisticsOffset, payload.size() - statisticsOffset, &encodedStatistics, sizeof(encodedStatistics));
        EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    }

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    const usize secondStatisticsOffset = statisticsOffset + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics);
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
    NWB_MEMCPY(&encodedStatistics, sizeof(encodedStatistics), payload.data() + secondStatisticsOffset, sizeof(encodedStatistics));
    encodedStatistics.queue.index = 1u;
    NWB_MEMCPY(payload.data() + secondStatisticsOffset, payload.size() - secondStatisticsOffset, &encodedStatistics, sizeof(encodedStatistics));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ++header.physicalQueueRuntimeStatisticsCount;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(917u, nodes, edges, records, testArena.scratchArena, payload));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/telemetry/frame_graph_registry.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_registry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_Target = "target";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

class PendingNameFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle source = builder.addPass(Name(TestCases::s_TelemetryCaseSource), "Source");
        const Telemetry::FrameGraphNodeHandle target = builder.addResource(Name(s_Target), "Target");
        const Telemetry::FrameGraphNodeHandle duplicateTarget = builder.addResource(Name(s_Target), "Duplicate Target");
        if(!target.valid() || !duplicateTarget.valid())
            return false;

        builder.dependsOnByName(source, Name(s_Target), 7u);
        builder.dependsOnByName(source, Name(TestCases::s_TelemetryCaseMissing), 9u);
        return true;
    }
};

struct ContributionFailure{};

class LocalLabelContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        char label[] = "local label";
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseLocalLabel), AStringView(label));
        label[0u] = 'X';
        return node.valid();
    }
};

class MutatingContributor final : public Telemetry::IFrameGraphContributor{
public:
    explicit MutatingContributor(Telemetry::FrameGraphRegistry& registry)noexcept
        : m_registry(registry)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        ++calls;
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseMutation), "mutation");
        if(clearRegistry)
            m_registry.clear();
        if(removeSelf)
            m_registry.unregisterContributor(*this);
        if(removeOther)
            m_registry.unregisterContributor(*removeOther);
        if(addOther)
            m_registry.registerContributor(*addOther);
        if(throwAfterMutation)
            throw ContributionFailure{};
        return node.valid();
    }

    Telemetry::IFrameGraphContributor* removeOther = nullptr;
    Telemetry::IFrameGraphContributor* addOther = nullptr;
    usize calls = 0u;
    b8 removeSelf = false;
    b8 clearRegistry = false;
    b8 throwAfterMutation = false;

private:
    Telemetry::FrameGraphRegistry& m_registry;
};

class OwnedLabelContributor final : public Telemetry::IFrameGraphContributor{
public:
    OwnedLabelContributor(Telemetry::FrameGraphRegistry& registry, Telemetry::TelemetryArena& arena)
        : m_registry(registry)
        , m_label("label retained after contributor destruction", arena)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseOwnedLabel), AStringView(m_label));
        m_registry.unregisterContributor(*this);
        return node.valid();
    }

private:
    Telemetry::FrameGraphRegistry& m_registry;
    AString<Telemetry::TelemetryArena> m_label;
};

class DestroyContributor final : public Telemetry::IFrameGraphContributor{
public:
    explicit DestroyContributor(Optional<OwnedLabelContributor>& target)noexcept
        : m_target(target)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        m_target.reset();
        return builder.addPass(Name(TestCases::s_TelemetryCaseDestroyer), "destroyer").valid();
    }

private:
    Optional<OwnedLabelContributor>& m_target;
};

class CacheLifetimeContributor final : public Telemetry::IFrameGraphContributor{
public:
    CacheLifetimeContributor(Telemetry::FrameGraphRegistry& registry, Telemetry::CaptureSession& session, const AString<Telemetry::TelemetryArena>& label)noexcept
        : m_registry(registry)
        , m_session(session)
        , m_label(label)
    {}


public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        if(!builder.addPass(Name(TestCases::s_TelemetryCaseCacheLifetime), AStringView(m_label)).valid())
            return false;
        if(m_throwNext){
            m_throwNext = false;
            throw ContributionFailure{};
        }
        m_reentryRejected = !m_registry.record(m_session);
        return true;
    }


public:
    b8 m_throwNext = true;
    b8 m_reentryRejected = false;


private:
    Telemetry::FrameGraphRegistry& m_registry;
    Telemetry::CaptureSession& m_session;
    const AString<Telemetry::TelemetryArena>& m_label;
};

TEST(Telemetry, RegistryOwnsLabelsFromContributorCallbacks){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    LocalLabelContributor contributor;
    registry.registerContributor(contributor);
    ASSERT_TRUE(registry.record(session));
    const auto* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 1u);
    EXPECT_EQ(parsed->nodes[0u].label, "local label");
}

TEST(Telemetry, RegistryDefersAddedContributorsAndSkipsRemovedOnes){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), removed(registry), last(registry), added(registry);
    first.removeSelf = true;
    first.removeOther = &removed;
    first.addOther = &added;
    registry.registerContributor(first);
    registry.registerContributor(removed);
    registry.registerContributor(last);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(removed.calls, 0u);
    EXPECT_EQ(last.calls, 1u);
    EXPECT_EQ(added.calls, 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(removed.calls, 0u);
    EXPECT_EQ(last.calls, 2u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryKeepsLabelsAfterRemovedContributorIsDestroyed){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    Optional<OwnedLabelContributor> first;
    first.emplace(registry, testArena.arena);
    DestroyContributor destroyer(first);
    registry.registerContributor(*first);
    registry.registerContributor(destroyer);
    ASSERT_TRUE(registry.record(session));
    EXPECT_FALSE(first.has_value());
    const auto* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 2u);
    EXPECT_EQ(parsed->nodes[0u].label, "label retained after contributor destruction");
}

TEST(Telemetry, RegistryClearDuringCallbackSkipsRemainingContributors){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), skipped(registry), added(registry);
    first.clearRegistry = true;
    first.addOther = &added;
    registry.registerContributor(first);
    registry.registerContributor(skipped);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryCanRemoveAndReregisterTheCurrentContributor){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor contributor(registry);
    contributor.removeSelf = true;
    contributor.addOther = &contributor;
    registry.registerContributor(contributor);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.calls, 1u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.calls, 2u);
}

TEST(Telemetry, RegistryAppliesCallbackMutationsAfterUnwind){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), skipped(registry), added(registry);
    first.removeSelf = true;
    first.removeOther = &skipped;
    first.addOther = &added;
    first.throwAfterMutation = true;
    registry.registerContributor(first);
    registry.registerContributor(skipped);
    EXPECT_THROW(EXPECT_TRUE(registry.record(session)), ContributionFailure);
    EXPECT_EQ(session.eventCount(), 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryClearsBorrowedDescriptorsAfterUnwindAndReusesGraphCapacity){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    AString<Telemetry::TelemetryArena> label("first borrowed label", testArena.arena);
    CacheLifetimeContributor contributor(registry, session, label);
    registry.registerContributor(contributor);
    EXPECT_THROW(EXPECT_TRUE(registry.record(session)), ContributionFailure);
    EXPECT_EQ(session.eventCount(), 0u);

    label.assign("replacement label after unwind");
    ASSERT_TRUE(registry.record(session));
    EXPECT_TRUE(contributor.m_reentryRejected);
    const auto* const event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 1u);
    EXPECT_EQ(AStringView(parsed->nodes[0u].label), AStringView(label));
}

TEST(Telemetry, PendingNameEdgesChooseFirstDuplicateAndOmitMissingTargets){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    PendingNameFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    EXPECT_TRUE(registry.record(session));
    EXPECT_EQ(session.eventCount(), 1u);

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    EXPECT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size())));
    ASSERT_EQ(parsed->nodes.size(), 3u);
    ASSERT_EQ(parsed->edges.size(), 1u);
    EXPECT_EQ(parsed->edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed->edges[0u].toNodeIndex, 1u);
}

TEST(Telemetry, PhysicalQueueBuilderOwnsSourceAndRejectsDuplicateOrExcessCounts){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, nodes, edges, pendingNameEdges, records, 915u);

    const Telemetry::FrameGraphNodeHandle owner = builder.addPass(Name(TestCases::s_RuntimeOwner), "Runtime owner", Telemetry::FrameGraphPassMetadata{ .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = MakeFrameGraphRuntimeStatistics(), });
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, statistics));
    statistics.compile.taskCount = 0u;

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0u].statistics.compile.taskCount, 50u);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics outOfBounds = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    outOfBounds.compile.taskCount = 79u;
    outOfBounds.compile.packetCount = 78u;
    outOfBounds.compile.mergedTaskCount = 1u;
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, outOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics derivedOutOfBounds = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    derivedOutOfBounds.compile.taskCount = 29u;
    derivedOutOfBounds.compile.mergedTaskCount = 2u;
    derivedOutOfBounds.recording.taskCount = 13u;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(derivedOutOfBounds));
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(derivedOutOfBounds, MakeFrameGraphRuntimeStatistics()));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, derivedOutOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics reorderedSubmissionDuration = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    reorderedSubmissionDuration.submission.submissionSeconds = 0.021000000000000004;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(reorderedSubmissionDuration, MakeFrameGraphRuntimeStatistics()));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/alloc/scratch.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_report_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

constexpr AStringView s_ReportArenaName = "tests/telemetry/report_work";

static constexpr AStringView s_PacketIndex = "\"packet\": {\"index\":";
static constexpr AStringView s_RuntimePacketSubmissionCount3 = "runtime_packet_submission_count=3";
static constexpr AStringView s_RuntimePacketSubmissionCount2 = "runtime_packet_submission_count=2";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


struct ReportFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;


public:
    explicit ReportFixture(Telemetry::TelemetryArena& arena)noexcept
        : nodes(arena)
        , edges(arena)
        , queues(arena)
        , packets(arena)
    {}


public:
    void addUnmeasuredNode(){
        nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name(TestCases::s_ReportTask), .label = "Unmeasured task", .kind = Telemetry::FrameGraphNodeKind::Pass, .flags = 0u, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = {}, });
    }

    void addOwner(const u32 packetCount, const u16 firstQueueIndex, const b8 includeQueues = true){
        const u32 ownerIndex = static_cast<u32>(nodes.size());
        const Telemetry::FrameGraphRuntimeStatistics owner = OwnerStatistics(packetCount);
        nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name(TestCases::s_ReportOwner), .label = "Measured owner", .kind = Telemetry::FrameGraphNodeKind::Pass, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = owner, });
        if(includeQueues){
            const u32 queueCount = packetCount > 1u ? 2u : packetCount;
            for(u32 queueOffset = 0u; queueOffset < queueCount; ++queueOffset){
                const u32 acceptedCount = packetCount / 2u + (queueOffset == 0u ? packetCount % 2u : 0u);
                Telemetry::FrameGraphPhysicalQueueRuntimeStatistics statistics;
                statistics.graphGeneration = owner.graphGeneration;
                statistics.planGeneration = owner.planGeneration;
                statistics.recordingAttemptGeneration = owner.recordingAttemptGeneration;
                statistics.deviceGeneration = owner.deviceGeneration;
                statistics.queue = { .index = static_cast<u16>(firstQueueIndex + queueOffset), .deviceGeneration = 17u };
                statistics.queueClass = Telemetry::FrameGraphQueueClass::Graphics;
                statistics.compile.taskCount = acceptedCount;
                statistics.compile.packetCount = acceptedCount;
                statistics.recording.packetCount = acceptedCount;
                statistics.recording.taskCount = acceptedCount;
                statistics.recording.commandListCount = acceptedCount;
                statistics.submission.acceptedPacketCount = acceptedCount;
                statistics.submission.acceptedTaskCount = acceptedCount;
                statistics.submission.nativeSubmissionCount = acceptedCount;
                statistics.submission.nativeCommandListCount = acceptedCount;
                queues.push_back({ .ownerNodeIndex = ownerIndex, .statistics = statistics });
            }
        }
        // Deliberately unsorted producer input exercises the codec's canonical owner and packet ordering.
        for(u32 remaining = packetCount; remaining != 0u; --remaining){
            const u32 packetIndex = remaining - 1u;
            packets.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
                .packetGeneration = owner.planGeneration,
                .taskCount = 1u,
                .commandListCount = 1u,
                .plannedWaitTokenCount = 0u,
                .sameQueueWaitElisionCount = 0u,
                .timelineWaitCount = 0u,
                .mergedTimelineWaitCount = 0u,
                .inheritedTimelineWaitElisionCount = 0u,
                .submissionSeconds = 0.0,
                .queue = { .index = static_cast<u16>(firstQueueIndex + packetIndex % 2u), .deviceGeneration = 17u },
                .ownerNodeIndex = ownerIndex,
                .packetIndex = packetIndex,
                .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
                .joinsAcceptedQueueFrontier = false,
                .recoverySubmission = false,
            });
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::CountText;

[[nodiscard]] AStringView FindLine(const AStringView text, const AStringView prefix)noexcept{
    const usize begin = text.find(prefix);
    if(begin == AStringView::npos)
        return {};
    const usize end = text.find('\n', begin);
    return text.substr(begin, end == AStringView::npos ? text.size() - begin : end - begin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameGraphReport, GroupsSparseOwnersWithoutLeakingStatisticsIntoAdjacentNodes){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.addUnmeasuredNode();
    fixture.addOwner(3u, 1u);
    fixture.addUnmeasuredNode();
    fixture.addOwner(0u, 5u);
    fixture.addUnmeasuredNode();
    fixture.addOwner(2u, 7u, false);
    fixture.addUnmeasuredNode();
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, 19u));
    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult2 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult2);
    report = Move(*reportResult2);
    ASSERT_EQ(report.summary.parseFailureCount, 0u);
    ASSERT_EQ(report.summary.frameGraphNodeCount, 7u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView first = FindLine(json, "{\"index\": 1, \"identity\":");
    const AStringView empty = FindLine(json, "{\"index\": 3, \"identity\":");
    const AStringView last = FindLine(json, "{\"index\": 5, \"identity\":");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(empty.empty());
    ASSERT_FALSE(last.empty());
    EXPECT_EQ(CountText(first, s_PacketIndex), 3u);
    EXPECT_TRUE(Contains(first, "\"physicalQueues\": [{"));
    EXPECT_TRUE(Contains(first, "\"queue\": {\"index\": 1,"));
    EXPECT_TRUE(Contains(first, "\"queue\": {\"index\": 2,"));
    EXPECT_FALSE(Contains(first, "\"queue\": {\"index\": 7,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 0,"), first.find("\"packet\": {\"index\": 1,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 1,"), first.find("\"packet\": {\"index\": 2,"));
    EXPECT_TRUE(Contains(empty, "\"physicalQueues\": null, \"packetSubmissions\": []"));
    EXPECT_EQ(CountText(last, s_PacketIndex), 2u);
    EXPECT_TRUE(Contains(last, "\"physicalQueues\": null"));
    EXPECT_TRUE(Contains(last, "\"queue\": {\"index\": 7,"));
    EXPECT_TRUE(Contains(last, "\"queue\": {\"index\": 8,"));
    EXPECT_FALSE(Contains(last, "\"queue\": {\"index\": 1,"));
    EXPECT_EQ(CountText(json, "\"runtimeStatistics\": null"), 4u);

    const AStringView dot(report.graph.data(), report.graph.size());
    const AStringView firstDot = FindLine(dot, "  n1 [");
    const AStringView emptyDot = FindLine(dot, "  n3 [");
    const AStringView lastDot = FindLine(dot, "  n5 [");
    EXPECT_TRUE(Contains(firstDot, "runtime_physical_queue_count=2"));
    EXPECT_TRUE(Contains(firstDot, s_RuntimePacketSubmissionCount3));
    EXPECT_TRUE(Contains(emptyDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(Contains(emptyDot, "runtime_packet_submission_count=0"));
    EXPECT_TRUE(Contains(lastDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(Contains(lastDot, s_RuntimePacketSubmissionCount2));
    EXPECT_EQ(CountText(dot, "runtime_packet_submission_count="), 3u);
}

TEST(FrameGraphReport, RestartsOwnerRangesForEveryCaptureAndReportRebuild){
    TestArena testArena;
    ReportFixture first(testArena.arena);
    first.addOwner(3u, 1u);
    first.addUnmeasuredNode();
    ReportFixture second(testArena.arena);
    second.addUnmeasuredNode();
    second.addOwner(2u, 7u);
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 918u, first.nodes, first.edges, first.queues, first.packets, 19u));
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 919u, second.nodes, second.edges, second.queues, second.packets, 20u));
    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult3 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult3);
    report = Move(*reportResult3);
    ASSERT_EQ(report.summary.frameGraphFrameCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 5u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_physical_queue_count=2"), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3), 1u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount2), 1u);

    recorder.clear();
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 920u, second.nodes, second.edges, second.queues, second.packets, 21u));
    auto reportResult4 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult4);
    report = Move(*reportResult4);
    ASSERT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount2), 1u);
    EXPECT_FALSE(Contains(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3));
}

TEST(FrameGraphReport, RejectsMalformedOwnerTablesBeforeReportingAndRetainsTheNextValidCapture){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.addUnmeasuredNode();
    fixture.addOwner(3u, 1u);
    Telemetry::TelemetryBytes original(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, original));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), original.data(), sizeof(header));
    ASSERT_EQ(header.queueAssignmentCount, 0u);
    ASSERT_EQ(header.compiledTaskCount, 0u);
    ASSERT_EQ(header.edgeCount, 0u);
    ASSERT_EQ(header.runtimeStatisticsCount, 1u);
    ASSERT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);
    ASSERT_EQ(header.packetSubmissionStatisticsCount, 3u);
    const usize queueOffset = sizeof(header) + header.nodeCount * sizeof(Telemetry::EncodedFrameGraphNode) + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics);
    const usize packetOffset = queueOffset + 2u * sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics);
    ASSERT_LE(packetOffset + 3u * sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics), original.size());
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::TelemetryBytes malformed(original);
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics packet;
    NWB_MEMCPY(&packet, sizeof(packet), malformed.data() + packetOffset, sizeof(packet));
    packet.ownerNodeIndex = header.nodeCount;
    NWB_MEMCPY(malformed.data() + packetOffset, malformed.size() - packetOffset, &packet, sizeof(packet));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 1u));

    malformed = original;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics nextPacket;
    NWB_MEMCPY(&packet, sizeof(packet), original.data() + packetOffset, sizeof(packet));
    NWB_MEMCPY(&nextPacket, sizeof(nextPacket), original.data() + packetOffset + sizeof(packet), sizeof(nextPacket));
    NWB_MEMCPY(malformed.data() + packetOffset, malformed.size() - packetOffset, &nextPacket, sizeof(nextPacket));
    NWB_MEMCPY(malformed.data() + packetOffset + sizeof(packet), sizeof(packet), &packet, sizeof(packet));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 2u));

    malformed = original;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics queue;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics nextQueue;
    NWB_MEMCPY(&queue, sizeof(queue), original.data() + queueOffset, sizeof(queue));
    NWB_MEMCPY(&nextQueue, sizeof(nextQueue), original.data() + queueOffset + sizeof(queue), sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset, malformed.size() - queueOffset, &nextQueue, sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset + sizeof(queue), sizeof(queue), &queue, sizeof(queue));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 3u));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, original.data(), original.size(), 4u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult5 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult5);
    report = Move(*reportResult5);
    EXPECT_EQ(report.summary.eventCount, 4u);
    EXPECT_EQ(report.summary.parseFailureCount, 3u);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 3u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3), 1u);
    EXPECT_TRUE(Contains(AStringView(report.graph.data(), report.graph.size()), "Frame 918 stream 4"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/not_null.h>

#include <gtest/gtest.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_runtime_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

using FrameGraphRuntimeStatisticsMutation = void(*)(Telemetry::FrameGraphRuntimeStatistics&);

static constexpr Array<NotNull<FrameGraphRuntimeStatisticsMutation>, 38> s_FrameGraphRuntimeStatisticsCountMutations = { MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.packetCount = statistics.compile.taskCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.mergedTaskCount;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.resourceVersionCount = 0u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.directResourceUseCount = statistics.compile.resourceUseCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.expandedResourceSetMemberUseCount;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.payloadObjectCount = statistics.compile.taskCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.repeatedOwnershipTransferSignatureCount = statistics.compile.logicalOwnershipTransferSignatureCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.concurrentSharingCouldAvoidTransferCount = statistics.compile.logicalOwnershipTransferCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.concurrentSharingAdviceResourceCount = statistics.compile.resourceCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferInternalCount = statistics.compile.logicalOwnershipTransferCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferExternalImportCount = statistics.compile.logicalOwnershipTransferCount - statistics.compile.logicalOwnershipTransferInternalCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.logicalOwnershipTransferExternalExportCount;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.packetCount = statistics.compile.packetCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = statistics.compile.taskCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = statistics.recording.packetCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.commandListCount = statistics.recording.packetCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.workerRoutedPacketCount = statistics.recording.packetCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.parallelPacketCount = statistics.recording.packetCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedPacketCount = statistics.compile.packetCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = statistics.compile.packetCount - statistics.submission.acceptedPacketCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedTaskCount = statistics.compile.taskCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedTaskCount = statistics.compile.taskCount - statistics.submission.acceptedTaskCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedTaskCount = statistics.submission.acceptedPacketCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedTaskCount = statistics.submission.rejectedPacketCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedPacketCount = statistics.submission.nativeSubmissionCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedSubmissionCount = statistics.submission.rejectedPacketCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedFrontierSubmissionCount = statistics.submission.nativeSubmissionCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.recoverySubmissionCount = statistics.submission.acceptedFrontierSubmissionCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeSubmissionCount = statistics.recording.packetCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeCommandListCount = statistics.submission.nativeSubmissionCount - 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeCommandListCount = statistics.recording.commandListCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.sameQueueWaitElisionCount = statistics.submission.plannedWaitTokenCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.mergedTimelineWaitCount = statistics.submission.plannedWaitTokenCount - statistics.submission.sameQueueWaitElisionCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = statistics.submission.plannedWaitTokenCount - statistics.submission.sameQueueWaitElisionCount - statistics.submission.mergedTimelineWaitCount + 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission = {};
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.inheritedTimelineWaitElisionCount = 1u;
    }),
    MakeNotNull<FrameGraphRuntimeStatisticsMutation>([](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.submission.timelineWaitCount;
    }),
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
    ;
    Telemetry::EncodedFrameGraphRuntimeStatistics first;
    Telemetry::EncodedFrameGraphRuntimeStatistics second;
    const auto loadRuntimeStatistics = [&]()->b8{
        if(!Telemetry::BuildFrameGraphPayload(911u, nodes, edges, testArena.scratchArena, payload))
            return false;
        NWB_MEMCPY(&first, sizeof(first), payload.data() + runtimeStatisticsOffset, sizeof(first));
        NWB_MEMCPY(&second, sizeof(second), payload.data() + runtimeStatisticsOffset + sizeof(first), sizeof(second));
        return true;
    };

    ASSERT_TRUE(loadRuntimeStatistics());
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset + sizeof(first), payload.size() - runtimeStatisticsOffset - sizeof(first), &second, sizeof(second));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 2u;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset + sizeof(first), payload.size() - runtimeStatisticsOffset - sizeof(first), &second, sizeof(second));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 3u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.reserved = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.graphGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recordingAttemptGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.compile.declarationSeconds = -1.0;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recording.recordingSeconds = Limit<f64>::s_Infinity;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(loadRuntimeStatistics());
        Telemetry::FrameGraphRuntimeStatistics malformed = MakeFrameGraphRuntimeStatistics();
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex].get()(malformed);
        first = EncodeTestFrameGraphRuntimeStatistics(malformed, first.nodeIndex, first.reserved);
        NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));
    }

    ASSERT_TRUE(loadRuntimeStatistics());
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.runtimeStatisticsCount = 4u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size()));

    ASSERT_TRUE(loadRuntimeStatistics());
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size() - 1u));
}

TEST(Telemetry, FrameGraphPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    nodes[0u].kind = Telemetry::FrameGraphNodeKind::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].label = AStringView("bad\0label", 9u);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    edges[0u].toNodeIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].queueAssignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::First;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].compiledTask = MakeFrameGraphCompiledTask(41u, 7u, Telemetry::FrameGraphTaskPacketizationDecision::FirstTask);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.graphGeneration = 0u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.compile.totalSeconds = -1.0;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.recording.recordingElapsedSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex].get()(nodes[0u].runtimeStatistics);
        EXPECT_FALSE(Telemetry::IsValidFrameGraphRuntimeStatistics(nodes[0u].runtimeStatistics));
        EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(1u, nodes, edges, testArena.scratchArena, payload));
    }
}


TEST(Telemetry, FrameGraphDurationTailsRejectInvalidValuesAndPreserveSignedZeroSubnormals){
    // The final submission durations are scalar tails after four-lane validation blocks.
    const f64 minimumSubnormal = BitCast<f64>(u64{ 1u });
    const Array<f64, 5> invalidDurations = { -1.0, BitCast<f64>(u64{ 0x8000000000000001ull }), Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN, BitCast<f64>(u64{ 0x7FF0000000000001ull }) };
    for(const f64 duration : invalidDurations){
        Telemetry::FrameGraphRuntimeStatistics owner = MakeFrameGraphRuntimeStatistics();
        Telemetry::FrameGraphPhysicalQueueRuntimeStatistics queue = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u);
        owner.submission.submissionSeconds = duration;
        EXPECT_FALSE(Telemetry::IsValidFrameGraphRuntimeStatistics(owner));
        owner = MakeFrameGraphRuntimeStatistics();
        queue.submission.submissionSeconds = duration;
        EXPECT_FALSE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(queue));
        EXPECT_FALSE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(queue, owner));
    }
    for(const f64 duration : { -0.0, minimumSubnormal }){
        Telemetry::FrameGraphRuntimeStatistics owner = MakeFrameGraphRuntimeStatistics();
        Telemetry::FrameGraphPhysicalQueueRuntimeStatistics queue = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u);
        owner.submission.submissionSeconds = duration;
        queue.submission.submissionSeconds = duration;
        EXPECT_TRUE(Telemetry::IsValidFrameGraphRuntimeStatistics(owner));
        EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(queue));
        EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(queue, owner));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/frame_graph_snapshot_validation.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_snapshot_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

using namespace TelemetryTestDetail;

struct Fixture{
    TestArena arena;
    Telemetry::FrameGraphPayload payload{ arena.arena };
    Telemetry::FrameGraphRuntimeStatistics runtime;


public:
    Fixture(){
        runtime.planGeneration = 9u;
        runtime.compile.taskCount = 4u;
        runtime.compile.packetCount = 3u;
        runtime.submission.acceptedTaskCount = 3u;
        runtime.submission.acceptedPacketCount = 2u;
        runtime.submission.rejectedTaskCount = 1u;
        runtime.submission.rejectedPacketCount = 1u;
        const Array<Name, 4u> names{ Name(TestCases::s_RenderFirst), Name(TestCases::s_RenderSecond), Name(TestCases::s_RenderPresent), Name(TestCases::s_RenderFrameRecovery) };
        const Array<u32, 4u> packets{ 0u, 0u, 1u, 2u };
        for(usize index = 0u; index < names.size(); ++index){
            auto& node = payload.nodes.emplace_back();
            node.name = names[index];
            node.compiledTask = { .planGeneration = runtime.planGeneration, .packetIndex = packets[index], .present = true };
        }
        for(u32 index = 0u; index < 2u; ++index){
            auto& packet = payload.packetSubmissionStatistics.emplace_back();
            packet.packetGeneration = runtime.planGeneration;
            packet.packetIndex = index;
            packet.taskCount = index == 0u ? 2u : 1u;
            packet.queue = { .index = 0u, .deviceGeneration = 1u };
        }
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return NWB::Tests::Smoke::ValidateNormalFrameSnapshotCompletion(payload, runtime); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameGraphSnapshotValidation, FullyAcceptedCountsCannotHideMissingTasksOrPackets){
    Fixture fixture;
    ASSERT_TRUE(fixture.valid());
    fixture.payload.nodes.pop_back();
    --fixture.runtime.compile.taskCount;
    --fixture.runtime.compile.packetCount;
    fixture.runtime.submission.rejectedTaskCount = 0u;
    fixture.runtime.submission.rejectedPacketCount = 0u;
    fixture.payload.nodes[2u].compiledTask.present = false;
    EXPECT_FALSE(fixture.valid());
    fixture.payload.nodes[2u].compiledTask.present = true;
    fixture.payload.packetSubmissionStatistics.pop_back();
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, FullyAcceptedCountsStillRejectExecutedRecovery){
    Fixture fixture;
    fixture.runtime.submission.acceptedTaskCount = fixture.runtime.compile.taskCount;
    fixture.runtime.submission.acceptedPacketCount = fixture.runtime.compile.packetCount;
    fixture.runtime.submission.rejectedTaskCount = 0u;
    fixture.runtime.submission.rejectedPacketCount = 0u;
    fixture.runtime.submission.recoverySubmissionCount = 1u;
    auto& recovery = fixture.payload.packetSubmissionStatistics.emplace_back();
    recovery.packetGeneration = fixture.runtime.planGeneration;
    recovery.packetIndex = 2u;
    recovery.taskCount = 1u;
    recovery.queue = { .index = 0u, .deviceGeneration = 1u };
    recovery.recoverySubmission = true;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.recoverySubmissionCount = 0u;
    EXPECT_FALSE(fixture.valid());
    recovery.recoverySubmission = false;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsNativeFailureOrExecutedRecovery){
    Fixture fixture;
    fixture.runtime.submission.rejectedSubmissionCount = 1u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedSubmissionCount = 0u;
    fixture.runtime.submission.recoverySubmissionCount = 1u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsAMissingNormalTaskOrAcceptedRecoveryTask){
    Fixture fixture;
    fixture.payload.nodes[3u].name = Name(TestCases::s_TelemetryCaseRenderNormalWork);
    EXPECT_FALSE(fixture.valid());
    fixture.payload.nodes[3u].name = Name(TestCases::s_RenderFrameRecovery);
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 2u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsRecoverySharingItsUnsubmittedPacket){
    Fixture fixture;
    fixture.payload.nodes[2u].compiledTask.packetIndex = 2u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsAdditionalDiscardsOrIncompleteAcceptanceCounts){
    Fixture fixture;
    fixture.runtime.submission.rejectedPacketCount = 2u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedPacketCount = 1u;
    fixture.runtime.submission.rejectedTaskCount = 2u;
    EXPECT_FALSE(fixture.valid());
    fixture.runtime.submission.rejectedTaskCount = 1u;
    --fixture.runtime.submission.acceptedTaskCount;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsDuplicatePacketRowsAndMismatchedTaskCounts){
    Fixture fixture;
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 0u;
    EXPECT_FALSE(fixture.valid());
    fixture.payload.packetSubmissionStatistics[1u].packetIndex = 1u;
    fixture.payload.packetSubmissionStatistics[0u].taskCount = 1u;
    EXPECT_FALSE(fixture.valid());
}

TEST(FrameGraphSnapshotValidation, RejectsGenerationMismatchAndMissingCompiledTask){
    Fixture fixture;
    ++fixture.payload.nodes[0u].compiledTask.planGeneration;
    EXPECT_FALSE(fixture.valid());
    --fixture.payload.nodes[0u].compiledTask.planGeneration;
    fixture.payload.nodes[0u].compiledTask.present = false;
    EXPECT_FALSE(fixture.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/telemetry/frame_graph_contributor.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_statistics_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_QueueAliasOwner = "queue_alias_owner";
static constexpr AStringView s_QueueAliasOwner2 = "Queue alias owner";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct StatisticsFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPendingNameEdges pendingEdges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;


public:
    explicit StatisticsFixture(Telemetry::TelemetryArena& arena)noexcept
        : nodes(arena)
        , edges(arena)
        , pendingEdges(arena)
        , queues(arena)
        , packets(arena)
    {}
};

[[nodiscard]] Telemetry::FrameGraphPhysicalQueueRuntimeStatistics QueueStatistics(const u16 queueIndex, const u32 packetCount)noexcept{
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics stats;
    stats.graphGeneration = 71u;
    stats.planGeneration = 72u;
    stats.recordingAttemptGeneration = 73u;
    stats.deviceGeneration = 17u;
    stats.queue = { .index = queueIndex, .deviceGeneration = 17u };
    stats.queueClass = queueIndex == 1u ? Telemetry::FrameGraphQueueClass::Graphics : Telemetry::FrameGraphQueueClass::Compute;
    stats.compile.taskCount = packetCount;
    stats.compile.packetCount = packetCount;
    stats.recording.packetCount = packetCount;
    stats.recording.taskCount = packetCount;
    stats.recording.commandListCount = packetCount;
    stats.submission.acceptedPacketCount = packetCount;
    stats.submission.acceptedTaskCount = packetCount;
    stats.submission.nativeSubmissionCount = packetCount;
    stats.submission.nativeCommandListCount = packetCount;
    return stats;
}

void PrepareFixture(const u32 packetCount, const u32 ownerCount, StatisticsFixture& fixture){
    const u32 packetsPerOwner = packetCount / ownerCount;
    fixture.nodes.reserve(ownerCount);
    fixture.queues.reserve(ownerCount * 2u);
    fixture.packets.reserve(packetCount);
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        fixture.nodes.push_back(Telemetry::FrameGraphNodeDesc{
            .name = Name(TestCases::s_TelemetryCaseScalingOwner),
            .label = "Scaling owner",
            .kind = Telemetry::FrameGraphNodeKind::Pass,
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = OwnerStatistics(packetsPerOwner),
        });
    }
}

[[nodiscard]] b8 AppendFixtureStatistics(const u32 packetCount, const u32 ownerCount, NWB::Core::Alloc::ScratchArena& scratchArena, StatisticsFixture& fixture){
    Telemetry::FrameGraphBuilder builder(scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    const u32 packetsPerOwner = packetCount / ownerCount;
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        const Telemetry::FrameGraphNodeHandle owner{ ownerIndex };
        if(!builder.addPhysicalQueueRuntimeStatistics(owner, QueueStatistics(3u, packetsPerOwner / 2u)))
            return false;
        if(!builder.addPhysicalQueueRuntimeStatistics(owner, QueueStatistics(1u, packetsPerOwner / 2u)))
            return false;
    }
    // Odd multiplication permutes the power-of-two workload, interleaving both owners and physical queues.
    for(u32 index = 0u; index < packetCount; ++index){
        const u32 shuffled = (index * 2053u + 7u) % packetCount;
        const u32 ownerIndex = shuffled / packetsPerOwner;
        const u32 packetIndex = shuffled % packetsPerOwner;
        const u16 queueIndex = (packetIndex & 1u) == 0u ? 1u : 3u;
        const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packet{
            .packetGeneration = 72u,
            .taskCount = 1u,
            .commandListCount = 1u,
            .plannedWaitTokenCount = 0u,
            .sameQueueWaitElisionCount = 0u,
            .timelineWaitCount = 0u,
            .mergedTimelineWaitCount = 0u,
            .inheritedTimelineWaitElisionCount = 0u,
            .submissionSeconds = 0.0,
            .queue = { .index = queueIndex, .deviceGeneration = 17u },
            .ownerNodeIndex = ownerIndex,
            .packetIndex = packetIndex,
            .queueClass = queueIndex == 1u ? Telemetry::FrameGraphQueueClass::Graphics : Telemetry::FrameGraphQueueClass::Compute,
            .joinsAcceptedQueueFrontier = false,
            .recoverySubmission = false,
        };
        if(!builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ ownerIndex }, packet))
            return false;
    }
    return true;
}


TEST(Telemetry, PacketStatisticsDuplicateRejectionPreservesSeededTables){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(64u, 2u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(64u, 2u, testArena.scratchArena, fixture));
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    const auto packet = fixture.packets[17u];
    const Telemetry::FrameGraphNodeHandle owner{ packet.ownerNodeIndex };
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, packet));
    auto changedDuplicate = packet;
    changedDuplicate.queue.index = packet.queue.index == 1u ? 3u : 1u;
    changedDuplicate.queueClass = packet.queueClass == Telemetry::FrameGraphQueueClass::Graphics ? Telemetry::FrameGraphQueueClass::Compute : Telemetry::FrameGraphQueueClass::Graphics;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, changedDuplicate));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_EQ(fixture.packets.size(), 64u);
    EXPECT_EQ(fixture.queues.size(), 4u);
}

TEST(Telemetry, PacketStatisticsPreserveOptionalQueueCoverageAndRejectMismatchedTotals){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(64u, 2u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(64u, 2u, testArena.scratchArena, fixture));
    Telemetry::TelemetryBytes payload(testArena.arena);
    fixture.queues.erase(fixture.queues.begin());
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    Expected<Telemetry::FrameGraphPayload> decoded = MakeUnexpected(Failure{});
    ASSERT_TRUE((decoded = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    ASSERT_EQ(decoded->physicalQueueRuntimeStatistics.size(), 3u);
    const auto original = fixture.packets[0u];
    fixture.packets[0u].queue.index = original.queue.index == 1u ? 3u : 1u;
    fixture.packets[0u].queueClass = original.queueClass == Telemetry::FrameGraphQueueClass::Graphics ? Telemetry::FrameGraphQueueClass::Compute : Telemetry::FrameGraphQueueClass::Graphics;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, PacketStatisticsSparsePacketIdentifiersRemainValid){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(2u, 1u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(2u, 1u, testArena.scratchArena, fixture));
    fixture.nodes[0u].runtimeStatistics.compile.packetCount = Limit<u32>::s_Max - 1u;
    fixture.nodes[0u].runtimeStatistics.compile.taskCount = Limit<u32>::s_Max - 1u;
    fixture.packets[0u].packetIndex = Limit<u32>::s_Max - 2u;
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
}

TEST(Telemetry, PacketStatisticsInvalidInputDoesNotConsumeIdentity){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(2u, 1u, fixture);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    auto queue = QueueStatistics(1u, 1u);
    queue.planGeneration = 74u;
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, queue));
    queue.planGeneration = 72u;
    EXPECT_TRUE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, queue));
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord packet{
        .packetGeneration = 74u,
        .taskCount = 1u,
        .commandListCount = 1u,
        .plannedWaitTokenCount = 0u,
        .sameQueueWaitElisionCount = 0u,
        .timelineWaitCount = 0u,
        .mergedTimelineWaitCount = 0u,
        .inheritedTimelineWaitElisionCount = 0u,
        .submissionSeconds = 0.0,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .ownerNodeIndex = 0u,
        .packetIndex = 0u,
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
        .joinsAcceptedQueueFrontier = false,
        .recoverySubmission = false,
    };
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    packet.packetGeneration = 72u;
    EXPECT_TRUE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    EXPECT_EQ(fixture.queues.size(), 1u);
    EXPECT_EQ(fixture.packets.size(), 1u);
}

TEST(Telemetry, PacketStatisticsQueueSourceSurvivesDestinationGrowth){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    const Telemetry::FrameGraphPassMetadata metadata{ .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = OwnerStatistics(1u), };
    const auto firstOwner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(firstOwner, QueueStatistics(1u, 1u)));
    while(fixture.queues.size() < fixture.queues.capacity()){
        const auto owner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
        ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, fixture.queues[0u].statistics));
    }
    const usize capacityBeforeGrowth = fixture.queues.capacity();
    const auto owner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, fixture.queues[0u].statistics));
    ASSERT_GT(fixture.queues.capacity(), capacityBeforeGrowth);
    EXPECT_EQ(fixture.queues.back().ownerNodeIndex, owner.index);
    EXPECT_EQ(fixture.queues.back().statistics.queue, fixture.queues[0u].statistics.queue);
    EXPECT_EQ(fixture.queues.back().statistics.planGeneration, 72u);
    EXPECT_EQ(fixture.queues.back().statistics.compile.packetCount, 1u);
    EXPECT_EQ(fixture.queues.back().statistics.submission.nativeSubmissionCount, 1u);
}

TEST(Telemetry, PacketStatisticsSeededDuplicatesRemainInvalidPayloads){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(2u, 1u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(2u, 1u, testArena.scratchArena, fixture));
    fixture.queues.push_back(fixture.queues[0u]);
    fixture.packets.push_back(fixture.packets[0u]);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    EXPECT_EQ(fixture.queues.size(), 3u);
    EXPECT_EQ(fixture.packets.size(), 3u);
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/smoke/gpu_pass_timing_probe.h>
#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_pass_timing_probe_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr TStringView s_PublishedWindows2 = NWB_TEXT("published_windows=2");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;


TEST(GpuPassTimingProbe, ReportsEveryRegisteredScopeBeyondSixtyFour){
    Tests::TestArena<> testArena;
    Core::Perf::TimingRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    constexpr u32 s_ScopeCount = 96u;
    Name lastScope;
    for(u32 index = 0u; index < s_ScopeCount; ++index){
        Array<char, 32u> scopeText = {};
        const auto scopeName = Name::FromText(FormatDecimal(index, scopeText));
        ASSERT_TRUE(scopeName);
        lastScope = *scopeName;
        const auto scopeResult = recorder.registerScope(lastScope);
        ASSERT_TRUE(scopeResult);
        const Core::Perf::TimingScopeId scope = *scopeResult;
        ASSERT_TRUE(scope.valid());
        recorder.recordSample(scope, 0.001, 1u);
    }
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(NWB_TEXT("Probe regression"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);

    EXPECT_EQ(logger.messageCount(), s_ScopeCount + 1u);
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(testArena.arena, NWB_TEXT("  {}: gpu_window_ms"), ConvertString(lastScope.resolvedText()));
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));
}

TEST(GpuPassTimingProbe, GrowthPreservesAccumulationAndWatermarksAcrossIntervals){
    Tests::TestArena<> testArena;
    Core::Perf::TimingRecorder recorder(testArena.arena);
    recorder.setEnabled(true);
    const auto firstScopeResult = recorder.registerScope(Name(TestCases::s_TelemetryCaseTestsProbeFirst));
    ASSERT_TRUE(firstScopeResult);
    const Core::Perf::TimingScopeId firstScope = *firstScopeResult;
    ASSERT_TRUE(firstScope.valid());
    recorder.recordSample(firstScope, 0.003, 1u);
    recorder.publishFrame(1u);
    const Core::Perf::TimingView view(recorder);
    Tests::Smoke::GpuPassTimingProbe probe(NWB_TEXT("Probe growth"));
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger);

    probe.recordFrame(0.25f, view);
    constexpr u32 s_AddedScopes = 96u;
    Core::Perf::TimingScopeId lastScope;
    for(u32 index = 0u; index < s_AddedScopes; ++index){
        Array<char, 32u> scopeText = {};
        const auto scopeName = Name::FromText(FormatDecimal(index, scopeText));
        ASSERT_TRUE(scopeName);
        const auto scopeResult = recorder.registerScope(*scopeName);
        ASSERT_TRUE(scopeResult);
        lastScope = *scopeResult;
        ASSERT_TRUE(lastScope.valid());
    }
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 2u);
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));

    // The old publication remains visible, but the next interval must not count it again.
    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 3u);

    recorder.recordSample(firstScope, 0.002, 2u);
    recorder.recordSample(lastScope, 0.004, 2u);
    recorder.publishFrame(2u);
    probe.recordFrame(0.25f, view);
    probe.recordFrame(0.25f, view);
    EXPECT_EQ(logger.messageCount(), 6u);
    const TString<Core::Alloc::GlobalArena> lastScopeText = StringFormat(testArena.arena, NWB_TEXT("  {}: gpu_window_ms"), ConvertString(recorder.scopeNameAt(s_AddedScopes).resolvedText()));
    EXPECT_TRUE(logger.sawMessageContaining(lastScopeText));
    EXPECT_FALSE(logger.sawMessageContaining(s_PublishedWindows2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_memory_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_MemoryProjectArena = TestCases::s_TelemetryMemoryProjectArena;
static constexpr AStringView s_ProjectArenaText = TestCases::s_TelemetryProjectArenaText;
using namespace TelemetryTestDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PerfMemoryPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    const Name scopeName{ s_MemoryProjectArena };
    NWB::Core::Perf::MemorySnapshot snapshot = MakeTestMemorySnapshot(scopeName);
    NWB::Core::Perf::MemoryDelta delta = MakeTestMemoryDelta();

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(s_NameNone, s_ProjectArenaText, snapshot, delta, payload));

    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, AStringView(), snapshot, delta, payload));

    snapshot.scopeName = Name(TestCases::s_TelemetryCaseMemoryOtherArena);
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, snapshot, delta, payload));

    snapshot = MakeTestMemorySnapshot(scopeName);
    delta.currentFrameIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, snapshot, delta, payload));
}

TEST(Telemetry, MemoryDifferencePreservesSignedLimitsAndLargeUnsignedCounters){
    using MemorySnapshot = NWB::Core::Perf::MemorySnapshot;
    constexpr u64 s_MaxSigned = static_cast<u64>(Limit<i64>::s_Max);
    constexpr u64 s_MinMagnitude = s_MaxSigned + 1u;
    using CounterCase = NWB::Tests::TelemetryCases::MemoryDifferenceCase;
    constexpr Array<CounterCase, 10> s_Cases = {{
        { .current = 0u, .previous = 0u, .expected = 0 },
        { .current = s_MaxSigned, .previous = 0u, .expected = Limit<i64>::s_Max },
        { .current = 0u, .previous = s_MaxSigned, .expected = -Limit<i64>::s_Max },
        { .current = 0u, .previous = s_MinMagnitude, .expected = Limit<i64>::s_Min },
        { .current = s_MinMagnitude - 1u, .previous = s_MinMagnitude, .expected = -1 },
        { .current = s_MinMagnitude + 5u, .previous = s_MinMagnitude - 5u, .expected = 10 },
        { .current = s_MinMagnitude - 5u, .previous = s_MinMagnitude + 5u, .expected = -10 },
        { .current = Limit<u64>::s_Max, .previous = Limit<u64>::s_Max - 17u, .expected = 17 },
        { .current = Limit<u64>::s_Max - 17u, .previous = Limit<u64>::s_Max, .expected = -17 },
        { .current = 0u, .previous = 128u, .expected = -128 },
    }};
    const Name scopeName(s_MemoryProjectArena);
    const auto makeSnapshot = [scopeName](const u64 counter, const u64 frame)noexcept{
        MemorySnapshot snapshot;
        snapshot.scopeName = scopeName;
        snapshot.frameIndex = frame;
        snapshot.reservedBytes = counter;
        snapshot.usedBytes = counter;
        snapshot.peakUsedBytes = counter;
        snapshot.allocationCount = counter;
        snapshot.reallocationCount = counter;
        snapshot.deallocationCount = counter;
        return snapshot;
    };
    for(const CounterCase& test : s_Cases){
        SCOPED_TRACE(test.current);
        SCOPED_TRACE(test.previous);
        const auto delta = NWB::Core::Perf::Difference(makeSnapshot(test.current, 2u), makeSnapshot(test.previous, 1u));
        ASSERT_TRUE(delta.valid());
        EXPECT_EQ(delta.previousFrameIndex, 1u);
        EXPECT_EQ(delta.currentFrameIndex, 2u);
        EXPECT_EQ(delta.reservedBytes, test.expected);
        EXPECT_EQ(delta.usedBytes, test.expected);
        EXPECT_EQ(delta.peakUsedBytes, test.expected);
        EXPECT_EQ(delta.allocationCount, test.expected);
        EXPECT_EQ(delta.reallocationCount, test.expected);
        EXPECT_EQ(delta.deallocationCount, test.expected);
    }
}

TEST(Telemetry, MemoryDifferenceOmitsUnrepresentableCountersFromTelemetry){
    using MemorySnapshot = NWB::Core::Perf::MemorySnapshot;
    using CounterField = u64 MemorySnapshot::*;
    constexpr Array<CounterField, 6> s_Fields = { &MemorySnapshot::reservedBytes, &MemorySnapshot::usedBytes, &MemorySnapshot::peakUsedBytes, &MemorySnapshot::allocationCount, &MemorySnapshot::reallocationCount, &MemorySnapshot::deallocationCount };
    using CounterCase = NWB::Tests::TelemetryCases::MemoryUnrepresentableCase;
    constexpr u64 s_MinMagnitude = static_cast<u64>(Limit<i64>::s_Max) + 1u;
    constexpr Array<CounterCase, 4> s_Cases = {{
        { .current = s_MinMagnitude, .previous = 0u },
        { .current = 0u, .previous = s_MinMagnitude + 1u },
        { .current = Limit<u64>::s_Max, .previous = 0u },
        { .current = 0u, .previous = Limit<u64>::s_Max },
    }};
    TestArena testArena;
    const Name scopeName(s_MemoryProjectArena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    for(const CounterField field : s_Fields){
        for(const CounterCase& test : s_Cases){
            SCOPED_TRACE(test.current);
            SCOPED_TRACE(test.previous);
            MemorySnapshot previous;
            previous.scopeName = scopeName;
            previous.frameIndex = 1u;
            previous.*field = test.previous;
            MemorySnapshot current;
            current.scopeName = scopeName;
            current.frameIndex = 2u;
            current.*field = test.current;
            const auto delta = NWB::Core::Perf::Difference(current, previous);
            EXPECT_FALSE(delta.valid());
            ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, s_ProjectArenaText, current, delta, payload));
            const auto parsed = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
            ASSERT_TRUE(parsed);
            EXPECT_FALSE(parsed->delta.valid());
            EXPECT_EQ(parsed->snapshot.*field, test.current);
            EXPECT_EQ(parsed->snapshot.frameIndex, 2u);
        }
    }
}

TEST(Telemetry, MemoryReportMarksUnrepresentableTotalsWithoutChangingIndividualRecords){
    using DeltaCase = NWB::Tests::TelemetryCases::MemoryDeltaCase;
    constexpr Array<DeltaCase, 6> s_Cases = {{
        { .first = Limit<i64>::s_Max, .second = 1, .expected = 0, .valid = false },
        { .first = Limit<i64>::s_Min, .second = -1, .expected = 0, .valid = false },
        { .first = Limit<i64>::s_Max - 1, .second = 1, .expected = Limit<i64>::s_Max, .valid = true },
        { .first = Limit<i64>::s_Min + 1, .second = -1, .expected = Limit<i64>::s_Min, .valid = true },
        { .first = Limit<i64>::s_Max, .second = -Limit<i64>::s_Max, .expected = 0, .valid = true },
        { .first = Limit<i64>::s_Min, .second = Limit<i64>::s_Max, .expected = -1, .valid = true },
    }};
    TestArena testArena;
    const Name scopeName(s_MemoryProjectArena);
    for(const DeltaCase& test : s_Cases){
        SCOPED_TRACE(test.first);
        SCOPED_TRACE(test.second);
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        auto snapshot = MakeTestMemorySnapshot(scopeName);
        auto delta = MakeTestMemoryDelta();
        delta.usedBytes = test.first;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        ++snapshot.frameIndex;
        ++delta.previousFrameIndex;
        ++delta.currentFrameIndex;
        delta.usedBytes = test.second;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        const auto report = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
        ASSERT_TRUE(report);
        EXPECT_EQ(report->summary.parseFailureCount, 0u);
        EXPECT_EQ(report->summary.memoryEventCount, 2u);
        const auto& summary = report->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope];
        EXPECT_EQ(summary.eventCount, 2u);
        EXPECT_EQ(summary.maxUsedBytes, snapshot.usedBytes);
        EXPECT_EQ(summary.totalUsedDeltaValid, test.valid);
        const AStringView json(report->json);
        if(test.valid){
            EXPECT_EQ(summary.totalUsedDeltaBytes, test.expected);
            const auto expected = StringFormat(testArena.arena, "\"totalUsedDeltaBytes\": {}", test.expected);
            EXPECT_NE(json.find(AStringView(expected)), AStringView::npos);
            EXPECT_EQ(json.find("\"totalUsedDeltaBytes\": null"), AStringView::npos);
        }
        else{
            EXPECT_NE(json.find("\"totalUsedDeltaBytes\": null"), AStringView::npos);
            ++snapshot.frameIndex;
            ++delta.previousFrameIndex;
            ++delta.currentFrameIndex;
            delta.usedBytes = -test.second;
            ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
            const auto continued = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
            ASSERT_TRUE(continued);
            EXPECT_FALSE(continued->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
            EXPECT_EQ(continued->summary.memoryEventCount, 3u);
        }
        const Array<i64, 2> values = { test.first, test.second };
        for(const i64 value : values){
            const auto expected = StringFormat(testArena.arena, "\"usedBytes\": {}", value);
            EXPECT_NE(json.find(AStringView(expected)), AStringView::npos);
        }

        recorder.clear();
        delta.usedBytes = 0;
        ASSERT_TRUE(Telemetry::RecordPerfMemory(recorder, scopeName, s_ProjectArenaText, snapshot, delta));
        const auto reset = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
        ASSERT_TRUE(reset);
        EXPECT_TRUE(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaValid);
        EXPECT_EQ(reset->summary.memorySources[NWB::Core::Perf::MemorySource::ExplicitScope].totalUsedDeltaBytes, 0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <global/named_registry.h>
#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_RendererFrame = "renderer/frame";
static constexpr AStringView s_RendererFrameText = "Renderer Frame";
static constexpr u32 s_ScopeNameConversionFailure = 0x5C0FEu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct GenericScopeRecord{
    Name name;
    u32 generation = 1u;
};

struct BorrowedScopePointer{
    GenericScopeRecord* record = nullptr;


public:
    [[nodiscard]] GenericScopeRecord* const& get()const noexcept{ return record; }
};

struct ThrowingScopeName{
    explicit operator Name()& noexcept{ return s_NameNone; }

    operator const Name&()const&{ throw s_ScopeNameConversionFailure; }
};

struct ThrowingScopeRecord{
    ThrowingScopeName name;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, PerfTimingPayloadRejectsNonCurrentVersionsAndCorruptMagicAfterValidParse){
    TestArena testArena;
    const Name scopeName{ s_RendererFrame };
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Gpu, scopeName, s_RendererFrameText, stats, payload));

    Expected<Telemetry::PerfTimingPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));

    const Telemetry::TelemetryBytes current = payload;
    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_PerfTimingPayloadVersion + 1u), Limit<u16>::s_Max };
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedPerfTimingPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));
        ASSERT_TRUE((parsed = Telemetry::ParsePerfTimingPayload(current.data(), current.size())));
        EXPECT_EQ(parsed->scopeName, scopeName);
    }
    payload = current;
    payload[0u] = 0u;
    EXPECT_FALSE((parsed = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size())));
}

TEST(Telemetry, PerfPayloadBuildersPreserveAliasedScopeText){
    TestArena testArena;
    const Name scopeName{ s_RendererFrame };
    const NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();
    const auto snapshot = MakeTestMemorySnapshot(scopeName);
    const auto delta = MakeTestMemoryDelta();
    constexpr AStringView s_Text = "borrowed scope inside output";
    Telemetry::TelemetryBytes payload(testArena.arena);

    const auto seed = [&](){
        payload.clear();
        payload.reserve(512u);
        payload.resize(s_Text.size());
        NWB_MEMCPY(payload.data(), payload.size(), s_Text.data(), s_Text.size());
        return AStringView(reinterpret_cast<const char*>(payload.data()), payload.size());
    };

    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, seed(), stats, payload));
    const auto timing = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size());
    ASSERT_TRUE(timing);
    EXPECT_EQ(timing->scopeText, s_Text);

    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, seed(), snapshot, delta, payload));
    const auto memory = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
    ASSERT_TRUE(memory);
    EXPECT_EQ(memory->scopeText, s_Text);

    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, memory->scopeText.substr(9u), stats, payload));
    const auto converted = Telemetry::ParsePerfTimingPayload(payload.data(), payload.size());
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->scopeText, "scope inside output");
    ASSERT_TRUE(Telemetry::BuildPerfMemoryPayload(scopeName, converted->scopeText, snapshot, delta, payload));
    const auto expanded = Telemetry::ParsePerfMemoryPayload(payload.data(), payload.size());
    ASSERT_TRUE(expanded);
    EXPECT_EQ(expanded->scopeText, "scope inside output");
}

TEST(Telemetry, PerfTimingPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    NWB::Core::Perf::TimingStats stats = MakeTestTimingStats();

    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Unknown, Name(s_RendererFrame), s_RendererFrameText, stats, payload));

    stats.sampleCount = 0u;
    EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, Name(s_RendererFrame), s_RendererFrameText, stats, payload));
}

TEST(Telemetry, PerfTimingRejectsNonfiniteDurationsAtBuildAndWireAdmission){
    using TimingStats = NWB::Core::Perf::TimingStats;
    using TimingField = f64 TimingStats::*;
    using WireField = f64 Telemetry::EncodedPerfTimingPayloadHeader::*;
    constexpr Array<TimingField, 4u> s_Fields = { &TimingStats::seconds, &TimingStats::minSeconds, &TimingStats::maxSeconds, &TimingStats::lastSeconds };
    constexpr Array<WireField, 4> s_WireFields = {
        &Telemetry::EncodedPerfTimingPayloadHeader::seconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::minSeconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::maxSeconds,
        &Telemetry::EncodedPerfTimingPayloadHeader::lastSeconds,
    };
    constexpr Array<u64, 3> s_NonfiniteBits = { 0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000001ull };
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::TelemetryBytes current(testArena.arena);
    const Name scopeName(s_RendererFrame);
    const TimingStats validStats = MakeTestTimingStats();
    ASSERT_TRUE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, s_RendererFrameText, validStats, current));
    for(usize field = 0u; field < s_Fields.size(); ++field){
        for(const u64 bits : s_NonfiniteBits){
            SCOPED_TRACE(field);
            SCOPED_TRACE(bits);
            TimingStats invalid = validStats;
            invalid.*s_Fields[field] = BitCast<f64>(bits);
            EXPECT_FALSE(Telemetry::BuildPerfTimingPayload(Telemetry::PerfTimingSource::Cpu, scopeName, s_RendererFrameText, invalid, payload));
            payload = current;
            usize cursor = 0u;
            const auto readHeader = ReadPOD<Telemetry::EncodedPerfTimingPayloadHeader>(current, cursor);
            ASSERT_TRUE(readHeader);
            auto header = *readHeader;
            header.*s_WireFields[field] = BitCast<f64>(bits);
            NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
            EXPECT_FALSE(Telemetry::ParsePerfTimingPayload(payload.data(), payload.size()));
            const auto recovered = Telemetry::ParsePerfTimingPayload(current.data(), current.size());
            ASSERT_TRUE(recovered);
            EXPECT_EQ(recovered->stats.seconds, validStats.seconds);
        }
    }
}

TEST(Telemetry, GenericScopeHelpersPreserveBorrowedPointerValuesAndImplicitNameFailures){
    using NWB::Core::Perf::TimingScopeId;
    GenericScopeRecord record{ Name(NWB::Tests::TelemetryCases::s_TelemetryCasePerfGenericScope), 3u };
    const Array<BorrowedScopePointer, 1u> ownerStorage{ BorrowedScopePointer{ &record } };
    const Span<const BorrowedScopePointer> owners(ownerStorage);
    const TimingScopeId scope{ 0u, 3u };
    static_assert(IsSame_V<decltype(FindNamedScope<TimingScopeId>(owners, scope)), GenericScopeRecord*>);
    static_assert(noexcept(FindNamedScope<TimingScopeId>(owners, scope)));
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, scope), &record);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 0u, 4u }), nullptr);
    EXPECT_EQ(FindNamedScope<TimingScopeId>(owners, TimingScopeId{ 1u, 3u }), nullptr);

    const Array<NotNull<GenericScopeRecord*>, 1u> recordStorage{ MakeNotNull(&record) };
    const Span<const NotNull<GenericScopeRecord*>> records(recordStorage);
    static_assert(noexcept(ScopeNameAt(records, 0u)));
    EXPECT_EQ(ScopeNameAt(records, 0u), record.name);

    ThrowingScopeRecord throwing{};
    const Array<NotNull<ThrowingScopeRecord*>, 1u> throwingRecordStorage{ MakeNotNull(&throwing) };
    const Span<const NotNull<ThrowingScopeRecord*>> throwingRecords(throwingRecordStorage);
    static_assert(noexcept(Name(throwing.name)));
    static_assert(!noexcept(DeclVal<void(*)(Name)noexcept>()(throwing.name)));
    static_assert(!noexcept(ScopeNameAt(throwingRecords, 0u)));
    EXPECT_THROW({ const Name value = ScopeNameAt(throwingRecords, 0u); EXPECT_EQ(value, s_NameNone); }, u32);
}

TEST(Telemetry, PerfTimingViewRejectsIndexAtScopeCount){
    TestArena testArena;
    NWB::Core::Perf::TimingRecorder cpuTiming(testArena.arena);
    cpuTiming.setEnabled(true);
    const auto scopeResult = cpuTiming.registerScope(Name(TestCases::s_TelemetryCasePerfCpuUpdate));
    ASSERT_TRUE(scopeResult);
    const NWB::Core::Perf::TimingScopeId scope = *scopeResult;
    cpuTiming.recordSample(scope, 0.010, 100u);
    cpuTiming.publishFrame(101u);
    const NWB::Core::Perf::TimingView view(cpuTiming);

    ASSERT_EQ(view.scopeCount(), 1u);
    EXPECT_FALSE(view.scopeAt(view.scopeCount()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, RecorderFiltersOwnsCallerPayloadAndRejectsEndIndex){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    const Array<u8, 2> perfPayload = { 1u, 2u };
    EXPECT_FALSE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 12u, perfPayload.data(), sizeof(perfPayload)));
    EXPECT_EQ(recorder.eventCount(), 0u);

    Array<u8, 3> frameGraphPayload = { 4u, 5u, 6u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 13u, frameGraphPayload.data(), sizeof(frameGraphPayload), 7u));
    frameGraphPayload[0u] = 99u;

    const Telemetry::EventView view = recorder.view();
    EXPECT_EQ(view.eventCount(), 1u);

    const Telemetry::EventRecord* record = view.eventAt(0u);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->payload.size(), 3u);
    EXPECT_EQ(record->payload[0u], 4u);

    EXPECT_EQ(view.eventAt(1u), nullptr);
}

TEST(Telemetry, EventCodecRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    Telemetry::EventHeader invalidKindHeader;
    invalidKindHeader.kind = Telemetry::EventKind::Unknown;
    invalidKindHeader.payloadBytes = 0u;
    EXPECT_FALSE(Telemetry::EncodeEvent(invalidKindHeader, nullptr, 0u, encoded));

    const u8 payload = 5u;

    Telemetry::EventHeader validHeader;
    validHeader.kind = Telemetry::EventKind::PerfFrame;
    validHeader.payloadBytes = sizeof(payload);
    EXPECT_FALSE(Telemetry::EncodeEvent(validHeader, nullptr, sizeof(payload), encoded));

    validHeader.payloadBytes = 0u;
    EXPECT_TRUE(Telemetry::EncodeEvent(validHeader, nullptr, 0u, encoded));
    encoded[0u] = 0u;

    auto result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), sizeof(Telemetry::EncodedEventHeader) - 1u);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedHeader);
}

TEST(Telemetry, EventCodecReportsTruncatedPayload){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);

    const Array<u8, 3> payload = { 1u, 2u, 3u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    EXPECT_TRUE(Telemetry::EncodeEvent(header, payload.data(), sizeof(payload), encoded));

    const auto result = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size() - 1u);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedPayload);
}

TEST(Telemetry, EventCodecPreservesAliasedPayloadAcrossCapacityGrowthAndSubranges){
    TestArena testArena;
    constexpr Array<u8, 5> s_Expected = { 17u, 29u, 43u, 71u, 113u };
    const Array<usize, 2> prefixes = { 0u, sizeof(Telemetry::EncodedEventHeader) + 3u };
    for(const usize prefix : prefixes){
        for(const b8 spareCapacity : { false, true }){
            SCOPED_TRACE(prefix);
            SCOPED_TRACE(spareCapacity);
            Telemetry::EventRecord event(testArena.arena);
            event.header.kind = Telemetry::EventKind::PerfFrame;
            event.header.payloadBytes = sizeof(s_Expected);
            event.payload.resize(prefix + sizeof(s_Expected));
            NWB_MEMCPY(event.payload.data() + prefix, sizeof(s_Expected), s_Expected.data(), sizeof(s_Expected));
            if(spareCapacity)
                event.payload.reserve(event.payload.size() + sizeof(Telemetry::EncodedEventHeader));
            if(prefix == 0u){
                if(!spareCapacity)
                    ASSERT_LT(event.payload.capacity(), event.payload.size() + sizeof(Telemetry::EncodedEventHeader));
                ASSERT_TRUE(Telemetry::EncodeEvent(event, event.payload));
            }
            else
                ASSERT_TRUE(Telemetry::EncodeEvent(event.header, event.payload.data() + prefix, sizeof(s_Expected), event.payload));

            const auto decoded = Telemetry::DecodeEvent(testArena.arena, event.payload.data(), event.payload.size());
            ASSERT_TRUE(decoded);
            ASSERT_EQ(decoded->event.payload.size(), sizeof(s_Expected));
            EXPECT_EQ(NWB_MEMCMP(decoded->event.payload.data(), s_Expected.data(), sizeof(s_Expected)), 0);
        }
    }
}

TEST(Telemetry, EventStreamCodecHandlesEmptyStreams){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    EXPECT_EQ(encoded.size(), sizeof(Telemetry::EncodedStreamHeader));

    Telemetry::Recorder decoded(testArena.arena);
    const auto result = Telemetry::DecodeEventStream(encoded.data(), encoded.size(), testArena.arena, decoded);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, encoded.size());
    EXPECT_EQ(decoded.eventCount(), 0u);
}

TEST(Telemetry, EventStreamCanDecodeBytesBorrowedFromDestinationRecorder){
    TestArena testArena;
    Telemetry::Recorder source(testArena.arena);
    source.setCaptureOptions(Telemetry::CaptureOptions::All());
    const Array<u8, 2u> first = { 7u, 8u };
    const Array<u8, 2u> second = { 9u, 10u };
    ASSERT_TRUE(source.recordBinary(Telemetry::EventKind::PerfFrame, 1u, first.data(), first.size()));
    ASSERT_TRUE(source.recordBinary(Telemetry::EventKind::PerfFrame, 2u, second.data(), second.size()));
    Telemetry::TelemetryBytes encoded(testArena.arena);
    ASSERT_TRUE(Telemetry::EncodeEventStream(source.view(), encoded));

    Telemetry::TelemetryBytes wrapped(testArena.arena);
    wrapped.resize(encoded.size() + 3u);
    NWB_MEMCPY(wrapped.data() + 3u, encoded.size(), encoded.data(), encoded.size());
    for(const b8 captureEnabled : { false, true }){
        SCOPED_TRACE(captureEnabled);
        Telemetry::Recorder destination(testArena.arena);
        if(captureEnabled)
            destination.setCaptureOptions(Telemetry::CaptureOptions::All());
        Telemetry::EventHeader envelope;
        envelope.kind = Telemetry::EventKind::PerfFrame;
        envelope.payloadBytes = wrapped.size();
        ASSERT_TRUE(destination.append(envelope, wrapped.data(), wrapped.size()));
        const Telemetry::EventRecord* borrowed = destination.view().eventAt(0u);
        ASSERT_NE(borrowed, nullptr);
        ASSERT_TRUE(Telemetry::DecodeEventStream(borrowed->payload.data() + 3u, encoded.size(), testArena.arena, destination));
        ASSERT_EQ(destination.eventCount(), 2u);
        const Telemetry::EventRecord* decoded = destination.view().eventAt(1u);
        ASSERT_NE(decoded, nullptr);
        ASSERT_EQ(decoded->payload.size(), second.size());
        EXPECT_EQ(decoded->payload[0u], second[0u]);
        EXPECT_EQ(decoded->payload[1u], second[1u]);
    }
}

TEST(Telemetry, EventStreamCodecRejectsInvalidInput){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());

    const Array<u8, 2> payload = { 7u, 8u };
    EXPECT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload.data(), sizeof(payload)));

    Telemetry::TelemetryBytes encoded(testArena.arena);
    EXPECT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));

    Telemetry::Recorder decoded(testArena.arena);
    auto result = Telemetry::DecodeEventStream(encoded.data(), sizeof(Telemetry::EncodedStreamHeader) - 1u, testArena.arena, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedHeader);

    Telemetry::TelemetryBytes corrupted(testArena.arena);
    corrupted = encoded;
    corrupted[0u] = 0u;
    result = Telemetry::DecodeEventStream(corrupted.data(), corrupted.size(), testArena.arena, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);

    result = Telemetry::DecodeEventStream(encoded.data(), encoded.size() - 1u, testArena.arena, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::TruncatedPayload);

    corrupted = encoded;
    Telemetry::EncodedStreamHeader streamHeader;
    NWB_MEMCPY(&streamHeader, sizeof(streamHeader), corrupted.data(), sizeof(streamHeader));
    streamHeader.eventCount = 0u;
    NWB_MEMCPY(corrupted.data(), corrupted.size(), &streamHeader, sizeof(streamHeader));
    result = Telemetry::DecodeEventStream(corrupted.data(), corrupted.size(), testArena.arena, decoded);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().status, Telemetry::DecodeStatus::InvalidHeader);
}

TEST(Telemetry, EventCodecRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    Telemetry::TelemetryBytes encoded(testArena.arena);
    const Array<u8, 2> payload = { 7u, 8u };
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = sizeof(payload);
    ASSERT_TRUE(Telemetry::EncodeEvent(header, payload.data(), sizeof(payload), encoded));
    const Telemetry::TelemetryBytes current = encoded;

    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_TelemetryFormatVersion + 1u), Limit<u16>::s_Max, };
    Expected<Telemetry::DecodedEvent, Telemetry::DecodeResult> decoded = MakeUnexpected(Telemetry::DecodeResult{});
    Telemetry::Recorder recorder(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        EXPECT_FALSE(Telemetry::EncodeEvent(header, payload.data(), sizeof(payload), encoded));
        EXPECT_EQ(encoded, current);
        EXPECT_FALSE(recorder.append(header, payload.data(), sizeof(payload)));
        EXPECT_EQ(recorder.eventCount(), 0u);

        usize cursor = 0u;
        const auto readEncodedHeader = ReadPOD<Telemetry::EncodedEventHeader>(current, cursor);
        ASSERT_TRUE(readEncodedHeader);
        Telemetry::EncodedEventHeader encodedHeader = *readEncodedHeader;
        encodedHeader.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &encodedHeader, sizeof(encodedHeader));
        decoded = Telemetry::DecodeEvent(testArena.arena, encoded.data(), encoded.size());
        ASSERT_FALSE(decoded);
        EXPECT_EQ(decoded.error().status, Telemetry::DecodeStatus::InvalidHeader);
        encoded = current;
        ASSERT_TRUE((decoded = Telemetry::DecodeEvent(testArena.arena, current.data(), current.size())));
        ASSERT_EQ(decoded->event.payload.size(), sizeof(payload));
        EXPECT_EQ(decoded->event.payload[0u], payload[0u]);
    }
    header.version = Telemetry::s_TelemetryFormatVersion;
    ASSERT_TRUE(recorder.append(header, payload.data(), sizeof(payload)));
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, EventStreamRejectsNonCurrentStreamAndNestedEventVersionsAndRecovers){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Array<u8, 2> payload = { 7u, 8u };
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload.data(), sizeof(payload)));
    Telemetry::TelemetryBytes encoded(testArena.arena);
    ASSERT_TRUE(Telemetry::EncodeEventStream(recorder.view(), encoded));
    const Telemetry::TelemetryBytes current = encoded;

    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_TelemetryFormatVersion + 1u), Limit<u16>::s_Max, };
    Telemetry::Recorder decoded(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        usize cursor = 0u;
        const auto readStreamHeader = ReadPOD<Telemetry::EncodedStreamHeader>(current, cursor);
        ASSERT_TRUE(readStreamHeader);
        Telemetry::EncodedStreamHeader streamHeader = *readStreamHeader;
        streamHeader.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &streamHeader, sizeof(streamHeader));
        const auto invalidStream = Telemetry::DecodeEventStream(encoded.data(), encoded.size(), testArena.arena, decoded);
        ASSERT_FALSE(invalidStream);
        EXPECT_EQ(invalidStream.error().status, Telemetry::DecodeStatus::InvalidHeader);
        EXPECT_EQ(decoded.eventCount(), 0u);

        encoded = current;
        const auto readEventHeader = ReadPOD<Telemetry::EncodedEventHeader>(current, cursor);
        ASSERT_TRUE(readEventHeader);
        Telemetry::EncodedEventHeader eventHeader = *readEventHeader;
        eventHeader.version = version;
        NWB_MEMCPY(encoded.data() + sizeof(streamHeader), encoded.size() - sizeof(streamHeader), &eventHeader, sizeof(eventHeader));
        const auto invalidEvent = Telemetry::DecodeEventStream(encoded.data(), encoded.size(), testArena.arena, decoded);
        ASSERT_FALSE(invalidEvent);
        EXPECT_EQ(invalidEvent.error().status, Telemetry::DecodeStatus::InvalidHeader);
        EXPECT_EQ(decoded.eventCount(), 0u);
        encoded = current;
        ASSERT_TRUE(Telemetry::DecodeEventStream(current.data(), current.size(), testArena.arena, decoded));
        ASSERT_EQ(decoded.eventCount(), 1u);
        const Telemetry::EventRecord* record = decoded.view().eventAt(0u);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->payload.size(), sizeof(payload));
    }
}

TEST(Telemetry, DiagnosticPayloadRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    DiagnosticEventRecord record;
    record.event = "assert";
    record.message = "retained diagnostic";
    Telemetry::TelemetryBytes encoded(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildDiagnosticPayload(record, testArena.arena, encoded));
    const Telemetry::TelemetryBytes current = encoded;
    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_DiagnosticPayloadVersion + 1u), Limit<u16>::s_Max, };
    Expected<Telemetry::DiagnosticPayload> parsed = MakeUnexpected(Failure{});
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(current.data(), current.size())));
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedDiagnosticPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedDiagnosticPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(encoded.data(), encoded.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseDiagnosticPayload(encoded.data(), encoded.size())));
        ASSERT_TRUE((parsed = Telemetry::ParseDiagnosticPayload(current.data(), current.size())));
        EXPECT_EQ(AStringView(parsed->message), record.message);
    }
}

TEST(Telemetry, RecorderAcceptsConcurrentRecords){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_EventsPerThread = 64u;
    Array<Thread, s_ThreadCount> threads;
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        threads[threadIndex] = Thread([&recorder, threadIndex](){
            for(u32 eventIndex = 0u; eventIndex < s_EventsPerThread; ++eventIndex){
                if(!Telemetry::RecordTextLog(recorder, NWB::Core::Common::LogType::Info, NWB_TEXT("concurrent telemetry record"), eventIndex, threadIndex))
                    return;
            }
        });
    }

    for(Thread& thread : threads)
        thread.join();

    EXPECT_EQ(recorder.eventCount(), s_ThreadCount * s_EventsPerThread);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder_reuse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct PayloadBuildFailure{};

[[nodiscard]] b8 RecordFilledPayload(Telemetry::Recorder& recorder, const usize byteCount, const u8 value){
    return recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, [byteCount, value](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
        EXPECT_TRUE(payload.empty());
        payload.resize(byteCount, value);
        return true;
    });
}

void RunConcurrentReset(const b8 disableCapture){
    constexpr usize s_WorkerCount = 4u;
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        Atomic<usize> accepted{ 0u };
        Atomic<usize> damagedPayloads{ 0u };
        Latch payloadsReady(s_WorkerCount);
        Latch releasePayloads(1u);
        Array<Thread, s_WorkerCount> workers;
        for(usize index = 0u; index < s_WorkerCount; ++index){
            workers[index] = Thread([&, index](){
                b8 enteredBuilder = false;
                const b8 recorded = recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 42u, static_cast<u32>(index), [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
                    enteredBuilder = true;
                    payload.resize(256u, static_cast<u8>(index + 1u));
                    payloadsReady.count_down();
                    releasePayloads.wait();
                    for(const u8 value : payload){
                        if(value != index + 1u){
                            damagedPayloads.fetch_add(1u, MemoryOrder::relaxed);
                            break;
                        }
                    }
                    return true;
                });
                if(!enteredBuilder)
                    payloadsReady.count_down();
                if(recorded)
                    accepted.fetch_add(1u, MemoryOrder::relaxed);
            });
        }
        payloadsReady.wait();
        recorder.clear();
        const u8 marker = 99u;
        const b8 markerRecorded = recorder.recordBinary(Telemetry::EventKind::PerfFrame, 7u, &marker, sizeof(marker), 99u);
        if(disableCapture)
            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        releasePayloads.count_down();
        for(Thread& worker : workers)
            worker.join();

        EXPECT_TRUE(markerRecorded);
        EXPECT_EQ(damagedPayloads.load(MemoryOrder::relaxed), 0u);
        EXPECT_EQ(accepted.load(MemoryOrder::relaxed), disableCapture ? 0u : s_WorkerCount);
        EXPECT_EQ(recorder.eventCount(), disableCapture ? 0u : s_WorkerCount + 1u);
        if(!disableCapture){
            Array<b8, s_WorkerCount> seen = {};
            for(usize index = 1u; index < recorder.eventCount(); ++index){
                const auto* event = recorder.view().eventAt(index);
                ASSERT_NE(event, nullptr);
                ASSERT_LT(event->header.streamId, s_WorkerCount);
                EXPECT_FALSE(seen[event->header.streamId]);
                seen[event->header.streamId] = true;
                ASSERT_EQ(event->payload.size(), 256u);
                EXPECT_EQ(event->payload.front(), event->header.streamId + 1u);
                EXPECT_EQ(event->payload.back(), event->header.streamId + 1u);
            }
            for(const b8 recorded : seen)
                EXPECT_TRUE(recorded);
        }
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.usedBytes, initial.usedBytes);
    EXPECT_EQ(after.reservedBytes, initial.reservedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, RecorderDisableReleasesClearedAndEmptyPayloadStorage){
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 11u));
        recorder.clear();
        const ArenaMemoryStats warmed = testArena.arena.memoryStats();

        ASSERT_TRUE(RecordFilledPayload(recorder, 0u, 0u));
        EXPECT_TRUE(recorder.view().eventAt(0u)->payload.empty());
        recorder.clear();
        recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        EXPECT_LT(testArena.arena.memoryStats().usedBytes, warmed.usedBytes);
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.usedBytes, initial.usedBytes);
    EXPECT_EQ(after.reservedBytes, initial.reservedBytes);
}

TEST(Telemetry, RecorderBuilderCanClearAndReenterWhileItsPayloadIsLeased){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 3u));
    ASSERT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 2u, 7u, [&](Telemetry::TelemetryArena& arena, Telemetry::TelemetryBytes& payload){
        EXPECT_EQ(&arena, &testArena.arena);
        EXPECT_TRUE(payload.empty());
        payload.resize(128u, 42u);
        const u8* const leasedData = payload.data();
        recorder.clear();
        EXPECT_TRUE(recorder.captureOptions().perfEnabled());
        EXPECT_EQ(recorder.eventCount(), 0u);
        const b8 nested = recorder.recordBinary(Telemetry::EventKind::PerfFrame, 1u, payload.data(), payload.size(), 5u);
        EXPECT_TRUE(nested);
        EXPECT_EQ(payload.data(), leasedData);
        for(const u8 value : payload)
            EXPECT_EQ(value, 42u);
        return nested;
    }));
    ASSERT_EQ(recorder.eventCount(), 2u);
    const auto* nested = recorder.view().eventAt(0u);
    const auto* outer = recorder.view().eventAt(1u);
    ASSERT_NE(nested, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(nested->header.streamId, 5u);
    EXPECT_EQ(outer->header.streamId, 7u);
    EXPECT_NE(nested->payload.data(), outer->payload.data());
    ASSERT_EQ(nested->payload.size(), 128u);
    ASSERT_EQ(outer->payload.size(), 128u);
    EXPECT_EQ(NWB_MEMCMP(nested->payload.data(), outer->payload.data(), 128u), 0);
}

TEST(Telemetry, RecorderDisableDuringBuildRejectsPublicationAndReleasesLease){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    EXPECT_FALSE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
        payload.resize(256u, 19u);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        EXPECT_EQ(payload.front(), 19u);
        payload.back() = 37u;
        EXPECT_EQ(payload.back(), 37u);
        return true;
    }));
    EXPECT_EQ(recorder.eventCount(), 0u);
    const ArenaMemoryStats disabled = testArena.arena.memoryStats();
    EXPECT_EQ(disabled.usedBytes, before.usedBytes);
    b8 builderCalled = false;
    EXPECT_FALSE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 2u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes&)noexcept{ builderCalled = true; return true; }));
    EXPECT_FALSE(builderCalled);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, disabled.allocationCount);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    EXPECT_TRUE(RecordFilledPayload(recorder, 64u, 23u));
}

TEST(Telemetry, RecorderPublicationUsesCaptureOptionsAtBuilderCompletion){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
        payload.resize(64u, 71u);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        return true;
    }));
    ASSERT_EQ(recorder.eventCount(), 1u);
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.back(), 71u);
    EXPECT_FALSE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 2u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){
        payload.resize(64u, 29u);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
        return true;
    }));
    EXPECT_EQ(recorder.eventCount(), 1u);
}

TEST(Telemetry, RecorderFailedAndThrowingBuildersRecycleWithoutAllocations){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 13u));
    recorder.clear();
    const ArenaMemoryStats warmed = testArena.arena.memoryStats();
    EXPECT_FALSE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, [](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload){ payload.resize(128u, 31u); return false; }));
    EXPECT_THROW(EXPECT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 2u, 0u, [](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload)->b8{
                payload.resize(128u, 47u);
                throw PayloadBuildFailure{};
            })), PayloadBuildFailure
    );
    EXPECT_EQ(recorder.eventCount(), 0u);
    ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 59u));
    recorder.clear();
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.allocationCount, warmed.allocationCount);
    EXPECT_EQ(after.reallocationCount, warmed.reallocationCount);
    EXPECT_EQ(after.deallocationCount, warmed.deallocationCount);
    EXPECT_EQ(after.usedBytes, warmed.usedBytes);
}

TEST(Telemetry, RecorderActiveEventAliasesRemainStableAcrossStorageGrowth){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    const Array<u8, 4> bytes = { 3u, 7u, 11u, 19u };
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 5u, bytes.data(), sizeof(bytes)));
    const auto* source = recorder.view().eventAt(0u);
    ASSERT_NE(source, nullptr);
    const u8* const sourceData = source->payload.data();
    for(u32 index = 0u; index < 128u; ++index){
        ASSERT_TRUE(recorder.append(source->header, source->payload.data(), source->payload.size()));
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, index, sourceData, sizeof(bytes)));
        Telemetry::TelemetryBytes ownedPayload(testArena.arena);
        ownedPayload.assign(source->payload.begin(), source->payload.end());
        const u8* const movedData = ownedPayload.data();
        ASSERT_TRUE(recorder.append(source->header, Move(ownedPayload)));
        EXPECT_EQ(recorder.view().eventAt(recorder.eventCount() - 1u)->payload.data(), movedData);
        EXPECT_TRUE(ownedPayload.empty());
        EXPECT_EQ(recorder.view().eventAt(0u), source);
        EXPECT_EQ(source->payload.data(), sourceData);
    }
    ASSERT_EQ(recorder.eventCount(), 385u);
    for(usize index = 0u; index < recorder.eventCount(); ++index){
        const auto* event = recorder.view().eventAt(index);
        ASSERT_NE(event, nullptr);
        ASSERT_EQ(event->payload.size(), sizeof(bytes));
        EXPECT_EQ(NWB_MEMCMP(event->payload.data(), bytes.data(), sizeof(bytes)), 0);
    }
}

TEST(Telemetry, RecorderReusedSlotsKeepForeignPayloadsIndependentAndMoveOwnedPayloads){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 17u));
    recorder.clear();
    {
        TestArena foreignArena;
        Telemetry::TelemetryBytes foreignPayload(foreignArena.arena);
        foreignPayload.resize(64u, 41u);
        const u8* const sourceData = foreignPayload.data();
        ASSERT_TRUE(recorder.recordPayload(Telemetry::EventKind::PerfFrame, 1u, Move(foreignPayload)));
        const auto* event = recorder.view().eventAt(0u);
        ASSERT_NE(event, nullptr);
        EXPECT_NE(event->payload.data(), sourceData);
        EXPECT_EQ(foreignPayload.data(), sourceData);
        ASSERT_EQ(foreignPayload.size(), 64u);
        foreignPayload.front() = 99u;
        EXPECT_EQ(event->payload.front(), 41u);
    }
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.back(), 41u);
    recorder.clear();
    Telemetry::TelemetryBytes ownedPayload(testArena.arena);
    ownedPayload.resize(128u, 67u);
    const u8* const ownedData = ownedPayload.data();
    ASSERT_TRUE(recorder.recordPayload(Telemetry::EventKind::PerfFrame, 2u, Move(ownedPayload)));
    EXPECT_EQ(recorder.view().eventAt(0u)->payload.data(), ownedData);
    EXPECT_TRUE(ownedPayload.empty());
    recorder.clear();
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::PerfFrame, 3u, nullptr, 0u));
    EXPECT_TRUE(recorder.view().eventAt(0u)->payload.empty());
}

TEST(Telemetry, RecorderRejectsInvalidInputsWithoutConsumingPayloadsOrReusedSlots){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 5u));
    recorder.clear();
    Telemetry::TelemetryBytes payload(testArena.arena);
    payload.resize(64u, 17u);
    const u8* const payloadData = payload.data();
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    const Array<Telemetry::EventKind::Enum, 2> invalidKinds = { Telemetry::EventKind::Unknown, static_cast<Telemetry::EventKind::Enum>(0xffffu), };
    b8 builderCalled = false;
    for(const auto kind : invalidKinds){
        EXPECT_FALSE(recorder.recordBuiltPayload(kind, 1u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes&)noexcept{ builderCalled = true; return true; }));
        Telemetry::EventHeader header;
        header.kind = kind;
        header.payloadBytes = payload.size();
        EXPECT_FALSE(recorder.append(header, Move(payload)));
        EXPECT_FALSE(recorder.append(header, payload.data(), payload.size()));
    }
    Telemetry::EventHeader header;
    header.kind = Telemetry::EventKind::PerfFrame;
    header.payloadBytes = payload.size();
    header.magic = 0u;
    EXPECT_FALSE(recorder.append(header, Move(payload)));
    header.magic = Telemetry::s_EventMagic;
    EXPECT_FALSE(recorder.append(header, nullptr, payload.size()));
    ++header.payloadBytes;
    EXPECT_FALSE(recorder.append(header, Move(payload)));
    EXPECT_FALSE(builderCalled);
    EXPECT_EQ(recorder.eventCount(), 0u);
    EXPECT_EQ(payload.data(), payloadData);
    EXPECT_EQ(payload.size(), 64u);
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
    ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 23u));
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
}

TEST(Telemetry, RecorderBuilderForeignMovesKeepOwnershipWithinEachArena){
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 256u, 7u));
        recorder.clear();
        {
            TestArena foreignArena;
            const ArenaMemoryStats foreignInitial = foreignArena.arena.memoryStats();
            {
                Telemetry::TelemetryBytes foreignPayload(foreignArena.arena);
                foreignPayload.resize(64u, 83u);
                ASSERT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 3u, 0u, [&](Telemetry::TelemetryArena&, Telemetry::TelemetryBytes& payload)noexcept{
                    payload = Move(foreignPayload);
                    return true;
                }));
            }
            EXPECT_EQ(foreignArena.arena.memoryStats().usedBytes, foreignInitial.usedBytes);
        }
        ASSERT_EQ(recorder.eventCount(), 1u);
        const auto* event = recorder.view().eventAt(0u);
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->payload.get_allocator().arenaPtr(), &testArena.arena);
        ASSERT_EQ(event->payload.size(), 64u);
        EXPECT_EQ(event->payload.front(), 83u);
        EXPECT_EQ(event->payload.back(), 83u);
        recorder.clear();
        ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 29u));
    }
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, initial.usedBytes);
}

TEST(Telemetry, RecorderExplicitForeignBuilderStorageIsCopiedOrReleasedOnEveryExit){
    using Payload = Telemetry::TelemetryBytes;
    TestArena testArena;
    const ArenaMemoryStats initial = testArena.arena.memoryStats();
    {
        Telemetry::Recorder recorder(testArena.arena);
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        for(u32 outcome = 0u; outcome < 4u; ++outcome){
            recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
            recorder.clear();
            {
                TestArena foreignArena;
                const ArenaMemoryStats foreignInitial = foreignArena.arena.memoryStats();
                {
                    Payload foreignPayload(foreignArena.arena);
                    foreignPayload.resize(64u, 37u);
                    const auto buildPayload = [&](Telemetry::TelemetryArena&, Payload& payload)->b8{
                        // Explicit move construction replaces allocator identity, independently of assignment policy.
                        payload.~Payload();
                        new(&payload) Payload(Move(foreignPayload));
                        EXPECT_EQ(payload.get_allocator().arenaPtr(), &foreignArena.arena);
                        if(outcome == 2u)
                            throw PayloadBuildFailure{};
                        if(outcome == 3u)
                            recorder.setCaptureOptions(Telemetry::CaptureOptions::Disabled());
                        return outcome != 1u;
                    };
                    if(outcome == 2u){
                        EXPECT_THROW(EXPECT_TRUE(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, buildPayload)), PayloadBuildFailure);
                    }
                    else{
                        EXPECT_EQ(recorder.recordBuiltPayload(Telemetry::EventKind::PerfFrame, 1u, 0u, buildPayload), outcome == 0u);
                    }
                }
                EXPECT_EQ(foreignArena.arena.memoryStats().usedBytes, foreignInitial.usedBytes);
            }
            ASSERT_EQ(recorder.eventCount(), outcome == 0u ? 1u : 0u);
            if(outcome == 0u){
                const auto* event = recorder.view().eventAt(0u);
                ASSERT_NE(event, nullptr);
                EXPECT_EQ(event->payload.get_allocator().arenaPtr(), &testArena.arena);
                ASSERT_EQ(event->payload.size(), 64u);
                EXPECT_EQ(event->payload.front(), 37u);
                EXPECT_EQ(event->payload.back(), 37u);
            }
        }
        recorder.setCaptureOptions(Telemetry::CaptureOptions::PerfOnly());
        ASSERT_TRUE(RecordFilledPayload(recorder, 64u, 59u));
    }
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, initial.usedBytes);
}

TEST(Telemetry, RecorderConcurrentClearPreservesExclusivelyLeasedPayloads){
    RunConcurrentReset(false);
}

TEST(Telemetry, RecorderConcurrentDisableWaitsForLeaseReleaseWithoutPublishing){
    RunConcurrentReset(true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/alloc/scratch.h>

#include <global/floating_point_environment.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_report_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReportArenaName = "tests/telemetry/report_work";

static constexpr AStringView s_GBuffer = "gbuffer";
static constexpr AStringView s_RuntimeStatisticsNone = "runtime_statistics=\"none\"";
static constexpr AStringView s_RuntimeStatisticsNullJson = "\"runtimeStatistics\": null";
static constexpr AStringView s_ResourceWithNullStatisticsJson = "\"kind\": \"resource\", \"flags\": 0, \"queueAssignment\": null, \"compiledTask\": null, \"runtimeStatistics\": null";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, TelemetryReportKeepsCaptureOrderAndCorrelatesTimingByFrame){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    const Name gbufferScopeName{s_GBuffer};
    NWB::Core::Perf::TimingStats firstTiming = MakeTestTimingStats();
    firstTiming.seconds = 0.041;
    firstTiming.sampleCount = 1u;
    firstTiming.publishFrameIndex = 41u;
    firstTiming.firstSampleFrameIndex = 40u;
    firstTiming.lastSampleFrameIndex = 40u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, gbufferScopeName, "gbuffer", firstTiming, 70u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 40u, nodes, edges, 7u));

    NWB::Core::Perf::TimingStats secondTiming = MakeTestTimingStats();
    secondTiming.seconds = 0.042;
    secondTiming.sampleCount = 1u;
    secondTiming.publishFrameIndex = 42u;
    secondTiming.firstSampleFrameIndex = 41u;
    secondTiming.lastSampleFrameIndex = 41u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, gbufferScopeName, "gbuffer", secondTiming, 80u));

    nodes[0u].label = "Second GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 41u, nodes, edges, 8u));

    nodes[0u].label = "Unmatched GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 42u, nodes, edges, 9u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult1 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult1);
    report = Move(*reportResult1);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 3u);

    const AStringView json(report.json.data(), report.json.size());
    const usize firstJsonGraph = json.find("\"frameIndex\": 40");
    const usize secondJsonGraph = json.find("\"frameIndex\": 41");
    const usize thirdJsonGraph = json.find("\"frameIndex\": 42");
    ASSERT_NE(firstJsonGraph, AStringView::npos);
    ASSERT_NE(secondJsonGraph, AStringView::npos);
    ASSERT_NE(thirdJsonGraph, AStringView::npos);
    EXPECT_LT(firstJsonGraph, secondJsonGraph);
    EXPECT_LT(secondJsonGraph, thirdJsonGraph);

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstDotGraph = dot.find("digraph frame_graph_40_7_0");
    const usize secondDotGraph = dot.find("digraph frame_graph_41_8_1");
    const usize thirdDotGraph = dot.find("digraph frame_graph_42_9_2");
    ASSERT_NE(firstDotGraph, AStringView::npos);
    ASSERT_NE(secondDotGraph, AStringView::npos);
    ASSERT_NE(thirdDotGraph, AStringView::npos);
    EXPECT_LT(firstDotGraph, secondDotGraph);
    EXPECT_LT(secondDotGraph, thirdDotGraph);
    EXPECT_EQ(dot.find("digraph frame_graph_", thirdDotGraph + 1u), AStringView::npos);
    const AStringView firstDotRecord = dot.substr(firstDotGraph, secondDotGraph - firstDotGraph);
    const AStringView secondDotRecord = dot.substr(secondDotGraph, thirdDotGraph - secondDotGraph);
    const AStringView thirdDotRecord = dot.substr(thirdDotGraph);
    EXPECT_TRUE(Contains(firstDotRecord, "GBuffer Pass\\n41.000 ms"));
    EXPECT_TRUE(Contains(secondDotRecord, "Second GBuffer Pass\\n42.000 ms"));
    EXPECT_TRUE(Contains(thirdDotRecord, "Unmatched GBuffer Pass"));
    EXPECT_FALSE(Contains(thirdDotRecord, " ms"));
}

TEST(Telemetry, TelemetryReportDoesNotAttachAggregatedTimingToOneGraph){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Core::Perf::TimingStats aggregatedTiming = MakeTestTimingStats();
    aggregatedTiming.seconds = 0.043;
    aggregatedTiming.sampleCount = 2u;
    aggregatedTiming.publishFrameIndex = 44u;
    aggregatedTiming.firstSampleFrameIndex = 43u;
    aggregatedTiming.lastSampleFrameIndex = 43u;
    ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, Telemetry::PerfTimingSource::Gpu, Name(s_GBuffer), "gbuffer", aggregatedTiming, 70u));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 43u, nodes, edges, 7u));
    nodes[0u].label = "Publish Frame GBuffer Pass";
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 44u, nodes, edges, 8u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult2 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult2);
    report = Move(*reportResult2);
    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_TRUE(Contains(dot, "digraph frame_graph_43_7_0"));
    EXPECT_TRUE(Contains(dot, "digraph frame_graph_44_8_1"));
    EXPECT_FALSE(Contains(dot, " ms"));
    EXPECT_TRUE(Contains(AStringView(report.perfCsv.data(), report.perfCsv.size()), "gpu,gbuffer"));
}

TEST(Telemetry, TelemetryReportDistinguishesExactEmptyPacketSubmissionsFromAbsentStatistics){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(testArena.arena, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics);
    nodes[0u].runtimeStatistics.submission = {};
    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics.clear();

    Telemetry::Recorder exactRecorder(testArena.arena);
    exactRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(exactRecorder, testArena.scratchArena, 58u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, 18u));
    Log::TelemetryReport exactReport(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult3 = Log::BuildTelemetryReport(exactRecorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult3);
    exactReport = Move(*reportResult3);
    const AStringView exactJson(exactReport.json.data(), exactReport.json.size());
    const AStringView exactDot(exactReport.graph.data(), exactReport.graph.size());
    EXPECT_TRUE(Contains(exactJson, "\"packetSubmissions\": []"));
    EXPECT_TRUE(Contains(exactDot, "runtime_packet_submission_count=0"));

    Telemetry::Recorder absentRecorder(testArena.arena);
    absentRecorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(absentRecorder, testArena.scratchArena, 59u, nodes, edges, 19u));
    Log::TelemetryReport absentReport(testArena.arena);
    auto reportResult4 = Log::BuildTelemetryReport(absentRecorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult4);
    absentReport = Move(*reportResult4);
    const AStringView absentJson(absentReport.json.data(), absentReport.json.size());
    const AStringView absentDot(absentReport.graph.data(), absentReport.graph.size());
    EXPECT_TRUE(Contains(absentJson, "\"packetSubmissions\": null"));
    EXPECT_TRUE(Contains(absentDot, "runtime_packet_submission_count=\"unknown\""));
}

TEST(Telemetry, TelemetryReportRejectsNonCurrentFrameGraphPayloads){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(55u, nodes, edges, testArena.scratchArena, payload));

    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 55u, nodes, edges, 15u));

    const Array<u16, 12> unsupportedVersions = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, static_cast<u16>(Telemetry::s_FrameGraphPayloadVersion + 1u), Limit<u16>::s_Max, };
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 55u, payload.data(), payload.size(), 15u));
    }

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult5 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult5);
    report = Move(*reportResult5);
    EXPECT_EQ(report.summary.eventCount, LengthOf(unsupportedVersions) + 1u);
    EXPECT_EQ(report.summary.parseFailureCount, LengthOf(unsupportedVersions));
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_TRUE(Contains(AStringView(report.graph.data(), report.graph.size()), "GBuffer Pass"));
}

TEST(Telemetry, TelemetryReportMarksAbsentRuntimeStatistics){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 53u, nodes, edges, 13u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult6 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult6);
    report = Move(*reportResult6);

    const AStringView json(report.json.data(), report.json.size());
    const usize firstPassJsonOffset = json.find("\"label\": \"GBuffer Pass\"");
    const usize resourceJsonOffset = json.find("\"label\": \"Albedo Texture\"");
    const usize secondPassJsonOffset = json.find("\"label\": \"Lighting Pass\"");
    const usize jsonEdgesOffset = json.find("\"edges\": [", secondPassJsonOffset);
    ASSERT_NE(firstPassJsonOffset, AStringView::npos);
    ASSERT_NE(resourceJsonOffset, AStringView::npos);
    ASSERT_NE(secondPassJsonOffset, AStringView::npos);
    ASSERT_NE(jsonEdgesOffset, AStringView::npos);
    ASSERT_LT(firstPassJsonOffset, resourceJsonOffset);
    ASSERT_LT(resourceJsonOffset, secondPassJsonOffset);
    ASSERT_LT(secondPassJsonOffset, jsonEdgesOffset);

    const AStringView firstPassJson = json.substr(firstPassJsonOffset, resourceJsonOffset - firstPassJsonOffset);
    const AStringView resourceJson = json.substr(resourceJsonOffset, secondPassJsonOffset - resourceJsonOffset);
    const AStringView secondPassJson = json.substr(secondPassJsonOffset, jsonEdgesOffset - secondPassJsonOffset);
    EXPECT_TRUE(Contains(firstPassJson, s_RuntimeStatisticsNullJson));
    EXPECT_TRUE(Contains(resourceJson, s_ResourceWithNullStatisticsJson));
    EXPECT_TRUE(Contains(secondPassJson, s_RuntimeStatisticsNullJson));

    const AStringView dot(report.graph.data(), report.graph.size());
    const usize firstPassDotOffset = dot.find("  n0 [");
    const usize resourceDotOffset = dot.find("  n1 [");
    const usize secondPassDotOffset = dot.find("  n2 [");
    const usize dotEdgesOffset = dot.find("  n0 ->", secondPassDotOffset);
    ASSERT_NE(firstPassDotOffset, AStringView::npos);
    ASSERT_NE(resourceDotOffset, AStringView::npos);
    ASSERT_NE(secondPassDotOffset, AStringView::npos);
    ASSERT_NE(dotEdgesOffset, AStringView::npos);
    ASSERT_LT(firstPassDotOffset, resourceDotOffset);
    ASSERT_LT(resourceDotOffset, secondPassDotOffset);
    ASSERT_LT(secondPassDotOffset, dotEdgesOffset);

    const AStringView firstPassDot = dot.substr(firstPassDotOffset, resourceDotOffset - firstPassDotOffset);
    const AStringView resourceDot = dot.substr(resourceDotOffset, secondPassDotOffset - resourceDotOffset);
    const AStringView secondPassDot = dot.substr(secondPassDotOffset, dotEdgesOffset - secondPassDotOffset);
    EXPECT_TRUE(Contains(firstPassDot, s_RuntimeStatisticsNone));
    EXPECT_TRUE(Contains(resourceDot, s_RuntimeStatisticsNone));
    EXPECT_TRUE(Contains(secondPassDot, s_RuntimeStatisticsNone));
}

TEST(Telemetry, TelemetryReportRetainsFiniteRecordsWhenTimingTotalsOverflow){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);
    ASSERT_TRUE(SetFloatingPointRoundingMode(FE_TONEAREST));

    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    NWB::Core::Perf::TimingStats timing = MakeTestTimingStats();
    timing.seconds = Limit<f64>::s_Max * 0.75;
    timing.minSeconds = timing.seconds;
    timing.maxSeconds = timing.seconds;
    timing.lastSeconds = timing.seconds;
    timing.sampleCount = 1u;
    timing.firstSampleFrameIndex = 70u;
    timing.lastSampleFrameIndex = 70u;
    for(const auto source : { Telemetry::PerfTimingSource::Cpu, Telemetry::PerfTimingSource::Gpu }){
        ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, source, Name(NWB::Tests::TelemetryCases::s_TelemetryCaseLargeFirst), "large_first", timing));
        ASSERT_TRUE(Telemetry::RecordPerfTiming(recorder, source, Name(NWB::Tests::TelemetryCases::s_TelemetryCaseLargeSecond), "large_second", timing));
    }

    const auto report = Log::BuildTelemetryReport(recorder.view(), testArena.arena, testArena.scratchArena);
    ASSERT_TRUE(report);
    EXPECT_EQ(report->summary.eventCount, 4u);
    EXPECT_EQ(report->summary.parseFailureCount, 0u);
    EXPECT_EQ(report->summary.cpuTimingEventCount, 2u);
    EXPECT_EQ(report->summary.gpuTimingEventCount, 2u);

    const AStringView json(report->json.data(), report->json.size());
    EXPECT_TRUE(Contains(json, "\"cpuTimingSeconds\": null"));
    EXPECT_TRUE(Contains(json, "\"gpuTimingSeconds\": null"));
    const AStringView csv(report->perfCsv.data(), report->perfCsv.size());
    EXPECT_TRUE(Contains(csv, "cpu,large_first,"));
    EXPECT_TRUE(Contains(csv, "cpu,large_second,"));
    EXPECT_TRUE(Contains(csv, "gpu,large_first,"));
    EXPECT_TRUE(Contains(csv, "gpu,large_second,"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_log_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;
namespace LogType = NWB::Core::Common::LogType;

void CheckPayload(const Span<const u8> payload, const Span<const u8> expected = {}){
    ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedTextLogPayloadHeader) + expected.size());
    usize cursor = 0u;
    const auto readHeader = ReadPOD<Telemetry::EncodedTextLogPayloadHeader>(payload, cursor);
    ASSERT_TRUE(readHeader);
    Telemetry::EncodedTextLogPayloadHeader header = *readHeader;
    EXPECT_EQ(header.messageBytes, expected.size());
    for(usize index = 0u; index < expected.size(); ++index)
        EXPECT_EQ(payload[cursor + index], expected[index]) << index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, TextLogPayloadPreservesUnicodeAndEmbeddedNullBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr TStringView s_Message(
        NWB_TEXT("A\u00e9\ud55c\U0001f642\0Z"),
        sizeof(NWB_TEXT("A\u00e9\ud55c\U0001f642\0Z")) / sizeof(tchar) - 1u
    );
    constexpr Array<u8, 12> s_Expected = { 0x41u, 0xc3u, 0xa9u, 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u, 0u, 0x5au };
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Warning, s_Message, testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadRetainsPlatformInvalidUnicodeConversion){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
#if defined(NWB_UNICODE)
#if WCHAR_MAX <= 0xffffu
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0xd800u), static_cast<tchar>('X'), static_cast<tchar>(0xdc00u) };
#else
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0x110000u), static_cast<tchar>('X'), static_cast<tchar>(0xffffffffu) };
#endif
    constexpr Array<u8, 3> s_Expected = { static_cast<u8>('?'), static_cast<u8>('X'), static_cast<u8>('?') };
#else
    // Narrow byte strings pass through without repairing invalid UTF-8.
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0xc0u), static_cast<tchar>(0xafu), static_cast<tchar>('X') };
    constexpr Array<u8, 3> s_Expected = { 0xc0u, 0xafu, static_cast<u8>('X') };
#endif
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Info, TStringView(s_Message.data(), s_Message.size()), testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadPreservesAliasedInputBeforeHeaderWrite){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr TStringView s_Message = NWB_TEXT("captured \ud55c\U0001f642");
    constexpr Array<u8, 16> s_Expected = { 'c', 'a', 'p', 't', 'u', 'r', 'e', 'd', ' ', 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u };
    constexpr usize s_Offset = sizeof(tchar);
    constexpr usize s_MessageBytes = s_Message.size() * sizeof(tchar);
    payload.resize(s_Offset + s_MessageBytes);
    NWB_MEMCPY(payload.data() + s_Offset, s_MessageBytes, s_Message.data(), s_MessageBytes);
    const TStringView aliased(reinterpret_cast<const tchar*>(payload.data() + s_Offset), s_Message.size());
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Error, aliased, testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Warning, NWB_TEXT("retained log"), testArena.scratchArena, payload));
    const Telemetry::TelemetryBytes current = payload;
    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_TextLogPayloadVersion + 1u), Limit<u16>::s_Max, };
    Expected<Telemetry::TextLogPayload> parsed = MakeUnexpected(Failure{});
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        ASSERT_TRUE((parsed = Telemetry::ParseTextLogPayload(current.data(), current.size())));
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedTextLogPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedTextLogPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseTextLogPayload(payload.data(), payload.size())));
        ASSERT_TRUE((parsed = Telemetry::ParseTextLogPayload(current.data(), current.size())));
        EXPECT_EQ(AStringView(parsed->messageUtf8), AStringView("retained log"));
    }
}

TEST(Telemetry, TextLogPayloadEmptyAndInvalidTypeReplacePreviousBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Info, NWB_TEXT("old payload"), testArena.scratchArena, payload));
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::EssentialInfo, {}, testArena.scratchArena, payload));
    CheckPayload(payload);
    EXPECT_FALSE(Telemetry::BuildTextLogPayload(static_cast<LogType::Enum>(0xffu), {}, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
