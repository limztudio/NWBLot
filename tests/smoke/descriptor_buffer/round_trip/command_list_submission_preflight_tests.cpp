// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "recording_capability_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NativePacketSubmissionLeaseMutationContext{
    CommandList* commandList = nullptr;
    QueueSubmissionNativeSignal signal;
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool ReplaceNativePacketSubmissionLease(
    void* const rawContext,
    const u64,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    NativePacketSubmissionLeaseMutationContext* const context =
        static_cast<NativePacketSubmissionLeaseMutationContext*>(rawContext)
    ;
    if(!context || !context->commandList || !executionQueue.valid() || !context->signal.valid())
        return false;

    ++context->invocationCount;
    context->commandList->close();
    context->commandList->open();
    context->commandList->close();
    outSignal = context->signal;
    return true;
}


TEST_F(DescriptorBufferRoundTripTest, NativeRecordingScopeRejectsStateMarkerAndQueryCommandsOutsideOpenLists){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);

    auto beforeOpen = device.createCommandList();
    ASSERT_TRUE(beforeOpen);
    beforeOpen->setBufferState(buffer.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(beforeOpen->commandRecordingFailed());
    EXPECT_FALSE(beforeOpen->hasCommandBuffer());
    beforeOpen->open();
    ASSERT_TRUE(beforeOpen->isRecording());
    EXPECT_FALSE(beforeOpen->commandRecordingFailed());
    beforeOpen->close();
    CommandList* const beforeOpenLists[]{ beforeOpen.get() };
    EXPECT_TRUE(device.executeCommandLists(
        beforeOpenLists,
        LengthOf(beforeOpenLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());

    auto afterCloseState = device.createCommandList();
    ASSERT_TRUE(afterCloseState);
    afterCloseState->open();
    afterCloseState->close();
    ASSERT_TRUE(afterCloseState->hasCommandBuffer());
    afterCloseState->setBufferState(buffer.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(afterCloseState->commandRecordingFailed());
    afterCloseState->close();
    EXPECT_FALSE(afterCloseState->hasCommandBuffer());

    auto afterCloseMarker = device.createCommandList();
    ASSERT_TRUE(afterCloseMarker);
    afterCloseMarker->open();
    afterCloseMarker->close();
    afterCloseMarker->beginMarker("tests/descriptor_buffer/marker_after_close");
    EXPECT_TRUE(afterCloseMarker->commandRecordingFailed());
    afterCloseMarker->close();
    EXPECT_FALSE(afterCloseMarker->hasCommandBuffer());

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(queueInfo, nullptr);
    if(queueInfo->timestampValidBits != 0u){
        auto query = device.createTimerQuery();
        ASSERT_TRUE(query);
        auto afterCloseBeginQuery = device.createCommandList();
        auto afterCloseEndQuery = device.createCommandList();
        ASSERT_TRUE(afterCloseBeginQuery);
        ASSERT_TRUE(afterCloseEndQuery);

        afterCloseBeginQuery->open();
        afterCloseBeginQuery->close();
        TimerQueryRecordingToken afterCloseQueryRecording;
        EXPECT_FALSE(afterCloseBeginQuery->beginTimerQuery(query.get(), afterCloseQueryRecording));
        EXPECT_TRUE(afterCloseBeginQuery->commandRecordingFailed());
        afterCloseBeginQuery->close();
        EXPECT_FALSE(afterCloseBeginQuery->hasCommandBuffer());

        afterCloseEndQuery->open();
        afterCloseEndQuery->close();
        EXPECT_FALSE(afterCloseEndQuery->endTimerQuery(query.get(), afterCloseQueryRecording));
        EXPECT_TRUE(afterCloseEndQuery->commandRecordingFailed());
        afterCloseEndQuery->close();
        EXPECT_FALSE(afterCloseEndQuery->hasCommandBuffer());
    }
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, SubmissionPreflightRejectsInvalidCommandListsBeforeHookInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    NativePacketSubmissionHookObserver hookObserver;
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookObserver,
            .invoke = RejectNativePacketSubmissionHook,
        },
    };

    CommandList* const nullLists[]{ nullptr };
    EXPECT_FALSE(device.executeCommandLists(
        nullLists,
        LengthOf(nullLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);

    auto neverOpenedList = device.createCommandList();
    ASSERT_TRUE(neverOpenedList);
    ASSERT_FALSE(neverOpenedList->hasCommandBuffer());
    CommandList* const neverOpenedLists[]{ neverOpenedList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        neverOpenedLists,
        LengthOf(neverOpenedLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);

    auto openList = device.createCommandList();
    ASSERT_TRUE(openList);
    openList->open();
    CommandList* const openLists[]{ openList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        openLists,
        LengthOf(openLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(openList->hasCommandBuffer());
    openList->close();
    EXPECT_TRUE(device.executeCommandLists(
        openLists,
        LengthOf(openLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    ).valid());

    auto stickyList = device.createCommandList();
    ASSERT_TRUE(stickyList);
    stickyList->open();
    stickyList->close();
    constexpr u32 s_InvalidPushConstant = 0xb98174c3u;
    stickyList->setPushConstants(&s_InvalidPushConstant, sizeof(s_InvalidPushConstant));
    ASSERT_TRUE(stickyList->commandRecordingFailed());
    CommandList* const stickyLists[]{ stickyList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        stickyLists,
        LengthOf(stickyLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    stickyList->close();

    auto duplicateList = device.createCommandList();
    ASSERT_TRUE(duplicateList);
    duplicateList->open();
    duplicateList->close();
    CommandList* const duplicateLists[]{ duplicateList.get(), duplicateList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        duplicateLists,
        LengthOf(duplicateLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(duplicateList->hasCommandBuffer());
    CommandList* const singleDuplicateList[]{ duplicateList.get() };
    EXPECT_TRUE(device.executeCommandLists(
        singleDuplicateList,
        LengthOf(singleDuplicateList),
        graphicsQueue,
        QueueSubmissionDesc{}
    ).valid());

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    auto foreignList = foreignDevice.createCommandList();
    ASSERT_TRUE(foreignList);
    foreignList->open();
    foreignList->close();
    CommandList* const foreignLists[]{ foreignList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        foreignLists,
        LengthOf(foreignLists),
        graphicsQueue,
        hookedSubmission
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(foreignList->hasCommandBuffer());
    EXPECT_TRUE(foreignDevice.executeCommandLists(
        foreignLists,
        LengthOf(foreignLists),
        foreignList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(foreignDevice.waitForIdle());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    const GpuPhysicalQueueInfo* alternateQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        if(topology.queues[queueIndex].id != graphicsQueue){
            alternateQueue = &topology.queues[queueIndex];
            break;
        }
    }
    if(alternateQueue){
        CommandListParameters alternateParameters;
        alternateParameters.setPhysicalQueue(alternateQueue->id);
        auto wrongQueueList = device.createCommandList(alternateParameters);
        ASSERT_TRUE(wrongQueueList);
        wrongQueueList->open();
        wrongQueueList->close();
        CommandList* const wrongQueueLists[]{ wrongQueueList.get() };
        EXPECT_FALSE(device.executeCommandLists(
            wrongQueueLists,
            LengthOf(wrongQueueLists),
            graphicsQueue,
            hookedSubmission
        ).valid());
        EXPECT_EQ(hookObserver.invocationCount, 0u);
        EXPECT_TRUE(wrongQueueList->hasCommandBuffer());
        EXPECT_TRUE(device.executeCommandLists(
            wrongQueueLists,
            LengthOf(wrongQueueLists),
            alternateQueue->id,
            QueueSubmissionDesc{}
        ).valid());
    }
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, SubmissionHookRecordingLeaseReplacementCannotAdvanceTimeline){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.waitForIdle());
    const u64 completedBeforeRejectedHook = device.queueGetCompletedInstance(graphicsQueue);

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    VulkanTestBinarySemaphore signal(device);
    ASSERT_TRUE(signal.valid());
    NativePacketSubmissionLeaseMutationContext hookContext{
        .commandList = commandList.get(),
        .signal = signal.nativeSignal(),
    };
    const QueueSubmissionDesc submissionDesc{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = ReplaceNativePacketSubmissionLease,
        },
    };
    CommandList* const commandLists[]{ commandList.get() };
    const QueueSubmissionToken rejectedToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        submissionDesc
    );
    if(rejectedToken.valid()){
        const bool idle = device.waitForIdle();
        ASSERT_TRUE(idle);
        FAIL() << "submission hook lease replacement unexpectedly reached the native queue";
    }
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(device.queueGetCompletedInstance(graphicsQueue), completedBeforeRejectedHook);
    const QueueSubmissionToken acceptedToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, HostTimerResetRejectsForeignDeviceQueryOwnership){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto query = device.createTimerQuery();
    ASSERT_TRUE(query);

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    EXPECT_FALSE(foreignDevice.resetTimerQuery(query.get()));
    EXPECT_TRUE(device.resetTimerQuery(query.get()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

