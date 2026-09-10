// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include <core/telemetry/frame_graph_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_registry_tests{


using namespace TelemetryTestDetail;

class PendingNameFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle source = builder.addPass(Name("source"), "Source");
        const Telemetry::FrameGraphNodeHandle target = builder.addResource(Name("target"), "Target");
        const Telemetry::FrameGraphNodeHandle duplicateTarget = builder.addResource(Name("target"), "Duplicate Target");
        if(!target.valid() || !duplicateTarget.valid())
            return false;

        builder.dependsOnByName(source, Name("target"), 7u);
        builder.dependsOnByName(source, Name("missing"), 9u);
        return true;
    }
};

class QueueAssignmentFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        return builder.addPass(
            Name("assigned_pass"),
            "Assigned Pass",
            MakeChangedFrameGraphQueueAssignment(),
            5u
        ).valid();
    }
};

[[nodiscard]] static bool AddRuntimeStatisticsPacketSubmissions(
    Telemetry::FrameGraphBuilder& builder,
    const Telemetry::FrameGraphNodeHandle owner
){
    for(u32 packetIndex = 0u; packetIndex < 19u; ++packetIndex){
        Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
            .packetGeneration = 52u,
            .taskCount = packetIndex == 0u ? 2u : 1u,
            .commandListCount = packetIndex == 0u ? 2u : 1u,
            .ownerNodeIndex = owner.index,
            .packetIndex = packetIndex,
            .queue = { .index = 1u, .deviceGeneration = 17u },
            .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
            .submissionSeconds = packetIndex == 0u ? 0.011 : 0.0,
            .joinsAcceptedQueueFrontier = packetIndex != 18u,
            .recoverySubmission = packetIndex < 5u,
        };
        if(packetIndex < 5u){
            statistics.plannedWaitTokenCount = 1u;
            statistics.sameQueueWaitElisionCount = 1u;
        }
        else if(packetIndex < 13u){
            statistics.plannedWaitTokenCount = 1u;
            statistics.timelineWaitCount = 1u;
        }
        else{
            statistics.mergedTimelineWaitCount = packetIndex == 13u ? 2u : 1u;
            statistics.plannedWaitTokenCount = statistics.mergedTimelineWaitCount;
        }
        if(!builder.addPacketSubmissionStatistics(owner, statistics))
            return false;
    }

    for(u32 queuePacketIndex = 0u; queuePacketIndex < 11u; ++queuePacketIndex){
        Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
            .packetGeneration = 52u,
            .taskCount = 1u,
            .commandListCount = queuePacketIndex == 0u ? 2u : 1u,
            .ownerNodeIndex = owner.index,
            .packetIndex = 19u + queuePacketIndex,
            .queue = { .index = 3u, .deviceGeneration = 17u },
            .queueClass = Telemetry::FrameGraphQueueClass::Compute,
            .submissionSeconds = queuePacketIndex == 0u ? 0.010 : 0.0,
            .joinsAcceptedQueueFrontier = queuePacketIndex != 10u,
            .recoverySubmission = queuePacketIndex < 3u,
        };
        if(queuePacketIndex == 0u){
            statistics.plannedWaitTokenCount = 7u;
            statistics.sameQueueWaitElisionCount = 7u;
        }
        else if(queuePacketIndex == 1u){
            statistics.plannedWaitTokenCount = 6u;
            statistics.timelineWaitCount = 6u;
        }
        else if(queuePacketIndex == 2u){
            statistics.plannedWaitTokenCount = 11u;
            statistics.mergedTimelineWaitCount = 11u;
        }
        if(!builder.addPacketSubmissionStatistics(owner, statistics))
            return false;
    }
    return true;
}

class RuntimeStatisticsFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle owner = builder.addPass(
            Name("runtime_pass"),
            "Runtime Pass",
            Telemetry::FrameGraphPassMetadata{
                .queueAssignment = MakeChangedFrameGraphQueueAssignment(),
                .compiledTask = MakeFrameGraphCompiledTask(
                    52u,
                    9u,
                    Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
                ),
                .runtimeStatistics = MakeFrameGraphRuntimeStatistics(),
            },
            6u
        );
        if(!owner.valid())
            return false;
        if(!builder.addPhysicalQueueRuntimeStatistics(
            owner,
            MakeFrameGraphPhysicalQueueRuntimeStatistics(3u)
        ) || !builder.addPhysicalQueueRuntimeStatistics(
            owner,
            MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)
        ))
            return false;
        return AddRuntimeStatisticsPacketSubmissions(builder, owner);
    }
};

class CaptureFrameIndexFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual bool appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        m_frameIndex = builder.frameIndex();
        return builder.addPass(Name("capture_frame_index"), "Capture Frame Index").valid();
    }
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }

private:
    u64 m_frameIndex = Limit<u64>::s_Max;
};


TEST(Telemetry, FrameGraphRegistryResolvesPendingNameEdges){
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

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    ASSERT_EQ(parsed.edges.size(), 1u);
    EXPECT_EQ(parsed.edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed.edges[0u].toNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[0u].kind, Telemetry::FrameGraphEdgeKind::DependsOn);
    EXPECT_EQ(parsed.edges[0u].flags, 7u);
}

TEST(Telemetry, FrameGraphRegistryPreservesQueueAssignments){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    QueueAssignmentFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 1u);
    EXPECT_EQ(parsed.nodes[0u].flags, 5u);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::Changed);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptedQueue.index, 3u);
    EXPECT_EQ(parsed.nodes[0u].queueAssignment.acceptedQueue.deviceGeneration, 17u);
}

TEST(Telemetry, FrameGraphRegistryPreservesRuntimeStatistics){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    RuntimeStatisticsFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 1u);
    EXPECT_EQ(parsed.nodes[0u].flags, 6u);
    EXPECT_TRUE(parsed.nodes[0u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.graphGeneration, 51u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionCount, 3u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionEdgeCount, 6u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.uploadBlobBytes, 30u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.recording.parallelPacketCount, 29u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.acceptedFrontierSubmissionCount, 28u);
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), 2u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].ownerNodeIndex, 0u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].statistics.queue.index, 1u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[1u].statistics.queue.index, 3u);
    EXPECT_TRUE(parsed.packetSubmissionStatisticsPresent);
    ASSERT_EQ(parsed.packetSubmissionStatistics.size(), 30u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[0u].queue.index, 1u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[18u].queue.index, 1u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[19u].queue.index, 3u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[29u].queue.index, 3u);
}

TEST(Telemetry, FrameGraphBuilderCopiesPhysicalQueueRuntimeStatistics){
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
    EXPECT_EQ(records[0u].ownerNodeIndex, owner.index);
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
    derivedOutOfBounds.compile.mergedTaskCount = 2u;
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

TEST(Telemetry, FrameGraphRegistryPropagatesCaptureFrameIndexToBuilder){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    session.setFrameIndex(914u);

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    CaptureFrameIndexFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.frameIndex(), 914u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    Telemetry::FrameGraphBuilder directBuilder(nodes, edges, pendingNameEdges);
    EXPECT_EQ(directBuilder.frameIndex(), 0u);
}

TEST(Telemetry, RecordFrameGraphUsesTelemetryEvent){
    TestArena testArena;
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    EXPECT_TRUE(Telemetry::RecordFrameGraph(recorder, 909u, nodes, edges, 14u));

    const Telemetry::EventRecord* event = recorder.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::FrameGraphFrame);
    EXPECT_EQ(event->header.frameIndex, 909u);
    EXPECT_EQ(event->header.streamId, 14u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 909u);
    EXPECT_EQ(parsed.nodes.size(), 3u);
    EXPECT_EQ(parsed.edges.size(), 2u);
}

TEST(Telemetry, CaptureSessionRecordsFrameGraphWithContext){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::All());
    session.setFrameIndex(910u);
    session.setStreamId(15u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    EXPECT_TRUE(session.recordFrameGraph(nodes, edges));

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(event->header.kind, Telemetry::EventKind::FrameGraphFrame);
    EXPECT_EQ(event->header.frameIndex, 910u);
    EXPECT_EQ(event->header.streamId, 15u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, event->payload.data(), event->payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 910u);
    EXPECT_EQ(parsed.nodes.size(), 3u);
    EXPECT_EQ(parsed.edges.size(), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

