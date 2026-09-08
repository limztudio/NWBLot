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


// AVBOIT Accumulation reuses one generated-vertex buffer across four immediately consumed dispatches.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitAccumulationSharedOutputComputeEmulationQuadruplesStayInPrePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitAccumulationLifecycleScope.identity, device, 1u));
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
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_state_probe"),
        false,
        true
    );
    auto materialStream = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_material_stream")
    );
    auto generatedVertex = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_generated_vertex"),
        true
    );
    const TextureDesc accumulationTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
    ;
    auto accumColor = device.createTexture(accumulationTextureDesc);
    auto accumExtinction = device.createTexture(accumulationTextureDesc);
    const TextureDesc depthTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::D32S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
    ;
    auto deferredDepth = device.createTexture(depthTextureDesc);
    ASSERT_TRUE(
        stateProbe
        && materialStream
        && generatedVertex
        && accumColor
        && accumExtinction
        && deferredDepth
    );
    Texture* const initialTextures[] = {
        accumColor.get(),
        accumExtinction.get(),
        deferredDepth.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

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
    const auto importTexture = [&graph](const TextureHandle& texture, const Name& identity, const AStringView markerLabel){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
        );
    };
    const GpuGraphResourceId stateProbeResource = importBuffer(
        stateProbe,
        "AVBOIT Accumulation Shared Output State Probe"
    );
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Accumulation Shared Output Material Stream"
    );
    const GpuGraphResourceId generatedVertexResource = importBuffer(
        generatedVertex,
        "AVBOIT Accumulation Shared Generated Vertex"
    );
    const GpuGraphResourceId accumColorResource = importTexture(
        accumColor,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_color"),
        "AVBOIT Accumulation Shared Output Color"
    );
    const GpuGraphResourceId accumExtinctionResource = importTexture(
        accumExtinction,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_extinction"),
        "AVBOIT Accumulation Shared Output Extinction"
    );
    const GpuGraphResourceId deferredDepthResource = importTexture(
        deferredDepth,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_depth"),
        "AVBOIT Accumulation Shared Output Depth"
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(generatedVertexResource.valid());
    ASSERT_TRUE(accumColorResource.valid());
    ASSERT_TRUE(accumExtinctionResource.valid());
    ASSERT_TRUE(deferredDepthResource.valid());

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
            .resource = accumColorResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = accumExtinctionResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = deferredDepthResource,
            .range = {},
            .requiredState = ResourceStates::DepthRead,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse finalizerUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumExtinctionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = deferredDepthResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> accumulationTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool streamRecorded = false;
    bool sharedPhaseRecorded[8u] = {};
    bool finalizerRecorded = false;
    bool accumulationTimingStarted = false;
    bool accumulationTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken sharedPhaseAcceptedTokens[8u] = {};
    QueueSubmissionToken finalizerAcceptedToken;

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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_pre_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_material_upload_task"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses)),
        Move(streamPayload)
    );
    ASSERT_TRUE(streamTask.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_dispatch_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_raster_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_dispatch_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_raster_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_dispatch_c_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_raster_c_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_dispatch_d_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_raster_d_task"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Accumulation Shared Compute Emulation Generate A",
        "AVBOIT Accumulation Shared Compute Emulation Raster A",
        "AVBOIT Accumulation Shared Compute Emulation Generate B",
        "AVBOIT Accumulation Shared Compute Emulation Raster B",
        "AVBOIT Accumulation Shared Compute Emulation Generate C",
        "AVBOIT Accumulation Shared Compute Emulation Raster C",
        "AVBOIT Accumulation Shared Compute Emulation Generate D",
        "AVBOIT Accumulation Shared Compute Emulation Raster D",
    };
    GpuTaskId sharedPhaseTasks[LengthOf(sharedPhaseIdentities)] = {};
    for(usize phaseIndex = 0u; phaseIndex < LengthOf(sharedPhaseTasks); ++phaseIndex){
        const bool isRaster = phaseIndex % 2u != 0u;
        const GpuTaskId dependency = phaseIndex == 0u ? streamTask : sharedPhaseTasks[phaseIndex - 1u];
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
            phasePayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::RenderTarget };
            phasePayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::RenderTarget };
            phasePayload.textureExpectations[2u] = { deferredDepth.get(), ResourceStates::DepthRead };
            phasePayload.textureExpectationCount = 3u;
        }
        phasePayload.recordOrdinal = &recordOrdinal;
        phasePayload.expectedOrdinal = static_cast<u32>(phaseIndex + 2u);
        phasePayload.device = &device;
        phasePayload.timing = &timing;
        phasePayload.timingTicket = &preTimingTicket;
        phasePayload.sharedTiming = &accumulationTiming;
        phasePayload.timingScope = &s_UnsplitAvboitAccumulationLifecycleScope;
        phasePayload.startTiming = phaseIndex == 0u;
        phasePayload.finishTiming = phaseIndex + 1u == LengthOf(sharedPhaseTasks);
        phasePayload.timingStarted = &accumulationTimingStarted;
        phasePayload.timingFinished = &accumulationTimingFinished;
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

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload finalizerPayload;
    finalizerPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    finalizerPayload.expectationCount = 1u;
    finalizerPayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectations[2u] = { deferredDepth.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectationCount = 3u;
    finalizerPayload.recordOrdinal = &recordOrdinal;
    finalizerPayload.expectedOrdinal = 10u;
    finalizerPayload.timingTicket = &preTimingTicket;
    finalizerPayload.recorded = &finalizerRecorded;
    finalizerPayload.acceptedToken = &finalizerAcceptedToken;
    const GpuTaskId finalizerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_finalize_task"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterDTask, 1u)
            .setResourceUses(finalizerUses, LengthOf(finalizerUses)),
        Move(finalizerPayload)
    );
    ASSERT_TRUE(finalizerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_shared_output_quad_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, dispatchATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterCTask, dispatchDTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchDTask, rasterDTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterDTask, finalizerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, dispatchATask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterCTask, dispatchDTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchDTask, rasterDTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterDTask, finalizerTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 11u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], preTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], dispatchATask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterATask);
    EXPECT_EQ(analysis.topologicalOrder()[4u], dispatchBTask);
    EXPECT_EQ(analysis.topologicalOrder()[5u], rasterBTask);
    EXPECT_EQ(analysis.topologicalOrder()[6u], dispatchCTask);
    EXPECT_EQ(analysis.topologicalOrder()[7u], rasterCTask);
    EXPECT_EQ(analysis.topologicalOrder()[8u], dispatchDTask);
    EXPECT_EQ(analysis.topologicalOrder()[9u], rasterDTask);
    EXPECT_EQ(analysis.topologicalOrder()[10u], finalizerTask);

    const GpuTaskId allTasks[] = {
        preTask,
        streamTask,
        dispatchATask,
        rasterATask,
        dispatchBTask,
        rasterBTask,
        dispatchCTask,
        rasterCTask,
        dispatchDTask,
        rasterDTask,
        finalizerTask,
    };
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
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, finalizerTask));
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
    const auto hasTextureTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
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
    EXPECT_TRUE(hasTextureTransition(
        rasterATask,
        accumColorResource,
        ResourceStates::Common,
        ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterATask,
        accumExtinctionResource,
        ResourceStates::Common,
        ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterATask,
        deferredDepthResource,
        ResourceStates::Common,
        ResourceStates::DepthRead
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        accumColorResource,
        ResourceStates::RenderTarget,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        accumExtinctionResource,
        ResourceStates::RenderTarget,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        deferredDepthResource,
        ResourceStates::DepthRead,
        ResourceStates::ShaderResource
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
    EXPECT_EQ(recordOrdinal, 11u);
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(streamRecorded);
    for(const bool recorded : sharedPhaseRecorded)
        EXPECT_TRUE(recorded);
    EXPECT_TRUE(finalizerRecorded);
    EXPECT_TRUE(accumulationTimingStarted);
    EXPECT_TRUE(accumulationTimingFinished);
    EXPECT_FALSE(accumulationTiming.has_value());
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(materialStream.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumColor.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumExtinction.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(deferredDepth.get(), 0u, 0u), ResourceStates::ShaderResource);
    finalStateProbe->close();

    // The renderer binds only Pre. Every following callback records beneath that one ticket and must resolve to the
    // same accepted Graphics token, including all four shared-output dispatch/raster pairs and the finalizer.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        finalizerTask,
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
    for(const QueueSubmissionToken& token : sharedPhaseAcceptedTokens)
        expectPacketToken(token);
    expectPacketToken(finalizerAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_UnsplitAvboitAccumulationLifecycleScope.identity);
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

