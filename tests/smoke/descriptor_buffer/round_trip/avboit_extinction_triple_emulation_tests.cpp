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


// The regular unsplit Extinction plan may reuse one generated-vertex buffer for exactly three materials. The complete
// Depth Warp -> stream -> D(A) -> R(A) -> D(B) -> R(B) -> D(C) -> R(C) -> typed Integration -> Accumulation route
// must remain inside AVBOIT Pre's accepted Graphics packet, including each alternating shared-output barrier and ticket.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitExtinctionSharedOutputRegularTriplesStayInPrePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitExtinctionLifecycleScope.identity, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitIntegrationLifecycleScope.identity, device, 1u));
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
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_state_probe"),
        false,
        true
    );
    auto coverage = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_coverage")
    );
    auto depthWarp = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_depth_warp")
    );
    auto control = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_control")
    );
    auto materialStream = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_material_stream")
    );
    auto generatedVertex = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_generated_vertex"),
        true
    );
    const TextureDesc lowRasterDescription = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
    ;
    auto lowRaster = device.createTexture(lowRasterDescription);
    auto extinction = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_extinction")
    );
    auto extinctionOverflow = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_overflow")
    );
    auto transmittance = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_transmittance")
    );
    ASSERT_TRUE(
        stateProbe
        && coverage
        && depthWarp
        && control
        && materialStream
        && generatedVertex
        && lowRaster
        && extinction
        && extinctionOverflow
        && transmittance
    );
    Texture* const initialTextures[] = { lowRaster.get() };
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
        const TextureDesc& description = texture->getDescription();
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId stateProbeResource = importBuffer(
        stateProbe,
        "AVBOIT Extinction Shared Output State Probe"
    );
    const GpuGraphResourceId coverageResource = importBuffer(coverage, "AVBOIT Coverage");
    const GpuGraphResourceId depthWarpResource = importBuffer(depthWarp, "AVBOIT Depth Warp");
    const GpuGraphResourceId controlResource = importBuffer(control, "AVBOIT Control");
    const GpuGraphResourceId materialStreamResource = importBuffer(
        materialStream,
        "AVBOIT Extinction Material Stream"
    );
    const GpuGraphResourceId generatedVertexResource = importBuffer(
        generatedVertex,
        "AVBOIT Extinction Shared Generated Vertex"
    );
    const GpuGraphResourceId lowRasterResource = importTexture(
        lowRaster,
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_low_raster"),
        "AVBOIT Low Raster"
    );
    const GpuGraphResourceId extinctionResource = importBuffer(extinction, "AVBOIT Extinction");
    const GpuGraphResourceId extinctionOverflowResource = importBuffer(
        extinctionOverflow,
        "AVBOIT Extinction Overflow"
    );
    const GpuGraphResourceId transmittanceResource = importBuffer(transmittance, "AVBOIT Transmittance");
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(depthWarpResource.valid());
    ASSERT_TRUE(controlResource.valid());
    ASSERT_TRUE(materialStreamResource.valid());
    ASSERT_TRUE(generatedVertexResource.valid());
    ASSERT_TRUE(lowRasterResource.valid());
    ASSERT_TRUE(extinctionResource.valid());
    ASSERT_TRUE(extinctionOverflowResource.valid());
    ASSERT_TRUE(transmittanceResource.valid());

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
            .resource = lowRasterResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::ReadWrite,
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
    const GpuTaskResourceUse accumulationUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = transmittanceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> extinctionTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool occupancyRecorded = false;
    bool depthWarpRecorded = false;
    bool streamRecorded = false;
    bool sharedPhaseRecorded[6u] = {};
    bool integrationRecorded = false;
    bool accumulationRecorded = false;
    bool extinctionTimingStarted = false;
    bool extinctionTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken occupancyAcceptedToken;
    QueueSubmissionToken depthWarpAcceptedToken;
    QueueSubmissionToken streamAcceptedToken;
    QueueSubmissionToken sharedPhaseAcceptedTokens[6u] = {};
    QueueSubmissionToken integrationAcceptedToken;
    QueueSubmissionToken accumulationAcceptedToken;

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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_pre_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_occupancy_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_depth_warp_task"))
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
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_material_upload_task"))
            .setMarkerLabel("AVBOIT Extinction Material Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&depthWarpTask, 1u)
            .setResourceUses(streamUses, LengthOf(streamUses)),
        Move(streamPayload)
    );
    ASSERT_TRUE(streamTask.valid());

    const Name sharedPhaseIdentities[] = {
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_dispatch_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_raster_a_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_dispatch_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_raster_b_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_dispatch_c_task"),
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_raster_c_task"),
    };
    const AStringView sharedPhaseMarkers[] = {
        "AVBOIT Extinction Shared Compute Emulation Generate A",
        "AVBOIT Extinction Shared Compute Emulation Raster A",
        "AVBOIT Extinction Shared Compute Emulation Generate B",
        "AVBOIT Extinction Shared Compute Emulation Raster B",
        "AVBOIT Extinction Shared Compute Emulation Generate C",
        "AVBOIT Extinction Shared Compute Emulation Raster C",
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
        if(isRaster){
            phasePayload.expectations[2u] = { depthWarp.get(), ResourceStates::ShaderResource };
            phasePayload.expectations[3u] = { control.get(), ResourceStates::ShaderResource };
            phasePayload.expectations[4u] = { generatedVertex.get(), ResourceStates::VertexBuffer };
            phasePayload.expectations[5u] = { extinction.get(), ResourceStates::UnorderedAccess };
            phasePayload.expectations[6u] = { extinctionOverflow.get(), ResourceStates::UnorderedAccess };
            phasePayload.expectationCount = 7u;
            phasePayload.textureExpectations[0u] = { lowRaster.get(), ResourceStates::RenderTarget };
            phasePayload.textureExpectationCount = 1u;
        }
        else{
            phasePayload.expectations[2u] = { generatedVertex.get(), ResourceStates::UnorderedAccess };
            phasePayload.expectationCount = 3u;
        }
        phasePayload.recordOrdinal = &recordOrdinal;
        phasePayload.expectedOrdinal = static_cast<u32>(phaseIndex + 4u);
        phasePayload.timingTicket = &preTimingTicket;
        phasePayload.sharedTiming = &extinctionTiming;
        phasePayload.timingScope = &s_UnsplitAvboitExtinctionLifecycleScope;
        phasePayload.startTiming = phaseIndex == 0u;
        phasePayload.finishTiming = phaseIndex + 1u == LengthOf(sharedPhaseTasks);
        if(phasePayload.startTiming || phasePayload.finishTiming){
            phasePayload.device = &device;
            phasePayload.timing = &timing;
        }
        phasePayload.timingStarted = &extinctionTimingStarted;
        phasePayload.timingFinished = &extinctionTimingFinished;
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

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload integrationPayload;
    integrationPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    integrationPayload.expectations[1u] = { extinction.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[2u] = { control.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[3u] = { extinctionOverflow.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[4u] = { transmittance.get(), ResourceStates::UnorderedAccess };
    integrationPayload.expectationCount = 5u;
    integrationPayload.recordOrdinal = &recordOrdinal;
    integrationPayload.expectedOrdinal = 10u;
    integrationPayload.device = &device;
    integrationPayload.timing = &timing;
    integrationPayload.timingTicket = &preTimingTicket;
    integrationPayload.timingScope = &s_UnsplitAvboitIntegrationLifecycleScope;
    integrationPayload.recordTiming = true;
    integrationPayload.recorded = &integrationRecorded;
    integrationPayload.acceptedToken = &integrationAcceptedToken;
    const GpuTaskId integrationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_integration_task"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&rasterCTask, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses)),
        Move(integrationPayload)
    );
    ASSERT_TRUE(integrationTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload accumulationPayload;
    accumulationPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    accumulationPayload.expectations[1u] = { transmittance.get(), ResourceStates::ShaderResource };
    accumulationPayload.expectationCount = 2u;
    accumulationPayload.recordOrdinal = &recordOrdinal;
    accumulationPayload.expectedOrdinal = 11u;
    accumulationPayload.timingTicket = &preTimingTicket;
    accumulationPayload.recorded = &accumulationRecorded;
    accumulationPayload.acceptedToken = &accumulationAcceptedToken;
    const GpuTaskId accumulationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_accumulation_task"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&integrationTask, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses)),
        Move(accumulationPayload)
    );
    ASSERT_TRUE(accumulationTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_extinction_shared_output_triple_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, occupancyTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(depthWarpTask, streamTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(streamTask, dispatchATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(rasterCTask, integrationTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(integrationTask, accumulationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(occupancyTask, depthWarpTask));
    EXPECT_TRUE(analysis.hasInferredEdge(depthWarpTask, rasterATask));
    EXPECT_TRUE(analysis.hasInferredEdge(streamTask, dispatchATask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchATask, rasterATask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterATask, dispatchBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchBTask, rasterBTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterBTask, dispatchCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(dispatchCTask, rasterCTask));
    EXPECT_TRUE(analysis.hasInferredEdge(rasterCTask, integrationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(integrationTask, accumulationTask));
    const GpuTaskId tasks[] = {
        preTask,
        occupancyTask,
        depthWarpTask,
        streamTask,
        dispatchATask,
        rasterATask,
        dispatchBTask,
        rasterBTask,
        dispatchCTask,
        rasterCTask,
        integrationTask,
        accumulationTask,
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
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, accumulationTask));
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
        rasterATask,
        depthWarpResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterATask,
        controlResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTextureTransition(
        rasterATask,
        lowRasterResource,
        ResourceStates::Common,
        ResourceStates::RenderTarget
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterATask,
        extinctionResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        rasterATask,
        extinctionOverflowResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferUav(rasterBTask, extinctionResource));
    EXPECT_TRUE(hasBufferUav(rasterBTask, extinctionOverflowResource));
    EXPECT_TRUE(hasBufferUav(rasterCTask, extinctionResource));
    EXPECT_TRUE(hasBufferUav(rasterCTask, extinctionOverflowResource));
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
    EXPECT_TRUE(hasBufferTransition(
        accumulationTask,
        transmittanceResource,
        ResourceStates::UnorderedAccess,
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
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(occupancyRecorded);
    EXPECT_TRUE(depthWarpRecorded);
    EXPECT_TRUE(streamRecorded);
    EXPECT_TRUE(sharedPhaseRecorded[0u]);
    EXPECT_TRUE(sharedPhaseRecorded[1u]);
    EXPECT_TRUE(sharedPhaseRecorded[2u]);
    EXPECT_TRUE(sharedPhaseRecorded[3u]);
    EXPECT_TRUE(sharedPhaseRecorded[4u]);
    EXPECT_TRUE(sharedPhaseRecorded[5u]);
    EXPECT_TRUE(integrationRecorded);
    EXPECT_TRUE(accumulationRecorded);
    EXPECT_TRUE(extinctionTimingStarted);
    EXPECT_TRUE(extinctionTimingFinished);
    EXPECT_FALSE(extinctionTiming.has_value());
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(stateProbe.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(coverage.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalStateProbe->getBufferState(depthWarp.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(control.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(materialStream.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getTextureSubresourceState(lowRaster.get(), 0u, 0u), ResourceStates::RenderTarget);
    EXPECT_EQ(finalStateProbe->getBufferState(extinction.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(extinctionOverflow.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(transmittance.get()), ResourceStates::ShaderResource);
    finalStateProbe->close();

    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        accumulationTask,
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
    for(const QueueSubmissionToken& token : sharedPhaseAcceptedTokens)
        expectPacketToken(token);
    expectPacketToken(integrationAcceptedToken);
    expectPacketToken(accumulationAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto extinctionTimingStats = timingSink.stats(s_UnsplitAvboitExtinctionLifecycleScope.identity);
    ASSERT_TRUE(extinctionTimingStats.valid());
    EXPECT_EQ(extinctionTimingStats.sampleCount, 1u);
    const auto integrationTimingStats = timingSink.stats(s_UnsplitAvboitIntegrationLifecycleScope.identity);
    ASSERT_TRUE(integrationTimingStats.valid());
    EXPECT_EQ(integrationTimingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

