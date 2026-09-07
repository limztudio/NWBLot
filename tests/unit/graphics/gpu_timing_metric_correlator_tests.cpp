// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/gpu_timing_metric_correlator.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metric_correlator_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingMetricCorrelatorTestsTag>;
inline constexpr Name s_MetricScratchArena("tests/timing/metric_correlator_scratch");


struct RegistrationSentinel{};

class ThrowOnceTimingSink final : public Core::Perf::TimingSink, NoCopy{
public:
    explicit ThrowOnceTimingSink(Core::Perf::TimingRecorder& timing)
        : m_timing(timing)
    {}


public:
    [[nodiscard]] virtual bool enabled()const noexcept override{ return true; }
    [[nodiscard]] virtual Core::Perf::TimingScopeId registerScope(const Name& scopeName)override{
        ++m_registrationAttempt;
        if(m_registrationAttempt == 3u)
            throw RegistrationSentinel{};
        return m_timing.registerScope(scopeName);
    }
    virtual void recordSample(
        const Core::Perf::TimingScopeId scope,
        const f64 seconds,
        const u64 sampleFrameIndex)override{
        m_timing.recordSample(scope, seconds, sampleFrameIndex);
    }
    virtual void publishFrame(const u64 publishFrameIndex)override{ m_timing.publishFrame(publishFrameIndex); }


private:
    Core::Perf::TimingRecorder& m_timing;
    usize m_registrationAttempt = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingPacketEnvelopeMetrics, CorrelatesLargeOutOfOrderFrame){
    constexpr usize s_ScopeCount = 2048u;
    constexpr usize s_PairCount = s_ScopeCount / 2u;
    constexpr u64 s_FrameIndex = 91u;
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> scopes{ scratchArena };
    scopes.reserve(s_ScopeCount);
    for(usize scopeIndex = 0u; scopeIndex < s_ScopeCount; ++scopeIndex){
        char scopeIndexBuffer[32u] = {};
        scopes.push_back(Core::GpuPacketEnvelopeMetricScope{
            .scopeName = DeriveName(Name("tests/timing/large_envelope/"), FormatDecimal(scopeIndex, scopeIndexBuffer)),
            .physicalQueue = { .index = static_cast<u16>(scopeIndex % 2u), .deviceGeneration = 7u },
        });
    }
    const Name overlapScope("tests/timing/large_envelope_overlap");
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = { .index = 0u, .deviceGeneration = 7u }, .internalIdleScopeName = Name("tests/timing/large_idle_0") },
        { .physicalQueue = { .index = 1u, .deviceGeneration = 7u }, .internalIdleScopeName = Name("tests/timing/large_idle_1") },
    };
    const Core::GpuPacketEnvelopeMetricScope* const scopeData = scopes.data();
    const Timer prepareBegin = TimerNow();
    const bool prepared = correlator.preparePacketEnvelopeMetrics(
        s_FrameIndex,
        MakeNotNull(scopeData),
        scopes.size(),
        overlapScope,
        MakeNotNull(&outputs[0u]),
        LengthOf(outputs)
    );
    const u64 prepareNanoseconds = DurationInNS<u64>(TimerNow(), prepareBegin);
    ASSERT_TRUE(prepared);

    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(1u + LengthOf(outputs));
    bool completedEarly = false;
    const Timer recordBegin = TimerNow();
    for(usize arrivalIndex = 0u; arrivalIndex < s_ScopeCount; ++arrivalIndex){
        // An odd stride permutes this power-of-two input count and separates arrival order from queue order.
        const usize scopeIndex = (arrivalIndex * 773u) % s_ScopeCount;
        const Core::GpuPacketEnvelopeMetricScope& scope = scopes[scopeIndex];
        const u64 beginTicks = static_cast<u64>(scopeIndex / 2u) * 20u + static_cast<u64>(scopeIndex % 2u) * 5u;
        correlator.recordTimestampRange(
            scope.scopeName,
            s_FrameIndex,
            Core::GpuComparableTimestampRange{
                .beginTicks = beginTicks,
                .endTicks = beginTicks + 10u,
                .secondsPerTick = 0.25,
                .physicalQueue = scope.physicalQueue,
            },
            samples,
            scratchArena
        );
        if(arrivalIndex + 1u < s_ScopeCount)
            completedEarly = completedEarly || !samples.empty();
    }
    const u64 recordNanoseconds = DurationInNS<u64>(TimerNow(), recordBegin);
    char durationText[32u] = {};
    RecordProperty("correlator_prepare_ns", FormatDecimal(prepareNanoseconds, durationText).data());
    RecordProperty("correlator_record_ns", FormatDecimal(recordNanoseconds, durationText).data());

    EXPECT_FALSE(completedEarly);
    ASSERT_EQ(samples.size(), 1u + LengthOf(outputs));
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(s_FrameIndex + 1u);

    const Core::Perf::TimingStats& overlap = timing.stats(overlapScope);
    ASSERT_EQ(overlap.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(overlap.seconds, static_cast<f64>(s_PairCount * 5u) * 0.25);
    EXPECT_EQ(overlap.firstSampleFrameIndex, s_FrameIndex);
    for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : outputs){
        const Core::Perf::TimingStats& idle = timing.stats(output.internalIdleScopeName);
        ASSERT_EQ(idle.sampleCount, 1u);
        EXPECT_DOUBLE_EQ(idle.seconds, static_cast<f64>((s_PairCount - 1u) * 10u) * 0.25);
        EXPECT_EQ(idle.firstSampleFrameIndex, s_FrameIndex);
    }
}

TEST(GpuTimingPacketEnvelopeMetrics, ReplacesDuplicateRangesWithoutCompletingMissingScopes){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Name overlapScope("tests/timing/duplicate_envelope_overlap");
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = Name("tests/timing/duplicate_envelope_first"), .physicalQueue = firstQueue },
        { .scopeName = Name("tests/timing/duplicate_envelope_second"), .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = secondQueue, .internalIdleScopeName = Name("tests/timing/duplicate_idle_1") },
        { .physicalQueue = firstQueue, .internalIdleScopeName = Name("tests/timing/duplicate_idle_0") },
    };
    const auto prepare = [&](){
        return correlator.preparePacketEnvelopeMetrics(
            73u,
            MakeNotNull(&scopes[0u]),
            LengthOf(scopes),
            overlapScope,
            MakeNotNull(&outputs[0u]),
            LengthOf(outputs)
        );
    };
    ASSERT_TRUE(prepare());
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(1u + LengthOf(outputs));
    Core::GpuComparableTimestampRange range{
        .beginTicks = 0u,
        .endTicks = 100u,
        .secondsPerTick = 0.5,
        .physicalQueue = firstQueue,
    };
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, range, samples, scratchArena);
    ASSERT_TRUE(samples.empty());
    ASSERT_TRUE(prepare());
    range.beginTicks = 10u;
    range.endTicks = 20u;
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, range, samples, scratchArena);
    ASSERT_TRUE(samples.empty());

    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, samples, scratchArena);
    correlator.recordTimestampRange(Name("tests/timing/unrelated_envelope"), 73u, range, samples, scratchArena);
    ASSERT_TRUE(samples.empty());
    range.beginTicks = 15u;
    range.endTicks = 25u;
    range.physicalQueue = secondQueue;
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, samples, scratchArena);
    ASSERT_EQ(samples.size(), 3u);
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(74u);
    EXPECT_DOUBLE_EQ(timing.stats(overlapScope).seconds, 2.5);
    EXPECT_DOUBLE_EQ(timing.stats(outputs[0u].internalIdleScopeName).seconds, 0.0);
    EXPECT_DOUBLE_EQ(timing.stats(outputs[1u].internalIdleScopeName).seconds, 0.0);

    samples.clear();
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, samples, scratchArena);
    EXPECT_TRUE(samples.empty());
}

TEST(GpuTimingPacketEnvelopeMetrics, PreservesPartialFramesAcrossGrowthRejectedPreparesAndOutOfOrderRetirement){
    constexpr usize s_FrameCount = 8u;
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Name overlapScope("tests/timing/growing_envelope_overlap");
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = Name("tests/timing/growing_envelope_first"), .physicalQueue = firstQueue },
        { .scopeName = Name("tests/timing/growing_envelope_second"), .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricScope reversedScopes[] = { scopes[1u], scopes[0u] };
    const Core::GpuPacketEnvelopeMetricScope duplicateScopes[] = {
        scopes[0u],
        { .scopeName = scopes[0u].scopeName, .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = Name("tests/timing/growing_idle_0") },
        { .physicalQueue = secondQueue, .internalIdleScopeName = Name("tests/timing/growing_idle_1") },
    };
    const auto prepare = [&](const u64 frameIndex, const Core::GpuPacketEnvelopeMetricScope* const scopeInputs){
        return correlator.preparePacketEnvelopeMetrics(
            frameIndex,
            MakeNotNull(scopeInputs),
            LengthOf(scopes),
            overlapScope,
            MakeNotNull(&outputs[0u]),
            LengthOf(outputs)
        );
    };
    EXPECT_FALSE(prepare(100u, duplicateScopes));
    EXPECT_FALSE(correlator.hasOutputRole(overlapScope));
    const Core::GpuComparableTimestampRange firstRange{
        .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = firstQueue,
    };
    const Core::GpuComparableTimestampRange secondRange{
        .beginTicks = 5u, .endTicks = 15u, .secondsPerTick = 0.5, .physicalQueue = secondQueue,
    };
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(3u);
    for(usize frameOffset = 0u; frameOffset < s_FrameCount; ++frameOffset){
        const u64 frameIndex = 100u + frameOffset;
        const bool reverseOrder = frameOffset % 2u != 0u;
        ASSERT_TRUE(prepare(frameIndex, reverseOrder ? reversedScopes : scopes));
        correlator.recordTimestampRange(scopes[0u].scopeName, frameIndex, firstRange, samples, scratchArena);
        ASSERT_TRUE(samples.empty());
        EXPECT_FALSE(prepare(frameIndex, duplicateScopes));
        EXPECT_FALSE(prepare(frameIndex, reverseOrder ? scopes : reversedScopes));
        ASSERT_TRUE(prepare(frameIndex, reverseOrder ? reversedScopes : scopes));
    }
    for(usize arrivalIndex = 0u; arrivalIndex < s_FrameCount; ++arrivalIndex){
        const u64 frameIndex = 100u + (arrivalIndex * 5u) % s_FrameCount;
        correlator.recordTimestampRange(scopes[0u].scopeName, frameIndex, firstRange, samples, scratchArena);
        ASSERT_TRUE(samples.empty());
        correlator.recordTimestampRange(scopes[1u].scopeName, frameIndex, secondRange, samples, scratchArena);
        ASSERT_EQ(samples.size(), 3u);
        EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 2.5);
        EXPECT_DOUBLE_EQ(samples[1u].durationSeconds, 0.0);
        EXPECT_DOUBLE_EQ(samples[2u].durationSeconds, 0.0);
        for(const Core::GpuTimingSinkSample& sample : samples)
            EXPECT_EQ(sample.sourceFrameIndex, frameIndex);
        samples.clear();
    }
}

TEST(GpuTimingPacketEnvelopeMetrics, PublishesNoPartialFrameWhenOutputRegistrationThrows){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    ThrowOnceTimingSink sink(timing);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, sink);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 13u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 13u };
    const Name overlapScope("tests/timing/throwing_envelope_overlap");
    const Core::GpuPacketEnvelopeMetricScope scopes[] = {
        { .scopeName = Name("tests/timing/throwing_envelope_first"), .physicalQueue = firstQueue },
        { .scopeName = Name("tests/timing/throwing_envelope_second"), .physicalQueue = secondQueue },
    };
    const Core::GpuPacketEnvelopeMetricQueueOutput outputs[] = {
        { .physicalQueue = firstQueue, .internalIdleScopeName = Name("tests/timing/throwing_idle_0") },
        { .physicalQueue = secondQueue, .internalIdleScopeName = Name("tests/timing/throwing_idle_1") },
    };
    const auto prepare = [&](){
        return correlator.preparePacketEnvelopeMetrics(
            73u,
            MakeNotNull(&scopes[0u]),
            LengthOf(scopes),
            overlapScope,
            MakeNotNull(&outputs[0u]),
            LengthOf(outputs)
        );
    };
    EXPECT_THROW(EXPECT_TRUE(prepare()), RegistrationSentinel);
    EXPECT_TRUE(correlator.hasOutputRole(overlapScope));
    EXPECT_TRUE(correlator.hasOutputRole(outputs[0u].internalIdleScopeName));
    EXPECT_FALSE(correlator.hasOutputRole(outputs[1u].internalIdleScopeName));
    ASSERT_TRUE(prepare());

    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(3u);
    const Core::GpuComparableTimestampRange firstRange{
        .beginTicks = 2u, .endTicks = 8u, .secondsPerTick = 0.5, .physicalQueue = firstQueue,
    };
    const Core::GpuComparableTimestampRange secondRange{
        .beginTicks = 4u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = secondQueue,
    };
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, firstRange, samples, scratchArena);
    EXPECT_TRUE(samples.empty());
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, secondRange, samples, scratchArena);
    ASSERT_EQ(samples.size(), 3u);
    EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 2.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

