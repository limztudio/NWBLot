// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


using namespace TelemetryTestDetail;


TEST(Telemetry, FrameGraphPayloadRejectsCorruptedHeaderAfterValidParse){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 905u, nodes, edges, payload));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphPayloadRejectsNonCurrentVersions){
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
        907u,
        nodes,
        edges,
        physicalQueueRuntimeStatistics,
        packetSubmissionStatistics,
        payload
    ));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ASSERT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);
    ASSERT_GT(header.nodeCount, 0u);
    ASSERT_GT(header.runtimeStatisticsCount, 0u);
    ASSERT_GT(header.physicalQueueRuntimeStatisticsCount, 0u);
    ASSERT_GT(header.packetSubmissionStatisticsCount, 0u);

    const u16 unsupportedVersions[] = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
        static_cast<u16>(Telemetry::s_FrameGraphPayloadVersion + 1u),
        Limit<u16>::s_Max,
    };
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        header.version = Telemetry::s_FrameGraphPayloadVersion;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
        ASSERT_FALSE(parsed.nodes.empty());
        ASSERT_FALSE(parsed.physicalQueueRuntimeStatistics.empty());
        ASSERT_FALSE(parsed.packetSubmissionStatistics.empty());

        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
        EXPECT_TRUE(parsed.nodes.empty());
        EXPECT_TRUE(parsed.edges.empty());
        EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());
        EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());
        EXPECT_EQ(parsed.frameIndex, 0u);
        EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());
        EXPECT_FALSE(parsed.packetSubmissionStatisticsPresent);
    }
}

TEST(Telemetry, FrameGraphPayloadRejectsHistoricalEmptyPayloads){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader));

    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 907u);

    const usize historicalHeaderBytes[] = { 28u, 32u, 36u, 40u, 44u, 44u, 48u, 52u, 52u };
    for(usize versionIndex = 0u; versionIndex < LengthOf(historicalHeaderBytes); ++versionIndex){
        header.version = static_cast<u16>(versionIndex + 1u);
        SCOPED_TRACE(header.version);
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(
            testArena.arena,
            payload.data(),
            historicalHeaderBytes[versionIndex],
            parsed
        ));
        EXPECT_EQ(parsed.frameIndex, 0u);
        EXPECT_TRUE(parsed.nodes.empty());
        EXPECT_TRUE(parsed.edges.empty());
    }
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    const usize assignmentOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
    ;
    Telemetry::EncodedFrameGraphQueueAssignment first;
    Telemetry::EncodedFrameGraphQueueAssignment second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + assignmentOffset + sizeof(first),
        sizeof(second)
    );

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + assignmentOffset + sizeof(first),
        payload.size() - assignmentOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + assignmentOffset + sizeof(first),
        sizeof(second)
    );
    first.nodeIndex = s_ExpectedDualCount;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    NWB_MEMCPY(
        payload.data() + assignmentOffset + sizeof(first),
        payload.size() - assignmentOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.modifiers = static_cast<u8>(1u << 7u);
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    ++first.scoreTotal;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.acceptedQueue.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + assignmentOffset, sizeof(first));
    first.previousAcceptedQueue.index = Limit<u16>::s_Max;
    NWB_MEMCPY(payload.data() + assignmentOffset, payload.size() - assignmentOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    const usize compiledTaskOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount
    ;
    Telemetry::EncodedFrameGraphCompiledTask first;
    Telemetry::EncodedFrameGraphCompiledTask second;
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    NWB_MEMCPY(
        &second,
        sizeof(second),
        payload.data() + compiledTaskOffset + sizeof(first),
        sizeof(second)
    );

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + compiledTaskOffset + sizeof(first),
        payload.size() - compiledTaskOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetIndex = Limit<u32>::s_Max;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::Unknown;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::kCount;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 909u, nodes, edges, payload));
    NWB_MEMCPY(&first, sizeof(first), payload.data() + compiledTaskOffset, sizeof(first));
    first.reserved[0u] = 1u;
    NWB_MEMCPY(payload.data() + compiledTaskOffset, payload.size() - compiledTaskOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

