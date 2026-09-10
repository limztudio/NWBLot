// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Test-owned analogue of the hybrid shadow tail's three-buffer restore transaction. The payload contains both the
// graph-owned source blobs and an independently copied declaration-time snapshot, so every stream is validated
// before the first task-owned transition or write. Compiler-owned prologue barriers may establish the task's
// declared state before this callback. This keeps failure injection out of renderer production code while exercising
// the same immutable-blob, packet-discard, final-state, and acceptance machinery.
struct GraphOwnedHybridHardwareMaterialContextRestoreTask{
    static constexpr usize s_StreamCount = 3u;
    static constexpr usize s_WordCount = 4u;

    struct Stream{
        GpuGraphResourceId destination;
        GpuUploadBlobId source;
        u32 expectedWords[s_WordCount] = {};
    };

    struct Payload{
        Stream streams[s_StreamCount];
        bool* recordAttempted = nullptr;
        bool* allBlobsMatched = nullptr;
        u32* matchedBlobCount = nullptr;
        u32* transitionAttemptCount = nullptr;
        u32* writeAttemptCount = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(payload.recordAttempted)
            *payload.recordAttempted = true;

        Buffer* destinations[s_StreamCount] = {};
        const void* sources[s_StreamCount] = {};
        usize sourceByteCounts[s_StreamCount] = {};
        for(usize streamIndex = 0u; streamIndex < s_StreamCount; ++streamIndex){
            const Stream& stream = payload.streams[streamIndex];
            destinations[streamIndex] = context.declarations.bufferForResource(stream.destination);
            sources[streamIndex] = context.declarations.uploadBlobData(stream.source, sourceByteCounts[streamIndex]);
            if(
                !destinations[streamIndex]
                || !sources[streamIndex]
                || sourceByteCounts[streamIndex] != sizeof(stream.expectedWords)
                || NWB_MEMCMP(sources[streamIndex], stream.expectedWords, sizeof(stream.expectedWords)) != 0
            )
                return false;
            if(payload.matchedBlobCount)
                ++*payload.matchedBlobCount;
        }
        if(payload.allBlobsMatched)
            *payload.allBlobsMatched = true;

        for(Buffer* const destination : destinations){
            if(payload.transitionAttemptCount)
                ++*payload.transitionAttemptCount;
            commandList.setBufferState(destination, ResourceStates::CopyDest);
        }
        commandList.commitBarriers();
        for(usize streamIndex = 0u; streamIndex < s_StreamCount; ++streamIndex){
            if(payload.writeAttemptCount)
                ++*payload.writeAttemptCount;
            if(!commandList.tryWriteBuffer(*destinations[streamIndex], sources[streamIndex], sourceByteCounts[streamIndex]))
                return false;
        }
        for(Buffer* const destination : destinations){
            if(payload.transitionAttemptCount)
                ++*payload.transitionAttemptCount;
            commandList.setBufferState(destination, ResourceStates::ShaderResource);
        }
        commandList.commitBarriers();
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


// The adaptive software-shadow route combines two typed buffer clears, a getter-only UAV traversal callback, and
// a stats readback in one selected Compute/Graphics packet.  This is the native counterpart of the compiler
// packet proof: it verifies that graph-built primitives establish the callback's states, preserve command order,
// and publish the copy token only after the containing packet accepts.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAdaptiveShadowPrimitiveChainRecordsAndPublishesReadback){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeScratchBuffer = [&device](const CpuAccessMode::Enum cpuAccess){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(u32) * 4u)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setCpuAccess(cpuAccess)
        );
    };
    const BufferHandle edgeStats = makeScratchBuffer(CpuAccessMode::Read);
    const BufferHandle edgeCounter = makeScratchBuffer(CpuAccessMode::Read);
    const BufferHandle statsReadback = makeScratchBuffer(CpuAccessMode::Read);
    ASSERT_NE(edgeStats.get(), nullptr);
    ASSERT_NE(edgeCounter.get(), nullptr);
    ASSERT_NE(statsReadback.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const GpuGraphResourceId edgeStatsResource = importBuffer(
        edgeStats,
        Name("tests/descriptor_buffer/adaptive_shadow_edge_stats"),
        "Adaptive Shadow Edge Statistics"
    );
    const GpuGraphResourceId edgeCounterResource = importBuffer(
        edgeCounter,
        Name("tests/descriptor_buffer/adaptive_shadow_edge_counter"),
        "Adaptive Shadow Edge Counter"
    );
    const GpuGraphResourceId statsReadbackResource = importBuffer(
        statsReadback,
        Name("tests/descriptor_buffer/adaptive_shadow_stats_readback"),
        "Adaptive Shadow Statistics Readback"
    );
    ASSERT_TRUE(edgeStatsResource.valid());
    ASSERT_TRUE(edgeCounterResource.valid());
    ASSERT_TRUE(statsReadbackResource.valid());

    const GpuQueueRequest computeTransferQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        GpuQueuePreference::Compute,
        true,
        false,
    };
    GpuTaskSchedulingHint firstPrimitiveScheduling;
    firstPrimitiveScheduling.cost = GpuTaskCostHint::Tiny;
    firstPrimitiveScheduling.allowPacketMerge = true;
    GpuTaskDesc statsClearDesc;
    statsClearDesc
        .setIdentity(Name("tests/descriptor_buffer/adaptive_shadow_stats_clear"))
        .setMarkerLabel("Adaptive Shadow Statistics Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(firstPrimitiveScheduling)
    ;
    const GpuTaskId statsClear = graph.addClearBufferTask(
        statsClearDesc,
        GpuClearBufferTaskDesc{
            .destination = edgeStatsResource,
            .clearValue = 0u,
        }
    );
    ASSERT_TRUE(statsClear.valid());

    GpuTaskSchedulingHint chainedScheduling = firstPrimitiveScheduling;
    chainedScheduling.mergeWithPrevious = true;
    GpuTaskDesc counterClearDesc;
    counterClearDesc
        .setIdentity(Name("tests/descriptor_buffer/adaptive_shadow_counter_clear"))
        .setMarkerLabel("Adaptive Shadow Counter Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&statsClear, 1u)
    ;
    const GpuTaskId counterClear = graph.addClearBufferTask(
        counterClearDesc,
        GpuClearBufferTaskDesc{
            .destination = edgeCounterResource,
            .clearValue = 0u,
        }
    );
    ASSERT_TRUE(counterClear.valid());

    const GpuTaskResourceUse shadowUses[] = {
        {
            .resource = edgeStatsResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = edgeCounterResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc shadowDesc;
    shadowDesc
        .setIdentity(Name("tests/descriptor_buffer/adaptive_shadow_visibility"))
        .setMarkerLabel("Adaptive Shadow Visibility")
        .setQueue(computeTransferQueue)
        .setScheduling(chainedScheduling)
        .setDependencies(&counterClear, 1u)
        .setResourceUses(shadowUses, LengthOf(shadowUses))
    ;
    bool shadowRecorded = false;
    const GpuTaskId shadow = graph.addTask<NativePacketPrefixTask>(
        shadowDesc,
        NativePacketPrefixTask::Payload{
            .buffer = edgeStats.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .additionalBuffer = edgeCounter.get(),
            .expectedAdditionalBufferState = ResourceStates::UnorderedAccess,
            .recorded = &shadowRecorded,
        }
    );
    ASSERT_TRUE(shadow.valid());

    const GpuCopyBufferTaskRegion statsReadbackRegion{
        .source = edgeStatsResource,
        .destination = statsReadbackResource,
        .dataSizeBytes = sizeof(u32) * 4u,
    };
    GpuTaskSchedulingHint statsCopyScheduling = chainedScheduling;
    statsCopyScheduling.allowMergeAcrossConsumerFrontier = true;
    GpuTaskDesc statsCopyDesc;
    statsCopyDesc
        .setIdentity(Name("tests/descriptor_buffer/adaptive_shadow_stats_readback"))
        .setMarkerLabel("Adaptive Shadow Statistics Readback")
        .setQueue(computeTransferQueue)
        .setScheduling(statsCopyScheduling)
        .setDependencies(&shadow, 1u)
    ;
    QueueSubmissionToken statsReadbackAcceptedToken;
    const GpuTaskId statsCopy = graph.addCopyBufferTask(
        statsCopyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &statsReadbackRegion,
            .regionCount = 1u,
            .acceptedToken = &statsReadbackAcceptedToken,
        }
    );
    ASSERT_TRUE(statsCopy.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/adaptive_shadow_primitives_scratch"));
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId shadowPacket = views.compiled.packetForTask(shadow);
    ASSERT_TRUE(shadowPacket.valid());
    EXPECT_EQ(views.compiled.packetForTask(statsClear), shadowPacket);
    EXPECT_EQ(views.compiled.packetForTask(counterClear), shadowPacket);
    EXPECT_EQ(views.compiled.packetForTask(statsCopy), shadowPacket);
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    ASSERT_EQ(views.compiled.packet(shadowPacket).plan->taskCount, 4u);

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
    EXPECT_TRUE(shadowRecorded);
    EXPECT_FALSE(statsReadbackAcceptedToken.valid());
    ASSERT_EQ(commandIrCapture.recordCount(), 3u);
    const GpuCommandIrBuiltinTaskRecord* const statsClearCapture = commandIrCapture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const counterClearCapture = commandIrCapture.recordAt(1u);
    const GpuCommandIrBuiltinTaskRecord* const statsCopyCapture = commandIrCapture.recordAt(2u);
    ASSERT_NE(statsClearCapture, nullptr);
    ASSERT_NE(counterClearCapture, nullptr);
    ASSERT_NE(statsCopyCapture, nullptr);
    EXPECT_EQ(statsClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(statsClearCapture->task, statsClear);
    EXPECT_EQ(statsClearCapture->destination, edgeStatsResource);
    EXPECT_EQ(counterClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(counterClearCapture->task, counterClear);
    EXPECT_EQ(counterClearCapture->destination, edgeCounterResource);
    EXPECT_EQ(statsCopyCapture->opcode, GpuCommandIrOpcode::CopyBuffer);
    EXPECT_EQ(statsCopyCapture->task, statsCopy);
    EXPECT_EQ(statsCopyCapture->source, edgeStatsResource);
    EXPECT_EQ(statsCopyCapture->destination, statsReadbackResource);
    EXPECT_EQ(statsCopyCapture->dataSizeBytes, statsReadbackRegion.dataSizeBytes);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, shadow, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    const CommandListHandle stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(edgeStats.get()), ResourceStates::CopySource);
    EXPECT_EQ(stateProbe->getBufferState(statsReadback.get()), ResourceStates::CopyDest);
    stateProbe->close();

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
    const QueueSubmissionToken shadowToken = transaction.taskToken(views.compiled, shadow);
    ASSERT_TRUE(shadowToken.valid());
    EXPECT_TRUE(statsReadbackAcceptedToken.valid());
    EXPECT_EQ(statsReadbackAcceptedToken.queue, shadowToken.queue);
    EXPECT_EQ(statsReadbackAcceptedToken.value, shadowToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const readbackWords = static_cast<const u32*>(device.mapBuffer(*statsReadback, CpuAccessMode::Read));
    ASSERT_NE(readbackWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < 4u; ++wordIndex)
        EXPECT_EQ(readbackWords[wordIndex], 0u);
    device.unmapBuffer(*statsReadback);
}


// A healthy hybrid tail may overwrite its software material context with three independently retained hardware
// streams. This test-owned packet proves the immutable triple records as one acceptance unit, restores exact bytes,
// and leaves every destination shader-readable without exposing a renderer-only test control.
TEST_F(DescriptorBufferRoundTripTest, HybridHardwareMaterialContextRestoreWritesCompleteImmutableTriple){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_ExpectedWords[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount]
        [GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount] = {
        { 0x13c0ffeeu, 0x4a7b12d3u, 0x9e3779b9u, 0xfeedfaceu },
        { 0x0badf00du, 0x7f4a7c15u, 0x6d2b79f5u, 0xd1cebeefu },
        { 0x58c4a931u, 0xa17ef20du, 0x349bc862u, 0xc001d00du },
    };
    u32 sourceWords[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount]
        [GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount] = {};
    NWB_MEMCPY(sourceWords, sizeof(sourceWords), s_ExpectedWords, sizeof(s_ExpectedWords));
    BufferHandle destinations[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount];
    for(BufferHandle& destination : destinations){
        destination = device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(sourceWords[0u]))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
        ASSERT_NE(destination.get(), nullptr);
    }

    bool recordAttempted = false;
    bool allBlobsMatched = false;
    u32 matchedBlobCount = 0u;
    u32 transitionAttemptCount = 0u;
    u32 writeAttemptCount = 0u;
    QueueSubmissionToken acceptedToken;
    u32 discardedCount = 0u;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const Name resourceIdentities[] = {
        Name("tests/descriptor_buffer/hybrid_restore_instance_materials"),
        Name("tests/descriptor_buffer/hybrid_restore_instances"),
        Name("tests/descriptor_buffer/hybrid_restore_material_typed"),
    };
    const AStringView resourceMarkers[] = {
        "Hybrid Restore Instance Materials",
        "Hybrid Restore Instances",
        "Hybrid Restore Typed Materials",
    };
    GpuGraphResourceId destinationResources[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount];
    GpuUploadBlobId sourceBlobs[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount];
    GpuTaskResourceUse restoreUses[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount] = {};
    GraphOwnedHybridHardwareMaterialContextRestoreTask::Payload restorePayload;
    for(usize streamIndex = 0u; streamIndex < LengthOf(destinations); ++streamIndex){
        destinationResources[streamIndex] = graph.importBuffer(
            destinations[streamIndex],
            GpuGraphResourceDesc{}
                .setIdentity(resourceIdentities[streamIndex])
                .setMarkerLabel(resourceMarkers[streamIndex])
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        sourceBlobs[streamIndex] = graph.copyUploadData(
            sourceWords[streamIndex],
            sizeof(sourceWords[streamIndex]),
            alignof(u32)
        );
        ASSERT_TRUE(destinationResources[streamIndex].valid());
        ASSERT_TRUE(sourceBlobs[streamIndex].valid());
        restoreUses[streamIndex] = GpuTaskResourceUse{
            .resource = destinationResources[streamIndex],
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Write,
        };
        restorePayload.streams[streamIndex].destination = destinationResources[streamIndex];
        restorePayload.streams[streamIndex].source = sourceBlobs[streamIndex];
        NWB_MEMCPY(
            restorePayload.streams[streamIndex].expectedWords,
            sizeof(restorePayload.streams[streamIndex].expectedWords),
            sourceWords[streamIndex],
            sizeof(sourceWords[streamIndex])
        );
    }
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_EQ(declarations.uploadBlobCount(), GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount);
    }

    restorePayload.recordAttempted = &recordAttempted;
    restorePayload.allBlobsMatched = &allBlobsMatched;
    restorePayload.matchedBlobCount = &matchedBlobCount;
    restorePayload.transitionAttemptCount = &transitionAttemptCount;
    restorePayload.writeAttemptCount = &writeAttemptCount;
    restorePayload.acceptedToken = &acceptedToken;
    restorePayload.discardedCount = &discardedCount;

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc restoreDesc;
    restoreDesc
        .setIdentity(Name("tests/descriptor_buffer/hybrid_hardware_material_context_restore"))
        .setMarkerLabel("Hybrid Hardware Material Context Restore")
        .setQueue(GpuQueueRequest{
            static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(restoreUses, LengthOf(restoreUses))
    ;
    const GpuTaskId restoreTask = graph.addTask<GraphOwnedHybridHardwareMaterialContextRestoreTask>(
        restoreDesc,
        Move(restorePayload)
    );
    ASSERT_TRUE(restoreTask.valid());
    NWB_MEMSET(sourceWords, 0, sizeof(sourceWords));

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/hybrid_restore_success_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(restoreTask);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    const bool recorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    );
    EXPECT_TRUE(recordAttempted);
    EXPECT_TRUE(allBlobsMatched);
    EXPECT_EQ(matchedBlobCount, GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount);
    EXPECT_EQ(transitionAttemptCount, GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount * 2u);
    EXPECT_EQ(writeAttemptCount, GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount);
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_EQ(discardedCount, 0u);
    ASSERT_TRUE(recorded);

    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, restoreTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    const CommandListHandle stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    for(const BufferHandle& destination : destinations)
        EXPECT_EQ(stateProbe->getBufferState(destination.get()), ResourceStates::ShaderResource);
    stateProbe->close();

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    EXPECT_EQ(acceptedToken.physicalQueueIndex, packetToken.physicalQueueIndex);
    EXPECT_EQ(acceptedToken.deviceGeneration, packetToken.deviceGeneration);
    EXPECT_EQ(discardedCount, 0u);
    ASSERT_TRUE(device.waitForIdle());

    for(usize streamIndex = 0u; streamIndex < LengthOf(destinations); ++streamIndex){
        const u32* const restoredWords = static_cast<const u32*>(
            device.mapBuffer(*destinations[streamIndex], CpuAccessMode::Read)
        );
        ASSERT_NE(restoredWords, nullptr);
        EXPECT_EQ(
            NWB_MEMCMP(
                restoredWords,
                s_ExpectedWords[streamIndex],
                sizeof(s_ExpectedWords[streamIndex])
            ),
            0
        );
        device.unmapBuffer(*destinations[streamIndex]);
    }
}


// A same-sized mutation in only the third retained stream must reject before any task-owned transition or write. The
// packet discard publishes no token or final-state handoff, and all three prior software values remain byte-for-byte
// intact on the device.
TEST_F(DescriptorBufferRoundTripTest, HybridHardwareMaterialContextRestoreRejectsMismatchedThirdBlobBeforeWrites){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SoftwareWords[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount]
        [GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount] = {
        { 0x510f7a31u, 0x82d4c6e9u, 0x17b39f02u, 0xea6c45d8u },
        { 0x6ab2d143u, 0x934ef807u, 0x25c719beu, 0xf10d68a4u },
        { 0x7ce34195u, 0xa82f0db6u, 0x39d574e1u, 0xc60ab827u },
    };
    static constexpr u32 s_HardwareWords[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount]
        [GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount] = {
        { 0x13c0ffeeu, 0x4a7b12d3u, 0x9e3779b9u, 0xfeedfaceu },
        { 0x0badf00du, 0x7f4a7c15u, 0x6d2b79f5u, 0xd1cebeefu },
        { 0x58c4a931u, 0xa17ef20du, 0x349bc862u, 0xc001d00du },
    };
    BufferHandle destinations[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount];
    for(BufferHandle& destination : destinations){
        destination = device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(s_SoftwareWords[0u]))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
        ASSERT_NE(destination.get(), nullptr);
    }

    const CommandListHandle seedCommandList = device.createCommandList();
    ASSERT_NE(seedCommandList.get(), nullptr);
    seedCommandList->open();
    for(const BufferHandle& destination : destinations)
        seedCommandList->setBufferState(destination.get(), ResourceStates::CopyDest);
    seedCommandList->commitBarriers();
    for(usize streamIndex = 0u; streamIndex < LengthOf(destinations); ++streamIndex){
        ASSERT_TRUE(seedCommandList->tryWriteBuffer(
            *destinations[streamIndex],
            s_SoftwareWords[streamIndex],
            sizeof(s_SoftwareWords[streamIndex])
        ));
    }
    for(const BufferHandle& destination : destinations)
        seedCommandList->setBufferState(destination.get(), ResourceStates::ShaderResource);
    seedCommandList->commitBarriers();
    seedCommandList->close();
    CommandList* const seedCommandLists[] = { seedCommandList.get() };
    const QueueSubmissionToken seedToken = device.executeCommandLists(
        seedCommandLists,
        LengthOf(seedCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(seedToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    u32 sourceWords[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount]
        [GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount] = {};
    NWB_MEMCPY(sourceWords, sizeof(sourceWords), s_HardwareWords, sizeof(s_HardwareWords));
    sourceWords[2u][GraphOwnedHybridHardwareMaterialContextRestoreTask::s_WordCount - 1u] ^= 0xffffffffu;

    bool recordAttempted = false;
    bool allBlobsMatched = false;
    u32 matchedBlobCount = 0u;
    u32 transitionAttemptCount = 0u;
    u32 writeAttemptCount = 0u;
    QueueSubmissionToken acceptedToken;
    u32 discardedCount = 0u;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const Name resourceIdentities[] = {
        Name("tests/descriptor_buffer/hybrid_restore_rejected_instance_materials"),
        Name("tests/descriptor_buffer/hybrid_restore_rejected_instances"),
        Name("tests/descriptor_buffer/hybrid_restore_rejected_material_typed"),
    };
    const AStringView resourceMarkers[] = {
        "Rejected Hybrid Restore Instance Materials",
        "Rejected Hybrid Restore Instances",
        "Rejected Hybrid Restore Typed Materials",
    };
    GpuTaskResourceUse restoreUses[GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount] = {};
    GraphOwnedHybridHardwareMaterialContextRestoreTask::Payload restorePayload;
    for(usize streamIndex = 0u; streamIndex < LengthOf(destinations); ++streamIndex){
        const GpuGraphResourceId destinationResource = graph.importBuffer(
            destinations[streamIndex],
            GpuGraphResourceDesc{}
                .setIdentity(resourceIdentities[streamIndex])
                .setMarkerLabel(resourceMarkers[streamIndex])
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::ShaderResource)
        );
        const GpuUploadBlobId sourceBlob = graph.copyUploadData(
            sourceWords[streamIndex],
            sizeof(sourceWords[streamIndex]),
            alignof(u32)
        );
        ASSERT_TRUE(destinationResource.valid());
        ASSERT_TRUE(sourceBlob.valid());
        restoreUses[streamIndex] = GpuTaskResourceUse{
            .resource = destinationResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Write,
        };
        restorePayload.streams[streamIndex].destination = destinationResource;
        restorePayload.streams[streamIndex].source = sourceBlob;
        NWB_MEMCPY(
            restorePayload.streams[streamIndex].expectedWords,
            sizeof(restorePayload.streams[streamIndex].expectedWords),
            s_HardwareWords[streamIndex],
            sizeof(s_HardwareWords[streamIndex])
        );
    }

    restorePayload.recordAttempted = &recordAttempted;
    restorePayload.allBlobsMatched = &allBlobsMatched;
    restorePayload.matchedBlobCount = &matchedBlobCount;
    restorePayload.transitionAttemptCount = &transitionAttemptCount;
    restorePayload.writeAttemptCount = &writeAttemptCount;
    restorePayload.acceptedToken = &acceptedToken;
    restorePayload.discardedCount = &discardedCount;

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc restoreDesc;
    restoreDesc
        .setIdentity(Name("tests/descriptor_buffer/hybrid_hardware_material_context_restore_rejected"))
        .setMarkerLabel("Rejected Hybrid Hardware Material Context Restore")
        .setQueue(GpuQueueRequest{
            static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(restoreUses, LengthOf(restoreUses))
    ;
    const GpuTaskId restoreTask = graph.addTask<GraphOwnedHybridHardwareMaterialContextRestoreTask>(
        restoreDesc,
        Move(restorePayload)
    );
    ASSERT_TRUE(restoreTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/hybrid_restore_rejected_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(restoreTask);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(recordAttempted);
    EXPECT_FALSE(allBlobsMatched);
    EXPECT_EQ(matchedBlobCount, GraphOwnedHybridHardwareMaterialContextRestoreTask::s_StreamCount - 1u);
    EXPECT_EQ(transitionAttemptCount, 0u);
    EXPECT_EQ(writeAttemptCount, 0u);
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
    EXPECT_FALSE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, restoreTask));
    const u64 failedRecordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    EXPECT_NE(failedRecordingAttemptGeneration, 0u);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    EXPECT_FALSE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        failedRecordingAttemptGeneration
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    EXPECT_EQ(discardedCount, 1u);

    for(usize streamIndex = 0u; streamIndex < LengthOf(destinations); ++streamIndex){
        const u32* const retainedWords = static_cast<const u32*>(
            device.mapBuffer(*destinations[streamIndex], CpuAccessMode::Read)
        );
        ASSERT_NE(retainedWords, nullptr);
        EXPECT_EQ(
            NWB_MEMCMP(retainedWords, s_SoftwareWords[streamIndex], sizeof(s_SoftwareWords[streamIndex])),
            0
        );
        device.unmapBuffer(*destinations[streamIndex]);
    }
}


// The monolithic Shadow Visibility callback cannot know during declaration whether Shadow Prepare will produce a
// route. Its preceding typed white clear therefore supplies the normal all-lit fallback, while the getter-only
// callback observes the compiler-owned CopyDest -> UAV handoff and a later lighting callback observes UAV -> SRV.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedShadowVisibilityAllLitClearRecordsBeforeMonolithicCallback){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferHandle constants = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    constexpr u32 shadowLayerCount = 3u;
    const TextureHandle shadowVisibility = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(shadowLayerCount)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(constants.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    Texture* const initialTextures[] = { shadowVisibility.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId constantsResource = graph.importBuffer(
        constants,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/monolithic_shadow_visibility_constants"))
            .setMarkerLabel("Monolithic Shadow Visibility Constants")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId shadowVisibilityResource = graph.importTexture(
        shadowVisibility,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/monolithic_shadow_visibility"))
            .setMarkerLabel("Monolithic Shadow Visibility")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(constantsResource.valid());
    ASSERT_TRUE(shadowVisibilityResource.valid());

    const GpuQueueRequest computeTransferQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        GpuQueuePreference::Compute,
        true,
        false,
    };
    const TextureSubresourceSet shadowSubresources(0u, 1u, 0u, shadowLayerCount);
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/monolithic_shadow_visibility_all_lit_clear"))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(clearScheduling)
    ;
    const GpuTaskId allLitClear = graph.addClearTextureTask(
        clearDesc,
        GpuClearTextureTaskDesc{
            .destination = shadowVisibilityResource,
            .subresources = shadowSubresources,
            .valueType = GpuClearTextureTaskValueType::Float,
            .floatValue = Color(1.f, 1.f, 1.f, 1.f),
        }
    );
    ASSERT_TRUE(allLitClear.valid());

    const GpuTaskResourceUse shadowUses[] = {
        {
            .resource = constantsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = GpuTaskCostHint::Large;
    shadowScheduling.allowPacketMerge = true;
    shadowScheduling.mergeWithPrevious = true;
    GpuTaskDesc shadowDesc;
    shadowDesc
        .setIdentity(Name("tests/descriptor_buffer/monolithic_shadow_visibility"))
        .setMarkerLabel("Monolithic Shadow Visibility")
        .setQueue(computeTransferQueue)
        .setScheduling(shadowScheduling)
        .setDependencies(&allLitClear, 1u)
        .setResourceUses(shadowUses, LengthOf(shadowUses))
    ;
    bool shadowRecorded = false;
    const GpuTaskId shadowTask = graph.addTask<NativePacketPrefixTask>(
        shadowDesc,
        NativePacketPrefixTask::Payload{
            .buffer = constants.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = shadowVisibility.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &shadowRecorded,
        }
    );
    ASSERT_TRUE(shadowTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        {
            .resource = constantsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = GpuTaskCostHint::Large;
    lightingScheduling.forceSubmissionBoundary = true;
    lightingScheduling.allowPacketMerge = false;
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/monolithic_shadow_visibility_lighting"))
        .setMarkerLabel("Monolithic Shadow Lighting")
        .setQueue(computeTransferQueue)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadowTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketPrefixTask>(
        lightingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = constants.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = shadowVisibility.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/monolithic_shadow_all_lit_clear_scratch"));
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId clearPacket = views.compiled.packetForTask(allLitClear);
    const GpuSubmissionPacketId shadowPacket = views.compiled.packetForTask(shadowTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(clearPacket, shadowPacket);
    EXPECT_NE(lightingPacket, shadowPacket);
    ASSERT_EQ(views.compiled.packetCount(), 2u);
    ASSERT_EQ(views.compiled.packet(shadowPacket).plan->taskCount, 2u);
    const GpuTaskId* const shadowPacketTasks = views.compiled.packet(shadowPacket).tasks;
    ASSERT_NE(shadowPacketTasks, nullptr);
    EXPECT_EQ(shadowPacketTasks[0u], allLitClear);
    EXPECT_EQ(shadowPacketTasks[1u], shadowTask);

    const auto hasTextureTransition = [&](
        const GpuTaskId task,
        const ResourceStates::Mask before,
        const ResourceStates::Mask after
    ){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        if(!compiledTask.valid())
            return false;
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(!barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == shadowVisibilityResource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureTransition(allLitClear, ResourceStates::Common, ResourceStates::CopyDest));
    EXPECT_TRUE(hasTextureTransition(shadowTask, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(lightingTask, ResourceStates::UnorderedAccess, ResourceStates::ShaderResource));

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
    EXPECT_TRUE(shadowRecorded);
    EXPECT_TRUE(lightingRecorded);
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const clearCapture = commandIrCapture.recordAt(0u);
    ASSERT_NE(clearCapture, nullptr);
    EXPECT_EQ(clearCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(clearCapture->task, allLitClear);
    EXPECT_EQ(clearCapture->packet, shadowPacket);
    EXPECT_EQ(clearCapture->destination, shadowVisibilityResource);
    EXPECT_EQ(clearCapture->destinationSubresources, shadowSubresources);
    EXPECT_EQ(clearCapture->clearTextureValueType, GpuClearTextureTaskValueType::Float);
    EXPECT_EQ(clearCapture->floatClearValue, Color(1.f, 1.f, 1.f, 1.f));

    CommandListResourceStateHandoff shadowFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        shadowTask,
        shadowFinalStateStorage
    ));
    CommandListResourceStateHandoff lightingFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        lightingTask,
        lightingFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const shadowFinalState = &shadowFinalStateStorage;
    const CommandListResourceStateHandoff* const lightingFinalState = &lightingFinalStateStorage;
    const CommandListHandle stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(shadowFinalState);
    EXPECT_EQ(
        stateProbe->getTextureSubresourceState(shadowVisibility.get(), shadowLayerCount - 1u, 0u),
        ResourceStates::UnorderedAccess
    );
    stateProbe->close();
    stateProbe->open(lightingFinalState);
    EXPECT_EQ(
        stateProbe->getTextureSubresourceState(shadowVisibility.get(), shadowLayerCount - 1u, 0u),
        ResourceStates::ShaderResource
    );
    stateProbe->close();

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
    const QueueSubmissionToken shadowToken = transaction.taskToken(views.compiled, shadowTask);
    const QueueSubmissionToken lightingToken = transaction.taskToken(views.compiled, lightingTask);
    ASSERT_TRUE(shadowToken.valid());
    ASSERT_TRUE(lightingToken.valid());
    EXPECT_NE(shadowToken.value, lightingToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

