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


inline constexpr GpuTimingScopeDefinition s_AsyncAvboitAccumulationLifecycleScope(
    "tests/timing_async_avboit_accumulation_lifecycle"
);


// Accumulation is the final split AVBOIT Graphics phase. Its alias-free generator follows the immutable stream
// upload, shares raster's timing ticket, and leaves the attachment finalizer in that accepted packet before
// Deferred Composite returns to dedicated Compute.
TEST_F(DescriptorBufferRoundTripTest, AsyncAvboitAccumulationComputeEmulationSharesGraphicsPacketTicketBetweenIntegrationAndComposite){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async AVBOIT Accumulation: no usable dedicated-compute headless Vulkan device on this host.";

    auto& graphics = asyncScope.graphics();
    auto& device = graphics.getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Async AVBOIT Accumulation: adapter has no dedicated compute-only queue family.";

    auto& timing = graphics.gpuTiming();
    auto& timingSink = asyncScope.gpuTimingSink();
    asyncScope.setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_AsyncAvboitAccumulationLifecycleScope.identity, device, 1u));
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
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        if(isVertexBuffer)
            description.setIsVertexBuffer(true);
        if(isConstantBuffer)
            description.setIsConstantBuffer(true);
        return device.createBuffer(description);
    };
    auto stateProbe = createWorkBuffer(
        Name("tests/descriptor_buffer/async_avboit_accumulation_state_probe"),
        false,
        true
    );
    auto integrationOutput = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_accumulation_integration_output"));
    auto materialStream = createWorkBuffer(Name("tests/descriptor_buffer/async_avboit_accumulation_material_stream"));
    auto generatedVertexA = createWorkBuffer(
        Name("tests/descriptor_buffer/async_avboit_accumulation_generated_vertex_a"),
        true
    );
    auto generatedVertexB = createWorkBuffer(
        Name("tests/descriptor_buffer/async_avboit_accumulation_generated_vertex_b"),
        true
    );
    const TextureDesc accumulationTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Unknown)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    auto accumColor = device.createTexture(accumulationTextureDesc);
    auto accumExtinction = device.createTexture(accumulationTextureDesc);
    const TextureDesc depthTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::D32S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    auto deferredDepth = device.createTexture(depthTextureDesc);
    ASSERT_TRUE(
        stateProbe
        && integrationOutput
        && materialStream
        && generatedVertexA
        && generatedVertexB
        && accumColor
        && accumExtinction
        && deferredDepth
    );
    Texture* const initialTextures[] = { deferredDepth.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

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
    const auto importTexture = [&graph](const TextureHandle& texture, const Name& identity, const AStringView markerLabel){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    const GpuGraphResourceId stateProbeResource = importBuffer(stateProbe, "AVBOIT Accumulation State Probe");
    const GpuGraphResourceId integrationOutputResource = importBuffer(
        integrationOutput,
        "AVBOIT Integration Output"
    );
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
        Name("tests/descriptor_buffer/async_avboit_accumulation_color"),
        "AVBOIT Accumulation Color"
    );
    const GpuGraphResourceId accumExtinctionResource = importTexture(
        accumExtinction,
        Name("tests/descriptor_buffer/async_avboit_accumulation_extinction"),
        "AVBOIT Accumulation Extinction"
    );
    const GpuGraphResourceId deferredDepthResource = importTexture(
        deferredDepth,
        Name("tests/descriptor_buffer/async_avboit_accumulation_depth"),
        "Deferred Depth"
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(integrationOutputResource.valid());
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
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Accumulation Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

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
    GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = GpuTaskCostHint::Medium;
    graphicsScheduling.overlapPreferred = false;
    graphicsScheduling.avoidQueueCrossing = true;
    graphicsScheduling.forceSubmissionBoundary = false;
    graphicsScheduling.allowPacketMerge = true;
    graphicsScheduling.mergeWithPrevious = true;
    graphicsScheduling.allowMergeAcrossConsumerFrontier = true;

    const GpuTaskResourceUse integrationUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = integrationOutputResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse streamUploadUses[] = {
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
            .resource = integrationOutputResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
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
            .resource = integrationOutputResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
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
    const GpuTaskResourceUse finalizeUses[] = {
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
    const GpuTaskResourceUse compositeUses[] = {
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

    GpuTimingSubmissionTicket accumulationTimingTicket(timing);
    Optional<GpuTimingMeasure> accumulationTiming;
    u32 recordOrdinal = 0u;
    bool integrationRecorded = false;
    bool streamUploadRecorded = false;
    bool producerRecorded = false;
    bool rasterRecorded = false;
    bool finalizerRecorded = false;
    bool compositeRecorded = false;
    bool accumulationTimingStarted = false;
    bool accumulationTimingFinished = false;
    QueueSubmissionToken integrationAcceptedToken;
    QueueSubmissionToken streamUploadAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken rasterAcceptedToken;
    QueueSubmissionToken finalizerAcceptedToken;
    QueueSubmissionToken compositeAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload integrationPayload;
    integrationPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    integrationPayload.expectations[1u] = { integrationOutput.get(), ResourceStates::UnorderedAccess };
    integrationPayload.expectationCount = 2u;
    integrationPayload.recordOrdinal = &recordOrdinal;
    integrationPayload.expectedOrdinal = 0u;
    integrationPayload.recorded = &integrationRecorded;
    integrationPayload.acceptedToken = &integrationAcceptedToken;
    const GpuTaskId integrationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_integration_task"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setResourceUses(integrationUses, LengthOf(integrationUses)),
        Move(integrationPayload)
    );
    ASSERT_TRUE(integrationTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload streamUploadPayload;
    streamUploadPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    streamUploadPayload.expectations[1u] = { materialStream.get(), ResourceStates::CopyDest };
    streamUploadPayload.expectationCount = 2u;
    streamUploadPayload.recordOrdinal = &recordOrdinal;
    streamUploadPayload.expectedOrdinal = 1u;
    streamUploadPayload.recorded = &streamUploadRecorded;
    streamUploadPayload.acceptedToken = &streamUploadAcceptedToken;
    const GpuTaskId streamUploadTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_material_upload_task"))
            .setMarkerLabel("AVBOIT Accumulation Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&integrationTask, 1u)
            .setResourceUses(streamUploadUses, LengthOf(streamUploadUses)),
        Move(streamUploadPayload)
    );
    ASSERT_TRUE(streamUploadTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    producerPayload.expectations[1u] = { integrationOutput.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[2u] = { materialStream.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[3u] = { generatedVertexA.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectations[4u] = { generatedVertexB.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 5u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 2u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &accumulationTimingTicket;
    producerPayload.sharedTiming = &accumulationTiming;
    producerPayload.timingScope = &s_AsyncAvboitAccumulationLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &accumulationTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Accumulation Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&streamUploadTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload rasterPayload;
    rasterPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    rasterPayload.expectations[1u] = { integrationOutput.get(), ResourceStates::ShaderResource };
    rasterPayload.expectations[2u] = { materialStream.get(), ResourceStates::ShaderResource };
    rasterPayload.expectations[3u] = { generatedVertexA.get(), ResourceStates::VertexBuffer };
    rasterPayload.expectations[4u] = { generatedVertexB.get(), ResourceStates::VertexBuffer };
    rasterPayload.expectationCount = 5u;
    rasterPayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::RenderTarget };
    rasterPayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::RenderTarget };
    rasterPayload.textureExpectations[2u] = { deferredDepth.get(), ResourceStates::DepthRead };
    rasterPayload.textureExpectationCount = 3u;
    rasterPayload.recordOrdinal = &recordOrdinal;
    rasterPayload.expectedOrdinal = 3u;
    rasterPayload.device = &device;
    rasterPayload.timing = &timing;
    rasterPayload.timingTicket = &accumulationTimingTicket;
    rasterPayload.sharedTiming = &accumulationTiming;
    rasterPayload.timingScope = &s_AsyncAvboitAccumulationLifecycleScope;
    rasterPayload.finishTiming = true;
    rasterPayload.timingFinished = &accumulationTimingFinished;
    rasterPayload.recorded = &rasterRecorded;
    rasterPayload.acceptedToken = &rasterAcceptedToken;
    const GpuTaskId rasterTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_raster_task"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
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
    finalizerPayload.recorded = &finalizerRecorded;
    finalizerPayload.acceptedToken = &finalizerAcceptedToken;
    const GpuTaskId finalizerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_finalize_task"))
            .setMarkerLabel("AVBOIT Accumulation Finalize")
            .setQueue(graphicsQueue)
            .setScheduling(graphicsScheduling)
            .setDependencies(&rasterTask, 1u)
            .setResourceUses(finalizeUses, LengthOf(finalizeUses)),
        Move(finalizerPayload)
    );
    ASSERT_TRUE(finalizerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload compositePayload;
    compositePayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    compositePayload.expectationCount = 1u;
    compositePayload.textureExpectations[0u] = { accumColor.get(), ResourceStates::ShaderResource };
    compositePayload.textureExpectations[1u] = { accumExtinction.get(), ResourceStates::ShaderResource };
    compositePayload.textureExpectationCount = 2u;
    compositePayload.recordOrdinal = &recordOrdinal;
    compositePayload.expectedOrdinal = 5u;
    compositePayload.recorded = &compositeRecorded;
    compositePayload.acceptedToken = &compositeAcceptedToken;
    const GpuTaskId compositeTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/async_avboit_accumulation_composite_task"))
            .setMarkerLabel("Deferred Composite")
            .setQueue(computeQueue)
            .setScheduling(computeScheduling)
            .setDependencies(&finalizerTask, 1u)
            .setResourceUses(compositeUses, LengthOf(compositeUses)),
        Move(compositePayload)
    );
    ASSERT_TRUE(compositeTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/async_avboit_accumulation_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(integrationTask, streamUploadTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterTask, finalizerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(finalizerTask, compositeTask));
    EXPECT_TRUE(analysis.hasInferredEdge(integrationTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterTask, finalizerTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 6u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], integrationTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], streamUploadTask);
    EXPECT_EQ(analysis.topologicalOrder()[2u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[3u], rasterTask);
    EXPECT_EQ(analysis.topologicalOrder()[4u], finalizerTask);
    EXPECT_EQ(analysis.topologicalOrder()[5u], compositeTask);

    const auto expectAssignment = [&](const GpuTaskId task, const GpuPhysicalQueueId queue, const CommandQueue::Enum queueClass){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, queue);
        EXPECT_EQ(assignment->queueClass, queueClass);
    };
    expectAssignment(integrationTask, primaryComputeQueue, CommandQueue::Compute);
    expectAssignment(streamUploadTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(producerTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(rasterTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(finalizerTask, primaryGraphicsQueue, CommandQueue::Graphics);
    expectAssignment(compositeTask, primaryComputeQueue, CommandQueue::Compute);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId integrationPacket = views.compiled.packetForTask(integrationTask);
    const GpuSubmissionPacketId accumulationPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId compositePacket = views.compiled.packetForTask(compositeTask);
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(accumulationPacket, views.compiled.packetForTask(streamUploadTask));
    EXPECT_EQ(accumulationPacket, views.compiled.packetForTask(rasterTask));
    EXPECT_EQ(accumulationPacket, views.compiled.packetForTask(finalizerTask));
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_NE(accumulationPacket, compositePacket);
    EXPECT_EQ(views.compiled.packetIdAt(0u), integrationPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), accumulationPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), compositePacket);
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(integrationTask, streamUploadTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(streamUploadTask, producerTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, rasterTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(rasterTask, finalizerTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(finalizerTask, compositeTask));
    const GpuCompiledPacketView accumulationPacketPlan = views.compiled.packet(accumulationPacket);
    ASSERT_TRUE(accumulationPacketPlan.valid());
    const GpuCompiledPacketView compositePacketPlan = views.compiled.packet(compositePacket);
    ASSERT_TRUE(compositePacketPlan.valid());
    EXPECT_EQ(accumulationPacketPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(compositePacketPlan.plan->queue, primaryComputeQueue);
    ASSERT_EQ(accumulationPacketPlan.plan->taskCount, 4u);
    const GpuTaskId* const accumulationPacketTasks = views.compiled.packet(accumulationPacket).tasks;
    ASSERT_NE(accumulationPacketTasks, nullptr);
    EXPECT_EQ(accumulationPacketTasks[0u], streamUploadTask);
    EXPECT_EQ(accumulationPacketTasks[1u], producerTask);
    EXPECT_EQ(accumulationPacketTasks[2u], rasterTask);
    EXPECT_EQ(accumulationPacketTasks[3u], finalizerTask);
    ASSERT_EQ(accumulationPacketPlan.plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(accumulationPacket).dependencies[0u].producer, integrationPacket);
    ASSERT_EQ(compositePacketPlan.plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(compositePacket).dependencies[0u].producer, accumulationPacket);

    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledRaster = views.compiled.findTask(rasterTask);
    const GpuCompiledTaskView compiledFinalizer = views.compiled.findTask(finalizerTask);
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledRaster.valid());
    ASSERT_TRUE(compiledFinalizer.valid());
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
    const auto hasTextureTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after, const GpuPhysicalQueueId sourceQueue, const GpuPhysicalQueueId destinationQueue){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
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
    EXPECT_TRUE(hasStateSeed(producerTask, integrationOutputResource, integrationPacket));
    EXPECT_TRUE(hasStateSeed(compositeTask, accumColorResource, accumulationPacket));
    EXPECT_TRUE(hasStateSeed(compositeTask, accumExtinctionResource, accumulationPacket));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        integrationOutputResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess,
        primaryComputeQueue,
        primaryComputeQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        streamUploadTask,
        materialStreamResource,
        ResourceStates::Common,
        ResourceStates::CopyDest,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        integrationOutputResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource,
        primaryComputeQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        materialStreamResource,
        ResourceStates::CopyDest,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    for(const GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producerTask,
            output,
            ResourceStates::Common,
            ResourceStates::UnorderedAccess,
            primaryGraphicsQueue,
            primaryGraphicsQueue
        ));
        EXPECT_TRUE(hasBufferTransition(
            rasterTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer,
            primaryGraphicsQueue,
            primaryGraphicsQueue
        ));
    }
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
        accumColorResource,
        ResourceStates::Unknown,
        ResourceStates::RenderTarget,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
        accumExtinctionResource,
        ResourceStates::Unknown,
        ResourceStates::RenderTarget,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterTask,
        deferredDepthResource,
        ResourceStates::Common,
        ResourceStates::DepthRead,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        accumColorResource,
        ResourceStates::RenderTarget,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        accumExtinctionResource,
        ResourceStates::RenderTarget,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));
    EXPECT_TRUE(hasTextureTransition(
        finalizerTask,
        deferredDepthResource,
        ResourceStates::DepthRead,
        ResourceStates::ShaderResource,
        primaryGraphicsQueue,
        primaryGraphicsQueue
    ));

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuSubmissionPacketRange packetRange = views.compiled.packetRange(integrationPacket, compositePacket);
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, 3u);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, 6u);
    EXPECT_TRUE(integrationRecorded);
    EXPECT_TRUE(streamUploadRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(rasterRecorded);
    EXPECT_TRUE(finalizerRecorded);
    EXPECT_TRUE(compositeRecorded);
    EXPECT_TRUE(accumulationTimingStarted);
    EXPECT_TRUE(accumulationTimingFinished);
    EXPECT_FALSE(accumulationTiming.has_value());
    CommandListResourceStateHandoff finalGraphicsStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        producerTask,
        finalGraphicsStateStorage
    ));
    const CommandListResourceStateHandoff* const finalGraphicsState = &finalGraphicsStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalGraphicsState);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumColor.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(accumExtinction.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(deferredDepth.get(), 0u, 0u), ResourceStates::ShaderResource);
    finalStateProbe->close();

    // Only the producer/raster pair spans the Accumulation timing measure; the finalizer shares the accepted
    // Graphics packet but must not claim the ticket after raster closes the timing interval.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = producerTask, .timingTicket = &accumulationTimingTicket },
        GpuTaskGraphTaskTimingTicket{ .task = rasterTask, .timingTicket = &accumulationTimingTicket },
    };
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        integrationTask,
        compositeTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken integrationPacketToken = transaction.packetToken(integrationPacket);
    const QueueSubmissionToken accumulationPacketToken = transaction.packetToken(accumulationPacket);
    const QueueSubmissionToken compositePacketToken = transaction.packetToken(compositePacket);
    ASSERT_TRUE(integrationPacketToken.valid());
    ASSERT_TRUE(accumulationPacketToken.valid());
    ASSERT_TRUE(compositePacketToken.valid());
    EXPECT_EQ(integrationPacketToken.queue, CommandQueue::Compute);
    EXPECT_EQ(accumulationPacketToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(compositePacketToken.queue, CommandQueue::Compute);
    EXPECT_EQ(integrationPacketToken.physicalQueueIndex, primaryComputeQueue.index);
    EXPECT_EQ(accumulationPacketToken.physicalQueueIndex, primaryGraphicsQueue.index);
    EXPECT_EQ(compositePacketToken.physicalQueueIndex, primaryComputeQueue.index);
    EXPECT_NE(integrationPacketToken.physicalQueueIndex, accumulationPacketToken.physicalQueueIndex);
    EXPECT_NE(accumulationPacketToken.physicalQueueIndex, compositePacketToken.physicalQueueIndex);
    const auto expectToken = [&](const QueueSubmissionToken& token, const QueueSubmissionToken& expected){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, expected.queue);
        EXPECT_EQ(token.value, expected.value);
        EXPECT_EQ(token.physicalQueueIndex, expected.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, expected.deviceGeneration);
    };
    expectToken(transaction.taskToken(views.compiled, integrationTask), integrationPacketToken);
    expectToken(transaction.taskToken(views.compiled, streamUploadTask), accumulationPacketToken);
    expectToken(transaction.taskToken(views.compiled, producerTask), accumulationPacketToken);
    expectToken(transaction.taskToken(views.compiled, rasterTask), accumulationPacketToken);
    expectToken(transaction.taskToken(views.compiled, finalizerTask), accumulationPacketToken);
    expectToken(transaction.taskToken(views.compiled, compositeTask), compositePacketToken);
    expectToken(integrationAcceptedToken, integrationPacketToken);
    expectToken(streamUploadAcceptedToken, accumulationPacketToken);
    expectToken(producerAcceptedToken, accumulationPacketToken);
    expectToken(rasterAcceptedToken, accumulationPacketToken);
    expectToken(finalizerAcceptedToken, accumulationPacketToken);
    expectToken(compositeAcceptedToken, compositePacketToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_AsyncAvboitAccumulationLifecycleScope.identity);
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

