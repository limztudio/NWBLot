// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_test_helpers.h"

#include <core/alloc/scratch.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_report_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ReportArenaName = "tests/telemetry/report_work";

static constexpr AStringView s_PacketIndex = "\"packet\": {\"index\":";
static constexpr AStringView s_RuntimePacketSubmissionCount3 = "runtime_packet_submission_count=3";
static constexpr AStringView s_RuntimePacketSubmissionCount2 = "runtime_packet_submission_count=2";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


struct ReportFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;


public:
    explicit ReportFixture(Telemetry::TelemetryArena& arena)noexcept
        : nodes(arena)
        , edges(arena)
        , queues(arena)
        , packets(arena)
    {}


public:
    void addUnmeasuredNode(){
        nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name("tests/telemetry/report/task"), .label = "Unmeasured task", .kind = Telemetry::FrameGraphNodeKind::Pass, .flags = 0u, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = {}, });
    }

    void addOwner(const u32 packetCount, const u16 firstQueueIndex, const b8 includeQueues = true){
        const u32 ownerIndex = static_cast<u32>(nodes.size());
        const Telemetry::FrameGraphRuntimeStatistics owner = OwnerStatistics(packetCount);
        nodes.push_back(Telemetry::FrameGraphNodeDesc{ .name = Name("tests/telemetry/report/owner"), .label = "Measured owner", .kind = Telemetry::FrameGraphNodeKind::Pass, .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = owner, });
        if(includeQueues){
            const u32 queueCount = packetCount > 1u ? 2u : packetCount;
            for(u32 queueOffset = 0u; queueOffset < queueCount; ++queueOffset){
                const u32 acceptedCount = packetCount / 2u + (queueOffset == 0u ? packetCount % 2u : 0u);
                Telemetry::FrameGraphPhysicalQueueRuntimeStatistics statistics;
                statistics.graphGeneration = owner.graphGeneration;
                statistics.planGeneration = owner.planGeneration;
                statistics.recordingAttemptGeneration = owner.recordingAttemptGeneration;
                statistics.deviceGeneration = owner.deviceGeneration;
                statistics.queue = { .index = static_cast<u16>(firstQueueIndex + queueOffset), .deviceGeneration = 17u };
                statistics.queueClass = Telemetry::FrameGraphQueueClass::Graphics;
                statistics.compile.taskCount = acceptedCount;
                statistics.compile.packetCount = acceptedCount;
                statistics.recording.packetCount = acceptedCount;
                statistics.recording.taskCount = acceptedCount;
                statistics.recording.commandListCount = acceptedCount;
                statistics.submission.acceptedPacketCount = acceptedCount;
                statistics.submission.acceptedTaskCount = acceptedCount;
                statistics.submission.nativeSubmissionCount = acceptedCount;
                statistics.submission.nativeCommandListCount = acceptedCount;
                queues.push_back({ .ownerNodeIndex = ownerIndex, .statistics = statistics });
            }
        }
        // Deliberately unsorted producer input exercises the codec's canonical owner and packet ordering.
        for(u32 remaining = packetCount; remaining != 0u; --remaining){
            const u32 packetIndex = remaining - 1u;
            packets.push_back(Telemetry::FrameGraphPacketSubmissionStatisticsRecord{
                .packetGeneration = owner.planGeneration,
                .taskCount = 1u,
                .commandListCount = 1u,
                .plannedWaitTokenCount = 0u,
                .sameQueueWaitElisionCount = 0u,
                .timelineWaitCount = 0u,
                .mergedTimelineWaitCount = 0u,
                .inheritedTimelineWaitElisionCount = 0u,
                .submissionSeconds = 0.0,
                .queue = { .index = static_cast<u16>(firstQueueIndex + packetIndex % 2u), .deviceGeneration = 17u },
                .ownerNodeIndex = ownerIndex,
                .packetIndex = packetIndex,
                .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
                .joinsAcceptedQueueFrontier = false,
                .recoverySubmission = false,
            });
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::CountText;

[[nodiscard]] AStringView FindLine(const AStringView text, const AStringView prefix)noexcept{
    const usize begin = text.find(prefix);
    if(begin == AStringView::npos)
        return {};
    const usize end = text.find('\n', begin);
    return text.substr(begin, end == AStringView::npos ? text.size() - begin : end - begin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameGraphReport, GroupsSparseOwnersWithoutLeakingStatisticsIntoAdjacentNodes){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.addUnmeasuredNode();
    fixture.addOwner(3u, 1u);
    fixture.addUnmeasuredNode();
    fixture.addOwner(0u, 5u);
    fixture.addUnmeasuredNode();
    fixture.addOwner(2u, 7u, false);
    fixture.addUnmeasuredNode();
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, 19u));
    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult2 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult2);
    report = Move(*reportResult2);
    ASSERT_EQ(report.summary.parseFailureCount, 0u);
    ASSERT_EQ(report.summary.frameGraphNodeCount, 7u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView first = FindLine(json, "{\"index\": 1, \"identity\":");
    const AStringView empty = FindLine(json, "{\"index\": 3, \"identity\":");
    const AStringView last = FindLine(json, "{\"index\": 5, \"identity\":");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(empty.empty());
    ASSERT_FALSE(last.empty());
    EXPECT_EQ(CountText(first, s_PacketIndex), 3u);
    EXPECT_TRUE(Contains(first, "\"physicalQueues\": [{"));
    EXPECT_TRUE(Contains(first, "\"queue\": {\"index\": 1,"));
    EXPECT_TRUE(Contains(first, "\"queue\": {\"index\": 2,"));
    EXPECT_FALSE(Contains(first, "\"queue\": {\"index\": 7,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 0,"), first.find("\"packet\": {\"index\": 1,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 1,"), first.find("\"packet\": {\"index\": 2,"));
    EXPECT_TRUE(Contains(empty, "\"physicalQueues\": null, \"packetSubmissions\": []"));
    EXPECT_EQ(CountText(last, s_PacketIndex), 2u);
    EXPECT_TRUE(Contains(last, "\"physicalQueues\": null"));
    EXPECT_TRUE(Contains(last, "\"queue\": {\"index\": 7,"));
    EXPECT_TRUE(Contains(last, "\"queue\": {\"index\": 8,"));
    EXPECT_FALSE(Contains(last, "\"queue\": {\"index\": 1,"));
    EXPECT_EQ(CountText(json, "\"runtimeStatistics\": null"), 4u);

    const AStringView dot(report.graph.data(), report.graph.size());
    const AStringView firstDot = FindLine(dot, "  n1 [");
    const AStringView emptyDot = FindLine(dot, "  n3 [");
    const AStringView lastDot = FindLine(dot, "  n5 [");
    EXPECT_TRUE(Contains(firstDot, "runtime_physical_queue_count=2"));
    EXPECT_TRUE(Contains(firstDot, s_RuntimePacketSubmissionCount3));
    EXPECT_TRUE(Contains(emptyDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(Contains(emptyDot, "runtime_packet_submission_count=0"));
    EXPECT_TRUE(Contains(lastDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(Contains(lastDot, s_RuntimePacketSubmissionCount2));
    EXPECT_EQ(CountText(dot, "runtime_packet_submission_count="), 3u);
}

TEST(FrameGraphReport, RestartsOwnerRangesForEveryCaptureAndReportRebuild){
    TestArena testArena;
    ReportFixture first(testArena.arena);
    first.addOwner(3u, 1u);
    first.addUnmeasuredNode();
    ReportFixture second(testArena.arena);
    second.addUnmeasuredNode();
    second.addOwner(2u, 7u);
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 918u, first.nodes, first.edges, first.queues, first.packets, 19u));
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 919u, second.nodes, second.edges, second.queues, second.packets, 20u));
    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult3 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult3);
    report = Move(*reportResult3);
    ASSERT_EQ(report.summary.frameGraphFrameCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 5u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_physical_queue_count=2"), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3), 1u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount2), 1u);

    recorder.clear();
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, testArena.scratchArena, 920u, second.nodes, second.edges, second.queues, second.packets, 21u));
    auto reportResult4 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult4);
    report = Move(*reportResult4);
    ASSERT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount2), 1u);
    EXPECT_FALSE(Contains(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3));
}

TEST(FrameGraphReport, RejectsMalformedOwnerTablesBeforeReportingAndRetainsTheNextValidCapture){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.addUnmeasuredNode();
    fixture.addOwner(3u, 1u);
    Telemetry::TelemetryBytes original(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, original));
    Telemetry::EncodedFrameGraphPayloadHeader header;
    NWB_MEMCPY(&header, sizeof(header), original.data(), sizeof(header));
    ASSERT_EQ(header.queueAssignmentCount, 0u);
    ASSERT_EQ(header.compiledTaskCount, 0u);
    ASSERT_EQ(header.edgeCount, 0u);
    ASSERT_EQ(header.runtimeStatisticsCount, 1u);
    ASSERT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);
    ASSERT_EQ(header.packetSubmissionStatisticsCount, 3u);
    const usize queueOffset = sizeof(header) + header.nodeCount * sizeof(Telemetry::EncodedFrameGraphNode) + sizeof(Telemetry::EncodedFrameGraphRuntimeStatistics);
    const usize packetOffset = queueOffset + 2u * sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics);
    ASSERT_LE(packetOffset + 3u * sizeof(Telemetry::EncodedFrameGraphPacketSubmissionStatistics), original.size());
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());

    Telemetry::TelemetryBytes malformed(original);
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics packet;
    NWB_MEMCPY(&packet, sizeof(packet), malformed.data() + packetOffset, sizeof(packet));
    packet.ownerNodeIndex = header.nodeCount;
    NWB_MEMCPY(malformed.data() + packetOffset, malformed.size() - packetOffset, &packet, sizeof(packet));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 1u));

    malformed = original;
    Telemetry::EncodedFrameGraphPacketSubmissionStatistics nextPacket;
    NWB_MEMCPY(&packet, sizeof(packet), original.data() + packetOffset, sizeof(packet));
    NWB_MEMCPY(&nextPacket, sizeof(nextPacket), original.data() + packetOffset + sizeof(packet), sizeof(nextPacket));
    NWB_MEMCPY(malformed.data() + packetOffset, malformed.size() - packetOffset, &nextPacket, sizeof(nextPacket));
    NWB_MEMCPY(malformed.data() + packetOffset + sizeof(packet), sizeof(packet), &packet, sizeof(packet));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 2u));

    malformed = original;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics queue;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics nextQueue;
    NWB_MEMCPY(&queue, sizeof(queue), original.data() + queueOffset, sizeof(queue));
    NWB_MEMCPY(&nextQueue, sizeof(nextQueue), original.data() + queueOffset + sizeof(queue), sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset, malformed.size() - queueOffset, &nextQueue, sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset + sizeof(queue), sizeof(queue), &queue, sizeof(queue));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 3u));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, original.data(), original.size(), 4u));

    Log::TelemetryReport report(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena{ Name(s_ReportArenaName) };
    auto reportResult5 = Log::BuildTelemetryReport(recorder.view(), testArena.arena, scratchArena);
    ASSERT_TRUE(reportResult5);
    report = Move(*reportResult5);
    EXPECT_EQ(report.summary.eventCount, 4u);
    EXPECT_EQ(report.summary.parseFailureCount, 3u);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), s_PacketIndex), 3u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), s_RuntimePacketSubmissionCount3), 1u);
    EXPECT_TRUE(Contains(AStringView(report.graph.data(), report.graph.size()), "Frame 918 stream 4"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

