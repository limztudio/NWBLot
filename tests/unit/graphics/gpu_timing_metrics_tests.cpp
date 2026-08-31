// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/gpu_timing_metrics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metrics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuPacketEnvelopeMetrics, UnionsSameQueueRangesAndOrdersEveryQueue){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/union"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 8u> ranges{{
        {
            .beginTicks = 40u,
            .endTicks = 50u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 100u,
            .endTicks = 100u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 1u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 8u,
            .endTicks = 20u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 5u,
            .endTicks = 15u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 0u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 20u,
            .endTicks = 30u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 42u,
            .endTicks = 45u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
        {
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 2u, .deviceGeneration = 7u },
        },
    }};
    Core::GpuPacketEnvelopeMetrics metrics;

    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        ranges.data(),
        ranges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_DOUBLE_EQ(metrics.secondsPerTick, 0.25);
    EXPECT_EQ(metrics.queueOverlapTicks, 10u);
    ASSERT_EQ(queueMetrics.size(), 3u);
    EXPECT_EQ(queueMetrics[0u].physicalQueue.index, 0u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].physicalQueue.index, 1u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[2u].physicalQueue.index, 2u);
    EXPECT_EQ(queueMetrics[2u].internalIdleTicks, 10u);
}

TEST(GpuPacketEnvelopeMetrics, CountsConcurrentQueueUnionOnceAtTripleOverlap){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/triple"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 3u> ranges{{
        {
            .beginTicks = 0u,
            .endTicks = 100u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 0u, .deviceGeneration = 2u },
        },
        {
            .beginTicks = 10u,
            .endTicks = 90u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 2u },
        },
        {
            .beginTicks = 20u,
            .endTicks = 80u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 2u, .deviceGeneration = 2u },
        },
    }};
    Core::GpuPacketEnvelopeMetrics metrics;

    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        ranges.data(),
        ranges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 80u);
    ASSERT_EQ(queueMetrics.size(), 3u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[2u].internalIdleTicks, 0u);
}

TEST(GpuPacketEnvelopeMetrics, MeasuresAlternatingInternalGapsAndOverlap){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/alternating"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 4u> ranges{{
        {
            .beginTicks = 20u,
            .endTicks = 30u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 0u, .deviceGeneration = 5u },
        },
        {
            .beginTicks = 5u,
            .endTicks = 15u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 5u },
        },
        {
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 0u, .deviceGeneration = 5u },
        },
        {
            .beginTicks = 25u,
            .endTicks = 35u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 5u },
        },
    }};
    Core::GpuPacketEnvelopeMetrics metrics;

    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        ranges.data(),
        ranges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 10u);
    ASSERT_EQ(queueMetrics.size(), 2u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 10u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 10u);
}

TEST(GpuPacketEnvelopeMetrics, PreservesLargeAndNearLimitTickPrecision){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/precision"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    static constexpr u64 s_LargeTick = 9007199254740993u;
    const Array<Core::GpuComparableTimestampRange, 3u> largeRanges{{
        {
            .beginTicks = s_LargeTick,
            .endTicks = s_LargeTick + 20u,
            .secondsPerTick = 0.5,
            .physicalQueue = { .index = 5u, .deviceGeneration = 9u },
        },
        {
            .beginTicks = s_LargeTick + 30u,
            .endTicks = s_LargeTick + 40u,
            .secondsPerTick = 0.5,
            .physicalQueue = { .index = 5u, .deviceGeneration = 9u },
        },
        {
            .beginTicks = s_LargeTick + 5u,
            .endTicks = s_LargeTick + 35u,
            .secondsPerTick = 0.5,
            .physicalQueue = { .index = 6u, .deviceGeneration = 9u },
        },
    }};
    Core::GpuPacketEnvelopeMetrics metrics;

    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        largeRanges.data(),
        largeRanges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 20u);
    ASSERT_EQ(queueMetrics.size(), 2u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 10u);

    const Array<Core::GpuComparableTimestampRange, 2u> nearLimitRanges{{
        {
            .beginTicks = Limit<u64>::s_Max - 20u,
            .endTicks = Limit<u64>::s_Max - 10u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 4u },
        },
        {
            .beginTicks = Limit<u64>::s_Max - 15u,
            .endTicks = Limit<u64>::s_Max - 5u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 2u, .deviceGeneration = 4u },
        },
    }};
    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        nearLimitRanges.data(),
        nearLimitRanges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 5u);
}

TEST(GpuPacketEnvelopeMetrics, AcceptsCompleteZeroOverlapInputs){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/zero"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 2u> touchingRanges{{
        {
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 0u, .deviceGeneration = 6u },
        },
        {
            .beginTicks = 10u,
            .endTicks = 20u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 6u },
        },
    }};
    Core::GpuPacketEnvelopeMetrics metrics;

    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        touchingRanges.data(),
        touchingRanges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 0u);
    ASSERT_EQ(queueMetrics.size(), 2u);

    const Array<Core::GpuComparableTimestampRange, 1u> singleRange{{
        {
            .beginTicks = 5u,
            .endTicks = 15u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 2u, .deviceGeneration = 6u },
        },
    }};
    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        singleRange.data(),
        singleRange.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 0u);
    ASSERT_EQ(queueMetrics.size(), 1u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);

    const Array<Core::GpuComparableTimestampRange, 2u> zeroLengthRanges{{
        {
            .beginTicks = 25u,
            .endTicks = 25u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 3u, .deviceGeneration = 6u },
        },
        {
            .beginTicks = 75u,
            .endTicks = 75u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 6u },
        },
    }};
    ASSERT_TRUE(Core::TryAggregateGpuPacketEnvelopeMetrics(
        zeroLengthRanges.data(),
        zeroLengthRanges.size(),
        metrics,
        queueMetrics,
        scratchArena
    ));
    EXPECT_EQ(metrics.queueOverlapTicks, 0u);
    ASSERT_EQ(queueMetrics.size(), 2u);
    EXPECT_EQ(queueMetrics[0u].physicalQueue.index, 1u);
    EXPECT_EQ(queueMetrics[1u].physicalQueue.index, 3u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
}

TEST(GpuPacketEnvelopeMetrics, RejectsInvalidOrIncomparableInputAndClearsOutputs){
    Core::Alloc::ScratchArena scratchArena(Name("tests/timing/packet_metrics/rejection"));
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    Core::GpuPacketEnvelopeMetrics metrics;
    const Array<Core::GpuComparableTimestampRange, 2u> validRanges{{
        {
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 0u, .deviceGeneration = 3u },
        },
        {
            .beginTicks = 5u,
            .endTicks = 15u,
            .secondsPerTick = 1.0,
            .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        },
    }};
    const auto expectRejected = [&](const Core::GpuComparableTimestampRange* const ranges, const usize rangeCount){
        metrics.secondsPerTick = 7.0;
        metrics.queueOverlapTicks = 11u;
        queueMetrics.push_back(Core::GpuQueuePacketEnvelopeMetrics{
            .physicalQueue = { .index = 4u, .deviceGeneration = 8u },
            .internalIdleTicks = 13u,
        });
        EXPECT_FALSE(Core::TryAggregateGpuPacketEnvelopeMetrics(
            ranges,
            rangeCount,
            metrics,
            queueMetrics,
            scratchArena
        ));
        EXPECT_DOUBLE_EQ(metrics.secondsPerTick, 0.0);
        EXPECT_EQ(metrics.queueOverlapTicks, 0u);
        EXPECT_TRUE(queueMetrics.empty());
    };

    expectRejected(nullptr, 0u);
    expectRejected(nullptr, 1u);
    expectRejected(validRanges.data(), 0u);
    Array<Core::GpuComparableTimestampRange, 2u> invalidRanges{};

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[1u] = validRanges[1u];
    invalidRanges[0u].beginTicks = 11u;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[0u].physicalQueue = {};
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[0u].secondsPerTick = Limit<f64>::s_Infinity;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[0u].secondsPerTick = Limit<f64>::s_QuietNaN;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[0u].secondsPerTick = 0.0;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[1u].secondsPerTick = 0.5;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    invalidRanges[1u] = validRanges[1u];
    invalidRanges[1u].physicalQueue.deviceGeneration = 4u;
    expectRejected(invalidRanges.data(), invalidRanges.size());

    const Core::GpuComparableTimestampRange* const inaccessibleRanges =
        reinterpret_cast<const Core::GpuComparableTimestampRange*>(usize{ 1u });
    expectRejected(inaccessibleRanges, Limit<usize>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

