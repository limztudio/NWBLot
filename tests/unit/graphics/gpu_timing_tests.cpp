// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/gpu_timing.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Narrow diagnostic peer for deterministic CPU validation of private listener-dispatch batch semantics.
class GpuTimingRecorderDiagnosticPeer final{
public:
    static void dispatchSample(GpuTimingRecorder& recorder, const GpuTimingSample& sample){
        GpuTimingRecorder::SampleVector samples{ recorder.m_arena };
        GpuTimingRecorder::SampleSubscriptionVector subscriptions{ recorder.m_arena };
        {
            ScopedLock lock(recorder.m_sampleListenerMutex);

            recorder.snapshotSampleSubscriptionsLocked(subscriptions);
        }
        samples.push_back(sample);
        recorder.dispatchCompletedSamples(samples, subscriptions);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingTestsTag>;


struct GpuTimingSubscriptionCapture{
    Core::GpuTimingRecorder* recorder = nullptr;
    Core::GpuTimingSampleSubscription* subscription = nullptr;
    Core::GpuTimingSampleSubscription* replacementSubscription = nullptr;
    GpuTimingSubscriptionCapture* replacementCapture = nullptr;
    u32 sampleCount = 0u;
    bool replaceOnFirstSample = false;


    static void Invoke(void* const context, const Core::GpuTimingSample&){
        GpuTimingSubscriptionCapture* const capture = static_cast<GpuTimingSubscriptionCapture*>(context);
        if(!capture)
            return;

        ++capture->sampleCount;
        if(
            !capture->replaceOnFirstSample
            || !capture->recorder
            || !capture->subscription
            || !capture->replacementSubscription
            || !capture->replacementCapture
        )
            return;

        capture->replaceOnFirstSample = false;
        capture->recorder->unsubscribeSampleListener(*capture->subscription);
        *capture->replacementSubscription = capture->recorder->subscribeSampleListener(Core::GpuTimingSampleListener{
            .context = capture->replacementCapture,
            .invoke = &Invoke,
        });
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

TEST(GpuTimingSampleSubscriptions, ReplacementStartsWithNextDispatchBatch){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder recorder(testArena.arena, timingSink);
    Core::GpuTimingSampleSubscription firstSubscription;
    Core::GpuTimingSampleSubscription secondSubscription;
    Core::GpuTimingSampleSubscription replacementSubscription;
    GpuTimingSubscriptionCapture replacementCapture;
    GpuTimingSubscriptionCapture firstCapture{
        .recorder = &recorder,
        .subscription = &firstSubscription,
        .replacementSubscription = &replacementSubscription,
        .replacementCapture = &replacementCapture,
        .replaceOnFirstSample = true,
    };
    GpuTimingSubscriptionCapture secondCapture;
    firstSubscription = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{
        .context = &firstCapture,
        .invoke = &GpuTimingSubscriptionCapture::Invoke,
    });
    secondSubscription = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{
        .context = &secondCapture,
        .invoke = &GpuTimingSubscriptionCapture::Invoke,
    });
    ASSERT_TRUE(firstSubscription.valid());
    ASSERT_TRUE(secondSubscription.valid());

    const Core::GpuTimingSampleAttribution attribution = recorder.allocateSampleAttribution();
    ASSERT_TRUE(attribution.valid());
    const Core::GpuTimingSample sample{
        .scopeName = Name("tests/timing/subscription_dispatch"),
        .sourceFrameIndex = 7u,
        .durationSeconds = 0.001,
        .attribution = attribution,
        .published = true,
        .comparableRange = {},
    };
    Core::GpuTimingRecorderDiagnosticPeer::dispatchSample(recorder, sample);

    ASSERT_TRUE(replacementSubscription.valid());
    EXPECT_NE(firstSubscription, replacementSubscription);
    EXPECT_EQ(firstCapture.sampleCount, 1u);
    EXPECT_EQ(secondCapture.sampleCount, 1u);
    EXPECT_EQ(replacementCapture.sampleCount, 0u);

    Core::GpuTimingRecorderDiagnosticPeer::dispatchSample(recorder, sample);
    EXPECT_EQ(firstCapture.sampleCount, 1u);
    EXPECT_EQ(secondCapture.sampleCount, 2u);
    EXPECT_EQ(replacementCapture.sampleCount, 1u);

    recorder.unsubscribeSampleListener(firstSubscription);
    recorder.unsubscribeSampleListener(secondSubscription);
    recorder.unsubscribeSampleListener(replacementSubscription);
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

