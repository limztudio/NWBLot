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


inline constexpr GpuTimingScopeDefinition s_AuxiliaryCompletionScope("tests/timing_auxiliary_completion");


// The backend result must carry the exact physical queue family's native timestamp width and preserve raw ticks
// until modular duration conversion. This is the real Vulkan complement to the synthetic wrap-math unit cases.
TEST_F(DescriptorBufferRoundTripTest, TimerQueryResultCarriesNativePhysicalQueueWidth){
    auto& nativeDevice = device();
    const GpuPhysicalQueueId graphicsQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const queueInfo = nativeDevice.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(queueInfo, nullptr);
    if(queueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer query result: primary Graphics queue does not support timestamps.";

    auto query = nativeDevice.createTimerQuery();
    ASSERT_NE(query.get(), nullptr);
    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto commandList = nativeDevice.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);

    commandList->open();
    TimerQueryRecordingToken queryRecording;
    ASSERT_TRUE(commandList->beginTimerQuery(query.get(), queryRecording));
    ASSERT_TRUE(commandList->endTimerQuery(query.get(), queryRecording));
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = nativeDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(nativeDevice.waitForIdle());

    TimerQueryResult result;
    ASSERT_TRUE(nativeDevice.getTimerQueryResult(query.get(), result));
    EXPECT_TRUE(result.valid());
    EXPECT_EQ(result.timestampValidBits, queueInfo->timestampValidBits);
    EXPECT_EQ(result.physicalQueue, graphicsQueue);
    EXPECT_GT(result.secondsPerTick, 0.0);
    EXPECT_GE(result.durationSeconds(), 0.0);
    EXPECT_EQ(
        result.comparableAcrossSubmissions,
        nativeDevice.supportsComparableGpuTimestamps(graphicsQueue)
    );
    EXPECT_EQ(
        result.hasComparableRange(),
        result.comparableAcrossSubmissions && result.beginTicks <= result.endTicks
    );
}


// The lifetime endpoint may consume only the claim retained by beginTimerQuery() on the same native recording.
// A foreign recording must fail without appending either claim or retained-resource storage.
TEST_F(DescriptorBufferRoundTripTest, TimerQueryExistingClaimEndNeverAppendsForeignCommandListStorage){
    auto& nativeDevice = device();
    const GpuPhysicalQueueId graphicsQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const queueInfo = nativeDevice.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(queueInfo, nullptr);
    if(queueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer query existing claim: primary Graphics queue does not support timestamps.";

    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto openingCommandList = nativeDevice.createCommandList(parameters);
    auto foreignCommandList = nativeDevice.createCommandList(parameters);
    auto query = nativeDevice.createTimerQuery();
    ASSERT_NE(openingCommandList.get(), nullptr);
    ASSERT_NE(foreignCommandList.get(), nullptr);
    ASSERT_NE(query.get(), nullptr);

    openingCommandList->open();
    foreignCommandList->open();
    TimerQueryRecordingToken recording;
    ASSERT_TRUE(openingCommandList->beginTimerQuery(query.get(), recording));

    const usize openingClaimCount = GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(
        *openingCommandList
    );
    const usize openingRetainedResourceCount = GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(
        *openingCommandList
    );
    ASSERT_GT(openingClaimCount, 0u);
    ASSERT_GT(openingRetainedResourceCount, 0u);
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(*foreignCommandList), 0u);
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(*foreignCommandList), 0u);

    EXPECT_FALSE(foreignCommandList->endTimerQueryFromExistingClaim(query.get(), recording));
    EXPECT_TRUE(foreignCommandList->commandRecordingFailed());
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(*foreignCommandList), 0u);
    EXPECT_EQ(GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(*foreignCommandList), 0u);

    ASSERT_TRUE(openingCommandList->endTimerQueryFromExistingClaim(query.get(), recording));
    EXPECT_EQ(
        GraphicsBackend::VulkanTestDispatchAccess::currentTimerQueryRecordingClaimCount(*openingCommandList),
        openingClaimCount
    );
    EXPECT_EQ(
        GraphicsBackend::VulkanTestDispatchAccess::currentRetainedResourceCount(*openingCommandList),
        openingRetainedResourceCount
    );
    foreignCommandList->close();
    openingCommandList->close();
    EXPECT_FALSE(foreignCommandList->hasCommandBuffer());

    CommandList* const commandLists[] = { openingCommandList.get() };
    ASSERT_TRUE(nativeDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(nativeDevice.waitForIdle());

    TimerQueryResult result;
    ASSERT_TRUE(nativeDevice.getTimerQueryResult(query.get(), result));
    EXPECT_TRUE(result.valid());
}


// Even same-family queues have distinct execution timelines. A query begun on one exact VkQueue must reject an end
// recorded on another, while the original queue remains able to complete and publish the same reservation.
TEST_F(DescriptorBufferRoundTripTest, TimerQueryRejectsDifferentExactPhysicalQueueEnd){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Timer query exact queue: no usable multi-queue headless Vulkan device on this host.";

    auto& nativeDevice = multiQueueScope.graphics().getDevice();
    const GpuPhysicalQueueTopology topology = nativeDevice.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const primaryInfo = nativeDevice.getPhysicalQueueInfo(primaryQueue);
    ASSERT_NE(primaryInfo, nullptr);
    if(primaryInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer query exact queue: primary Graphics queue does not support timestamps.";

    const GpuPhysicalQueueInfo* secondaryInfo = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != primaryQueue
            && candidate.familyIndex == primaryInfo->familyIndex
            && candidate.timestampValidBits != 0u
        ){
            secondaryInfo = &candidate;
            break;
        }
    }
    if(!secondaryInfo)
        GTEST_SKIP() << "Timer query exact queue: adapter exposes only one timestamp-capable queue in the Graphics family.";

    CommandListParameters primaryParameters;
    primaryParameters.setPhysicalQueue(primaryQueue);
    CommandListParameters secondaryParameters;
    secondaryParameters.setPhysicalQueue(secondaryInfo->id);
    auto primaryCommandList = nativeDevice.createCommandList(primaryParameters);
    auto secondaryCommandList = nativeDevice.createCommandList(secondaryParameters);
    auto query = nativeDevice.createTimerQuery();
    ASSERT_NE(primaryCommandList.get(), nullptr);
    ASSERT_NE(secondaryCommandList.get(), nullptr);
    ASSERT_NE(query.get(), nullptr);

    primaryCommandList->open();
    secondaryCommandList->open();
    TimerQueryRecordingToken queryRecording;
    ASSERT_TRUE(primaryCommandList->beginTimerQuery(query.get(), queryRecording));
    EXPECT_FALSE(secondaryCommandList->endTimerQuery(query.get(), queryRecording));
    ASSERT_TRUE(primaryCommandList->endTimerQuery(query.get(), queryRecording));
    secondaryCommandList->close();
    primaryCommandList->close();

    CommandList* const commandLists[] = { primaryCommandList.get() };
    ASSERT_TRUE(nativeDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        primaryQueue,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(nativeDevice.waitForIdle());

    TimerQueryResult result;
    ASSERT_TRUE(nativeDevice.getTimerQueryResult(query.get(), result));
    EXPECT_EQ(result.timestampValidBits, primaryInfo->timestampValidBits);
    EXPECT_EQ(result.physicalQueue, primaryQueue);
}


#if !defined(NWB_FINAL)

// Completion ownership follows the exact auxiliary queue token, not its broad Graphics class. Finish the auxiliary
// command buffer while gating its tracking signal, then prove a completed primary-queue lookalike cannot publish it.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingCompletionTracksExactAuxiliaryPhysicalQueue){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "GPU timing auxiliary completion: no usable multi-queue headless Vulkan device on this host.";

    auto& graphics = multiQueueScope.graphics();
    auto& nativeDevice = graphics.getDevice();
    const GpuPhysicalQueueTopology topology = nativeDevice.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* secondaryInfo = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != primaryQueue
            && candidate.queueClass == CommandQueue::Graphics
            && candidate.timestampValidBits != 0u
        ){
            secondaryInfo = &candidate;
            break;
        }
    }
    if(!secondaryInfo)
        GTEST_SKIP() << "GPU timing auxiliary completion: adapter exposes no auxiliary timestamp-capable Graphics queue.";

    __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate submissionGate(
        nativeDevice,
        secondaryInfo->id
    );
    ASSERT_TRUE(submissionGate.valid());

    auto& timing = graphics.gpuTiming();
    multiQueueScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AuxiliaryCompletionScope.identity, nativeDevice, 1u));
    timing.beginFrame(240u);
    GpuTimingSampleCapture completedSamples;
    ScopedGpuTimingSampleListener sampleListener(timing, completedSamples);
    ASSERT_TRUE(sampleListener.valid());
    const GpuTimingSampleAttribution auxiliaryAttribution = timing.allocateSampleAttribution();
    ASSERT_TRUE(auxiliaryAttribution.valid());

    CommandListParameters parameters;
    parameters.setPhysicalQueue(secondaryInfo->id);
    auto commandList = nativeDevice.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket ticket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(ticket);
        commandList->open();
        {
            GpuTimingMeasure measure(
                timing,
                s_AuxiliaryCompletionScope,
                nativeDevice,
                *commandList,
                auxiliaryAttribution
            );
            ASSERT_TRUE(measure.valid());
        }
        commandList->close();
    }
    CommandList* commandLists[] = { commandList.get() };
    QueueSubmissionToken token;
    {
        __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate::ScopedSubmitInterception interception(
            submissionGate
        );
        ASSERT_TRUE(interception.valid());
        token = ticket.submit(
            nativeDevice,
            commandLists,
            LengthOf(commandLists),
            secondaryInfo->id,
            QueueSubmissionDesc{}
        );
    }
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(token.matchesPhysicalQueue(secondaryInfo->id.index, secondaryInfo->id.deviceGeneration));
    ASSERT_TRUE(submissionGate.splitSubmissionAccepted());
    ASSERT_TRUE(submissionGate.waitUntilReady());
    EXPECT_LT(nativeDevice.queueGetCompletedInstance(secondaryInfo->id), token.value);

    timing.collect(nativeDevice, 241u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);

    QueueSubmissionDesc forcedPrimarySubmission;
    forcedPrimarySubmission.forceNativeSubmission = true;
    QueueSubmissionToken primaryToken;
    do{
        primaryToken = nativeDevice.executeCommandLists(nullptr, 0u, primaryQueue, forcedPrimarySubmission);
        ASSERT_TRUE(primaryToken.valid());
    }while(primaryToken.value < token.value);
    GraphicsBackend::Queue* const primaryNativeQueue = nativeDevice.getQueue(primaryQueue);
    ASSERT_NE(primaryNativeQueue, nullptr);
    primaryNativeQueue->waitForIdle();
    EXPECT_GE(nativeDevice.queueGetCompletedInstance(primaryQueue), token.value);
    EXPECT_LT(nativeDevice.queueGetCompletedInstance(secondaryInfo->id), token.value);
    timing.collect(nativeDevice, 241u);
    EXPECT_EQ(completedSamples.sampleCount, 0u);

    ASSERT_TRUE(submissionGate.releaseAndWait());
    EXPECT_GE(nativeDevice.queueGetCompletedInstance(secondaryInfo->id), token.value);
    timing.collect(nativeDevice, 241u);
    ASSERT_EQ(completedSamples.sampleCount, 1u);
    EXPECT_EQ(completedSamples.samples[0u].scopeName, s_AuxiliaryCompletionScope.identity);
    EXPECT_EQ(completedSamples.samples[0u].attribution, auxiliaryAttribution);
    EXPECT_TRUE(completedSamples.samples[0u].published);
    if(nativeDevice.supportsComparableGpuTimestamps(secondaryInfo->id)){
        EXPECT_TRUE(completedSamples.samples[0u].comparableRange.valid());
        EXPECT_EQ(completedSamples.samples[0u].comparableRange.physicalQueue, secondaryInfo->id);
    }
    else
        EXPECT_FALSE(completedSamples.samples[0u].comparableRange.valid());

    multiQueueScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


#if !defined(NWB_FINAL)

// A pool reset accepted on the primary queue must become an explicit prerequisite when the reserved query records
// on another physical Graphics queue. Gate the reset token, then inspect and exercise the auxiliary native wait.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingDirectSubmissionWaitsForCrossQueueFrameReset){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "GPU timing reset prerequisite: no usable multi-queue headless Vulkan device on this host.";

    auto& graphics = multiQueueScope.graphics();
    auto& nativeDevice = graphics.getDevice();
    const GpuPhysicalQueueId primaryQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueTopology topology = nativeDevice.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* secondaryInfo = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != primaryQueue
            && candidate.queueClass == CommandQueue::Graphics
            && candidate.timestampValidBits != 0u
        ){
            secondaryInfo = &candidate;
            break;
        }
    }
    if(!secondaryInfo)
        GTEST_SKIP() << "GPU timing reset prerequisite: adapter exposes no auxiliary timestamp-capable Graphics queue.";

    auto& timing = graphics.gpuTiming();
    multiQueueScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AuxiliaryCompletionScope.identity, nativeDevice, 1u));
    timing.beginFrame(242u);

    auto resetCommandList = nativeDevice.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate resetGate(nativeDevice, primaryQueue);
    ASSERT_TRUE(resetGate.valid());
    QueueSubmissionToken resetToken;
    {
        __hidden_descriptor_buffer_round_trip_tests::VulkanSubmissionTailGate::ScopedSubmitInterception interception(
            resetGate
        );
        ASSERT_TRUE(interception.valid());
        resetToken = nativeDevice.executeCommandLists(
            resetCommandLists,
            LengthOf(resetCommandLists),
            primaryQueue,
            QueueSubmissionDesc{}
        );
    }
    ASSERT_TRUE(resetToken.valid());
    ASSERT_TRUE(resetGate.splitSubmissionAccepted());
    timing.confirmFrameReset(resetToken);
    ASSERT_TRUE(resetGate.waitUntilReady());
    EXPECT_LT(nativeDevice.queueGetCompletedInstance(primaryQueue), resetToken.value);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(secondaryInfo->id);
    auto commandList = nativeDevice.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket ticket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(ticket);
        commandList->open();
        {
            GpuTimingMeasure measure(timing, s_AuxiliaryCompletionScope, nativeDevice, *commandList);
            ASSERT_TRUE(measure.valid());
        }
        commandList->close();
    }

    VulkanTestQueueSubmit2Observer submissionObserver(nativeDevice);
    ASSERT_TRUE(submissionObserver.valid());
    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = ticket.submit(
        nativeDevice,
        commandLists,
        LengthOf(commandLists),
        secondaryInfo->id,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 1u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 1u);
    VulkanTestQueueSubmit2Capture capture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(0u, capture));
    ASSERT_EQ(capture.result, VK_SUCCESS);
    ASSERT_FALSE(capture.overflowed);
    ASSERT_EQ(capture.submitCount, 1u);
    ASSERT_EQ(capture.submits[0u].waitCount, 1u);
    EXPECT_EQ(capture.submits[0u].waits[0u].value, resetToken.value);
    EXPECT_LT(nativeDevice.queueGetCompletedInstance(secondaryInfo->id), token.value);

    ASSERT_TRUE(resetGate.releaseAndWait());
    ASSERT_TRUE(nativeDevice.waitForIdle());
    EXPECT_GE(nativeDevice.queueGetCompletedInstance(secondaryInfo->id), token.value);
    timing.collect(nativeDevice, 243u);
    const GpuTimingRecorderStatistics statistics = timing.statistics(nativeDevice);
    EXPECT_EQ(statistics.recordedScopeCount, 1u);
    EXPECT_EQ(statistics.acceptedScopeCount, 1u);
    EXPECT_EQ(statistics.quarantinedScopeCount, 0u);

    multiQueueScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


TEST_F(DescriptorBufferRoundTripTest, TimerQueryUsesExternalResetForTimestampCapableExactTransferQueue){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Timer-query exact capability validation: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& graphics = transferScope.graphics();
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* transferOnlyQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        const u8 capabilityBits = static_cast<u8>(candidate.capabilities);
        if(
            candidate.queueClass == CommandQueue::Transfer
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) == 0u
        ){
            transferOnlyQueue = &candidate;
            break;
        }
    }
    if(!transferOnlyQueue)
        GTEST_SKIP() << "Timer-query exact capability validation: adapter exposes no physical Transfer-only queue.";
    if(transferOnlyQueue->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer-query exact capability validation: Transfer-only queue exposes no timestamp bits.";

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer-query exact capability validation: Graphics reset queue exposes no timestamp bits.";

    auto query = device.createTimerQuery();
    ASSERT_NE(query.get(), nullptr);
    CommandListParameters transferParameters;
    transferParameters.setPhysicalQueue(transferOnlyQueue->id);

    auto resetCommandList = device.createCommandList(transferParameters);
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    EXPECT_FALSE(resetCommandList->canResetTimerQueryHere());
    EXPECT_FALSE(resetCommandList->resetTimerQuery(query.get()));
    EXPECT_TRUE(resetCommandList->commandRecordingFailed());
    resetCommandList->close();
    EXPECT_FALSE(resetCommandList->hasCommandBuffer());

    auto beginCommandList = device.createCommandList(transferParameters);
    ASSERT_NE(beginCommandList.get(), nullptr);
    beginCommandList->open();
    TimerQueryRecordingToken rejectedTransferQueryRecording;
    EXPECT_FALSE(beginCommandList->beginTimerQuery(query.get(), rejectedTransferQueryRecording));
    EXPECT_TRUE(beginCommandList->commandRecordingFailed());
    beginCommandList->close();
    EXPECT_FALSE(beginCommandList->hasCommandBuffer());

    CommandListParameters graphicsParameters;
    graphicsParameters.setPhysicalQueue(graphicsQueue);
    auto externalResetCommandList = device.createCommandList(graphicsParameters);
    ASSERT_TRUE(externalResetCommandList);
    externalResetCommandList->open();
    ASSERT_TRUE(externalResetCommandList->canResetTimerQueryHere());
    ASSERT_TRUE(externalResetCommandList->resetTimerQuery(query.get()));
    externalResetCommandList->close();
    CommandList* const externalResetCommandLists[] = { externalResetCommandList.get() };
    const QueueSubmissionToken externalResetToken = device.executeCommandLists(
        externalResetCommandLists,
        LengthOf(externalResetCommandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(externalResetToken.valid());

    auto transferQueryCommandList = device.createCommandList(transferParameters);
    ASSERT_TRUE(transferQueryCommandList);
    transferQueryCommandList->open();
    EXPECT_FALSE(transferQueryCommandList->canResetTimerQueryHere());
    TimerQueryRecordingToken transferQueryRecording;
    ASSERT_TRUE(transferQueryCommandList->beginTimerQuery(query.get(), transferQueryRecording));
    ASSERT_TRUE(transferQueryCommandList->endTimerQuery(query.get(), transferQueryRecording));
    transferQueryCommandList->close();
    CommandList* const transferQueryCommandLists[] = { transferQueryCommandList.get() };
    const QueueSubmissionToken transferQueryToken = device.executeCommandLists(
        transferQueryCommandLists,
        LengthOf(transferQueryCommandLists),
        transferOnlyQueue->id,
        QueueSubmissionDesc().setWaitTokens(&externalResetToken, 1u)
    );
    ASSERT_TRUE(transferQueryToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    TimerQueryResult transferResult;
    ASSERT_TRUE(device.getTimerQueryResult(query.get(), transferResult));
    EXPECT_TRUE(transferResult.valid());
    EXPECT_EQ(transferResult.physicalQueue, transferOnlyQueue->id);
    EXPECT_EQ(transferResult.timestampValidBits, transferOnlyQueue->timestampValidBits);

    auto& timing = graphics.gpuTiming();
    auto& timingSink = transferScope.gpuTimingSink();
    transferScope.setGpuTimingEnabled(false);
    timing.resetQueries();
    transferScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_TimerQueryUnsupportedQueueScope.identity, device, 1u));
    timing.beginFrame(251u);

    auto timingResetCommandList = device.createCommandList(graphicsParameters);
    ASSERT_TRUE(timingResetCommandList);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* const timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    GpuTimingFrameTransaction transaction(timing);
    GpuTimingSubmissionTicket ticket(timing);
    auto transferTimingCommandList = device.createCommandList(transferParameters);
    ASSERT_TRUE(transferTimingCommandList);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(ticket);
        transferTimingCommandList->open();
        EXPECT_FALSE(transferTimingCommandList->canResetTimerQueryHere());
        ASSERT_TRUE(transaction.begin(s_TimerQueryUnsupportedQueueScope, device, *transferTimingCommandList));
        EXPECT_TRUE(transaction.needsRetirement());
        ASSERT_TRUE(transaction.recordEnd(*transferTimingCommandList));
        EXPECT_FALSE(transferTimingCommandList->commandRecordingFailed());
        transferTimingCommandList->close();
    }
    ASSERT_TRUE(transferTimingCommandList->hasCommandBuffer());
    CommandList* const transferTimingCommandLists[] = { transferTimingCommandList.get() };
    const QueueSubmissionToken transferTimingToken = ticket.submit(
        device,
        transferTimingCommandLists,
        LengthOf(transferTimingCommandLists),
        transferOnlyQueue->id,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(transferTimingToken.valid());
    ASSERT_TRUE(transaction.confirmBeginSubmission(transferTimingToken));
    ASSERT_TRUE(transaction.confirmEndSubmission(transferTimingToken, true));
    EXPECT_FALSE(transaction.needsRetirement());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 252u);
    EXPECT_TRUE(timingSink.stats(s_TimerQueryUnsupportedQueueScope.identity).valid());

    transferScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

