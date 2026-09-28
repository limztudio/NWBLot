// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;



using namespace TelemetryTestDetail;



TEST(Telemetry, FrameGraphLegacyV1PayloadRoundTrip){
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
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphLegacyPayloadVersion);
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

    Telemetry::EncodedFrameGraphPayloadHeader legacyHeader;
    NWB_MEMCPY(&legacyHeader, sizeof(legacyHeader), payload.data(), sizeof(legacyHeader));
    EXPECT_EQ(legacyHeader.version, Telemetry::s_FrameGraphLegacyPayloadVersion);

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
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV9)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV9 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphAutomaticQueueAssignmentPayloadVersion);
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

TEST(Telemetry, FrameGraphLegacyQueueAssignmentsPreserveRoutesAndActiveScores){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 906u, nodes, edges, currentPayload));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV9ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion,
        legacyPayload
    ));
    const usize assignmentOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
    ;
    constexpr usize s_LegacyAssignmentBytes = 56u;
    constexpr usize s_LegacyPreferenceOffset = 20u;
    constexpr usize s_LegacyTotalOffset = 44u;
    constexpr usize s_LegacyReasonOffset = 49u;
    const i32 firstPreference = 11;
    const i32 firstTotal = 8;
    const i32 secondPreference = 5;
    const i32 secondTotal = 1;
    const auto writeLegacyScore = [&](const usize recordOffset, const i32 preference, const i32 total){
        NWB_MEMCPY(
            legacyPayload.data() + recordOffset + s_LegacyPreferenceOffset,
            legacyPayload.size() - recordOffset - s_LegacyPreferenceOffset,
            &preference,
            sizeof(preference)
        );
        NWB_MEMCPY(
            legacyPayload.data() + recordOffset + s_LegacyTotalOffset,
            legacyPayload.size() - recordOffset - s_LegacyTotalOffset,
            &total,
            sizeof(total)
        );
    };
    writeLegacyScore(assignmentOffset, firstPreference, firstTotal);
    writeLegacyScore(assignmentOffset + s_LegacyAssignmentBytes, secondPreference, secondTotal);

    for(u8 reason = 1u; reason <= 9u; ++reason){
        legacyPayload[assignmentOffset + s_LegacyReasonOffset] = reason;
        Telemetry::FrameGraphPayload parsed(testArena.arena);
        ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, legacyPayload.data(), legacyPayload.size(), parsed));
        EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
        const Telemetry::FrameGraphQueueAssignment& changed = parsed.nodes[0u].queueAssignment;
        EXPECT_EQ(changed.initialQueue, nodes[0u].queueAssignment.initialQueue);
        EXPECT_EQ(changed.plannedQueue, nodes[0u].queueAssignment.plannedQueue);
        EXPECT_EQ(changed.acceptedQueue, nodes[0u].queueAssignment.acceptedQueue);
        EXPECT_EQ(changed.previousAcceptedQueue, nodes[0u].queueAssignment.previousAcceptedQueue);
        EXPECT_EQ(changed.queueClass, Telemetry::FrameGraphQueueClass::Compute);
        EXPECT_EQ(changed.modifiers, Telemetry::FrameGraphQueueAssignmentModifier::All);
        EXPECT_EQ(changed.acceptance, Telemetry::FrameGraphQueueAssignmentAcceptance::Changed);
        EXPECT_TRUE(changed.dedicated);
        EXPECT_EQ(changed.score.overlap, 7);
        EXPECT_EQ(changed.score.queueLoad, 3);
        EXPECT_EQ(changed.score.incomingCrossings, 2);
        EXPECT_EQ(changed.score.outgoingCrossings, 1);
        EXPECT_EQ(changed.score.ownershipTransfers, 4);
        EXPECT_EQ(changed.score.total, -3);
        const Telemetry::FrameGraphQueueAssignmentReason::Enum expectedReason = reason == 1u
            ? Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics
            : (reason == 6u ? Telemetry::FrameGraphQueueAssignmentReason::Conservative : Telemetry::FrameGraphQueueAssignmentReason::Scored)
        ;
        EXPECT_EQ(changed.reason, expectedReason);
        EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].queueAssignment.plannedQueue, nodes[s_ThirdElementIndex].queueAssignment.plannedQueue);
        EXPECT_EQ(parsed.nodes[s_ThirdElementIndex].queueAssignment.score.total, -4);
    }

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    legacyPayload[assignmentOffset + s_LegacyReasonOffset] = 10u;
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, legacyPayload.data(), legacyPayload.size(), parsed));
    legacyPayload[assignmentOffset + s_LegacyReasonOffset] = 9u;
    writeLegacyScore(assignmentOffset, firstPreference, firstTotal + 1);
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, legacyPayload.data(), legacyPayload.size(), parsed));
}

TEST(Telemetry, FrameGraphLegacyV2AssignmentsAndV3CompiledTasksDecode){
    TestArena testArena;
    const u16 versions[] = {
        Telemetry::s_FrameGraphQueueAssignmentPayloadVersion,
        Telemetry::s_FrameGraphCompiledTaskPayloadVersion,
    };
    for(const u16 version : versions){
        SCOPED_TRACE(version);
        const bool hasCompiledTasks = version == Telemetry::s_FrameGraphCompiledTaskPayloadVersion;
        Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
        Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
        if(hasCompiledTasks)
            BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
        else
            BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

        Telemetry::TelemetryBytes currentPayload(testArena.arena);
        ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 906u, nodes, edges, currentPayload));
        Telemetry::TelemetryBytes legacyPayload(testArena.arena);
        if(hasCompiledTasks){
            EXPECT_FALSE(ConvertFrameGraphPayloadV9ToLegacy(
                currentPayload,
                Telemetry::s_FrameGraphQueueAssignmentPayloadVersion,
                legacyPayload
            ));
        }
        ASSERT_TRUE(ConvertFrameGraphPayloadV9ToLegacy(currentPayload, version, legacyPayload));
        const usize headerBytes = hasCompiledTasks
            ? sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV3)
            : sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV2)
        ;
        constexpr usize s_LegacyAssignmentBytes = 56u;
        EXPECT_EQ(legacyPayload.size(), headerBytes
            + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
            + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
            + s_LegacyAssignmentBytes * s_ExpectedDualCount
            + (hasCompiledTasks ? sizeof(Telemetry::EncodedFrameGraphCompiledTask) * s_ExpectedDualCount : 0u)
            + sizeof("GBuffer Pass") + sizeof("Albedo Texture") + sizeof("Lighting Pass")
        );

        Telemetry::FrameGraphPayload parsed(testArena.arena);
        ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, legacyPayload.data(), legacyPayload.size(), parsed));
        EXPECT_EQ(parsed.wireVersion, version);
        EXPECT_EQ(parsed.frameIndex, 906u);
        ASSERT_EQ(parsed.nodes.size(), nodes.size());
        EXPECT_EQ(parsed.edges.size(), edges.size());
        EXPECT_FALSE(parsed.nodes[1u].queueAssignment.present);
        const u32 taskIndices[] = { 0u, s_ThirdElementIndex };
        for(const u32 taskIndex : taskIndices){
            const Telemetry::FrameGraphQueueAssignment& assignment = parsed.nodes[taskIndex].queueAssignment;
            const Telemetry::FrameGraphQueueAssignment& expected = nodes[taskIndex].queueAssignment;
            EXPECT_TRUE(assignment.present);
            EXPECT_EQ(assignment.initialQueue, expected.initialQueue);
            EXPECT_EQ(assignment.plannedQueue, expected.plannedQueue);
            EXPECT_EQ(assignment.acceptedQueue, expected.acceptedQueue);
            EXPECT_EQ(assignment.previousAcceptedQueue, expected.previousAcceptedQueue);
            EXPECT_EQ(assignment.queueClass, expected.queueClass);
            EXPECT_EQ(assignment.reason, expected.reason);
            EXPECT_EQ(assignment.modifiers, expected.modifiers);
            EXPECT_EQ(assignment.acceptance, expected.acceptance);
            EXPECT_EQ(assignment.dedicated, expected.dedicated);
            EXPECT_EQ(assignment.score.overlap, expected.score.overlap);
            EXPECT_EQ(assignment.score.queueLoad, expected.score.queueLoad);
            EXPECT_EQ(assignment.score.incomingCrossings, expected.score.incomingCrossings);
            EXPECT_EQ(assignment.score.outgoingCrossings, expected.score.outgoingCrossings);
            EXPECT_EQ(assignment.score.ownershipTransfers, expected.score.ownershipTransfers);
            EXPECT_EQ(assignment.score.total, expected.score.total);
            const Telemetry::FrameGraphCompiledTask& compiled = parsed.nodes[taskIndex].compiledTask;
            const Telemetry::FrameGraphCompiledTask& expectedCompiled = nodes[taskIndex].compiledTask;
            EXPECT_EQ(compiled.present, hasCompiledTasks);
            if(hasCompiledTasks){
                EXPECT_EQ(compiled.planGeneration, expectedCompiled.planGeneration);
                EXPECT_EQ(compiled.packetIndex, expectedCompiled.packetIndex);
                EXPECT_EQ(compiled.packetizationDecision, expectedCompiled.packetizationDecision);
            }
        }
    }
}

TEST(Telemetry, FrameGraphCompiledTaskPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV9)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * s_ExpectedDualCount)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV9 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphAutomaticQueueAssignmentPayloadVersion);
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

TEST(Telemetry, FrameGraphPayloadRejectsUnknownVersion){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 907u, nodes, edges, payload));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.version = 99u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
}

TEST(Telemetry, FrameGraphQueueAssignmentPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 908u, nodes, edges, payload));
    const usize assignmentOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV9)
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
    const usize compiledTaskOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV9)
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

