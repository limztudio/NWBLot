// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_physical_queue_statistics_tests{


using namespace TelemetryTestDetail;

using EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation = void(*)(
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6&
);

static constexpr EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation
s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations[] = {
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.reserved[6u] = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.ownerNodeIndex = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.queue.deviceGeneration = 18u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.queueClass = Telemetry::FrameGraphQueueClass::Unknown;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.recording.recordingSeconds = -1.0;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.recoverySubmissionCount =
            statistics.submission.acceptedFrontierSubmissionCount + 1u
        ;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.compile.taskCount = 79u;
        statistics.compile.packetCount = 78u;
        statistics.compile.mergedTaskCount = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.compile.taskCount = 51u;
        statistics.compile.packetCount = 50u;
        statistics.compile.mergedTaskCount = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.compile.prologueBarrierCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.recording.barrierCount = 24u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.recording.taskCount = 22u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.rejectedPacketCount = 23u;
        statistics.submission.rejectedTaskCount = 24u;
        statistics.submission.rejectedSubmissionCount = 23u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.compile.incomingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.incomingRepeatedOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.recording = {};
        statistics.recording.commandListCount = 1u;
        statistics.submission = {};
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.nativeSubmissionCount = 0u;
        statistics.submission.nativeCommandListCount = 0u;
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.sameQueueWaitElisionCount = 0u;
        statistics.submission.timelineWaitCount = 1u;
        statistics.submission.mergedTimelineWaitCount = 0u;
        statistics.submission.acceptedFrontierSubmissionCount = 0u;
        statistics.submission.recoverySubmissionCount = 0u;
        statistics.submission.submissionSeconds = 0.0;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.acceptedPacketCount = 0u;
        statistics.submission.acceptedTaskCount = 1u;
        statistics.submission.nativeSubmissionCount = 0u;
        statistics.submission.nativeCommandListCount = 0u;
        statistics.submission.plannedWaitTokenCount = 0u;
        statistics.submission.sameQueueWaitElisionCount = 0u;
        statistics.submission.timelineWaitCount = 0u;
        statistics.submission.mergedTimelineWaitCount = 0u;
        statistics.submission.acceptedFrontierSubmissionCount = 0u;
        statistics.submission.recoverySubmissionCount = 0u;
        statistics.submission.submissionSeconds = 0.0;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.submission.rejectedPacketCount = 0u;
        statistics.submission.rejectedTaskCount = 1u;
        statistics.submission.rejectedSubmissionCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics){
        statistics.compile.taskCount = 0u;
        statistics.compile.packetCount = 0u;
        statistics.compile.mergedTaskCount = 0u;
        statistics.compile.prologueBarrierCount = 1u;
        statistics.compile.epilogueBarrierCount = 0u;
        statistics.compile.ownershipReleaseBarrierCount = 0u;
        statistics.compile.ownershipAcquireBarrierCount = 0u;
        statistics.recording = {};
        statistics.submission = {};
    },
};


TEST(Telemetry, FrameGraphPhysicalQueueRuntimeStatisticsPayloadRoundTripAndWireOrderIsStable){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);

    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 916u, nodes, edges, records, payload));
    EXPECT_EQ(payload.size(), sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
            + (sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size())
            + (sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size())
            + (sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8) * 2u)
            + (sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) * 2u)
            + sizeof("GBuffer Pass")
            + sizeof("Albedo Texture")
            + sizeof("Lighting Pass"));

    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_EQ(header.runtimeStatisticsCount, 2u);
    EXPECT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphResourceVersionStatisticsPayloadVersion);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatisticsPresent);
    EXPECT_TRUE(parsed.packetSubmissionStatistics.empty());
    EXPECT_FALSE(parsed.packetSubmissionStatisticsPresent);
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), 2u);
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& first =
        parsed.physicalQueueRuntimeStatistics[0u].statistics
    ;
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics expected =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)
    ;
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].ownerNodeIndex, 0u);
    EXPECT_EQ(first.queue.index, 1u);
    EXPECT_EQ(first.graphGeneration, 51u);
    EXPECT_EQ(first.planGeneration, 52u);
    EXPECT_EQ(first.recordingAttemptGeneration, 53u);
    EXPECT_EQ(first.deviceGeneration, 17u);
    EXPECT_EQ(NWB_MEMCMP(&first.compile, &expected.compile, sizeof(expected.compile)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.recording, &expected.recording, sizeof(expected.recording)), 0);
    EXPECT_EQ(NWB_MEMCMP(&first.submission, &expected.submission, sizeof(expected.submission)), 0);
    EXPECT_EQ(first.submission.recoverySubmissionCount, 5u);
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& second =
        parsed.physicalQueueRuntimeStatistics[1u].statistics
    ;
    const Telemetry::FrameGraphPhysicalQueueRuntimeStatistics expectedSecond =
        MakeFrameGraphPhysicalQueueRuntimeStatistics(3u)
    ;
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[1u].ownerNodeIndex, 0u);
    EXPECT_EQ(second.queue.index, 3u);
    EXPECT_EQ(second.queue.deviceGeneration, 17u);
    EXPECT_EQ(second.queueClass, Telemetry::FrameGraphQueueClass::Compute);
    EXPECT_EQ(second.graphGeneration, 51u);
    EXPECT_EQ(second.planGeneration, 52u);
    EXPECT_EQ(second.recordingAttemptGeneration, 53u);
    EXPECT_EQ(second.deviceGeneration, 17u);
    EXPECT_EQ(NWB_MEMCMP(&second.compile, &expectedSecond.compile, sizeof(expectedSecond.compile)), 0);
    EXPECT_EQ(NWB_MEMCMP(&second.recording, &expectedSecond.recording, sizeof(expectedSecond.recording)), 0);
    EXPECT_EQ(NWB_MEMCMP(&second.submission, &expectedSecond.submission, sizeof(expectedSecond.submission)), 0);
    EXPECT_EQ(second.submission.recoverySubmissionCount, 3u);

    const usize physicalQueueRuntimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8) * 2u
    ;
    const auto readU8 = [&payload, physicalQueueRuntimeStatisticsOffset](const usize wireOffset){
        u8 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + physicalQueueRuntimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU16 = [&payload, physicalQueueRuntimeStatisticsOffset](const usize wireOffset){
        u16 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + physicalQueueRuntimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU32 = [&payload, physicalQueueRuntimeStatisticsOffset](const usize wireOffset){
        u32 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + physicalQueueRuntimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readU64 = [&payload, physicalQueueRuntimeStatisticsOffset](const usize wireOffset){
        u64 value = 0u;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + physicalQueueRuntimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };
    const auto readF64 = [&payload, physicalQueueRuntimeStatisticsOffset](const usize wireOffset){
        f64 value = 0.0;
        NWB_MEMCPY(&value, sizeof(value), payload.data() + physicalQueueRuntimeStatisticsOffset + wireOffset, sizeof(value));
        return value;
    };

    EXPECT_EQ(readU32(0u), 0u);
    EXPECT_EQ(readU16(4u), 1u);
    EXPECT_EQ(readU16(6u), 17u);
    EXPECT_EQ(readU8(8u), Telemetry::FrameGraphQueueClass::Graphics);
    for(usize reservedIndex = 0u; reservedIndex < 7u; ++reservedIndex)
        EXPECT_EQ(readU8(9u + reservedIndex), 0u);

    const u64 expectedCompileCounts[] = {
        50u, 49u, 1u, 11u, 12u, 6u, 7u, 30u, 31u, 8u, 9u, 7u, 8u, 1u,
    };
    for(usize fieldIndex = 0u; fieldIndex < LengthOf(expectedCompileCounts); ++fieldIndex)
        EXPECT_EQ(readU64(16u + fieldIndex * sizeof(u64)), expectedCompileCounts[fieldIndex]);
    const u64 expectedRecordingCounts[] = { 20u, 21u, 21u, 23u, 19u, 18u };
    for(usize fieldIndex = 0u; fieldIndex < LengthOf(expectedRecordingCounts); ++fieldIndex)
        EXPECT_EQ(readU64(128u + fieldIndex * sizeof(u64)), expectedRecordingCounts[fieldIndex]);
    const f64 expectedRecordingSeconds[] = { 0.005, 0.006, 0.007, 0.008 };
    for(usize fieldIndex = 0u; fieldIndex < LengthOf(expectedRecordingSeconds); ++fieldIndex)
        EXPECT_DOUBLE_EQ(readF64(176u + fieldIndex * sizeof(f64)), expectedRecordingSeconds[fieldIndex]);
    const u64 expectedSubmissionCounts[] = {
        25u, 26u, 24u, 24u, 19u, 23u, 20u, 20u, 5u, 8u, 7u, 18u,
    };
    for(usize fieldIndex = 0u; fieldIndex < LengthOf(expectedSubmissionCounts); ++fieldIndex)
        EXPECT_EQ(readU64(208u + fieldIndex * sizeof(u64)), expectedSubmissionCounts[fieldIndex]);
    EXPECT_DOUBLE_EQ(readF64(304u), 0.011);
    EXPECT_EQ(readU64(312u), 5u);

    records.resize(1u);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 916u, nodes, edges, records, payload));
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), 1u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].statistics.queue.index, 3u);
}

TEST(Telemetry, FrameGraphPhysicalQueueRuntimeStatisticsV5PayloadDefaultsRecoverySubmissionCountsToZero){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);

    Telemetry::TelemetryBytes currentPayload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena,
        916u,
        nodes,
        edges,
        records,
        currentPayload
    ));
    Telemetry::TelemetryBytes legacyPayload(testArena.arena);
    ASSERT_TRUE(ConvertFrameGraphPayloadV8ToLegacy(
        currentPayload,
        Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion,
        legacyPayload
    ));

    Telemetry::EncodedFrameGraphPayloadHeaderV5 header;
    NWB_MEMCPY(&header, sizeof(header), legacyPayload.data(), sizeof(header));
    EXPECT_EQ(header.version, Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion);
    EXPECT_EQ(header.runtimeStatisticsCount, 2u);
    EXPECT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);

    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(
        testArena.arena,
        legacyPayload.data(),
        legacyPayload.size(),
        parsed
    ));
    EXPECT_EQ(parsed.wireVersion, Telemetry::s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion);
    EXPECT_TRUE(parsed.physicalQueueRuntimeStatisticsPresent);
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), 2u);
    EXPECT_EQ(parsed.nodes[0u].runtimeStatistics.submission.recoverySubmissionCount, 0u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].statistics.submission.recoverySubmissionCount, 0u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[1u].statistics.submission.recoverySubmissionCount, 0u);
    EXPECT_EQ(
        parsed.physicalQueueRuntimeStatistics[0u].statistics.submission.acceptedFrontierSubmissionCount,
        18u
    );
    EXPECT_EQ(
        parsed.physicalQueueRuntimeStatistics[1u].statistics.submission.acceptedFrontierSubmissionCount,
        10u
    );
}

TEST(Telemetry, FrameGraphPhysicalQueueRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    Telemetry::TelemetryBytes payload(testArena.arena);

    records[0u].statistics.graphGeneration = 99u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].ownerNodeIndex = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.queue.deviceGeneration = 18u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.queueClass = Telemetry::FrameGraphQueueClass::Unknown;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.recordingSeconds = -1.0;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.recoverySubmissionCount =
        records[0u].statistics.submission.acceptedFrontierSubmissionCount + 1u
    ;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.recoverySubmissionCount;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(records[0u].statistics));
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.compile.mergedTaskCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.timelineWaitCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.taskCount = 79u;
    records[0u].statistics.compile.packetCount = 78u;
    records[0u].statistics.compile.mergedTaskCount = 1u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.recordingSeconds = 0.017;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.taskCount = 29u;
    records[0u].statistics.compile.mergedTaskCount = 2u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ++records[0u].statistics.submission.plannedWaitTokenCount;
    ++records[0u].statistics.submission.timelineWaitCount;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.epilogueBarrierCount = 4u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.barrierCount = 12u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.recording.taskCount = 13u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.submission.acceptedTaskCount = 13u;
    records[0u].statistics.submission.rejectedPacketCount = 14u;
    records[0u].statistics.submission.rejectedTaskCount = 15u;
    records[0u].statistics.submission.rejectedSubmissionCount = 14u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records[0u].statistics.compile.incomingLogicalOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.outgoingLogicalOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.incomingRepeatedOwnershipTransferSignatureCount = 0u;
    records[0u].statistics.compile.outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    records.push_back(records[0u]);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));

    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    const usize statisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeaderV8)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * 2u
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * 2u
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8) * 2u
    ;
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    for(
        usize mutationIndex = 0u;
        mutationIndex < LengthOf(s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations);
        ++mutationIndex
    ){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
        Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 encodedStatistics;
        NWB_MEMCPY(
            &encodedStatistics,
            sizeof(encodedStatistics),
            payload.data() + statisticsOffset,
            sizeof(encodedStatistics)
        );
        s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations[mutationIndex](encodedStatistics);
        NWB_MEMCPY(
            payload.data() + statisticsOffset,
            payload.size() - statisticsOffset,
            &encodedStatistics,
            sizeof(encodedStatistics)
        );
        EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    }

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    const usize secondStatisticsOffset = statisticsOffset
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6)
    ;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 encodedStatistics;
    NWB_MEMCPY(
        &encodedStatistics,
        sizeof(encodedStatistics),
        payload.data() + secondStatisticsOffset,
        sizeof(encodedStatistics)
    );
    encodedStatistics.queue.index = 1u;
    NWB_MEMCPY(
        payload.data() + secondStatisticsOffset,
        payload.size() - secondStatisticsOffset,
        &encodedStatistics,
        sizeof(encodedStatistics)
    );
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    ++header.physicalQueueRuntimeStatisticsCount;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));

    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
    EXPECT_FALSE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u, parsed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

