// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "timing_preamble_test_support.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_AbandonedTimingMarkerScope("tests/timing_abandoned_marker_ownership");


inline constexpr GpuTimingScopeDefinition s_TimingStatisticsScope("tests/timing_statistics_scope");


inline constexpr GpuTimingScopeDefinition s_TimingMeasureExistingClaimClosureScope(
    "tests/timing_measure_existing_claim_closure"
);


// The global reset must precede every render pass. This probe places its timing scope inside dynamic rendering,
// where it cannot reset a newly reserved query itself; a valid sample on the next frame proves the Graphics preamble
// made the query device-ready before render-pass recording began.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleResetsTimerQueriesBeforeRenderPasses){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingPreambleScope.identity, device, 1u));

    FrameTimingPreambleProbePass probePass(graphics);
    ASSERT_TRUE(probePass.initialize());

    graphics.addRenderPassToBack(probePass);
    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    // collect() runs at the next frame open, before that frame's reset can overwrite the completed sample.
    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingPreambleScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// A snapshot must report why a valid timing request produced no scope, keep recorded/accepted/published outcomes
// distinct, and clear all generation-local counts and query capacity at the recorder reset boundary.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingStatisticsDistinguishSkipsOutcomesAndReset){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing statistics: the primary graphics queue does not support timestamps.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();

    const GpuTimingRecorderStatistics initialStatistics = timing.statistics(device);
    ASSERT_TRUE(initialStatistics.valid());
    EXPECT_EQ(initialStatistics.deviceGeneration, device.getDeviceGeneration());
    EXPECT_FALSE(initialStatistics.queryCollectionEnabled);
    EXPECT_FALSE(initialStatistics.timingSinkEnabled);
    EXPECT_FALSE(initialStatistics.feedbackCollectionEnabled);
    EXPECT_FALSE(initialStatistics.collectionActive);
    EXPECT_EQ(initialStatistics.scopeAttemptCount, 0u);
    EXPECT_EQ(initialStatistics.preparedScopeCount, 0u);

    auto inactiveCommandList = device.createCommandList();
    ASSERT_NE(inactiveCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket inactiveTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(inactiveTicket);
        inactiveCommandList->open();
        {
            GpuTimingMeasure inactiveMeasure(timing, s_TimingStatisticsScope, device, *inactiveCommandList);
            EXPECT_FALSE(inactiveMeasure.valid());
        }
        inactiveCommandList->close();
    }
    const GpuTimingRecorderStatistics inactiveStatistics = timing.statistics(device);
    EXPECT_EQ(inactiveStatistics.scopeAttemptCount, 1u);
    EXPECT_EQ(
        inactiveStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::CollectionInactive],
        1u
    );

    s_scope->setGpuTimingEnabled(true);
    auto unpreparedCommandList = device.createCommandList();
    ASSERT_NE(unpreparedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket unpreparedTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(unpreparedTicket);
        unpreparedCommandList->open();
        {
            GpuTimingMeasure unpreparedMeasure(timing, s_TimingStatisticsScope, device, *unpreparedCommandList);
            EXPECT_FALSE(unpreparedMeasure.valid());
        }
        unpreparedCommandList->close();
    }
    const GpuTimingRecorderStatistics unpreparedStatistics = timing.statistics(device);
    EXPECT_TRUE(unpreparedStatistics.queryCollectionEnabled);
    EXPECT_TRUE(unpreparedStatistics.timingSinkEnabled);
    EXPECT_TRUE(unpreparedStatistics.collectionActive);
    EXPECT_EQ(unpreparedStatistics.scopeAttemptCount, 2u);
    EXPECT_EQ(
        unpreparedStatistics.skippedScopeCountByReason[GpuTimingScopeSkipReason::ScopeNotPrepared],
        1u
    );

    ASSERT_TRUE(timing.prepareScopeQueries(s_TimingStatisticsScope.identity, device, 1u));
    const GpuTimingRecorderStatistics preparedStatistics = timing.statistics(device);
    EXPECT_EQ(preparedStatistics.preparedScopeCount, 1u);
    EXPECT_EQ(preparedStatistics.requestedQueryCount, 1u);
    EXPECT_EQ(preparedStatistics.materializedQueryCount, 1u);
    EXPECT_EQ(preparedStatistics.queryMaterializationFailureCount, 0u);

    auto discardedCommandList = device.createCommandList();
    ASSERT_NE(discardedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket discardedTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(discardedTicket);
        discardedCommandList->open();
        {
            GpuTimingMeasure discardedMeasure(timing, s_TimingStatisticsScope, device, *discardedCommandList);
            ASSERT_TRUE(discardedMeasure.valid());
            discardedMeasure.discardTiming();
        }
        discardedCommandList->close();
    }

    timing.beginFrame(240u);
    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        {
            GpuTimingMeasure acceptedMeasure(timing, s_TimingStatisticsScope, device, *acceptedCommandList);
            ASSERT_TRUE(acceptedMeasure.valid());
        }
        acceptedCommandList->close();
    }
    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    const QueueSubmissionToken acceptedToken = acceptedTicket.submit(
        device,
        acceptedCommandLists,
        LengthOf(acceptedCommandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());
    CommandList* staleDiscardedCommandLists[] = { discardedCommandList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        staleDiscardedCommandLists,
        LengthOf(staleDiscardedCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 241u);

    const GpuTimingRecorderStatistics completedStatistics = timing.statistics(device);
    EXPECT_EQ(completedStatistics.scopeAttemptCount, 4u);
    EXPECT_EQ(completedStatistics.recordedScopeCount, 2u);
    EXPECT_EQ(completedStatistics.acceptedScopeCount, 1u);
    EXPECT_EQ(completedStatistics.publishedSampleCount, 1u);
    EXPECT_EQ(completedStatistics.unpublishedSampleCount, 0u);
    EXPECT_EQ(completedStatistics.discardedScopeCount, 1u);
    EXPECT_EQ(completedStatistics.quarantinedScopeCount, 0u);
    EXPECT_EQ(completedStatistics.beginFailureCount, 0u);
    EXPECT_EQ(completedStatistics.comparableTimestampsSupported, device.supportsComparableGpuTimestamps());

    inactiveCommandList.reset();
    unpreparedCommandList.reset();
    discardedCommandList.reset();
    acceptedCommandList.reset();
    timing.resetQueries();
    const GpuTimingRecorderStatistics resetStatistics = timing.statistics(device);
    EXPECT_EQ(resetStatistics.preparedScopeCount, 0u);
    EXPECT_EQ(resetStatistics.requestedQueryCount, 0u);
    EXPECT_EQ(resetStatistics.materializedQueryCount, 0u);
    EXPECT_EQ(resetStatistics.scopeAttemptCount, 0u);
    EXPECT_EQ(resetStatistics.recordedScopeCount, 0u);
    EXPECT_EQ(resetStatistics.acceptedScopeCount, 0u);
    EXPECT_EQ(resetStatistics.publishedSampleCount, 0u);
    EXPECT_EQ(resetStatistics.discardedScopeCount, 0u);

    s_scope->setGpuTimingEnabled(false);
}


// Ordinary RAII closure writes the end timestamp through the opening command list's pre-existing claim. It must
// neither grow command-buffer storage after begin nor leave the native marker open, and the ticket still publishes
// the completed sample through the ordinary submission path.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingMeasureDestructorClosesExistingClaimWithoutGrowingStorage){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(queueInfo, nullptr);
    if(queueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing measure existing claim: primary Graphics queue does not support timestamps.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimingMeasureExistingClaimClosureScope.identity, device, 1u));
    timing.beginFrame(252u);

    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket ticket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(ticket);
        commandList->open();
        const usize claimCountBeforeBegin = GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(
            *commandList
        );
        const usize retainedResourceCountBeforeBegin = GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(
            *commandList
        );
        const u32 markerDepthBeforeBegin = GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList);

        Optional<GpuTimingMeasure> timingMeasure;
        timingMeasure.emplace(timing, s_TimingMeasureExistingClaimClosureScope, device, *commandList);
        ASSERT_TRUE(timingMeasure.value().valid());
        const usize claimCountAfterBegin = GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(
            *commandList
        );
        const usize retainedResourceCountAfterBegin = GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(
            *commandList
        );
        ASSERT_GT(claimCountAfterBegin, claimCountBeforeBegin);
        ASSERT_GT(retainedResourceCountAfterBegin, retainedResourceCountBeforeBegin);
        EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), markerDepthBeforeBegin + 1u);

        timingMeasure.reset();
        EXPECT_EQ(
            GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(*commandList),
            claimCountAfterBegin
        );
        EXPECT_EQ(
            GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(*commandList),
            retainedResourceCountAfterBegin
        );
        EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), markerDepthBeforeBegin);
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
    }

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = ticket.submit(
        device,
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 253u);
    EXPECT_TRUE(timingSink.stats(s_TimingMeasureExistingClaimClosureScope.identity).valid());

    commandList.reset();
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(false);
}


// Callback-free marker abandonment closes the native marker before CommandList::close(), so neither command-list
// recovery nor the later timing destructor has an unmatched marker to diagnose.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingMeasureRelinquishesMarkerToCommandListRecovery){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    ASSERT_TRUE(s_logger.has_value());
    // Other domains intentionally recover markers. Capture this scenario independently of the suite's log history.
    CapturingLogger recoveryLogger;
    Common::LoggerRegistrationGuard recoveryLoggerGuard(recoveryLogger);
    EXPECT_FALSE(recoveryLogger.sawMessageContaining(NWB_TEXT("Ignoring an unmatched command-list marker end")));
#if !defined(NWB_FINAL)
    const u32 messageCountBeforeRecovery = recoveryLogger.messageCount();
#endif

    CommandListHandle commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket timingTicket(timing);
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open();
        {
            GpuTimingMeasure timingMeasure(timing, s_AbandonedTimingMarkerScope, device, *commandList);
            EXPECT_FALSE(timingMeasure.valid());
            timingMeasure.discardTiming();
            timingMeasure.abandonMarker();
            commandList->close();
        }
    }

    ASSERT_TRUE(commandList->hasCommandBuffer());
    EXPECT_FALSE(commandList->isRecording());
#if !defined(NWB_FINAL)
    EXPECT_EQ(recoveryLogger.messageCount(), messageCountBeforeRecovery);
    EXPECT_FALSE(recoveryLogger.sawMessageContaining(NWB_TEXT("Recovering 1 unterminated command-list marker scope(s)")));
#endif
    EXPECT_FALSE(recoveryLogger.sawMessageContaining(NWB_TEXT("Ignoring an unmatched command-list marker end")));

    commandList->open();
    commandList->beginMarker("tests/timing_abandoned_marker_ownership/reused");
    commandList->endMarker();
    commandList->close();
    CommandList* commandLists[] = { commandList.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
    commandList.reset();
    EXPECT_FALSE(recoveryLogger.sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
        << "validation-enabled marker recovery must not emit a Vulkan severity=error message";
}


TEST_F(DescriptorBufferRoundTripTest, GpuTimingMeasureRefusesToCloseAnotherOwnersTopMarker){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    GpuTimingSubmissionTicket timingTicket(timing);
    GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
    commandList->open();
    commandList->beginMarker("tests/timing_marker_exact_owner/outer");
    ASSERT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
    {
        GpuTimingMeasure timingMeasure(timing, s_AbandonedTimingMarkerScope, device, *commandList);
        EXPECT_FALSE(timingMeasure.valid());
        ASSERT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 2u);

        commandList->endMarker();
        ASSERT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
        EXPECT_FALSE(timingMeasure.finishMarker());
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
        timingMeasure.discardTiming();
    }
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 0u);
}


TEST_F(DescriptorBufferRoundTripTest, GpuTimingMeasureStaleMarkerLeaseCannotCloseReusedRecordingMarker){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    GpuTimingSubmissionTicket timingTicket(timing);
    GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
    commandList->open();
    Optional<GpuTimingMeasure> timingMeasure;
    timingMeasure.emplace(timing, s_AbandonedTimingMarkerScope, device, *commandList);
    EXPECT_FALSE(timingMeasure.value().valid());
    ASSERT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    commandList->open();
    commandList->beginMarker("tests/timing_marker_stale_lease/reused");
    ASSERT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
    EXPECT_FALSE(timingMeasure.value().finishMarker());
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 1u);
    timingMeasure.value().discardTiming();
    timingMeasure.reset();
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::markerDepth(*commandList), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

