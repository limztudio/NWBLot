// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/graphics/gpu_timing_test_names.adhd.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/gpu_timing_metric_correlator.h>
#include <core/perf/timing.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingTestsTag>;
inline constexpr Name s_GpuTimingMetricScratchArena = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingMetricScratch;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


TEST(GpuTimingOverlapRegistration, IsCanonicalIdempotentAndRejectsMetricRoleConflicts){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSecond;
    const Name thirdScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapThird;
    const Name fourthScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapFourth;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapOutput;
    const Name alternateOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapAlternateOutput;

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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleOutput;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpOutput;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleSecond;
    const Name resetFirstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleResetFirst;
    const Name resetSecondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleResetSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleOutput;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeSecond;
    const Name thirdScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeThird;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueueOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueue0InternalIdle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueue1InternalIdle;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleSecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleOverlap;
    const Name alternateOverlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleQueue1Idle;
    const Name alternateFirstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateQueue0Idle;
    const Name alternateSecondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateQueue1Idle;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureSecond;
    const Name retryFirstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureRetryFirst;
    const Name retrySecondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureRetrySecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureQueue1Idle;
    const Name alternateSecondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureAlternateQueue1Idle;
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
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationSecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationQueue1Idle;
    const Name resetFirstOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetFirstOutput;
    const Name resetSecondInputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetSecondInput;
    const Name resetSecondOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetSecondOutput;
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

