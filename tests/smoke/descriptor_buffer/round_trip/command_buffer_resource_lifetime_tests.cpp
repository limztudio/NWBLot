// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_skinning_test_support.h"
#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Queue completion must release a direct command list's retained resource during periodic device GC, even when no
// later command-list acquire or explicit wait-for-idle happens to trigger the same queue sweep.
TEST_F(DescriptorBufferRoundTripTest, GarbageCollectionRetiresCompletedCommandBufferResources){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32) * 4u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    auto retainedBuffer = buffer;

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->clearBufferUInt(buffer.get(), 0u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    buffer.reset();
    ASSERT_EQ(retainedBuffer->getReferenceCount(), 2u);

    for(u32 retry = 0u; retry < 5000u && retainedBuffer->getReferenceCount() != 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedBuffer->getReferenceCount(), 1u)
        << "periodic device GC did not retire the completed command-buffer resource reference";
}


// Framebuffer construction owns every attachment category even when the adapter cannot execute variable-rate
// shading. Keeping this ownership check feature-independent makes a caller-side shading-rate handle safe to drop.
TEST_F(DescriptorBufferRoundTripTest, FramebufferOwnsShadingRateAttachment){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto shadingRateTexture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(shadingRateTexture);
    auto retainedShadingRateTexture = shadingRateTexture;
    const usize referencesBeforeFramebuffer = shadingRateTexture->getReferenceCount();

    auto framebuffer = device.createFramebuffer(
        FramebufferDesc().setShadingRateAttachment(shadingRateTexture.get())
    );
    ASSERT_TRUE(framebuffer);
    EXPECT_EQ(retainedShadingRateTexture->getReferenceCount(), referencesBeforeFramebuffer + 1u);

    shadingRateTexture.reset();
    EXPECT_EQ(retainedShadingRateTexture->getReferenceCount(), referencesBeforeFramebuffer);
    framebuffer.reset();
    EXPECT_EQ(retainedShadingRateTexture->getReferenceCount(), referencesBeforeFramebuffer - 1u);
}


// State setup emits native pipeline, framebuffer, vertex-buffer, and index-buffer bindings even without a draw or
// dispatch. Their raw GraphicsState/ComputeState pointers must therefore become command-buffer-owned references.
TEST_F(DescriptorBufferRoundTripTest, StateOnlyNativeBindingsRetainResourcesUntilCompletion){
    auto& device = DescriptorBufferRoundTripTest::device();

    ShaderDesc vertexShaderDesc(DescriptorBufferRoundTripTest::arena());
    vertexShaderDesc
        .setShaderType(ShaderType::Vertex)
        .setDebugName(Name("tests/descriptor_buffer/command_buffer_lifetime_vertex"))
    ;
    auto vertexShader = device.createShader(
        vertexShaderDesc,
        s_CommandBufferLifetimeVertexSpirv,
        sizeof(s_CommandBufferLifetimeVertexSpirv)
    );
    ASSERT_TRUE(vertexShader);

    ShaderDesc fragmentShaderDesc(DescriptorBufferRoundTripTest::arena());
    fragmentShaderDesc
        .setShaderType(ShaderType::Pixel)
        .setDebugName(Name("tests/descriptor_buffer/command_buffer_lifetime_fragment"))
    ;
    auto fragmentShader = device.createShader(
        fragmentShaderDesc,
        s_CommandBufferLifetimeFragmentSpirv,
        sizeof(s_CommandBufferLifetimeFragmentSpirv)
    );
    ASSERT_TRUE(fragmentShader);

    auto renderTarget = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(renderTarget);
    const FramebufferDesc framebufferDesc = FramebufferDesc().addColorAttachment(renderTarget.get());
    auto framebuffer = device.createFramebuffer(framebufferDesc);
    ASSERT_TRUE(framebuffer);

    DepthStencilState depthStencilState;
    depthStencilState.disableDepthTest().disableDepthWrite();
    RenderState renderState;
    renderState.setDepthStencilState(depthStencilState);
    GraphicsPipelineDesc graphicsPipelineDesc;
    graphicsPipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(fragmentShader)
        .setRenderState(renderState)
    ;
    auto graphicsPipeline = device.createGraphicsPipeline(graphicsPipelineDesc, FramebufferInfo(framebufferDesc));
    ASSERT_TRUE(graphicsPipeline);

    ShaderDesc computeShaderDesc(DescriptorBufferRoundTripTest::arena());
    computeShaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name("tests/descriptor_buffer/command_buffer_lifetime_compute"))
    ;
    auto computeShader = device.createShader(
        computeShaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(computeShader);
    ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.setComputeShader(computeShader);
    auto computePipeline = device.createComputePipeline(computePipelineDesc);
    ASSERT_TRUE(computePipeline);

    auto vertexBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    auto indexBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32))
            .setIsIndexBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(vertexBuffer);
    ASSERT_TRUE(indexBuffer);

    auto retainedGraphicsPipeline = graphicsPipeline;
    auto retainedComputePipeline = computePipeline;
    auto retainedFramebuffer = framebuffer;
    auto retainedVertexBuffer = vertexBuffer;
    auto retainedIndexBuffer = indexBuffer;
    const usize graphicsPipelineReferencesBeforeRecord = graphicsPipeline->getReferenceCount();
    const usize computePipelineReferencesBeforeRecord = computePipeline->getReferenceCount();
    const usize framebufferReferencesBeforeRecord = framebuffer->getReferenceCount();
    const usize vertexBufferReferencesBeforeRecord = vertexBuffer->getReferenceCount();
    const usize indexBufferReferencesBeforeRecord = indexBuffer->getReferenceCount();

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    GraphicsState graphicsState;
    graphicsState
        .setPipeline(graphicsPipeline.get())
        .setFramebuffer(framebuffer.get())
        .addVertexBuffer(VertexBufferBinding().setBuffer(vertexBuffer.get()))
        .setIndexBuffer(IndexBufferBinding().setBuffer(indexBuffer.get()).setFormat(Format::R32_UINT))
    ;
    commandList->setGraphicsState(graphicsState);
    commandList->endRenderPass();
    commandList->setComputeState(ComputeState().setPipeline(computePipeline.get()));
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    EXPECT_EQ(retainedGraphicsPipeline->getReferenceCount(), graphicsPipelineReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedComputePipeline->getReferenceCount(), computePipelineReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedFramebuffer->getReferenceCount(), framebufferReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedVertexBuffer->getReferenceCount(), vertexBufferReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedIndexBuffer->getReferenceCount(), indexBufferReferencesBeforeRecord + 1u);

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());

    graphicsPipeline.reset();
    computePipeline.reset();
    framebuffer.reset();
    vertexBuffer.reset();
    indexBuffer.reset();
    commandList.reset();
    EXPECT_EQ(retainedGraphicsPipeline->getReferenceCount(), graphicsPipelineReferencesBeforeRecord);
    EXPECT_EQ(retainedComputePipeline->getReferenceCount(), computePipelineReferencesBeforeRecord);
    EXPECT_EQ(retainedFramebuffer->getReferenceCount(), framebufferReferencesBeforeRecord);
    EXPECT_EQ(retainedVertexBuffer->getReferenceCount(), vertexBufferReferencesBeforeRecord);
    EXPECT_EQ(retainedIndexBuffer->getReferenceCount(), indexBufferReferencesBeforeRecord);

    ASSERT_TRUE(device.waitForIdle());
    for(u32 retry = 0u; retry < 5000u && retainedGraphicsPipeline->getReferenceCount() != graphicsPipelineReferencesBeforeRecord - 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedGraphicsPipeline->getReferenceCount(), graphicsPipelineReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedComputePipeline->getReferenceCount(), computePipelineReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedFramebuffer->getReferenceCount(), framebufferReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedVertexBuffer->getReferenceCount(), vertexBufferReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedIndexBuffer->getReferenceCount(), indexBufferReferencesBeforeRecord - 1u);
}


// Transition/UAV barriers are native uses by themselves. keepInitialState forces a second close-time transition,
// proving both ordinary barrier emission and restoration share one completion-retired resource reference.
TEST_F(DescriptorBufferRoundTripTest, BarrierOnlyResourcesRetainUntilCompletion){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    auto restoreOnlyBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(restoreOnlyBuffer);

    auto retainedBuffer = buffer;
    auto retainedTexture = texture;
    auto retainedRestoreOnlyBuffer = restoreOnlyBuffer;
    const usize bufferReferencesBeforeRecord = buffer->getReferenceCount();
    const usize textureReferencesBeforeRecord = texture->getReferenceCount();
    const usize restoreOnlyBufferReferencesBeforeRecord = restoreOnlyBuffer->getReferenceCount();

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setBufferState(buffer.get(), ResourceStates::CopyDest);
    commandList->setTextureState(texture.get(), s_AllSubresources, ResourceStates::CopyDest);
    commandList->beginTrackingBufferState(restoreOnlyBuffer.get(), ResourceStates::CopyDest);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedTexture->getReferenceCount(), textureReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedRestoreOnlyBuffer->getReferenceCount(), restoreOnlyBufferReferencesBeforeRecord + 1u);

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    buffer.reset();
    texture.reset();
    restoreOnlyBuffer.reset();
    commandList.reset();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord);
    EXPECT_EQ(retainedTexture->getReferenceCount(), textureReferencesBeforeRecord);
    EXPECT_EQ(retainedRestoreOnlyBuffer->getReferenceCount(), restoreOnlyBufferReferencesBeforeRecord);

    ASSERT_TRUE(device.waitForIdle());
    for(u32 retry = 0u; retry < 5000u && retainedBuffer->getReferenceCount() != bufferReferencesBeforeRecord - 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedTexture->getReferenceCount(), textureReferencesBeforeRecord - 1u);
    EXPECT_EQ(retainedRestoreOnlyBuffer->getReferenceCount(), restoreOnlyBufferReferencesBeforeRecord - 1u);
}


// A graph import is an owner only until the graph/recording transaction is torn down. The accepted native packet
// remains the final owner of its barrier-only resource until the queue timeline retires that command buffer.
TEST_F(DescriptorBufferRoundTripTest, AcceptedGraphBarrierOutlivesGraphResourceHandle){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto graphBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(graphBuffer);
    auto retainedGraphBuffer = graphBuffer;
    const usize referencesBeforeGraph = graphBuffer->getReferenceCount();

    {
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        const GpuGraphResourceId resource = graph.importBuffer(
            graphBuffer,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/descriptor_buffer/accepted_graph_barrier_lifetime"))
                .setMarkerLabel("Accepted Graph Barrier Lifetime")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        graphBuffer.reset();

        const GpuTaskResourceUse resourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        };
        GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name("tests/descriptor_buffer/accepted_graph_barrier_lifetime_task"))
            .setMarkerLabel("Accepted Graph Barrier Lifetime")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setResourceUses(&resourceUse, 1u)
        ;
        bool recorded = false;
        SkinningGraphStateProbeTask::Payload payload;
        payload.expectations[0u] = { resource, ResourceStates::CopyDest };
        payload.expectationCount = 1u;
        payload.recorded = &recorded;
        const GpuTaskId task = graph.addTask<SkinningGraphStateProbeTask>(taskDesc, Move(payload));
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/accepted_graph_barrier_lifetime_scratch"));
        const GpuTaskGraphCompiler compiler;
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations,
                analysis,
                device.getPhysicalQueueTopology(),
                assignments,
                compiledGraph,
                scratchArena
            ));
        }
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
        const GpuNativePacketRecorder recorder(device);
        ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            views.compiled.allPacketRange(),
            recordedGraph
        ));
        EXPECT_TRUE(recorded);
        EXPECT_EQ(retainedGraphBuffer->getReferenceCount(), referencesBeforeGraph + 1u);

        GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
        transaction.reset(compiledGraph);
        const GpuTaskScheduler submitter(device);
        ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            views.compiled.allPacketRange(),
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            scratchArena
        ));
        EXPECT_TRUE(transaction.hasAcceptedPackets());
        EXPECT_EQ(retainedGraphBuffer->getReferenceCount(), referencesBeforeGraph + 1u);
    }

    EXPECT_EQ(retainedGraphBuffer->getReferenceCount(), referencesBeforeGraph);
    ASSERT_TRUE(device.waitForIdle());
    for(u32 retry = 0u; retry < 5000u && retainedGraphBuffer->getReferenceCount() != referencesBeforeGraph - 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedGraphBuffer->getReferenceCount(), referencesBeforeGraph - 1u);
}


// Same-family imports emit no ownership-acquire barrier, but the handoff contract lets its caller release resource
// ownership immediately after open(). The consumer must own every imported raw tracker entry through close/submission.
TEST_F(DescriptorBufferRoundTripTest, ImportedHandoffResourcesOutliveCallerAfterConsumerOpen){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    auto retainedBuffer = buffer;
    const usize referencesBeforeProducer = buffer->getReferenceCount();

    CommandListResourceStateHandoff handoff(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->setBufferState(buffer.get(), ResourceStates::CopyDest);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());
    CommandList* const producerLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists,
        LengthOf(producerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    producer.reset();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer);

    auto consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&handoff);
    ASSERT_TRUE(consumer->isRecording());
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer + 1u);

    buffer.reset();
    handoff.reset();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer);
    consumer->close();
    ASSERT_TRUE(consumer->hasCommandBuffer());
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer);

    CommandList* const consumerLists[] = { consumer.get() };
    const QueueSubmissionToken consumerToken = device.executeCommandLists(
        consumerLists,
        LengthOf(consumerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(consumerToken.valid());
    consumer.reset();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer);

    ASSERT_TRUE(device.waitForIdle());
    for(u32 retry = 0u; retry < 5000u && retainedBuffer->getReferenceCount() != referencesBeforeProducer - 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedBuffer->getReferenceCount(), referencesBeforeProducer - 1u);
}


// Timestamp reset/begin/end commands all embed the raw VkQueryPool in native work. Each independently recorded path
// must own the TimerQuery, deduplicate repeated uses, release abandoned/rejected work immediately, and retire accepted
// work only after its queue completion is collected.
TEST_F(DescriptorBufferRoundTripTest, TimerQueryCommandsRetainQueryThroughAbandonmentRejectionAndCompletion){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_TRUE(queueInfo);
    if(queueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Timer-query lifetime: primary Graphics queue does not support timestamps.";

    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto query = device.createTimerQuery();
    ASSERT_TRUE(query);
    auto retainedQuery = query;
    const usize referencesBeforeRecord = query->getReferenceCount();

    auto resetOnly = device.createCommandList(parameters);
    ASSERT_TRUE(resetOnly);

    resetOnly->open();
    ASSERT_TRUE(resetOnly->resetTimerQuery(query.get()));
    resetOnly->close();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 1u);
    resetOnly.reset();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord);

    auto beginOnly = device.createCommandList(parameters);
    auto endOnly = device.createCommandList(parameters);
    ASSERT_TRUE(beginOnly);
    ASSERT_TRUE(endOnly);
    beginOnly->open();
    TimerQueryRecordingToken abandonedQueryRecording;
    ASSERT_TRUE(beginOnly->beginTimerQuery(query.get(), abandonedQueryRecording));
    beginOnly->close();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 1u);

    endOnly->open();
    ASSERT_TRUE(endOnly->endTimerQuery(query.get(), abandonedQueryRecording));
    endOnly->close();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 2u);

    beginOnly.reset();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 1u);
    endOnly.reset();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord);

    auto rejected = device.createCommandList(parameters);
    ASSERT_TRUE(rejected);
    rejected->open();
    ASSERT_TRUE(rejected->resetTimerQuery(query.get()));
    TimerQueryRecordingToken rejectedQueryRecording;
    ASSERT_TRUE(rejected->beginTimerQuery(query.get(), rejectedQueryRecording));
    ASSERT_TRUE(rejected->endTimerQuery(query.get(), rejectedQueryRecording));
    rejected->close();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 1u);

    CommandList* const rejectedCommandLists[] = { rejected.get() };
    QueueSubmissionToken rejectedToken;
    {
        const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
        );
        ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        rejectedToken = device.executeCommandLists(
            rejectedCommandLists,
            LengthOf(rejectedCommandLists),
            graphicsQueue,
            QueueSubmissionDesc{}
        );
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(rejectedToken.valid());
    EXPECT_FALSE(rejected->hasCommandBuffer());
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord);
    rejected.reset();

    auto completed = device.createCommandList(parameters);
    ASSERT_TRUE(completed);
    completed->open();
    ASSERT_TRUE(completed->resetTimerQuery(query.get()));
    TimerQueryRecordingToken completedQueryRecording;
    ASSERT_TRUE(completed->beginTimerQuery(query.get(), completedQueryRecording));
    ASSERT_TRUE(completed->endTimerQuery(query.get(), completedQueryRecording));
    completed->close();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord + 1u);

    CommandList* const completedCommandLists[] = { completed.get() };
    const QueueSubmissionToken completedToken = device.executeCommandLists(
        completedCommandLists,
        LengthOf(completedCommandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(completedToken.valid());
    EXPECT_FALSE(retainedQuery->discardUnacceptedRecording(completedQueryRecording));
    query.reset();
    completed.reset();
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord);

    ASSERT_TRUE(device.waitForIdle());
    TimerQueryResult result;
    EXPECT_TRUE(device.getTimerQueryResult(retainedQuery.get(), result));
    for(u32 retry = 0u; retry < 5000u && retainedQuery->getReferenceCount() != referencesBeforeRecord - 1u; ++retry){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedQuery->getReferenceCount(), referencesBeforeRecord - 1u);
}


// Never-submitted and native-submit-failed command buffers must not turn safety retention into a leak. Both paths
// release state-only pipeline and barrier-only buffer references as soon as the native buffer is abandoned.
TEST_F(DescriptorBufferRoundTripTest, AbandonedAndRejectedCommandBuffersReleaseRetainedResourcesPromptly){
    auto& device = DescriptorBufferRoundTripTest::device();
    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name("tests/descriptor_buffer/abandoned_command_buffer_lifetime"))
    ;
    auto shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(shader);
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader);
    auto pipeline = device.createComputePipeline(pipelineDesc);
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(pipeline);
    ASSERT_TRUE(buffer);

    auto retainedPipeline = pipeline;
    auto retainedBuffer = buffer;
    const usize pipelineReferencesBeforeRecord = pipeline->getReferenceCount();
    const usize bufferReferencesBeforeRecord = buffer->getReferenceCount();

    auto abandoned = device.createCommandList();
    ASSERT_TRUE(abandoned);
    abandoned->open();
    abandoned->setBufferState(buffer.get(), ResourceStates::CopyDest);
    abandoned->setComputeState(ComputeState().setPipeline(pipeline.get()));
    abandoned->close();
    EXPECT_EQ(retainedPipeline->getReferenceCount(), pipelineReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord + 1u);
    abandoned.reset();
    EXPECT_EQ(retainedPipeline->getReferenceCount(), pipelineReferencesBeforeRecord);
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord);

    auto rejected = device.createCommandList();
    ASSERT_TRUE(rejected);
    rejected->open();
    rejected->setBufferState(buffer.get(), ResourceStates::CopyDest);
    rejected->setComputeState(ComputeState().setPipeline(pipeline.get()));
    rejected->close();
    EXPECT_EQ(retainedPipeline->getReferenceCount(), pipelineReferencesBeforeRecord + 1u);
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord + 1u);

    CommandList* const commandLists[] = { rejected.get() };
    QueueSubmissionToken rejectedToken;
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
        rejectedToken = device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(rejectedToken.valid());
    EXPECT_FALSE(rejected->hasCommandBuffer());
    EXPECT_EQ(retainedPipeline->getReferenceCount(), pipelineReferencesBeforeRecord);
    EXPECT_EQ(retainedBuffer->getReferenceCount(), bufferReferencesBeforeRecord);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

