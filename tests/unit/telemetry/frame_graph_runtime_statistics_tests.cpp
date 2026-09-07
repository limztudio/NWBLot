// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_runtime_statistics_tests{


using namespace TelemetryTestDetail;

using FrameGraphRuntimeStatisticsMutation = void(*)(Telemetry::FrameGraphRuntimeStatistics&);

static constexpr FrameGraphRuntimeStatisticsMutation s_FrameGraphRuntimeStatisticsCountMutations[] = {
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.packetCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.mergedTaskCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.resourceVersionCount = 0u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.directResourceUseCount = statistics.compile.resourceUseCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.expandedResourceSetMemberUseCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.payloadObjectCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.repeatedOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferSignatureCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.concurrentSharingCouldAvoidTransferCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.concurrentSharingAdviceResourceCount = statistics.compile.resourceCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferInternalCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.compile.logicalOwnershipTransferExternalImportCount =
            statistics.compile.logicalOwnershipTransferCount
            - statistics.compile.logicalOwnershipTransferInternalCount
            + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.compile.logicalOwnershipTransferExternalExportCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.packetCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.taskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.taskCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.commandListCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.workerRoutedPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.recording.parallelPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedPacketCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedPacketCount = statistics.compile.packetCount
            - statistics.submission.acceptedPacketCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedTaskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedTaskCount = statistics.compile.taskCount
            - statistics.submission.acceptedTaskCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedTaskCount = statistics.submission.acceptedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedTaskCount = statistics.submission.rejectedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedPacketCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.rejectedSubmissionCount = statistics.submission.rejectedPacketCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.acceptedFrontierSubmissionCount = statistics.submission.nativeSubmissionCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.recoverySubmissionCount = statistics.submission.acceptedFrontierSubmissionCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeSubmissionCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeCommandListCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.nativeCommandListCount = statistics.recording.commandListCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.sameQueueWaitElisionCount = statistics.submission.plannedWaitTokenCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        statistics.submission.mergedTimelineWaitCount = statistics.submission.plannedWaitTokenCount
            - statistics.submission.sameQueueWaitElisionCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics){
        ++statistics.submission.timelineWaitCount;
    },
};


TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRoundTrip){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 910u, nodes, edges, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8) * 2u)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_EQ(header.queueAssignmentCount, 2u);
    EXPECT_EQ(header.compiledTaskCount, 2u);
    EXPECT_EQ(header.runtimeStatisticsCount, 2u);
    EXPECT_EQ(header.physicalQueueRuntimeStatisticsCount, 0u);
    EXPECT_EQ(header.packetSubmissionStatisticsPresent, 0u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    ASSERT_EQ(parsed.nodes.size(), 3u);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatistics.empty());
    EXPECT_FALSE(parsed.physicalQueueRuntimeStatisticsPresent);
    EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());
    EXPECT_FALSE(parsed.packetSubmissionStatisticsPresent);
    const Telemetry::FrameGraphRuntimeStatistics expected = MakeFrameGraphRuntimeStatistics();
    const Telemetry::FrameGraphRuntimeStatistics& first = parsed.nodes[0u].runtimeStatistics;
    EXPECT_TRUE(first.present);
    EXPECT_EQ(first.graphGeneration, 51u);
    EXPECT_EQ(first.planGeneration, 52u);
    EXPECT_EQ(first.recordingAttemptGeneration, 53u);
    EXPECT_EQ(first.deviceGeneration, 17u);
    EXPECT_EQ(NWB_MEMCMP(&first.compile, &expected.compile, sizeof(expected.compile)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.recording, &expected.recording, sizeof(expected.recording)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.submission, &expected.submission, sizeof(expected.submission)), 0);
    EXPECT_EQ(first.submission.recoverySubmissionCount, 8u);
    EXPECT_FALSE(parsed.nodes[1u].runtimeStatistics.present);
    EXPECT_TRUE(parsed.nodes[2u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.graphGeneration, 61u);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.planGeneration, 62u);
    EXPECT_EQ(parsed.nodes[2u].runtimeStatistics.recordingAttemptGeneration, 63u);
}

TEST(Telemetry, FrameGraphRuntimeStatisticsV8WireFieldOrderIsStable){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 912u, nodes, edges, payload));
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
    ;
    ASSERT_GE(payload.size(), runtimeStatisticsOffset + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8));

    const auto readU16 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u16 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU32 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u32 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU64 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        u64 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readF64 = [&payload, runtimeStatisticsOffset](const usize wireOffset){
        f64 value = 0.0;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + runtimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };

    EXPECT_EQ(readU32(0u), 0u);
    EXPECT_EQ(readU16(4u), 17u);
    EXPECT_EQ(readU16(6u), 0u);
    EXPECT_EQ(readU64(8u), 51u);
    EXPECT_EQ(readU64(16u), 52u);
    EXPECT_EQ(readU64(24u), 53u);

    const u64 expectedCompileCounts[] = {
        78u, 2u, 50u, 4u, 5u, 76u, 7u, 2u, 9u, 10u,
        11u, 12u, 13u, 60u, 15u, 14u, 17u, 2u, 19u, 20u,
        21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedCompileCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(32u + fieldIndex * sizeof(u64)), expectedCompileCounts[fieldIndex]);
    const f64 expectedCompileSeconds[] = {
        0.001, 0.002, 0.003, 0.004, 0.005, 0.006,
        0.007, 0.008, 0.009, 0.010, 0.011, 0.012,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedCompileSeconds);
        ++fieldIndex
    )
        EXPECT_DOUBLE_EQ(readF64(272u + fieldIndex * sizeof(f64)), expectedCompileSeconds[fieldIndex]);
    EXPECT_EQ(readU64(368u), 3u);
    EXPECT_EQ(readU64(376u), 6u);

    const u64 expectedRecordingCounts[] = { 31u, 32u, 33u, 34u, 30u, 29u };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedRecordingCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(384u + fieldIndex * sizeof(u64)), expectedRecordingCounts[fieldIndex]);
    const f64 expectedRecordingSeconds[] = {
        0.013, 0.014, 0.015, 0.016, 0.017, 0.018, 0.019, 0.020,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedRecordingSeconds);
        ++fieldIndex
    )
        EXPECT_DOUBLE_EQ(readF64(432u + fieldIndex * sizeof(f64)), expectedRecordingSeconds[fieldIndex]);

    const u64 expectedSubmissionCounts[] = {
        37u, 38u, 39u, 40u, 30u, 38u, 32u, 44u, 12u, 14u, 18u, 28u,
    };
    for(
        usize fieldIndex = 0u;
        fieldIndex < LengthOf(expectedSubmissionCounts);
        ++fieldIndex
    )
        EXPECT_EQ(readU64(496u + fieldIndex * sizeof(u64)), expectedSubmissionCounts[fieldIndex]);
    EXPECT_DOUBLE_EQ(readF64(592u), 0.021);
    EXPECT_EQ(readU64(600u), 8u);
}

TEST(Telemetry, FrameGraphRuntimeStatisticsV4PayloadDefaultsRecoverySubmissionCountToZero){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 912u, nodes, edges, currentPayload));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV8ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion,
        legacyPayload
    ));

    Telemetry::EncodedFrameGraphPayloadHeaderV4 header;
    NWB_MEMCPY(&header, sizeof(header), legacyPayload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion);
    EXPECT_EQ(header.runtimeStatisticsCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(
        testArena.arena,
        legacyPayload.data(),
        legacyPayload.size(),
        parsed
    ));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphRuntimeStatisticsPayloadVersion);
    EXPECT_FALSE(parsed.physicalQueueRuntimeStatisticsPresent);
    ASSERT_TRUE(parsed.nodes[0u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionCount, 0u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionEdgeCount, 0u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.recoverySubmissionCount, 0u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.acceptedFrontierSubmissionCount, 28u);
    EXPECT_DOUBLE_EQ(parsed.nodes[0u].runtimeStatistics.submission.submissionSeconds, 0.021);
}

TEST(Telemetry, FrameGraphRuntimeStatisticsV7PayloadDefaultsResourceVersionStatisticsToZero){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.submission = {};
    nodes[2u].runtimeStatistics.submission = {};

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 913u, nodes, edges, currentPayload));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV8ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphPacketSubmissionStatisticsPayloadVersion,
        legacyPayload
    ));

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(
        testArena.arena,
        legacyPayload.data(),
        legacyPayload.size(),
        parsed
    ));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphPacketSubmissionStatisticsPayloadVersion);
    EXPECT_TRUE(parsed.packetSubmissionStatisticsPresent);
    EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());
    ASSERT_TRUE(parsed.nodes[0u].runtimeStatistics.present);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionCount, 0u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.compile.resourceVersionEdgeCount, 0u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.recoverySubmissionCount, 0u);
}

TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
    ;
    Telemetry::EncodedFrameGraphRuntimeStatisticsV8 first;
    Telemetry::EncodedFrameGraphRuntimeStatisticsV8 second;
    const auto loadRuntimeStatistics = [&]()->bool{
        if(!Telemetry::BuildFrameGraphPayload(testArena.arena, 911u, nodes, edges, payload))
            return false;
        NWB_MEMCPY(
            &first,
            sizeof(first),
            payload.data() + runtimeStatisticsOffset,
            sizeof(first)
        );
        NWB_MEMCPY(
            &second,
            sizeof(second),
            payload.data() + runtimeStatisticsOffset + sizeof(first),
            sizeof(second)
        );
        return true;
    };
    Telemetry::FrameGraphPayload parsed(testArena.arena);

    ASSERT_TRUE(loadRuntimeStatistics());
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 2u;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 3u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.reserved = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.graphGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recordingAttemptGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.compile.declarationSeconds = -1.0;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recording.recordingSeconds = Limit<f64>::s_Infinity;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(loadRuntimeStatistics());
        Telemetry::FrameGraphRuntimeStatistics malformed = MakeFrameGraphRuntimeStatistics();
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex](malformed);
        first = EncodeTestFrameGraphRuntimeStatisticsV8(malformed, first.nodeIndex, first.reserved);
        NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    }

    ASSERT_TRUE(loadRuntimeStatistics());
    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.runtimeStatisticsCount = 4u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(loadRuntimeStatistics());
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u, parsed));
}

TEST(Telemetry, FrameGraphPayloadRejectsInvalidInput){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestFrameGraph(testArena.arena, nodes, edges);

    nodes[0u].kind = Telemetry::FrameGraphNodeKind::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].label = AStringView("bad\0label", 9u);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    edges[0u].toNodeIndex = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].queueAssignment.acceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::First;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestAssignedFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].queueAssignment = MakeChangedFrameGraphQueueAssignment();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestCompiledFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].compiledTask = MakeFrameGraphCompiledTask(
        41u,
        7u,
        Telemetry::FrameGraphTaskPacketizationDecision::FirstTask
    );
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestFrameGraph(testArena.arena, nodes, edges);
    nodes[1u].runtimeStatistics = MakeFrameGraphRuntimeStatistics();
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.graphGeneration = 0u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.compile.totalSeconds = -1.0;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    nodes[0u].runtimeStatistics.recording.recordingElapsedSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex](nodes[0u].runtimeStatistics);
        EXPECT_FALSE(Telemetry::IsValidFrameGraphRuntimeStatistics(nodes[0u].runtimeStatistics));
        EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 1u, nodes, edges, payload));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

