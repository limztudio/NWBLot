// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_packet_submission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;



using namespace TelemetryTestDetail;



TEST(Telemetry, PacketSubmissionStatisticsRejectInconsistentCountsAndNonfiniteTime){
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
        .packetGeneration = 72u,
        .taskCount = s_ExpectedDualCount,
        .commandListCount = 1u,
        .ownerNodeIndex = 0u,
        .packetIndex = 1u,
        .queue = { .index = 3u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Compute,
        .joinsAcceptedQueueFrontier = true,
        .recoverySubmission = true,
        .plannedWaitTokenCount = 4u,
        .sameQueueWaitElisionCount = 1u,
        .timelineWaitCount = 1u,
        .mergedTimelineWaitCount = 1u,
        .inheritedTimelineWaitElisionCount = 1u,
        .submissionSeconds = 0.125,
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
    BuildTestPacketSubmissionFrameGraph(
        testArena.arena,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics
    );
    ++nodes[0u].runtimeStatistics.submission.plannedWaitTokenCount;
    ++nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.plannedWaitTokenCount;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.inheritedTimelineWaitElisionCount;
    ++packetSubmissionStatistics[s_ThirdElementIndex].plannedWaitTokenCount;
    ++packetSubmissionStatistics[s_ThirdElementIndex].inheritedTimelineWaitElisionCount;

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, payload
    ));
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));
    EXPECT_EQ(parsed->nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount, 1u);
    EXPECT_EQ(parsed->physicalQueueRuntimeStatistics[1u].statistics.submission.inheritedTimelineWaitElisionCount, 1u);
    EXPECT_EQ(parsed->packetSubmissionStatistics[1u].inheritedTimelineWaitElisionCount, 1u);

    const usize computePacketOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics) * s_ExpectedDualCount
        + sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics)
    ;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics encoded;
    NWB_MEMCPY(&encoded, sizeof(encoded), payload.data() + computePacketOffset, sizeof(encoded));
    encoded.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    NWB_MEMCPY(payload.data() + computePacketOffset, payload.size() - computePacketOffset, &encoded, sizeof(encoded));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount = 0u;
    ++nodes[0u].runtimeStatistics.submission.timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphRuntimeStatistics(nodes[0u].runtimeStatistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, payload
    ));
    EXPECT_TRUE(payload.empty());

    nodes[0u].runtimeStatistics.submission.inheritedTimelineWaitElisionCount = 1u;
    --nodes[0u].runtimeStatistics.submission.timelineWaitCount;
    physicalQueueRuntimeStatistics[0u].statistics.submission.inheritedTimelineWaitElisionCount = 0u;
    ++physicalQueueRuntimeStatistics[0u].statistics.submission.timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(physicalQueueRuntimeStatistics[0u].statistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, payload
    ));
    EXPECT_TRUE(payload.empty());

    physicalQueueRuntimeStatistics.clear();
    packetSubmissionStatistics[s_ThirdElementIndex].inheritedTimelineWaitElisionCount = 0u;
    ++packetSubmissionStatistics[s_ThirdElementIndex].timelineWaitCount;
    ASSERT_TRUE(Telemetry::IsValidFrameGraphPacketSubmissionStatistics(packetSubmissionStatistics[s_ThirdElementIndex]));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 921u, nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, payload
    ));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, PacketSubmissionEncodingIsIndependentOfInputOrder){
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
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecord firstPhysicalQueue =
        physicalQueueRuntimeStatistics[0u]
    ;
    physicalQueueRuntimeStatistics[0u] = physicalQueueRuntimeStatistics[1u];
    physicalQueueRuntimeStatistics[1u] = firstPhysicalQueue;
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetZero = packetSubmissionStatistics[1u];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetOne = packetSubmissionStatistics[s_ThirdElementIndex];
    const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packetTwo = packetSubmissionStatistics[0u];
    packetSubmissionStatistics[0u] = packetZero;
    packetSubmissionStatistics[1u] = packetOne;
    packetSubmissionStatistics[s_ThirdElementIndex] = packetTwo;

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

TEST(Telemetry, FrameGraphPacketSubmissionStatisticsPreservesExactEmptyAndAbsent){
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
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));
    EXPECT_TRUE(parsed->packetSubmissionStatisticsPresent);
    EXPECT_TRUE(parsed->packetSubmissionStatistics.empty());

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 919u, nodes, edges, payload));
    ASSERT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));
    EXPECT_FALSE(parsed->packetSubmissionStatisticsPresent);
    EXPECT_TRUE(parsed->packetSubmissionStatistics.empty());
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
    packetSubmissionStatistics[1u].packetIndex = packetSubmissionStatistics[s_ThirdElementIndex].packetIndex;
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
    packetSubmissionStatistics[s_ThirdElementIndex].commandListCount = 1u;
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
    const usize packetSubmissionStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics)
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics) * s_ExpectedDualCount
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
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

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
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

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
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ++header.packetSubmissionStatisticsCount;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

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
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

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
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        920u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

