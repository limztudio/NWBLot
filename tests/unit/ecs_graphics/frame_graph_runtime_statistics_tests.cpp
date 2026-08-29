// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>

#include <core/telemetry/frame_graph_registry.h>
#include <core/telemetry/session.h>

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
    statistics.submission.plannedWaitTokenCount = 2u;
    statistics.submission.sameQueueWaitElisionCount = 1u;
    statistics.submission.timelineWaitCount = 1u;
    statistics.submission.acceptedFrontierSubmissionCount = 1u;
    statistics.submission.recoverySubmissionCount = 1u;
    statistics.submission.submissionSeconds = 0.002;
    return statistics;
}

[[nodiscard]] static NWB::Core::GpuTaskGraphPacketSubmissionStatistics
MakeValidPacketSubmissionStatistics()noexcept{
    return NWB::Core::GpuTaskGraphPacketSubmissionStatistics{
        .graphGeneration = 11u,
        .planGeneration = 12u,
        .recordingAttemptGeneration = 13u,
        .deviceGeneration = 7u,
        .packet = { .index = 0u, .generation = 12u },
        .queue = { .index = 2u, .deviceGeneration = 7u },
        .queueClass = NWB::Core::CommandQueue::Compute,
        .taskCount = 1u,
        .nativeCommandListCount = 1u,
        .plannedWaitTokenCount = 2u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 0u,
        .submissionSeconds = 0.002,
        .joinsAcceptedQueueFrontier = true,
        .isRecoverySubmission = true,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PacketSubmissionStatisticsFrameGraphContributor final
    : public NWB::Core::Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(NWB::Core::Telemetry::FrameGraphBuilder& builder)override{
        const NWB::Core::Telemetry::FrameGraphRuntimeStatistics runtimeStatistics =
            NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(
                MakeValidRuntimeStatistics(),
                builder.frameIndex(),
                builder.frameIndex()
            )
        ;
        const NWB::Core::Telemetry::FrameGraphNodeHandle owner = builder.addPass(
            Name("packet_submission_owner"),
            "Packet submission owner",
            NWB::Core::Telemetry::FrameGraphPassMetadata{
                .queueAssignment = {},
                .compiledTask = {},
                .runtimeStatistics = runtimeStatistics,
            }
        );
        if(!owner.valid())
            return false;

        return builder.addPacketSubmissionStatistics(
            owner,
            NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(
                MakeValidPacketSubmissionStatistics(),
                owner.index
            )
        );
    }
};

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

TEST(EcsGraphics, FrameGraphPacketSubmissionStatisticsMapsExactNativePacket){
    const NWB::Core::GpuTaskGraphPacketSubmissionStatistics statistics =
        MakeValidPacketSubmissionStatistics()
    ;
    const NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord telemetry =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(statistics, 4u)
    ;

    ASSERT_TRUE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(telemetry));
    EXPECT_EQ(telemetry.ownerNodeIndex, 4u);
    EXPECT_EQ(telemetry.packetIndex, 0u);
    EXPECT_EQ(telemetry.packetGeneration, 12u);
    EXPECT_EQ(telemetry.queue.index, 2u);
    EXPECT_EQ(telemetry.queue.deviceGeneration, 7u);
    EXPECT_EQ(telemetry.queueClass, NWB::Core::Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(telemetry.taskCount, 1u);
    EXPECT_EQ(telemetry.commandListCount, 1u);
    EXPECT_EQ(telemetry.plannedWaitTokenCount, 2u);
    EXPECT_EQ(telemetry.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(telemetry.timelineWaitCount, 1u);
    EXPECT_EQ(telemetry.mergedTimelineWaitCount, 0u);
    EXPECT_TRUE(telemetry.joinsAcceptedQueueFrontier);
    EXPECT_TRUE(telemetry.recoverySubmission);
    EXPECT_DOUBLE_EQ(telemetry.submissionSeconds, 0.002);

    NWB::Core::GpuTaskGraphPacketSubmissionStatistics invalid = statistics;
    invalid.queueClass = NWB::Core::CommandQueue::kCount;
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(invalid, 4u)
    ));
    EXPECT_FALSE(NWB::Core::Telemetry::IsValidFrameGraphPacketSubmissionStatistics(
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(
            statistics,
            Limit<u32>::s_Max
        )
    ));
}

TEST(EcsGraphics, FrameGraphBuilderCopiesOwnerBoundPacketSubmissionStatistics){
    NWB::Tests::TestArena<> testArena;
    NWB::Core::Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    NWB::Core::Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueStatistics(testArena.arena);
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetStatistics(testArena.arena);
    NWB::Core::Telemetry::FrameGraphBuilder builder(
        nodes,
        edges,
        pendingNameEdges,
        physicalQueueStatistics,
        packetStatistics,
        41u
    );

    const NWB::Core::Telemetry::FrameGraphNodeHandle owner = builder.addPass(
        Name("packet_owner"),
        "Packet owner",
        NWB::Core::Telemetry::FrameGraphPassMetadata{
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = NWB::Impl::ECSRenderDetail::BuildFrameGraphRuntimeStatistics(
                MakeValidRuntimeStatistics(),
                41u,
                41u
            ),
        }
    );
    ASSERT_TRUE(owner.valid());
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics =
        NWB::Impl::ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(
            MakeValidPacketSubmissionStatistics(),
            owner.index
        )
    ;
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord excessiveDuration = statistics;
    excessiveDuration.submissionSeconds = 0.003;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, excessiveDuration));
    ASSERT_TRUE(builder.addPacketSubmissionStatistics(owner, statistics));
    statistics.commandListCount = 3u;

    ASSERT_EQ(packetStatistics.size(), 1u);
    EXPECT_EQ(packetStatistics[0u].commandListCount, 1u);
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, packetStatistics[0u]));

    const NWB::Core::Telemetry::FrameGraphNodeHandle resource = builder.addResource(
        Name("packet_resource"),
        "Packet resource"
    );
    ASSERT_TRUE(resource.valid());
    statistics = packetStatistics[0u];
    statistics.ownerNodeIndex = resource.index;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(resource, statistics));

    statistics = packetStatistics[0u];
    ++statistics.packetGeneration;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, statistics));
}

TEST(EcsGraphics, FrameGraphRegistryRecordsExactPacketSubmissionStatistics){
    NWB::Tests::TestArena<> testArena;
    NWB::Core::Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(NWB::Core::Telemetry::CaptureOptions::FrameGraphOnly());
    session.setFrameIndex(41u);

    NWB::Core::Telemetry::FrameGraphRegistry registry(testArena.arena);
    PacketSubmissionStatisticsFrameGraphContributor contributor;
    registry.registerContributor(contributor);
    ASSERT_TRUE(registry.record(session));
    ASSERT_EQ(session.eventCount(), 1u);

    const NWB::Core::Telemetry::EventRecord* const event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    NWB::Core::Telemetry::FrameGraphPayload payload(testArena.arena);
    ASSERT_TRUE(NWB::Core::Telemetry::ParseFrameGraphPayload(
        testArena.arena,
        event->payload.data(),
        event->payload.size(),
        payload
    ));
    EXPECT_EQ(
        payload.wireVersion,
        NWB::Core::Telemetry::s_FrameGraphPacketSubmissionStatisticsPayloadVersion
    );
    EXPECT_TRUE(payload.packetSubmissionStatisticsPresent);
    ASSERT_EQ(payload.packetSubmissionStatistics.size(), 1u);
    EXPECT_EQ(payload.packetSubmissionStatistics[0u].ownerNodeIndex, 0u);
    EXPECT_EQ(payload.packetSubmissionStatistics[0u].packetIndex, 0u);
    EXPECT_EQ(payload.packetSubmissionStatistics[0u].plannedWaitTokenCount, 2u);
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
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp",
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
    const usize packetSnapshotLoopOffset = frameGraph.find(
        "for(usize packetIndex = 0u; packetIndex < m_deferredLightingCompiledGraph.packetCount(); ++packetIndex){",
        snapshotLoopOffset
    );
    const usize structuredLoopOffset = frameGraph.find(
        ": physicalQueueRuntimeStatistics",
        rendererFrameOffset
    );
    const usize packetStructuredLoopOffset = frameGraph.find(
        ": packetSubmissionStatistics",
        structuredLoopOffset
    );
    const usize frameSetupOffset = frameGraph.find(
        "const Handle frameSetup = builder.addPass(",
        structuredLoopOffset
    );
    ASSERT_NE(runtimeTopologyOffset, AStringView::npos);
    ASSERT_NE(snapshotLoopOffset, AStringView::npos);
    ASSERT_NE(packetSnapshotLoopOffset, AStringView::npos);
    ASSERT_NE(rendererFrameOffset, AStringView::npos);
    ASSERT_NE(structuredLoopOffset, AStringView::npos);
    ASSERT_NE(packetStructuredLoopOffset, AStringView::npos);
    ASSERT_NE(frameSetupOffset, AStringView::npos);
    EXPECT_LT(runtimeTopologyOffset, snapshotLoopOffset);
    EXPECT_LT(snapshotLoopOffset, rendererFrameOffset);
    EXPECT_LT(snapshotLoopOffset, packetSnapshotLoopOffset);
    EXPECT_LT(packetSnapshotLoopOffset, rendererFrameOffset);
    EXPECT_LT(rendererFrameOffset, structuredLoopOffset);
    EXPECT_LT(structuredLoopOffset, packetStructuredLoopOffset);
    EXPECT_LT(structuredLoopOffset, frameSetupOffset);
    EXPECT_LT(packetStructuredLoopOffset, frameSetupOffset);

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
    EXPECT_NE(queueSnapshotExport.find(
        "m_deferredLightingSubmissionTransaction.packetSubmissionStatistics("
    ), AStringView::npos);
    EXPECT_NE(queueSnapshotExport.find(
        "packetSubmissionStatistics.size() != deferredRuntimeStatistics.submission.nativeSubmissionCount"
    ), AStringView::npos);

    const AStringView structuredExport = frameGraph.substr(
        rendererFrameOffset,
        frameSetupOffset - rendererFrameOffset
    );
    EXPECT_NE(structuredExport.find(
        "builder.addPhysicalQueueRuntimeStatistics(rendererFrame, queueStatistics)"
    ), AStringView::npos);
    EXPECT_NE(structuredExport.find(
        "ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, rendererFrame.index)"
    ), AStringView::npos);
    EXPECT_NE(structuredExport.find(
        "builder.addPacketSubmissionStatistics(rendererFrame, telemetryStatistics)"
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

