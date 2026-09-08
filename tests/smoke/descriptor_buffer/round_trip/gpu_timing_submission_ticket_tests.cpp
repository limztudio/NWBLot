// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "timing_capture_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_SubmissionTicketScope("tests/timing_submission_ticket");


inline constexpr GpuTimingScopeDefinition s_SubmissionTicketEndFailedScope("tests/timing_submission_ticket_end_failed");


inline constexpr GpuTimingScopeDefinition s_ConcurrentSubmissionTicketScope("tests/timing_submission_ticket_concurrent");


// A frame metric starts on the G-buffer primary and ends on the ordered post-G-buffer primary. Omitting the consumer
// must reject before either list is consumed, then the same complete batch must remain submit-ready and publish its
// sample. Once accepted, the query still needs a new device-timeline reset before reuse inside dynamic rendering.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketRejectsIncompleteSplitScopeWithoutMutationAndRecovers){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SubmissionTicketScope.identity, device, 1u));
    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution recoveredTimingAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution acceptedTimingAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(recoveredTimingAttribution.valid());
    ASSERT_TRUE(acceptedTimingAttribution.valid());

    auto target = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(target.get(), nullptr);
    auto framebuffer = device.createFramebuffer(FramebufferDesc().addColorAttachment(target.get()));
    ASSERT_NE(framebuffer.get(), nullptr);

    // Establish the one prepared pool on the device timeline, exactly as Graphics::prepareFramePreamble() does
    // before its passes.
    auto resetCommandList = device.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    const QueueSubmissionToken resetToken = device.executeCommandLists(
        resetCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(resetToken.valid());
    timing.confirmFrameReset(resetToken);

    auto abandonedCommandList = device.createCommandList();
    ASSERT_NE(abandonedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket abandonedTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(abandonedTicket);
            abandonedCommandList->open();
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(framebuffer.get());
            abandonedCommandList->setGraphicsState(graphicsState);
            {
                GpuTimingMeasure abandonedTiming(timing, s_SubmissionTicketScope, device, *abandonedCommandList);
                ASSERT_TRUE(abandonedTiming.valid());
                abandonedTiming.discardTiming();
            }
            abandonedCommandList->endRenderPass();
            abandonedCommandList->close();
        }
    }

    auto splitScopeResetCommandList = device.createCommandList();
    ASSERT_NE(splitScopeResetCommandList.get(), nullptr);
    splitScopeResetCommandList->open();
    timing.recordFrameReset(*splitScopeResetCommandList);
    splitScopeResetCommandList->close();
    CommandList* splitScopeResetCommandLists[] = { splitScopeResetCommandList.get() };
    const QueueSubmissionToken splitScopeResetToken = device.executeCommandLists(
        splitScopeResetCommandLists,
        LengthOf(splitScopeResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(splitScopeResetToken.valid());
    timing.confirmFrameReset(splitScopeResetToken);
    CommandList* staleAbandonedCommandLists[] = { abandonedCommandList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        staleAbandonedCommandLists,
        LengthOf(staleAbandonedCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());

    auto producer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);
    {
        GpuTimingSubmissionTicket rejectedTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(rejectedTicket);
            producer->open();
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(framebuffer.get());
            producer->setGraphicsState(graphicsState);

            GpuTimingMeasure rejectedTiming(
                timing,
                s_SubmissionTicketScope,
                device,
                *producer,
                recoveredTimingAttribution
            );
            ASSERT_TRUE(rejectedTiming.valid());
            ASSERT_TRUE(rejectedTiming.finishMarker());
            producer->endRenderPass();
            producer->close();

            consumer->open();
            rejectedTiming.finishTiming(*consumer);
            consumer->close();
        }

        CommandList* incompleteCommandLists[] = { producer.get() };
        EXPECT_FALSE(rejectedTicket.submit(device, incompleteCommandLists, LengthOf(incompleteCommandLists)));
        EXPECT_TRUE(producer->hasCommandBuffer());
        EXPECT_TRUE(consumer->hasCommandBuffer());

        CommandList* reversedCommandLists[] = { consumer.get(), producer.get() };
        EXPECT_FALSE(rejectedTicket.submit(device, reversedCommandLists, LengthOf(reversedCommandLists)));
        EXPECT_TRUE(producer->hasCommandBuffer());
        EXPECT_TRUE(consumer->hasCommandBuffer());

        CommandList* recoveredCommandLists[] = { producer.get(), consumer.get() };
        ASSERT_TRUE(rejectedTicket.submit(device, recoveredCommandLists, LengthOf(recoveredCommandLists)));
    }
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_SubmissionTicketScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].attribution, recoveredTimingAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);
    EXPECT_GE(completedSamples.samples[0u].durationSeconds, 0.0);

    // The accepted split consumed its reset, so a retry cannot reuse the query inside dynamic rendering until the
    // next accepted graph preamble resets it on the device timeline.
    auto unavailableRetryCommandList = device.createCommandList();
    ASSERT_NE(unavailableRetryCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket unavailableRetryTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(unavailableRetryTicket);
            unavailableRetryCommandList->open();
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(framebuffer.get());
            unavailableRetryCommandList->setGraphicsState(graphicsState);
            {
                GpuTimingMeasure unavailableRetryTiming(
                    timing,
                    s_SubmissionTicketScope,
                    device,
                    *unavailableRetryCommandList
                );
                EXPECT_FALSE(unavailableRetryTiming.valid());
            }
            unavailableRetryCommandList->endRenderPass();
            unavailableRetryCommandList->close();
        }
    }

    auto retryResetCommandList = device.createCommandList();
    ASSERT_NE(retryResetCommandList.get(), nullptr);
    retryResetCommandList->open();
    timing.recordFrameReset(*retryResetCommandList);
    retryResetCommandList->close();
    CommandList* retryResetCommandLists[] = { retryResetCommandList.get() };
    const QueueSubmissionToken retryResetToken = device.executeCommandLists(
        retryResetCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(retryResetToken.valid());
    timing.confirmFrameReset(retryResetToken);

    CommandList* staleSplitCommandLists[] = { producer.get(), consumer.get() };
    EXPECT_FALSE(device.executeCommandLists(
        staleSplitCommandLists,
        LengthOf(staleSplitCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    producer.reset();
    consumer.reset();

    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        GraphicsState graphicsState;
        graphicsState.setFramebuffer(framebuffer.get());
        acceptedCommandList->setGraphicsState(graphicsState);
        {
            GpuTimingMeasure acceptedTiming(
                timing,
                s_SubmissionTicketScope,
                device,
                *acceptedCommandList,
                acceptedTimingAttribution
            );
        }
        acceptedCommandList->endRenderPass();
        acceptedCommandList->close();
    }

    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    ASSERT_TRUE(acceptedTicket.submit(device, acceptedCommandLists, 1u));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    EXPECT_TRUE(timingSink.stats(s_SubmissionTicketScope.identity).valid());
    ASSERT_EQ(completedSamples.sampleCount, 2u);
    EXPECT_EQ(completedSamples.samples[1u].scopeName, s_SubmissionTicketScope.identity);
    EXPECT_EQ(completedSamples.samples[1u].attribution, acceptedTimingAttribution);
    EXPECT_TRUE(completedSamples.samples[1u].published);
    EXPECT_GE(completedSamples.samples[1u].durationSeconds, 0.0);

    // Collecting the accepted sample frees its slot and consumes that frame's reset, so the next render-pass scope
    // needs another accepted preamble before it can write the query again.
    auto retirementResetCommandList = device.createCommandList();
    ASSERT_NE(retirementResetCommandList.get(), nullptr);
    retirementResetCommandList->open();
    timing.recordFrameReset(*retirementResetCommandList);
    retirementResetCommandList->close();
    CommandList* retirementResetCommandLists[] = { retirementResetCommandList.get() };
    const QueueSubmissionToken retirementResetToken = device.executeCommandLists(
        retirementResetCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(retirementResetToken.valid());
    timing.confirmFrameReset(retirementResetToken);

    // Retiring capture before an accepted query is collected must notify the listener with an unusable result. This
    // releases higher-level task attribution without turning an old epoch into a timing sample after reactivation.
    const GpuTimingSampleAttribution retiredTimingAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(retiredTimingAttribution.valid());
    auto retiredCommandList = device.createCommandList();
    ASSERT_NE(retiredCommandList.get(), nullptr);
    GpuTimingSubmissionTicket retiredTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(retiredTicket);
        retiredCommandList->open();
        GraphicsState graphicsState;
        graphicsState.setFramebuffer(framebuffer.get());
        retiredCommandList->setGraphicsState(graphicsState);
        {
            GpuTimingMeasure retiredTiming(
                timing,
                s_SubmissionTicketScope,
                device,
                *retiredCommandList,
                retiredTimingAttribution
            );
        }
        retiredCommandList->endRenderPass();
        retiredCommandList->close();
    }
    CommandList* retiredCommandLists[] = { retiredCommandList.get() };
    const GpuPhysicalQueueId retiredQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(retiredQueue.valid());
    ASSERT_TRUE(retiredTicket.submit(device, retiredCommandLists, 1u));
    ASSERT_TRUE(device.waitForIdle());

    s_scope->setGpuTimingEnabled(false);
    ASSERT_EQ(completedSamples.sampleCount, 3u);
    EXPECT_EQ(completedSamples.samples[2u].scopeName, s_SubmissionTicketScope.identity);
    EXPECT_EQ(completedSamples.samples[2u].attribution, retiredTimingAttribution);
    EXPECT_FALSE(completedSamples.samples[2u].published);
    EXPECT_EQ(completedSamples.samples[2u].durationSeconds, 0.0);
    EXPECT_EQ(completedSamples.samples[2u].physicalQueue, retiredQueue);
    EXPECT_FALSE(completedSamples.samples[2u].comparableRange.valid());
    timing.resetQueries();
}


// Replacing the consumer's native lease after publishing an end removes that exact end claim. Submission must
// terminally revoke the producer claim before Vulkan sees it, while leaving the query reusable without resetQueries().
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketRejectsReplacedEndLeaseAndReusesQuery){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(
        device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing failed endpoint: Graphics queue timestamps are unavailable.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SubmissionTicketEndFailedScope.identity, device, 1u));
    timing.beginFrame(2u);

    auto producer = device.createCommandList();
    auto replacedConsumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(replacedConsumer.get(), nullptr);
    GpuTimingSubmissionTicket failedEndpointTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(failedEndpointTicket);
        producer->open();
        GpuTimingMeasure failedEndpointTiming(
            timing,
            s_SubmissionTicketEndFailedScope,
            device,
            *producer
        );
        ASSERT_TRUE(failedEndpointTiming.valid());
        ASSERT_TRUE(failedEndpointTiming.finishMarker());
        producer->close();
        replacedConsumer->open();
        failedEndpointTiming.finishTiming(*replacedConsumer);
        replacedConsumer->close();
    }

    const u64 publishedEndLease = replacedConsumer->recordingLeaseSerial();
    ASSERT_NE(publishedEndLease, 0u);
    replacedConsumer->open();
    replacedConsumer->close();
    ASSERT_NE(replacedConsumer->recordingLeaseSerial(), publishedEndLease);

    CommandList* replacedEndpointCommandLists[] = { producer.get(), replacedConsumer.get() };
    EXPECT_FALSE(failedEndpointTicket.submit(
        device,
        replacedEndpointCommandLists,
        LengthOf(replacedEndpointCommandLists)
    ));
    EXPECT_TRUE(producer->hasCommandBuffer());
    EXPECT_TRUE(replacedConsumer->hasCommandBuffer());
    CommandList* producerCommandLists[] = { producer.get() };
    EXPECT_FALSE(device.executeCommandLists(
        producerCommandLists,
        LengthOf(producerCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution reusedAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(reusedAttribution.valid());
    auto reusedCommandList = device.createCommandList();
    ASSERT_NE(reusedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket reusedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(reusedTicket);
        reusedCommandList->open();
        {
            GpuTimingMeasure reusedTiming(
                timing,
                s_SubmissionTicketEndFailedScope,
                device,
                *reusedCommandList,
                reusedAttribution
            );
            ASSERT_TRUE(reusedTiming.valid());
        }
        reusedCommandList->close();
    }
    CommandList* reusedCommandLists[] = { reusedCommandList.get() };
    ASSERT_TRUE(reusedTicket.submit(device, reusedCommandLists, LengthOf(reusedCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 3u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_SubmissionTicketEndFailedScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].attribution, reusedAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Both submit forms must reject an incomplete batch before Vulkan sees any part of it, and that rejection must
// resolve the ticket so a later retry cannot accidentally submit a split timing scope.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketMalformedBatchesResolveAcrossSubmitOverloads){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    auto implicitCommandList = device.createCommandList();
    auto explicitQueueCommandList = device.createCommandList();
    ASSERT_NE(implicitCommandList.get(), nullptr);
    ASSERT_NE(explicitQueueCommandList.get(), nullptr);
    implicitCommandList->open();
    implicitCommandList->close();
    explicitQueueCommandList->open();
    explicitQueueCommandList->close();
    ASSERT_TRUE(implicitCommandList->hasCommandBuffer());
    ASSERT_TRUE(explicitQueueCommandList->hasCommandBuffer());

    CommandList* implicitCommandLists[] = { implicitCommandList.get() };
    GpuTimingSubmissionTicket rejectedImplicitTicket(timing);
    EXPECT_FALSE(rejectedImplicitTicket.submit(device, nullptr, 0u));
    EXPECT_FALSE(rejectedImplicitTicket.submit(device, implicitCommandLists, 1u));
    EXPECT_TRUE(implicitCommandList->hasCommandBuffer());

    CommandList* incompleteExplicitCommandLists[] = { nullptr };
    CommandList* explicitCommandLists[] = { explicitQueueCommandList.get() };
    GpuTimingSubmissionTicket rejectedExplicitTicket(timing);
    EXPECT_FALSE(rejectedExplicitTicket.submit(
        device,
        incompleteExplicitCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_FALSE(rejectedExplicitTicket.submit(
        device,
        explicitCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(explicitQueueCommandList->hasCommandBuffer());

    GpuTimingSubmissionTicket acceptedImplicitTicket(timing);
    ASSERT_TRUE(acceptedImplicitTicket.submit(device, implicitCommandLists, 1u));
    GpuTimingSubmissionTicket acceptedExplicitTicket(timing);
    ASSERT_TRUE(acceptedExplicitTicket.submit(
        device,
        explicitCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
}


// Independent packet jobs can share one submission ticket. Both workers reserve the same timing scope at the same
// latch, so the recorder must claim distinct query slots before either command list reaches its ending timestamp.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketReservesConcurrentWorkerScopes){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_ConcurrentSubmissionTicketScope.identity, device, 2u));

    auto resetCommandList = device.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    const QueueSubmissionToken resetToken = device.executeCommandLists(
        resetCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(resetToken.valid());
    timing.confirmFrameReset(resetToken);

    auto firstCommandList = device.createCommandList();
    auto secondCommandList = device.createCommandList();
    ASSERT_NE(firstCommandList.get(), nullptr);
    ASSERT_NE(secondCommandList.get(), nullptr);

    GpuTimingSubmissionTicket timingTicket(timing);
    Latch recordingStarted(2);
    Latch queryReservationsStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::TaskHandle firstJob = graphics.scheduleGraphicsTask([&](){
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        firstCommandList->open();
        recordingStarted.count_down();
        recordingStarted.wait();
        {
            GpuTimingMeasure measure(timing, s_ConcurrentSubmissionTicketScope, device, *firstCommandList);
            queryReservationsStarted.count_down();
            queryReservationsStarted.wait();
        }
        firstCommandList->close();
        firstRecorded = firstCommandList->hasCommandBuffer();
    });
    const Graphics::TaskHandle secondJob = graphics.scheduleGraphicsTask([&](){
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        secondCommandList->open();
        recordingStarted.count_down();
        recordingStarted.wait();
        {
            GpuTimingMeasure measure(timing, s_ConcurrentSubmissionTicketScope, device, *secondCommandList);
            queryReservationsStarted.count_down();
            queryReservationsStarted.wait();
        }
        secondCommandList->close();
        secondRecorded = secondCommandList->hasCommandBuffer();
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitTask(firstJob);
    graphics.waitTask(secondJob);
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    CommandList* commandLists[] = { firstCommandList.get(), secondCommandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, 2u));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    EXPECT_EQ(timingSink.stats(s_ConcurrentSubmissionTicketScope.identity).sampleCount, 2u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

