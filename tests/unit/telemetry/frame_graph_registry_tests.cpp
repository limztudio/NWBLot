// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include <core/telemetry/frame_graph_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_registry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TARGET = "target";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


using namespace TelemetryTestDetail;

class PendingNameFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle source = builder.addPass(Name("source"), "Source");
        const Telemetry::FrameGraphNodeHandle target = builder.addResource(Name(s_TARGET), "Target");
        const Telemetry::FrameGraphNodeHandle duplicateTarget = builder.addResource(Name(s_TARGET), "Duplicate Target");
        if(!target.valid() || !duplicateTarget.valid())
            return false;

        builder.dependsOnByName(source, Name(s_TARGET), 7u);
        builder.dependsOnByName(source, Name("missing"), 9u);
        return true;
    }
};

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
    EXPECT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size())));
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
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingNameEdges, records, 915u);

    const Telemetry::FrameGraphNodeHandle owner = builder.addPass(
        Name("runtime_owner"),
        "Runtime owner",
        Telemetry::FrameGraphPassMetadata{
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = MakeFrameGraphRuntimeStatistics(),
        }
    );
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics statistics =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)
    ;
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, statistics));
    statistics.compile.taskCount = 0u;

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0u].statistics.compile.taskCount, 50u);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics outOfBounds =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(3u)
    ;
    outOfBounds.compile.taskCount = 79u;
    outOfBounds.compile.packetCount = 78u;
    outOfBounds.compile.mergedTaskCount = 1u;
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, outOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics derivedOutOfBounds =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(3u)
    ;
    derivedOutOfBounds.compile.taskCount = 29u;
    derivedOutOfBounds.compile.mergedTaskCount = s_ExpectedDualCount;
    derivedOutOfBounds.recording.taskCount = 13u;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(derivedOutOfBounds));
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
        derivedOutOfBounds,
        MakeFrameGraphRuntimeStatistics()
    ));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, derivedOutOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics reorderedSubmissionDuration =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(3u)
    ;
    reorderedSubmissionDuration.submission.submissionSeconds = 0.021000000000000004;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
        reorderedSubmissionDuration,
        MakeFrameGraphRuntimeStatistics()
    ));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(
        owner,
        MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

