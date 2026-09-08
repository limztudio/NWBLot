// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture uploads must leave ImGui's create/update status pending if the Vulkan submission is rejected. The next
// recording batch then retries the request and commits its status only after the device accepts that retry.
TEST_F(DescriptorBufferRoundTripTest, ImguiTextureUploadBatchCommitsOnlyAfterAcceptedSubmission){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name s_TestArenaName{"tests/descriptor_buffer/imgui_texture_upload_batch_arena"};
    Alloc::GlobalArena arena{s_TestArenaName};
    Impl::UiTextureUploadBatch uploads{arena};
    ImTextureData createTexture;
    ImTextureData updateTexture;
    bool createInitialUploadAccepted = false;
    bool updateInitialUploadAccepted = false;
    createTexture.SetStatus(ImTextureStatus_WantCreate);
    updateTexture.SetStatus(ImTextureStatus_WantUpdates);

    auto rejected = device.createCommandList();
    ASSERT_NE(rejected.get(), nullptr);
    rejected->open();
    rejected->close();
    ASSERT_TRUE(rejected->hasCommandBuffer());

    uploads.add(createTexture, &createInitialUploadAccepted);
    uploads.add(updateTexture, &updateInitialUploadAccepted);
    CommandList* rejectedCommandLists[] = { rejected.get() };
    bool submitted = true;
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
        device.executeCommandLists(rejectedCommandLists, 1u, CommandQueue::Graphics, &submitted);
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(submitted);
    uploads.complete(submitted);
    EXPECT_EQ(createTexture.Status, ImTextureStatus_WantCreate);
    EXPECT_EQ(updateTexture.Status, ImTextureStatus_WantUpdates);
    EXPECT_FALSE(createInitialUploadAccepted);
    EXPECT_FALSE(updateInitialUploadAccepted);

    auto accepted = device.createCommandList();
    ASSERT_NE(accepted.get(), nullptr);
    accepted->open();
    accepted->close();
    ASSERT_TRUE(accepted->hasCommandBuffer());

    uploads.add(createTexture, &createInitialUploadAccepted);
    uploads.add(updateTexture, &updateInitialUploadAccepted);
    CommandList* acceptedCommandLists[] = { accepted.get() };
    submitted = false;
    device.executeCommandLists(acceptedCommandLists, 1u, CommandQueue::Graphics, &submitted);
    ASSERT_TRUE(submitted);
    uploads.complete(submitted);
    EXPECT_EQ(createTexture.Status, ImTextureStatus_OK);
    EXPECT_EQ(updateTexture.Status, ImTextureStatus_OK);
    EXPECT_TRUE(createInitialUploadAccepted);
    EXPECT_FALSE(updateInitialUploadAccepted);
}


// The last-resort ImGui raster path reuses its command list after a rejected native submission. Verify that Device
// returns the rejected buffer to the queue and accepts a newly recorded retry on that same direct command-list owner.
TEST_F(DescriptorBufferRoundTripTest, DirectCommandListCanRetryAfterRejectedSubmission){
    auto& device = DescriptorBufferRoundTripTest::device();

    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    commandList->open();
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    CommandList* commandLists[] = { commandList.get() };
    bool submitted = true;
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
        device.executeCommandLists(commandLists, 1u, CommandQueue::Graphics, &submitted);
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(submitted);
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    submitted = false;
    device.executeCommandLists(commandLists, 1u, CommandQueue::Graphics, &submitted);
    ASSERT_TRUE(submitted);
    ASSERT_TRUE(device.waitForIdle());
}


// A structurally exact token can still name a producer value that was never submitted. Reject that dependency
// before graph submission reservation so the packet and lifecycle hooks remain available for a real token.
TEST_F(DescriptorBufferRoundTripTest, NativePacketFutureWaitPreflightRemainsRetryable){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    CommandListParameters producerParameters;
    producerParameters.setPhysicalQueue(graphicsQueue);
    const CommandListHandle producer = device.createCommandList(producerParameters);
    ASSERT_NE(producer.get(), nullptr);
    producer->open();
    producer->close();
    CommandList* const producerLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists,
        LengthOf(producerLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_LT(producerToken.value, Limit<u64>::s_Max);
    ASSERT_TRUE(device.validateSubmissionWaitToken(producerToken));
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);

    QueueSubmissionToken futureToken = producerToken;
    ++futureToken.value;
    EXPECT_FALSE(device.validateSubmissionWaitToken(futureToken));
    QueueSubmissionToken missingIdentityToken = producerToken;
    missingIdentityToken.physicalQueueIndex = Limit<u16>::s_Max;
    EXPECT_FALSE(device.validateSubmissionWaitToken(missingIdentityToken));
    QueueSubmissionToken wrongClassToken = producerToken;
    wrongClassToken.queue = CommandQueue::Compute;
    EXPECT_FALSE(device.validateSubmissionWaitToken(wrongClassToken));
    QueueSubmissionToken staleGenerationToken = producerToken;
    staleGenerationToken.deviceGeneration = producerToken.deviceGeneration == Limit<u16>::s_Max
        ? static_cast<u16>(producerToken.deviceGeneration - 1u)
        : static_cast<u16>(producerToken.deviceGeneration + 1u)
    ;
    EXPECT_FALSE(device.validateSubmissionWaitToken(staleGenerationToken));

    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuExternalCompletionId completion = graph.importExternalCompletion(
        GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/descriptor_buffer/future_wait_completion"))
            .setMarkerLabel("Future Wait Completion")
    );
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/future_wait_buffer"))
            .setMarkerLabel("Future Wait Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(completion.valid());
    ASSERT_TRUE(resource.valid());
    const GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/future_wait_consumer"))
        .setMarkerLabel("Future Wait Consumer")
        .setQueue(graphicsRequest)
        .setExternalDependencies(&completion, 1u)
        .setResourceUses(&use, 1u)
    ;
    bool recorded = false;
    QueueSubmissionToken acceptedToken;
    u32 discardedCount = 0u;
    const GpuTaskId task = graph.addTask<NativePacketPrefixTask>(
        taskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &recorded,
            .acceptedToken = &acceptedToken,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/future_wait_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(views.compiled.packet(packet).plan->externalDependencyCount, 1u);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    ASSERT_TRUE(recorded);

    const GpuTaskGraphExternalCompletionToken futureBinding{
        .completion = completion,
        .token = futureToken,
    };
    const GpuTaskScheduler submitter(device);
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        &futureBinding,
        1u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_EQ(discardedCount, 0u);
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    const GpuTaskGraphSubmissionStatistics rejectedStatistics = transaction.submissionStatistics();
    EXPECT_EQ(rejectedStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(rejectedStatistics.rejectedPacketCount, 0u);
    EXPECT_EQ(rejectedStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(rejectedStatistics.rejectedSubmissionCount, 0u);
    const GpuTaskGraphPhysicalQueueSubmissionStatistics rejectedQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(views.compiled, graphicsQueue);
    ASSERT_TRUE(rejectedQueueStatistics.valid());
    EXPECT_EQ(rejectedQueueStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(rejectedQueueStatistics.rejectedPacketCount, 0u);
    EXPECT_EQ(rejectedQueueStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(rejectedQueueStatistics.rejectedSubmissionCount, 0u);
    const Optional<GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
    ASSERT_TRUE(recordedPacket.has_value());
    ASSERT_GT(recordedPacket->commandListCount, 0u);
    EXPECT_TRUE(recordedPacket->commandLists[0u]->hasCommandBuffer());

    const GpuTaskGraphExternalCompletionToken currentBinding{
        .completion = completion,
        .token = producerToken,
    };
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        &currentBinding,
        1u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken consumerToken = transaction.packetToken(packet);
    ASSERT_TRUE(consumerToken.valid());
    EXPECT_EQ(acceptedToken.value, consumerToken.value);
    EXPECT_EQ(discardedCount, 0u);
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 0u);
    VulkanTestQueueSubmit2Capture consumerCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(1u, consumerCapture));
    ASSERT_EQ(consumerCapture.result, VK_SUCCESS);
    ASSERT_FALSE(consumerCapture.overflowed);
    EXPECT_EQ(consumerCapture.queue, nativeGraphicsQueue);
    ASSERT_EQ(consumerCapture.submitCount, 1u);
    ASSERT_FALSE(consumerCapture.submits[0u].overflowed);
    EXPECT_EQ(consumerCapture.submits[0u].waitCount, 0u);
    ASSERT_GT(consumerCapture.submits[0u].signalCount, 0u);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].value, consumerToken.value);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].deviceIndex, 0u);
    const GpuTaskGraphSubmissionStatistics acceptedStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(acceptedStatistics.valid());
    EXPECT_EQ(acceptedStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(acceptedStatistics.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(acceptedStatistics.timelineWaitCount, 0u);
    ASSERT_TRUE(device.waitForIdle());
}


// Frame acquisition is a queue-global binary wait. An injected native-submit failure must leave global synchronization
// by the queue so an accepted compatibility submission can consume it instead of reusing a still-signaled semaphore.
TEST_F(DescriptorBufferRoundTripTest, QueueGlobalSynchronizationSurvivesInjectedNativeSubmitFailure){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    auto producer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    producer->open();
    producer->close();
    ASSERT_TRUE(producer->hasCommandBuffer());

    CommandList* producerCommandLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerCommandLists,
        LengthOf(producerCommandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    device.queueWaitForCommandList(CommandQueue::Graphics, producerToken.queue, producerToken.value);
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    EXPECT_FALSE(device.executeCommandLists(nullptr, 0u, graphicsQueue, QueueSubmissionDesc{}).valid());
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);

    const QueueSubmissionToken retryToken = device.executeCommandLists(
        nullptr,
        0u,
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(retryToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// A forced empty submission is an explicit timeline packet, unlike the default empty no-op. Presentation recovery
// uses this to accept a drain even when earlier Graphics work already consumed every queue-global acquire wait.
TEST_F(DescriptorBufferRoundTripTest, ForcedEmptySubmissionAdvancesExactQueueAndRetriesAfterRejection){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    EXPECT_FALSE(device.executeCommandLists(nullptr, 0u, graphicsQueue, QueueSubmissionDesc{}).valid());

    QueueSubmissionDesc forcedSubmitDesc;
    forcedSubmitDesc.forceNativeSubmission = true;
    const QueueSubmissionToken firstToken = device.executeCommandLists(nullptr, 0u, graphicsQueue, forcedSubmitDesc);
    ASSERT_TRUE(firstToken.valid());
    EXPECT_EQ(firstToken.queue, CommandQueue::Graphics);
    EXPECT_TRUE(firstToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    ASSERT_TRUE(device.waitForIdle());

    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    EXPECT_FALSE(device.executeCommandLists(nullptr, 0u, graphicsQueue, forcedSubmitDesc).valid());
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);

    const QueueSubmissionToken retryToken = device.executeCommandLists(nullptr, 0u, graphicsQueue, forcedSubmitDesc);
    ASSERT_TRUE(retryToken.valid());
    EXPECT_TRUE(retryToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_GT(retryToken.value, firstToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

