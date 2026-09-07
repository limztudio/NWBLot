// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The public standalone boundary normally owns no renderer tail.  This probe makes a Transfer packet accept before
// a dependent Graphics packet is rejected, then observes the standalone helper's recovery frontier submission.
struct StandaloneGraphAcceptedFrontierRecoveryCompletionTask{
    struct Payload{
        bool* recorded = nullptr;
        bool* accepted = nullptr;
        bool* discarded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(payload.recorded)
            *payload.recorded = true;
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.accepted)
            *payload.accepted = token.valid();
    }

    static void discarded(Payload& payload){
        if(payload.discarded)
            *payload.discarded = true;
    }
};


struct StandaloneGraphAcceptedFrontierRecoveryContext{
    BufferHandle destination;
    const void* bytes = nullptr;
    usize byteCount = 0u;
    QueueSubmissionToken* acceptedTransferToken = nullptr;
    bool* graphicsRecorded = nullptr;
    bool* graphicsAccepted = nullptr;
    bool* graphicsDiscarded = nullptr;
};


[[nodiscard]] static GpuTaskId DeclareStandaloneGraphAcceptedFrontierRecovery(
    void* const rawContext,
    GpuTaskGraph& graph
){
    StandaloneGraphAcceptedFrontierRecoveryContext* const context =
        static_cast<StandaloneGraphAcceptedFrontierRecoveryContext*>(rawContext)
    ;
    if(
        !context
        || !context->destination
        || !context->bytes
        || context->byteCount == 0u
    )
        return {};

    const GpuGraphResourceId destination = graph.importBuffer(
        context->destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests.standalone_graph_accepted_frontier_recovery.destination"))
            .setMarkerLabel("Standalone Accepted Frontier Recovery Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    const GpuUploadBlobId source = graph.copyUploadData(context->bytes, context->byteCount, alignof(u32));
    if(!destination.valid() || !source.valid())
        return {};

    GpuTaskSchedulingHint packetScheduling;
    packetScheduling.cost = GpuTaskCostHint::Tiny;
    packetScheduling.overlapPreferred = false;
    packetScheduling.forceSubmissionBoundary = true;
    packetScheduling.allowPacketMerge = false;
    const GpuQueueRequest transferRequest{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        false,
        false,
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId transferTask = graph.addUploadBufferTask(
        GpuTaskDesc{}
            .setIdentity(Name("tests.standalone_graph_accepted_frontier_recovery.transfer"))
            .setMarkerLabel("Standalone Accepted Frontier Recovery Transfer")
            .setQueue(transferRequest)
            .setScheduling(packetScheduling),
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destination,
            .finalState = ResourceStates::CopyDest,
            .acceptedToken = context->acceptedTransferToken,
        }
    );
    if(!transferTask.valid())
        return {};

    const GpuTaskId suffixTask = graph.addTask<StandaloneGraphAcceptedFrontierRecoveryCompletionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests.standalone_graph_accepted_frontier_recovery.graphics"))
            .setMarkerLabel("Standalone Accepted Frontier Recovery Graphics")
            .setQueue(graphicsRequest)
            .setScheduling(packetScheduling)
            .setDependencies(&transferTask, 1u),
        StandaloneGraphAcceptedFrontierRecoveryCompletionTask::Payload{
            .recorded = context->graphicsRecorded,
            .accepted = context->graphicsAccepted,
            .discarded = context->graphicsDiscarded,
        }
    );
    return suffixTask;
}


// The generic public standalone path must neither discard an already accepted Transfer packet nor leave it without a
// Graphics-side frontier when a later Graphics packet rejects. A second rejection proves the fail-closed recreation
// latch engages when that recovery packet itself cannot be accepted.
TEST_F(DescriptorBufferRoundTripTest, StandaloneGraphRecoversAcceptedTransferFrontierOrRequestsRecreation){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Standalone recovery: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& graphics = transferScope.graphics();
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId transferQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    if(
        !device.getQueue(CommandQueue::Transfer)
        || !transferQueue.valid()
        || transferQueue == graphicsQueue
        || device.getQueueFamilyIndex(transferQueue) == device.getQueueFamilyIndex(graphicsQueue)
    ){
        GTEST_SKIP() << "Standalone recovery: adapter has no dedicated transfer-only queue family.";
    }
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.matchesPhysicalQueueIdentity(graphicsQueue));
    ASSERT_TRUE(device.matchesPhysicalQueueIdentity(transferQueue));
    const VkQueue nativeTransferQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, transferQueue).pointer()
    );
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeTransferQueue, VK_NULL_HANDLE);
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);

    static constexpr u32 s_TransferWords[] = {
        0x1e35a7c9u,
        0x73b4d2f0u,
        0x0badbeefu,
        0x9e3779b9u,
    };
    const BufferHandle recoveredDestination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_TransferWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(recoveredDestination.get(), nullptr);

    QueueSubmissionToken acceptedTransferToken;
    bool graphicsRecorded = false;
    bool graphicsAccepted = false;
    bool graphicsDiscarded = false;
    StandaloneGraphAcceptedFrontierRecoveryContext recoveredContext{
        .destination = recoveredDestination,
        .bytes = s_TransferWords,
        .byteCount = sizeof(s_TransferWords),
        .acceptedTransferToken = &acceptedTransferToken,
        .graphicsRecorded = &graphicsRecorded,
        .graphicsAccepted = &graphicsAccepted,
        .graphicsDiscarded = &graphicsDiscarded,
    };
    QueueSubmissionToken terminalToken;
    {
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        EXPECT_FALSE(graphics.submitStandaloneTaskGraph(
            &recoveredContext,
            &DeclareStandaloneGraphAcceptedFrontierRecovery,
            terminalToken,
            graphicsQueue
        ));
        EXPECT_FALSE(terminalToken.valid());
        EXPECT_TRUE(graphicsRecorded);
        EXPECT_FALSE(graphicsAccepted);
        EXPECT_TRUE(graphicsDiscarded);
        ASSERT_TRUE(acceptedTransferToken.valid());
        EXPECT_EQ(acceptedTransferToken.queue, CommandQueue::Transfer);
        EXPECT_TRUE(acceptedTransferToken.matchesPhysicalQueue(transferQueue.index, transferQueue.deviceGeneration));
        EXPECT_FALSE(graphics.isDeviceRecreationRequested());
        EXPECT_FALSE(submissionObserver.overflowed());
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
        ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 3u);
        EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
        EXPECT_EQ(submissionObserver.successfulWaitCount(), 1u);
        VulkanTestQueueSubmit2Capture transferCapture;
        VulkanTestQueueSubmit2Capture failedGraphicsCapture;
        VulkanTestQueueSubmit2Capture recoveryCapture;
        ASSERT_TRUE(submissionObserver.capturedSubmission(0u, transferCapture));
        ASSERT_TRUE(submissionObserver.capturedSubmission(1u, failedGraphicsCapture));
        ASSERT_TRUE(submissionObserver.capturedSubmission(2u, recoveryCapture));
        EXPECT_EQ(failedGraphicsCapture.queue, nativeGraphicsQueue);
        EXPECT_EQ(failedGraphicsCapture.result, VK_ERROR_OUT_OF_HOST_MEMORY);
        EXPECT_FALSE(failedGraphicsCapture.overflowed);
        ASSERT_EQ(transferCapture.result, VK_SUCCESS);
        ASSERT_FALSE(transferCapture.overflowed);
        ASSERT_EQ(transferCapture.queue, nativeTransferQueue);
        ASSERT_EQ(transferCapture.submitCount, 1u);
        ASSERT_GT(transferCapture.submits[0u].signalCount, 0u);
        const VulkanTestSemaphoreSubmitInfo& transferSignal = transferCapture.submits[0u].signals[0u];
        ASSERT_NE(transferSignal.semaphore, VK_NULL_HANDLE);
        EXPECT_EQ(transferSignal.value, acceptedTransferToken.value);
        EXPECT_EQ(transferSignal.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        EXPECT_EQ(transferSignal.deviceIndex, 0u);
        ASSERT_EQ(recoveryCapture.result, VK_SUCCESS);
        ASSERT_FALSE(recoveryCapture.overflowed);
        ASSERT_EQ(recoveryCapture.queue, nativeGraphicsQueue);
        ASSERT_EQ(recoveryCapture.submitCount, 1u);
        ASSERT_EQ(recoveryCapture.submits[0u].waitCount, 1u);
        const VulkanTestSemaphoreSubmitInfo& recoveryWait = recoveryCapture.submits[0u].waits[0u];
        EXPECT_EQ(recoveryWait.semaphore, transferSignal.semaphore);
        EXPECT_EQ(recoveryWait.value, transferSignal.value);
        EXPECT_EQ(recoveryWait.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        EXPECT_EQ(recoveryWait.deviceIndex, 0u);
        ASSERT_GT(recoveryCapture.submits[0u].signalCount, 0u);
        const VulkanTestSemaphoreSubmitInfo& recoverySignal = recoveryCapture.submits[0u].signals[0u];
        ASSERT_NE(recoverySignal.semaphore, VK_NULL_HANDLE);
        EXPECT_GT(recoverySignal.value, 0u);
        EXPECT_EQ(recoverySignal.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        EXPECT_EQ(recoverySignal.deviceIndex, 0u);
        ASSERT_TRUE(device.waitForIdle());
    }

    const BufferHandle unrecoveredDestination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_TransferWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(unrecoveredDestination.get(), nullptr);

    QueueSubmissionToken unrecoveredTransferToken;
    bool unrecoveredGraphicsRecorded = false;
    bool unrecoveredGraphicsAccepted = false;
    bool unrecoveredGraphicsDiscarded = false;
    StandaloneGraphAcceptedFrontierRecoveryContext unrecoveredContext{
        .destination = unrecoveredDestination,
        .bytes = s_TransferWords,
        .byteCount = sizeof(s_TransferWords),
        .acceptedTransferToken = &unrecoveredTransferToken,
        .graphicsRecorded = &unrecoveredGraphicsRecorded,
        .graphicsAccepted = &unrecoveredGraphicsAccepted,
        .graphicsDiscarded = &unrecoveredGraphicsDiscarded,
    };
    QueueSubmissionToken unrecoveredTerminalToken;
    {
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue, 2u));
        EXPECT_FALSE(graphics.submitStandaloneTaskGraph(
            &unrecoveredContext,
            &DeclareStandaloneGraphAcceptedFrontierRecovery,
            unrecoveredTerminalToken,
            graphicsQueue
        ));
        EXPECT_FALSE(submissionObserver.overflowed());
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 2u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
        EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 3u);
        EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 1u);
    }
    EXPECT_FALSE(unrecoveredTerminalToken.valid());
    EXPECT_TRUE(unrecoveredGraphicsRecorded);
    EXPECT_FALSE(unrecoveredGraphicsAccepted);
    EXPECT_TRUE(unrecoveredGraphicsDiscarded);
    EXPECT_TRUE(unrecoveredTransferToken.valid());
    EXPECT_TRUE(graphics.isDeviceRecreationRequested());
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

