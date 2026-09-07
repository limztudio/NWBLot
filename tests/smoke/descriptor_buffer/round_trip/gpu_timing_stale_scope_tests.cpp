// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "timing_capture_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_UnpreparedTimingScope("tests/timing_unprepared_scope");


inline constexpr GpuTimingScopeDefinition s_StableSubmissionTicketScope("tests/timing_stable_submission_ticket");


inline constexpr GpuTimingScopeDefinition s_StableFrameTransactionScope("tests/timing_stable_frame_transaction");


// Recording must not grow persistent timer-query pools. A scope that was not declared during preparation simply
// produces no sample, even when the command list is outside dynamic rendering and could otherwise self-reset a new
// query pool on the device timeline.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingScopesRequirePreparedQueryPools){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(graphics.prepareFramePreamble());

    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open();
        {
            GpuTimingMeasure timingMeasure(timing, s_UnpreparedTimingScope, device, *commandList);
        }
        commandList->close();
    }

    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, 1u));
    ASSERT_TRUE(device.waitForIdle());
    ASSERT_TRUE(graphics.prepareFramePreamble());
    EXPECT_FALSE(timingSink.stats(s_UnpreparedTimingScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// An unresolved ticket can outlive both its discarded command buffer and a recorder reset. Recreating the same
// accumulator gives its first slot the same index and reservation, so only the recorder epoch can keep the stale
// ticket from releasing the new scope.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketStaleScopeCannotReleaseRecreatedAccumulatorSlot){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_StableSubmissionTicketScope.identity, device, 1u));
    timing.beginFrame(210u);

    GpuTimingSubmissionTicket staleTicket(timing);
    auto staleCommandList = device.createCommandList();
    ASSERT_NE(staleCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(staleTicket);
        staleCommandList->open();
        {
            GpuTimingMeasure staleTiming(timing, s_StableSubmissionTicketScope, device, *staleCommandList);
            ASSERT_TRUE(staleTiming.valid());
        }
        staleCommandList->close();
    }
    ASSERT_TRUE(staleCommandList->hasCommandBuffer());

    // Drop every native reference to the old query pool before resetQueries() destroys it. Only the ticket's stable
    // logical identity remains stale across recreation.
    staleCommandList.reset();
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_StableSubmissionTicketScope.identity, device, 1u));
    timing.beginFrame(211u);

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution acceptedTimingAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(acceptedTimingAttribution.valid());
    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        {
            GpuTimingMeasure acceptedTiming(
                timing,
                s_StableSubmissionTicketScope,
                device,
                *acceptedCommandList,
                acceptedTimingAttribution
            );
            ASSERT_TRUE(acceptedTiming.valid());
        }
        acceptedCommandList->close();
    }

    staleTicket.discard();
    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    ASSERT_TRUE(acceptedTicket.submit(device, acceptedCommandLists, LengthOf(acceptedCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 212u);

    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_StableSubmissionTicketScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].sourceFrameIndex, 211u);
    EXPECT_EQ(completedSamples.samples[0u].attribution, acceptedTimingAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);
    EXPECT_GE(completedSamples.samples[0u].durationSeconds, 0.0);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Frame transactions keep a deferred scope after recording both endpoints. Its stale cleanup must resolve the live
// accumulator by name without releasing a recreated same-name/index/reservation record from a later transaction.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionStaleScopeCannotReleaseRecreatedAccumulatorSlot){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    if(!device.supportsComparableGpuTimestamps(device.getPrimaryPhysicalQueue(CommandQueue::Graphics)))
        GTEST_SKIP() << "GPU timing stale frame transaction: comparable 64-bit device timestamps are unavailable.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_StableFrameTransactionScope.identity, device, 1u));
    timing.beginFrame(220u);

    GpuTimingFrameTransaction staleTransaction(timing);
    auto staleCommandList = device.createCommandList();
    ASSERT_NE(staleCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket staleRecordingTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(staleRecordingTicket);
        staleCommandList->open();
        ASSERT_TRUE(staleTransaction.begin(s_StableFrameTransactionScope, device, *staleCommandList));
        ASSERT_TRUE(staleTransaction.recordEnd(*staleCommandList));
        staleCommandList->close();
    }
    ASSERT_TRUE(staleCommandList->hasCommandBuffer());

    staleCommandList.reset();
    timing.resetQueries();
    ASSERT_TRUE(timing.prepareScopeQueries(s_StableFrameTransactionScope.identity, device, 1u));
    timing.beginFrame(221u);

    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution acceptedTimingAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(acceptedTimingAttribution.valid());
    GpuTimingFrameTransaction acceptedTransaction(timing);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        ASSERT_TRUE(acceptedTransaction.begin(
            s_StableFrameTransactionScope,
            device,
            *acceptedCommandList,
            acceptedTimingAttribution
        ));
        ASSERT_TRUE(acceptedTransaction.recordEnd(*acceptedCommandList));
        acceptedCommandList->close();
    }

    staleTransaction.discard();
    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    const QueueSubmissionToken acceptedToken = acceptedTicket.submit(
        device,
        acceptedCommandLists,
        LengthOf(acceptedCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());
    ASSERT_TRUE(acceptedTransaction.confirmBeginSubmission(acceptedToken));
    ASSERT_TRUE(acceptedTransaction.confirmEndSubmission(acceptedToken, true));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 222u);

    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_StableFrameTransactionScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].sourceFrameIndex, 221u);
    EXPECT_EQ(completedSamples.samples[0u].attribution, acceptedTimingAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);
    EXPECT_GE(completedSamples.samples[0u].durationSeconds, 0.0);
    EXPECT_TRUE(completedSamples.samples[0u].comparableRange.valid());
    EXPECT_EQ(completedSamples.samples[0u].physicalQueue, completedSamples.samples[0u].comparableRange.physicalQueue);
    EXPECT_EQ(completedSamples.samples[0u].comparableRange.physicalQueue.index, acceptedToken.physicalQueueIndex);
    EXPECT_EQ(completedSamples.samples[0u].comparableRange.physicalQueue.deviceGeneration, acceptedToken.deviceGeneration);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

