// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_test_helpers.h"

#include <core/telemetry/frame_graph_contributor.h>

#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_statistics_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_QueueAliasOwner = "queue_alias_owner";
static constexpr AStringView s_QueueAliasOwner2 = "Queue alias owner";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

struct StatisticsFixture{
    Telemetry::FrameGraphNodeDescs nodes;
    Telemetry::FrameGraphEdgeDescs edges;
    Telemetry::FrameGraphPendingNameEdges pendingEdges;
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queues;
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords packets;


public:
    explicit StatisticsFixture(Telemetry::TelemetryArena& arena)noexcept
        : nodes(arena)
        , edges(arena)
        , pendingEdges(arena)
        , queues(arena)
        , packets(arena)
    {}
};

[[nodiscard]] Telemetry::FrameGraphPhysicalQueueRuntimeStatistics QueueStatistics(const u16 queueIndex, const u32 packetCount)noexcept{
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

void PrepareFixture(const u32 packetCount, const u32 ownerCount, StatisticsFixture& fixture){
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

[[nodiscard]] b8 AppendFixtureStatistics(const u32 packetCount, const u32 ownerCount, NWB::Core::Alloc::ScratchArena& scratchArena, StatisticsFixture& fixture){
    Telemetry::FrameGraphBuilder builder(scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
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


TEST(Telemetry, PacketStatisticsDuplicateRejectionPreservesSeededTables){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(64u, 2u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(64u, 2u, testArena.scratchArena, fixture));
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    const auto packet = fixture.packets[17u];
    const Telemetry::FrameGraphNodeHandle owner{ packet.ownerNodeIndex };
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, packet));
    auto changedDuplicate = packet;
    changedDuplicate.queue.index = packet.queue.index == 1u ? 3u : 1u;
    changedDuplicate.queueClass = packet.queueClass == Telemetry::FrameGraphQueueClass::Graphics ? Telemetry::FrameGraphQueueClass::Compute : Telemetry::FrameGraphQueueClass::Graphics;
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(owner, changedDuplicate));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_EQ(fixture.packets.size(), 64u);
    EXPECT_EQ(fixture.queues.size(), 4u);
}

TEST(Telemetry, PacketStatisticsPreserveOptionalQueueCoverageAndRejectMismatchedTotals){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(64u, 2u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(64u, 2u, testArena.scratchArena, fixture));
    Telemetry::TelemetryBytes payload(testArena.arena);
    fixture.queues.erase(fixture.queues.begin());
    ASSERT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    Expected<Telemetry::FrameGraphPayload> decoded = MakeUnexpected(Failure{});
    ASSERT_TRUE((decoded = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, payload.data(), payload.size())));
    ASSERT_EQ(decoded->physicalQueueRuntimeStatistics.size(), 3u);
    const auto original = fixture.packets[0u];
    fixture.packets[0u].queue.index = original.queue.index == 1u ? 3u : 1u;
    fixture.packets[0u].queueClass = original.queueClass == Telemetry::FrameGraphQueueClass::Graphics ? Telemetry::FrameGraphQueueClass::Compute : Telemetry::FrameGraphQueueClass::Graphics;
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}

TEST(Telemetry, PacketStatisticsSparsePacketIdentifiersRemainValid){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(2u, 1u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(2u, 1u, testArena.scratchArena, fixture));
    fixture.nodes[0u].runtimeStatistics.compile.packetCount = Limit<u32>::s_Max - 1u;
    fixture.nodes[0u].runtimeStatistics.compile.taskCount = Limit<u32>::s_Max - 1u;
    fixture.packets[0u].packetIndex = Limit<u32>::s_Max - 2u;
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_TRUE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
}

TEST(Telemetry, PacketStatisticsInvalidInputDoesNotConsumeIdentity){
    TestArena testArena;
    StatisticsFixture fixture(testArena.arena);
    PrepareFixture(2u, 1u, fixture);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
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
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    const Telemetry::FrameGraphPassMetadata metadata{ .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = OwnerStatistics(1u), };
    const auto firstOwner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(firstOwner, QueueStatistics(1u, 1u)));
    while(fixture.queues.size() < fixture.queues.capacity()){
        const auto owner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
        ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, fixture.queues[0u].statistics));
    }
    const usize capacityBeforeGrowth = fixture.queues.capacity();
    const auto owner = builder.addPass(Name(s_QueueAliasOwner), s_QueueAliasOwner2, metadata);
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
    PrepareFixture(2u, 1u, fixture);
    ASSERT_TRUE(AppendFixtureStatistics(2u, 1u, testArena.scratchArena, fixture));
    fixture.queues.push_back(fixture.queues[0u]);
    fixture.packets.push_back(fixture.packets[0u]);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, fixture.nodes, fixture.edges, fixture.pendingEdges, fixture.queues, fixture.packets);
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.queues[0u].statistics));
    EXPECT_FALSE(builder.addPacketSubmissionStatistics(Telemetry::FrameGraphNodeHandle{ 0u }, fixture.packets[0u]));
    EXPECT_EQ(fixture.queues.size(), 3u);
    EXPECT_EQ(fixture.packets.size(), 3u);
    Telemetry::TelemetryBytes payload(testArena.arena);
    EXPECT_FALSE(Telemetry::BuildFrameGraphPayload(918u, fixture.nodes, fixture.edges, fixture.queues, fixture.packets, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

