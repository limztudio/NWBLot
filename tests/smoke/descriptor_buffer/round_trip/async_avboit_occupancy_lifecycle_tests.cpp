// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_AsyncAvboitOccupancyLifecycleScope(
    "tests/timing_async_avboit_occupancy_lifecycle"
);


// Occupancy is AVBOIT Pre's split Graphics phase. Its optional generator must follow the final clear, share the
// Pre packet/ticket with raster, and publish coverage to dedicated-Compute depth warp without any native bridge.
TEST_F(DescriptorBufferRoundTripTest, AsyncAvboitOccupancyComputeEmulationSharesPrePacketTicketBeforeDepthWarp){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async AVBOIT Occupancy: no usable dedicated-compute headless Vulkan device on this host.";

    auto& graphics = asyncScope.graphics();
    auto& device = graphics.getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Async AVBOIT Occupancy: adapter has no dedicated compute-only queue family.";

    auto& timing = graphics.gpuTiming();
    auto& timingSink = asyncScope.gpuTimingSink();
    asyncScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AsyncAvboitOccupancyLifecycleScope.identity, device, 1u));
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
    auto coverage = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_occupancy_coverage"));
    auto generatedVertex = createWorkBuffer(
        Name("tests/descriptor_buffer/async_avboit_occupancy_generated_vertex"),
        true
    );
    auto depthWarp = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_occupancy_depth_warp"));
    ASSERT_TRUE(coverage && generatedVertex && depthWarp);

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
    const GpuGraphResourceId coverageResource = importBuffer(coverage, "AVBOIT Coverage");
    const GpuGraphResourceId generatedVertexResource = importBuffer(
        generatedVertex,
        "AVBOIT Occupancy Generated Vertex"
    );
    const GpuGraphResourceId depthWarpResource = importBuffer(depthWarp, "AVBOIT Depth Warp");
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(generatedVertexResource.valid());
    ASSERT_TRUE(depthWarpResource.valid());

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
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = true;
    preScheduling.allowMergeAcrossConsumerFrontier = true;
    GpuTaskSchedulingHint depthWarpScheduling;
    depthWarpScheduling.cost = GpuTaskCostHint::Medium;
    depthWarpScheduling.forceSubmissionBoundary = true;
    depthWarpScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
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
    const GpuTaskResourceUse occupancyUses[] = {
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse depthWarpUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = depthWarpResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };

    GpuTimingSubmissionTicket occupancyTimingTicket(timing);
    Optional<GpuTimingMeasure> occupancyTiming;
    u32 recordOrdinal = 0u;
    bool clearRecorded = false;
    bool producerRecorded = false;
    bool occupancyRecorded = false;
    bool depthWarpRecorded = false;
    bool occupancyTimingStarted = false;
    bool occupancyTimingFinished = false;
    QueueSubmissionToken clearAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken occupancyAcceptedToken;
    QueueSubmissionToken depthWarpAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload clearPayload;
    clearPayload.expectations[0u] = { coverage.get(), ResourceStates::CopyDest };
    clearPayload.expectationCount = 1u;
    clearPayload.recordOrdinal = &recordOrdinal;
    clearPayload.expectedOrdinal = 0u;
    clearPayload.recorded = &clearRecorded;
    clearPayload.acceptedToken = &clearAcceptedToken;
    const GpuTaskId clearTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_occupancy_clear_task"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses)),
        Move(clearPayload)
    );
    ASSERT_TRUE(clearTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { generatedVertex.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 1u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 1u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &occupancyTimingTicket;
    producerPayload.sharedTiming = &occupancyTiming;
    producerPayload.timingScope = &s_AsyncAvboitOccupancyLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &occupancyTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_occupancy_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Occupancy Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(preScheduling)
            .setDependencies(&clearTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload occupancyPayload;
    occupancyPayload.expectations[0u] = { generatedVertex.get(), ResourceStates::VertexBuffer };
    occupancyPayload.expectations[1u] = { coverage.get(), ResourceStates::UnorderedAccess };
    occupancyPayload.expectationCount = 2u;
    occupancyPayload.recordOrdinal = &recordOrdinal;
    occupancyPayload.expectedOrdinal = 2u;
    occupancyPayload.device = &device;
    occupancyPayload.timing = &timing;
    occupancyPayload.timingTicket = &occupancyTimingTicket;
    occupancyPayload.sharedTiming = &occupancyTiming;
    occupancyPayload.timingScope = &s_AsyncAvboitOccupancyLifecycleScope;
    occupancyPayload.finishTiming = true;
    occupancyPayload.timingFinished = &occupancyTimingFinished;
    occupancyPayload.recorded = &occupancyRecorded;
    occupancyPayload.acceptedToken = &occupancyAcceptedToken;
    const GpuTaskId occupancyTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_occupancy_raster_task"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(preScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses)),
        Move(occupancyPayload)
    );
    ASSERT_TRUE(occupancyTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload depthWarpPayload;
    depthWarpPayload.expectations[0u] = { coverage.get(), ResourceStates::ShaderResource };
    depthWarpPayload.expectations[1u] = { depthWarp.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectationCount = 2u;
    depthWarpPayload.recordOrdinal = &recordOrdinal;
    depthWarpPayload.expectedOrdinal = 3u;
    depthWarpPayload.recorded = &depthWarpRecorded;
    depthWarpPayload.acceptedToken = &depthWarpAcceptedToken;
    const GpuTaskId depthWarpTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_occupancy_depth_warp_task"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(computeQueue)
            .setScheduling(depthWarpScheduling)
            .setDependencies(&occupancyTask, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses)),
        Move(depthWarpPayload)
    );
    ASSERT_TRUE(depthWarpTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/async_avboit_occupancy_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(clearTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, occupancyTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasInferredEdge(clearTask, occupancyTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, occupancyTask));
    EXPECT_TRUE(analysis.hasInferredEdge(occupancyTask, depthWarpTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 4u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], clearTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], occupancyTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], depthWarpTask);

    const auto expectAssignment = [&](const GpuTaskId task, const GpuPhysicalQueueId queue, const CommandQueue::Enum queueClass){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(clearTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(producerTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(occupancyTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(depthWarpTask, primaryComputeQueue, CommandQueue::Compute);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId prePacket = views.compiled.packetForTask(clearTask);
    const GpuSubmissionPacketId depthWarpPacket = views.compiled.packetForTask(depthWarpTask);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    EXPECT_EQ(prePacket, views.compiled.packetForTask(producerTask));
    EXPECT_EQ(prePacket, views.compiled.packetForTask(occupancyTask));
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_EQ(views.compiled.packetIdAt(0u), prePacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), depthWarpPacket);
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, occupancyTask));
    const GpuSubmissionPacketRange packetRange = views.compiled.packetRange(prePacket, depthWarpPacket);
    ASSERT_TRUE(packetRange.valid());
    EXPECT_EQ(packetRange.packetCount, 2u);
    const GpuCompiledPacketView prePacketPlan = views.compiled.packet(prePacket);
    ASSERT_TRUE(prePacketPlan.valid());
    const GpuCompiledPacketView depthWarpPacketPlan = views.compiled.packet(depthWarpPacket);
    ASSERT_TRUE(depthWarpPacketPlan.valid());
    EXPECT_EQ(prePacketPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(depthWarpPacketPlan.plan->queue, primaryComputeQueue);
    ASSERT_EQ(prePacketPlan.plan->taskCount, 3u);
    const GpuTaskId* const prePacketTasks = views.compiled.packet(prePacket).tasks;
    ASSERT_NE(prePacketTasks, nullptr);
    EXPECT_EQ(prePacketTasks[0u], clearTask);
    EXPECT_EQ(prePacketTasks[1u], producerTask);
    EXPECT_EQ(prePacketTasks[2u], occupancyTask);
    ASSERT_EQ(depthWarpPacketPlan.plan->dependencyCount, 1u);
    ASSERT_NE(views.compiled.packet(depthWarpPacket).dependencies, nullptr);
    EXPECT_EQ(views.compiled.packet(depthWarpPacket).dependencies[0u].producer, prePacket);

    const GpuCompiledTaskView compiledDepthWarp = views.compiled.findTask(depthWarpTask);
    ASSERT_TRUE(compiledDepthWarp.valid());
    ASSERT_EQ(compiledDepthWarp.plan->prologueStateSeedCount, 1u);
    const GpuPacketStateSeed* const depthWarpSeeds = views.compiled.findTask(depthWarpTask).prologueStateSeeds;
    ASSERT_NE(depthWarpSeeds, nullptr);
    EXPECT_EQ(depthWarpSeeds[0u].resource, coverageResource);
    EXPECT_EQ(depthWarpSeeds[0u].sourcePacket, prePacket);
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
    EXPECT_TRUE(hasBufferTransition(
        clearTask,
        coverageResource,
        ResourceStates::Common,
        ResourceStates::CopyDest,
        primaryGraphicsQueue,
        primaryGraphicsQueue
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
        occupancyTask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        occupancyTask,
        coverageResource,
        ResourceStates::CopyDest,
        ResourceStates::UnorderedAccess,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        coverageResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryComputeQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarpResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryComputeQueue,
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
    EXPECT_TRUE(clearRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(occupancyRecorded);
    EXPECT_TRUE(depthWarpRecorded);
    EXPECT_TRUE(occupancyTimingStarted);
    EXPECT_TRUE(occupancyTimingFinished);
    EXPECT_FALSE(occupancyTiming.has_value());

    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = producerTask, .timingTicket = &occupancyTimingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = occupancyTask, .timingTicket = &occupancyTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        clearTask,
        depthWarpTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken prePacketToken = transaction.packetToken(prePacket);
    const QueueSubmissionToken depthWarpPacketToken = transaction.packetToken(depthWarpPacket);
    ASSERT_TRUE(prePacketToken.valid());
    ASSERT_TRUE(depthWarpPacketToken.valid());
    EXPECT_EQ(prePacketToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(depthWarpPacketToken.queue, CommandQueue::Compute);
    EXPECT_EQ(prePacketToken.physicalQueueIndex, primaryGraphicsQueue.index);
    EXPECT_EQ(depthWarpPacketToken.physicalQueueIndex, primaryComputeQueue.index);
    EXPECT_NE(prePacketToken.physicalQueueIndex, depthWarpPacketToken.physicalQueueIndex);
    const auto expectToken = [&](const QueueSubmissionToken& token, const QueueSubmissionToken& expected){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, expected.queue);
        EXPECT_EQ(token.value, expected.value);
        EXPECT_EQ(token.physicalQueueIndex, expected.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, expected.deviceGeneration);
    };
    expectToken(transaction.taskToken(views.compiled, clearTask), prePacketToken);
    expectToken(transaction.taskToken(views.compiled, producerTask), prePacketToken);
    expectToken(transaction.taskToken(views.compiled, occupancyTask), prePacketToken);
    expectToken(transaction.taskToken(views.compiled, depthWarpTask), depthWarpPacketToken);
    expectToken(clearAcceptedToken, prePacketToken);
    expectToken(producerAcceptedToken, prePacketToken);
    expectToken(occupancyAcceptedToken, prePacketToken);
    expectToken(depthWarpAcceptedToken, depthWarpPacketToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_AsyncAvboitOccupancyLifecycleScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    asyncScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

