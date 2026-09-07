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

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

