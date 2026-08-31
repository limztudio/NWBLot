// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/gpu_timing_metric_correlator.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingTestsTag>;
inline constexpr Name s_GpuTimingMetricScratchArena("tests.gpu_timing.metric_scratch");


class FailOnceTimingSink final : public Core::Perf::TimingSink, NoCopy{
public:
    explicit FailOnceTimingSink(const usize failureAttempt)
        : m_failureAttempt(failureAttempt)
    {}


public:
    [[nodiscard]] virtual bool enabled()const override{ return true; }
    [[nodiscard]] virtual Core::Perf::TimingScopeId registerScope(const Name& scopeName)override{
        static_cast<void>(scopeName);
        const usize attempt = m_registrationAttempt;
        ++m_registrationAttempt;
        if(attempt == m_failureAttempt)
            return {};

        const u32 index = m_nextScopeIndex;
        ++m_nextScopeIndex;
        return { .index = index, .generation = 1u };
    }
    virtual void recordSample(
        const Core::Perf::TimingScopeId scope,
        const f64 seconds,
        const u64 sampleFrameIndex
    )override{
        static_cast<void>(scope);
        static_cast<void>(seconds);
        static_cast<void>(sampleFrameIndex);
    }
    virtual void publishFrame(const u64 publishFrameIndex)override{ static_cast<void>(publishFrameIndex); }


private:
    usize m_failureAttempt = 0u;
    usize m_registrationAttempt = 0u;
    u32 m_nextScopeIndex = 0u;
};


struct GpuTimingSubscriptionCapture{
    u32 sampleCount = 0u;


    static void Invoke(void* const context, const Core::GpuTimingSample&){
        GpuTimingSubscriptionCapture* const capture = static_cast<GpuTimingSubscriptionCapture*>(context);
        if(!capture)
            return;

        ++capture->sampleCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingSampleSubscriptions, AggregateIndependentCollectionRequests){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder recorder(testArena.arena, timingSink);
    GpuTimingSubscriptionCapture firstCapture;
    GpuTimingSubscriptionCapture secondCapture;
    const Core::GpuTimingSampleSubscription first = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{
        .context = &firstCapture,
        .invoke = &GpuTimingSubscriptionCapture::Invoke,
    });
    const Core::GpuTimingSampleSubscription second = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{
        .context = &secondCapture,
        .invoke = &GpuTimingSubscriptionCapture::Invoke,
    });

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
    EXPECT_FALSE(recorder.collectionActive());
    ASSERT_TRUE(recorder.setFeedbackCollectionEnabled(first, true));
    EXPECT_TRUE(recorder.collectionActive());
    ASSERT_TRUE(recorder.setFeedbackCollectionEnabled(second, true));
    ASSERT_TRUE(recorder.setFeedbackCollectionEnabled(first, false));
    EXPECT_TRUE(recorder.collectionActive());
    ASSERT_TRUE(recorder.setFeedbackCollectionEnabled(first, false));
    EXPECT_TRUE(recorder.collectionActive());

    recorder.unsubscribeSampleListener(second);
    EXPECT_FALSE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionEnabled(second, true));
    recorder.unsubscribeSampleListener(second);
    recorder.unsubscribeSampleListener(first);
}

TEST(GpuTimingSampleAttribution, IssuesUniqueProcessIdentitiesAcrossRecorderResetAndRecreation){
    TestArena testArena;
    Core::Perf::TimingRecorder firstSink(testArena.arena);
    Core::Perf::TimingRecorder secondSink(testArena.arena);
    Core::GpuTimingRecorder firstRecorder(testArena.arena, firstSink);
    Core::GpuTimingRecorder secondRecorder(testArena.arena, secondSink);

    const Core::GpuTimingSampleAttribution first = firstRecorder.allocateSampleAttribution();
    const Core::GpuTimingSampleAttribution second = firstRecorder.allocateSampleAttribution();
    const Core::GpuTimingSampleAttribution recreated = secondRecorder.allocateSampleAttribution();
    firstRecorder.resetQueries();
    const Core::GpuTimingSampleAttribution afterReset = firstRecorder.allocateSampleAttribution();

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(recreated.valid());
    ASSERT_TRUE(afterReset.valid());
    EXPECT_NE(first, second);
    EXPECT_NE(first, recreated);
    EXPECT_NE(first, afterReset);
    EXPECT_NE(second, recreated);
    EXPECT_NE(second, afterReset);
    EXPECT_NE(recreated, afterReset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingRecorderStatistics, DefaultsToUnknownDeviceAndExplicitZeroOutcomes){
    static_assert(Core::GpuTimingScopeSkipReason::kCount == 6u);

    const Core::GpuTimingRecorderStatistics statistics;

    EXPECT_FALSE(statistics.valid());
    EXPECT_EQ(statistics.deviceGeneration, 0u);
    EXPECT_EQ(statistics.preparedScopeCount, 0u);
    EXPECT_EQ(statistics.requestedQueryCount, 0u);
    EXPECT_EQ(statistics.materializedQueryCount, 0u);
    EXPECT_EQ(statistics.queryMaterializationFailureCount, 0u);
    EXPECT_EQ(statistics.scopeAttemptCount, 0u);
    EXPECT_EQ(statistics.recordedScopeCount, 0u);
    EXPECT_EQ(statistics.acceptedScopeCount, 0u);
    EXPECT_EQ(statistics.publishedSampleCount, 0u);
    EXPECT_EQ(statistics.unpublishedSampleCount, 0u);
    EXPECT_EQ(statistics.discardedScopeCount, 0u);
    EXPECT_EQ(statistics.quarantinedScopeCount, 0u);
    EXPECT_EQ(statistics.beginFailureCount, 0u);
    for(u8 reason = 0u; reason < Core::GpuTimingScopeSkipReason::kCount; ++reason)
        EXPECT_EQ(statistics.skippedScopeCountByReason[reason], 0u);
    EXPECT_FALSE(statistics.queryCollectionEnabled);
    EXPECT_FALSE(statistics.timingSinkEnabled);
    EXPECT_FALSE(statistics.feedbackCollectionEnabled);
    EXPECT_FALSE(statistics.collectionActive);
    EXPECT_FALSE(statistics.comparableTimestampsSupported);
}


TEST(GpuTimingSample, DefaultsToUnpublishedWithoutComparableRange){
    const Core::GpuTimingSample sample;

    EXPECT_EQ(sample.scopeName, NAME_NONE);
    EXPECT_EQ(sample.sourceFrameIndex, 0u);
    EXPECT_DOUBLE_EQ(sample.durationSeconds, 0.0);
    EXPECT_FALSE(sample.physicalQueue.valid());
    EXPECT_FALSE(sample.comparableRange.valid());
    EXPECT_EQ(sample.attribution, Core::s_NoGpuTimingSampleAttribution);
    EXPECT_FALSE(sample.published);
}


TEST(TimerQueryResult, ConvertsStraightSixtyFourBitTickRange){
    const Core::TimerQueryResult result{
        .beginTicks = 100u,
        .endTicks = 145u,
        .secondsPerTick = 0.25,
        .timestampValidBits = 64u,
        .physicalQueue = { .index = 2u, .deviceGeneration = 11u },
        .comparableAcrossSubmissions = true,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), Limit<u64>::s_Max);
    EXPECT_EQ(result.maskedBeginTicks(), 100u);
    EXPECT_EQ(result.durationTicks(), 45u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 11.25);
    EXPECT_TRUE(result.hasComparableRange());
}

TEST(TimerQueryResult, MasksHighBitsAndWrapsPartialWidth){
    const Core::TimerQueryResult result{
        .beginTicks = 0x1fau,
        .endTicks = 0x105u,
        .secondsPerTick = 0.5,
        .timestampValidBits = 8u,
        .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        .comparableAcrossSubmissions = true,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0xffu);
    EXPECT_EQ(result.maskedBeginTicks(), 0xfau);
    EXPECT_EQ(result.durationTicks(), 11u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 5.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, WrapsFullWidthWithoutShiftingBySixtyFour){
    const Core::TimerQueryResult result{
        .beginTicks = Limit<u64>::s_Max - 3u,
        .endTicks = 5u,
        .secondsPerTick = 1.0,
        .timestampValidBits = 64u,
        .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        .comparableAcrossSubmissions = true,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 9u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 9.0);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, RetainsNearFullWidthDurationWithoutPublishingAbsoluteRange){
    const Core::TimerQueryResult result{
        .beginTicks = (u64{ 1u } << 63u) - 3u,
        .endTicks = 2u,
        .secondsPerTick = 0.5,
        .timestampValidBits = 63u,
        .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        .comparableAcrossSubmissions = true,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 5u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 2.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, ComparableRangeRequiresCapabilityAndPhysicalQueue){
    Core::TimerQueryResult result{
        .beginTicks = 10u,
        .endTicks = 20u,
        .secondsPerTick = 1.0,
        .timestampValidBits = 64u,
        .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        .comparableAcrossSubmissions = false,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_FALSE(result.hasComparableRange());

    result.comparableAcrossSubmissions = true;
    result.physicalQueue = {};
    EXPECT_FALSE(result.hasComparableRange());

    result.physicalQueue = { .index = 1u, .deviceGeneration = 3u };
    EXPECT_TRUE(result.hasComparableRange());
}

TEST(TimerQueryResult, RejectsUnknownOrOutOfRangeWidths){
    Core::TimerQueryResult result{
        .beginTicks = 10u,
        .endTicks = 20u,
        .secondsPerTick = 1.0,
        .timestampValidBits = 0u,
        .physicalQueue = {},
        .comparableAcrossSubmissions = false,
    };

    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);
    EXPECT_EQ(result.durationTicks(), 0u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 0.0);

    result.timestampValidBits = 65u;
    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);

    result.timestampValidBits = 64u;
    result.secondsPerTick = 0.0;
    EXPECT_FALSE(result.valid());
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(GpuComparableTimestampRange, IntersectsRawTicksBeforeFloatingPointConversion){
    static constexpr u64 s_LargeTick = 9007199254740993u;
    const Core::GpuComparableTimestampRange first{
        .beginTicks = s_LargeTick,
        .endTicks = s_LargeTick + 5u,
        .secondsPerTick = 0.25,
        .physicalQueue = { .index = 0u, .deviceGeneration = 7u },
    };
    const Core::GpuComparableTimestampRange second{
        .beginTicks = s_LargeTick + 4u,
        .endTicks = s_LargeTick + 8u,
        .secondsPerTick = 0.25,
        .physicalQueue = { .index = 3u, .deviceGeneration = 7u },
    };

    u64 overlapTicks = 0u;
    ASSERT_TRUE(Core::TryComputeGpuTimestampOverlap(first, second, overlapTicks));
    EXPECT_EQ(overlapTicks, 1u);

    Core::GpuComparableTimestampRange disjoint = second;
    disjoint.beginTicks = first.endTicks;
    disjoint.endTicks = first.endTicks + 2u;
    ASSERT_TRUE(Core::TryComputeGpuTimestampOverlap(first, disjoint, overlapTicks));
    EXPECT_EQ(overlapTicks, 0u);

    Core::GpuComparableTimestampRange recreatedDevice = second;
    recreatedDevice.physicalQueue.deviceGeneration = 8u;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, recreatedDevice, overlapTicks));
    EXPECT_EQ(overlapTicks, 0u);

    Core::GpuComparableTimestampRange mismatchedPeriod = second;
    mismatchedPeriod.secondsPerTick = 0.5;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, mismatchedPeriod, overlapTicks));
    EXPECT_EQ(overlapTicks, 0u);

    Core::GpuComparableTimestampRange infinitePeriod = second;
    infinitePeriod.secondsPerTick = Limit<f64>::s_Infinity;
    EXPECT_FALSE(infinitePeriod.valid());
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, infinitePeriod, overlapTicks));
    EXPECT_EQ(overlapTicks, 0u);
}

TEST(GpuTimingOverlapRegistration, IsCanonicalIdempotentAndRejectsMetricRoleConflicts){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope("tests/timing/overlap/first");
    const Name secondScope("tests/timing/overlap/second");
    const Name thirdScope("tests/timing/overlap/third");
    const Name fourthScope("tests/timing/overlap/fourth");
    const Name outputScope("tests/timing/overlap/output");
    const Name alternateOutputScope("tests/timing/overlap/alternate_output");

    EXPECT_FALSE(correlator.prepareOverlapMetric(NAME_NONE, secondScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, NAME_NONE, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, NAME_NONE));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, firstScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, firstScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, secondScope));
    EXPECT_EQ(timing.scopeCount(), 0u);

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    EXPECT_EQ(timing.scopeCount(), 1u);
    EXPECT_TRUE(correlator.prepareOverlapMetric(secondScope, firstScope, outputScope));
    EXPECT_EQ(timing.scopeCount(), 1u);

    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(secondScope, firstScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(thirdScope, fourthScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(outputScope, thirdScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(thirdScope, fourthScope, firstScope));
    EXPECT_EQ(timing.scopeCount(), 1u);

    EXPECT_TRUE(correlator.prepareOverlapMetric(firstScope, thirdScope, alternateOutputScope));
    EXPECT_EQ(timing.scopeCount(), 2u);
}

TEST(GpuTimingOverlapRegistration, ReversedDuplicatePublishesOneSample){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope("tests/timing/overlap/sample_first");
    const Name secondScope("tests/timing/overlap/sample_second");
    const Name outputScope("tests/timing/overlap/sample_output");
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(secondScope, firstScope, outputScope));
    correlator.recordTimestampRange(
        firstScope,
        41u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 10u,
            .endTicks = 20u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 0u, .deviceGeneration = 3u },
        },
        scratchArena
    );
    correlator.recordTimestampRange(
        secondScope,
        41u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 18u,
            .endTicks = 30u,
            .secondsPerTick = 0.25,
            .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
        },
        scratchArena
    );
    timing.publishFrame(42u);

    const Core::Perf::TimingStats& stats = timing.stats(outputScope);
    ASSERT_EQ(stats.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(stats.seconds, 0.5);
    EXPECT_EQ(stats.firstSampleFrameIndex, 41u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 41u);
}

TEST(GpuTimingOverlapRegistration, DiscardPreservesRegistrationAndResetClearsIt){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope("tests/timing/overlap/lifecycle_first");
    const Name secondScope("tests/timing/overlap/lifecycle_second");
    const Name resetFirstScope("tests/timing/overlap/lifecycle_reset_first");
    const Name resetSecondScope("tests/timing/overlap/lifecycle_reset_second");
    const Name outputScope("tests/timing/overlap/lifecycle_output");
    const Core::GpuComparableTimestampRange firstRange{
        .beginTicks = 10u,
        .endTicks = 20u,
        .secondsPerTick = 0.25,
        .physicalQueue = { .index = 0u, .deviceGeneration = 3u },
    };
    const Core::GpuComparableTimestampRange secondRange{
        .beginTicks = 18u,
        .endTicks = 30u,
        .secondsPerTick = 0.25,
        .physicalQueue = { .index = 1u, .deviceGeneration = 3u },
    };

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    correlator.recordTimestampRange(firstScope, 51u, firstRange, scratchArena);
    correlator.discardPendingRanges();
    correlator.recordTimestampRange(secondScope, 51u, secondRange, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    correlator.recordTimestampRange(firstScope, 53u, firstRange, scratchArena);
    correlator.recordTimestampRange(secondScope, 53u, secondRange, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(outputScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(afterDiscard.seconds, 0.5);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 53u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 53u);

    correlator.recordTimestampRange(firstScope, 55u, firstRange, scratchArena);
    correlator.reset();
    correlator.recordTimestampRange(secondScope, 55u, secondRange, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    ASSERT_TRUE(correlator.prepareOverlapMetric(resetFirstScope, resetSecondScope, outputScope));
    correlator.recordTimestampRange(resetFirstScope, 57u, firstRange, scratchArena);
    correlator.recordTimestampRange(resetSecondScope, 57u, secondRange, scratchArena);
    timing.publishFrame(58u);
    const Core::Perf::TimingStats& afterReset = timing.stats(outputScope);
    ASSERT_EQ(afterReset.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(afterReset.seconds, 0.5);
    EXPECT_EQ(afterReset.firstSampleFrameIndex, 57u);
    EXPECT_EQ(afterReset.lastSampleFrameIndex, 57u);
}

TEST(GpuTimingPacketEnvelopeMetrics, PublishesCompleteOutOfOrderEnvelopeByPhysicalQueue){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope("tests/timing/envelope/first");
    const Name secondScope("tests/timing/envelope/second");
    const Name thirdScope("tests/timing/envelope/third");
    const Name overlapScope("tests/timing/envelope/queue_overlap");
    const Name firstQueueIdleScope("tests/timing/envelope/queue_0_internal_idle");
    const Name secondQueueIdleScope("tests/timing/envelope/queue_1_internal_idle");
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 7u };
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = firstScope, .physicalQueue = firstQueue },
        { .scopeName = secondScope, .physicalQueue = secondQueue },
        { .scopeName = thirdScope, .physicalQueue = firstQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        73u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(
        73u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    correlator.recordTimestampRange(
        thirdScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 15u,
            .endTicks = 20u,
            .secondsPerTick = 0.25,
            .physicalQueue = firstQueue,
        },
        scratchArena
    );
    correlator.recordTimestampRange(
        firstScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 0.25,
            .physicalQueue = firstQueue,
        },
        scratchArena
    );
    timing.publishFrame(74u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    correlator.recordTimestampRange(
        secondScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 5u,
            .endTicks = 18u,
            .secondsPerTick = 0.25,
            .physicalQueue = secondQueue,
        },
        scratchArena
    );
    timing.publishFrame(75u);

    const Core::Perf::TimingStats& overlap = timing.stats(overlapScope);
    const Core::Perf::TimingStats& firstQueueIdle = timing.stats(firstQueueIdleScope);
    const Core::Perf::TimingStats& secondQueueIdle = timing.stats(secondQueueIdleScope);
    ASSERT_EQ(overlap.sampleCount, 1u);
    ASSERT_EQ(firstQueueIdle.sampleCount, 1u);
    ASSERT_EQ(secondQueueIdle.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(overlap.seconds, 2.0);
    EXPECT_DOUBLE_EQ(firstQueueIdle.seconds, 1.25);
    EXPECT_DOUBLE_EQ(secondQueueIdle.seconds, 0.0);
    EXPECT_EQ(overlap.firstSampleFrameIndex, 73u);
    EXPECT_EQ(overlap.lastSampleFrameIndex, 73u);

    correlator.recordTimestampRange(
        thirdScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 15u,
            .endTicks = 20u,
            .secondsPerTick = 0.25,
            .physicalQueue = firstQueue,
        },
        scratchArena
    );
    correlator.recordTimestampRange(
        firstScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 0u,
            .endTicks = 10u,
            .secondsPerTick = 0.25,
            .physicalQueue = firstQueue,
        },
        scratchArena
    );
    correlator.recordTimestampRange(
        secondScope,
        73u,
        Core::GpuComparableTimestampRange{
            .beginTicks = 5u,
            .endTicks = 18u,
            .secondsPerTick = 0.25,
            .physicalQueue = secondQueue,
        },
        scratchArena
    );
    timing.publishFrame(76u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());
}

TEST(GpuTimingPacketEnvelopeMetrics, RejectsOutputRoleAndPhysicalQueueCollisions){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope("tests/timing/envelope/role_first");
    const Name secondScope("tests/timing/envelope/role_second");
    const Name overlapScope("tests/timing/envelope/role_overlap");
    const Name alternateOverlapScope("tests/timing/envelope/role_alternate_overlap");
    const Name firstQueueIdleScope("tests/timing/envelope/role_queue_0_idle");
    const Name secondQueueIdleScope("tests/timing/envelope/role_queue_1_idle");
    const Name alternateFirstQueueIdleScope("tests/timing/envelope/role_alternate_queue_0_idle");
    const Name alternateSecondQueueIdleScope("tests/timing/envelope/role_alternate_queue_1_idle");
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = firstScope, .physicalQueue = firstQueue },
        { .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        80u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));

    const Core::GpuPacketEnvelopeMetricQueueOutput overlapAsIdleOutputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = overlapScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(
        81u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        alternateOverlapScope,
        MakeNotNull(&overlapAsIdleOutputs[0u]),
        LengthOf(overlapAsIdleOutputs)
    ));

    const Core::GpuPacketEnvelopeMetricQueueOutput alternateOutputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(
        81u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        firstQueueIdleScope,
        MakeNotNull(&alternateOutputs[0u]),
        LengthOf(alternateOutputs)
    ));

    const Core::GpuPacketEnvelopeMetricQueueOutput remappedOutputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = firstQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(
        81u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        alternateOverlapScope,
        MakeNotNull(&remappedOutputs[0u]),
        LengthOf(remappedOutputs)
    ));
    EXPECT_EQ(timing.scopeCount(), 3u);
}

TEST(GpuTimingPacketEnvelopeMetrics, RetainsSuccessfulOutputRolesWhenSinkRegistrationFails){
    TestArena testArena;
    FailOnceTimingSink timing(2u);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope("tests/timing/envelope/failure_first");
    const Name secondScope("tests/timing/envelope/failure_second");
    const Name retryFirstScope("tests/timing/envelope/failure_retry_first");
    const Name retrySecondScope("tests/timing/envelope/failure_retry_second");
    const Name overlapScope("tests/timing/envelope/failure_overlap");
    const Name firstQueueIdleScope("tests/timing/envelope/failure_queue_0_idle");
    const Name secondQueueIdleScope("tests/timing/envelope/failure_queue_1_idle");
    const Name alternateSecondQueueIdleScope("tests/timing/envelope/failure_alternate_queue_1_idle");
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 13u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 13u };
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = firstScope, .physicalQueue = firstQueue },
        { .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput retryOutputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };

    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(
        90u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, firstQueueIdleScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, secondQueueIdleScope));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(
        90u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&retryOutputs[0u]),
        LengthOf(retryOutputs)
    ));
}

TEST(GpuTimingPacketEnvelopeMetrics, IsolatesFramesRejectsWrongQueuesPrunesIncompleteFramesAndResetsState){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope("tests/timing/envelope/isolation_first");
    const Name secondScope("tests/timing/envelope/isolation_second");
    const Name overlapScope("tests/timing/envelope/isolation_overlap");
    const Name firstQueueIdleScope("tests/timing/envelope/isolation_queue_0_idle");
    const Name secondQueueIdleScope("tests/timing/envelope/isolation_queue_1_idle");
    const Name resetFirstOutputScope("tests/timing/envelope/isolation_reset_first_output");
    const Name resetSecondInputScope("tests/timing/envelope/isolation_reset_second_input");
    const Name resetSecondOutputScope("tests/timing/envelope/isolation_reset_second_output");
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 11u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 11u };
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = firstScope, .physicalQueue = firstQueue },
        { .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        { .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        50u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        51u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));

    const Core::GpuComparableTimestampRange firstRange{
        .beginTicks = 2u,
        .endTicks = 8u,
        .secondsPerTick = 0.5,
        .physicalQueue = firstQueue,
    };
    const Core::GpuComparableTimestampRange secondRange{
        .beginTicks = 4u,
        .endTicks = 10u,
        .secondsPerTick = 0.5,
        .physicalQueue = secondQueue,
    };
    correlator.recordTimestampRange(firstScope, 50u, firstRange, scratchArena);
    correlator.recordTimestampRange(secondScope, 51u, secondRange, scratchArena);
    correlator.recordTimestampRange(firstScope, 51u, secondRange, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    correlator.recordTimestampRange(firstScope, 51u, firstRange, scratchArena);
    timing.publishFrame(53u);
    const Core::Perf::TimingStats& frameFiftyOne = timing.stats(overlapScope);
    ASSERT_EQ(frameFiftyOne.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFiftyOne.seconds, 2.0);
    EXPECT_EQ(frameFiftyOne.firstSampleFrameIndex, 51u);

    correlator.recordTimestampRange(secondScope, 50u, secondRange, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& frameFifty = timing.stats(overlapScope);
    ASSERT_EQ(frameFifty.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFifty.seconds, 2.0);
    EXPECT_EQ(frameFifty.firstSampleFrameIndex, 50u);
    EXPECT_EQ(frameFifty.lastSampleFrameIndex, 50u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        55u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    correlator.recordTimestampRange(firstScope, 55u, firstRange, scratchArena);
    correlator.discardPendingRanges();
    correlator.recordTimestampRange(secondScope, 55u, secondRange, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, firstQueueIdleScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, secondQueueIdleScope));

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        57u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    correlator.recordTimestampRange(firstScope, 57u, firstRange, scratchArena);
    correlator.recordTimestampRange(secondScope, 57u, secondRange, scratchArena);
    timing.publishFrame(58u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(overlapScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 57u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 57u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        60u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    correlator.recordTimestampRange(firstScope, 60u, firstRange, scratchArena);
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(
        1000u,
        MakeNotNull(&scopes[0u]),
        LengthOf(scopes),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    ));
    correlator.recordTimestampRange(secondScope, 60u, secondRange, scratchArena);
    timing.publishFrame(1001u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());

    correlator.recordTimestampRange(firstScope, 1000u, firstRange, scratchArena);
    correlator.reset();
    correlator.recordTimestampRange(secondScope, 1000u, secondRange, scratchArena);
    timing.publishFrame(1002u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());

    ASSERT_TRUE(correlator.prepareOverlapMetric(overlapScope, firstQueueIdleScope, resetFirstOutputScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(secondQueueIdleScope, resetSecondInputScope, resetSecondOutputScope));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

