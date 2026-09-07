// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The split AVBOIT schedule crosses dedicated Compute twice, but Extinction's optional compute-emulation producer
// and raster must stay adjacent in one Graphics submission.  Exercise that exact lifecycle on a real device: packet
// prologues own every Compute/Graphics state handoff, both Extinction callbacks share one timing ticket and token,
// and the one timing scope yields one accepted sample.
TEST_F(DescriptorBufferRoundTripTest, AsyncAvboitExtinctionComputeEmulationSharesGraphicsPacketTicketBetweenDepthWarpAndIntegration){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async AVBOIT Extinction: no usable dedicated-compute headless Vulkan device on this host.";

    auto& graphics = asyncScope.graphics();
    auto& device = graphics.getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Async AVBOIT Extinction: adapter has no dedicated compute-only queue family.";

    auto& timing = graphics.gpuTiming();
    auto& timingSink = asyncScope.gpuTimingSink();
    asyncScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AsyncAvboitExtinctionLifecycleScope.identity, device, 1u));
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

    const auto createWorkBuffer = [&device](const Name& debugName, const bool isVertexBuffer = false){
        BufferDesc description;
        description
            .setDebugName(debugName)
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        if(isVertexBuffer)
            description.setIsVertexBuffer(true);
        return device.createBuffer(description);
    };
    auto depthWarp = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_extinction_depth_warp"));
    auto control = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_extinction_control"));
    auto generatedVertex = createWorkBuffer(
        Name("tests/descriptor_buffer/async_avboit_extinction_generated_vertex"),
        true
    );
    auto extinction = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_extinction_output"));
    auto extinctionOverflow = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_extinction_overflow"));
    ASSERT_TRUE(depthWarp && control && generatedVertex && extinction && extinctionOverflow);

    GpuTaskGraph graph(asyncScope.arena());
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
    const GpuGraphResourceId depthWarpResource = importBuffer(depthWarp, "AVBOIT Depth Warp");
    const GpuGraphResourceId controlResource = importBuffer(control, "AVBOIT Control");
    const GpuGraphResourceId generatedVertexResource = importBuffer(
        generatedVertex,
        "AVBOIT Extinction Generated Vertex"
    );
    const GpuGraphResourceId extinctionResource = importBuffer(extinction, "AVBOIT Extinction");
    const GpuGraphResourceId extinctionOverflowResource = importBuffer(
        extinctionOverflow,
        "AVBOIT Extinction Overflow"
    );
    ASSERT_TRUE(depthWarpResource.valid());
    ASSERT_TRUE(controlResource.valid());
    ASSERT_TRUE(generatedVertexResource.valid());
    ASSERT_TRUE(extinctionResource.valid());
    ASSERT_TRUE(extinctionOverflowResource.valid());

    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        false,
        false,
    };
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
    GpuTaskSchedulingHint computeScheduling;
    computeScheduling.cost = GpuTaskCostHint::Medium;
    computeScheduling.forceSubmissionBoundary = true;
    computeScheduling.allowPacketMerge = false;
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = GpuTaskCostHint::Medium;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = true;
    producerScheduling.allowMergeAcrossConsumerFrontier = true;

    const GpuTaskResourceUse depthWarpUses[] = {
        GpuTaskResourceUse{
            .resource = depthWarpResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse extinctionUses[] = {
        GpuTaskResourceUse{
            .resource = depthWarpResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = extinctionOverflowResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse integrationUses[] = {
        GpuTaskResourceUse{
            .resource = extinctionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionOverflowResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    GpuTimingSubmissionTicket extinctionTimingTicket(timing);
    Optional<GpuTimingMeasure> extinctionTiming;
    u32 recordOrdinal = 0u;
    bool depthWarpRecorded = false;
    bool producerRecorded = false;
    bool extinctionRecorded = false;
    bool integrationRecorded = false;
    bool extinctionTimingStarted = false;
    bool extinctionTimingFinished = false;
    QueueSubmissionToken depthWarpAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken extinctionAcceptedToken;
    QueueSubmissionToken integrationAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload depthWarpPayload;
    depthWarpPayload.expectations[0u] = { depthWarp.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectations[1u] = { control.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectationCount = 2u;
    depthWarpPayload.recordOrdinal = &recordOrdinal;
    depthWarpPayload.expectedOrdinal = 0u;
    depthWarpPayload.recorded = &depthWarpRecorded;
    depthWarpPayload.acceptedToken = &depthWarpAcceptedToken;
    const GpuTaskId depthWarpTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_extinction_depth_warp_task"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses)),
        Move(depthWarpPayload)
    );
    ASSERT_TRUE(depthWarpTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { generatedVertex.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 1u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 1u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &extinctionTimingTicket;
    producerPayload.sharedTiming = &extinctionTiming;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &extinctionTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_extinction_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Extinction Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setDependencies(&depthWarpTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload extinctionPayload;
    extinctionPayload.expectations[0u] = { depthWarp.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[1u] = { control.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[2u] = { generatedVertex.get(), ResourceStates::VertexBuffer };
    extinctionPayload.expectations[3u] = { extinction.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectations[4u] = { extinctionOverflow.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectationCount = 5u;
    extinctionPayload.recordOrdinal = &recordOrdinal;
    extinctionPayload.expectedOrdinal = 2u;
    extinctionPayload.device = &device;
    extinctionPayload.timing = &timing;
    extinctionPayload.timingTicket = &extinctionTimingTicket;
    extinctionPayload.sharedTiming = &extinctionTiming;
    extinctionPayload.finishTiming = true;
    extinctionPayload.timingFinished = &extinctionTimingFinished;
    extinctionPayload.recorded = &extinctionRecorded;
    extinctionPayload.acceptedToken = &extinctionAcceptedToken;
    const GpuTaskId extinctionTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_extinction_raster_task"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsQueue)
            .setScheduling(producerScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses)),
        Move(extinctionPayload)
    );
    ASSERT_TRUE(extinctionTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload integrationPayload;
    integrationPayload.expectations[0u] = { extinction.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[1u] = { control.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[2u] = { extinctionOverflow.get(), ResourceStates::ShaderResource };
    integrationPayload.expectationCount = 3u;
    integrationPayload.recordOrdinal = &recordOrdinal;
    integrationPayload.expectedOrdinal = 3u;
    integrationPayload.recorded = &integrationRecorded;
    integrationPayload.acceptedToken = &integrationAcceptedToken;
    const GpuTaskId integrationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_extinction_integration_task"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setDependencies(&extinctionTask, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses)),
        Move(integrationPayload)
    );
    ASSERT_TRUE(integrationTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId primaryComputeQueue = device.getPrimaryPhysicalQueue(CommandQueue::Compute);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 1u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    ASSERT_TRUE(primaryComputeQueue.valid());
    EXPECT_NE(primaryGraphicsQueue, primaryComputeQueue);
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/async_avboit_extinction_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(depthWarpTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(depthWarpTask, integrationTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 4u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], depthWarpTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], extinctionTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], integrationTask);

    const auto expectAssignment = [&](const GpuTaskId task, const GpuPhysicalQueueId queue, const CommandQueue::Enum queueClass){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(depthWarpTask, primaryComputeQueue, CommandQueue::Compute);
    expectAssignment(producerTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(extinctionTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(integrationTask, primaryComputeQueue, CommandQueue::Compute);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId depthWarpPacket = views.compiled.packetForTask(depthWarpTask);
    const GpuSubmissionPacketId extinctionPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId integrationPacket = views.compiled.packetForTask(integrationTask);
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    EXPECT_EQ(extinctionPacket, views.compiled.packetForTask(extinctionTask));
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_EQ(views.compiled.packetIdAt(0u), depthWarpPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), extinctionPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), integrationPacket);
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(depthWarpTask, producerTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, extinctionTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(extinctionTask, integrationTask));
    const GpuSubmissionPacketRange packetRange = views.compiled.packetRange(depthWarpPacket, integrationPacket);
    ASSERT_TRUE(packetRange.valid());
    EXPECT_EQ(packetRange.packetCount, 3u);
    const GpuCompiledPacketView depthWarpPacketPlan = views.compiled.packet(depthWarpPacket);
    ASSERT_TRUE(depthWarpPacketPlan.valid());
    const GpuCompiledPacketView extinctionPacketPlan = views.compiled.packet(extinctionPacket);
    ASSERT_TRUE(extinctionPacketPlan.valid());
    const GpuCompiledPacketView integrationPacketPlan = views.compiled.packet(integrationPacket);
    ASSERT_TRUE(integrationPacketPlan.valid());
    EXPECT_EQ(depthWarpPacketPlan.plan->queue, primaryComputeQueue);
    EXPECT_EQ(extinctionPacketPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(integrationPacketPlan.plan->queue, primaryComputeQueue);
    ASSERT_EQ(extinctionPacketPlan.plan->taskCount, 2u);
    const GpuTaskId* const extinctionPacketTasks = views.compiled.packet(extinctionPacket).tasks;
    ASSERT_NE(extinctionPacketTasks, nullptr);
    EXPECT_EQ(extinctionPacketTasks[0u], producerTask);
    EXPECT_EQ(extinctionPacketTasks[1u], extinctionTask);
    ASSERT_EQ(extinctionPacketPlan.plan->dependencyCount, 1u);
    ASSERT_EQ(integrationPacketPlan.plan->dependencyCount, 1u);
    const GpuPacketDependency* const integrationPacketDependencies = views.compiled.packet(integrationPacket).dependencies;
    ASSERT_NE(integrationPacketDependencies, nullptr);
    EXPECT_EQ(views.compiled.packet(extinctionPacket).dependencies[0u].producer, depthWarpPacket);
    // The raw control-path edge remains diagnostic; packet scheduling uses Depth-Warp -> Extinction -> Integration.
    EXPECT_EQ(integrationPacketDependencies[0u].producer, extinctionPacket);

    const GpuCompiledTaskView compiledDepthWarp = views.compiled.findTask(depthWarpTask);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledExtinction = views.compiled.findTask(extinctionTask);
    const GpuCompiledTaskView compiledIntegration = views.compiled.findTask(integrationTask);
    ASSERT_TRUE(compiledDepthWarp.valid());
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledExtinction.valid());
    ASSERT_TRUE(compiledIntegration.valid());
    EXPECT_EQ(compiledDepthWarp.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledProducer.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledExtinction.plan->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledIntegration.plan->prologueStateSeedCount, 3u);
    ASSERT_EQ(compiledDepthWarp.plan->prologueBarrierCount, 2u);
    ASSERT_EQ(compiledProducer.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledExtinction.plan->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledIntegration.plan->prologueBarrierCount, 2u);
    const auto hasStateSeed = [&](const GpuTaskId task, const GpuGraphResourceId resource, const GpuSubmissionPacketId sourcePacket){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuPacketStateSeed* const seeds = views.compiled.findTask(task).prologueStateSeeds;
for(u32 seedIndex = 0u; compiledTask.valid() && seeds && seedIndex < compiledTask.plan->prologueStateSeedCount; ++seedIndex){
            if(seeds[seedIndex].resource == resource && seeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after, const GpuPhysicalQueueId sourceQueue, const GpuPhysicalQueueId destinationQueue){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == destinationQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(extinctionTask, depthWarpResource, depthWarpPacket));
    EXPECT_TRUE(hasStateSeed(extinctionTask, controlResource, depthWarpPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, extinctionResource, extinctionPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, controlResource, extinctionPacket));
    EXPECT_TRUE(hasStateSeed(integrationTask, extinctionOverflowResource, extinctionPacket));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarpResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryComputeQueue,
        primaryComputeQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        controlResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryComputeQueue,
        primaryComputeQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        generatedVertexResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        depthWarpResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryComputeQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        controlResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryComputeQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionOverflowResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryComputeQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflowResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryComputeQueue
    ));

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, 4u);
    EXPECT_TRUE(depthWarpRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(extinctionRecorded);
    EXPECT_TRUE(integrationRecorded);
    EXPECT_TRUE(extinctionTimingStarted);
    EXPECT_TRUE(extinctionTimingFinished);
    EXPECT_FALSE(extinctionTiming.has_value());

    // These two semantic anchors intentionally share one ticket, so their same-packet aliases coalesce.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = producerTask, .timingTicket = &extinctionTimingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = extinctionTask, .timingTicket = &extinctionTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        depthWarpTask,
        integrationTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken depthWarpPacketToken = transaction.packetToken(depthWarpPacket);
    const QueueSubmissionToken extinctionPacketToken = transaction.packetToken(extinctionPacket);
    const QueueSubmissionToken integrationPacketToken = transaction.packetToken(integrationPacket);
    ASSERT_TRUE(depthWarpPacketToken.valid());
    ASSERT_TRUE(extinctionPacketToken.valid());
    ASSERT_TRUE(integrationPacketToken.valid());
    EXPECT_EQ(depthWarpPacketToken.queue, CommandQueue::Compute);
    EXPECT_EQ(extinctionPacketToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(integrationPacketToken.queue, CommandQueue::Compute);
    EXPECT_EQ(depthWarpPacketToken.physicalQueueIndex, primaryComputeQueue.index);
    EXPECT_EQ(extinctionPacketToken.physicalQueueIndex, primaryGraphicsQueue.index);
    EXPECT_EQ(integrationPacketToken.physicalQueueIndex, primaryComputeQueue.index);
    EXPECT_NE(depthWarpPacketToken.physicalQueueIndex, extinctionPacketToken.physicalQueueIndex);
    EXPECT_NE(extinctionPacketToken.physicalQueueIndex, integrationPacketToken.physicalQueueIndex);
    const auto expectToken = [&](const QueueSubmissionToken& token, const QueueSubmissionToken& expected){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, expected.queue);
        EXPECT_EQ(token.value, expected.value);
        EXPECT_EQ(token.physicalQueueIndex, expected.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, expected.deviceGeneration);
    };
    expectToken(transaction.taskToken(views.compiled, depthWarpTask), depthWarpPacketToken);
    expectToken(transaction.taskToken(views.compiled, producerTask), extinctionPacketToken);
    expectToken(transaction.taskToken(views.compiled, extinctionTask), extinctionPacketToken);
    expectToken(transaction.taskToken(views.compiled, integrationTask), integrationPacketToken);
    expectToken(depthWarpAcceptedToken, depthWarpPacketToken);
    expectToken(producerAcceptedToken, extinctionPacketToken);
    expectToken(extinctionAcceptedToken, extinctionPacketToken);
    expectToken(integrationAcceptedToken, integrationPacketToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_AsyncAvboitExtinctionLifecycleScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    asyncScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}


// AVBOIT submission is anchored by five semantic stages, but compiler-owned work may introduce another packet
// inside that range. The auxiliary Graphics packet deliberately has no timing binding; task-derived submission
// must still accept it together with the five timed G/C/G/C/G stage packets.
TEST_F(DescriptorBufferRoundTripTest, AsyncAvboitSemanticRangeAcceptsAuxiliaryCompilerPacket){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async AVBOIT semantic range: no usable dedicated-compute headless Vulkan device on this host.";

    auto& graphics = asyncScope.graphics();
    auto& device = graphics.getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Async AVBOIT semantic range: adapter has no dedicated compute-only queue family.";

    auto& timing = graphics.gpuTiming();
    auto& timingSink = asyncScope.gpuTimingSink();
    asyncScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AsyncAvboitExtinctionLifecycleScope.identity, device, 5u));
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
    ASSERT_TRUE(device.waitForIdle());

    auto workBuffer = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/async_avboit_semantic_range_work"))
            .setByteSize(sizeof(u32) * 4u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(workBuffer.get(), nullptr);

    GpuTaskGraph graph(asyncScope.arena());
    const BufferDesc& workBufferDesc = workBuffer->getDescription();
    const GpuGraphResourceId workResource = graph.importBuffer(
        workBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(workBufferDesc.debugName)
            .setMarkerLabel("AVBOIT Semantic Range Work")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(workBufferDesc.initialState)
            .setQueueSharing(workBufferDesc.queueSharing)
    );
    ASSERT_TRUE(workResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuTaskResourceUse workUse{
        .resource = workResource,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::ReadWrite,
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    GpuTimingSubmissionTicket depthWarpTimingTicket(timing);
    GpuTimingSubmissionTicket extinctionTimingTicket(timing);
    GpuTimingSubmissionTicket integrationTimingTicket(timing);
    GpuTimingSubmissionTicket accumulationTimingTicket(timing);
    GpuTimingSubmissionTicket* const taskTimingTickets[] = {
        &preTimingTicket,
        &depthWarpTimingTicket,
        &extinctionTimingTicket,
        nullptr,
        &integrationTimingTicket,
        &accumulationTimingTicket,
    };
    bool tasksRecorded[6u] = {};
    QueueSubmissionToken acceptedTaskTokens[6u] = {};
    u32 recordOrdinal = 0u;
    const auto addTask = [&](
        const Name& identity,
        const AStringView markerLabel,
        const GpuQueueRequest& queue,
        const GpuTaskId dependency,
        GpuTimingSubmissionTicket* const timingTicket,
        const usize taskIndex
    ){
        NativePacketAsyncAvboitExtinctionLifecycleTask::Payload payload;
        payload.expectations[0u] = { workBuffer.get(), ResourceStates::UnorderedAccess };
        payload.expectationCount = 1u;
        payload.recordOrdinal = &recordOrdinal;
        payload.expectedOrdinal = static_cast<u32>(taskIndex);
        payload.device = timingTicket ? &device : nullptr;
        payload.timing = timingTicket ? &timing : nullptr;
        payload.timingTicket = timingTicket;
        payload.recordTiming = timingTicket != nullptr;
        payload.recorded = &tasksRecorded[taskIndex];
        payload.acceptedToken = &acceptedTaskTokens[taskIndex];

        GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(identity)
            .setMarkerLabel(markerLabel)
            .setQueue(queue)
            .setScheduling(scheduling)
            .setResourceUses(&workUse, 1u)
        ;
        if(dependency.valid())
            taskDesc.setDependencies(&dependency, 1u);
        return graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(taskDesc, Move(payload));
    };

    GpuTaskId tasks[6u] = {};
    tasks[0u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_pre"),
        "AVBOIT Pre",
        graphicsQueue,
        {},
        taskTimingTickets[0u],
        0u
    );
    tasks[1u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_depth_warp"),
        "AVBOIT Depth Warp",
        computeQueue,
        tasks[0u],
        taskTimingTickets[1u],
        1u
    );
    tasks[2u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_extinction"),
        "AVBOIT Extinction",
        graphicsQueue,
        tasks[1u],
        taskTimingTickets[2u],
        2u
    );
    tasks[3u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_auxiliary"),
        "AVBOIT Compiler Auxiliary",
        graphicsQueue,
        tasks[2u],
        taskTimingTickets[3u],
        3u
    );
    tasks[4u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_integration"),
        "AVBOIT Integration",
        computeQueue,
        tasks[3u],
        taskTimingTickets[4u],
        4u
    );
    tasks[5u] = addTask(
        Name("tests/descriptor_buffer/async_avboit_semantic_accumulation"),
        "AVBOIT Accumulation",
        graphicsQueue,
        tasks[4u],
        taskTimingTickets[5u],
        5u
    );
    for(const GpuTaskId task : tasks)
        ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId primaryComputeQueue = device.getPrimaryPhysicalQueue(CommandQueue::Compute);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 1u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    ASSERT_TRUE(primaryComputeQueue.valid());
    ASSERT_NE(primaryGraphicsQueue, primaryComputeQueue);

    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/async_avboit_semantic_range_scratch"));
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), LengthOf(tasks));

    const CommandQueue::Enum expectedQueueClasses[] = {
        CommandQueue::Graphics,
        CommandQueue::Compute,
        CommandQueue::Graphics,
        CommandQueue::Graphics,
        CommandQueue::Compute,
        CommandQueue::Graphics,
    };
    const GpuPhysicalQueueId expectedQueues[] = {
        primaryGraphicsQueue,
        primaryComputeQueue,
        primaryGraphicsQueue,
        primaryGraphicsQueue,
        primaryComputeQueue,
        primaryGraphicsQueue,
    };
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);
        const GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, expectedQueues[taskIndex]);
        EXPECT_EQ(assignment->queueClass, expectedQueueClasses[taskIndex]);
        const GpuSubmissionPacketId packet = views.compiled.packetForTask(tasks[taskIndex]);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(views.compiled.packetIdAt(taskIndex), packet);
    }

    const GpuSubmissionPacketRange packetRange = views.compiled.packetRangeForTasks(tasks[0u], tasks[5u]);
    ASSERT_TRUE(packetRange.valid());
    ASSERT_TRUE(views.compiled.validPacketRange(packetRange));
    ASSERT_EQ(packetRange.packetCount, LengthOf(tasks));

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        tasks[0u],
        tasks[5u],
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    for(const bool recorded : tasksRecorded)
        EXPECT_TRUE(recorded);

    const GpuTaskGraphTaskTimingTicket timingBindings[] = {
        GpuTaskGraphTaskTimingTicket{ .task = tasks[0u], .timingTicket = taskTimingTickets[0u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[1u], .timingTicket = taskTimingTickets[1u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[2u], .timingTicket = taskTimingTickets[2u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[4u], .timingTicket = taskTimingTickets[4u] },
        GpuTaskGraphTaskTimingTicket{ .task = tasks[5u], .timingTicket = taskTimingTickets[5u] },
    };
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        tasks[0u],
        tasks[5u],
        nullptr,
        0u,
        timingBindings,
        LengthOf(timingBindings),
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.hasAcceptedPackets());
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const GpuSubmissionPacketId packet = views.compiled.packetForTask(tasks[taskIndex]);
        const QueueSubmissionToken packetToken = transaction.packetToken(packet);
        const QueueSubmissionToken taskToken = transaction.taskToken(views.compiled, tasks[taskIndex]);
        ASSERT_TRUE(packetToken.valid());
        ASSERT_TRUE(taskToken.valid());
        ASSERT_TRUE(acceptedTaskTokens[taskIndex].valid());
        EXPECT_EQ(taskToken.queue, packetToken.queue);
        EXPECT_EQ(taskToken.value, packetToken.value);
        EXPECT_EQ(acceptedTaskTokens[taskIndex].queue, packetToken.queue);
        EXPECT_EQ(acceptedTaskTokens[taskIndex].value, packetToken.value);
        EXPECT_TRUE(packetToken.matchesPhysicalQueue(
            expectedQueues[taskIndex].index,
            expectedQueues[taskIndex].deviceGeneration
        ));
    }

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_AsyncAvboitExtinctionLifecycleScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, LengthOf(timingBindings));

    asyncScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

