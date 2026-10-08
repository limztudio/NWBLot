// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/reflection/statistics_readback.h>
#include <impl/assets/graphics/reflection/frame_constants.h>

#include <core/alloc/general.h>
#include <core/task/gpu/task_graph.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct StatisticsContext{
    Core::Alloc::GlobalArena arena{Name("tests/reflection/statistics")};
    ReflectionStatisticsControlHandle control = CreateReflectionStatisticsControl(arena, 7u);
};

[[nodiscard]] ReflectionStatistics Metadata(const u32 frameIndex = 9u){
    ReflectionStatistics value;
    value.frameIndex = frameIndex;
    value.width = 320u;
    value.height = 180u;
    value.requestedHardwareBudget = 128u;
    value.effectiveHardwareBudget = 64u;
    value.queueCapacity = 64u;
    value.maxOpticalQueries = 8u;
    value.traceMode = ReflectionTraceMode::Hybrid;
    value.hardwareRequested = true;
    value.hardwareAvailable = true;
    return value;
}

[[nodiscard]] Core::QueueSubmissionToken Token(const u64 value = 20u){
    return Core::QueueSubmissionToken{
        .value = value,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 7u,
        .queue = Core::CommandQueue::Graphics,
    };
}

struct LeaseTask{
    struct Payload{
        ReflectionStatisticsReservation reservation;
    };

    static void Discarded(Payload& payload){ payload.reservation.discard(); }
};

struct DestructorLeaseTask{
    struct Payload{
        ReflectionStatisticsReservation reservation;
    };
};

[[nodiscard]] Core::GpuTaskDesc TaskDesc(){
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests.reflection.statistics.lease"))
        .setMarkerLabel("Statistics lease")
    ;
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ReflectionStatistics, RingSkipsWhenThreeReservationsAreOutstanding){
    StatisticsContext context;
    ASSERT_TRUE(context.control);
    const auto first = context.control->reserve(Metadata());
    const auto second = context.control->reserve(Metadata());
    const auto third = context.control->reserve(Metadata());
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    EXPECT_NE(first.slot, second.slot);
    EXPECT_NE(second.slot, third.slot);
    EXPECT_FALSE(context.control->reserve(Metadata()).valid());
}

TEST(ReflectionStatistics, DiscardedReservationCanBeReusedWithoutStaleCallbackMutation){
    StatisticsContext context;
    const auto previous = context.control->reserve(Metadata());
    context.control->discard(previous);
    const auto current = context.control->reserve(Metadata(10u));
    ASSERT_EQ(previous.slot, current.slot);
    ASSERT_GT(current.sequence, previous.sequence);
    context.control->discard(previous);
    context.control->accept(previous, Token(), true);
    context.control->accept(current, Token(21u), false);
    const auto pending = context.control->pending(current.slot);
    ASSERT_TRUE(pending);
    EXPECT_EQ(pending->key.sequence, current.sequence);
    EXPECT_EQ(pending->token.value, 21u);
}

TEST(ReflectionStatistics, PublishesFrozenMetadataAndNonzeroCountersOnlyAtCompletion){
    StatisticsContext context;
    ReflectionStatistics metadata = Metadata();
    const auto key = context.control->reserve(metadata);
    metadata.frameIndex = 500u;
    metadata.width = 1u;
    context.control->accept(key, Token(), true);
    context.control->discard(key);
    Expected<ReflectionStatistics> statistics;
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    const u32 counters[] = {
        100u, 64u, 30u, 80u, 20u, 50u, 90u, 20u, 98u, 42u, 12u, s_ExpectedDualCount, 1u, 3u, 4u, 5u,
        123u, 25u, 70u, s_ExpectedDualCount, 17u, 3u, 6u, 19u,
    };
    static_assert(sizeof(counters) == NWB_REFLECTION_COUNTER_SIZE);
    context.control->complete(key, Token(), counters);
    statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);

    EXPECT_EQ(statistics->frameIndex, 9u);
    EXPECT_EQ(statistics->width, 320u);

    EXPECT_EQ(statistics->candidates, 100u);

}

TEST(ReflectionStatistics, FeedbackEpochSurvivesAcceptedInputMutation){
    StatisticsContext context;
    ReflectionStatistics metadata = Metadata();
    metadata.feedbackRequested = true;
    metadata.feedbackSequence = 12u;
    metadata.schedulingCounterValid = true;
    const auto key = context.control->reserve(metadata);
    ReflectionFeedbackOutcome feedback;
    feedback.epoch = 4u;
    feedback.startGraphicsFrame = 90u;
    feedback.probeIndex = 7u;
    feedback.eligible = true;
    feedback.reused = true;
    context.control->accept(key, Token(), true, nullptr, &feedback);
    feedback.epoch = 999u;
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(key, Token(), counters);
    Expected<ReflectionStatistics> statistics;
    statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);

    EXPECT_EQ(statistics->feedbackEpoch, 4u);

}

TEST(ReflectionStatistics, OlderCompletionsCannotReplaceNewerPublishedWork){
    StatisticsContext context;
    const auto older = context.control->reserve(Metadata(10u));
    const auto newer = context.control->reserve(Metadata(11u));
    context.control->accept(older, Token(20u), true);
    context.control->accept(newer, Token(21u), false);
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(newer, Token(21u), counters);
    context.control->complete(older, Token(20u), counters);
    Expected<ReflectionStatistics> statistics;
    statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->sequence, newer.sequence);
    EXPECT_EQ(statistics->frameIndex, 11u);
    EXPECT_FALSE(statistics->hardwareReady);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, older.slot);
}

TEST(ReflectionStatistics, CompletionRequiresExactAcceptedPhysicalToken){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, Token(), true);
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    Core::QueueSubmissionToken wrong = Token();
    wrong.physicalQueueIndex = 1u;
    context.control->complete(key, wrong, counters);
    wrong = Token();
    wrong.deviceGeneration = 8u;
    context.control->complete(key, wrong, counters);
    context.control->complete(key, Token(19u), counters);
    Expected<ReflectionStatistics> statistics;
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    context.control->complete(key, Token(), counters);
    statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
}

TEST(ReflectionStatistics, ResetRejectsOldCallbacksAndClearsPublication){
    StatisticsContext context;
    const auto previous = context.control->reserve(Metadata());
    context.control->accept(previous, Token(), true);
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(previous, Token(), counters);
    context.control->reset(8u);
    Expected<ReflectionStatistics> statistics;
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    const auto current = context.control->reserve(Metadata(12u));
    EXPECT_GT(current.generation, previous.generation);
    context.control->accept(previous, Token(), true);
    context.control->discard(previous);
    context.control->complete(previous, Token(), counters);
    Core::QueueSubmissionToken next = Token(1u);
    next.deviceGeneration = 8u;
    context.control->accept(current, next, false);
    context.control->complete(current, next, counters);
    statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->frameIndex, 12u);
    EXPECT_EQ(statistics->acceptedToken.deviceGeneration, 8u);
}

TEST(ReflectionStatistics, FailedMappingRetiresSlotWithoutPublishingZeros){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, Token(), true);
    context.control->complete(key, Token(), nullptr);
    EXPECT_FALSE(context.control->tryGetLatestStatistics());
    EXPECT_EQ(context.control->reserve(Metadata()).slot, key.slot);
}

TEST(ReflectionStatistics, InvalidAcceptedTokenQuarantinesSlotUntilReset){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, {}, true);
    context.control->discard(key);
    EXPECT_FALSE(context.control->pending(key.slot));
    EXPECT_FALSE(context.control->pending(ReflectionStatisticsState::s_SlotCount));
    EXPECT_FALSE(context.control->pending(Limit<u32>::s_Max));
    EXPECT_NE(context.control->reserve(Metadata()).slot, key.slot);
    context.control->reset(7u);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, key.slot);
}

TEST(ReflectionStatistics, LeaseDestructionReclaimsAnUnregisteredReservation){
    StatisticsContext context;
    u32 index = 0u;
    {
        ReflectionStatisticsReservation lease(context.control, Metadata());
        ASSERT_TRUE(lease.valid());
        index = lease.slotIndex();
    }
    EXPECT_EQ(context.control->reserve(Metadata()).slot, index);
}

TEST(ReflectionStatistics, MovingLeaseTransfersOwnershipAndReclaimsAssignmentDestination){
    StatisticsContext context;
    ReflectionStatisticsReservation first(context.control, Metadata());
    const u32 firstSlot = first.slotIndex();
    ReflectionStatisticsReservation second(Move(first));
    EXPECT_FALSE(first.valid());
    EXPECT_EQ(second.slotIndex(), firstSlot);
    ReflectionStatisticsReservation third(context.control, Metadata());
    const u32 replacedSlot = third.slotIndex();
    third = Move(second);
    EXPECT_FALSE(second.valid());
    EXPECT_EQ(third.slotIndex(), firstSlot);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, replacedSlot);
}

TEST(ReflectionStatistics, AcceptedLeaseRemainsInFlightAfterPayloadDestruction){
    StatisticsContext context;
    u32 index = 0u;
    {
        ReflectionStatisticsReservation lease(context.control, Metadata());
        index = lease.slotIndex();
        lease.accept(Token(), true);
    }
    EXPECT_NE(context.control->reserve(Metadata()).slot, index);
    const auto pending = context.control->pending(index);
    ASSERT_TRUE(pending);
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    context.control->complete(pending->key, pending->token, counters);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, index);
}

TEST(ReflectionStatistics, RejectedGraphRegistrationReleasesLease){
    StatisticsContext context;
    Core::GpuTaskGraph graph(context.arena);
    const Core::GpuTaskId task = graph.addTask<LeaseTask>(
        Core::GpuTaskDesc{}, LeaseTask::Payload{ReflectionStatisticsReservation(context.control, Metadata())}
    );
    EXPECT_FALSE(task.valid());
    EXPECT_EQ(context.control->reserve(Metadata()).slot, 0u);
}

TEST(ReflectionStatistics, GraphResetDiscardsUnrecordedReservations){
    StatisticsContext context;
    Core::GpuTaskGraph graph(context.arena);
    ASSERT_TRUE(graph.addTask<LeaseTask>(
        TaskDesc(), LeaseTask::Payload{ReflectionStatisticsReservation(context.control, Metadata())}
    ).valid());
    graph.reset();
    EXPECT_EQ(context.control->reserve(Metadata()).slot, 0u);
}

TEST(ReflectionStatistics, PayloadDestructionReclaimsLeaseWithoutDiscardCallback){
    StatisticsContext context;
    Core::GpuTaskGraph graph(context.arena);
    ASSERT_TRUE(graph.addTask<DestructorLeaseTask>(
        TaskDesc(), DestructorLeaseTask::Payload{ReflectionStatisticsReservation(context.control, Metadata())}
    ).valid());
    graph.reset();
    EXPECT_EQ(context.control->reserve(Metadata()).slot, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

