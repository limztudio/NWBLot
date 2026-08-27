// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_runtime_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    return statistics;
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

