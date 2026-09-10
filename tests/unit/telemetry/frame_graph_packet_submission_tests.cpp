// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_packet_submission_tests{


using namespace TelemetryTestDetail;



TEST(Telemetry, FrameGraphPacketSubmissionStatisticsValidation){
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
        .packetGeneration = 72u,
        .taskCount = 2u,
        .commandListCount = 1u,
        .ownerNodeIndex = 0u,
        .packetIndex = 1u,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .plannedWaitTokenCount = 3u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 1u,
        .submissionSeconds = 0.125,
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
    statistics.commandListCount = 0u;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
    statistics.commandListCount = 1u;
    statistics.submissionSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(statistics));
}

TEST(Telemetry, FrameGraphPacketSubmissionStatisticsV8RoundTripAndWireOrderIsStable){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        918u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_EQ(header.runtimeStatisticsCount, 1u);
    EXPECT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);
    EXPECT_EQ(header.packetSubmissionStatisticsCount, 3u);
    EXPECT_EQ(header.packetSubmissionStatisticsPresent, 1u);

    const usize packetSubmissionStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) * 2u
    ;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics firstEncodedStatistics;
    NWB_MEMCPY(
        &firstEncodedStatistics,
        sizeof(firstEncodedStatistics),
        payload.data() + packetSubmissionStatisticsOffset,
        sizeof(firstEncodedStatistics)
    );
    EXPECT_EQ(firstEncodedStatistics.ownerNodeIndex, 0u);
    EXPECT_EQ(firstEncodedStatistics.packetIndex, 0u);
    EXPECT_EQ(firstEncodedStatistics.packetGeneration, 72u);
    EXPECT_EQ(firstEncodedStatistics.queue.index, 1u);
    EXPECT_EQ(firstEncodedStatistics.queue.deviceGeneration, 17u);
    EXPECT_EQ(firstEncodedStatistics.queueClass, Telemetry::FrameGraphQueueClass::Graphics);
    EXPECT_EQ(firstEncodedStatistics.joinsAcceptedQueueFrontier, 0u);
    EXPECT_EQ(firstEncodedStatistics.recoverySubmission, 0u);
    EXPECT_EQ(firstEncodedStatistics.reserved, 0u);
    EXPECT_EQ(firstEncodedStatistics.taskCount, 2u);
    EXPECT_EQ(firstEncodedStatistics.commandListCount, 1u);
    EXPECT_EQ(firstEncodedStatistics.plannedWaitTokenCount, 2u);
    EXPECT_EQ(firstEncodedStatistics.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(firstEncodedStatistics.timelineWaitCount, 1u);
    EXPECT_EQ(firstEncodedStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_DOUBLE_EQ(firstEncodedStatistics.submissionSeconds, 0.125);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_TRUE(parsed.packetSubmissionStatisticsPresent);
    ASSERT_EQ(parsed.packetSubmissionStatistics.size(), 3u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[0u].packetIndex, 0u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[1u].packetIndex, 1u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[2u].packetIndex, 2u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[1u].queue.index, 3u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[1u].queueClass, Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(parsed.packetSubmissionStatistics[1u].commandListCount, 2u);
    EXPECT_EQ(parsed.packetSubmissionStatistics[1u].mergedTimelineWaitCount, 2u);
    EXPECT_TRUE(parsed.packetSubmissionStatistics[2u].joinsAcceptedQueueFrontier);
    EXPECT_TRUE(parsed.packetSubmissionStatistics[2u].recoverySubmission);

    const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord firstPhysicalQueue =
        physicalQueueRuntimeStatistics[0u]
    ;
    physicalQueueRuntimeStatistics[0u] = physicalQueueRuntimeStatistics[1u];
    physicalQueueRuntimeStatistics[1u] = firstPhysicalQueue;
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetZero = packetSubmissionStatistics[1u];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetOne = packetSubmissionStatistics[2u];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetTwo = packetSubmissionStatistics[0u];
    packetSubmissionStatistics[0u] = packetZero;
    packetSubmissionStatistics[1u] = packetOne;
    packetSubmissionStatistics[2u] = packetTwo;

    Telemetry::TelemetryBytes reorderedPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        918u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        reorderedPayload
    ));
    ASSERT_EQ(payload.size(), reorderedPayload.size());
    EXPECT_EQ(NWB_MEMCMP(payload.data(), reorderedPayload.data(), payload.size()), 0);
}

TEST(Telemetry, FrameGraphPacketSubmissionStatisticsV8PreservesExactEmptyAndAbsent){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    nodes[0u].runtimeStatistics.submission = {};
    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics.clear();

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        919u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_TRUE(parsed.packetSubmissionStatisticsPresent);
    EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 919u, nodes, edges, payload));
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_FALSE(parsed.packetSubmissionStatisticsPresent);
    EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());
}

TEST(Telemetry, FrameGraphPacketSubmissionStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords physicalQueueRuntimeStatistics(testArena.arena);
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetSubmissionStatistics(testArena.arena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );

    packetSubmissionStatistics[0u].joinsAcceptedQueueFrontier = false;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    packetSubmissionStatistics[1u].packetGeneration = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    packetSubmissionStatistics[1u].packetIndex = packetSubmissionStatistics[2u].packetIndex;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    packetSubmissionStatistics[2u].commandListCount = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    packetSubmissionStatistics[1u].submissionSeconds += 0.01;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    packetSubmissionStatistics[1u].queue = { .index = 3u, .deviceGeneration = 17u };
    packetSubmissionStatistics[1u].queueClass = Telemetry::FrameGraphQueueClass::Compute;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));

    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    const usize packetSubmissionStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) * 2u
    ;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics encodedStatistics;
    NWB_MEMCPY(
        &encodedStatistics,
        sizeof(encodedStatistics),
        payload.data() + packetSubmissionStatisticsOffset,
        sizeof(encodedStatistics)
    );
    encodedStatistics.reserved = 1u;
    NWB_MEMCPY(
        payload.data() + packetSubmissionStatisticsOffset,
        payload.size() - packetSubmissionStatisticsOffset,
        &encodedStatistics,
        sizeof(encodedStatistics)
    );
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    NWB_MEMCPY(
        &encodedStatistics,
        sizeof(encodedStatistics),
        payload.data() + packetSubmissionStatisticsOffset,
        sizeof(encodedStatistics)
    );
    encodedStatistics.submissionSeconds += 0.01;
    NWB_MEMCPY(
        payload.data() + packetSubmissionStatisticsOffset,
        payload.size() - packetSubmissionStatisticsOffset,
        &encodedStatistics,
        sizeof(encodedStatistics)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    const usize secondPacketSubmissionStatisticsOffset = packetSubmissionStatisticsOffset
        + sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics)
    ;
    NWB_MEMCPY(
        &encodedStatistics,
        sizeof(encodedStatistics),
        payload.data() + secondPacketSubmissionStatisticsOffset,
        sizeof(encodedStatistics)
    );
    encodedStatistics.packetIndex = 0u;
    NWB_MEMCPY(
        payload.data() + secondPacketSubmissionStatisticsOffset,
        payload.size() - secondPacketSubmissionStatisticsOffset,
        &encodedStatistics,
        sizeof(encodedStatistics)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ++header.packetSubmissionStatisticsCount;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.packetSubmissionStatisticsPresent = 0u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.reservedTail[1u] = 1u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u, parsed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

