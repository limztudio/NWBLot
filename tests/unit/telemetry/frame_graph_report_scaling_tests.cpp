// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_report_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;


[[nodiscard]] Telemetry::FrameGraphRuntimeStatistics OwnerStatistics(const u32 packetCount){
    Telemetry::FrameGraphRuntimeStatistics statistics;
    statistics.graphGeneration = 71u;
    statistics.planGeneration = 72u;
    statistics.recordingAttemptGeneration = 73u;
    statistics.deviceGeneration = 17u;
    statistics.compile.taskCount = packetCount;
    statistics.compile.packetCount = packetCount;
    statistics.recording.packetCount = packetCount;
    statistics.recording.taskCount = packetCount;
    statistics.recording.commandListCount = packetCount;
    statistics.submission.acceptedPacketCount = packetCount;
    statistics.submission.acceptedTaskCount = packetCount;
    statistics.submission.nativeSubmissionCount = packetCount;
    statistics.submission.nativeCommandListCount = packetCount;
    statistics.present = true;
    return statistics;
}

struct ReportFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;

    explicit ReportFixture(Telemetry::TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
        , queues(arena)
        , packets(arena)
    {}

    void addUnmeasuredNode(){
        nodes.push_back(Telemetry::FrameGraphNodeDesc{
            .name = Name("tests/telemetry/report/task"),
            .label = "Unmeasured task",
            .kind = Telemetry::FrameGraphNodeKind::Pass,
            .flags = 0u,
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = {},
        });
    }

    void addOwner(const u32 packetCount, const u16 firstQueueIndex, const bool includeQueues = true){
        const u32 ownerIndex = static_cast<u32>(nodes.size());
        const Telemetry::FrameGraphRuntimeStatistics owner = OwnerStatistics(packetCount);
        nodes.push_back(Telemetry::FrameGraphNodeDesc{
            .name = Name("tests/telemetry/report/owner"),
            .label = "Measured owner",
            .kind = Telemetry::FrameGraphNodeKind::Pass,
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = owner,
        });
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
                .ownerNodeIndex = ownerIndex,
                .packetIndex = packetIndex,
                .queue = { .index = static_cast<u16>(firstQueueIndex + packetIndex % 2u), .deviceGeneration = 17u },
                .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
            });
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize CountText(const AStringView text, const AStringView needle){
    usize count = 0u;
    usize offset = 0u;
    while((offset = text.find(needle, offset)) != AStringView::npos){
        ++count;
        offset += needle.size();
    }
    return count;
}

[[nodiscard]] AStringView FindLine(const AStringView text, const AStringView prefix){
    const usize begin = text.find(prefix);
    if(begin == AStringView::npos)
        return {};
    const usize end = text.find('\n', begin);
    return text.substr(begin, end == AStringView::npos ? text.size() - begin : end - begin);
}

void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

void BenchmarkReport(const u32 packetCount, const u32 ownerCount, const u32 taskCount, const usize iterations = 3u){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.nodes.reserve(ownerCount + taskCount);
    fixture.queues.reserve(ownerCount * 2u);
    fixture.packets.reserve(packetCount);
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        fixture.addOwner(packetCount / ownerCount, 1u);
        for(u32 taskIndex = 0u; taskIndex < taskCount / ownerCount; ++taskIndex)
            fixture.addUnmeasuredNode();
    }
    Telemetry::Recorder recorder(testArena.arena);
    recorder.setCaptureOptions(Telemetry::CaptureOptions::All());
    ASSERT_TRUE(Telemetry::RecordFrameGraph(
        recorder, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, 19u
    ));
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    usize accepted = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report))
            ++accepted;
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    ASSERT_EQ(accepted, iterations);
    ASSERT_EQ(report.summary.parseFailureCount, 0u);
    ASSERT_EQ(report.summary.frameGraphFrameCount, 1u);
    ASSERT_EQ(report.summary.frameGraphNodeCount, ownerCount + taskCount);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView dot(report.graph.data(), report.graph.size());
    EXPECT_EQ(CountText(json, "\"packet\": {\"index\":"), packetCount);
    EXPECT_EQ(CountText(json, "\"runtimeStatistics\": null"), taskCount);
    EXPECT_EQ(CountText(dot, "runtime_packet_submission_count="), ownerCount);
    RecordUnsignedProperty(MakeNotNull("report_build_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("report_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("report_node_count"), fixture.nodes.size());
    RecordUnsignedProperty(MakeNotNull("report_owner_count"), ownerCount);
    RecordUnsignedProperty(MakeNotNull("report_packet_count"), packetCount);
    RecordUnsignedProperty(MakeNotNull("report_physical_queue_count"), fixture.queues.size());
    RecordUnsignedProperty(MakeNotNull("report_heap_allocations"), after.allocationCount - before.allocationCount);
    RecordUnsignedProperty(MakeNotNull("report_json_bytes"), report.json.size());
    RecordUnsignedProperty(MakeNotNull("report_dot_bytes"), report.graph.size());
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
    ASSERT_TRUE(Telemetry::RecordFrameGraph(
        recorder, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, 19u
    ));
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    ASSERT_EQ(report.summary.parseFailureCount, 0u);
    ASSERT_EQ(report.summary.frameGraphNodeCount, 7u);
    const AStringView json(report.json.data(), report.json.size());
    const AStringView first = FindLine(json, "{\"index\": 1, \"identity\":");
    const AStringView empty = FindLine(json, "{\"index\": 3, \"identity\":");
    const AStringView last = FindLine(json, "{\"index\": 5, \"identity\":");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(empty.empty());
    ASSERT_FALSE(last.empty());
    EXPECT_EQ(CountText(first, "\"packet\": {\"index\":"), 3u);
    EXPECT_TRUE(ContainsText(first, "\"physicalQueues\": [{"));
    EXPECT_TRUE(ContainsText(first, "\"queue\": {\"index\": 1,"));
    EXPECT_TRUE(ContainsText(first, "\"queue\": {\"index\": 2,"));
    EXPECT_FALSE(ContainsText(first, "\"queue\": {\"index\": 7,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 0,"), first.find("\"packet\": {\"index\": 1,"));
    EXPECT_LT(first.find("\"packet\": {\"index\": 1,"), first.find("\"packet\": {\"index\": 2,"));
    EXPECT_TRUE(ContainsText(empty, "\"physicalQueues\": null, \"packetSubmissions\": []"));
    EXPECT_EQ(CountText(last, "\"packet\": {\"index\":"), 2u);
    EXPECT_TRUE(ContainsText(last, "\"physicalQueues\": null"));
    EXPECT_TRUE(ContainsText(last, "\"queue\": {\"index\": 7,"));
    EXPECT_TRUE(ContainsText(last, "\"queue\": {\"index\": 8,"));
    EXPECT_FALSE(ContainsText(last, "\"queue\": {\"index\": 1,"));
    EXPECT_EQ(CountText(json, "\"runtimeStatistics\": null"), 4u);

    const AStringView dot(report.graph.data(), report.graph.size());
    const AStringView firstDot = FindLine(dot, "  n1 [");
    const AStringView emptyDot = FindLine(dot, "  n3 [");
    const AStringView lastDot = FindLine(dot, "  n5 [");
    EXPECT_TRUE(ContainsText(firstDot, "runtime_physical_queue_count=2"));
    EXPECT_TRUE(ContainsText(firstDot, "runtime_packet_submission_count=3"));
    EXPECT_TRUE(ContainsText(emptyDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(ContainsText(emptyDot, "runtime_packet_submission_count=0"));
    EXPECT_TRUE(ContainsText(lastDot, "runtime_physical_queue_count=\"unknown\""));
    EXPECT_TRUE(ContainsText(lastDot, "runtime_packet_submission_count=2"));
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
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 918u, first.nodes, first.edges, first.queues, first.packets, 19u));
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 919u, second.nodes, second.edges, second.queues, second.packets, 20u));
    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    ASSERT_EQ(report.summary.frameGraphFrameCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), "\"packet\": {\"index\":"), 5u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_physical_queue_count=2"), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_packet_submission_count=3"), 1u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_packet_submission_count=2"), 1u);

    recorder.clear();
    ASSERT_TRUE(Telemetry::RecordFrameGraph(recorder, 920u, second.nodes, second.edges, second.queues, second.packets, 21u));
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    ASSERT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), "\"packet\": {\"index\":"), 2u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_packet_submission_count=2"), 1u);
    EXPECT_FALSE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "runtime_packet_submission_count=3"));
}

TEST(FrameGraphReport, RejectsMalformedOwnerTablesBeforeReportingAndRetainsTheNextValidCapture){
    TestArena testArena;
    ReportFixture fixture(testArena.arena);
    fixture.addUnmeasuredNode();
    fixture.addOwner(3u, 1u);
    Telemetry::TelemetryBytes original(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, original
    ));
    Telemetry::EncodedFrameGraphPayloadHeaderV8 header;
    NWB_MEMCPY(&header, sizeof(header), original.data(), sizeof(header));
    ASSERT_EQ(header.queueAssignmentCount, 0u);
    ASSERT_EQ(header.compiledTaskCount, 0u);
    ASSERT_EQ(header.edgeCount, 0u);
    ASSERT_EQ(header.runtimeStatisticsCount, 1u);
    ASSERT_EQ(header.physicalQueueRuntimeStatisticsCount, 2u);
    ASSERT_EQ(header.packetSubmissionStatisticsCount, 3u);
    const usize queueOffset = sizeof(header) + header.nodeCount * sizeof(Telemetry::EncodedFrameGraphNode)
        + sizeof(Telemetry::EncodedFrameGraphRuntimeStatisticsV8)
    ;
    const usize packetOffset = queueOffset + 2u * sizeof(Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6);
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
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 queue;
    Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 nextQueue;
    NWB_MEMCPY(&queue, sizeof(queue), original.data() + queueOffset, sizeof(queue));
    NWB_MEMCPY(&nextQueue, sizeof(nextQueue), original.data() + queueOffset + sizeof(queue), sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset, malformed.size() - queueOffset, &nextQueue, sizeof(nextQueue));
    NWB_MEMCPY(malformed.data() + queueOffset + sizeof(queue), sizeof(queue), &queue, sizeof(queue));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, malformed.data(), malformed.size(), 3u));
    ASSERT_TRUE(recorder.recordBinary(Telemetry::EventKind::FrameGraphFrame, 918u, original.data(), original.size(), 4u));

    Log::TelemetryReport report(testArena.arena);
    ASSERT_TRUE(Log::BuildTelemetryReport(testArena.arena, recorder.view(), report));
    EXPECT_EQ(report.summary.eventCount, 4u);
    EXPECT_EQ(report.summary.parseFailureCount, 3u);
    EXPECT_EQ(report.summary.frameGraphFrameCount, 1u);
    EXPECT_EQ(report.summary.frameGraphNodeCount, 2u);
    EXPECT_EQ(CountText(AStringView(report.json.data(), report.json.size()), "\"packet\": {\"index\":"), 3u);
    EXPECT_EQ(CountText(AStringView(report.graph.data(), report.graph.size()), "runtime_packet_submission_count=3"), 1u);
    EXPECT_TRUE(ContainsText(AStringView(report.graph.data(), report.graph.size()), "Frame 918 stream 4"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opt in with --gtest_also_run_disabled_tests and the FrameGraphReportBenchmark.* filter.
TEST(FrameGraphReportBenchmark, DISABLED_SingleOwnerSinglePacket){
    BenchmarkReport(1u, 1u, 1u, 128u);
}

TEST(FrameGraphReportBenchmark, DISABLED_SingleOwner1024PacketsAndTasks){
    BenchmarkReport(1024u, 1u, 1024u);
}

TEST(FrameGraphReportBenchmark, DISABLED_SingleOwner4096PacketsAndTasks){
    BenchmarkReport(4096u, 1u, 4096u);
}

TEST(FrameGraphReportBenchmark, DISABLED_SixtyFourOwners4096PacketsAndTasks){
    BenchmarkReport(4096u, 64u, 4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

