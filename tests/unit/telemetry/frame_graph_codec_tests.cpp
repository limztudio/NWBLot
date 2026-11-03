// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <gtest/gtest.h>

#include "frame_graph_test_helpers.h"


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

