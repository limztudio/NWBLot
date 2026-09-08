// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NativePacketRenderPassBoundaryTask{
    struct Payload{
        Framebuffer* framebuffer = nullptr;
        bool* observed = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.observed)
            return false;

        if(payload.framebuffer){
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(payload.framebuffer);
            commandList.setGraphicsState(graphicsState);
            *payload.observed = commandList.isRenderPassActive();
        }else{
            *payload.observed = !commandList.isRenderPassActive();
        }
        return *payload.observed;
    }
};


// Clear helpers are deliberately graph-native primitives: their CopyDest declarations remain authoritative while an
// explicitly supplied Phase 11 capture receives compact resource-ID records. The normal recorder call sites pass
// no capture object and continue directly to native command lists.
TEST_F(DescriptorBufferRoundTripTest, BuiltInClearTasksRecordAndCapture){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferDesc clearBufferDesc = BufferDesc()
        .setByteSize(sizeof(u32) * 4u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto buffer = device.createBuffer(clearBufferDesc);
    ASSERT_NE(buffer.get(), nullptr);

    const TextureDesc clearTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UINT)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto texture = device.createTexture(clearTextureDesc);
    ASSERT_NE(texture.get(), nullptr);

    const TextureDesc clearDepthTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::D32S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto depthTexture = device.createTexture(clearDepthTextureDesc);
    ASSERT_NE(depthTexture.get(), nullptr);
    Texture* const initialTextures[] = { texture.get(), depthTexture.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_buffer"))
            .setMarkerLabel("Built-In Clear Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_texture"))
            .setMarkerLabel("Built-In Clear Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId depthTextureResource = graph.importTexture(
        depthTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_depth_texture"))
            .setMarkerLabel("Built-In Clear Depth Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(bufferResource.valid());
    ASSERT_TRUE(textureResource.valid());
    ASSERT_TRUE(depthTextureResource.valid());

    struct ClearTextureRecordHookState{
        u32 beforeCount = 0u;
        u32 afterCount = 0u;
        u32 discardedCount = 0u;
    };
    ClearTextureRecordHookState textureHooks;
    const GpuClearTextureTaskRecordHook beforeTextureClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        ClearTextureRecordHookState* const state = static_cast<ClearTextureRecordHookState*>(rawState);
        if(!state)
            return false;
        ++state->beforeCount;
        return true;
    };
    const GpuClearTextureTaskRecordHook afterTextureClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        ClearTextureRecordHookState* const state = static_cast<ClearTextureRecordHookState*>(rawState);
        if(!state)
            return false;
        ++state->afterCount;
        return true;
    };
    const GpuClearTextureTaskDiscardedHook discardTextureClear = [](void* const rawState){
        ClearTextureRecordHookState* const state = static_cast<ClearTextureRecordHookState*>(rawState);
        if(state)
            ++state->discardedCount;
    };

    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Small;
    clearScheduling.forceSubmissionBoundary = true;
    clearScheduling.allowPacketMerge = false;
    const GpuQueueRequest transferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        true,
        true,
    };
    GpuTaskDesc clearBufferTaskDesc;
    clearBufferTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_buffer_task"))
        .setMarkerLabel("Built-In Clear Buffer Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    GpuTaskDesc clearTextureTaskDesc;
    clearTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_texture_task"))
        .setMarkerLabel("Built-In Clear Texture Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    GpuTaskDesc clearTextureRectTaskDesc;
    clearTextureRectTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_texture_rect_task"))
        .setMarkerLabel("Built-In Clear Texture Rect Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    GpuTaskDesc clearDepthTextureTaskDesc;
    clearDepthTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_depth_texture_task"))
        .setMarkerLabel("Built-In Clear Depth Texture Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    QueueSubmissionToken clearBufferAcceptedToken;
    QueueSubmissionToken clearTextureAcceptedToken;
    QueueSubmissionToken clearTextureRectAcceptedToken;
    QueueSubmissionToken clearDepthTextureAcceptedToken;
    const GpuTaskId clearBufferTask = graph.addClearBufferTask(
        clearBufferTaskDesc,
        GpuClearBufferTaskDesc{
            .destination = bufferResource,
            .clearValue = 0xdecafbadU,
            .acceptedToken = &clearBufferAcceptedToken,
        }
    );
    GpuClearTextureTaskDesc clearTextureTaskDescPayload;
    clearTextureTaskDescPayload.destination = textureResource;
    clearTextureTaskDescPayload.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTextureTaskDescPayload.valueType = GpuClearTextureTaskValueType::UInt;
    clearTextureTaskDescPayload.uintValue = UIntColor(1u, 2u, 3u, 4u);
    clearTextureTaskDescPayload.recordHooks = GpuClearTextureTaskRecordHooks{
        .context = &textureHooks,
        .beforeClear = beforeTextureClear,
        .afterClear = afterTextureClear,
        .discarded = discardTextureClear,
    };
    clearTextureTaskDescPayload.acceptedToken = &clearTextureAcceptedToken;
    const GpuTaskId clearTextureTask = graph.addClearTextureTask(clearTextureTaskDesc, clearTextureTaskDescPayload);
    const GpuTaskId clearTextureRectDependencies[] = { clearTextureTask };
    clearTextureRectTaskDesc.setDependencies(
        clearTextureRectDependencies,
        LengthOf(clearTextureRectDependencies)
    );
    const GpuClearTextureRectUIntTaskDesc clearTextureRectTaskDescPayload{
        .destination = textureResource,
        .subresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
        .rect = Rect(1, 3, 1, 3),
        .uintValue = UIntColor(9u, 10u, 11u, 12u),
        .recordHooks = {},
        .acceptedToken = &clearTextureRectAcceptedToken,
    };
    const GpuTaskId clearTextureRectTask = graph.addClearTextureRectUIntTask(
        clearTextureRectTaskDesc,
        clearTextureRectTaskDescPayload
    );
    const GpuTaskId clearDepthTextureTask = graph.addClearTextureTask(
        clearDepthTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .acceptedToken = &clearDepthTextureAcceptedToken,
            .destination = depthTextureResource,
            .depthValue = 0.25f,
            .subresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
            .valueType = GpuClearTextureTaskValueType::DepthStencil,
            .stencilValue = 0x7fu,
            .clearDepth = true,
            .clearStencil = true,
        }
    );
    ASSERT_TRUE(clearBufferTask.valid());
    ASSERT_TRUE(clearTextureTask.valid());
    ASSERT_TRUE(clearTextureRectTask.valid());
    ASSERT_TRUE(clearDepthTextureTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_EQ(declarations.taskAt(clearBufferTask.index).resourceUseCount, 1u);
        ASSERT_EQ(declarations.taskAt(clearTextureTask.index).resourceUseCount, 1u);
        ASSERT_EQ(declarations.taskAt(clearTextureRectTask.index).resourceUseCount, 1u);
        ASSERT_EQ(declarations.taskAt(clearDepthTextureTask.index).resourceUseCount, 1u);
        EXPECT_EQ(
            declarations.taskAt(clearBufferTask.index).resourceUses[0u].requiredState,
            ResourceStates::CopyDest
        );
        EXPECT_EQ(
            declarations.taskAt(clearTextureTask.index).resourceUses[0u].requiredState,
            ResourceStates::CopyDest
        );
        EXPECT_EQ(
            declarations.taskAt(clearTextureRectTask.index).resourceUses[0u].requiredState,
            ResourceStates::CopyDest
        );
        EXPECT_EQ(
            declarations.taskAt(clearDepthTextureTask.index).resourceUses[0u].requiredState,
            ResourceStates::CopyDest
        );
    }
    // Typed task creation rejects value types that native Vulkan clear commands cannot lower for the target image.
    EXPECT_FALSE(graph.addClearTextureTask(
        clearTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = textureResource,
            .valueType = GpuClearTextureTaskValueType::Float,
        }
    ).valid());
    EXPECT_FALSE(graph.addClearTextureTask(
        clearTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = depthTextureResource,
            .valueType = GpuClearTextureTaskValueType::UInt,
        }
    ).valid());
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(
        clearTextureRectTaskDesc,
        GpuClearTextureRectUIntTaskDesc{
            .destination = depthTextureResource,
            .rect = Rect(1, 3, 1, 3),
            .uintValue = {},
            .recordHooks = {},
        }
    ).valid());
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(
        clearTextureRectTaskDesc,
        GpuClearTextureRectUIntTaskDesc{
            .destination = textureResource,
            .rect = Rect(2, 2, 1, 3),
            .uintValue = {},
            .recordHooks = {},
        }
    ).valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_clear_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId clearBufferPacket = views.compiled.packetForTask(clearBufferTask);
    const GpuSubmissionPacketId clearTexturePacket = views.compiled.packetForTask(clearTextureTask);
    const GpuSubmissionPacketId clearTextureRectPacket = views.compiled.packetForTask(clearTextureRectTask);
    const GpuSubmissionPacketId clearDepthTexturePacket = views.compiled.packetForTask(clearDepthTextureTask);
    ASSERT_TRUE(clearBufferPacket.valid());
    ASSERT_TRUE(clearTexturePacket.valid());
    ASSERT_TRUE(clearTextureRectPacket.valid());
    ASSERT_TRUE(clearDepthTexturePacket.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    EXPECT_EQ(textureHooks.beforeCount, 1u);
    EXPECT_EQ(textureHooks.afterCount, 1u);
    EXPECT_EQ(textureHooks.discardedCount, 0u);
    ASSERT_EQ(commandIrCapture.recordCount(), 4u);
    const GpuCommandIrBuiltinTaskRecord* const bufferCapture = commandIrCapture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const textureCapture = commandIrCapture.recordAt(1u);
    const GpuCommandIrBuiltinTaskRecord* const textureRectCapture = commandIrCapture.recordAt(2u);
    const GpuCommandIrBuiltinTaskRecord* const depthTextureCapture = commandIrCapture.recordAt(3u);
    ASSERT_NE(bufferCapture, nullptr);
    ASSERT_NE(textureCapture, nullptr);
    ASSERT_NE(textureRectCapture, nullptr);
    ASSERT_NE(depthTextureCapture, nullptr);
    EXPECT_EQ(bufferCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(bufferCapture->task, clearBufferTask);
    EXPECT_EQ(bufferCapture->packet, clearBufferPacket);
    EXPECT_EQ(bufferCapture->destination, bufferResource);
    EXPECT_EQ(bufferCapture->uintClearValue, UIntColor(0xdecafbadU));
    EXPECT_EQ(textureCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(textureCapture->task, clearTextureTask);
    EXPECT_EQ(textureCapture->packet, clearTexturePacket);
    EXPECT_EQ(textureCapture->destination, textureResource);
    EXPECT_EQ(textureCapture->clearTextureValueType, GpuClearTextureTaskValueType::UInt);
    EXPECT_EQ(textureCapture->uintClearValue, UIntColor(1u, 2u, 3u, 4u));
    EXPECT_EQ(textureRectCapture->opcode, GpuCommandIrOpcode::ClearTextureRectUInt);
    EXPECT_EQ(textureRectCapture->task, clearTextureRectTask);
    EXPECT_EQ(textureRectCapture->packet, clearTextureRectPacket);
    EXPECT_EQ(textureRectCapture->destination, textureResource);
    EXPECT_EQ(textureRectCapture->destinationSubresources, clearTextureRectTaskDescPayload.subresources);
    EXPECT_EQ(textureRectCapture->clearRect, clearTextureRectTaskDescPayload.rect);
    EXPECT_EQ(textureRectCapture->uintClearValue, clearTextureRectTaskDescPayload.uintValue);
    EXPECT_EQ(depthTextureCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(depthTextureCapture->task, clearDepthTextureTask);
    EXPECT_EQ(depthTextureCapture->packet, clearDepthTexturePacket);
    EXPECT_EQ(depthTextureCapture->destination, depthTextureResource);
    EXPECT_EQ(depthTextureCapture->clearTextureValueType, GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(depthTextureCapture->depthClearValue, 0.25f);
    EXPECT_EQ(depthTextureCapture->stencilClearValue, 0x7fu);
    EXPECT_TRUE(depthTextureCapture->clearDepth);
    EXPECT_TRUE(depthTextureCapture->clearStencil);

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
    EXPECT_TRUE(clearBufferAcceptedToken.valid());
    EXPECT_TRUE(clearTextureAcceptedToken.valid());
    EXPECT_TRUE(clearTextureRectAcceptedToken.valid());
    EXPECT_TRUE(clearDepthTextureAcceptedToken.valid());
    EXPECT_EQ(
        clearTextureRectAcceptedToken.value,
        transaction.packetToken(clearTextureRectPacket).value
    );
    ASSERT_TRUE(device.waitForIdle());

    // The ordinary replay lowerer remains graph-aware: it selects only the rectangular-clear packet from the full
    // capture after preflight, then relies on the already-established graph final state rather than a native
    // state bridge. The narrow direct-Vulkan prototype remains CopyBuffer-only in its separate coverage.
    CommandListResourceStateHandoff clearTextureRectFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        clearTextureRectTask,
        clearTextureRectFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const clearTextureRectFinalState = &clearTextureRectFinalStateStorage;
    const GpuPhysicalQueueInfo* const clearTextureRectQueue = views.compiled.queueInfo(
        views.compiled.packet(clearTextureRectPacket).plan->queue
    );
    ASSERT_NE(clearTextureRectQueue, nullptr);
    CommandListParameters replayParameters;
    replayParameters.setPhysicalQueue(clearTextureRectQueue->id);
    CommandListHandle replay = device.createCommandList(replayParameters);
    ASSERT_NE(replay.get(), nullptr);
    replay->open(clearTextureRectFinalState);
    ASSERT_TRUE(replay->isRecording());
    const GpuCommandIrReplayResult replayResult = ReplayGpuCommandIrPacket(
        commandIrCapture.commandBytes(),
        views.declarations,
        views.compiled,
        clearTextureRectPacket,
        *replay
    );
    EXPECT_TRUE(replayResult.valid());
    EXPECT_TRUE(replayResult.streamValidation.valid());
    replay->close();
    CommandList* const replayLists[] = { replay.get() };
    const QueueSubmissionToken replayToken = device.executeCommandLists(
        replayLists,
        LengthOf(replayLists),
        clearTextureRectQueue->id,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(replayToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    // Read the small uint image back after both graph recording and graph-aware replay. The two interior pixels
    // prove that the region coordinates, component payload, and lowerer are exact rather than falling back to a
    // whole-image clear.
    StagingTextureHandle textureReadback = device.createStagingTexture(texture->getDescription(), CpuAccessMode::Read);
    ASSERT_NE(textureReadback.get(), nullptr);
    CommandListHandle textureReadbackCommandList = device.createCommandList();
    ASSERT_NE(textureReadbackCommandList.get(), nullptr);
    textureReadbackCommandList->open(clearTextureRectFinalState);
    ASSERT_TRUE(textureReadbackCommandList->hasCommandBuffer());
    textureReadbackCommandList->copyTexture(
        textureReadback.get(),
        TextureSlice{},
        texture.get(),
        TextureSlice{}
    );
    textureReadbackCommandList->close();
    CommandList* const textureReadbackLists[] = { textureReadbackCommandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        textureReadbackLists,
        LengthOf(textureReadbackLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    usize textureReadbackRowPitch = 0u;
    const u8* const textureReadbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        textureReadback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &textureReadbackRowPitch
    ));
    ASSERT_NE(textureReadbackBytes, nullptr);
    ASSERT_GE(textureReadbackRowPitch, 4u * sizeof(u32));
    for(u32 row = 0u; row < 4u; ++row){
        for(u32 column = 0u; column < 4u; ++column){
            const UIntColor expected = column >= 1u && column < 3u && row >= 1u && row < 3u
                ? clearTextureRectTaskDescPayload.uintValue
                : clearTextureTaskDescPayload.uintValue
            ;
            const u8* const pixel = textureReadbackBytes
                + static_cast<usize>(row) * textureReadbackRowPitch
                + static_cast<usize>(column) * sizeof(u32)
            ;
            EXPECT_EQ(pixel[0u], static_cast<u8>(expected.r));
            EXPECT_EQ(pixel[1u], static_cast<u8>(expected.g));
            EXPECT_EQ(pixel[2u], static_cast<u8>(expected.b));
            EXPECT_EQ(pixel[3u], static_cast<u8>(expected.a));
        }
    }
    device.unmapStagingTexture(textureReadback.get());

    const u32* const clearedWords = static_cast<const u32*>(device.mapBuffer(buffer.get(), CpuAccessMode::Read));
    ASSERT_NE(clearedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < 4u; ++wordIndex)
        EXPECT_EQ(clearedWords[wordIndex], 0xdecafbadU);
    device.unmapBuffer(buffer.get());
}


// Full Vulkan image clears are legal for multisampled images outside rendering. The graph must retain that direct
// route, publish its accepted packet token, and leave exact color data that survives a later resolve and readback.
TEST_F(DescriptorBufferRoundTripTest, GraphFullMultisampleTextureClearSubmitsAndResolves){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 4u;
    constexpr u32 s_Height = 4u;
    constexpr Color s_ClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    const TextureDesc multisampleColorDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setDimension(TextureDimension::Texture2DMS)
        .setFormat(Format::RGBA8_UNORM)
        .setSampleCount(2u)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureDesc resolvedColorDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureHandle multisampleColor = device.createTexture(multisampleColorDesc);
    if(!multisampleColor)
        GTEST_SKIP() << "Full multisample graph clear: sample-2 RGBA8_UNORM is unavailable.";
    const TextureHandle resolvedColor = device.createTexture(resolvedColorDesc);
    const StagingTextureHandle readback = device.createStagingTexture(resolvedColorDesc, CpuAccessMode::Read);
    ASSERT_TRUE(resolvedColor);
    ASSERT_TRUE(readback);

    TextureHandle multisampleDepth;
    if((device.queryFormatSupport(Format::D32) & FormatSupport::DepthStencil) == FormatSupport::DepthStencil){
        multisampleDepth = device.createTexture(
            TextureDesc()
                .setWidth(s_Width)
                .setHeight(s_Height)
                .setDimension(TextureDimension::Texture2DMS)
                .setFormat(Format::D32)
                .setSampleCount(2u)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    }

    Texture* initialTextures[3u] = {
        multisampleColor.get(),
        resolvedColor.get(),
        multisampleDepth.get(),
    };
    const usize initialTextureCount = multisampleDepth ? LengthOf(initialTextures) : 2u;
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        initialTextureCount,
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId colorResource = graph.importTexture(
        multisampleColor,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/full_multisample_color_clear"))
            .setMarkerLabel("Full Multisample Color Clear")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(colorResource.valid());
    GpuGraphResourceId depthResource;
    if(multisampleDepth){
        depthResource = graph.importTexture(
            multisampleDepth,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/descriptor_buffer/full_multisample_depth_clear"))
                .setMarkerLabel("Full Multisample Depth Clear")
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::Common)
        );
        ASSERT_TRUE(depthResource.valid());
    }

    const GpuQueueRequest transferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc colorTaskDesc;
    colorTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/full_multisample_color_clear_task"))
        .setMarkerLabel("Full Multisample Color Clear Task")
        .setQueue(transferQueue)
    ;
    QueueSubmissionToken colorAcceptedToken{
        .queue = CommandQueue::Graphics,
        .value = 1u,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    GpuClearTextureTaskDesc colorClear;
    colorClear.acceptedToken = &colorAcceptedToken;
    colorClear.destination = colorResource;
    colorClear.valueType = GpuClearTextureTaskValueType::Float;
    colorClear.floatValue = s_ClearColor;
    const GpuTaskId colorTask = graph.addClearTextureTask(colorTaskDesc, colorClear);
    ASSERT_TRUE(colorTask.valid());
    EXPECT_FALSE(colorAcceptedToken.valid());

    QueueSubmissionToken depthAcceptedToken;
    GpuTaskId depthTask;
    if(multisampleDepth){
        GpuTaskDesc depthTaskDesc;
        depthTaskDesc
            .setIdentity(Name("tests/descriptor_buffer/full_multisample_depth_clear_task"))
            .setMarkerLabel("Full Multisample Depth Clear Task")
            .setQueue(transferQueue)
            .setDependencies(&colorTask, 1u)
        ;
        GpuClearTextureTaskDesc depthClear;
        depthClear.acceptedToken = &depthAcceptedToken;
        depthClear.destination = depthResource;
        depthClear.depthValue = 0.375f;
        depthClear.valueType = GpuClearTextureTaskValueType::DepthStencil;
        depthClear.clearDepth = true;
        depthTask = graph.addClearTextureTask(depthTaskDesc, depthClear);
        ASSERT_TRUE(depthTask.valid());
        EXPECT_FALSE(depthAcceptedToken.valid());
    }

    const GpuQueueCapability::Mask colorCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Transfer) | static_cast<u8>(GpuQueueCapability::Compute)
    );
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskAt(colorTask.index).queue.requiredCapabilities, colorCapabilities);
        EXPECT_EQ(declarations.taskAt(colorTask.index).resourceUses[0u].requiredState, ResourceStates::CopyDest);
        if(depthTask.valid()){
            const GpuQueueCapability::Mask depthCapabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Transfer) | static_cast<u8>(GpuQueueCapability::Graphics)
            );
            EXPECT_EQ(declarations.taskAt(depthTask.index).queue.requiredCapabilities, depthCapabilities);
            EXPECT_EQ(declarations.taskAt(depthTask.index).resourceUses[0u].requiredState, ResourceStates::CopyDest);
        }
    }

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/full_multisample_clear_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    const GpuTaskQueueAssignment* const colorAssignment = assignments.find(colorTask);
    ASSERT_NE(colorAssignment, nullptr);
    EXPECT_EQ(colorAssignment->queueClass, CommandQueue::Graphics);
    if(depthTask.valid()){
        const GpuTaskQueueAssignment* const depthAssignment = assignments.find(depthTask);
        ASSERT_NE(depthAssignment, nullptr);
        EXPECT_EQ(depthAssignment->queueClass, CommandQueue::Graphics);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture capture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &capture
    )) << "failed packet " << failedPacket.index;
    EXPECT_EQ(capture.recordCount(), depthTask.valid() ? 2u : 1u);
    const GpuCommandIrBuiltinTaskRecord* const colorRecord = capture.recordAt(0u);
    ASSERT_NE(colorRecord, nullptr);
    EXPECT_EQ(colorRecord->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(colorRecord->destination, colorResource);

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
    const GpuSubmissionPacketId colorPacket = views.compiled.packetForTask(colorTask);
    ASSERT_TRUE(colorPacket.valid());
    EXPECT_TRUE(colorAcceptedToken.valid());
    EXPECT_EQ(colorAcceptedToken.value, transaction.packetToken(colorPacket).value);
    if(depthTask.valid()){
        const GpuSubmissionPacketId depthPacket = views.compiled.packetForTask(depthTask);
        ASSERT_TRUE(depthPacket.valid());
        EXPECT_TRUE(depthAcceptedToken.valid());
        EXPECT_EQ(depthAcceptedToken.value, transaction.packetToken(depthPacket).value);
    }
    ASSERT_TRUE(device.waitForIdle());

    const CommandListHandle resolveList = device.createCommandList();
    ASSERT_TRUE(resolveList);
    resolveList->open();
    resolveList->resolveTexture(
        resolvedColor.get(),
        s_AllSubresources,
        multisampleColor.get(),
        s_AllSubresources
    );
    resolveList->copyTexture(readback.get(), TextureSlice{}, resolvedColor.get(), TextureSlice{});
    ASSERT_FALSE(resolveList->commandRecordingFailed());
    resolveList->close();
    CommandList* const resolveLists[] = { resolveList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        resolveLists,
        LengthOf(resolveLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());

    usize rowPitch = 0u;
    const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        readback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &rowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(rowPitch, static_cast<usize>(s_Width * 4u));
    constexpr u8 s_ExpectedColor[] = { 64u, 128u, 191u, 255u };
    for(u32 y = 0u; y < s_Height; ++y){
        for(u32 x = 0u; x < s_Width; ++x){
            const u8* const pixel = readbackBytes + static_cast<usize>(y) * rowPitch + x * 4u;
            for(u32 channel = 0u; channel < LengthOf(s_ExpectedColor); ++channel)
                EXPECT_EQ(pixel[channel], s_ExpectedColor[channel]);
        }
    }
    device.unmapStagingTexture(readback.get());
}


// Merged graph tasks share one native command list but do not share ownership of dynamic rendering. Exercise one
// primitive from every built-in transfer source and prove both the task boundary and clear-hook boundary are closed.
TEST_F(DescriptorBufferRoundTripTest, MergedGraphBuiltInsEndInheritedAndHookOpenedDynamicRendering){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_UploadWords[] = { 0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u };
    static constexpr u32 s_ClearValue = 0xdecafbadU;

    const BufferDesc transferBufferDesc = BufferDesc()
        .setByteSize(sizeof(s_UploadWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    const BufferHandle uploadDestination = device.createBuffer(transferBufferDesc);
    const BufferHandle copyDestination = device.createBuffer(transferBufferDesc);
    const BufferHandle clearDestination = device.createBuffer(transferBufferDesc);
    ASSERT_NE(uploadDestination.get(), nullptr);
    ASSERT_NE(copyDestination.get(), nullptr);
    ASSERT_NE(clearDestination.get(), nullptr);

    const TextureDesc copyTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    const TextureHandle copySource = device.createTexture(copyTextureDesc);
    const TextureHandle copyDestinationTexture = device.createTexture(copyTextureDesc);
    const TextureHandle clearDestinationTexture = device.createTexture(
        TextureDesc(copyTextureDesc)
            .setFormat(Format::RGBA8_UINT)
            .setInRenderTarget(true)
    );
    ASSERT_NE(copySource.get(), nullptr);
    ASSERT_NE(copyDestinationTexture.get(), nullptr);
    ASSERT_NE(clearDestinationTexture.get(), nullptr);
    const FramebufferHandle framebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(clearDestinationTexture.get())
    );
    ASSERT_NE(framebuffer.get(), nullptr);
    Texture* const initialTextures[] = {
        copySource.get(),
        copyDestinationTexture.get(),
        clearDestinationTexture.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId uploadDestinationResource = graph.importBuffer(
        uploadDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_upload_destination"))
            .setMarkerLabel("Merged Rendering Upload Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId copyDestinationResource = graph.importBuffer(
        copyDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_copy_destination"))
            .setMarkerLabel("Merged Rendering Copy Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId clearDestinationResource = graph.importBuffer(
        clearDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_clear_destination"))
            .setMarkerLabel("Merged Rendering Clear Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId copySourceResource = graph.importTexture(
        copySource,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_copy_source"))
            .setMarkerLabel("Merged Rendering Copy Source")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId copyDestinationTextureResource = graph.importTexture(
        copyDestinationTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_copy_texture_destination"))
            .setMarkerLabel("Merged Rendering Copy Texture Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId clearDestinationTextureResource = graph.importTexture(
        clearDestinationTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_rendering_clear_texture_destination"))
            .setMarkerLabel("Merged Rendering Clear Texture Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(uploadDestinationResource.valid());
    ASSERT_TRUE(copyDestinationResource.valid());
    ASSERT_TRUE(clearDestinationResource.valid());
    ASSERT_TRUE(copySourceResource.valid());
    ASSERT_TRUE(copyDestinationTextureResource.valid());
    ASSERT_TRUE(clearDestinationTextureResource.valid());
    const GpuUploadBlobId uploadBlob = graph.copyUploadData(
        s_UploadWords,
        sizeof(s_UploadWords),
        alignof(u32)
    );
    ASSERT_TRUE(uploadBlob.valid());

    const GpuQueueCapability::Mask allCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Transfer)
    );
    const GpuQueueRequest graphicsTransferQueue{
        allCapabilities,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = GpuTaskCostHint::Tiny;
    firstScheduling.overlapPreferred = false;
    firstScheduling.allowPacketMerge = true;
    GpuTaskSchedulingHint chainedScheduling = firstScheduling;
    chainedScheduling.mergeWithPrevious = true;

    const GpuTaskResourceUse openerUses[] = {
        {
            .resource = clearDestinationTextureResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool openerObservedActive = false;
    GpuTaskDesc openerDesc;
    openerDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_opener"))
        .setMarkerLabel("Merged Rendering Opener")
        .setQueue(graphicsTransferQueue)
        .setScheduling(firstScheduling)
        .setResourceUses(openerUses, LengthOf(openerUses))
    ;
    const GpuTaskId openerTask = graph.addTask<NativePacketRenderPassBoundaryTask>(
        openerDesc,
        NativePacketRenderPassBoundaryTask::Payload{
            .framebuffer = framebuffer.get(),
            .observed = &openerObservedActive,
        }
    );
    ASSERT_TRUE(openerTask.valid());

    const GpuTaskResourceUse boundaryProbeUses[] = {
        {
            .resource = clearDestinationTextureResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool boundaryObservedInactive = false;
    GpuTaskDesc boundaryProbeDesc;
    boundaryProbeDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_boundary_probe"))
        .setMarkerLabel("Merged Rendering Boundary Probe")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&openerTask, 1u)
        .setResourceUses(boundaryProbeUses, LengthOf(boundaryProbeUses))
    ;
    const GpuTaskId boundaryProbeTask = graph.addTask<NativePacketRenderPassBoundaryTask>(
        boundaryProbeDesc,
        NativePacketRenderPassBoundaryTask::Payload{
            .observed = &boundaryObservedInactive,
        }
    );
    ASSERT_TRUE(boundaryProbeTask.valid());

    QueueSubmissionToken uploadAcceptedToken;
    GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_upload"))
        .setMarkerLabel("Merged Rendering Upload")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&boundaryProbeTask, 1u)
    ;
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadDesc,
        GpuUploadBufferTaskDesc{
            .source = uploadBlob,
            .destination = uploadDestinationResource,
            .finalState = ResourceStates::CopySource,
            .acceptedToken = &uploadAcceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());

    const GpuCopyBufferTaskRegion copyBufferRegion{
        .source = uploadDestinationResource,
        .destination = copyDestinationResource,
        .dataSizeBytes = sizeof(s_UploadWords),
    };
    QueueSubmissionToken copyBufferAcceptedToken;
    GpuTaskDesc copyBufferDesc;
    copyBufferDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_copy_buffer"))
        .setMarkerLabel("Merged Rendering Buffer Copy")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&uploadTask, 1u)
    ;
    const GpuTaskId copyBufferTask = graph.addCopyBufferTask(
        copyBufferDesc,
        GpuCopyBufferTaskDesc{
            .regions = &copyBufferRegion,
            .regionCount = 1u,
            .acceptedToken = &copyBufferAcceptedToken,
        }
    );
    ASSERT_TRUE(copyBufferTask.valid());

    QueueSubmissionToken clearBufferAcceptedToken;
    GpuTaskDesc clearBufferDesc;
    clearBufferDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_clear_buffer"))
        .setMarkerLabel("Merged Rendering Buffer Clear")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&copyBufferTask, 1u)
    ;
    const GpuTaskId clearBufferTask = graph.addClearBufferTask(
        clearBufferDesc,
        GpuClearBufferTaskDesc{
            .destination = clearDestinationResource,
            .clearValue = s_ClearValue,
            .acceptedToken = &clearBufferAcceptedToken,
        }
    );
    ASSERT_TRUE(clearBufferTask.valid());

    const GpuCopyTextureTaskRegion copyTextureRegion{
        .source = copySourceResource,
        .sourceSlice = {},
        .destination = copyDestinationTextureResource,
        .destinationSlice = {},
    };
    QueueSubmissionToken copyTextureAcceptedToken;
    GpuTaskDesc copyTextureTaskDesc;
    copyTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_copy_texture"))
        .setMarkerLabel("Merged Rendering Texture Copy")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&clearBufferTask, 1u)
    ;
    const GpuTaskId copyTextureTask = graph.addCopyTextureTask(
        copyTextureTaskDesc,
        GpuCopyTextureTaskDesc{
            .regions = &copyTextureRegion,
            .regionCount = 1u,
            .acceptedToken = &copyTextureAcceptedToken,
        }
    );
    ASSERT_TRUE(copyTextureTask.valid());

    struct ClearHookState{
        Framebuffer* framebuffer = nullptr;
        bool openedRenderPass = false;
        bool observedInactiveAfterClear = false;
    };
    ClearHookState clearHookState{
        .framebuffer = framebuffer.get(),
    };
    const GpuClearTextureTaskRecordHook beforeClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        ClearHookState* const state = static_cast<ClearHookState*>(rawState);
        if(!state || !state->framebuffer)
            return false;
        GraphicsState graphicsState;
        graphicsState.setFramebuffer(state->framebuffer);
        commandList.setGraphicsState(graphicsState);
        state->openedRenderPass = commandList.isRenderPassActive();
        return state->openedRenderPass;
    };
    const GpuClearTextureTaskRecordHook afterClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        ClearHookState* const state = static_cast<ClearHookState*>(rawState);
        if(!state)
            return false;
        state->observedInactiveAfterClear = !commandList.isRenderPassActive();
        return state->observedInactiveAfterClear;
    };
    QueueSubmissionToken clearTextureAcceptedToken;
    GpuClearTextureTaskDesc clearTexturePayload;
    clearTexturePayload.destination = clearDestinationTextureResource;
    clearTexturePayload.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTexturePayload.valueType = GpuClearTextureTaskValueType::UInt;
    clearTexturePayload.uintValue = UIntColor(0x11u, 0x22u, 0x33u, 0x44u);
    clearTexturePayload.recordHooks = GpuClearTextureTaskRecordHooks{
        .context = &clearHookState,
        .beforeClear = beforeClear,
        .afterClear = afterClear,
    };
    clearTexturePayload.acceptedToken = &clearTextureAcceptedToken;
    GpuTaskDesc clearTextureTaskDesc;
    clearTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_rendering_clear_texture"))
        .setMarkerLabel("Merged Rendering Texture Clear")
        .setQueue(graphicsTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&copyTextureTask, 1u)
    ;
    const GpuTaskId clearTextureTask = graph.addClearTextureTask(clearTextureTaskDesc, clearTexturePayload);
    ASSERT_TRUE(clearTextureTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = allCapabilities,
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/merged_rendering_scratch"));
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(openerTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 7u);
    EXPECT_EQ(views.compiled.packetForTask(boundaryProbeTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(uploadTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(copyBufferTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(clearBufferTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(copyTextureTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(clearTextureTask), packet);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(openerObservedActive);
    EXPECT_TRUE(boundaryObservedInactive);
    EXPECT_TRUE(clearHookState.openedRenderPass);
    EXPECT_TRUE(clearHookState.observedInactiveAfterClear);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, openerTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

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
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(uploadAcceptedToken.valid());
    ASSERT_TRUE(copyBufferAcceptedToken.valid());
    ASSERT_TRUE(clearBufferAcceptedToken.valid());
    ASSERT_TRUE(copyTextureAcceptedToken.valid());
    ASSERT_TRUE(clearTextureAcceptedToken.valid());
    EXPECT_EQ(uploadAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(uploadAcceptedToken.value, packetToken.value);
    EXPECT_EQ(copyBufferAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(copyBufferAcceptedToken.value, packetToken.value);
    EXPECT_EQ(clearBufferAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(clearBufferAcceptedToken.value, packetToken.value);
    EXPECT_EQ(copyTextureAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(copyTextureAcceptedToken.value, packetToken.value);
    EXPECT_EQ(clearTextureAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(clearTextureAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(
        device.mapBuffer(copyDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_UploadWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_UploadWords[wordIndex]);
    device.unmapBuffer(copyDestination.get());
    const u32* const clearedWords = static_cast<const u32*>(
        device.mapBuffer(clearDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(clearedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_UploadWords); ++wordIndex)
        EXPECT_EQ(clearedWords[wordIndex], s_ClearValue);
    device.unmapBuffer(clearDestination.get());

    const StagingTextureHandle clearTextureReadback = device.createStagingTexture(
        clearDestinationTexture->getDescription(),
        CpuAccessMode::Read
    );
    ASSERT_NE(clearTextureReadback.get(), nullptr);
    const CommandListHandle clearTextureReadbackCommandList = device.createCommandList();
    ASSERT_NE(clearTextureReadbackCommandList.get(), nullptr);
    clearTextureReadbackCommandList->open(finalState);
    ASSERT_TRUE(clearTextureReadbackCommandList->hasCommandBuffer());
    clearTextureReadbackCommandList->copyTexture(
        clearTextureReadback.get(),
        TextureSlice{},
        clearDestinationTexture.get(),
        TextureSlice{}
    );
    clearTextureReadbackCommandList->close();
    CommandList* const clearTextureReadbackLists[] = { clearTextureReadbackCommandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        clearTextureReadbackLists,
        LengthOf(clearTextureReadbackLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    usize clearTextureReadbackRowPitch = 0u;
    const u8* const clearTextureReadbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        clearTextureReadback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &clearTextureReadbackRowPitch
    ));
    ASSERT_NE(clearTextureReadbackBytes, nullptr);
    ASSERT_GE(clearTextureReadbackRowPitch, 4u * sizeof(u32));
    for(u32 row = 0u; row < 4u; ++row){
        for(u32 column = 0u; column < 4u; ++column){
            const u8* const pixel = clearTextureReadbackBytes
                + static_cast<usize>(row) * clearTextureReadbackRowPitch
                + static_cast<usize>(column) * sizeof(u32)
            ;
            EXPECT_EQ(pixel[0u], 0x11u);
            EXPECT_EQ(pixel[1u], 0x22u);
            EXPECT_EQ(pixel[2u], 0x33u);
            EXPECT_EQ(pixel[3u], 0x44u);
        }
    }
    device.unmapStagingTexture(clearTextureReadback.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

