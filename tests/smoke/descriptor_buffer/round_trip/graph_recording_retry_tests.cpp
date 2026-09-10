// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Two public recorder calls contend for one merged packet while its prefix thunk is blocked. This exercises the
// runtime claim, cancellation, abort, retry, and exactly-once discard contract without exposing packet lifecycle
// controls to tests.
struct ConcurrentPacketRecordingProbeTask{
    struct State{
        Atomic<u32> prefixRecordCount{ 0u };
        Atomic<u32> suffixRecordCount{ 0u };
        Atomic<u32> prefixDiscardCount{ 0u };
        Atomic<u32> suffixDiscardCount{ 0u };
        AtomicFlag prefixRecordEntered;
        AtomicFlag ownerRecordingProgress;
        AtomicFlag releasePrefixRecord;
        bool prefixShouldRecord = false;
    };

    struct Payload{
        State* state = nullptr;
        bool isPrefix = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.state || !commandList.isRecording())
            return false;

        State& state = *payload.state;
        if(!payload.isPrefix){
            state.suffixRecordCount.fetch_add(1u, MemoryOrder::relaxed);
            return true;
        }

        const u32 priorPrefixRecordCount = state.prefixRecordCount.fetch_add(1u, MemoryOrder::relaxed);
        if(priorPrefixRecordCount != 0u)
            return state.prefixShouldRecord;

        state.prefixRecordEntered.test_and_set(MemoryOrder::release);
        state.prefixRecordEntered.notify_all();
        state.ownerRecordingProgress.test_and_set(MemoryOrder::release);
        state.ownerRecordingProgress.notify_all();
        while(!state.releasePrefixRecord.test(MemoryOrder::acquire))
            state.releasePrefixRecord.wait(false, MemoryOrder::acquire);
        return state.prefixShouldRecord;
    }

    static void discarded(Payload& payload){
        if(!payload.state)
            return;
        if(payload.isPrefix)
            payload.state->prefixDiscardCount.fetch_add(1u, MemoryOrder::relaxed);
        else
            payload.state->suffixDiscardCount.fetch_add(1u, MemoryOrder::relaxed);
    }
};


TEST_F(DescriptorBufferRoundTripTest, BuiltInClearTextureHooksDiscardOnPacketRecordFailure){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UINT)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/clear_texture_hook_discard"))
            .setMarkerLabel("Clear Texture Hook Discard")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(textureResource.valid());

    struct HookState{
        u32 beforeCount = 0u;
        u32 afterCount = 0u;
        u32 discardedCount = 0u;
    } hooks;
    const GpuClearTextureTaskRecordHook beforeClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        HookState* const state = static_cast<HookState*>(rawState);
        if(!state)
            return false;
        ++state->beforeCount;
        return true;
    };
    const GpuClearTextureTaskRecordHook afterClear = [](
        void* const rawState,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        HookState* const state = static_cast<HookState*>(rawState);
        if(!state)
            return false;
        ++state->afterCount;
        return true;
    };
    const GpuClearTextureTaskDiscardedHook discarded = [](void* const rawState){
        HookState* const state = static_cast<HookState*>(rawState);
        if(state)
            ++state->discardedCount;
    };

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/clear_texture_hook_discard_clear"))
        .setMarkerLabel("Clear Texture Hook Discard Clear")
        .setQueue(graphicsQueue)
        .setScheduling(clearScheduling)
    ;
    GpuClearTextureTaskDesc clearTaskPayload;
    clearTaskPayload.destination = textureResource;
    clearTaskPayload.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTaskPayload.valueType = GpuClearTextureTaskValueType::UInt;
    clearTaskPayload.uintValue = UIntColor(0x12345678u);
    clearTaskPayload.recordHooks = GpuClearTextureTaskRecordHooks{
        .context = &hooks,
        .beforeClear = beforeClear,
        .afterClear = afterClear,
        .discarded = discarded,
    };
    const GpuTaskId clearTask = graph.addClearTextureTask(clearDesc, clearTaskPayload);
    ASSERT_TRUE(clearTask.valid());

    bool shouldRecord = false;
    bool retryTaskAttempted = false;
    GpuTaskSchedulingHint retryScheduling = clearScheduling;
    retryScheduling.mergeWithPrevious = true;
    GpuTaskDesc retryDesc;
    retryDesc
        .setIdentity(Name("tests/descriptor_buffer/clear_texture_hook_discard_retry"))
        .setMarkerLabel("Clear Texture Hook Discard Retry")
        .setQueue(graphicsQueue)
        .setScheduling(retryScheduling)
        .setDependencies(&clearTask, 1u)
    ;
    const GpuTaskId retryTask = graph.addTask<NativePacketCaptureRetryTask>(
        retryDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &retryTaskAttempted,
        }
    );
    ASSERT_TRUE(retryTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/clear_texture_hook_discard_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(clearTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(retryTask), packet);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(s_logger.has_value());
    const u32 messageCountBeforeRecordFailure = s_logger->messageCount();
    const u32 errorCountBeforeRecordFailure = s_logger->errorCount();
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_EQ(s_logger->messageCount(), messageCountBeforeRecordFailure + 1u);
    EXPECT_EQ(s_logger->errorCount(), errorCountBeforeRecordFailure);
    EXPECT_EQ(s_logger->lastType(), Core::Common::LogType::CriticalWarning);
    const GpuTaskGraphTaskView retryTaskView = views.declarations.taskAt(retryTask.index);
    const auto expectedFailure = StringFormat(
        DescriptorBufferRoundTripTest::arena(),
        NWB_TEXT("Gpu task graph: semantic record thunk for task identity '{}' marker '{}' returned false for packet {}:{} on assigned physical queue class {} index {} device generation {}"),
        StringConvert(retryTaskView.identity.c_str()),
        StringConvert(retryTaskView.markerLabel),
        packet.index,
        packet.generation,
        static_cast<u32>(queue.queueClass),
        queue.id.index,
        queue.id.deviceGeneration
    );
    EXPECT_TRUE(s_logger->sawMessageContaining(expectedFailure));
    EXPECT_EQ(hooks.discardedCount, 1u);
    const u64 failedRecordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    EXPECT_NE(failedRecordingAttemptGeneration, 0u);
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    EXPECT_FALSE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        failedRecordingAttemptGeneration
    ));
    EXPECT_TRUE(retryTaskAttempted);
    EXPECT_EQ(hooks.beforeCount, 1u);
    EXPECT_EQ(hooks.afterCount, 1u);
    EXPECT_EQ(hooks.discardedCount, 1u);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
    const GpuTaskGraphRecordingStatistics failedRecordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(failedRecordingStatistics.valid());
    EXPECT_EQ(failedRecordingStatistics.packetCount, 0u);
    EXPECT_EQ(failedRecordingStatistics.commandListCount, 0u);
    EXPECT_EQ(failedRecordingStatistics.commandListAcquisitionSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.graphBarrierRecordingSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.taskRecordSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.recordingSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.recordingElapsedSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.readyFrontierElapsedSeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.readyFrontierWorkerBusySeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.readyFrontierWorkerCapacitySeconds, 0.0);
    EXPECT_EQ(failedRecordingStatistics.readyFrontierWorkerUtilization(), 0.0);
}


TEST_F(DescriptorBufferRoundTripTest, ConcurrentRecordersClaimOneMergedPacketAndAbortForRetry){
    auto& device = DescriptorBufferRoundTripTest::device();
    ConcurrentPacketRecordingProbeTask::State state;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.allowPacketMerge = true;
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/concurrent_packet_recording_prefix"))
        .setMarkerLabel("Concurrent Packet Recording Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(prefixScheduling)
    ;
    const GpuTaskId prefixTask = graph.addTask<ConcurrentPacketRecordingProbeTask>(
        prefixDesc,
        ConcurrentPacketRecordingProbeTask::Payload{
            .state = &state,
            .isPrefix = true,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    GpuTaskSchedulingHint suffixScheduling = prefixScheduling;
    suffixScheduling.mergeWithPrevious = true;
    GpuTaskDesc suffixDesc;
    suffixDesc
        .setIdentity(Name("tests/descriptor_buffer/concurrent_packet_recording_suffix"))
        .setMarkerLabel("Concurrent Packet Recording Suffix")
        .setQueue(graphicsQueue)
        .setScheduling(suffixScheduling)
        .setDependencies(&prefixTask, 1u)
    ;
    const GpuTaskId suffixTask = graph.addTask<ConcurrentPacketRecordingProbeTask>(
        suffixDesc,
        ConcurrentPacketRecordingProbeTask::Payload{
            .state = &state,
            .isPrefix = false,
        }
    );
    ASSERT_TRUE(suffixTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/concurrent_packet_recording_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId packet;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 1u);
        packet = views.compiled.packetForTask(prefixTask);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(views.compiled.packetForTask(suffixTask), packet);
        const GpuCompiledPacketView packetView = views.compiled.packet(packet);
        ASSERT_TRUE(packetView.valid());
        EXPECT_EQ(packetView.plan->taskCount, 2u);
    }

    GpuRecordedGraph ownerRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuRecordedGraph contenderRecordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder ownerRecorder(device);
    const GpuNativePacketRecorder contenderRecorder(device);
    GpuSubmissionPacketId ownerFailedPacket;
    bool ownerRecordingResult = true;
    Thread ownerRecordingThread([&](){
        ownerRecordingResult = ownerRecorder.recordTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            prefixTask,
            suffixTask,
            ownerRecordedGraph,
            &ownerFailedPacket
        );
        state.ownerRecordingProgress.test_and_set(MemoryOrder::release);
        state.ownerRecordingProgress.notify_all();
    });

    while(!state.ownerRecordingProgress.test(MemoryOrder::acquire))
        state.ownerRecordingProgress.wait(false, MemoryOrder::acquire);
    const bool ownerRecordThunkEntered = state.prefixRecordEntered.test(MemoryOrder::acquire);
    if(!ownerRecordThunkEntered)
        ownerRecordingThread.join();
    ASSERT_TRUE(ownerRecordThunkEntered);

    GpuSubmissionPacketId contenderFailedPacket;
    EXPECT_FALSE(contenderRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        prefixTask,
        suffixTask,
        contenderRecordedGraph,
        &contenderFailedPacket
    ));
    EXPECT_FALSE(contenderFailedPacket.valid());
    EXPECT_FALSE(contenderRecordedGraph.packetSnapshot(packet).has_value());
    EXPECT_EQ(state.prefixRecordCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.suffixRecordCount.load(MemoryOrder::relaxed), 0u);

    state.releasePrefixRecord.test_and_set(MemoryOrder::release);
    state.releasePrefixRecord.notify_all();
    ownerRecordingThread.join();

    EXPECT_FALSE(ownerRecordingResult);
    EXPECT_EQ(ownerFailedPacket, packet);
    EXPECT_FALSE(ownerRecordedGraph.packetSnapshot(packet).has_value());
    EXPECT_EQ(state.prefixRecordCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.suffixRecordCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 1u);

    state.prefixShouldRecord = true;
    GpuRecordedGraph retryRecordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder retryRecorder(device);
    ASSERT_TRUE(retryRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        prefixTask,
        suffixTask,
        retryRecordedGraph
    ));
    ASSERT_TRUE(retryRecordedGraph.packetSnapshot(packet).has_value());
    EXPECT_EQ(state.prefixRecordCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(state.suffixRecordCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 1u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 1u);

    GpuGraphSubmissionTransaction retryTransaction(DescriptorBufferRoundTripTest::arena());
    retryTransaction.reset(compiledGraph);
    retryTransaction.rejectTask(
        graph,
        compiledGraph,
        suffixTask,
        retryRecordedGraph.recordingAttemptGeneration()
    );
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 2u);
    const GpuTaskGraphSubmissionStatistics retryStatistics = retryTransaction.submissionStatistics();
    ASSERT_TRUE(retryStatistics.valid());
    EXPECT_EQ(retryStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(retryStatistics.rejectedTaskCount, 2u);
    EXPECT_EQ(retryStatistics.rejectedSubmissionCount, 0u);

    retryTransaction.rejectTask(
        graph,
        compiledGraph,
        prefixTask,
        retryRecordedGraph.recordingAttemptGeneration()
    );
    EXPECT_TRUE(retryTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        retryRecordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(state.prefixDiscardCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(state.suffixDiscardCount.load(MemoryOrder::relaxed), 2u);
}


TEST_F(DescriptorBufferRoundTripTest, CommandIrCaptureRollsBackARejectedPacketBeforeRetry){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_buffer"))
            .setMarkerLabel("Command IR Rollback Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(bufferResource.valid());

    const GpuQueueRequest transferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        true,
        true,
    };
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_clear"))
        .setMarkerLabel("Command IR Rollback Clear")
        .setQueue(transferQueue)
    ;
    const GpuTaskId clearTask = graph.addClearBufferTask(
        clearDesc,
        GpuClearBufferTaskDesc{
            .destination = bufferResource,
            .clearValue = 0x8badf00dU,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    bool shouldRecord = false;
    bool retryTaskAttempted = false;
    GpuTaskSchedulingHint retryScheduling;
    retryScheduling.mergeWithPrevious = true;
    GpuTaskDesc retryDesc;
    retryDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_retry"))
        .setMarkerLabel("Command IR Rollback Retry")
        .setQueue(transferQueue)
        .setScheduling(retryScheduling)
        .setDependencies(&clearTask, 1u)
    ;
    const GpuTaskId retryTask = graph.addTask<NativePacketCaptureRetryTask>(
        retryDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &retryTaskAttempted,
        }
    );
    ASSERT_TRUE(retryTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/command_ir_rollback_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId packet;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 1u);
        packet = views.compiled.packetForTask(clearTask);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(views.compiled.packetForTask(retryTask), packet);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    EXPECT_TRUE(retryTaskAttempted);
    EXPECT_EQ(commandIrCapture.recordCount(), 0u);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());

    shouldRecord = true;
    retryTaskAttempted = false;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    EXPECT_TRUE(retryTaskAttempted);
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const captureRecord = commandIrCapture.recordAt(0u);
    ASSERT_NE(captureRecord, nullptr);
    EXPECT_EQ(captureRecord->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(captureRecord->task, clearTask);
    EXPECT_EQ(captureRecord->packet, packet);
    EXPECT_EQ(captureRecord->destination, bufferResource);

    // A non-empty capture belongs to this graph generation. Reject it before recording an unrelated packet that
    // contains no primitive command, rather than making old records appear to be a trace for the new graph.
    GpuTaskGraph foreignGraph(DescriptorBufferRoundTripTest::arena());
    bool foreignTaskAttempted = false;
    GpuTaskDesc foreignTaskDesc;
    foreignTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_foreign_graph"))
        .setMarkerLabel("Command IR Foreign Graph")
        .setQueue(transferQueue)
    ;
    const GpuTaskId foreignTask = foreignGraph.addTask<NativePacketCaptureRetryTask>(
        foreignTaskDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &foreignTaskAttempted,
        }
    );
    ASSERT_TRUE(foreignTask.valid());
    GpuTaskGraphAnalysis foreignAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments foreignAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph foreignCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena foreignScratchArena(Name("tests/descriptor_buffer/command_ir_foreign_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(foreignGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            foreignAnalysis,
            topology,
            foreignAssignments,
            foreignCompiledGraph,
            foreignScratchArena
        ));
    }
    GpuSubmissionPacketId foreignPacket;
    {
        const GpuTaskGraphReadViews foreignViews(foreignGraph, foreignCompiledGraph);
        ASSERT_TRUE(foreignViews.valid());
        foreignPacket = foreignViews.compiled.packetForTask(foreignTask);
        ASSERT_TRUE(foreignPacket.valid());
    }
    GpuRecordedGraph foreignRecordedGraph(DescriptorBufferRoundTripTest::arena());
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        foreignGraph,
        foreignCompiledGraph,
        GpuSubmissionPacketRange{ .first = foreignPacket, .packetCount = 1u },
        foreignRecordedGraph,
        nullptr,
        &commandIrCapture
    ));
    EXPECT_FALSE(foreignTaskAttempted);
    EXPECT_EQ(commandIrCapture.recordCount(), 1u);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    GpuGraphSubmissionTransaction foreignTransaction(DescriptorBufferRoundTripTest::arena());
    foreignTransaction.reset(foreignCompiledGraph);
    EXPECT_TRUE(foreignTransaction.discardUnaccepted(
        foreignGraph,
        foreignCompiledGraph,
        foreignRecordedGraph.recordingAttemptGeneration()
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

