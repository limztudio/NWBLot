// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"
#include "timing_capture_test_support.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


inline constexpr GpuTimingScopeDefinition s_TimerQueryFailureScope("tests/timing_query_failure");


inline constexpr GpuTimingScopeDefinition s_TimerQueryAcceptedRecoveryScope("tests/timing_query_accepted_recovery");


inline constexpr GpuTimingScopeDefinition s_TimerQueryForeignDeviceScope("tests/timing_query_foreign_device");


inline constexpr GpuTimingScopeDefinition s_TimerQueryComparableEpochScope("tests/timing_query_comparable_epoch");


inline constexpr GpuTimingScopeDefinition s_FrameTimingAcceptedDiscardQuarantineScope(
    "tests/frame_timing_accepted_discard_quarantine"
);


inline constexpr GpuTimingScopeDefinition s_FrameTimingAcceptedDestructionQuarantineScope(
    "tests/frame_timing_accepted_destruction_quarantine"
);


#if !defined(NWB_FINAL)

// Device identity is part of timing preflight, independently of physical queue ID values. A command list from a
// second live Device must reject before the backend can bind a query pool from the first Device's recorder.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionRejectsForeignCommandListDevice){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryForeignDeviceScope.identity, device, 1u));
    timing.beginFrame(200u);

    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize()){
        s_scope->setGpuTimingEnabled(false);
        timing.resetQueries();
        GTEST_SKIP() << "GPU timing foreign device: second headless Vulkan device is unavailable.";
    }

    auto foreignCommandList = foreignScope.graphics().getDevice().createCommandList();
    ASSERT_NE(foreignCommandList.get(), nullptr);
    GpuTimingFrameTransaction transaction(timing);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        foreignCommandList->open();
        EXPECT_FALSE(transaction.begin(s_TimerQueryForeignDeviceScope, device, *foreignCommandList));
        foreignCommandList->close();
    }
    EXPECT_FALSE(transaction.needsRetirement());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// timestampValidBits==0 is a legitimate queue-family capability. If the adapter exposes such a queue, an optional
// frame scope becomes a successful inactive transaction and never asks the backend to emit an invalid timestamp.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingUnsupportedPhysicalQueueIsSuccessfulInactiveScope){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* unsupportedQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        if(topology.queues[queueIndex].timestampValidBits == 0u){
            unsupportedQueue = &topology.queues[queueIndex];
            break;
        }
    }
    if(!unsupportedQueue)
        GTEST_SKIP() << "GPU timing unsupported queue: every exposed physical queue supports timestamps.";

    auto& timing = graphics.gpuTiming();
    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryUnsupportedQueueScope.identity, device, 1u));
    timing.beginFrame(201u);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(unsupportedQueue->id);
    auto commandList = device.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingFrameTransaction transaction(timing);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open();
        EXPECT_TRUE(transaction.begin(s_TimerQueryUnsupportedQueueScope, device, *commandList));
        EXPECT_FALSE(transaction.needsRetirement());
        EXPECT_TRUE(transaction.recordEnd(*commandList));
        commandList->close();
    }
    const GpuTimingRecorderStatistics unsupportedStatistics = timing.statistics(device);
    EXPECT_EQ(unsupportedStatistics.recordedScopeCount, 0u);
    EXPECT_EQ(
        unsupportedStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::QueueTimestampsUnsupported],
        1u
    );

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// A same-queue frame duration needs only the queue's native timestamp width. Calibrated absolute timestamps enrich
// its optional comparable range, but their absence must not turn the ordinary frame transaction into a no-op.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionFollowsQueueTimestampCapability){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    const bool queueTimestampsSupported = graphicsQueueInfo->timestampValidBits != 0u;

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryComparableEpochScope.identity, device, 1u));
    timing.beginFrame(202u);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto commandList = device.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingFrameTransaction transaction(timing);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open();
        ASSERT_TRUE(transaction.begin(s_TimerQueryComparableEpochScope, device, *commandList));
        commandList->close();
        EXPECT_EQ(transaction.recordEnd(*commandList), !queueTimestampsSupported);
    }
    EXPECT_FALSE(transaction.needsRetirement());
    const GpuTimingRecorderStatistics timestampStatistics = timing.statistics(device);
    EXPECT_EQ(timestampStatistics.recordedScopeCount, queueTimestampsSupported ? 1u : 0u);
    EXPECT_EQ(
        timestampStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::QueueTimestampsUnsupported],
        queueTimestampsSupported ? 0u : 1u
    );
    EXPECT_EQ(
        timestampStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::ComparableTimestampsUnsupported],
        0u
    );

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// Closed-list begin/end failures are hard recording failures, not inactive timing. A failure before begin acceptance
// releases the single reservation immediately; a failure after acceptance keeps it owned for an exact-queue recovery
// endpoint. Successful follow-up samples prove neither path leaks the one prepared query slot.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionPropagatesBackendFailuresWithoutLeakingReservations){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(
        device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing failure propagation: Graphics queue timestamps are unavailable.";

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryFailureScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryAcceptedRecoveryScope.identity, device, 1u));
    timing.beginFrame(202u);

    auto unopenedBegin = device.createCommandList();
    ASSERT_NE(unopenedBegin.get(), nullptr);
    GpuTimingFrameTransaction failedBeginTransaction(timing);
    GpuTimingSubmissionTicket failedBeginTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(failedBeginTicket);
        EXPECT_FALSE(failedBeginTransaction.begin(s_TimerQueryFailureScope, device, *unopenedBegin));
    }
    EXPECT_FALSE(failedBeginTransaction.needsRetirement());

    auto abandonedBegin = device.createCommandList();
    auto unopenedEnd = device.createCommandList();
    ASSERT_NE(abandonedBegin.get(), nullptr);
    ASSERT_NE(unopenedEnd.get(), nullptr);
    GpuTimingFrameTransaction abandonedTransaction(timing);
    GpuTimingSubmissionTicket abandonedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(abandonedTicket);
        abandonedBegin->open();
        ASSERT_TRUE(abandonedTransaction.begin(s_TimerQueryFailureScope, device, *abandonedBegin));
        EXPECT_FALSE(abandonedTransaction.recordEnd(*unopenedEnd));
        abandonedBegin->close();
    }
    EXPECT_FALSE(abandonedTransaction.needsRetirement());
    abandonedBegin.reset();
    unopenedEnd.reset();

    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingFrameTransaction acceptedTransaction(timing);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        ASSERT_TRUE(acceptedTransaction.begin(s_TimerQueryFailureScope, device, *acceptedCommandList));
        ASSERT_TRUE(acceptedTransaction.recordEnd(*acceptedCommandList));
        acceptedCommandList->close();
    }
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
    timing.collect(device, 203u);
    EXPECT_TRUE(timingSink.stats(s_TimerQueryFailureScope.identity).valid());

    timing.beginFrame(203u);
    auto acceptedPrefix = device.createCommandList();
    ASSERT_NE(acceptedPrefix.get(), nullptr);
    GpuTimingFrameTransaction recoveryTransaction(timing);
    GpuTimingSubmissionTicket acceptedPrefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedPrefixTicket);
        acceptedPrefix->open();
        ASSERT_TRUE(recoveryTransaction.begin(s_TimerQueryAcceptedRecoveryScope, device, *acceptedPrefix));
        acceptedPrefix->close();
    }
    CommandList* acceptedPrefixCommandLists[] = { acceptedPrefix.get() };
    const QueueSubmissionToken acceptedPrefixToken = acceptedPrefixTicket.submit(
        device,
        acceptedPrefixCommandLists,
        LengthOf(acceptedPrefixCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedPrefixToken.valid());
    ASSERT_TRUE(recoveryTransaction.confirmBeginSubmission(acceptedPrefixToken));
    ASSERT_TRUE(recoveryTransaction.needsRetirement());

    auto failedAcceptedEnd = device.createCommandList();
    ASSERT_NE(failedAcceptedEnd.get(), nullptr);
    EXPECT_FALSE(recoveryTransaction.recordEnd(*failedAcceptedEnd));
    EXPECT_TRUE(recoveryTransaction.needsRetirement());

    auto recoveryCommandList = device.createCommandList();
    ASSERT_NE(recoveryCommandList.get(), nullptr);
    recoveryCommandList->open();
    ASSERT_TRUE(recoveryTransaction.recordEnd(*recoveryCommandList));
    recoveryCommandList->close();
    CommandList* recoveryCommandLists[] = { recoveryCommandList.get() };
    const QueueSubmissionToken recoveryToken = device.executeCommandLists(
        recoveryCommandLists,
        LengthOf(recoveryCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(recoveryToken.valid());
    ASSERT_TRUE(recoveryTransaction.confirmEndSubmission(recoveryToken, false));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 204u);
    EXPECT_FALSE(timingSink.stats(s_TimerQueryAcceptedRecoveryScope.identity).valid());

    timing.beginFrame(204u);
    auto postRecoveryCommandList = device.createCommandList();
    ASSERT_NE(postRecoveryCommandList.get(), nullptr);
    GpuTimingFrameTransaction postRecoveryTransaction(timing);
    GpuTimingSubmissionTicket postRecoveryTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(postRecoveryTicket);
        postRecoveryCommandList->open();
        ASSERT_TRUE(postRecoveryTransaction.begin(
            s_TimerQueryAcceptedRecoveryScope,
            device,
            *postRecoveryCommandList
        ));
        ASSERT_TRUE(postRecoveryTransaction.recordEnd(*postRecoveryCommandList));
        postRecoveryCommandList->close();
    }
    CommandList* postRecoveryCommandLists[] = { postRecoveryCommandList.get() };
    const QueueSubmissionToken postRecoveryToken = postRecoveryTicket.submit(
        device,
        postRecoveryCommandLists,
        LengthOf(postRecoveryCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(postRecoveryToken.valid());
    ASSERT_TRUE(postRecoveryTransaction.confirmBeginSubmission(postRecoveryToken));
    ASSERT_TRUE(postRecoveryTransaction.confirmEndSubmission(postRecoveryToken, true));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 205u);
    EXPECT_TRUE(timingSink.stats(s_TimerQueryAcceptedRecoveryScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// Once a frame prefix is accepted, neither explicit discard nor ordinary destruction may mark its begin-only native
// query cycle Available. Both paths quarantine that exact object. The next frame materializes a replacement query,
// resets it through a valid preamble submission, and records again without requiring resetQueries().
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionAcceptedBeginOnlyCleanupReplacesQuarantinedQueries){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing accepted begin-only cleanup: Graphics queue timestamps are unavailable.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingAcceptedDiscardQuarantineScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingAcceptedDestructionQuarantineScope.identity, device, 1u));
    timing.beginFrame(206u);

    auto discardedPrefix = device.createCommandList();
    ASSERT_NE(discardedPrefix.get(), nullptr);
    GpuTimingFrameTransaction discardedTransaction(timing);
    GpuTimingSubmissionTicket discardedPrefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(discardedPrefixTicket);
        discardedPrefix->open();
        ASSERT_TRUE(discardedTransaction.begin(
            s_FrameTimingAcceptedDiscardQuarantineScope,
            device,
            *discardedPrefix
        ));
        discardedPrefix->close();
    }
    CommandList* discardedPrefixCommandLists[] = { discardedPrefix.get() };
    const QueueSubmissionToken discardedPrefixToken = discardedPrefixTicket.submit(
        device,
        discardedPrefixCommandLists,
        LengthOf(discardedPrefixCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(discardedPrefixToken.valid());
    ASSERT_TRUE(discardedTransaction.confirmBeginSubmission(discardedPrefixToken));
    discardedTransaction.discard();
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 207u);

    timing.beginFrame(207u);
    ASSERT_TRUE(timing.materializeRequestedQueries(device));
    auto postDiscardPreamble = device.createCommandList();
    ASSERT_NE(postDiscardPreamble.get(), nullptr);
    postDiscardPreamble->open();
    timing.recordFrameReset(*postDiscardPreamble);
    postDiscardPreamble->close();
    ASSERT_TRUE(postDiscardPreamble->hasCommandBuffer());
    CommandList* postDiscardPreambleCommandLists[] = { postDiscardPreamble.get() };
    const QueueSubmissionToken postDiscardPreambleToken = device.executeCommandLists(
        postDiscardPreambleCommandLists,
        LengthOf(postDiscardPreambleCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(postDiscardPreambleToken.valid());
    timing.confirmFrameReset(postDiscardPreambleToken);

    auto postDiscardProbe = device.createCommandList();
    ASSERT_NE(postDiscardProbe.get(), nullptr);
    GpuTimingSubmissionTicket postDiscardProbeTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(postDiscardProbeTicket);
        postDiscardProbe->open();
        {
            GpuTimingMeasure probe(
                timing,
                s_FrameTimingAcceptedDiscardQuarantineScope,
                device,
                *postDiscardProbe
            );
            EXPECT_TRUE(probe.valid());
        }
        postDiscardProbe->close();
    }
    ASSERT_TRUE(postDiscardProbe->hasCommandBuffer());
    CommandList* postDiscardProbeCommandLists[] = { postDiscardProbe.get() };
    ASSERT_TRUE(postDiscardProbeTicket.submit(
        device,
        postDiscardProbeCommandLists,
        LengthOf(postDiscardProbeCommandLists)
    ));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 208u);

    timing.beginFrame(208u);
    {
        auto destroyedPrefix = device.createCommandList();
        ASSERT_NE(destroyedPrefix.get(), nullptr);
        GpuTimingFrameTransaction destroyedTransaction(timing);
        GpuTimingSubmissionTicket destroyedPrefixTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(destroyedPrefixTicket);
            destroyedPrefix->open();
            ASSERT_TRUE(destroyedTransaction.begin(
                s_FrameTimingAcceptedDestructionQuarantineScope,
                device,
                *destroyedPrefix
            ));
            destroyedPrefix->close();
        }
        CommandList* destroyedPrefixCommandLists[] = { destroyedPrefix.get() };
        const QueueSubmissionToken destroyedPrefixToken = destroyedPrefixTicket.submit(
            device,
            destroyedPrefixCommandLists,
            LengthOf(destroyedPrefixCommandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(destroyedPrefixToken.valid());
        ASSERT_TRUE(destroyedTransaction.confirmBeginSubmission(destroyedPrefixToken));
    }
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 209u);

    timing.beginFrame(209u);
    ASSERT_TRUE(timing.materializeRequestedQueries(device));
    auto postDestructionPreamble = device.createCommandList();
    ASSERT_NE(postDestructionPreamble.get(), nullptr);
    postDestructionPreamble->open();
    timing.recordFrameReset(*postDestructionPreamble);
    postDestructionPreamble->close();
    ASSERT_TRUE(postDestructionPreamble->hasCommandBuffer());
    CommandList* postDestructionPreambleCommandLists[] = { postDestructionPreamble.get() };
    const QueueSubmissionToken postDestructionPreambleToken = device.executeCommandLists(
        postDestructionPreambleCommandLists,
        LengthOf(postDestructionPreambleCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(postDestructionPreambleToken.valid());
    timing.confirmFrameReset(postDestructionPreambleToken);

    auto postDestructionProbe = device.createCommandList();
    ASSERT_NE(postDestructionProbe.get(), nullptr);
    GpuTimingSubmissionTicket postDestructionProbeTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(postDestructionProbeTicket);
        postDestructionProbe->open();
        {
            GpuTimingMeasure probe(
                timing,
                s_FrameTimingAcceptedDestructionQuarantineScope,
                device,
                *postDestructionProbe
            );
            EXPECT_TRUE(probe.valid());
        }
        postDestructionProbe->close();
    }
    ASSERT_TRUE(postDestructionProbe->hasCommandBuffer());
    CommandList* postDestructionProbeCommandLists[] = { postDestructionProbe.get() };
    ASSERT_TRUE(postDestructionProbeTicket.submit(
        device,
        postDestructionProbeCommandLists,
        LengthOf(postDestructionProbeCommandLists)
    ));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 210u);

    const GpuTimingRecorderStatistics statistics = timing.statistics(device);
    EXPECT_EQ(statistics.quarantinedScopeCount, 2u);
    EXPECT_EQ(statistics.materializedQueryCount, 4u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// The async renderer records its Graphics-prefix timestamp before it knows whether the pre-recorded final packet will
// submit. Reject that final submit after the prefix is accepted, then use a tiny Graphics recovery packet to complete
// the query without publishing a misleading partial render.frame sample. A following valid transaction proves the
// one reserved query slot was released rather than leaked.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionRetiresAcceptedPrefixAfterRejectedFinal){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing recovery transaction: Graphics queue timestamps are unavailable.";
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution recoveryAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(recoveryAttribution.valid());

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
    timing.beginFrame(90u);

    auto prefix = device.createCommandList();
    auto rejectedFinal = device.createCommandList();
    auto recovery = device.createCommandList();
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(rejectedFinal.get(), nullptr);
    ASSERT_NE(recovery.get(), nullptr);

    GpuTimingFrameTransaction rejectedTransaction(timing);
    GpuTimingSubmissionTicket prefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(prefixTicket);
        prefix->open();
        ASSERT_TRUE(rejectedTransaction.begin(
            s_FrameTransactionScope,
            device,
            *prefix,
            recoveryAttribution
        ));
        prefix->close();
    }
    GpuTimingSubmissionTicket finalTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(finalTicket);
        rejectedFinal->open();
        ASSERT_TRUE(rejectedTransaction.recordEnd(*rejectedFinal));
        rejectedFinal->close();
    }

    CommandList* prefixCommandLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = prefixTicket.submit(
        device,
        prefixCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());
    ASSERT_TRUE(rejectedTransaction.confirmBeginSubmission(prefixToken));

    CommandList* rejectedFinalCommandLists[] = { rejectedFinal.get() };
    {
        const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
        );
        ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        EXPECT_FALSE(finalTicket.submit(
            device,
            rejectedFinalCommandLists,
            1u,
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        ).valid());
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }

    ASSERT_TRUE(rejectedTransaction.prepareForRecovery());
    recovery->open();
    ASSERT_TRUE(rejectedTransaction.recordEnd(*recovery));
    recovery->close();
    CommandList* recoveryCommandLists[] = { recovery.get() };
    __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate submissionGate(device, graphicsQueue);
    ASSERT_TRUE(submissionGate.valid());
    QueueSubmissionToken recoveryToken;
    {
        __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate::ScopedSubmitInterception interception(
            submissionGate
        );
        ASSERT_TRUE(interception.valid());
        recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
    }
    ASSERT_TRUE(recoveryToken.valid());
    ASSERT_TRUE(submissionGate.splitSubmissionAccepted());
    ASSERT_TRUE(rejectedTransaction.confirmEndSubmission(recoveryToken, false));
    ASSERT_TRUE(submissionGate.waitUntilReady());
    EXPECT_LT(device.queueGetCompletedInstance(graphicsQueue), recoveryToken.value);
    timing.collect(device, 91u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);
    EXPECT_FALSE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    auto blockedCommandList = device.createCommandList();
    ASSERT_NE(blockedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket blockedTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(blockedTicket);
        blockedCommandList->open();
        {
            GpuTimingMeasure blockedMeasure(timing, s_FrameTransactionScope, device, *blockedCommandList);
            EXPECT_FALSE(blockedMeasure.valid());
        }
        blockedCommandList->close();
    }

    ASSERT_TRUE(submissionGate.releaseAndWait());
    EXPECT_GE(device.queueGetCompletedInstance(graphicsQueue), recoveryToken.value);
    timing.collect(device, 91u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_FrameTransactionScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].attribution, recoveryAttribution);
    EXPECT_FALSE(completedSamples.samples[0u].published);
    EXPECT_FALSE(completedSamples.samples[0u].comparableRange.valid());

    timing.beginFrame(91u);
    auto acceptedPrefix = device.createCommandList();
    auto acceptedFinal = device.createCommandList();
    ASSERT_NE(acceptedPrefix.get(), nullptr);
    ASSERT_NE(acceptedFinal.get(), nullptr);

    GpuTimingFrameTransaction acceptedTransaction(timing);
    GpuTimingSubmissionTicket acceptedPrefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedPrefixTicket);
        acceptedPrefix->open();
        ASSERT_TRUE(acceptedTransaction.begin(s_FrameTransactionScope, device, *acceptedPrefix));
        acceptedPrefix->close();
    }
    GpuTimingSubmissionTicket acceptedFinalTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedFinalTicket);
        acceptedFinal->open();
        ASSERT_TRUE(acceptedTransaction.recordEnd(*acceptedFinal));
        acceptedFinal->close();
    }

    CommandList* acceptedPrefixCommandLists[] = { acceptedPrefix.get() };
    const QueueSubmissionToken acceptedPrefixToken = acceptedPrefixTicket.submit(
        device,
        acceptedPrefixCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedPrefixToken.valid());
    ASSERT_TRUE(acceptedTransaction.confirmBeginSubmission(acceptedPrefixToken));
    CommandList* acceptedFinalCommandLists[] = { acceptedFinal.get() };
    const QueueSubmissionToken acceptedFinalToken = acceptedFinalTicket.submit(
        device,
        acceptedFinalCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedFinalToken.valid());
    ASSERT_TRUE(acceptedTransaction.confirmEndSubmission(acceptedFinalToken, true));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 92u);
    EXPECT_TRUE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

