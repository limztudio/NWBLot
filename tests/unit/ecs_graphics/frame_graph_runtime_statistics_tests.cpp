// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>

#include <core/telemetry/frame_graph_contributor.h>
#include <core/task/gpu/compiler_internal.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_runtime_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PhysicalQueueRuntimeSnapshots{
    NWB::Core::GpuTaskGraphPhysicalQueueCompileStatistics compile;
    NWB::Core::GpuTaskGraphPhysicalQueueRecordingStatistics recording;
    NWB::Core::GpuTaskGraphPhysicalQueueSubmissionStatistics submission;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static PhysicalQueueRuntimeSnapshots MakeValidPhysicalQueueRuntimeSnapshots()noexcept{
    PhysicalQueueRuntimeSnapshots snapshots;
    snapshots.compile.graphGeneration = 11u;
    snapshots.compile.planGeneration = 12u;
    snapshots.compile.deviceGeneration = 7u;
    snapshots.compile.queue = { .index = 2u, .deviceGeneration = 7u };
    snapshots.compile.queueClass = NWB::Core::CommandQueue::Compute;
    snapshots.compile.taskCount = 3u;
    snapshots.compile.packetCount = 2u;
    snapshots.compile.mergedTaskCount = 1u;
    snapshots.compile.prologueBarrierCount = 2u;
    snapshots.compile.epilogueBarrierCount = 1u;
    snapshots.compile.ownershipReleaseBarrierCount = 1u;
    snapshots.compile.incomingLogicalOwnershipTransferCount = 2u;
    snapshots.compile.incomingLogicalOwnershipTransferSignatureCount = 1u;
    snapshots.compile.incomingRepeatedOwnershipTransferSignatureCount = 1u;
    snapshots.compile.concurrentSharingAdviceResourceCount = 1u;

    snapshots.recording.graphGeneration = 11u;
    snapshots.recording.planGeneration = 12u;
    snapshots.recording.recordingAttemptGeneration = 13u;
    snapshots.recording.deviceGeneration = 7u;
    snapshots.recording.queue = snapshots.compile.queue;
    snapshots.recording.queueClass = NWB::Core::CommandQueue::Compute;
    snapshots.recording.packetCount = 2u;
    snapshots.recording.taskCount = 3u;
    snapshots.recording.commandListCount = 2u;
    snapshots.recording.barrierCount = 3u;
    snapshots.recording.workerRoutedPacketCount = 1u;
    snapshots.recording.parallelPacketCount = 1u;
    snapshots.recording.commandListAcquisitionSeconds = 0.001;
    snapshots.recording.graphBarrierRecordingSeconds = 0.002;
    snapshots.recording.taskRecordSeconds = 0.003;
    snapshots.recording.recordingSeconds = 0.004;

    snapshots.submission.graphGeneration = 11u;
    snapshots.submission.planGeneration = 12u;
    snapshots.submission.recordingAttemptGeneration = 13u;
    snapshots.submission.deviceGeneration = 7u;
    snapshots.submission.queue = snapshots.compile.queue;
    snapshots.submission.queueClass = NWB::Core::CommandQueue::Compute;
    snapshots.submission.acceptedPacketCount = 2u;
    snapshots.submission.acceptedTaskCount = 3u;
    snapshots.submission.nativeSubmissionCount = 2u;
    snapshots.submission.nativeCommandListCount = 2u;
    snapshots.submission.plannedWaitTokenCount = 4u;
    snapshots.submission.sameQueueWaitElisionCount = 1u;
    snapshots.submission.timelineWaitCount = 2u;
    snapshots.submission.mergedTimelineWaitCount = 1u;
    snapshots.submission.acceptedFrontierSubmissionCount = 1u;
    snapshots.submission.recoverySubmissionCount = 1u;
    snapshots.submission.submissionSeconds = 0.005;
    return snapshots;
}

[[nodiscard]] static NWB::Core::GpuTaskGraphRuntimeStatistics MakeValidRuntimeStatistics()noexcept{
    NWB::Core::GpuTaskGraphRuntimeStatistics statistics;
    statistics.compile.graphGeneration = 11u;
    statistics.compile.planGeneration = 12u;
    statistics.compile.deviceGeneration = 7u;
    statistics.compile.taskCount = 1u;
    statistics.compile.resourceCount = 1u;
    statistics.compile.resourceVersionCount = 2u;
    statistics.compile.resourceVersionEdgeCount = 1u;
    statistics.compile.packetCount = 1u;

    statistics.recording.graphGeneration = 11u;
    statistics.recording.planGeneration = 12u;
    statistics.recording.recordingAttemptGeneration = 13u;
    statistics.recording.deviceGeneration = 7u;
    statistics.recording.packetCount = 1u;
    statistics.recording.taskCount = 1u;
    statistics.recording.commandListCount = 1u;

    statistics.submission.graphGeneration = 11u;
    statistics.submission.planGeneration = 12u;
    statistics.submission.recordingAttemptGeneration = 13u;
    statistics.submission.deviceGeneration = 7u;
    statistics.submission.acceptedPacketCount = 1u;
    statistics.submission.acceptedTaskCount = 1u;
    statistics.submission.nativeSubmissionCount = 1u;
    statistics.submission.nativeCommandListCount = 1u;
    statistics.submission.plannedWaitTokenCount = 2u;
    statistics.submission.sameQueueWaitElisionCount = 1u;
    statistics.submission.timelineWaitCount = 1u;
    statistics.submission.acceptedFrontierSubmissionCount = 1u;
    statistics.submission.recoverySubmissionCount = 1u;
    statistics.submission.submissionSeconds = 0.002;
    return statistics;
}

[[nodiscard]] static NWB::Core::GpuTaskGraphPacketSubmissionStatistics MakeValidPacketSubmissionStatistics()noexcept{
    return NWB::Core::GpuTaskGraphPacketSubmissionStatistics{
        .graphGeneration = 11u,
        .planGeneration = 12u,
        .recordingAttemptGeneration = 13u,
        .packet = { .generation = 12u, .index = 0u },
        .queue = { .index = 2u, .deviceGeneration = 7u },
        .queueClass = NWB::Core::CommandQueue::Compute,
        .deviceGeneration = 7u,
        .joinsAcceptedQueueFrontier = true,
        .isRecoverySubmission = true,
        .taskCount = 1u,
        .nativeCommandListCount = 1u,
        .plannedWaitTokenCount = 2u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 0u,
        .submissionSeconds = 0.002,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, FrameGraphPhysicalQueueRuntimeStatisticsRejectsMixedSnapshots){
    PhysicalQueueRuntimeSnapshots snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    ++snapshots.submission.recordingAttemptGeneration;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    ++snapshots.recording.queue.index;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    snapshots.submission.queueClass = NWB::Core::CommandQueue::Graphics;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    snapshots.submission.recoverySubmissionCount = 2u;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));
}

TEST(EcsGraphics, FrameGraphPacketSubmissionStatisticsRejectsInvalidQueueOrOwner){
    const NWB::Core::GpuTaskGraphPacketSubmissionStatistics statistics = MakeValidPacketSubmissionStatistics();
    NWB::Core::GpuTaskGraphPacketSubmissionStatistics invalid = statistics;
    invalid.queueClass = NWB::Core::CommandQueue::kCount;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(invalid, 4u)));
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(statistics, Limit<u32>::s_Max)));
}

TEST(EcsGraphics, FrameGraphBuilderCopiesOwnerBoundPacketSubmissionStatistics){
    NWB::Tests::TestArena<> testArena;
    NWB::Core::Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    NWB::Core::Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueStatistics(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetStatistics(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/ecs_graphics/frame_graph_builder_scratch"));
    NWB::Core::Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingNameEdges, physicalQueueStatistics, packetStatistics, 41u);

    const NWB::Core::Telemetry::FrameGraphNodeHandle owner = builder.addPass(
        Name("packet_owner"),
        "Packet owner",
        NWB::Core::Telemetry::FrameGraphPassMetadata{ .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(MakeValidRuntimeStatistics(), 41u, 41u), }
    );
    ASSERT_TRUE(owner.valid());
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics = NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(MakeValidPacketSubmissionStatistics(), owner.index);
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord excessiveDuration = statistics;
    excessiveDuration.submissionSeconds = 0.003;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, excessiveDuration));
    ASSERT_TRUE(builder.addPacketSubmissionStatistics(owner, statistics));
    statistics.commandListCount = 3u;

    ASSERT_EQ(packetStatistics.size(), 1u);
    EXPECT_EQ(packetStatistics[0u].commandListCount, 1u);
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, packetStatistics[0u]));

    const NWB::Core::Telemetry::FrameGraphNodeHandle resource = builder.addResource(Name("packet_resource"), "Packet resource");
    ASSERT_TRUE(resource.valid());
    statistics = packetStatistics[0u];
    statistics.ownerNodeIndex = resource.index;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(resource, statistics));

    statistics = packetStatistics[0u];
    ++statistics.packetGeneration;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, statistics));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, FrameGraphRuntimeStatisticsSelectsOnlyMatchingCoherentSnapshot){
    const NWB::Core::GpuTaskGraphRuntimeStatistics statistics = MakeValidRuntimeStatistics();
    ASSERT_TRUE(statistics.valid());

    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics matching = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(statistics, 41u, 41u);
    EXPECT_TRUE(matching.present);
    EXPECT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphRuntimeStatistics(matching));
    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics stale = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(statistics, 42u, 41u);
    EXPECT_FALSE(stale.present);

    NWB::Core::GpuTaskGraphRuntimeStatistics invalidGenerations = statistics;
    ++invalidGenerations.submission.planGeneration;
    ASSERT_FALSE(invalidGenerations.valid());
    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics invalid = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(invalidGenerations, 41u, 41u);
    EXPECT_FALSE(invalid.present);
}


TEST(EcsGraphics, FrameGraphRuntimeStatisticsRejectsInconsistentInheritedWaitElisions){
    NWB::Core::GpuTaskGraphRuntimeStatistics runtimeStatistics = MakeValidRuntimeStatistics();
    runtimeStatistics.submission.timelineWaitCount = 0u;
    runtimeStatistics.submission.inheritedTimelineWaitElisionCount = 1u;
    ASSERT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(runtimeStatistics, 41u, 41u)));
    ++runtimeStatistics.submission.inheritedTimelineWaitElisionCount;
    EXPECT_FALSE(NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(runtimeStatistics, 41u, 41u).present);

    PhysicalQueueRuntimeSnapshots snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    snapshots.submission.timelineWaitCount = 1u;
    snapshots.submission.inheritedTimelineWaitElisionCount = 1u;
    ASSERT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));
    ++snapshots.submission.inheritedTimelineWaitElisionCount;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(snapshots.compile, snapshots.recording, snapshots.submission)));

    NWB::Core::GpuTaskGraphPacketSubmissionStatistics packetStatistics = MakeValidPacketSubmissionStatistics();
    packetStatistics.timelineWaitCount = 0u;
    packetStatistics.inheritedTimelineWaitElisionCount = 1u;
    ASSERT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, 4u)));
    ++packetStatistics.inheritedTimelineWaitElisionCount;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, 4u)));
}


TEST(EcsGraphics, FrameGraphRuntimeStatisticsOmitsResetArtifactsForMatchingFrame){
    NWB::Tests::TestArena<> testArena;
    NWB::Core::GpuTaskGraph graph(testArena.arena);
    NWB::Core::GpuCompiledGraph compiledGraph(testArena.arena);
    NWB::Core::GpuRecordedGraph recordedGraph(testArena.arena);
    NWB::Core::GpuGraphSubmissionTransaction transaction(testArena.arena);

    const NWB::Core::GpuTaskGraph::DeclarationReadView declarationAccess(graph);
    ASSERT_TRUE(declarationAccess.valid());
    const NWB::Core::GpuPhysicalQueueInfo queue{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = NWB::Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u },
        .queueClass = NWB::Core::CommandQueue::Graphics,
        .capabilities = static_cast<NWB::Core::GpuQueueCapability::Mask>(static_cast<u8>(NWB::Core::GpuQueueCapability::Graphics) | static_cast<u8>(NWB::Core::GpuQueueCapability::Compute) | static_cast<u8>(NWB::Core::GpuQueueCapability::Transfer)),
        .dedicated = false,
    };
    const NWB::Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    NWB::Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    NWB::Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/ecs_graphics/reset_runtime_statistics_scratch"));
    const NWB::Core::GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(declarationAccess, topology, NWB::Core::GpuTaskGraphCompileOptions{}, scratchArena, analysis, assignments, compiledGraph));

    const NWB::Core::GpuCompiledGraph::ReadView planAccess(compiledGraph);
    ASSERT_TRUE(planAccess.validFor(declarationAccess));
    const NWB::Core::GpuTaskGraphRuntimeStatistics resetStatistics = NWB::Core::CollectGpuTaskGraphRuntimeStatistics(compiledGraph, planAccess, recordedGraph, transaction);
    ASSERT_FALSE(resetStatistics.valid());
    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics telemetry = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(resetStatistics, 41u, 41u);
    EXPECT_FALSE(telemetry.present);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

