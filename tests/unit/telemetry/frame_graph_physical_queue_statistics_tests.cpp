// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_physical_queue_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;



using namespace TelemetryTestDetail;

using EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation = void(*)(
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics&
);

static constexpr EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutation
s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations[] = {
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.reserved[6u] = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.ownerNodeIndex = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.queue.deviceGeneration = 18u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.queueClass = Telemetry::FrameGraphQueueClass::Unknown;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.recordingSeconds = -1.0;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        --statistics.submission.timelineWaitCount;
        ++statistics.submission.inheritedTimelineWaitElisionCount;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission = {};
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.inheritedTimelineWaitElisionCount = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.recoverySubmissionCount =
            statistics.submission.acceptedFrontierSubmissionCount + 1u
        ;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.taskCount = 79u;
        statistics.compile.packetCount = 78u;
        statistics.compile.mergedTaskCount = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.taskCount = 51u;
        statistics.compile.packetCount = 50u;
        statistics.compile.mergedTaskCount = 1u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.compile.prologueBarrierCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.barrierCount = 24u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = 22u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = 23u;
        statistics.submission.rejectedTaskCount = 24u;
        statistics.submission.rejectedSubmissionCount = 23u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics){
        statistics.compile.incomingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingLogicalOwnershipTransferSignatureCount = 0u;
        statistics.compile.incomingRepeatedOwnershipTransferSignatureCount = 0u;
        statistics.compile.outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.recording = {};
        statistics.recording.commandListCount = 1u;
        statistics.submission = {};
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics){
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
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics){
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
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = 0u;
        statistics.submission.rejectedTaskCount = 1u;
        statistics.submission.rejectedSubmissionCount = 0u;
    },
    [](Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics& statistics){
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


TEST(Telemetry, PhysicalQueueDecodeShrinkingInputClearsPreviousRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    BuildTestPhysicalQueueRuntimeStatistics(testArena.arena, records);
    Telemetry::TelemetryBytes payload(testArena.arena);
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 916u, nodes, edges, records, payload));
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), s_ExpectedDualCount);

    records.resize(1u);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 916u, nodes, edges, records, payload));
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), parsed));
    ASSERT_EQ(parsed.physicalQueueRuntimeStatistics.size(), 1u);
    EXPECT_EQ(parsed.physicalQueueRuntimeStatistics[0u].statistics.queue.index, 3u);
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
    records[0u].statistics.compile.mergedTaskCount = s_ExpectedDualCount;
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
    const usize statisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * s_ExpectedDualCount
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics) * s_ExpectedDualCount
    ;
    Telemetry::FrameGraphPayload parsed(testArena.arena);
    for(
        usize mutationIndex = 0u;
        mutationIndex < LengthOf(s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsMutations);
        ++mutationIndex
    ){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(testArena.arena, 917u, nodes, edges, records, payload));
        Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
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
        + sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics)
    ;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics encodedStatistics;
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
    Telemetry::EncodedFrameGraphPayloadHeader header;
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

