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


struct BlockingGpuTimingSampleCapture{
    GpuTimingSample samples[2u] = {};
    AtomicFlag firstCallbackEntered;
    AtomicFlag releaseFirstCallback;
    u32 sampleCount = 0u;


    static void Invoke(void* const context, const GpuTimingSample& sample){
        BlockingGpuTimingSampleCapture* const capture = static_cast<BlockingGpuTimingSampleCapture*>(context);
        if(!capture || capture->sampleCount >= LengthOf(capture->samples))
            return;

        capture->samples[capture->sampleCount] = sample;
        ++capture->sampleCount;
        if(capture->sampleCount != 1u)
            return;

        capture->firstCallbackEntered.test_and_set(MemoryOrder::release);
        capture->firstCallbackEntered.notify_all();
        while(!capture->releaseFirstCallback.test(MemoryOrder::acquire))
            capture->releaseFirstCallback.wait(false, MemoryOrder::acquire);
    }
};


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackIsolationFirstScope(
    "tests/frame_timing_scoped_feedback_isolation_first"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackIsolationSecondScope(
    "tests/frame_timing_scoped_feedback_isolation_second"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackRetirementScope(
    "tests/frame_timing_scoped_feedback_retirement"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingScopedFeedbackKeepaliveScope(
    "tests/frame_timing_scoped_feedback_keepalive"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingRetirementDiscardScope("tests/frame_timing_retirement_discard");


inline constexpr GpuTimingScopeDefinition s_FrameTimingQuarantineScope("tests/frame_timing_quarantine");


inline constexpr GpuTimingScopeDefinition s_FrameTimingRejectedGraphResetScope("tests/frame_timing_rejected_graph_reset");


// Replacing or clearing one registration's scope set must preserve every overlapping or disjoint demand owned by a
// second registration. Unsubscription applies the same ownership rule and removes only that registration's scopes.
TEST_F(DescriptorBufferRoundTripTest, ScopedFeedbackClearAndUnsubscribeRemainIsolated){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackIsolationFirstScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackIsolationSecondScope.identity, device, 1u));

    GpuTimingSampleCapture firstSamples;
    GpuTimingSampleCapture secondSamples;
    ScopedGpuTimingSampleListener firstListener(timing, firstSamples);
    ScopedGpuTimingSampleListener secondListener(timing, secondSamples);
    ASSERT_TRUE(firstListener.valid());
    ASSERT_TRUE(secondListener.valid());
    const Name secondScopes[] = {
        s_FrameTimingScopedFeedbackIsolationFirstScope.identity,
        s_FrameTimingScopedFeedbackIsolationSecondScope.identity,
    };
    ASSERT_TRUE(firstListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackIsolationFirstScope.identity),
        1u
    ));
    ASSERT_TRUE(secondListener.setFeedbackCollectionScopes(MakeNotNull(&secondScopes[0u]), LengthOf(secondScopes)));
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());
    const GpuTimingSampleAttribution sharedFirstAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution sharedSecondAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution isolatedFirstAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution isolatedSecondAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(sharedFirstAttribution.valid());
    ASSERT_TRUE(sharedSecondAttribution.valid());
    ASSERT_TRUE(isolatedFirstAttribution.valid());
    ASSERT_TRUE(isolatedSecondAttribution.valid());

    ASSERT_TRUE(firstListener.clearFeedbackCollectionScopes());
    EXPECT_TRUE(timing.collectionActive());
    auto sharedCommandList = device.createCommandList();
    ASSERT_NE(sharedCommandList.get(), nullptr);
    bool sharedFirstRecorded = false;
    bool sharedSecondRecorded = false;
    GpuTimingSubmissionTicket sharedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(sharedTicket);

        sharedCommandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackIsolationFirstScope,
                device,
                *sharedCommandList,
                sharedFirstAttribution
            );
            sharedFirstRecorded = timingMeasure.valid();
        }
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackIsolationSecondScope,
                device,
                *sharedCommandList,
                sharedSecondAttribution
            );
            sharedSecondRecorded = timingMeasure.valid();
        }
        sharedCommandList->close();
    }
    EXPECT_TRUE(sharedFirstRecorded);
    EXPECT_TRUE(sharedSecondRecorded);
    sharedTicket.discard();

    ASSERT_TRUE(firstListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackIsolationFirstScope.identity),
        1u
    ));
    secondListener.unsubscribe();
    EXPECT_TRUE(timing.collectionActive());
    auto isolatedCommandList = device.createCommandList();
    ASSERT_NE(isolatedCommandList.get(), nullptr);
    bool isolatedFirstRecorded = false;
    bool isolatedSecondRecorded = false;
    GpuTimingSubmissionTicket isolatedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(isolatedTicket);

        isolatedCommandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackIsolationFirstScope,
                device,
                *isolatedCommandList,
                isolatedFirstAttribution
            );
            isolatedFirstRecorded = timingMeasure.valid();
        }
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackIsolationSecondScope,
                device,
                *isolatedCommandList,
                isolatedSecondAttribution
            );
            isolatedSecondRecorded = timingMeasure.valid();
        }
        isolatedCommandList->close();
    }
    EXPECT_TRUE(isolatedFirstRecorded);
    EXPECT_FALSE(isolatedSecondRecorded);
    isolatedTicket.discard();

    ASSERT_TRUE(firstListener.clearFeedbackCollectionScopes());
    EXPECT_FALSE(timing.collectionActive());
    EXPECT_EQ(firstSamples.sampleCount, 0u);
    EXPECT_EQ(secondSamples.sampleCount, 0u);
    timing.resetQueries();
}


// Removing the last feedback owner of one accepted scope defers its terminal attribution to ordinary collection while
// another demanded scope keeps collection globally active. Completion must still drain that accumulator for reuse.
TEST_F(DescriptorBufferRoundTripTest, ScopedFeedbackUnsubscribeRetiresAcceptedScopeWhileOtherDemandRemains){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackRetirementScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingScopedFeedbackKeepaliveScope.identity, device, 1u));

    GpuTimingSampleCapture removedOwnerSamples;
    GpuTimingSampleCapture survivingSamples;
    ScopedGpuTimingSampleListener removedOwner(timing, removedOwnerSamples);
    ScopedGpuTimingSampleListener survivingListener(timing, survivingSamples);
    ASSERT_TRUE(removedOwner.valid());
    ASSERT_TRUE(survivingListener.valid());
    ASSERT_TRUE(removedOwner.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackRetirementScope.identity),
        1u
    ));
    ASSERT_TRUE(survivingListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackKeepaliveScope.identity),
        1u
    ));
    ASSERT_TRUE(graphics.prepareFramePreamble());
    ASSERT_TRUE(device.waitForIdle());
    timing.beginFrame(405u);

    const GpuTimingSampleAttribution retiredAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(retiredAttribution.valid());
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    const GpuPhysicalQueueId physicalQueue = commandList->getResolvedDescription().physicalQueue;
    ASSERT_TRUE(physicalQueue.valid());
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackRetirementScope,
                device,
                *commandList,
                retiredAttribution
            );
            ASSERT_TRUE(timingMeasure.valid());
        }
        commandList->close();
    }
    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, LengthOf(commandLists)));

    removedOwner.unsubscribe();
    EXPECT_TRUE(timing.collectionActive());
    EXPECT_EQ(removedOwnerSamples.sampleCount, 0u);
    EXPECT_EQ(survivingSamples.sampleCount, 0u);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 406u);
    ASSERT_EQ(survivingSamples.sampleCount, 1u);
    const GpuTimingSample* const retiredSample = survivingSamples.find(retiredAttribution);
    ASSERT_NE(retiredSample, nullptr);
    EXPECT_EQ(retiredSample->scopeName, s_FrameTimingScopedFeedbackRetirementScope.identity);
    EXPECT_EQ(retiredSample->sourceFrameIndex, 405u);
    EXPECT_EQ(retiredSample->physicalQueue, physicalQueue);
    EXPECT_FALSE(retiredSample->published);

    EXPECT_EQ(removedOwnerSamples.sampleCount, 0u);
    EXPECT_EQ(survivingSamples.sampleCount, 1u);
    const GpuTimingRecorderStatistics retiredStatistics = timing.statistics(device);
    EXPECT_EQ(retiredStatistics.publishedSampleCount, 0u);
    EXPECT_EQ(retiredStatistics.unpublishedSampleCount, 1u);

    GpuTimingSampleCapture replacementSamples;
    ScopedGpuTimingSampleListener replacementListener(timing, replacementSamples);
    ASSERT_TRUE(replacementListener.valid());
    ASSERT_TRUE(replacementListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingScopedFeedbackRetirementScope.identity),
        1u
    ));
    timing.beginFrame(407u);
    const GpuTimingSampleAttribution replacementAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(replacementAttribution.valid());
    auto replacementCommandList = device.createCommandList();
    ASSERT_NE(replacementCommandList.get(), nullptr);
    GpuTimingSubmissionTicket replacementTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(replacementTicket);

        replacementCommandList->open();
        {
            GpuTimingMeasure timingMeasure(
                timing,
                s_FrameTimingScopedFeedbackRetirementScope,
                device,
                *replacementCommandList,
                replacementAttribution
            );
            ASSERT_TRUE(timingMeasure.valid());
        }
        replacementCommandList->close();
    }
    CommandList* replacementCommandLists[] = { replacementCommandList.get() };
    ASSERT_TRUE(replacementTicket.submit(device, replacementCommandLists, LengthOf(replacementCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 408u);

    EXPECT_EQ(removedOwnerSamples.sampleCount, 0u);
    ASSERT_EQ(survivingSamples.sampleCount, 2u);
    ASSERT_EQ(replacementSamples.sampleCount, 1u);
    const GpuTimingSample* const survivingReplacementSample = survivingSamples.find(replacementAttribution);
    const GpuTimingSample* const replacementSample = replacementSamples.find(replacementAttribution);
    ASSERT_NE(survivingReplacementSample, nullptr);
    ASSERT_NE(replacementSample, nullptr);
    EXPECT_TRUE(survivingReplacementSample->published);
    EXPECT_TRUE(replacementSample->published);

    ASSERT_TRUE(replacementListener.clearFeedbackCollectionScopes());
    ASSERT_TRUE(survivingListener.clearFeedbackCollectionScopes());
    EXPECT_FALSE(timing.collectionActive());
    timing.resetQueries();
}


// Ordinary collection streams deactivation retirements without retaining the recorder lock. Discarding the still-
// unaccepted command packet while the first callback is blocked preserves every later tombstoned notification.
TEST_F(DescriptorBufferRoundTripTest, TimingRetirementSurvivesConcurrentUnacceptedPacketDiscard){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    timing.beginFrame(91u);

    BlockingGpuTimingSampleCapture observingSamples;
    ScopedGpuTimingSampleListener observingListener(timing, GpuTimingSampleListener{
        .context = &observingSamples,
        .invoke = &BlockingGpuTimingSampleCapture::Invoke,
    });
    GpuTimingSampleCapture ownerSamples;
    ScopedGpuTimingSampleListener ownerListener(timing, ownerSamples);
    GpuTimingSampleCapture secondarySamples;
    ScopedGpuTimingSampleListener secondaryListener(timing, secondarySamples);
    ASSERT_TRUE(observingListener.valid());
    ASSERT_TRUE(ownerListener.valid());
    ASSERT_TRUE(secondaryListener.valid());
    ASSERT_TRUE(ownerListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingRetirementDiscardScope.identity),
        1u
    ));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingRetirementDiscardScope.identity, device, 2u));

    const GpuTimingSampleAttribution firstAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution secondAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(firstAttribution.valid());
    ASSERT_TRUE(secondAttribution.valid());

    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    const GpuPhysicalQueueId physicalQueue = commandList->getResolvedDescription().physicalQueue;
    ASSERT_TRUE(physicalQueue.valid());
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        {
            GpuTimingMeasure firstTimingMeasure(
                timing,
                s_FrameTimingRetirementDiscardScope,
                device,
                *commandList,
                firstAttribution
            );
            ASSERT_TRUE(firstTimingMeasure.valid());
        }
        {
            GpuTimingMeasure secondTimingMeasure(
                timing,
                s_FrameTimingRetirementDiscardScope,
                device,
                *commandList,
                secondAttribution
            );
            ASSERT_TRUE(secondTimingMeasure.valid());
        }
        commandList->close();
    }

    ownerListener.unsubscribe();
    Thread collectionThread([&](){ timing.collect(device, 92u); });
    const Timer callbackWaitBegin = TimerNow();
    while(
        !observingSamples.firstCallbackEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), callbackWaitBegin) < 5.0
    )
        YieldThread();
    const bool callbackEntered = observingSamples.firstCallbackEntered.test(MemoryOrder::acquire);
    if(!callbackEntered){
        observingSamples.releaseFirstCallback.test_and_set(MemoryOrder::release);
        observingSamples.releaseFirstCallback.notify_all();
        timingTicket.discard();
        collectionThread.join();
        EXPECT_TRUE(callbackEntered);
        timing.resetQueries();
        return;
    }

    AtomicFlag observingUnsubscribeStarted;
    AtomicFlag observingUnsubscribeReturned;
    Thread observingUnsubscribeThread([&](){
        observingUnsubscribeStarted.test_and_set(MemoryOrder::release);
        observingUnsubscribeStarted.notify_all();
        observingListener.unsubscribe();
        observingUnsubscribeReturned.test_and_set(MemoryOrder::release);
        observingUnsubscribeReturned.notify_all();
    });
    while(!observingUnsubscribeStarted.test(MemoryOrder::acquire))
        observingUnsubscribeStarted.wait(false, MemoryOrder::acquire);
    const Timer unsubscribeWaitBegin = TimerNow();
    while(
        !observingUnsubscribeReturned.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), unsubscribeWaitBegin) < 0.05
    )
        YieldThread();
    EXPECT_FALSE(observingUnsubscribeReturned.test(MemoryOrder::acquire));

    timingTicket.discard();
    EXPECT_FALSE(observingUnsubscribeReturned.test(MemoryOrder::acquire));
    observingSamples.releaseFirstCallback.test_and_set(MemoryOrder::release);
    observingSamples.releaseFirstCallback.notify_all();
    collectionThread.join();
    observingUnsubscribeThread.join();

    EXPECT_TRUE(observingUnsubscribeReturned.test(MemoryOrder::acquire));
    ASSERT_EQ(observingSamples.sampleCount, 1u);
    EXPECT_EQ(observingSamples.samples[0u].scopeName, s_FrameTimingRetirementDiscardScope.identity);
    EXPECT_EQ(observingSamples.samples[0u].sourceFrameIndex, 91u);
    EXPECT_EQ(observingSamples.samples[0u].physicalQueue, physicalQueue);
    EXPECT_EQ(observingSamples.samples[0u].attribution, firstAttribution);
    EXPECT_FALSE(observingSamples.samples[0u].published);
    ASSERT_EQ(secondarySamples.sampleCount, 2u);
    EXPECT_EQ(secondarySamples.samples[0u].scopeName, s_FrameTimingRetirementDiscardScope.identity);
    EXPECT_EQ(secondarySamples.samples[0u].sourceFrameIndex, 91u);
    EXPECT_EQ(secondarySamples.samples[0u].physicalQueue, physicalQueue);
    EXPECT_EQ(secondarySamples.samples[0u].attribution, firstAttribution);
    EXPECT_FALSE(secondarySamples.samples[0u].published);
    EXPECT_EQ(secondarySamples.samples[1u].scopeName, s_FrameTimingRetirementDiscardScope.identity);
    EXPECT_EQ(secondarySamples.samples[1u].sourceFrameIndex, 91u);
    EXPECT_EQ(secondarySamples.samples[1u].physicalQueue, physicalQueue);
    EXPECT_EQ(secondarySamples.samples[1u].attribution, secondAttribution);
    EXPECT_FALSE(secondarySamples.samples[1u].published);
    EXPECT_EQ(ownerSamples.sampleCount, 0u);
    EXPECT_FALSE(timing.collectionActive());
    timing.resetQueries();
}


// Invalid accepted ownership transitions permanently quarantine their native query, but their higher-level
// attribution must still receive one terminal unpublished notification on the next ordinary collection pass.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingQuarantineRetiresAttributedMalformedTransition){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    timing.beginFrame(93u);

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    ASSERT_TRUE(sampleListener.setFeedbackCollectionScopes(
        MakeNotNull(&s_FrameTimingQuarantineScope.identity),
        1u
    ));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingQuarantineScope.identity, device, 1u));

    const GpuTimingSampleAttribution attribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(attribution.valid());
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    const GpuPhysicalQueueId physicalQueue = commandList->getResolvedDescription().physicalQueue;
    ASSERT_TRUE(physicalQueue.valid());

    GpuTimingSubmissionTicket timingTicket(timing);
    GpuTimingFrameTransaction transaction(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

        commandList->open();
        ASSERT_TRUE(transaction.begin(
            s_FrameTimingQuarantineScope,
            device,
            *commandList,
            attribution
        ));
        commandList->close();
    }

    EXPECT_FALSE(transaction.confirmBeginSubmission(QueueSubmissionToken{}));
    timing.collect(device, 94u);

    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_FrameTimingQuarantineScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].sourceFrameIndex, 93u);
    EXPECT_EQ(completedSamples.samples[0u].physicalQueue, physicalQueue);
    EXPECT_EQ(completedSamples.samples[0u].attribution, attribution);
    EXPECT_FALSE(completedSamples.samples[0u].published);
    EXPECT_EQ(timing.statistics(device).quarantinedScopeCount, 1u);

    timingTicket.discard();
    ASSERT_TRUE(sampleListener.clearFeedbackCollectionScopes());
    timing.resetQueries();
}


// The graph-owned reset task must publish query-pool availability only from packet acceptance. A rejected reset is
// followed by a dynamic-rendering timing scope, which cannot reset the pool itself; the next accepted preamble must
// still establish a fresh usable reset rather than leaving either stale availability or a permanently stuck pool.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleRollsBackRejectedGraphTimingReset){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingRejectedGraphResetScope.identity, device, 1u));

    FrameTimingPreambleProbePass rejectedProbe(graphics, s_FrameTimingRejectedGraphResetScope);
    ASSERT_TRUE(rejectedProbe.initialize());
    graphics.addRenderPassToBack(rejectedProbe);
    {
        const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        ASSERT_TRUE(graphicsQueue.valid());
        const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
        );
        ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        ASSERT_TRUE(graphics.prepareFramePreamble());
        graphics.render();
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    graphics.removeRenderPass(rejectedProbe);
    ASSERT_TRUE(rejectedProbe.recorded());
    ASSERT_TRUE(device.waitForIdle());

    // collect() observes no sample from the rejected reset frame, then the second preamble's accepted graph packet
    // makes the pool available for the next dynamic-rendering scope.
    ASSERT_TRUE(graphics.prepareFramePreamble());
    EXPECT_FALSE(timingSink.stats(s_FrameTimingRejectedGraphResetScope.identity).valid());

    FrameTimingPreambleProbePass acceptedProbe(graphics, s_FrameTimingRejectedGraphResetScope);
    ASSERT_TRUE(acceptedProbe.initialize());
    graphics.addRenderPassToBack(acceptedProbe);
    graphics.render();
    graphics.removeRenderPass(acceptedProbe);
    ASSERT_TRUE(acceptedProbe.recorded());
    ASSERT_TRUE(device.waitForIdle());

    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingRejectedGraphResetScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

