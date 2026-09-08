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


// The normal AVBOIT route keeps the repaired Extinction path entirely in the AVBOIT Pre packet.  Model the
// regular alias-free case explicitly: Occupancy's coverage UAV feeds a separate Depth Warp task, the immutable
// Extinction stream feeds two distinct generated-vertex UAVs, raster consumes both as vertex buffers, and
// Integration receives Extinction's outputs.  One Pre ticket must cover the whole accepted Graphics packet.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitExtinctionAliasFreeComputeEmulationStaysInPrePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitExtinctionLifecycleScope.identity, device, 1u));
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
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_state_probe"),
        false,
        true
    );
    auto materialStream = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_material_stream"));
    auto coverage = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_coverage"));
    auto depthWarp = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_depth_warp"));
    auto control = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_control"));
    auto generatedVertexA = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_generated_vertex_a"),
        true
    );
    auto generatedVertexB = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_generated_vertex_b"),
        true
    );
    auto extinction = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_output"));
    auto extinctionOverflow = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_overflow"));
    auto transmittance = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_transmittance"));
    ASSERT_TRUE(
        stateProbe
        && materialStream
        && coverage
        && depthWarp
        && control
        && generatedVertexA
        && generatedVertexB
        && extinction
        && extinctionOverflow
        && transmittance
    );
    EXPECT_NE(generatedVertexA.get(), generatedVertexB.get());

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
    const GpuGraphResourceId stateProbeResource = importBuffer(stateProbe, "AVBOIT Extinction State Probe");
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Extinction Material Stream"
    );
    const GpuGraphResourceId coverageResource = importBuffer(coverage, "AVBOIT Coverage");
    const GpuGraphResourceId depthWarpResource = importBuffer(depthWarp, "AVBOIT Depth Warp");
    const GpuGraphResourceId controlResource = importBuffer(control, "AVBOIT Control");
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "AVBOIT Extinction Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "AVBOIT Extinction Generated Vertex B"
    );
    const GpuGraphResourceId extinctionResource = importBuffer(extinction, "AVBOIT Extinction");
    const GpuGraphResourceId extinctionOverflowResource = importBuffer(
        extinctionOverflow,
        "AVBOIT Extinction Overflow"
    );
    const GpuGraphResourceId transmittanceResource = importBuffer(transmittance, "AVBOIT Transmittance");
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(depthWarpResource.valid());
    ASSERT_TRUE(controlResource.valid());
    ASSERT_TRUE(generatedVertexAResource.valid());
    ASSERT_TRUE(generatedVertexBResource.valid());
    ASSERT_TRUE(extinctionResource.valid());
    ASSERT_TRUE(extinctionOverflowResource.valid());
    ASSERT_TRUE(transmittanceResource.valid());
    EXPECT_NE(generatedVertexAResource, generatedVertexBResource);

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Extinction Generated Vertex Outputs")
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
    const GpuTaskResourceUse occupancyUses[] = {
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
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
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
    const GpuTaskResourceUse extinctionUses[] = {
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
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
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
        GpuTaskResourceUse{
            .resource = transmittanceResource,
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
    const GpuTaskResourceSetUse extinctionGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> extinctionTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool occupancyRecorded = false;
    bool depthWarpRecorded = false;
    bool streamRecorded = false;
    bool producerRecorded = false;
    bool extinctionRecorded = false;
    bool integrationRecorded = false;
    bool extinctionTimingStarted = false;
    bool extinctionTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken occupancyAcceptedToken;
    QueueSubmissionToken depthWarpAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken extinctionAcceptedToken;
    QueueSubmissionToken integrationAcceptedToken;

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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_pre_task"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses)),
        Move(prePayload)
    );
    ASSERT_TRUE(preTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload occupancyPayload;
    occupancyPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    occupancyPayload.expectations[1u] = { coverage.get(), ResourceStates::UnorderedAccess };
    occupancyPayload.expectationCount = 2u;
    occupancyPayload.recordOrdinal = &recordOrdinal;
    occupancyPayload.expectedOrdinal = 1u;
    occupancyPayload.timingTicket = &preTimingTicket;
    occupancyPayload.recorded = &occupancyRecorded;
    occupancyPayload.acceptedToken = &occupancyAcceptedToken;
    const GpuTaskId occupancyTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_occupancy_task"))
            .setMarkerLabel("AVBOIT Occupancy")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(occupancyUses, LengthOf(occupancyUses)),
        Move(occupancyPayload)
    );
    ASSERT_TRUE(occupancyTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload depthWarpPayload;
    depthWarpPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    depthWarpPayload.expectations[1u] = { coverage.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectations[2u] = { depthWarp.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectations[3u] = { control.get(), ResourceStates::UnorderedAccess };
    depthWarpPayload.expectationCount = 4u;
    depthWarpPayload.recordOrdinal = &recordOrdinal;
    depthWarpPayload.expectedOrdinal = 2u;
    depthWarpPayload.timingTicket = &preTimingTicket;
    depthWarpPayload.recorded = &depthWarpRecorded;
    depthWarpPayload.acceptedToken = &depthWarpAcceptedToken;
    const GpuTaskId depthWarpTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_depth_warp_task"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&occupancyTask, 1u)
            .setResourceUses(depthWarpUses, LengthOf(depthWarpUses)),
        Move(depthWarpPayload)
    );
    ASSERT_TRUE(depthWarpTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload streamPayload;
    streamPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    streamPayload.expectations[1u] = { materialStream.get(), ResourceStates::CopyDest };
    streamPayload.expectationCount = 2u;
    streamPayload.recordOrdinal = &recordOrdinal;
    streamPayload.expectedOrdinal = 3u;
    streamPayload.timingTicket = &preTimingTicket;
    streamPayload.recorded = &streamRecorded;
    streamPayload.acceptedToken = &streamAcceptedToken;
    const GpuTaskId streamTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_material_upload_task"))
            .setMarkerLabel("AVBOIT Extinction Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&depthWarpTask, 1u)
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
    producerPayload.expectedOrdinal = 4u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &preTimingTicket;
    producerPayload.sharedTiming = &extinctionTiming;
    producerPayload.timingScope = &s_UnsplitAvboitExtinctionLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &extinctionTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Extinction Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&streamTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload extinctionPayload;
    extinctionPayload.expectations[0u] = { depthWarp.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[1u] = { control.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[2u] = { generatedVertexA.get(), ResourceStates::VertexBuffer };
    extinctionPayload.expectations[3u] = { generatedVertexB.get(), ResourceStates::VertexBuffer };
    extinctionPayload.expectations[4u] = { extinction.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectationCount = 5u;
    extinctionPayload.recordOrdinal = &recordOrdinal;
    extinctionPayload.expectedOrdinal = 5u;
    extinctionPayload.device = &device;
    extinctionPayload.timing = &timing;
    extinctionPayload.timingTicket = &preTimingTicket;
    extinctionPayload.sharedTiming = &extinctionTiming;
    extinctionPayload.timingScope = &s_UnsplitAvboitExtinctionLifecycleScope;
    extinctionPayload.finishTiming = true;
    extinctionPayload.timingFinished = &extinctionTimingFinished;
    extinctionPayload.recorded = &extinctionRecorded;
    extinctionPayload.acceptedToken = &extinctionAcceptedToken;
    const GpuTaskId extinctionTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_raster_task"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses))
            .setResourceSetUses(&extinctionGeneratedVertexUse, 1u),
        Move(extinctionPayload)
    );
    ASSERT_TRUE(extinctionTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload integrationPayload;
    integrationPayload.expectations[0u] = { extinction.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[1u] = { control.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[2u] = { extinctionOverflow.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[3u] = { transmittance.get(), ResourceStates::UnorderedAccess };
    integrationPayload.expectationCount = 4u;
    integrationPayload.recordOrdinal = &recordOrdinal;
    integrationPayload.expectedOrdinal = 6u;
    integrationPayload.timingTicket = &preTimingTicket;
    integrationPayload.recorded = &integrationRecorded;
    integrationPayload.acceptedToken = &integrationAcceptedToken;
    const GpuTaskId integrationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_integration_task"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&extinctionTask, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses)),
        Move(integrationPayload)
    );
    ASSERT_TRUE(integrationTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_extinction_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, occupancyTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasInferredEdge(depthWarpTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(extinctionTask, integrationTask));
    const GpuTaskId tasks[] = {
        preTask,
        occupancyTask,
        depthWarpTask,
        streamTask,
        producerTask,
        extinctionTask,
        integrationTask,
    };
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);

    const auto expectAssignment = [&](const GpuTaskId task){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    };
    for(const GpuTaskId task : tasks)
        expectAssignment(task);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    for(const GpuTaskId task : tasks)
        EXPECT_EQ(packet, views.compiled.packetForTask(task));
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, integrationTask));
    for(usize taskIndex = 0u; taskIndex + 1u < LengthOf(tasks); ++taskIndex)
        EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(tasks[taskIndex], tasks[taskIndex + 1u]));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, LengthOf(tasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);

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
        occupancyTask,
        coverageResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(depthWarpTask, coverageResource));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        depthWarpResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        depthWarpTask,
        controlResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
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
            extinctionTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        depthWarpResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        controlResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionOverflowResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflowResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        transmittanceResource,
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
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(occupancyRecorded);
    EXPECT_TRUE(depthWarpRecorded);
    EXPECT_TRUE(streamRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(extinctionRecorded);
    EXPECT_TRUE(integrationRecorded);
    EXPECT_TRUE(extinctionTimingStarted);
    EXPECT_TRUE(extinctionTimingFinished);
    EXPECT_FALSE(extinctionTiming.has_value());
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(materialStream.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(coverage.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalStateProbe->getBufferState(depthWarp.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(control.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(extinction.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(extinctionOverflow.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(transmittance.get()), ResourceStates::UnorderedAccess);
    finalStateProbe->close();

    // AVBOIT's non-split submit path publishes only Pre.  Resolving that one binding must accept the complete
    // seven-task packet, the producer/raster timing span, and every callback with its single Graphics token.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        integrationTask,
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
    for(const GpuTaskId task : tasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(occupancyAcceptedToken);
    expectPacketToken(depthWarpAcceptedToken);
    expectPacketToken(streamAcceptedToken);
    expectPacketToken(producerAcceptedToken);
    expectPacketToken(extinctionAcceptedToken);
    expectPacketToken(integrationAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_UnsplitAvboitExtinctionLifecycleScope.identity);
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

