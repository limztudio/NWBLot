// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;



using namespace TelemetryTestDetail;



TEST(Telemetry, FrameGraphPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 905u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.frameIndex, 905u);
    ASSERT_EQ(parsed.nodes.size(), 3u);
    ASSERT_EQ(parsed.edges.size(), s_ExpectedDualCount);
    EXPECT_EQ(parsed.nodes[0u].name, Name("gbuffer"));
    EXPECT_EQ(parsed.nodes[0u].label, "GBuffer Pass");
    EXPECT_EQ(parsed.nodes[0u].kind, Telemetry::FrameGraphNodeKind::Pass);
    EXPECT_EQ(parsed.nodes[0u].flags, 1u);
    EXPECT_EQ(parsed.nodes[1u].name, Name("albedo"));
    EXPECT_EQ(parsed.nodes[1u].label, "Albedo Texture");
    EXPECT_EQ(parsed.nodes[1u].kind, Telemetry::FrameGraphNodeKind::Resource);
    EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].name, Name("lighting"));
    EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].label, "Lighting Pass");
    EXPECT_EQ(parsed.edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed.edges[0u].toNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[0u].kind, Telemetry::FrameGraphEdgeKind::Writes);
    EXPECT_EQ(parsed.edges[1u].fromNodeIndex, 1u);
    EXPECT_EQ(parsed.edges[1u].toNodeIndex, s_ExpectedDualCount);
    EXPECT_EQ(parsed.edges[1u].kind, Telemetry::FrameGraphEdgeKind::Reads);
    EXPECT_EQ(parsed.edges[1u].flags, s_ExpectedDualCount);
    EXPECT_FALSE(parsed.nodes[0u].queueAssignment.present);
    EXPECT_FALSE(parsed.nodes[1u].queueAssignment.present);
    EXPECT_FALSE(parsed.nodes[s_ThirdElementIndex].queueAssignment.present);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());

    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);

    payload[0u] = 0u;
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 906u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, s_ExpectedDualCount);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());
    const Telemetry::FrameGraphQueueAssignment& changed = parsed.nodes[0u].queueAssignment;
    EXPECT_TRUE(changed.present);
    EXPECT_EQ(changed.initialQueue.index, 1u);
    EXPECT_EQ(changed.initialQueue.deviceGeneration, 17u);
    EXPECT_EQ(changed.plannedQueue.index, 3u);
    EXPECT_EQ(changed.acceptedQueue, changed.plannedQueue);
    EXPECT_EQ(changed.previousAcceptedQueue.index, s_ExpectedDualCount);
    EXPECT_EQ(changed.previousAcceptedQueue.deviceGeneration, 17u);
    EXPECT_EQ(changed.queueClass, Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(changed.reason, Telemetry::FrameGraphQueueAssignmentReason::Scored);
    EXPECT_EQ(changed.modifiers, Telemetry::FrameGraphQueueAssignmentModifier::All);
    EXPECT_TRUE(changed.dedicated);
    EXPECT_EQ(changed.score.overlap, 7);
    EXPECT_EQ(changed.score.queueLoad, 3);
    EXPECT_EQ(changed.score.incomingCrossings, 2);
    EXPECT_EQ(changed.score.outgoingCrossings, 1);
    EXPECT_EQ(changed.score.ownershipTransfers, 4);
    EXPECT_EQ(changed.score.total, -3);
    EXPECT_EQ(changed.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::Changed);

    EXPECT_FALSE(parsed.nodes[1u].queueAssignment.present);
    const Telemetry::FrameGraphQueueAssignment& notAccepted = parsed.nodes[s_ThirdElementIndex].queueAssignment;
    EXPECT_TRUE(notAccepted.present);
    EXPECT_EQ(notAccepted.initialQueue.index, 4u);
    EXPECT_EQ(notAccepted.plannedQueue.index, 5u);
    EXPECT_FALSE(notAccepted.acceptedQueue.valid());
    EXPECT_EQ(notAccepted.previousAcceptedQueue.index, s_ExpectedDualCount);
    EXPECT_EQ(notAccepted.queueClass, Telemetry::FrameGraphQueueClass::Transfer);
    EXPECT_EQ(notAccepted.reason, Telemetry::FrameGraphQueueAssignmentReason::Scored);
    EXPECT_EQ(notAccepted.modifiers, Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback);
    EXPECT_FALSE(notAccepted.dedicated);
    EXPECT_EQ(notAccepted.score.total, -4);
    EXPECT_EQ(notAccepted.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted);
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * s_ExpectedDualCount)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, s_ExpectedDualCount);
    EXPECT_EQ(header.compiledTaskCount, s_ExpectedDualCount);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.nodes.size(), 3u);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());
    EXPECT_TRUE(parsed.nodes[0u].compiledTask.present);
    EXPECT_EQ(parsed.nodes[0u].compiledTask.planGeneration, 41u);
    EXPECT_EQ(parsed.nodes[0u].compiledTask.packetIndex, 7u);
    EXPECT_EQ(
        parsed.nodes[0u].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    EXPECT_FALSE(parsed.nodes[1u].compiledTask.present);
    EXPECT_TRUE(parsed.nodes[s_ThirdElementIndex].compiledTask.present);
    EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].compiledTask.planGeneration, 41u);
    EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].compiledTask.packetIndex, 7u);
    EXPECT_EQ(
        parsed.nodes[s_ThirdElementIndex].compiledTask.packetizationDecision,
        Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit
    );
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
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
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

    const usize historicalHeaderBytes[] = { 28u, 32u, 36u, 40u, 44u, 44u, 48u, 52u };
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

