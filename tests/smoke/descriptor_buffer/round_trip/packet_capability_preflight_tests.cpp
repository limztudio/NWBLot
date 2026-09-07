// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The native recorder validates the capability declaration after the thunk has recorded its command stream. This
// deliberately records a real dispatch from a Transfer-declared task so the test proves the packet is rejected
// before any native submission can accept it.
struct NativePacketComputeCapabilityMismatchTask{
    struct Payload{
        ComputePipeline* pipeline = nullptr;
        bool* attempted = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.pipeline)
            return false;
        if(payload.attempted)
            *payload.attempted = true;

        ComputeState state;
        state.pipeline = payload.pipeline;
        commandList.setComputeState(state);
        commandList.dispatch(1u, 1u, 1u);
        return true;
    }
};


// These operations deliberately have no valid native body on a Transfer-only physical queue. Setup-only state is
// important because no later dispatch can retroactively expose it, while the terminal dispatch proves Final builds
// reject the command before Vulkan sees a compute opcode without an active pipeline.
struct NativePacketExactTransferCapabilityMismatchTask{
    enum class Operation : u8{
        SetComputeState,
        Dispatch,
    };

    struct Payload{
        Operation operation = Operation::SetComputeState;
        bool* attempted = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;

        switch(payload.operation){
        case Operation::SetComputeState:
            commandList.setComputeState(ComputeState{});
            return true;
        case Operation::Dispatch:
            commandList.dispatch(1u, 1u, 1u);
            return true;
        default:
            return false;
        }
    }
};


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderRejectsCommandsOutsideTaskCapabilities){
#if !defined(NWB_DEBUG)
    GTEST_SKIP() << "task command-capability validation is debug-only";
#else
    auto& device = DescriptorBufferRoundTripTest::device();

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/task_capability_mismatch"})
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
    ASSERT_TRUE(pipeline);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    bool attempted = false;
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/task_capability_mismatch"))
        .setMarkerLabel("Task Capability Mismatch")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
    ;
    const GpuTaskId task = graph.addTask<NativePacketComputeCapabilityMismatchTask>(
        taskDesc,
        NativePacketComputeCapabilityMismatchTask::Payload{
            .pipeline = pipeline.get(),
            .attempted = &attempted,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/task_capability_mismatch_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const GpuPhysicalQueueInfo* const assignedQueue = views.compiled.queueInfo(views.compiled.packet(packet).plan->queue);
    ASSERT_NE(assignedQueue, nullptr);
    EXPECT_EQ(assignedQueue->queueClass, CommandQueue::Graphics);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(attempted);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
#endif
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderRejectsComputeSetupAndDispatchOnExactTransferQueueInAllBuilds){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Exact capability validation: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

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
        GTEST_SKIP() << "Exact capability validation: adapter exposes no physical Transfer-only queue.";

    struct Case{
        NativePacketExactTransferCapabilityMismatchTask::Operation operation;
        const char* identity;
        const char* label;
    };
    const Case cases[] = {
        {
            NativePacketExactTransferCapabilityMismatchTask::Operation::SetComputeState,
            "tests/descriptor_buffer/exact_transfer_set_compute_state",
            "Exact Transfer Set Compute State",
        },
        {
            NativePacketExactTransferCapabilityMismatchTask::Operation::Dispatch,
            "tests/descriptor_buffer/exact_transfer_dispatch",
            "Exact Transfer Dispatch",
        },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        GpuTaskGraph graph(transferScope.arena());
        bool attempted = false;
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name(testCase.identity))
            .setMarkerLabel(testCase.label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                false,
                false,
            })
            .setScheduling(scheduling)
        ;
        const GpuTaskId task = graph.addTask<NativePacketExactTransferCapabilityMismatchTask>(
            taskDesc,
            NativePacketExactTransferCapabilityMismatchTask::Payload{
                .operation = testCase.operation,
                .attempted = &attempted,
            }
        );
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(transferScope.arena());
        GpuTaskGraphQueueAssignments assignments(transferScope.arena());
        GpuCompiledGraph compiledGraph(transferScope.arena());
        Alloc::ScratchArena scratchArena(Name(testCase.identity));
        const GpuTaskGraphCompiler compiler;
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        const GpuPhysicalQueueInfo* const assignedQueue = views.compiled.queueInfo(views.compiled.packet(packet).plan->queue);
        ASSERT_NE(assignedQueue, nullptr);
        const u8 assignedCapabilityBits = static_cast<u8>(assignedQueue->capabilities);
        EXPECT_TRUE(assignedQueue->id.valid());
        EXPECT_TRUE(device.matchesPhysicalQueueIdentity(assignedQueue->id));
        EXPECT_EQ(assignedQueue->queueClass, CommandQueue::Transfer);
        EXPECT_TRUE(assignedQueue->dedicated);
        EXPECT_NE(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Transfer), 0u);
        EXPECT_EQ(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Graphics), 0u);
        EXPECT_EQ(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Compute), 0u);

        GpuRecordedGraph recordedGraph(transferScope.arena());
        const GpuNativePacketRecorder recorder(device);
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
            recordedGraph
        ));
        EXPECT_TRUE(attempted);
        EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());

        GpuGraphSubmissionTransaction transaction(transferScope.arena());
        transaction.reset(compiledGraph);
        EXPECT_FALSE(transaction.hasAcceptedPackets());
        EXPECT_FALSE(transaction.packetToken(packet).valid());
    }
}


TEST_F(DescriptorBufferRoundTripTest, OrdinaryCommandIrReplayPreflightRejectsExactQueueCapabilityMismatch){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Command-IR exact capability replay: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    bool hasTransferOnlyQueue = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u8 capabilityBits = static_cast<u8>(topology.queues[queueIndex].capabilities);
        hasTransferOnlyQueue = hasTransferOnlyQueue || (
            topology.queues[queueIndex].queueClass == CommandQueue::Transfer
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
            && (capabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) == 0u
        );
    }
    if(!hasTransferOnlyQueue)
        GTEST_SKIP() << "Command-IR exact capability replay: adapter exposes no physical Transfer-only queue.";

    const TextureDesc textureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UINT)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto texture = device.createTexture(textureDesc);
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph graph(transferScope.arena());
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/replay_exact_transfer_texture"))
            .setMarkerLabel("Replay Exact Transfer Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(textureResource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc taskDesc;
    GpuClearTextureTaskDesc clearDesc;
    clearDesc.destination = textureResource;
    clearDesc.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearDesc.valueType = GpuClearTextureTaskValueType::UInt;
    clearDesc.uintValue = UIntColor(1u, 2u, 3u, 4u);
    const GpuTaskResourceUse uses[] = {
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = GpuTaskResourceRange{ .textureSubresources = clearDesc.subresources },
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/replay_exact_transfer_clear"))
        .setMarkerLabel("Replay Exact Transfer Clear")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(uses, LengthOf(uses))
    ;
    const GpuTaskId task = graph.addTask<NativePacketExactTransferCapabilityMismatchTask>(
        taskDesc,
        NativePacketExactTransferCapabilityMismatchTask::Payload{}
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(transferScope.arena());
    GpuTaskGraphQueueAssignments assignments(transferScope.arena());
    GpuCompiledGraph compiledGraph(transferScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/replay_exact_transfer_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const GpuPhysicalQueueInfo* const assignedQueue = views.compiled.queueInfo(views.compiled.packet(packet).plan->queue);
    ASSERT_NE(assignedQueue, nullptr);
    const u8 assignedCapabilityBits = static_cast<u8>(assignedQueue->capabilities);
    ASSERT_EQ(assignedQueue->queueClass, CommandQueue::Transfer);
    ASSERT_NE(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Transfer), 0u);
    ASSERT_EQ(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Graphics), 0u);
    ASSERT_EQ(assignedCapabilityBits & static_cast<u8>(GpuQueueCapability::Compute), 0u);

    GpuCommandIrCapture capture(transferScope.arena());
    ASSERT_TRUE(capture.captureClearTexture(task, packet, assignedQueue->id, textureResource, clearDesc));
    ASSERT_EQ(capture.recordCount(), 1u);

    CommandListParameters commandListParameters;
    commandListParameters.setPhysicalQueue(assignedQueue->id);
    auto replayCommandList = device.createCommandList(commandListParameters);
    ASSERT_NE(replayCommandList.get(), nullptr);
    replayCommandList->open();
    ASSERT_TRUE(replayCommandList->isRecording());

    const GpuCommandIrReplayResult replayResult = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *replayCommandList
    );
    EXPECT_EQ(replayResult.error, GpuCommandIrReplayError::InvalidTextureClear);
    EXPECT_EQ(replayResult.recordIndex, 0u);
    EXPECT_FALSE(replayCommandList->commandRecordingFailed());

    replayCommandList->close();
    EXPECT_TRUE(replayCommandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

