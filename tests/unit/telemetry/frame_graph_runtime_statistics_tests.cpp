// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include <gtest/gtest.h>
#include "frame_graph_test_helpers.h"
#include "frame_graph_wire_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_runtime_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;



using namespace TelemetryTestDetail;

using FrameGraphRuntimeStatisticsMutation = void(*)(Telemetry::FrameGraphRuntimeStatistics&);

static constexpr FrameGraphRuntimeStatisticsMutation s_FrameGraphRuntimeStatisticsCountMutations[] = {
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.packetCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.mergedTaskCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.resourceVersionCount = 0u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.directResourceUseCount = statistics.compile.resourceUseCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.expandedResourceSetMemberUseCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.payloadObjectCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.repeatedOwnershipTransferSignatureCount =
            statistics.compile.logicalOwnershipTransferSignatureCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.concurrentSharingCouldAvoidTransferCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.concurrentSharingAdviceResourceCount = statistics.compile.resourceCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferInternalCount =
            statistics.compile.logicalOwnershipTransferCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.compile.logicalOwnershipTransferExternalImportCount =
            statistics.compile.logicalOwnershipTransferCount
            - statistics.compile.logicalOwnershipTransferInternalCount
            + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.compile.logicalOwnershipTransferExternalExportCount;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.packetCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.taskCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.commandListCount = statistics.recording.packetCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.workerRoutedPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.recording.parallelPacketCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedPacketCount = statistics.compile.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedPacketCount = statistics.compile.packetCount
            - statistics.submission.acceptedPacketCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedTaskCount = statistics.compile.taskCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedTaskCount = statistics.compile.taskCount
            - statistics.submission.acceptedTaskCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedTaskCount = statistics.submission.acceptedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedTaskCount = statistics.submission.rejectedPacketCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedPacketCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.rejectedSubmissionCount = statistics.submission.rejectedPacketCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.acceptedFrontierSubmissionCount = statistics.submission.nativeSubmissionCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.recoverySubmissionCount = statistics.submission.acceptedFrontierSubmissionCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeSubmissionCount = statistics.recording.packetCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeCommandListCount = statistics.submission.nativeSubmissionCount - 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.nativeCommandListCount = statistics.recording.commandListCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.sameQueueWaitElisionCount = statistics.submission.plannedWaitTokenCount + 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.mergedTimelineWaitCount = statistics.submission.plannedWaitTokenCount
            - statistics.submission.sameQueueWaitElisionCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = statistics.submission.plannedWaitTokenCount
            - statistics.submission.sameQueueWaitElisionCount
            - statistics.submission.mergedTimelineWaitCount + 1u
        ;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission.inheritedTimelineWaitElisionCount = Limit<u64>::s_Max;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        statistics.submission = {};
        statistics.submission.plannedWaitTokenCount = 1u;
        statistics.submission.inheritedTimelineWaitElisionCount = 1u;
    },
    [](Telemetry::FrameGraphRuntimeStatistics& statistics)noexcept{
        ++statistics.submission.timelineWaitCount;
    },
};


TEST(Telemetry, FrameGraphRuntimeStatisticsPayloadRejectsMalformedRecords){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    BuildTestRuntimeFrameGraph(testArena.arena, nodes, edges);

    Telemetry::TelemetryBytes payload(testArena.arena);
    const usize runtimeStatisticsOffset = sizeof(Telemetry::EncodedFrameGraphPayloadHeader)
        + sizeof(Telemetry::EncodedFrameGraphNode) * nodes.size()
        + sizeof(Telemetry::EncodedFrameGraphEdge) * edges.size()
        + sizeof(Telemetry::EncodedFrameGraphQueueAssignment) * s_ExpectedDualCount
        + sizeof(Telemetry::EncodedFrameGraphCompiledTask) * s_ExpectedDualCount
    ;
    Telemetry::EncodedFrameGraphRuntimeStatistics first;
    Telemetry::EncodedFrameGraphRuntimeStatistics second;
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
    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});

    ASSERT_TRUE(loadRuntimeStatistics());
    second.nodeIndex = first.nodeIndex;
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = s_ExpectedDualCount;
    second.nodeIndex = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    NWB_MEMCPY(
        payload.data() + runtimeStatisticsOffset + sizeof(first),
        payload.size() - runtimeStatisticsOffset - sizeof(first),
        &second,
        sizeof(second)
    );
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.nodeIndex = 3u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.reserved = 1u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.graphGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.planGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recordingAttemptGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.deviceGeneration = 0u;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.compile.declarationSeconds = -1.0;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.recording.recordingSeconds = Limit<f64>::s_Infinity;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    first.submission.submissionSeconds = Limit<f64>::s_QuietNaN;
    NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    for(usize mutationIndex = 0u; mutationIndex < LengthOf(s_FrameGraphRuntimeStatisticsCountMutations); ++mutationIndex){
        SCOPED_TRACE(mutationIndex);
        ASSERT_TRUE(loadRuntimeStatistics());
        Telemetry::FrameGraphRuntimeStatistics malformed = MakeFrameGraphRuntimeStatistics();
        s_FrameGraphRuntimeStatisticsCountMutations[mutationIndex](malformed);
        first = EncodeTestFrameGraphRuntimeStatistics(malformed, first.nodeIndex, first.reserved);
        NWB_MEMCPY(payload.data() + runtimeStatisticsOffset, payload.size() - runtimeStatisticsOffset, &first, sizeof(first));
        EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));
    }

    ASSERT_TRUE(loadRuntimeStatistics());
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), payload.data(), sizeof(header));
    header.runtimeStatisticsCount = 4u;
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size())));

    ASSERT_TRUE(loadRuntimeStatistics());
    EXPECT_FALSE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size() - 1u)));
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

