// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_probes_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The normal AVBOIT route keeps Pre, the final immutable Occupancy stream, the serial Coverage clear, and the
// alias-free regular compute-emulation producer/raster pair in one accepting Graphics packet.  The one AVBOIT Pre
// timing ticket is intentionally bound at Pre only; the producer/raster timing span and every callback must still
// receive that same packet token while the graph owns CopyDest-to-UAV and UAV-to-VertexBuffer transitions.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitOccupancyAliasFreeComputeEmulationStaysInPrePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitOccupancyLifecycleScope.identity, device, 1u));
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

    const auto createWorkBuffer = [&device](
        const Name& debugName,
        const bool isVertexBuffer = false,
        const bool isConstantBuffer = false
    ){
        BufferDesc description;
        description
            .setDebugName(debugName)
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
        ;
        if(isVertexBuffer)
            description.setIsVertexBuffer(true);
        if(isConstantBuffer)
            description.setIsConstantBuffer(true);
        return device.createBuffer(description);
    };
    auto stateProbe = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_state_probe"),
        false,
        true
    );
    auto materialStream = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_material_stream"));
    auto coverage = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_coverage"));
    auto generatedVertexA = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_generated_vertex_a"),
        true
    );
    auto generatedVertexB = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_generated_vertex_b"),
        true
    );
    ASSERT_TRUE(stateProbe && materialStream && coverage && generatedVertexA && generatedVertexB);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const AStringView markerLabel){
        const BufferDesc& description = buffer->getDescription();
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(description.debugName)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId stateProbeResource = importBuffer(stateProbe, "AVBOIT Occupancy State Probe");
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Occupancy Material Stream"
    );
    const GpuGraphResourceId coverageResource = importBuffer(coverage, "AVBOIT Coverage");
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "AVBOIT Occupancy Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "AVBOIT Occupancy Generated Vertex B"
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(generatedVertexAResource.valid());
    ASSERT_TRUE(generatedVertexBResource.valid());
    EXPECT_NE(generatedVertexAResource, generatedVertexBResource);

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

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
    GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    const GpuTaskResourceUse preUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse streamUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialStreamResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialStreamResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse occupancyUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialStreamResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceSetUse producerGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceSetUse occupancyGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> occupancyTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool streamRecorded = false;
    bool clearRecorded = false;
    bool producerRecorded = false;
    bool occupancyRecorded = false;
    bool occupancyTimingStarted = false;
    bool occupancyTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken clearAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken occupancyAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload prePayload;
    prePayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    prePayload.expectationCount = 1u;
    prePayload.recordOrdinal = &recordOrdinal;
    prePayload.expectedOrdinal = 0u;
    prePayload.timingTicket = &preTimingTicket;
    prePayload.recorded = &preRecorded;
    prePayload.acceptedToken = &preAcceptedToken;
    const GpuTaskId preTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_pre_task"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses)),
        Move(prePayload)
    );
    ASSERT_TRUE(preTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload streamPayload;
    streamPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    streamPayload.expectations[1u] = { materialStream.get(), ResourceStates::CopyDest };
    streamPayload.expectationCount = 2u;
    streamPayload.recordOrdinal = &recordOrdinal;
    streamPayload.expectedOrdinal = 1u;
    streamPayload.timingTicket = &preTimingTicket;
    streamPayload.recorded = &streamRecorded;
    streamPayload.acceptedToken = &streamAcceptedToken;
    const GpuTaskId streamTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_material_upload_task"))
            .setMarkerLabel("AVBOIT Occupancy Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses)),
        Move(streamPayload)
    );
    ASSERT_TRUE(streamTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload clearPayload;
    clearPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    clearPayload.expectations[1u] = { coverage.get(), ResourceStates::CopyDest };
    clearPayload.expectationCount = 2u;
    clearPayload.recordOrdinal = &recordOrdinal;
    clearPayload.expectedOrdinal = 2u;
    clearPayload.timingTicket = &preTimingTicket;
    clearPayload.recorded = &clearRecorded;
    clearPayload.acceptedToken = &clearAcceptedToken;
    const GpuTaskId clearTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_clear_task"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamTask, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses)),
        Move(clearPayload)
    );
    ASSERT_TRUE(clearTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    producerPayload.expectations[1u] = { materialStream.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[2u] = { generatedVertexA.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectations[3u] = { generatedVertexB.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 4u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 3u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &preTimingTicket;
    producerPayload.sharedTiming = &occupancyTiming;
    producerPayload.timingScope = &s_UnsplitAvboitOccupancyLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &occupancyTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Occupancy Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clearTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload occupancyPayload;
    occupancyPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    occupancyPayload.expectations[1u] = { materialStream.get(), ResourceStates::ShaderResource };
    occupancyPayload.expectations[2u] = { generatedVertexA.get(), ResourceStates::VertexBuffer };
    occupancyPayload.expectations[3u] = { generatedVertexB.get(), ResourceStates::VertexBuffer };
    occupancyPayload.expectations[4u] = { coverage.get(), ResourceStates::UnorderedAccess };
    occupancyPayload.expectationCount = 5u;
    occupancyPayload.recordOrdinal = &recordOrdinal;
    occupancyPayload.expectedOrdinal = 4u;
    occupancyPayload.device = &device;
    occupancyPayload.timing = &timing;
    occupancyPayload.timingTicket = &preTimingTicket;
    occupancyPayload.sharedTiming = &occupancyTiming;
    occupancyPayload.timingScope = &s_UnsplitAvboitOccupancyLifecycleScope;
    occupancyPayload.finishTiming = true;
    occupancyPayload.timingFinished = &occupancyTimingFinished;
    occupancyPayload.recorded = &occupancyRecorded;
    occupancyPayload.acceptedToken = &occupancyAcceptedToken;
    const GpuTaskId occupancyTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_raster_task"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses))
            .setResourceSetUses(&occupancyGeneratedVertexUse, 1u),
        Move(occupancyPayload)
    );
    ASSERT_TRUE(occupancyTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, clearTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(clearTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, occupancyTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(clearTask, occupancyTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, occupancyTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 5u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], preTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clearTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[4u], occupancyTask);

    const auto expectAssignment = [&](const GpuTaskId task){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    };
    expectAssignment(preTask);
    expectAssignment(streamTask);
    expectAssignment(clearTask);
    expectAssignment(producerTask);
    expectAssignment(occupancyTask);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(packet, views.compiled.packetForTask(streamTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(clearTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(producerTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(occupancyTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, occupancyTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(preTask, streamTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(streamTask, clearTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(clearTask, producerTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(producerTask, occupancyTask));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, 5u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], preTask);
    EXPECT_EQ(packetTasks[1u], streamTask);
    EXPECT_EQ(packetTasks[2u], clearTask);
    EXPECT_EQ(packetTasks[3u], producerTask);
    EXPECT_EQ(packetTasks[4u], occupancyTask);

    const auto hasBufferTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == primaryGraphicsQueue
                && barrier.destinationQueue == primaryGraphicsQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        streamTask,
        materialStreamResource,
        ResourceStates::Common,
        ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clearTask,
        coverageResource,
        ResourceStates::Common,
        ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        materialStreamResource,
        ResourceStates::CopyDest,
        ResourceStates::ShaderResource
    ));
    for(const GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producerTask,
            output,
            ResourceStates::Common,
            ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            occupancyTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        occupancyTask,
        coverageResource,
        ResourceStates::CopyDest,
        ResourceStates::UnorderedAccess
    ));

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
    EXPECT_EQ(recordOrdinal, 5u);
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(streamRecorded);
    EXPECT_TRUE(clearRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(occupancyRecorded);
    EXPECT_TRUE(occupancyTimingStarted);
    EXPECT_TRUE(occupancyTimingFinished);
    EXPECT_FALSE(occupancyTiming.has_value());
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(materialStream.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(coverage.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    finalStateProbe->close();

    // The renderer submits this semantic packet through AVBOIT Pre. Resolving only that binding must accept the
    // later producer/raster timing span and every callback with the packet's one Graphics submission token.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        occupancyTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_EQ(packetToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(packetToken.physicalQueueIndex, primaryGraphicsQueue.index);
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    expectPacketToken(transaction.taskToken(views.compiled, preTask));
    expectPacketToken(transaction.taskToken(views.compiled, streamTask));
    expectPacketToken(transaction.taskToken(views.compiled, clearTask));
    expectPacketToken(transaction.taskToken(views.compiled, producerTask));
    expectPacketToken(transaction.taskToken(views.compiled, occupancyTask));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(streamAcceptedToken);
    expectPacketToken(clearAcceptedToken);
    expectPacketToken(producerAcceptedToken);
    expectPacketToken(occupancyAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_UnsplitAvboitOccupancyLifecycleScope.identity);
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

