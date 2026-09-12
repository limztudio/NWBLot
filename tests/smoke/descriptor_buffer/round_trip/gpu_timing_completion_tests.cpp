// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"
#include "timing_capture_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


inline constexpr GpuTimingScopeDefinition s_AcceptedCompletionScope("tests/timing_accepted_completion");


#if !defined(NWB_FINAL)

// An available result from seed A must not let capacity-one query B publish or return to the free list before B's
// exact accepted submission completes. A test-owned native submission tail gate makes that real ordering observable.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingAcceptedSubmissionCompletionGatesPublicationAndReuse){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AcceptedCompletionScope.identity, device, 1u));
    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());

    timing.beginFrame(230u);
    auto seedCommandList = device.createCommandList();
    ASSERT_NE(seedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket seedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(seedTicket);
        seedCommandList->open();
        {
            GpuTimingMeasure seedMeasure(timing, s_AcceptedCompletionScope, device, *seedCommandList);
            ASSERT_TRUE(seedMeasure.valid());
        }
        seedCommandList->close();
    }
    CommandList* seedCommandLists[] = { seedCommandList.get() };
    const QueueSubmissionToken seedToken = seedTicket.submit(
        device,
        seedCommandLists,
        LengthOf(seedCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(seedToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 231u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate submissionGate(device, graphicsQueue);
    ASSERT_TRUE(submissionGate.valid());

    timing.beginFrame(231u);
    const GpuTimingSampleAttribution acceptedAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(acceptedAttribution.valid());
    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        {
            GpuTimingMeasure acceptedMeasure(
                timing,
                s_AcceptedCompletionScope,
                device,
                *acceptedCommandList,
                acceptedAttribution
            );
            ASSERT_TRUE(acceptedMeasure.valid());
        }
        acceptedCommandList->close();
    }
    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    QueueSubmissionToken acceptedToken;
    {
        __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate::ScopedSubmitInterception interception(
            submissionGate
        );
        ASSERT_TRUE(interception.valid());
        acceptedToken = acceptedTicket.submit(
            device,
            acceptedCommandLists,
            LengthOf(acceptedCommandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
    }
    ASSERT_TRUE(acceptedToken.valid());
    ASSERT_TRUE(submissionGate.splitSubmissionAccepted());
    ASSERT_TRUE(submissionGate.waitUntilReady());
    EXPECT_LT(device.queueGetCompletedInstance(graphicsQueue), acceptedToken.value);
    timing.collect(device, 232u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);

    auto blockedCommandList = device.createCommandList();
    ASSERT_NE(blockedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket blockedTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(blockedTicket);
        blockedCommandList->open();
        {
            GpuTimingMeasure blockedMeasure(timing, s_AcceptedCompletionScope, device, *blockedCommandList);
            EXPECT_FALSE(blockedMeasure.valid());
        }
        blockedCommandList->close();
    }
    const GpuTimingRecorderStatistics blockedStatistics = timing.statistics(device);
    EXPECT_EQ(
        blockedStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::QueryCapacityUnavailable],
        1u
    );

    ASSERT_TRUE(submissionGate.releaseAndWait());
    EXPECT_GE(device.queueGetCompletedInstance(graphicsQueue), acceptedToken.value);
    timing.collect(device, 232u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_AcceptedCompletionScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].sourceFrameIndex, 231u);
    EXPECT_EQ(completedSamples.samples[0u].attribution, acceptedAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);
    EXPECT_EQ(completedSamples.samples[0u].physicalQueue.index, acceptedToken.physicalQueueIndex);
    EXPECT_EQ(completedSamples.samples[0u].physicalQueue.deviceGeneration, acceptedToken.deviceGeneration);

    auto reusableCommandList = device.createCommandList();
    ASSERT_NE(reusableCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket reusableTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(reusableTicket);
        reusableCommandList->open();
        {
            GpuTimingMeasure reusableMeasure(timing, s_AcceptedCompletionScope, device, *reusableCommandList);
            ASSERT_TRUE(reusableMeasure.valid());
            reusableMeasure.discardTiming();
        }
        reusableCommandList->close();
    }

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

// Slot zero is released while slot one retains an older unsubmitted recording. Reusing slot zero must not turn
// the next catch-up publication's source-frame bounds into slot traversal order or reorder listener samples.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingReusedSlotsPublishSourceBoundsWithoutReorderingSamples){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    const GpuTimingScopeDefinition scopeDefinition("tests/timing_reused_slot_source_bounds");

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(scopeDefinition.identity, device, 2u));
    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());

    auto seedCommandList = device.createCommandList();
    auto olderCommandList = device.createCommandList();
    auto newerCommandList = device.createCommandList();
    ASSERT_NE(seedCommandList.get(), nullptr);
    ASSERT_NE(olderCommandList.get(), nullptr);
    ASSERT_NE(newerCommandList.get(), nullptr);
    GpuTimingSubmissionTicket seedTicket(timing);
    GpuTimingSubmissionTicket olderTicket(timing);
    GpuTimingSubmissionTicket newerTicket(timing);
    const auto recordScope = [&](
        const u64 frameIndex,
        CommandList& commandList,
        GpuTimingSubmissionTicket& ticket,
        const GpuTimingSampleAttribution attribution){
        timing.beginFrame(frameIndex);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(ticket);
        commandList.open();
        bool recorded = false;
        {
            GpuTimingMeasure measure(timing, scopeDefinition, device, commandList, attribution);
            recorded = measure.valid();
        }
        commandList.close();
        return recorded;
    };

    ASSERT_TRUE(recordScope(49u, *seedCommandList, seedTicket, s_NoGpuTimingSampleAttribution));
    CommandList* seedLists[] = { seedCommandList.get() };
    ASSERT_TRUE(seedTicket.submit(device, seedLists, LengthOf(seedLists)));
    ASSERT_TRUE(device.waitForIdle());
    const GpuTimingSampleAttribution olderAttribution = timing.allocateSampleAttribution();
    const GpuTimingSampleAttribution newerAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(olderAttribution.valid());
    ASSERT_TRUE(newerAttribution.valid());
    ASSERT_TRUE(recordScope(50u, *olderCommandList, olderTicket, olderAttribution));

    // The seed occupies slot zero until this collect; the unsubmitted frame-50 query keeps slot one reserved.
    timing.collect(device, 50u);
    const Perf::TimingStats seedStats = timingSink.stats(scopeDefinition.identity);
    ASSERT_EQ(seedStats.sampleCount, 1u);
    EXPECT_EQ(seedStats.firstSampleFrameIndex, 49u);
    EXPECT_EQ(seedStats.lastSampleFrameIndex, 49u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);
    ASSERT_TRUE(recordScope(51u, *newerCommandList, newerTicket, newerAttribution));
    CommandList* olderLists[] = { olderCommandList.get() };
    CommandList* newerLists[] = { newerCommandList.get() };
    ASSERT_TRUE(olderTicket.submit(device, olderLists, LengthOf(olderLists)));
    ASSERT_TRUE(newerTicket.submit(device, newerLists, LengthOf(newerLists)));
    ASSERT_TRUE(device.waitForIdle());

    timing.collect(device, 52u);
    ASSERT_EQ(completedSamples.sampleCount, 2u);
    const GpuTimingSample& first = completedSamples.samples[0u];
    const GpuTimingSample& last = completedSamples.samples[1u];
    EXPECT_EQ(first.sourceFrameIndex, 51u);
    EXPECT_EQ(first.attribution, newerAttribution);
    EXPECT_EQ(last.sourceFrameIndex, 50u);
    EXPECT_EQ(last.attribution, olderAttribution);
    EXPECT_TRUE(first.published);
    EXPECT_TRUE(last.published);
    const Perf::TimingStats& stats = timingSink.stats(scopeDefinition.identity);
    ASSERT_EQ(stats.sampleCount, 2u);
    EXPECT_EQ(stats.publishFrameIndex, 52u);
    EXPECT_EQ(stats.firstSampleFrameIndex, 50u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 51u);
    EXPECT_DOUBLE_EQ(stats.seconds, first.durationSeconds + last.durationSeconds);
    EXPECT_DOUBLE_EQ(stats.minSeconds, Min(first.durationSeconds, last.durationSeconds));
    EXPECT_DOUBLE_EQ(stats.maxSeconds, Max(first.durationSeconds, last.durationSeconds));
    EXPECT_DOUBLE_EQ(stats.lastSeconds, last.durationSeconds);
    EXPECT_EQ(timing.statistics(device).materializedQueryCount, 2u);

    timing.collect(device, 53u);
    EXPECT_EQ(completedSamples.sampleCount, 2u);
    EXPECT_FALSE(timingSink.stats(scopeDefinition.identity).valid());
    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

