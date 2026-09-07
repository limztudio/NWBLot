// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "timing_capture_test_support.h"
#include "timing_preamble_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingGpuTimingSampleCapture{
    u32 invocationCount = 0u;


    static void invoke(void* const context, const GpuTimingSample&){
        ThrowingGpuTimingSampleCapture* const capture = static_cast<ThrowingGpuTimingSampleCapture*>(context);
        if(!capture)
            return;

        ++capture->invocationCount;
        throw 1u;
    }
};


struct ReplacingGpuTimingSampleCapture{
    GpuTimingSampleCapture capturedSamples;
    GpuTimingSampleCapture replacementSamples;
    GpuTimingRecorder& timing;
    GpuTimingSampleSubscription subscription;
    GpuTimingSampleSubscription replacementSubscription;
    bool replaceOnFirstSample = false;


    static void Invoke(void* const context, const GpuTimingSample& sample){
        ReplacingGpuTimingSampleCapture* const capture = static_cast<ReplacingGpuTimingSampleCapture*>(context);
        if(!capture)
            return;

        GpuTimingSampleCapture::Invoke(&capture->capturedSamples, sample);
        if(!capture->replaceOnFirstSample)
            return;

        capture->replaceOnFirstSample = false;
        capture->timing.unsubscribeSampleListener(capture->subscription);
        capture->replacementSubscription = capture->timing.subscribeSampleListener(GpuTimingSampleListener{
            .context = &capture->replacementSamples,
            .invoke = &GpuTimingSampleCapture::Invoke,
        });
    }


    explicit ReplacingGpuTimingSampleCapture(GpuTimingRecorder& timingRecorder)
        : timing(timingRecorder)
    {}
    ~ReplacingGpuTimingSampleCapture(){
        timing.unsubscribeSampleListener(subscription);
        timing.unsubscribeSampleListener(replacementSubscription);
    }
};


inline constexpr GpuTimingScopeDefinition s_FrameTimingLateActivationScope("tests/frame_timing_late_activation");


inline constexpr GpuTimingScopeDefinition s_FrameTimingFeedbackOnlyScope("tests/frame_timing_feedback_only");


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackDemandedScope(
    "tests/frame_timing_scoped_feedback_demanded"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackUndemandedScope(
    "tests/frame_timing_scoped_feedback_undemanded"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackPerfDemandedScope(
    "tests/frame_timing_scoped_feedback_perf_demanded"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackPerfUndemandedScope(
    "tests/frame_timing_scoped_feedback_perf_undemanded"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingFeedbackPerfLateActivationScope(
    "tests/frame_timing_feedback_perf_late_activation"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingFeedbackPerfRestartedSessionScope(
    "tests/frame_timing_feedback_perf_restarted_session"
);


// Renderer systems declare their timing capacities while resources validate, often before a project enables capture.
// The next Graphics preamble must materialize those declarations before the first dynamic-rendering scope records.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleMaterializesTimerQueriesAfterCaptureActivation){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingLateActivationScope.identity, device, 1u));
    s_scope->setGpuTimingEnabled(true);

    FrameTimingPreambleProbePass probePass(graphics, s_FrameTimingLateActivationScope);
    ASSERT_TRUE(probePass.initialize());

    graphics.addRenderPassToBack(probePass);
    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingLateActivationScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Adaptive queue feedback collects only its annotated scopes, even when broad Perf capture is disabled. Its first
// preamble must still materialize and reset the previously declared pool before a timing ticket can write it.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleMaterializesTimerQueriesForFeedbackOnlyCollection){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    timingSink.setEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingFeedbackOnlyScope.identity, device, 2u));
    ReplacingGpuTimingSampleCapture replacingSamples(timing);
    replacingSamples.replaceOnFirstSample = true;
    replacingSamples.subscription = timing.subscribeSampleListener(GpuTimingSampleListener{
        .context = &replacingSamples,
        .invoke = &ReplacingGpuTimingSampleCapture::Invoke,
    });
    GpuTimingSampleCapture observingSamples;
    ScopedGpuTimingSampleListener observingListener(timing, observingSamples);
    ASSERT_TRUE(replacingSamples.subscription.valid());
    ASSERT_TRUE(observingListener.valid());
    const GpuTimingSampleAttribution firstAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution secondAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution thirdAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(firstAttribution.valid());
    ASSERT_TRUE(secondAttribution.valid());
    ASSERT_TRUE(thirdAttribution.valid());

    ASSERT_TRUE(timing.setFeedbackCollectionScopes(
        replacingSamples.subscription,
        MakeNotNull(&s_FrameTimingFeedbackOnlyScope.identity),
        1u
    ));
    ASSERT_TRUE(observingListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingFeedbackOnlyScope.identity),
        1u
    ));
    ASSERT_TRUE(timing.clearFeedbackCollectionScopes(replacingSamples.subscription));
    ASSERT_TRUE(timing.collectionActive());
    ASSERT_TRUE(graphics.prepareFramePreamble());

    auto firstCommandList = device.createCommandList();
    ASSERT_NE(firstCommandList.get(), nullptr);
    GpuTimingSubmissionTicket firstTimingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(firstTimingTicket);

        firstCommandList->open();
        {
            GpuTimingMeasure firstTimingMeasure(
                timing,
                s_FrameTimingFeedbackOnlyScope,
                device,
                *firstCommandList,
                firstAttribution
            );
            ASSERT_TRUE(firstTimingMeasure.valid());
        }
        {
            GpuTimingMeasure secondTimingMeasure(
                timing,
                s_FrameTimingFeedbackOnlyScope,
                device,
                *firstCommandList,
                secondAttribution
            );
            ASSERT_TRUE(secondTimingMeasure.valid());
        }
        firstCommandList->close();
    }

    CommandList* firstCommandLists[] = { firstCommandList.get() };
    ASSERT_TRUE(firstTimingTicket.submit(device, firstCommandLists, LengthOf(firstCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device);
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());

    ASSERT_TRUE(replacingSamples.replacementSubscription.valid());
    ASSERT_EQ(replacingSamples.capturedSamples.sampleCount, 1u);
    EXPECT_EQ(replacingSamples.capturedSamples.samples[0u].scopeName, s_FrameTimingFeedbackOnlyScope.identity);
    EXPECT_EQ(replacingSamples.capturedSamples.samples[0u].attribution, firstAttribution);
    EXPECT_TRUE(replacingSamples.capturedSamples.samples[0u].published);
    EXPECT_GE(replacingSamples.capturedSamples.samples[0u].durationSeconds, 0.0);
    ASSERT_EQ(observingSamples.sampleCount, 2u);
    EXPECT_EQ(observingSamples.samples[0u].attribution, firstAttribution);
    EXPECT_EQ(observingSamples.samples[1u].attribution, secondAttribution);
    EXPECT_TRUE(observingSamples.samples[0u].published);
    EXPECT_TRUE(observingSamples.samples[1u].published);
    EXPECT_EQ(replacingSamples.replacementSamples.sampleCount, 0u);
    EXPECT_EQ(timing.statistics(device).sampleListenerFailureCount, 0u);
    EXPECT_FALSE(timingSink.stats(s_FrameTimingFeedbackOnlyScope.identity).valid());

    auto secondCommandList = device.createCommandList();
    ASSERT_NE(secondCommandList.get(), nullptr);
    GpuTimingSubmissionTicket secondTimingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(secondTimingTicket);

        secondCommandList->open();
        {
            GpuTimingMeasure thirdTimingMeasure(
                timing,
                s_FrameTimingFeedbackOnlyScope,
                device,
                *secondCommandList,
                thirdAttribution
            );
            ASSERT_TRUE(thirdTimingMeasure.valid());
        }
        secondCommandList->close();
    }

    CommandList* secondCommandLists[] = { secondCommandList.get() };
    ASSERT_TRUE(secondTimingTicket.submit(device, secondCommandLists, LengthOf(secondCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());

    EXPECT_EQ(replacingSamples.capturedSamples.sampleCount, 1u);
    ASSERT_EQ(observingSamples.sampleCount, 3u);
    EXPECT_EQ(observingSamples.samples[2u].scopeName, s_FrameTimingFeedbackOnlyScope.identity);
    EXPECT_EQ(observingSamples.samples[2u].attribution, thirdAttribution);
    EXPECT_TRUE(observingSamples.samples[2u].published);
    ASSERT_EQ(replacingSamples.replacementSamples.sampleCount, 1u);
    EXPECT_EQ(replacingSamples.replacementSamples.samples[0u].scopeName, s_FrameTimingFeedbackOnlyScope.identity);
    EXPECT_EQ(replacingSamples.replacementSamples.samples[0u].attribution, thirdAttribution);
    EXPECT_TRUE(replacingSamples.replacementSamples.samples[0u].published);
    EXPECT_EQ(timing.statistics(device).sampleListenerFailureCount, 0u);

    ASSERT_TRUE(observingListener.clearFeedbackCollectionScopes());
    EXPECT_FALSE(timing.collectionActive());
    timingSink.setEnabled(false);
    timing.resetQueries();
}


// A throwing listener ends collection. Check callback/query cleanup without starting another frame or dispatch.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingListenerFailureUnwindsCompletedCollectionWithoutResumingWork){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingFeedbackOnlyScope.identity, device, 2u));
    GpuTimingSampleCapture earlierSamples;
    ScopedGpuTimingSampleListener earlierListener(timing, earlierSamples);
    ThrowingGpuTimingSampleCapture throwingSamples;
    ScopedGpuTimingSampleListener throwingListener(timing, GpuTimingSampleListener{
        .context = &throwingSamples,
        .invoke = &ThrowingGpuTimingSampleCapture::invoke,
    });
    GpuTimingSampleCapture laterSamples;
    ScopedGpuTimingSampleListener laterListener(timing, laterSamples);
    ASSERT_TRUE(earlierListener.valid());
    ASSERT_TRUE(throwingListener.valid());
    ASSERT_TRUE(laterListener.valid());
    ASSERT_TRUE(throwingListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingFeedbackOnlyScope.identity), 1u
    ));
    ASSERT_TRUE(graphics.prepareFramePreamble());
    const GpuTimingSampleAttribution attributions[] = {
        timing.allocateSampleAttribution(), timing.allocateSampleAttribution(),
    };
    ASSERT_TRUE(attributions[0u].valid());
    ASSERT_TRUE(attributions[1u].valid());
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        for(const GpuTimingSampleAttribution attribution : attributions){
            GpuTimingMeasure timingMeasure(timing, s_FrameTimingFeedbackOnlyScope, device, *commandList, attribution);
            ASSERT_TRUE(timingMeasure.valid());
        }
        commandList->close();
    }
    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, LengthOf(commandLists)));
    ASSERT_TRUE(device.waitForIdle());

    EXPECT_THROW(timing.collect(device), u32);
    EXPECT_EQ(throwingSamples.invocationCount, 1u);
    ASSERT_EQ(earlierSamples.sampleCount, 1u);
    EXPECT_EQ(earlierSamples.samples[0u].attribution, attributions[0u]);
    EXPECT_TRUE(earlierSamples.samples[0u].published);
    EXPECT_EQ(laterSamples.sampleCount, 0u);
    const GpuTimingRecorderStatistics failedStatistics = timing.statistics(device);
    EXPECT_EQ(failedStatistics.acceptedScopeCount, 2u);
    EXPECT_EQ(failedStatistics.publishedSampleCount, 2u);
    EXPECT_EQ(failedStatistics.sampleListenerFailureCount, 1u);

    throwingListener.unsubscribe();
    earlierListener.unsubscribe();
    laterListener.unsubscribe();
    EXPECT_FALSE(throwingListener.valid());
    EXPECT_FALSE(timing.collectionActive());
    timing.resetQueries();
    const GpuTimingRecorderStatistics clearedStatistics = timing.statistics(device);
    EXPECT_EQ(clearedStatistics.preparedScopeCount, 0u);
    EXPECT_EQ(clearedStatistics.materializedQueryCount, 0u);
    EXPECT_EQ(throwingSamples.invocationCount, 1u);
    EXPECT_EQ(earlierSamples.sampleCount, 1u);
    EXPECT_EQ(laterSamples.sampleCount, 0u);
}

// Feedback-only collection must materialize, reset, and record only the scopes named by a live subscription. Keep
// the Perf sink enabled while broad query collection is disabled so accidental publication is independently visible.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleMaterializesAndRecordsOnlyDemandedFeedbackScopes){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    timingSink.setEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackDemandedScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackUndemandedScope.identity, device, 3u));

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    ASSERT_TRUE(sampleListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackDemandedScope.identity),
        1u
    ));
    const GpuTimingRecorderStatistics declaredStatistics = timing.statistics(device);
    EXPECT_EQ(declaredStatistics.preparedScopeCount, 2u);
    EXPECT_EQ(declaredStatistics.requestedQueryCount, 4u);
    EXPECT_EQ(declaredStatistics.materializedQueryCount, 0u);

    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());
    const GpuTimingRecorderStatistics materializedStatistics = timing.statistics(device);
    EXPECT_EQ(materializedStatistics.preparedScopeCount, 2u);
    EXPECT_EQ(materializedStatistics.requestedQueryCount, 4u);
    EXPECT_EQ(materializedStatistics.materializedQueryCount, 1u);
    timing.beginFrame(401u);

    const GpuTimingSampleAttribution demandedAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution undemandedAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(demandedAttribution.valid());
    ASSERT_TRUE(undemandedAttribution.valid());
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    bool unattributedDemandedScopeRecorded = false;
    bool demandedScopeRecorded = false;
    bool undemandedScopeRecorded = false;
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackDemandedScope,
                device,
                *commandList
            );
            unattributedDemandedScopeRecorded = timingMeasure.valid();
        }
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackDemandedScope,
                device,
                *commandList,
                demandedAttribution
            );
            demandedScopeRecorded = timingMeasure.valid();
        }
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackUndemandedScope,
                device,
                *commandList,
                undemandedAttribution
            );
            undemandedScopeRecorded = timingMeasure.valid();
        }
        commandList->close();
    }
    EXPECT_FALSE(unattributedDemandedScopeRecorded);
    EXPECT_TRUE(demandedScopeRecorded);
    EXPECT_FALSE(undemandedScopeRecorded);

    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, LengthOf(commandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 402u);

    ASSERT_EQ(completedSamples.sampleCount, 1u);
    const GpuTimingSample* const demandedSample = completedSamples.find(demandedAttribution);
    ASSERT_NE(demandedSample, nullptr);
    EXPECT_EQ(demandedSample->scopeName, s_FrameTimingScopedFeedbackDemandedScope.identity);
    EXPECT_EQ(demandedSample->sourceFrameIndex, 401u);
    EXPECT_TRUE(demandedSample->published);
    EXPECT_EQ(completedSamples.find(undemandedAttribution), nullptr);
    EXPECT_FALSE(timingSink.stats(s_FrameTimingScopedFeedbackDemandedScope.identity).valid());
    EXPECT_FALSE(timingSink.stats(s_FrameTimingScopedFeedbackUndemandedScope.identity).valid());

    const GpuTimingRecorderStatistics completedStatistics = timing.statistics(device);
    EXPECT_EQ(completedStatistics.scopeAttemptCount, 3u);
    EXPECT_EQ(completedStatistics.recordedScopeCount, 1u);
    EXPECT_EQ(completedStatistics.acceptedScopeCount, 1u);
    EXPECT_EQ(completedStatistics.publishedSampleCount, 1u);
    EXPECT_EQ(
        completedStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::CollectionInactive],
        2u
    );

    ASSERT_TRUE(sampleListener.clearFeedbackCollectionScopes());
    timingSink.setEnabled(false);
    timing.resetQueries();
}


// Broad Perf capture owns every prepared scope. Removing a narrower feedback demand must therefore neither retire its
// accepted attribution nor prevent any undemanded scope from materializing, recording, or publishing normally.
TEST_F(DescriptorBufferRoundTripTest, BroadGpuTimingCaptureOverridesScopedFeedbackDemand){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackPerfDemandedScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackPerfUndemandedScope.identity, device, 3u));

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    ASSERT_TRUE(sampleListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackPerfDemandedScope.identity),
        1u
    ));
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());

    const GpuTimingRecorderStatistics materializedStatistics = timing.statistics(device);
    EXPECT_EQ(materializedStatistics.preparedScopeCount, 2u);
    EXPECT_EQ(materializedStatistics.requestedQueryCount, 4u);
    EXPECT_EQ(materializedStatistics.materializedQueryCount, 4u);
    timing.beginFrame(403u);

    const GpuTimingSampleAttribution demandedAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution undemandedAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(demandedAttribution.valid());
    ASSERT_TRUE(undemandedAttribution.valid());
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    bool demandedScopeRecorded = false;
    bool undemandedScopeRecorded = false;
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackPerfDemandedScope,
                device,
                *commandList,
                demandedAttribution
            );
            demandedScopeRecorded = timingMeasure.valid();
        }
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackPerfUndemandedScope,
                device,
                *commandList,
                undemandedAttribution
            );
            undemandedScopeRecorded = timingMeasure.valid();
        }
        commandList->close();
    }
    ASSERT_TRUE(demandedScopeRecorded);
    ASSERT_TRUE(undemandedScopeRecorded);

    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, LengthOf(commandLists)));
    ASSERT_TRUE(sampleListener.clearFeedbackCollectionScopes());
    EXPECT_EQ(completedSamples.sampleCount, 0u);
    EXPECT_TRUE(timing.collectionActive());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 404u);

    ASSERT_EQ(completedSamples.sampleCount, 2u);
    const GpuTimingSample* const demandedSample = completedSamples.find(demandedAttribution);
    const GpuTimingSample* const undemandedSample = completedSamples.find(undemandedAttribution);
    ASSERT_NE(demandedSample, nullptr);
    ASSERT_NE(undemandedSample, nullptr);
    EXPECT_EQ(demandedSample->scopeName, s_FrameTimingScopedFeedbackPerfDemandedScope.identity);
    EXPECT_EQ(undemandedSample->scopeName, s_FrameTimingScopedFeedbackPerfUndemandedScope.identity);
    EXPECT_TRUE(demandedSample->published);
    EXPECT_TRUE(undemandedSample->published);
    const Perf::TimingStats& demandedStats = timingSink.stats(s_FrameTimingScopedFeedbackPerfDemandedScope.identity);
    const Perf::TimingStats& undemandedStats = timingSink.stats(
        s_FrameTimingScopedFeedbackPerfUndemandedScope.identity
    );
    ASSERT_TRUE(demandedStats.valid());
    ASSERT_TRUE(undemandedStats.valid());
    EXPECT_EQ(demandedStats.firstSampleFrameIndex, 403u);
    EXPECT_EQ(demandedStats.lastSampleFrameIndex, 403u);
    EXPECT_EQ(demandedStats.publishFrameIndex, 404u);
    EXPECT_EQ(undemandedStats.firstSampleFrameIndex, 403u);
    EXPECT_EQ(undemandedStats.lastSampleFrameIndex, 403u);
    EXPECT_EQ(undemandedStats.publishFrameIndex, 404u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Listener publication follows feedback demand, but Perf publication belongs to the capture session active when a
// query begins. Late activation and a complete off/on restart must not import an older query into the new session.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingPerformanceCaptureEpochExcludesOlderFeedbackQueries){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingFeedbackPerfLateActivationScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingFeedbackPerfRestartedSessionScope.identity, device, 1u));

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const Name feedbackScopes[] = {
        s_FrameTimingFeedbackPerfLateActivationScope.identity,
        s_FrameTimingFeedbackPerfRestartedSessionScope.identity,
    };
    ASSERT_TRUE(sampleListener.setFeedbackCollectionScopes(MakeNotNull(&feedbackScopes[0u]), LengthOf(feedbackScopes)));
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());
    timing.beginFrame(409u);

    const GpuTimingSampleAttribution lateActivationAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(lateActivationAttribution.valid());
    auto lateActivationCommandList = device.createCommandList();
    ASSERT_NE(lateActivationCommandList.get(), nullptr);
    GpuTimingSubmissionTicket lateActivationTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(lateActivationTicket);

        lateActivationCommandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingFeedbackPerfLateActivationScope,
                device,
                *lateActivationCommandList,
                lateActivationAttribution
            );
            ASSERT_TRUE(timingMeasure.valid());
        }
        lateActivationCommandList->close();
    }
    CommandList* lateActivationCommandLists[] = { lateActivationCommandList.get() };
    ASSERT_TRUE(lateActivationTicket.submit(
        device,
        lateActivationCommandLists,
        LengthOf(lateActivationCommandLists)
    ));

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 410u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    const GpuTimingSample* const lateActivationSample = completedSamples.find(lateActivationAttribution);
    ASSERT_NE(lateActivationSample, nullptr);
    EXPECT_EQ(lateActivationSample->scopeName, s_FrameTimingFeedbackPerfLateActivationScope.identity);
    EXPECT_EQ(lateActivationSample->sourceFrameIndex, 409u);
    EXPECT_TRUE(lateActivationSample->published);
    EXPECT_FALSE(timingSink.stats(s_FrameTimingFeedbackPerfLateActivationScope.identity).valid());

    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());
    timing.beginFrame(411u);
    const GpuTimingSampleAttribution restartedSessionAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(restartedSessionAttribution.valid());
    auto restartedSessionCommandList = device.createCommandList();
    ASSERT_NE(restartedSessionCommandList.get(), nullptr);
    GpuTimingSubmissionTicket restartedSessionTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(restartedSessionTicket);

        restartedSessionCommandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingFeedbackPerfRestartedSessionScope,
                device,
                *restartedSessionCommandList,
                restartedSessionAttribution
            );
            ASSERT_TRUE(timingMeasure.valid());
        }
        restartedSessionCommandList->close();
    }
    CommandList* restartedSessionCommandLists[] = { restartedSessionCommandList.get() };
    ASSERT_TRUE(restartedSessionTicket.submit(
        device,
        restartedSessionCommandLists,
        LengthOf(restartedSessionCommandLists)
    ));

    s_scope->setGpuTimingEnabled(false);
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 412u);
    ASSERT_EQ(completedSamples.sampleCount, 2u);
    const GpuTimingSample* const restartedSessionSample = completedSamples.find(restartedSessionAttribution);
    ASSERT_NE(restartedSessionSample, nullptr);
    EXPECT_EQ(restartedSessionSample->scopeName, s_FrameTimingFeedbackPerfRestartedSessionScope.identity);
    EXPECT_EQ(restartedSessionSample->sourceFrameIndex, 411u);
    EXPECT_TRUE(restartedSessionSample->published);
    EXPECT_FALSE(timingSink.stats(s_FrameTimingFeedbackPerfRestartedSessionScope.identity).valid());

    ASSERT_TRUE(sampleListener.clearFeedbackCollectionScopes());
    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

