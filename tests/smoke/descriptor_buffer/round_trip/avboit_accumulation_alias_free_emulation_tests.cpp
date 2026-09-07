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


// On the normal AVBOIT route, Accumulation stays in AVBOIT Pre's one accepting Graphics packet.  Its final
// immutable stream must precede an alias-free regular compute-emulation producer, raster must consume the outputs
// as vertex buffers, and the no-op finalizer must publish the attachment/depth ShaderResource handoff.  The one
// AVBOIT Pre timing ticket is intentionally bound at the Pre task only; the later callbacks record under it and
// must receive the same accepted packet token.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitAccumulationAliasFreeComputeEmulationStaysInPrePacket){
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
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_state_probe"),
        false,
        true
    );
    auto materialStream = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_material_stream"));
    auto generatedVertexA = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_generated_vertex_a"),
        true
    );
    auto generatedVertexB = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_generated_vertex_b"),
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
        && generatedVertexA
        && generatedVertexB
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
    const GpuGraphResourceId stateProbeResource = importBuffer(stateProbe, "AVBOIT Accumulation State Probe");
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Accumulation Material Stream"
    );
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "AVBOIT Accumulation Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "AVBOIT Accumulation Generated Vertex B"
    );
    const GpuGraphResourceId accumColorResource = importTexture(
        accumColor,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_color"),
        "AVBOIT Accumulation Color"
    );
    const GpuGraphResourceId accumExtinctionResource = importTexture(
        accumExtinction,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_extinction"),
        "AVBOIT Accumulation Extinction"
    );
    const GpuGraphResourceId deferredDepthResource = importTexture(
        deferredDepth,
        Name("tests/descriptor_buffer/unsplit_avboit_accumulation_depth"),
        "Deferred Depth"
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(generatedVertexAResource.valid());
    ASSERT_TRUE(generatedVertexBResource.valid());
    ASSERT_TRUE(accumColorResource.valid());
    ASSERT_TRUE(accumExtinctionResource.valid());
    ASSERT_TRUE(deferredDepthResource.valid());

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Outputs")
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
    const GpuTaskResourceSetUse producerGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceSetUse rasterGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> accumulationTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool streamRecorded = false;
    bool producerRecorded = false;
    bool rasterRecorded = false;
    bool finalizerRecorded = false;
    bool accumulationTimingStarted = false;
    bool accumulationTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken rasterAcceptedToken;
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_pre_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_material_upload_task"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses)),
        Move(streamPayload)
    );
    ASSERT_TRUE(streamTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    producerPayload.expectations[1u] = { materialStream.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[2u] = { generatedVertexA.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectations[3u] = { generatedVertexB.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 4u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 2u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &preTimingTicket;
    producerPayload.sharedTiming = &accumulationTiming;
    producerPayload.timingScope = &s_UnsplitAvboitAccumulationLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &accumulationTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload rasterPayload;
    rasterPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    rasterPayload.expectations[1u] = { materialStream.get(), ResourceStates::ShaderResource };
    rasterPayload.expectations[2u] = { generatedVertexA.get(), ResourceStates::VertexBuffer };
    rasterPayload.expectations[3u] = { generatedVertexB.get(), ResourceStates::VertexBuffer };
    rasterPayload.expectationCount = 4u;
    rasterPayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::RenderTarget };
    rasterPayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::RenderTarget };
    rasterPayload.textureExpectations[2u] = { deferredDepth.get(), ResourceStates::DepthRead };
    rasterPayload.textureExpectationCount = 3u;
    rasterPayload.recordOrdinal = &recordOrdinal;
    rasterPayload.expectedOrdinal = 3u;
    rasterPayload.device = &device;
    rasterPayload.timing = &timing;
    rasterPayload.timingTicket = &preTimingTicket;
    rasterPayload.sharedTiming = &accumulationTiming;
    rasterPayload.timingScope = &s_UnsplitAvboitAccumulationLifecycleScope;
    rasterPayload.finishTiming = true;
    rasterPayload.timingFinished = &accumulationTimingFinished;
    rasterPayload.recorded = &rasterRecorded;
    rasterPayload.acceptedToken = &rasterAcceptedToken;
    const GpuTaskId rasterTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_raster_task"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(rasterUses, LengthOf(rasterUses))
            .setResourceSetUses(&rasterGeneratedVertexUse, 1u),
        Move(rasterPayload)
    );
    ASSERT_TRUE(rasterTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload finalizerPayload;
    finalizerPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    finalizerPayload.expectationCount = 1u;
    finalizerPayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectations[2u] = { deferredDepth.get(), ResourceStates::ShaderResource };
    finalizerPayload.textureExpectationCount = 3u;
    finalizerPayload.recordOrdinal = &recordOrdinal;
    finalizerPayload.expectedOrdinal = 4u;
    finalizerPayload.timingTicket = &preTimingTicket;
    finalizerPayload.recorded = &finalizerRecorded;
    finalizerPayload.acceptedToken = &finalizerAcceptedToken;
    const GpuTaskId finalizerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_finalize_task"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterTask, 1u)
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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_accumulation_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterTask, finalizerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterTask, finalizerTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 5u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], preTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterTask);
    EXPECT_EQ(analysis.topologicalOrder()[4u], finalizerTask);

    const auto expectAssignment = [&](const GpuTaskId task){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    };
    expectAssignment(preTask);
    expectAssignment(streamTask);
    expectAssignment(producerTask);
    expectAssignment(rasterTask);
    expectAssignment(finalizerTask);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(packet, views.compiled.packetForTask(streamTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(producerTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(rasterTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(finalizerTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, finalizerTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(preTask, streamTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(streamTask, producerTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(producerTask, rasterTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(rasterTask, finalizerTask));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, 5u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], preTask);
    EXPECT_EQ(packetTasks[1u], streamTask);
    EXPECT_EQ(packetTasks[2u], producerTask);
    EXPECT_EQ(packetTasks[3u], rasterTask);
    EXPECT_EQ(packetTasks[4u], finalizerTask);

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
            rasterTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
        accumColorResource,
        ResourceStates::Common,
        ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
        accumExtinctionResource,
        ResourceStates::Common,
        ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
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
    EXPECT_EQ(recordOrdinal, 5u);
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(streamRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(rasterRecorded);
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
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumColor.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumExtinction.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(deferredDepth.get(), 0u, 0u), ResourceStates::ShaderResource);
    finalStateProbe->close();

    // The renderer submits this semantic packet through AVBOIT Pre.  Resolving only that task's binding must accept
    // the producer/raster timing pair and every callback with one Graphics submission token.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
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
    expectPacketToken(transaction.taskToken(views.compiled, preTask));
    expectPacketToken(transaction.taskToken(views.compiled, streamTask));
    expectPacketToken(transaction.taskToken(views.compiled, producerTask));
    expectPacketToken(transaction.taskToken(views.compiled, rasterTask));
    expectPacketToken(transaction.taskToken(views.compiled, finalizerTask));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(streamAcceptedToken);
    expectPacketToken(producerAcceptedToken);
    expectPacketToken(rasterAcceptedToken);
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

