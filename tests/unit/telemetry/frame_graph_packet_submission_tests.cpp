// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/simd/vector.h>

#include <gtest/gtest.h>

#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_packet_submission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


TEST(Telemetry, PacketSubmissionStatisticsRejectInconsistentCountsAndNonfiniteTime){
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord statistics{
        .packetGeneration = 72u,
        .taskCount = 2u,
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

