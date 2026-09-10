// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <core/telemetry/frame_graph_contributor.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_statistics_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char buffer[32u] = {};
    const AStringView formatted = FormatDecimal(value, buffer);
    buffer[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), buffer);
}

struct StatisticsFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPendingNameEdges pendingEdges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;

    explicit StatisticsFixture(Telemetry::TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
        , pendingEdges(arena)
        , queues(arena)
        , packets(arena)
    {}
};

[[nodiscard]] Telemetry::FrameGraphRuntimeStatistics OwnerStatistics(const u32 packetCount){
    Telemetry::FrameGraphRuntimeStatistics stats;
    stats.graphGeneration = 71u;
    stats.planGeneration = 72u;
    stats.recordingAttemptGeneration = 73u;
    stats.deviceGeneration = 17u;
    stats.compile.taskCount = packetCount;
    stats.compile.packetCount = packetCount;
    stats.recording.packetCount = packetCount;
    stats.recording.taskCount = packetCount;
    stats.recording.commandListCount = packetCount;
    stats.submission.acceptedPacketCount = packetCount;
    stats.submission.acceptedTaskCount = packetCount;
    stats.submission.nativeSubmissionCount = packetCount;
    stats.submission.nativeCommandListCount = packetCount;
    stats.present = true;
    return stats;
}

[[nodiscard]] Telemetry::FrameGraphPhysicalQueueRuntimeStatistics QueueStatistics(
    const u16 queueIndex,
    const u32 packetCount
){
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics stats;
    stats.graphGeneration = 71u;
    stats.planGeneration = 72u;
    stats.recordingAttemptGeneration = 73u;
    stats.deviceGeneration = 17u;
    stats.queue = { .index = queueIndex, .deviceGeneration = 17u };
    stats.queueClass = queueIndex == 1u ? Telemetry::FrameGraphQueueClass::Graphics : Telemetry::FrameGraphQueueClass::Compute;
    stats.compile.taskCount = packetCount;
    stats.compile.packetCount = packetCount;
    stats.recording.packetCount = packetCount;
    stats.recording.taskCount = packetCount;
    stats.recording.commandListCount = packetCount;
    stats.submission.acceptedPacketCount = packetCount;
    stats.submission.acceptedTaskCount = packetCount;
    stats.submission.nativeSubmissionCount = packetCount;
    stats.submission.nativeCommandListCount = packetCount;
    return stats;
}

void PrepareFixture(StatisticsFixture& fixture, const u32 packetCount, const u32 ownerCount){
    const u32 packetsPerOwner = packetCount / ownerCount;
    fixture.nodes.reserve(ownerCount);
    fixture.queues.reserve(ownerCount * 2u);
    fixture.packets.reserve(packetCount);
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        fixture.nodes.push_back(Telemetry::FrameGraphNodeDesc{
            .name = Name("tests/telemetry/scaling_owner"),
            .label = "Scaling owner",
            .kind = Telemetry::FrameGraphNodeKind::Pass,
            .queueAssignment = {},
            .compiledTask = {},
            .runtimeStatistics = OwnerStatistics(packetsPerOwner),
        });
    }
}

[[nodiscard]] bool AppendFixtureStatistics(
    StatisticsFixture& fixture,
    const u32 packetCount,
    const u32 ownerCount
){
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes,
        fixture.edges,
        fixture.pendingEdges,
        fixture.queues,
        fixture.packets
    );
    const u32 packetsPerOwner = packetCount / ownerCount;
    for(u32 ownerIndex = 0u; ownerIndex < ownerCount; ++ownerIndex){
        const Telemetry::FrameGraphNodeHandle owner{ ownerIndex };
        if(!builder.addPhysicalQueueRuntimeStatistics(owner, QueueStatistics(3u, packetsPerOwner / 2u)))
            return false;
        if(!builder.addPhysicalQueueRuntimeStatistics(owner, QueueStatistics(1u, packetsPerOwner / 2u)))
            return false;
    }
    // Odd multiplication permutes the power-of-two workload, interleaving both owners and physical queues.
    for(u32 index = 0u; index < packetCount; ++index){
        const u32 shuffled = (index * 2053u + 7u) % packetCount;
        const u32 ownerIndex = shuffled / packetsPerOwner;
        const u32 packetIndex = shuffled % packetsPerOwner;
        const u16 queueIndex = (packetIndex & 1u) == 0u ? 1u : 3u;
        const Telemetry::FrameGraphPacketSubmissionStatisticsRecord packet{
            .packetGeneration = 72u,
            .taskCount = 1u,
            .commandListCount = 1u,
            .ownerNodeIndex = ownerIndex,
            .packetIndex = packetIndex,
            .queue = { .index = queueIndex, .deviceGeneration = 17u },
            .queueClass = queueIndex == 1u ? Telemetry::FrameGraphQueueClass::Graphics : Telemetry::FrameGraphQueueClass::Compute,
        };
        if(!builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ ownerIndex }, packet))
            return false;
    }
    return true;
}

void RunScalingScenario(const u32 packetCount, const u32 ownerCount){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, packetCount, ownerCount);
    const Timer appendBegin = TimerNow();
    const bool appended = AppendFixtureStatistics(fixture, packetCount, ownerCount);
    const u64 appendNanoseconds = DurationInNS<u64>(TimerNow(), appendBegin);
    ASSERT_TRUE(appended);
    ASSERT_EQ(fixture.packets.size(), packetCount);
    ASSERT_EQ(fixture.queues.size(), ownerCount * 2u);

    Telemetry::TelemetryBytes payload(testArena.arena);
    const Timer encodeBegin = TimerNow();
    const bool encoded = Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, payload
    );
    const u64 encodeNanoseconds = DurationInNS<u64>(TimerNow(), encodeBegin);
    ASSERT_TRUE(encoded);
    Telemetry::FrameGraphPayload decoded(testArena.arena);
    const Timer decodeBegin = TimerNow();
    const bool parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), decoded);
    const u64 decodeNanoseconds = DurationInNS<u64>(TimerNow(), decodeBegin);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(decoded.nodes.size(), ownerCount);
    ASSERT_EQ(decoded.physicalQueueRuntimeStatistics.size(), ownerCount * 2u);
    ASSERT_EQ(decoded.packetSubmissionStatistics.size(), packetCount);
    const u32 packetsPerOwner = packetCount / ownerCount;
    for(u32 index = 0u; index < packetCount; ++index){
        const auto& packet = decoded.packetSubmissionStatistics[index];
        EXPECT_EQ(packet.ownerNodeIndex, index / packetsPerOwner);
        EXPECT_EQ(packet.packetIndex, index % packetsPerOwner);
        EXPECT_EQ(packet.packetGeneration, 72u);
        EXPECT_EQ(packet.queue.index, (packet.packetIndex & 1u) == 0u ? 1u : 3u);
        EXPECT_EQ(packet.queue.deviceGeneration, 17u);
        EXPECT_EQ(packet.taskCount, 1u);
        EXPECT_EQ(packet.commandListCount, 1u);
    }
    testing::Test::RecordProperty("packet_count", packetCount);
    testing::Test::RecordProperty("owner_count", ownerCount);
    RecordUnsignedProperty(MakeNotNull("builder_ns"), appendNanoseconds);
    RecordUnsignedProperty(MakeNotNull("encode_ns"), encodeNanoseconds);
    RecordUnsignedProperty(MakeNotNull("decode_ns"), decodeNanoseconds);
}

TEST(Telemetry, PacketStatisticsInterleavedOwnersRoundTrip){
    RunScalingScenario(256u, 4u);
}

TEST(Telemetry, PacketStatisticsDuplicateRejectionPreservesSeededTables){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, 64u, 2u);
    ASSERT_TRUE(AppendFixtureStatistics(fixture, 64u, 2u));
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets
    );
    const auto packet = fixture.packets[17u];
    const Telemetry::FrameGraphNodeHandle owner{ packet.ownerNodeIndex };
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, packet));
    auto changedDuplicate = packet;
    changedDuplicate.queue.index = packet.queue.index == 1u ? 3u : 1u;
    changedDuplicate.queueClass = packet.queueClass == Telemetry::FrameGraphQueueClass::Graphics
        ? Telemetry::FrameGraphQueueClass::Compute
        : Telemetry::FrameGraphQueueClass::Graphics
    ;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, changedDuplicate));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_EQ(fixture.packets.size(), 64u);
    EXPECT_EQ(fixture.queues.size(), 4u);
}

TEST(Telemetry, PacketStatisticsPreserveOptionalQueueCoverageAndRejectMismatchedTotals){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, 64u, 2u);
    ASSERT_TRUE(AppendFixtureStatistics(fixture, 64u, 2u));
    Telemetry::TelemetryBytes payload(testArena.arena);
    fixture.queues.erase(fixture.queues.begin());
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, payload
    ));
    Telemetry::FrameGraphPayload decoded(testArena.arena);
    ASSERT_TRUE(Telemetry::ParseFrameGraphPayload(testArena.arena, payload.data(), payload.size(), decoded));
    ASSERT_EQ(decoded.physicalQueueRuntimeStatistics.size(), 3u);
    const auto original = fixture.packets[0u];
    fixture.packets[0u].queue.index = original.queue.index == 1u ? 3u : 1u;
    fixture.packets[0u].queueClass = original.queueClass == Telemetry::FrameGraphQueueClass::Graphics
        ? Telemetry::FrameGraphQueueClass::Compute
        : Telemetry::FrameGraphQueueClass::Graphics
    ;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, payload
    ));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, PacketStatisticsSparsePacketIdentifiersRemainValid){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, 2u, 1u);
    ASSERT_TRUE(AppendFixtureStatistics(fixture, 2u, 1u));
    fixture.nodes[0u].runtimeStatistics.compile.packetCount = Limit<u32>::s_Max - 1u;
    fixture.nodes[0u].runtimeStatistics.compile.taskCount = Limit<u32>::s_Max - 1u;
    fixture.packets[0u].packetIndex = Limit<u32>::s_Max - 2u;
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets
    );
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, payload
    ));
}

TEST(Telemetry, PacketStatisticsInvalidInputDoesNotConsumeIdentity){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, 2u, 1u);
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets
    );
    auto queue = QueueStatistics(1u, 1u);
    queue.planGeneration = 74u;
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, queue));
    queue.planGeneration = 72u;
    EXPECT_TRUE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, queue));
    Telemetry::FrameGraphPacketSubmissionStatisticsRecord packet{
        .packetGeneration = 74u,
        .taskCount = 1u,
        .commandListCount = 1u,
        .ownerNodeIndex = 0u,
        .packetIndex = 0u,
        .queue = { .index = 1u, .deviceGeneration = 17u },
        .queueClass = Telemetry::FrameGraphQueueClass::Graphics,
    };
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    packet.packetGeneration = 72u;
    EXPECT_TRUE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, packet));
    EXPECT_EQ(fixture.queues.size(), 1u);
    EXPECT_EQ(fixture.packets.size(), 1u);
}

TEST(Telemetry, PacketStatisticsQueueSourceSurvivesDestinationGrowth){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets
    );
    const Telemetry::FrameGraphPassMetadata metadata{
        .queueAssignment = {},
        .compiledTask = {},
        .runtimeStatistics = OwnerStatistics(1u),
    };
    const auto firstOwner = builder.addPass(Name("queue_alias_owner"), "Queue alias owner", metadata);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(firstOwner, QueueStatistics(1u, 1u)));
    while(fixture.queues.size() < fixture.queues.capacity()){
        const auto owner = builder.addPass(Name("queue_alias_owner"), "Queue alias owner", metadata);
        ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, fixture.queues[0u].statistics));
    }
    const usize capacityBeforeGrowth = fixture.queues.capacity();
    const auto owner = builder.addPass(Name("queue_alias_owner"), "Queue alias owner", metadata);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, fixture.queues[0u].statistics));
    ASSERT_GT(fixture.queues.capacity(), capacityBeforeGrowth);
    EXPECT_EQ(fixture.queues.back().ownerNodeIndex, owner.index);
    EXPECT_EQ(fixture.queues.back().statistics.queue, fixture.queues[0u].statistics.queue);
    EXPECT_EQ(fixture.queues.back().statistics.planGeneration, 72u);
    EXPECT_EQ(fixture.queues.back().statistics.compile.packetCount, 1u);
    EXPECT_EQ(fixture.queues.back().statistics.submission.nativeSubmissionCount, 1u);
}

TEST(Telemetry, PacketStatisticsSeededDuplicatesRemainInvalidPayloads){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(fixture, 2u, 1u);
    ASSERT_TRUE(AppendFixtureStatistics(fixture, 2u, 1u));
    fixture.queues.push_back(fixture.queues[0u]);
    fixture.packets.push_back(fixture.packets[0u]);
    Telemetry::FrameGraphBuilder builder(
        fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets
    );
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    EXPECT_EQ(fixture.queues.size(), 3u);
    EXPECT_EQ(fixture.packets.size(), 3u);
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(
        testArena.arena, 918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, payload
    ));
    EXPECT_TRUE(payload.empty());
}

// Opt in with --gtest_also_run_disabled_tests and a PacketStatisticsBenchmark filter.
TEST(Telemetry, DISABLED_PacketStatisticsBenchmarkSingleOwner512){
    RunScalingScenario(512u, 1u);
}

TEST(Telemetry, DISABLED_PacketStatisticsBenchmarkSingleOwner1024){
    RunScalingScenario(1024u, 1u);
}

TEST(Telemetry, DISABLED_PacketStatisticsBenchmarkSingleOwner4096){
    RunScalingScenario(4096u, 1u);
}

TEST(Telemetry, DISABLED_PacketStatisticsBenchmarkManyOwners4096){
    RunScalingScenario(4096u, 64u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

