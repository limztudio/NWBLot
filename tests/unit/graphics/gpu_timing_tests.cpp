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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsNothrowDestructible_V<Core::GpuTimingFrameTransaction>, "GPU timing frame transactions must unwind without throwing");
static_assert(IsNothrowDestructible_V<Core::GpuTimingMeasure>, "GPU timing measures stored in Optional must unwind without throwing");


class FailOnceTimingSink final : public Core::Perf::TimingSink, NoCopy{
public:
    explicit FailOnceTimingSink(const usize failureAttempt)noexcept
        : m_failureAttempt(failureAttempt)
    {}


public:
    [[nodiscard]] virtual b8 enabled()const noexcept override{ return true; }

    [[nodiscard]] virtual Expected<Core::Perf::TimingScopeId> registerScope([[maybe_unused]] const Name& scopeName)noexcept override{
        const usize attempt = m_registrationAttempt;
        ++m_registrationAttempt;
        if(attempt == m_failureAttempt)
            return MakeUnexpected(Failure{});

        const u32 index = m_nextScopeIndex;
        ++m_nextScopeIndex;
        return Core::Perf::TimingScopeId{ .index = index, .generation = 1u };
    }

    virtual void recordSample([[maybe_unused]] const Core::Perf::TimingScopeId scope, [[maybe_unused]] const f64 seconds, [[maybe_unused]] const u64 sampleFrameIndex)noexcept override{}

    virtual void publishFrame([[maybe_unused]] const u64 publishFrameIndex)noexcept override{}


private:
    usize m_failureAttempt = 0u;
    usize m_registrationAttempt = 0u;
    u32 m_nextScopeIndex = 0u;
};


void RecordAndPublishTimestampRange(const Name& scopeName, const u64 frameIndex, const Core::GpuComparableTimestampRange& range, Core::GpuTimingMetricCorrelator& correlator, Core::Perf::TimingSink& timing, Core::Alloc::ScratchArena& scratchArena){
    Core::GpuTimingSinkSampleVector performanceSamples{ scratchArena };
    correlator.recordTimestampRange(scopeName, frameIndex, range, scratchArena, performanceSamples);
    for(const Core::GpuTimingSinkSample& sample : performanceSamples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
}


struct GpuTimingSubscriptionCapture{
    u32 sampleCount = 0u;


public:
    static void Invoke(void* const context, const Core::GpuTimingSample&)noexcept{
        GpuTimingSubscriptionCapture* const capture = static_cast<GpuTimingSubscriptionCapture*>(context);
        if(!capture)
            return;

        ++capture->sampleCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingSampleSubscriptions, ScopedDemandsReplaceAtomicallyAndRemainSubscriptionIsolated){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder recorder(testArena.arena, timingSink);
    GpuTimingSubscriptionCapture firstCapture;
    GpuTimingSubscriptionCapture secondCapture;
    const Name firstScope("tests.gpu_timing.feedback.first");
    const Name sharedScope("tests.gpu_timing.feedback.shared");
    const Name secondScope("tests.gpu_timing.feedback.second");
    const Name metricFirstInput("tests.gpu_timing.feedback.metric_first_input");
    const Name metricSecondInput("tests.gpu_timing.feedback.metric_second_input");
    const Name metricAlternateFirstInput("tests.gpu_timing.feedback.metric_alternate_first_input");
    const Name metricAlternateSecondInput("tests.gpu_timing.feedback.metric_alternate_second_input");
    const Name metricOutput("tests.gpu_timing.feedback.metric_output");
    const Array<Name, 2u> firstScopes = { firstScope, sharedScope };
    const Array<Name, 2u> secondScopes = { sharedScope, secondScope };
    const Array<Name, 2u> invalidScopes = { firstScope, s_NameNone };
    const Array<Name, 2u> duplicateScopes = { firstScope, firstScope };
    const auto firstResult = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{ .context = &firstCapture, .invoke = &GpuTimingSubscriptionCapture::Invoke, });
    ASSERT_TRUE(firstResult);
    const Core::GpuTimingSampleSubscription first = *firstResult;
    const auto secondResult = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{ .context = &secondCapture, .invoke = &GpuTimingSubscriptionCapture::Invoke, });
    ASSERT_TRUE(secondResult);
    const Core::GpuTimingSampleSubscription second = *secondResult;

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    static_assert(noexcept(recorder.unsubscribeSampleListener(first)));
    EXPECT_NE(first, second);
    EXPECT_FALSE(recorder.collectionActive());
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, firstScopes));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, firstScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, invalidScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, duplicateScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.prepareOverlapMetric(metricFirstInput, metricSecondInput, firstScope));
    ASSERT_TRUE(recorder.prepareOverlapMetric(metricFirstInput, metricSecondInput, metricOutput));
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&metricOutput, 1u)));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.prepareOverlapMetric(metricAlternateFirstInput, metricAlternateSecondInput, firstScope));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(second, secondScopes));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    ASSERT_TRUE(recorder.clearFeedbackCollectionScopes(first));
    EXPECT_TRUE(recorder.collectionActive());

    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    recorder.unsubscribeSampleListener(second);
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(second, Span<const Name>(&secondScope, 1u)));
    EXPECT_FALSE(recorder.clearFeedbackCollectionScopes(second));

    ASSERT_TRUE(recorder.clearFeedbackCollectionScopes(first));
    EXPECT_FALSE(recorder.collectionActive());
    EXPECT_TRUE(recorder.clearFeedbackCollectionScopes(first));

    recorder.unsubscribeSampleListener(first);
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    EXPECT_FALSE(recorder.clearFeedbackCollectionScopes(first));
}

TEST(GpuTimingSampleAttribution, IssuesUniqueProcessIdentitiesAcrossRecorderResetAndRecreation){
    TestArena testArena;
    Core::Perf::TimingRecorder firstSink(testArena.arena);
    Core::Perf::TimingRecorder secondSink(testArena.arena);
    Core::GpuTimingRecorder firstRecorder(testArena.arena, firstSink);
    Core::GpuTimingRecorder secondRecorder(testArena.arena, secondSink);

    const auto firstResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(firstResult);
    const Core::GpuTimingSampleAttribution first = *firstResult;
    const auto secondResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(secondResult);
    const Core::GpuTimingSampleAttribution second = *secondResult;
    const auto recreatedResult = secondRecorder.allocateSampleAttribution();
    ASSERT_TRUE(recreatedResult);
    const Core::GpuTimingSampleAttribution recreated = *recreatedResult;
    firstRecorder.resetQueries();
    const auto afterResetResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(afterResetResult);
    const Core::GpuTimingSampleAttribution afterReset = *afterResetResult;

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


TEST(TimerQueryResult, MasksHighBitsAndWrapsPartialWidth){
    const Core::TimerQueryResult result{ .beginTicks = 0x1fau, .endTicks = 0x105u, .secondsPerTick = 0.5, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 8u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0xffu);
    EXPECT_EQ(result.maskedBeginTicks(), 0xfau);
    EXPECT_EQ(result.durationTicks(), 11u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 5.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, WrapsFullWidthWithoutShiftingBySixtyFour){
    const Core::TimerQueryResult result{ .beginTicks = Limit<u64>::s_Max - 3u, .endTicks = 5u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 64u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 9u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 9.0);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, RetainsNearFullWidthDurationWithoutPublishingAbsoluteRange){
    const Core::TimerQueryResult result{ .beginTicks = (u64{ 1u } << 63u) - 3u, .endTicks = 2u, .secondsPerTick = 0.5, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 63u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 5u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 2.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, ComparableRangeRequiresCapabilityAndPhysicalQueue){
    Core::TimerQueryResult result{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 64u, .comparableAcrossSubmissions = false, };

    ASSERT_TRUE(result.valid());
    EXPECT_FALSE(result.hasComparableRange());

    result.comparableAcrossSubmissions = true;
    result.physicalQueue = {};
    EXPECT_FALSE(result.hasComparableRange());

    result.physicalQueue = { .index = 1u, .deviceGeneration = 3u };
    EXPECT_TRUE(result.hasComparableRange());
}

TEST(TimerQueryResult, RejectsUnknownOrOutOfRangeWidths){
    Core::TimerQueryResult result{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 1.0, .physicalQueue = {}, .timestampValidBits = 0u, .comparableAcrossSubmissions = false, };

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
    const Core::GpuComparableTimestampRange first{ .beginTicks = s_LargeTick, .endTicks = s_LargeTick + 5u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 7u }, };
    const Core::GpuComparableTimestampRange second{ .beginTicks = s_LargeTick + 4u, .endTicks = s_LargeTick + 8u, .secondsPerTick = 0.25, .physicalQueue = { .index = 3u, .deviceGeneration = 7u }, };

    const auto overlapTicks = Core::TryComputeGpuTimestampOverlap(first, second);
    ASSERT_TRUE(overlapTicks);
    EXPECT_EQ(*overlapTicks, 1u);

    Core::GpuComparableTimestampRange disjoint = second;
    disjoint.beginTicks = first.endTicks;
    disjoint.endTicks = first.endTicks + 2u;
    const auto disjointOverlap = Core::TryComputeGpuTimestampOverlap(first, disjoint);
    ASSERT_TRUE(disjointOverlap);
    EXPECT_EQ(*disjointOverlap, 0u);

    Core::GpuComparableTimestampRange recreatedDevice = second;
    recreatedDevice.physicalQueue.deviceGeneration = 8u;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, recreatedDevice));

    Core::GpuComparableTimestampRange mismatchedPeriod = second;
    mismatchedPeriod.secondsPerTick = 0.5;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, mismatchedPeriod));

    Core::GpuComparableTimestampRange infinitePeriod = second;
    infinitePeriod.secondsPerTick = Limit<f64>::s_Infinity;
    EXPECT_FALSE(infinitePeriod.valid());
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, infinitePeriod));
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

    EXPECT_FALSE(correlator.prepareOverlapMetric(s_NameNone, secondScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, s_NameNone, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, s_NameNone));
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
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 18u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };
    Core::GpuTimingSinkSampleVector performanceSamples{ scratchArena };

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(secondScope, firstScope, outputScope));
    correlator.recordTimestampRange(firstScope, 41u, firstRange, scratchArena, performanceSamples);
    EXPECT_TRUE(performanceSamples.empty());
    correlator.recordTimestampRange(secondScope, 41u, secondRange, scratchArena, performanceSamples);
    ASSERT_EQ(performanceSamples.size(), 1u);
    const Core::GpuTimingSinkSample publishedSample = performanceSamples.front();
    const Core::Perf::TimingScopeId outputScopeId = timing.scopeAt(0u);
    EXPECT_EQ(publishedSample.scope.index, outputScopeId.index);
    EXPECT_EQ(publishedSample.scope.generation, outputScopeId.generation);
    EXPECT_DOUBLE_EQ(publishedSample.durationSeconds, 0.5);
    EXPECT_EQ(publishedSample.sourceFrameIndex, 41u);

    performanceSamples.clear();
    correlator.recordTimestampRange(secondScope, 41u, secondRange, scratchArena, performanceSamples);
    EXPECT_TRUE(performanceSamples.empty());
    timing.recordSample(publishedSample.scope, publishedSample.durationSeconds, publishedSample.sourceFrameIndex);
    timing.publishFrame(42u);

    const Core::Perf::TimingStats& stats = timing.stats(outputScope);
    ASSERT_EQ(stats.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(stats.seconds, 0.5);
    EXPECT_EQ(stats.firstSampleFrameIndex, 41u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 41u);
}

TEST(GpuTimingOverlapRegistration, CatchUpBatchBoundsSourceFramesAndPreservesLastArrivalDuration){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope("tests/timing/overlap/catch_up_first");
    const Name secondScope("tests/timing/overlap/catch_up_second");
    const Name outputScope("tests/timing/overlap/catch_up_output");
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange newerRange{ .beginTicks = 18u, .endTicks = 34u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };
    Core::GpuComparableTimestampRange olderRange = newerRange;
    olderRange.beginTicks = 26u;
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    correlator.recordTimestampRange(firstScope, 50u, firstRange, scratchArena, samples);
    correlator.recordTimestampRange(firstScope, 51u, firstRange, scratchArena, samples);
    correlator.recordTimestampRange(secondScope, 51u, newerRange, scratchArena, samples);
    correlator.recordTimestampRange(secondScope, 50u, olderRange, scratchArena, samples);
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0u].sourceFrameIndex, 51u);
    EXPECT_EQ(samples[1u].sourceFrameIndex, 50u);
    EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 3.0);
    EXPECT_DOUBLE_EQ(samples[1u].durationSeconds, 1.0);
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(52u);

    const Core::Perf::TimingStats& stats = timing.stats(outputScope);
    ASSERT_EQ(stats.sampleCount, 2u);
    EXPECT_EQ(stats.publishFrameIndex, 52u);
    EXPECT_EQ(stats.firstSampleFrameIndex, 50u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 51u);
    EXPECT_DOUBLE_EQ(stats.seconds, 4.0);
    EXPECT_DOUBLE_EQ(stats.minSeconds, 1.0);
    EXPECT_DOUBLE_EQ(stats.maxSeconds, 3.0);
    EXPECT_DOUBLE_EQ(stats.lastSeconds, 1.0);
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
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 18u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    RecordAndPublishTimestampRange(firstScope, 51u, firstRange, correlator, timing, scratchArena);
    correlator.discardPendingRanges();
    RecordAndPublishTimestampRange(secondScope, 51u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    RecordAndPublishTimestampRange(firstScope, 53u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 53u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(outputScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(afterDiscard.seconds, 0.5);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 53u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 53u);

    RecordAndPublishTimestampRange(firstScope, 55u, firstRange, correlator, timing, scratchArena);
    correlator.reset();
    RecordAndPublishTimestampRange(secondScope, 55u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    ASSERT_TRUE(correlator.prepareOverlapMetric(resetFirstScope, resetSecondScope, outputScope));
    RecordAndPublishTimestampRange(resetFirstScope, 57u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(resetSecondScope, 57u, secondRange, correlator, timing, scratchArena);
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
    const Array<Core::GpuPacketEnvelopeMetricScope, 3u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = thirdScope, .physicalQueue = firstQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(thirdScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 15u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    timing.publishFrame(74u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    RecordAndPublishTimestampRange(secondScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 5u, .endTicks = 18u, .secondsPerTick = 0.25, .physicalQueue = secondQueue, }, correlator, timing, scratchArena);
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

    RecordAndPublishTimestampRange(thirdScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 15u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 5u, .endTicks = 18u, .secondsPerTick = 0.25, .physicalQueue = secondQueue, }, correlator, timing, scratchArena);
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
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(80u, scopes, overlapScope, outputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> overlapAsIdleOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = overlapScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, alternateOverlapScope, overlapAsIdleOutputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> alternateOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, firstQueueIdleScope, alternateOutputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> remappedOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = firstQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, alternateOverlapScope, remappedOutputs));
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
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> retryOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };

    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(90u, scopes, overlapScope, outputs));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, firstQueueIdleScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, secondQueueIdleScope));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(90u, scopes, overlapScope, retryOutputs));
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
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(50u, scopes, overlapScope, outputs));
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(51u, scopes, overlapScope, outputs));

    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 2u, .endTicks = 8u, .secondsPerTick = 0.5, .physicalQueue = firstQueue, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 4u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = secondQueue, };
    RecordAndPublishTimestampRange(firstScope, 50u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 51u, secondRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 51u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    RecordAndPublishTimestampRange(firstScope, 51u, firstRange, correlator, timing, scratchArena);
    timing.publishFrame(53u);
    const Core::Perf::TimingStats& frameFiftyOne = timing.stats(overlapScope);
    ASSERT_EQ(frameFiftyOne.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFiftyOne.seconds, 2.0);
    EXPECT_EQ(frameFiftyOne.firstSampleFrameIndex, 51u);

    RecordAndPublishTimestampRange(secondScope, 50u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& frameFifty = timing.stats(overlapScope);
    ASSERT_EQ(frameFifty.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFifty.seconds, 2.0);
    EXPECT_EQ(frameFifty.firstSampleFrameIndex, 50u);
    EXPECT_EQ(frameFifty.lastSampleFrameIndex, 50u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(55u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 55u, firstRange, correlator, timing, scratchArena);
    correlator.discardPendingRanges();
    RecordAndPublishTimestampRange(secondScope, 55u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, firstQueueIdleScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, secondQueueIdleScope));

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(57u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 57u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 57u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(58u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(overlapScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 57u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 57u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(60u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 60u, firstRange, correlator, timing, scratchArena);
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(1000u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(secondScope, 60u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(1001u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());

    RecordAndPublishTimestampRange(firstScope, 1000u, firstRange, correlator, timing, scratchArena);
    correlator.reset();
    RecordAndPublishTimestampRange(secondScope, 1000u, secondRange, correlator, timing, scratchArena);
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

