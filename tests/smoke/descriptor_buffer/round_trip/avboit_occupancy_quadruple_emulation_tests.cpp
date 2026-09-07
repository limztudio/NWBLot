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


// Four regular Occupancy draws reuse one retained generated-vertex output only when every dispatch is consumed
// before the next overwrite. Exercise Pre -> stream -> typed clear -> D(A) -> R(A) -> D(B) -> R(B) -> D(C) ->
// R(C) -> D(D) -> R(D) -> packet-local Depth Warp in one accepting Graphics packet under Pre's shared timing ticket.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitOccupancySharedOutputComputeEmulationQuadruplesStayInPrePacket){
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
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_state_probe"),
        false,
        true
    );
    auto materialStream = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_material_stream")
    );
    auto coverage = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_coverage")
    );
    auto depthWarp = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_depth_warp")
    );
    auto generatedVertex = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_generated_vertex"),
        true
    );
    ASSERT_TRUE(stateProbe && materialStream && coverage && depthWarp && generatedVertex);

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
    const GpuGraphResourceId stateProbeResource = importBuffer(
        stateProbe,
        "AVBOIT Occupancy Shared Output State Probe"
    );
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Occupancy Shared Output Material Stream"
    );
    const GpuGraphResourceId coverageResource = importBuffer(
        coverage,
        "AVBOIT Occupancy Shared Output Coverage"
    );
    const GpuGraphResourceId depthWarpResource = importBuffer(
        depthWarp,
        "AVBOIT Occupancy Shared Output Depth Warp"
    );
    const GpuGraphResourceId generatedVertexResource = importBuffer(
        generatedVertex,
        "AVBOIT Occupancy Shared Generated Vertex"
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(depthWarpResource.valid());
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
    const GpuTaskResourceUse dispatchUses[] = {
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
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse rasterUses[] = {
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
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = depthWarpResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> occupancyTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool streamRecorded = false;
    bool clearRecorded = false;
    bool sharedPhaseRecorded[8u] = {};
    bool depthWarpRecorded = false;
    bool occupancyTimingStarted = false;
    bool occupancyTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken clearAcceptedToken;
    QueueSubmissionToken sharedPhaseAcceptedTokens[8u] = {};
    QueueSubmissionToken depthWarpAcceptedToken;

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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_pre_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_material_upload_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_clear_task"))
            .setMarkerLabel("AVBOIT Clear Coverage")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamTask, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses)),
        Move(clearPayload)
    );
    ASSERT_TRUE(clearTask.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_dispatch_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_raster_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_dispatch_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_raster_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_dispatch_c_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_raster_c_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_dispatch_d_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_raster_d_task"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Occupancy Shared Compute Emulation Generate A",
        "AVBOIT Occupancy Shared Compute Emulation Raster A",
        "AVBOIT Occupancy Shared Compute Emulation Generate B",
        "AVBOIT Occupancy Shared Compute Emulation Raster B",
        "AVBOIT Occupancy Shared Compute Emulation Generate C",
        "AVBOIT Occupancy Shared Compute Emulation Raster C",
        "AVBOIT Occupancy Shared Compute Emulation Generate D",
        "AVBOIT Occupancy Shared Compute Emulation Raster D",
    };
    GpuTaskId sharedPhaseTasks[LengthOf(sharedPhaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(sharedPhaseTasks); ++phaseIndex){
        const bool isRaster = phaseIndex % 2u != 0u;
        const GpuTaskId dependency = phaseIndex == 0u ? clearTask : sharedPhaseTasks[phaseIndex - 1u];
        GpuTaskDesc phaseDesc;
        phaseDesc
            .setIdentity(sharedPhaseIdentities[phaseIndex])
            .setMarkerLabel(sharedPhaseMarkers[phaseIndex])
            .setQueue(isRaster ? graphicsQueue : graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(
                isRaster ? rasterUses : dispatchUses,
                isRaster ? LengthOf(rasterUses) : LengthOf(dispatchUses)
            )
        ;
        NativePacketAsyncAvboitExtinctionLifecycleTask::Payload phasePayload;
        phasePayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
        phasePayload.expectations[1u] = { materialStream.get(), ResourceStates::ShaderResource };
        phasePayload.expectations[2u] = {
            generatedVertex.get(),
            isRaster ? ResourceStates::VertexBuffer : ResourceStates::UnorderedAccess,
        };
        phasePayload.expectationCount = 3u;
        if(isRaster){
            phasePayload.expectations[3u] = { coverage.get(), ResourceStates::UnorderedAccess };
            phasePayload.expectationCount = 4u;
        }
        phasePayload.recordOrdinal = &recordOrdinal;
        phasePayload.expectedOrdinal = static_cast<u32>(phaseIndex + 3u);
        phasePayload.device = &device;
        phasePayload.timing = &timing;
        phasePayload.timingTicket = &preTimingTicket;
        phasePayload.sharedTiming = &occupancyTiming;
        phasePayload.timingScope = &s_UnsplitAvboitOccupancyLifecycleScope;
        phasePayload.startTiming = phaseIndex == 0u;
        phasePayload.finishTiming = phaseIndex + 1u == LengthOf(sharedPhaseTasks);
        phasePayload.timingStarted = &occupancyTimingStarted;
        phasePayload.timingFinished = &occupancyTimingFinished;
        phasePayload.recorded = &sharedPhaseRecorded[phaseIndex];
        phasePayload.acceptedToken = &sharedPhaseAcceptedTokens[phaseIndex];
        sharedPhaseTasks[phaseIndex] = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
            phaseDesc,
            Move(phasePayload)
        );
        ASSERT_TRUE(sharedPhaseTasks[phaseIndex].valid());
    }
    const GpuTaskId dispatchATask = sharedPhaseTasks[0u];
    const GpuTaskId rasterATask = sharedPhaseTasks[1u];
    const GpuTaskId dispatchBTask = sharedPhaseTasks[2u];
    const GpuTaskId rasterBTask = sharedPhaseTasks[3u];
    const GpuTaskId dispatchCTask = sharedPhaseTasks[4u];
    const GpuTaskId rasterCTask = sharedPhaseTasks[5u];
    const GpuTaskId dispatchDTask = sharedPhaseTasks[6u];
    const GpuTaskId rasterDTask = sharedPhaseTasks[7u];

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload depthWarpPayload;
    depthWarpPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    depthWarpPayload.expectations[1u] = { coverage.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectations[2u] = { depthWarp.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectationCount = 3u;
    depthWarpPayload.recordOrdinal = &recordOrdinal;
    depthWarpPayload.expectedOrdinal = 11u;
    depthWarpPayload.timingTicket = &preTimingTicket;
    depthWarpPayload.recorded = &depthWarpRecorded;
    depthWarpPayload.acceptedToken = &depthWarpAcceptedToken;
    const GpuTaskId depthWarpTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_depth_warp_task"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterDTask, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses)),
        Move(depthWarpPayload)
    );
    ASSERT_TRUE(depthWarpTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_occupancy_shared_output_quad_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, clearTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(clearTask, dispatchATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterCTask, dispatchDTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchDTask, rasterDTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterDTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, dispatchATask));
    EXPECT_TRUE(analysis.hasInferredEdge(clearTask, rasterATask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterCTask, dispatchDTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchDTask, rasterDTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterDTask, depthWarpTask));

    const GpuTaskId allTasks[] = {
        preTask,
        streamTask,
        clearTask,
        dispatchATask,
        rasterATask,
        dispatchBTask,
        rasterBTask,
        dispatchCTask,
        rasterCTask,
        dispatchDTask,
        rasterDTask,
        depthWarpTask,
    };
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(allTasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(allTasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], allTasks[taskIndex]);
    for(const GpuTaskId task : allTasks){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    for(const GpuTaskId task : allTasks)
        EXPECT_EQ(packet, views.compiled.packetForTask(task));
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, depthWarpTask));
    for(usize taskIndex = 0u; taskIndex + 1u < LengthOf(allTasks); ++taskIndex)
        EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(allTasks[taskIndex], allTasks[taskIndex + 1u]));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, LengthOf(allTasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(allTasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], allTasks[taskIndex]);

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
    const auto hasBufferUav = [&](const GpuTaskId task, const GpuGraphResourceId resource){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferUav
                && barrier.resource == resource
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::UnorderedAccess
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
        dispatchATask,
        materialStreamResource,
        ResourceStates::CopyDest,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchATask,
        generatedVertexResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterATask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterATask,
        coverageResource,
        ResourceStates::CopyDest,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchBTask,
        generatedVertexResource,
        ResourceStates::VertexBuffer,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterBTask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchCTask,
        generatedVertexResource,
        ResourceStates::VertexBuffer,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterCTask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        dispatchDTask,
        generatedVertexResource,
        ResourceStates::VertexBuffer,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterDTask,
        generatedVertexResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::VertexBuffer
    ));
    EXPECT_TRUE(hasBufferUav(depthWarpTask, coverageResource));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarpResource,
        ResourceStates::Common,
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
    EXPECT_EQ(recordOrdinal, 12u);
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(streamRecorded);
    EXPECT_TRUE(clearRecorded);
    for(const bool recorded : sharedPhaseRecorded)
        EXPECT_TRUE(recorded);
    EXPECT_TRUE(depthWarpRecorded);
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
    EXPECT_EQ(finalStateProbe->getBufferState(depthWarp.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    finalStateProbe->close();

    // Only the semantic AVBOIT Pre task owns a submission binding. Its one Graphics packet must publish the
    // whole bounded shared-output route, its cross-callback timing span, and every callback's packet token.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        depthWarpTask,
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
    for(const GpuTaskId task : allTasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(streamAcceptedToken);
    expectPacketToken(clearAcceptedToken);
    for(const QueueSubmissionToken& token : sharedPhaseAcceptedTokens)
        expectPacketToken(token);
    expectPacketToken(depthWarpAcceptedToken);

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

