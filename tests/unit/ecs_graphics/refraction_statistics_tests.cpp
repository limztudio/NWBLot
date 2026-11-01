// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_statistics_test_helpers.h"

#include <impl/ecs_render/refraction/statistics_readback.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

inline constexpr usize s_RefractionCounterWordCount = NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32);

struct StatisticsContext{
    Core::Alloc::GlobalArena arena{Name("tests/refraction/statistics")};
    RefractionStatisticsControlHandle control = CreateStatisticsControl<RefractionStatisticsControl>(arena, 7u).value_or(RefractionStatisticsControlHandle{});
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
    return ReflectionStatisticsTestHelpers::Token<7u, 20u>(value);
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

TEST(RefractionStatistics, CompletionRequiresExactAcceptedPhysicalToken){
    StatisticsContext context;
    const auto key = context.control->reserve(Metadata());
    context.control->accept(key, Token(), true);
    const Array<u32, s_RefractionCounterWordCount> counters = {};
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
    static constexpr Array<u32, s_RefractionCounterWordCount> s_Counters = {
        23u, 41u, 2u, 3u, 5u, 7u, 11u, 43u, 13u, 17u, 19u, 29u, 31u, 47u, 37u, 53u,
        59u, 61u, 67u, 71u, 73u, 79u, 83u, 89u, 97u, 101u, 103u, 107u, 1u,
    };
    context.control->complete(newer, Token(21u), s_Counters);
    const Array<u32, s_RefractionCounterWordCount> olderCounters = {};
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
    const Array<u32, s_RefractionCounterWordCount> counters = {};
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
    const Array<u32, s_RefractionCounterWordCount> counters = {};
    context.control->complete(first, Token(), counters);
    const auto failed = context.control->reserve(Metadata(10u));
    context.control->accept(failed, Token(21u), true);
    context.control->complete(failed, Token(21u), {});
    const auto statistics = context.control->tryGetLatestStatistics();
    ASSERT_TRUE(statistics);
    EXPECT_EQ(statistics->sequence, first.sequence);
    EXPECT_EQ(context.control->reserve(Metadata()).slot, failed.slot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

