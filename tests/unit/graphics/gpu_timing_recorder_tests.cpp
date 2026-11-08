// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/gpu_timing.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_recorder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingRecorderTestsTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsNothrowDestructible_V<Core::GpuTimingFrameTransaction>, "GPU timing frame transactions must unwind without throwing");
static_assert(IsNothrowDestructible_V<Core::GpuTimingMeasure>, "GPU timing measures stored in Optional must unwind without throwing");


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

