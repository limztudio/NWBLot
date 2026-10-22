// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/refraction/statistics_readback.h>

#include <core/alloc/general.h>
#include <core/task/gpu/task_graph.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct StatisticsContext{
    Core::Alloc::GlobalArena arena{Name("tests/refraction/statistics")};
    RefractionStatisticsControlHandle control = CreateRefractionStatisticsControl(arena, 7u).value_or(RefractionStatisticsControlHandle{});
};

[[nodiscard]] RefractionStatistics Metadata(const u32 frameIndex = 9u)noexcept{
    RefractionStatistics value;
    value.frameIndex = frameIndex;
    value.graphicsFrameIndex = 100u + frameIndex;
    value.width = 320u;
    value.height = 180u;
    value.hardwareRequested = true;
    value.hardwareAvailable = true;
    value.csgHardwareEnabled = true;
    return value;
}

[[nodiscard]] Core::QueueSubmissionToken Token(const u64 value = 20u)noexcept{
    return Core::QueueSubmissionToken{ .value = value, .physicalQueueIndex = 0u, .deviceGeneration = 7u, .queue = Core::CommandQueue::Graphics, };
}

struct LeaseTask{
    struct Payload{
        RefractionStatisticsReservation reservation;
    };


public:
    static void Discarded(Payload& payload)noexcept{ payload.reservation.discard(); }
};

[[nodiscard]] Core::GpuTaskDesc TaskDesc()noexcept{
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name("tests.refraction.statistics.lease")).setMarkerLabel("Refraction statistics lease");
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RefractionStatistics, RingSaturationDoesNotRecycleOutstandingGpuWork){
    StatisticsContext context;
    ASSERT_TRUE(context.control);
    const auto first = context.control->reserve(Metadata());
    const auto second = context.control->reserve(Metadata());
    const auto third = context.control->reserve(Metadata());
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    EXPECT_NE(first.slot, second.slot);
    EXPECT_NE(first.slot, third.slot);
    EXPECT_NE(second.slot, third.slot);
    context.control->accept(first, Token(), true);
    context.control->discard(first);
    EXPECT_FALSE(context.control->reserve(Metadata()).valid());
    context.control->discard(second);
    const auto reused = context.control->reserve(Metadata(10u));
    EXPECT_EQ(reused.slot, second.slot);
    EXPECT_GT(reused.sequence, second.sequence);
    EXPECT_FALSE(context.control->reserve(Metadata()).valid());
}

TEST(RefractionStatistics, RejectedReservationCannotMutateItsReusedSlot){
    StatisticsContext context;
    const auto rejected = context.control->reserve(Metadata());
    context.control->discard(rejected);
    const auto current = context.control->reserve(Metadata(10u));
    ASSERT_EQ(rejected.slot, current.slot);
    context.control->accept(rejected, Token(), true);
    context.control->discard(rejected);
    context.control->accept(current, Token(21u), false);
    const auto pending = context.control->pending(current.slot);
    ASSERT_TRUE(pending);
    EXPECT_EQ(pending->key.sequence, current.sequence);
    EXPECT_EQ(pending->token.value, 21u);
}

TEST(RefractionStatistics, CompletionRequiresExactAcceptedPhysicalToken){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, Token(), true);
    const u32 counters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {};
    Core::QueueSubmissionToken wrong = Token();
    wrong.physicalQueueIndex = 1u;
    context.control->complete(key, wrong, counters);
    wrong = Token();
    wrong.deviceGeneration = 8u;
    context.control->complete(key, wrong, counters);
    wrong = Token();
    wrong.queue = Core::CommandQueue::Compute;
    context.control->complete(key, wrong, counters);
    context.control->complete(key, Token(19u), counters);
    context.control->complete(key, {}, counters);
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    EXPECT_TRUE(context.control->pending(key.slot));
    context.control->complete(key, Token(), counters);
    const auto statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->acceptedToken.value, Token().value);
    EXPECT_EQ(statistics->acceptedToken.physicalQueueIndex, 0u);
    EXPECT_FALSE(context.control->pending(key.slot));
}

TEST(RefractionStatistics, OlderCompletionsCannotReplaceFrozenNewerPublication){
    StatisticsContext context;
    RefractionStatistics metadata = Metadata(10u);
    const auto older = context.control->reserve(metadata);
    metadata.frameIndex = 11u;
    metadata.graphicsFrameIndex = 111u;
    const auto newer = context.control->reserve(metadata);
    metadata.frameIndex = 999u;
    metadata.width = 1u;
    context.control->accept(older, Token(20u), true);
    context.control->accept(newer, Token(21u), false);
    const u32 counters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {
        23u, 41u, 2u, 3u, 5u, 7u, 11u, 43u, 13u, 17u, 19u, 29u, 31u, 47u, 37u, 53u,
        59u, 61u, 67u, 71u, 73u, 79u, 83u, 89u, 97u, 101u, 103u, 107u, 1u,
    };
    context.control->complete(newer, Token(21u), counters);
    const u32 olderCounters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(older, Token(20u), olderCounters);
    const auto statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->sequence, newer.sequence);
    EXPECT_EQ(statistics->frameIndex, 11u);
    EXPECT_EQ(statistics->graphicsFrameIndex, 111u);
    EXPECT_EQ(statistics->width, 320u);
    EXPECT_EQ(statistics->height, 180u);
    EXPECT_FALSE(statistics->hardwareReady);
    EXPECT_FALSE(statistics->csgHardwareEnabled);
    EXPECT_EQ(statistics->hardwarePixels, 23u);
    EXPECT_EQ(statistics->entryQueries, 41u);
    EXPECT_EQ(statistics->exitQueries, 43u);
    EXPECT_EQ(statistics->backgroundQueries, 47u);
    EXPECT_EQ(statistics->physicalQueries, 103u);
    EXPECT_EQ(statistics->queryBudgetUnits, 107u);
    EXPECT_EQ(statistics->overflowFlags, 1u);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, older.slot);
}

TEST(RefractionStatistics, DeviceResetRejectsStaleAcceptanceAndCompletion){
    StatisticsContext context;
    const auto previous = context.control->reserve(Metadata());
    context.control->accept(previous, Token(), true);
    const u32 counters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(previous, Token(), counters);
    context.control->reset(0u);
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    EXPECT_FALSE(context.control->reserve(Metadata()).valid());
    context.control->reset(8u);
    const auto current = context.control->reserve(Metadata(12u));
    ASSERT_TRUE(current.valid());
    EXPECT_GT(current.generation, previous.generation);
    context.control->accept(previous, Token(), true);
    context.control->discard(previous);
    context.control->complete(previous, Token(), counters);
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    Core::QueueSubmissionToken next = Token(1u);
    next.deviceGeneration = 8u;
    context.control->accept(current, next, true);
    context.control->complete(current, next, counters);
    const auto statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->frameIndex, 12u);
    EXPECT_EQ(statistics->acceptedToken.deviceGeneration, 8u);
    EXPECT_TRUE(statistics->hardwareReady);
    EXPECT_TRUE(statistics->csgHardwareEnabled);
}

TEST(RefractionStatistics, FailedMappingRetiresSlotWithoutReplacingPublication){
    StatisticsContext context;
    const auto first = context.control->reserve(Metadata());
    context.control->accept(first, Token(), true);
    const u32 counters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(first, Token(), counters);
    const auto failed = context.control->reserve(Metadata(10u));
    context.control->accept(failed, Token(21u), true);
    context.control->complete(failed, Token(21u), nullptr);
    const auto statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->sequence, first.sequence);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, failed.slot);
}

TEST(RefractionStatistics, InvalidAcceptedTokenQuarantinesSlotUntilReset){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, {}, true);
    context.control->discard(key);
    EXPECT_FALSE(context.control->pending(key.slot));
    EXPECT_FALSE(context.control->pending(RefractionStatisticsState::s_SlotCount));
    EXPECT_FALSE(context.control->pending(Limit<u32>::s_Max));
    EXPECT_NE(context.control->reserve(Metadata()).slot, key.slot);
    context.control->reset(7u);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, key.slot);
}

TEST(RefractionStatistics, RejectedGraphRegistrationAndGraphResetReleaseUnacceptedLeases){
    StatisticsContext context;
    Core::GpuTaskGraph graph(context.arena);
    const auto rejected = graph.addTask<LeaseTask>(Core::GpuTaskDesc{}, LeaseTask::Payload{RefractionStatisticsReservation(context.control, Metadata())});
    EXPECT_FALSE(rejected);
    {
        const auto task = graph.addTask<LeaseTask>(TaskDesc(), LeaseTask::Payload{RefractionStatisticsReservation(context.control, Metadata())});
        ASSERT_TRUE(task);
        EXPECT_NE(context.control->reserve(Metadata()).slot, 0u);
    }
    graph.reset();
    EXPECT_EQ(context.control->reserve(Metadata()).slot, 0u);
}

TEST(RefractionStatistics, MovingLeaseKeepsAcceptedGpuWorkReservedAfterDestruction){
    StatisticsContext context;
    u32 acceptedSlot = 0u;
    {
        RefractionStatisticsReservation first(context.control, Metadata());
        acceptedSlot = first.slotIndex();
        RefractionStatisticsReservation second(context.control, Metadata());
        const u32 replacedSlot = second.slotIndex();
        second = Move(first);
        EXPECT_FALSE(first.valid());
        EXPECT_EQ(second.slotIndex(), acceptedSlot);
        EXPECT_EQ(context.control->reserve(Metadata()).slot, replacedSlot);
        second.accept(Token(), true);
    }
    EXPECT_NE(context.control->reserve(Metadata()).slot, acceptedSlot);
    const auto pending = context.control->pending(acceptedSlot);
    ASSERT_TRUE(pending);
    const u32 counters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(pending->key, pending->token, counters);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, acceptedSlot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

