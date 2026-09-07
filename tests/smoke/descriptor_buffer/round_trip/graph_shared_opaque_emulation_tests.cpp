// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_SharedOpaqueComputeEmulationScope(
    "tests/timing_shared_opaque_compute_emulation"
);


// A getter-only shared-output probe.  Its ordinal makes native packet recording prove the dispatch/raster
// interleaving rather than merely observing the right final state, while the optional scope lets one semantic
// timing ticket be bound to every merged callback without any callback-local state bridge.
struct NativePacketSharedOutputHandoffTask{
    struct Payload{
        Buffer* buffer = nullptr;
        ResourceStates::Mask expectedState = ResourceStates::Unknown;
        u32* recordOrdinal = nullptr;
        u32 expectedOrdinal = 0u;
        Device* device = nullptr;
        GpuTimingRecorder* timing = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool recordTiming = false;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.buffer || !payload.recordOrdinal || !payload.timingTicket)
            return false;

        GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        bool ready = commandList.getBufferState(payload.buffer) == payload.expectedState
            && *payload.recordOrdinal == payload.expectedOrdinal
        ;
        if(ready && payload.recordTiming){
            if(!payload.device || !payload.timing)
                ready = false;
            else{
                GpuTimingMeasure timingMeasure(
                    *payload.timing,
                    s_SharedOpaqueComputeEmulationScope,
                    *payload.device,
                    commandList
                );
            }
        }
        if(ready)
            ++*payload.recordOrdinal;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


// Compute-emulated material work writes its generated vertex stream before the ordinary raster callback consumes
// that exact stream. Both command capabilities must remain on primary Graphics, so the graph owns the direct
// UAV -> VertexBuffer handoff inside one accepting packet rather than relying on a callback-local bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedComputeEmulationGeneratedVertexHandoffMergesWithRaster){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto generatedVertex = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/compute_emulation_generated_vertex"))
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(generatedVertex.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& generatedVertexDesc = generatedVertex->getDescription();
    const GpuGraphResourceId generatedVertexResource = graph.importBuffer(
        generatedVertex,
        GpuGraphResourceDesc{}
            .setIdentity(generatedVertexDesc.debugName)
            .setMarkerLabel("Compute-Emulated Generated Vertex")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(generatedVertexDesc.initialState)
            .setQueueSharing(generatedVertexDesc.queueSharing)
    );
    ASSERT_TRUE(generatedVertexResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = GpuTaskCostHint::Small;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool producerObservedUnorderedAccess = false;
    QueueSubmissionToken producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/compute_emulation_generated_vertex_producer"))
            .setMarkerLabel("Compute-Emulated Generated Vertex Producer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        NativePacketPrefixTask::Payload{
            // This callback only observes the graph-owned UAV entry state.
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &producerObservedUnorderedAccess,
            .acceptedToken = &producerAcceptedToken,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceUse rasterUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool rasterObservedVertexBuffer = false;
    QueueSubmissionToken rasterAcceptedToken;
    const GpuTaskId rasterTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/compute_emulation_generated_vertex_raster"))
            .setMarkerLabel("Compute-Emulated Generated Vertex Raster Consume")
            .setQueue(graphicsQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(rasterUses, LengthOf(rasterUses)),
        NativePacketPrefixTask::Payload{
            // This callback likewise performs no native transition; packet lowering owns UAV -> VertexBuffer.
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .recorded = &rasterObservedVertexBuffer,
            .acceptedToken = &rasterAcceptedToken,
        }
    );
    ASSERT_TRUE(rasterTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/compute_emulation_generated_vertex_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, rasterTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], rasterTask);

    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    const GpuTaskQueueAssignment* const rasterAssignment = assignments.find(rasterTask);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(rasterAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(rasterAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(rasterAssignment->queueClass, CommandQueue::Graphics);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId rasterPacket = views.compiled.packetForTask(rasterTask);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(rasterPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(rasterPacket, producerPacket);
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, rasterTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(producerTask, rasterTask));
    EXPECT_EQ(views.compiled.packet(producerPacket).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(producerPacket).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(producerPacket).plan->taskCount, 2u);
    const GpuTaskId* const packetTasks = views.compiled.packet(producerPacket).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], producerTask);
    EXPECT_EQ(packetTasks[1u], rasterTask);

    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledRaster = views.compiled.findTask(rasterTask);
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledRaster.valid());
    ASSERT_EQ(compiledProducer.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledRaster.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const producerBarrier = views.compiled.findTask(producerTask).prologueBarriers;
    const GpuCompiledBarrier* const rasterBarrier = views.compiled.findTask(rasterTask).prologueBarriers;
    ASSERT_NE(producerBarrier, nullptr);
    ASSERT_NE(rasterBarrier, nullptr);
    EXPECT_EQ(producerBarrier[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(producerBarrier[0u].resource, generatedVertexResource);
    EXPECT_EQ(producerBarrier[0u].before, ResourceStates::Common);
    EXPECT_EQ(producerBarrier[0u].after, ResourceStates::UnorderedAccess);
    EXPECT_EQ(rasterBarrier[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(rasterBarrier[0u].resource, generatedVertexResource);
    EXPECT_EQ(rasterBarrier[0u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(rasterBarrier[0u].after, ResourceStates::VertexBuffer);
    EXPECT_EQ(rasterBarrier[0u].sourceQueue, primaryGraphicsQueue);
    EXPECT_EQ(rasterBarrier[0u].destinationQueue, primaryGraphicsQueue);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_TRUE(producerObservedUnorderedAccess);
    EXPECT_TRUE(rasterObservedVertexBuffer);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, producerTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
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
    const QueueSubmissionToken packetToken = transaction.packetToken(producerPacket);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(producerAcceptedToken.valid());
    EXPECT_EQ(producerAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(producerAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(rasterAcceptedToken.valid());
    EXPECT_EQ(rasterAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(rasterAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());
}


// A shared persistent generated-vertex buffer cannot batch both compute draws before raster: the second dispatch
// would overwrite A.  The graph must therefore lower Dispatch(A) -> Raster(A) -> Dispatch(B) -> Raster(B) in the
// existing primary-Graphics packet, with no callback-local state transition or extra timing/acceptance endpoint.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSharedOpaqueComputeEmulationPairsStayInOnePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SharedOpaqueComputeEmulationScope.identity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    auto generatedVertex = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/shared_opaque_compute_generated_vertex"))
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(generatedVertex.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& generatedVertexDesc = generatedVertex->getDescription();
    const GpuGraphResourceId generatedVertexResource = graph.importBuffer(
        generatedVertex,
        GpuGraphResourceDesc{}
            .setIdentity(generatedVertexDesc.debugName)
            .setMarkerLabel("Shared Opaque Compute Generated Vertex")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(generatedVertexDesc.initialState)
            .setQueueSharing(generatedVertexDesc.queueSharing)
    );
    ASSERT_TRUE(generatedVertexResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsRasterQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    const GpuTaskResourceUse dispatchUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse rasterUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTimingSubmissionTicket timingTicket(timing);
    u32 recordOrdinal = 0u;
    bool dispatchAObservedUnorderedAccess = false;
    bool rasterAObservedVertexBuffer = false;
    bool dispatchBObservedUnorderedAccess = false;
    bool rasterBObservedVertexBuffer = false;
    QueueSubmissionToken dispatchAAcceptedToken;
    QueueSubmissionToken rasterAAcceptedToken;
    QueueSubmissionToken dispatchBAcceptedToken;
    QueueSubmissionToken rasterBAcceptedToken;
    const GpuTaskId dispatchA = graph.addTask<NativePacketSharedOutputHandoffTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_a"))
            .setMarkerLabel("Shared Opaque Compute Dispatch A")
            .setQueue(graphicsComputeQueue)
            .setScheduling(dispatchScheduling)
            .setResourceUses(dispatchUses, LengthOf(dispatchUses)),
        NativePacketSharedOutputHandoffTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recordOrdinal = &recordOrdinal,
            .expectedOrdinal = 0u,
            .device = &device,
            .timing = &timing,
            .timingTicket = &timingTicket,
            .recorded = &dispatchAObservedUnorderedAccess,
            .acceptedToken = &dispatchAAcceptedToken,
        }
    );
    ASSERT_TRUE(dispatchA.valid());

    GpuTaskSchedulingHint pairTailScheduling = dispatchScheduling;
    pairTailScheduling.mergeWithPrevious = true;
    pairTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskId rasterADependencies[] = { dispatchA };
    const GpuTaskId rasterA = graph.addTask<NativePacketSharedOutputHandoffTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shared_opaque_compute_raster_a"))
            .setMarkerLabel("Shared Opaque Compute Raster A")
            .setQueue(graphicsRasterQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(rasterADependencies, LengthOf(rasterADependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses)),
        NativePacketSharedOutputHandoffTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .recordOrdinal = &recordOrdinal,
            .expectedOrdinal = 1u,
            .device = &device,
            .timing = &timing,
            .timingTicket = &timingTicket,
            .recorded = &rasterAObservedVertexBuffer,
            .acceptedToken = &rasterAAcceptedToken,
        }
    );
    ASSERT_TRUE(rasterA.valid());

    const GpuTaskId dispatchBDependencies[] = { rasterA };
    const GpuTaskId dispatchB = graph.addTask<NativePacketSharedOutputHandoffTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_b"))
            .setMarkerLabel("Shared Opaque Compute Dispatch B")
            .setQueue(graphicsComputeQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(dispatchBDependencies, LengthOf(dispatchBDependencies))
            .setResourceUses(dispatchUses, LengthOf(dispatchUses)),
        NativePacketSharedOutputHandoffTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recordOrdinal = &recordOrdinal,
            .expectedOrdinal = 2u,
            .device = &device,
            .timing = &timing,
            .timingTicket = &timingTicket,
            .recorded = &dispatchBObservedUnorderedAccess,
            .acceptedToken = &dispatchBAcceptedToken,
        }
    );
    ASSERT_TRUE(dispatchB.valid());

    const GpuTaskId rasterBDependencies[] = { dispatchB };
    const GpuTaskId rasterB = graph.addTask<NativePacketSharedOutputHandoffTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shared_opaque_compute_raster_b"))
            .setMarkerLabel("Shared Opaque Compute Raster B")
            .setQueue(graphicsRasterQueue)
            .setScheduling(pairTailScheduling)
            .setDependencies(rasterBDependencies, LengthOf(rasterBDependencies))
            .setResourceUses(rasterUses, LengthOf(rasterUses)),
        NativePacketSharedOutputHandoffTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .recordOrdinal = &recordOrdinal,
            .expectedOrdinal = 3u,
            .device = &device,
            .timing = &timing,
            .timingTicket = &timingTicket,
            .recordTiming = true,
            .recorded = &rasterBObservedVertexBuffer,
            .acceptedToken = &rasterBAcceptedToken,
        }
    );
    ASSERT_TRUE(rasterB.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shared_opaque_compute_emulation_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchA, rasterA));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterA, dispatchB));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchB, rasterB));

    const GpuTaskQueueAssignment* const dispatchAAssignment = assignments.find(dispatchA);
    const GpuTaskQueueAssignment* const rasterAAssignment = assignments.find(rasterA);
    const GpuTaskQueueAssignment* const dispatchBAssignment = assignments.find(dispatchB);
    const GpuTaskQueueAssignment* const rasterBAssignment = assignments.find(rasterB);
    ASSERT_NE(dispatchAAssignment, nullptr);
    ASSERT_NE(rasterAAssignment, nullptr);
    ASSERT_NE(dispatchBAssignment, nullptr);
    ASSERT_NE(rasterBAssignment, nullptr);
    EXPECT_EQ(dispatchAAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(rasterAAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(dispatchBAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(rasterBAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(dispatchAAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(rasterAAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(dispatchBAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(rasterBAssignment->queueClass, CommandQueue::Graphics);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(dispatchA);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(packet, views.compiled.packetForTask(rasterA));
    EXPECT_EQ(packet, views.compiled.packetForTask(dispatchB));
    EXPECT_EQ(packet, views.compiled.packetForTask(rasterB));
    EXPECT_TRUE(views.compiled.tasksSharePacket(dispatchA, rasterB));
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(packet).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 4u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], dispatchA);
    EXPECT_EQ(packetTasks[1u], rasterA);
    EXPECT_EQ(packetTasks[2u], dispatchB);
    EXPECT_EQ(packetTasks[3u], rasterB);

    const auto expectTransition = [&](const GpuTaskId task, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        ASSERT_TRUE(compiledTask.valid());
        ASSERT_EQ(compiledTask.plan->prologueBarrierCount, 1u);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexResource);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, primaryGraphicsQueue);
        EXPECT_EQ(barrier.destinationQueue, primaryGraphicsQueue);
    };
    expectTransition(dispatchA, ResourceStates::Common, ResourceStates::UnorderedAccess);
    expectTransition(rasterA, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(dispatchB, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(rasterB, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, 4u);
    EXPECT_TRUE(dispatchAObservedUnorderedAccess);
    EXPECT_TRUE(rasterAObservedVertexBuffer);
    EXPECT_TRUE(dispatchBObservedUnorderedAccess);
    EXPECT_TRUE(rasterBObservedVertexBuffer);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, dispatchA, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    stateProbe->close();

    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = dispatchA, .timingTicket = &timingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = rasterA, .timingTicket = &timingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = dispatchB, .timingTicket = &timingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = rasterB, .timingTicket = &timingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        dispatchA,
        rasterB,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    expectPacketToken(transaction.taskToken(views.compiled, dispatchA));
    expectPacketToken(transaction.taskToken(views.compiled, rasterA));
    expectPacketToken(transaction.taskToken(views.compiled, dispatchB));
    expectPacketToken(transaction.taskToken(views.compiled, rasterB));
    expectPacketToken(dispatchAAcceptedToken);
    expectPacketToken(rasterAAcceptedToken);
    expectPacketToken(dispatchBAcceptedToken);
    expectPacketToken(rasterBAcceptedToken);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_SharedOpaqueComputeEmulationScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// The next bounded shared-output class extends the same graph-owned interleave to three draws. All six callbacks
// observe compiler-lowered states only: D(A) -> R(A) -> D(B) -> R(B) -> D(C) -> R(C).
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSharedOpaqueComputeEmulationTriplesStayInOnePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    const Name packetTimingTaskIdentity("tests/descriptor_buffer/shared_opaque_compute_dispatch_a_triple");
    const Name packetTimingScopeIdentity = GpuTaskPacketTimingScopeName(packetTimingTaskIdentity);

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SharedOpaqueComputeEmulationScope.identity, device, 3u));
    ASSERT_TRUE(timing.prepareScopeQueries(packetTimingScopeIdentity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    auto generatedVertex = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/shared_opaque_compute_generated_vertex_triple"))
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(generatedVertex.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& generatedVertexDesc = generatedVertex->getDescription();
    const GpuGraphResourceId generatedVertexResource = graph.importBuffer(
        generatedVertex,
        GpuGraphResourceDesc{}
            .setIdentity(generatedVertexDesc.debugName)
            .setMarkerLabel("Shared Opaque Compute Generated Vertex Triple")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(generatedVertexDesc.initialState)
            .setQueueSharing(generatedVertexDesc.queueSharing)
    );
    ASSERT_TRUE(generatedVertexResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsRasterQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint sequenceTailScheduling = dispatchScheduling;
    sequenceTailScheduling.mergeWithPrevious = true;
    sequenceTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceUse dispatchUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse rasterUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const Name identities[] = {
        packetTimingTaskIdentity,
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_a_triple"),
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_b_triple"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_b_triple"),
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_c_triple"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_c_triple"),
    };
    const AStringView markers[] = {
        "Shared Opaque Compute Dispatch A Triple",
        "Shared Opaque Compute Raster A Triple",
        "Shared Opaque Compute Dispatch B Triple",
        "Shared Opaque Compute Raster B Triple",
        "Shared Opaque Compute Dispatch C Triple",
        "Shared Opaque Compute Raster C Triple",
    };
    GpuTimingSubmissionTicket firstPairTimingTicket(timing);
    GpuTimingSubmissionTicket secondPairTimingTicket(timing);
    GpuTimingSubmissionTicket thirdPairTimingTicket(timing);
    GpuTimingSubmissionTicket* const pairTimingTickets[] = {
        &firstPairTimingTicket,
        &secondPairTimingTicket,
        &thirdPairTimingTicket,
    };
    u32 recordOrdinal = 0u;
    bool observedStates[LengthOf(identities)] = {};
    QueueSubmissionToken acceptedTokens[LengthOf(identities)] = {};
    GpuTaskId tasks[LengthOf(identities)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const bool isRaster = taskIndex % 2u != 0u;
        GpuTaskDesc desc;
        desc
            .setIdentity(identities[taskIndex])
            .setMarkerLabel(markers[taskIndex])
            .setQueue(isRaster ? graphicsRasterQueue : graphicsComputeQueue)
            .setScheduling(taskIndex == 0u ? dispatchScheduling : sequenceTailScheduling)
            .setResourceUses(
                isRaster ? rasterUses : dispatchUses,
                isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
            )
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        if(taskIndex == 0u)
            desc.setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly });
        tasks[taskIndex] = graph.addTask<NativePacketSharedOutputHandoffTask>(
            desc,
            NativePacketSharedOutputHandoffTask::Payload{
                .buffer = generatedVertex.get(),
                .expectedState = isRaster ? ResourceStates::VertexBuffer : ResourceStates::UnorderedAccess,
                .recordOrdinal = &recordOrdinal,
                .expectedOrdinal = static_cast<u32>(taskIndex),
                .device = &device,
                .timing = &timing,
                .timingTicket = pairTimingTickets[taskIndex / 2u],
                .recordTiming = isRaster,
                .recorded = &observedStates[taskIndex],
                .acceptedToken = &acceptedTokens[taskIndex],
            }
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shared_opaque_compute_emulation_triple_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    for(usize taskIndex = 1u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(analysis.hasExplicitEdge(tasks[taskIndex - 1u], tasks[taskIndex]));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    for(const GpuTaskId task : tasks){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
        EXPECT_EQ(views.compiled.packetForTask(task), packet);
    }
    EXPECT_TRUE(views.compiled.tasksSharePacket(tasks[0u], tasks[5u]));
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(packet).plan->dependencyCount, 0u);
    EXPECT_TRUE(views.compiled.packet(packet).plan->recordsTiming);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, LengthOf(tasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

    const auto expectTransition = [&](const usize taskIndex, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(tasks[taskIndex]);
        ASSERT_TRUE(compiledTask.valid());
        ASSERT_EQ(compiledTask.plan->prologueBarrierCount, 1u);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(tasks[taskIndex]).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexResource);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, primaryGraphicsQueue);
        EXPECT_EQ(barrier.destinationQueue, primaryGraphicsQueue);
    };
    expectTransition(0u, ResourceStates::Common, ResourceStates::UnorderedAccess);
    expectTransition(1u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(2u, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(3u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(4u, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(5u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device, timing);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    for(const bool observed : observedStates)
        EXPECT_TRUE(observed);
    ASSERT_EQ(views.compiled.packetForTask(tasks[0u]), views.compiled.packetForTask(tasks[5u]));
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff mergedFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, tasks[0u], finalStateStorage));
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        tasks[5u],
        mergedFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    stateProbe->close();

    GpuTaskGraphTaskTimingTicket timingTickets[LengthOf(tasks)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        timingTickets[taskIndex] = GpuTaskGraphTaskTimingTicket{
            .task = tasks[taskIndex],
            .timingTicket = pairTimingTickets[taskIndex / 2u],
        };
    }
    const GpuTaskGraphSubmitter submitter(device);
    GpuTimingSubmissionTicket resolvedTimingTicket(timing);
    resolvedTimingTicket.discard();
    const GpuTaskGraphTaskTimingTicket retryableTimingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = tasks[0u], .timingTicket = pairTimingTickets[0u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[2u], .timingTicket = pairTimingTickets[1u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[4u], .timingTicket = pairTimingTickets[2u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[5u], .timingTicket = &resolvedTimingTicket },
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        tasks[0u],
        tasks[5u],
        nullptr,
        0u,
        retryableTimingTickets,
        LengthOf(retryableTimingTickets),
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    const GpuTaskGraphSubmissionStatistics retryableStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(retryableStatistics.valid());
    EXPECT_EQ(retryableStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(retryableStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(retryableStatistics.rejectedSubmissionCount, 0u);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        tasks[0u],
        tasks[5u],
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const GpuTaskGraphSubmissionStatistics acceptedStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(acceptedStatistics.valid());
    EXPECT_EQ(acceptedStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(acceptedStatistics.acceptedTaskCount, LengthOf(tasks));
    EXPECT_EQ(acceptedStatistics.nativeSubmissionCount, 1u);
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    for(const GpuTaskId task : tasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    for(const QueueSubmissionToken& token : acceptedTokens)
        expectPacketToken(token);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_SharedOpaqueComputeEmulationScope.identity);
    const auto packetTimingStats = timingSink.stats(packetTimingScopeIdentity);
    ASSERT_TRUE(timingStats.valid());
    ASSERT_TRUE(packetTimingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 3u);
    EXPECT_EQ(packetTimingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSharedOpaqueComputeEmulationQuadruplesStayInOnePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SharedOpaqueComputeEmulationScope.identity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    auto generatedVertex = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/shared_opaque_compute_generated_vertex_quad"))
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(generatedVertex.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& generatedVertexDesc = generatedVertex->getDescription();
    const GpuGraphResourceId generatedVertexResource = graph.importBuffer(
        generatedVertex,
        GpuGraphResourceDesc{}
            .setIdentity(generatedVertexDesc.debugName)
            .setMarkerLabel("Shared Opaque Compute Generated Vertex Quadruple")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(generatedVertexDesc.initialState)
            .setQueueSharing(generatedVertexDesc.queueSharing)
    );
    ASSERT_TRUE(generatedVertexResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsRasterQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint dispatchScheduling;
    dispatchScheduling.cost = GpuTaskCostHint::Medium;
    dispatchScheduling.overlapPreferred = false;
    dispatchScheduling.avoidQueueCrossing = true;
    dispatchScheduling.forceSubmissionBoundary = false;
    dispatchScheduling.allowPacketMerge = true;
    dispatchScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint sequenceTailScheduling = dispatchScheduling;
    sequenceTailScheduling.mergeWithPrevious = true;
    sequenceTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceUse dispatchUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse rasterUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const Name identities[] = {
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_a_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_a_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_b_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_b_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_c_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_c_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_dispatch_d_quad"),
        Name("tests/descriptor_buffer/shared_opaque_compute_raster_d_quad"),
    };
    const AStringView markers[] = {
        "Shared Opaque Compute Dispatch A Quadruple",
        "Shared Opaque Compute Raster A Quadruple",
        "Shared Opaque Compute Dispatch B Quadruple",
        "Shared Opaque Compute Raster B Quadruple",
        "Shared Opaque Compute Dispatch C Quadruple",
        "Shared Opaque Compute Raster C Quadruple",
        "Shared Opaque Compute Dispatch D Quadruple",
        "Shared Opaque Compute Raster D Quadruple",
    };
    GpuTimingSubmissionTicket timingTicket(timing);
    u32 recordOrdinal = 0u;
    bool observedStates[LengthOf(identities)] = {};
    QueueSubmissionToken acceptedTokens[LengthOf(identities)] = {};
    GpuTaskId tasks[LengthOf(identities)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const bool isRaster = taskIndex % 2u != 0u;
        GpuTaskDesc desc;
        desc
            .setIdentity(identities[taskIndex])
            .setMarkerLabel(markers[taskIndex])
            .setQueue(isRaster ? graphicsRasterQueue : graphicsComputeQueue)
            .setScheduling(taskIndex == 0u ? dispatchScheduling : sequenceTailScheduling)
            .setResourceUses(
                isRaster ? rasterUses : dispatchUses,
                isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
            )
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        tasks[taskIndex] = graph.addTask<NativePacketSharedOutputHandoffTask>(
            desc,
            NativePacketSharedOutputHandoffTask::Payload{
                .buffer = generatedVertex.get(),
                .expectedState = isRaster ? ResourceStates::VertexBuffer : ResourceStates::UnorderedAccess,
                .recordOrdinal = &recordOrdinal,
                .expectedOrdinal = static_cast<u32>(taskIndex),
                .device = &device,
                .timing = &timing,
                .timingTicket = &timingTicket,
                .recordTiming = taskIndex + 1u == LengthOf(tasks),
                .recorded = &observedStates[taskIndex],
                .acceptedToken = &acceptedTokens[taskIndex],
            }
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shared_opaque_compute_emulation_quad_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    for(usize taskIndex = 1u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(analysis.hasExplicitEdge(tasks[taskIndex - 1u], tasks[taskIndex]));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    for(const GpuTaskId task : tasks){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
        EXPECT_EQ(views.compiled.packetForTask(task), packet);
    }
    EXPECT_TRUE(views.compiled.tasksSharePacket(tasks[0u], tasks[7u]));
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(packet).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, LengthOf(tasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

    const auto expectTransition = [&](const usize taskIndex, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(tasks[taskIndex]);
        ASSERT_TRUE(compiledTask.valid());
        ASSERT_EQ(compiledTask.plan->prologueBarrierCount, 1u);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(tasks[taskIndex]).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        const GpuCompiledBarrier& barrier = barriers[0u];
        EXPECT_EQ(barrier.type, GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(barrier.resource, generatedVertexResource);
        EXPECT_EQ(barrier.before, before);
        EXPECT_EQ(barrier.after, after);
        EXPECT_EQ(barrier.sourceQueue, primaryGraphicsQueue);
        EXPECT_EQ(barrier.destinationQueue, primaryGraphicsQueue);
    };
    expectTransition(0u, ResourceStates::Common, ResourceStates::UnorderedAccess);
    expectTransition(1u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(2u, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(3u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(4u, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(5u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);
    expectTransition(6u, ResourceStates::VertexBuffer, ResourceStates::UnorderedAccess);
    expectTransition(7u, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    for(const bool observed : observedStates)
        EXPECT_TRUE(observed);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, tasks[0u], finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    stateProbe->close();

    GpuTaskGraphTaskTimingTicket timingTickets[LengthOf(tasks)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        timingTickets[taskIndex] = GpuTaskGraphTaskTimingTicket{
            .task = tasks[taskIndex],
            .timingTicket = &timingTicket,
        };
    }
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        tasks[0u],
        tasks[7u],
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    for(const GpuTaskId task : tasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    for(const QueueSubmissionToken& token : acceptedTokens)
        expectPacketToken(token);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_SharedOpaqueComputeEmulationScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

