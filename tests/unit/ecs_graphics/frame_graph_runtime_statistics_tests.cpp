// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>

#include <tests/common/test_context.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_runtime_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

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
    statistics.submission.acceptedFrontierSubmissionCount = 1u;
    statistics.submission.recoverySubmissionCount = 1u;
    return statistics;
}

TEST(EcsGraphics, FrameGraphPhysicalQueueRuntimeStatisticsMapsCoherentSnapshots){
    const PhysicalQueueRuntimeSnapshots snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    const NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics telemetry =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
            snapshots.compile,
            snapshots.recording,
            snapshots.submission
        )
    ;

    ASSERT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(telemetry));
    EXPECT_EQ(telemetry.graphGeneration, 11u);
    EXPECT_EQ(telemetry.planGeneration, 12u);
    EXPECT_EQ(telemetry.recordingAttemptGeneration, 13u);
    EXPECT_EQ(telemetry.deviceGeneration, 7u);
    EXPECT_EQ(telemetry.queue.index, 2u);
    EXPECT_EQ(telemetry.queueClass, NWB::Core::Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(telemetry.compile.taskCount, 3u);
    EXPECT_EQ(telemetry.compile.incomingLogicalOwnershipTransferCount, 2u);
    EXPECT_EQ(telemetry.recording.commandListCount, 2u);
    EXPECT_DOUBLE_EQ(telemetry.recording.recordingSeconds, 0.004);
    EXPECT_EQ(telemetry.submission.plannedWaitTokenCount, 4u);
    EXPECT_EQ(telemetry.submission.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(telemetry.submission.recoverySubmissionCount, 1u);
    EXPECT_DOUBLE_EQ(telemetry.submission.submissionSeconds, 0.005);
}

TEST(EcsGraphics, FrameGraphPhysicalQueueRuntimeStatisticsRejectsMixedSnapshots){
    PhysicalQueueRuntimeSnapshots snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    ++snapshots.submission.recordingAttemptGeneration;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
            snapshots.compile,
            snapshots.recording,
            snapshots.submission
        )
    ));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    ++snapshots.recording.queue.index;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
            snapshots.compile,
            snapshots.recording,
            snapshots.submission
        )
    ));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    snapshots.submission.queueClass = NWB::Core::CommandQueue::Graphics;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
            snapshots.compile,
            snapshots.recording,
            snapshots.submission
        )
    ));

    snapshots = MakeValidPhysicalQueueRuntimeSnapshots();
    snapshots.submission.recoverySubmissionCount = 2u;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
            snapshots.compile,
            snapshots.recording,
            snapshots.submission
        )
    ));
}

TEST(EcsGraphics, FrameGraphExportsEveryCompiledPhysicalQueueAsStructuredRuntimeTelemetry){
    NWB::Tests::TestArena<> testArena;
    const TestPath repoRoot = TestPath(testArena.arena, __FILE__)
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path()
        .lexically_normal()
    ;
    NWB::Tests::TestAString source;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "kernel" / "frame_graph_export.cpp",
        source
    ));
    const AStringView frameGraph(source.data(), source.size());

    const usize runtimeTopologyOffset = frameGraph.find(
        "const Core::GpuPhysicalQueueTopology runtimeQueueTopology = "
        "m_deferredLightingCompiledGraph.queueTopology();"
    );
    const usize snapshotLoopOffset = frameGraph.find(
        "for(usize queueIndex = 0u; queueIndex < runtimeQueueTopology.queueCount; ++queueIndex){",
        runtimeTopologyOffset
    );
    const usize rendererFrameOffset = frameGraph.find("const Handle rendererFrame = builder.addPass(");
    const usize structuredLoopOffset = frameGraph.find(
        ": physicalQueueRuntimeStatistics",
        rendererFrameOffset
    );
    const usize frameSetupOffset = frameGraph.find(
        "const Handle frameSetup = builder.addPass(",
        structuredLoopOffset
    );
    ASSERT_NE(runtimeTopologyOffset, AStringView::npos);
    ASSERT_NE(snapshotLoopOffset, AStringView::npos);
    ASSERT_NE(rendererFrameOffset, AStringView::npos);
    ASSERT_NE(structuredLoopOffset, AStringView::npos);
    ASSERT_NE(frameSetupOffset, AStringView::npos);
    EXPECT_LT(runtimeTopologyOffset, snapshotLoopOffset);
    EXPECT_LT(snapshotLoopOffset, rendererFrameOffset);
    EXPECT_LT(rendererFrameOffset, structuredLoopOffset);
    EXPECT_LT(structuredLoopOffset, frameSetupOffset);

    const AStringView queueSnapshotExport = frameGraph.substr(
        runtimeTopologyOffset,
        rendererFrameOffset - runtimeTopologyOffset
    );
    EXPECT_NE(queueSnapshotExport.find(
        "ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics("
    ), AStringView::npos);
    EXPECT_NE(queueSnapshotExport.find(
        "physicalQueueRuntimeStatistics.push_back(queueStatistics);"
    ), AStringView::npos);

    const AStringView structuredExport = frameGraph.substr(
        rendererFrameOffset,
        frameSetupOffset - rendererFrameOffset
    );
    EXPECT_NE(structuredExport.find(
        "builder.addPhysicalQueueRuntimeStatistics(rendererFrame, queueStatistics)"
    ), AStringView::npos);
    EXPECT_EQ(
        structuredExport.find("BuildFrameGraphPhysicalQueueRuntimeStatistics("),
        AStringView::npos
    );
    EXPECT_EQ(structuredExport.find("physicalQueueCompileStatistics("), AStringView::npos);
    EXPECT_EQ(structuredExport.find("physicalQueueRecordingStatistics("), AStringView::npos);
    EXPECT_EQ(structuredExport.find("physicalQueueSubmissionStatistics("), AStringView::npos);
    EXPECT_EQ(structuredExport.find("hasTerminalSubmissionWork"), AStringView::npos);
    EXPECT_EQ(structuredExport.find("hasLogicalOwnershipTelemetry"), AStringView::npos);
    EXPECT_EQ(structuredExport.find("commandArenaStatistics"), AStringView::npos);
    EXPECT_EQ(structuredExport.find("continue;"), AStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, FrameGraphRuntimeStatisticsSelectsOnlyMatchingCoherentSnapshot){
    const NWB::Core::GpuTaskGraphRuntimeStatistics statistics = MakeValidRuntimeStatistics();
    ASSERT_TRUE(statistics.valid());

    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics matching =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(statistics, 41u, 41u)
    ;
    EXPECT_TRUE(matching.present);
    EXPECT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphRuntimeStatistics(matching));
    EXPECT_EQ(matching.graphGeneration, statistics.compile.graphGeneration);
    EXPECT_EQ(matching.planGeneration, statistics.compile.planGeneration);
    EXPECT_EQ(matching.recordingAttemptGeneration, statistics.recording.recordingAttemptGeneration);
    EXPECT_EQ(matching.deviceGeneration, statistics.compile.deviceGeneration);
    EXPECT_EQ(matching.compile.taskCount, statistics.compile.taskCount);
    EXPECT_EQ(matching.recording.commandListCount, statistics.recording.commandListCount);
    EXPECT_EQ(matching.submission.nativeSubmissionCount, statistics.submission.nativeSubmissionCount);
    EXPECT_EQ(
        matching.submission.acceptedFrontierSubmissionCount,
        statistics.submission.acceptedFrontierSubmissionCount
    );
    EXPECT_EQ(matching.submission.recoverySubmissionCount, statistics.submission.recoverySubmissionCount);

    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics stale =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(statistics, 42u, 41u)
    ;
    EXPECT_FALSE(stale.present);

    NWB::Core::GpuTaskGraphRuntimeStatistics invalidGenerations = statistics;
    ++invalidGenerations.submission.planGeneration;
    ASSERT_FALSE(invalidGenerations.valid());
    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics invalid =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(invalidGenerations, 41u, 41u)
    ;
    EXPECT_FALSE(invalid.present);
}


TEST(EcsGraphics, FrameGraphRuntimeStatisticsOmitsResetArtifactsForMatchingFrame){
    NWB::Tests::TestArena<> testArena;
    NWB::Core::GpuCompiledGraph compiledGraph(testArena.arena);
    NWB::Core::GpuRecordedGraph recordedGraph(testArena.arena);
    NWB::Core::GpuGraphSubmissionTransaction transaction(testArena.arena);

    compiledGraph.reset();
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);

    const NWB::Core::GpuTaskGraphRuntimeStatistics resetStatistics = NWB::Core::CollectGpuTaskGraphRuntimeStatistics(
        compiledGraph,
        recordedGraph,
        transaction
    );
    ASSERT_FALSE(resetStatistics.valid());
    const NWB::Core::Telemetry::FrameGraphRuntimeStatistics telemetry =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(resetStatistics, 41u, 41u)
    ;
    EXPECT_FALSE(telemetry.present);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

