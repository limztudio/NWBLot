// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shadow Visibility's graph callback deliberately owns no entry-state bridge. This probe is the callback seam: it
// only observes graph-established descriptor-visible states before any renderer/native state call can run.
struct NativePacketShadowVisibilityEntryProbeTask{
    struct Payload{
        Buffer* currentBindlessSlots = nullptr;
        Buffer* materialContextSlots = nullptr;
        Buffer* sceneShading = nullptr;
        Buffer* lights = nullptr;
        Buffer* softwareMeshNodes = nullptr;
        Texture* worldPosition = nullptr;
        Texture* normal = nullptr;
        Texture* depth = nullptr;
        Texture* shadowVisibility = nullptr;
        Texture* shadowSoftHalfA = nullptr;
        Texture* shadowCoarseTransmittance = nullptr;
        Texture* shadowSoftGeometry = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.currentBindlessSlots
            || !payload.materialContextSlots
            || !payload.sceneShading
            || !payload.lights
            || !payload.softwareMeshNodes
            || !payload.worldPosition
            || !payload.normal
            || !payload.depth
            || !payload.shadowVisibility
            || !payload.shadowSoftHalfA
            || !payload.shadowCoarseTransmittance
            || !payload.shadowSoftGeometry
        )
            return false;
        const bool ready =
            commandList.getBufferState(payload.currentBindlessSlots) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.materialContextSlots) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.sceneShading) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.lights) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.softwareMeshNodes) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.normal, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.depth, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.shadowVisibility, 0u, 0u) == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.shadowSoftHalfA, 0u, 0u) == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.shadowCoarseTransmittance, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The AVBOIT occupancy material pass obtains depth and coverage through global descriptors. Its graph task must
// therefore see the clear's CopyDest -> UAV transition before it records, without a renderer thunk reissuing it;
// the following unsplit tail still owns the required UAV -> UAV ordering barrier.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAvboitOccupancyCoverageStateRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto coverage = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(coverage.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId coverageResource = graph.importBuffer(
        coverage,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_coverage"))
            .setMarkerLabel("AVBOIT Coverage")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(coverageResource.valid());

    const GpuQueueRequest graphicsQueue{
        // AVBOIT's clear stays on the primary Graphics transport, while the typed graph primitive advertises
        // the Transfer capability required by its native fill operation.
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_clear"))
        .setMarkerLabel("AVBOIT Clear")
        .setQueue(graphicsQueue)
        .setScheduling(clearScheduling)
    ;
    const GpuTaskId clearTask = graph.addClearBufferTask(
        clearDesc,
        GpuClearBufferTaskDesc{
            .destination = coverageResource,
            .clearValue = 0u,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = GpuTaskCostHint::Large;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse occupancyUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy"))
        .setMarkerLabel("AVBOIT Occupancy")
        .setQueue(graphicsQueue)
        .setScheduling(occupancyScheduling)
        .setDependencies(&clearTask, 1u)
        .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    ;
    bool occupancyRecorded = false;
    const GpuTaskId occupancyTask = graph.addTask<NativePacketPrefixTask>(
        occupancyDesc,
        NativePacketPrefixTask::Payload{
            // This probe deliberately performs no native transition or barrier; the packet prologue is the contract.
            .buffer = coverage.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &occupancyRecorded,
        }
    );
    ASSERT_TRUE(occupancyTask.valid());

    GpuTaskSchedulingHint tailScheduling;
    tailScheduling.cost = GpuTaskCostHint::Large;
    tailScheduling.forceSubmissionBoundary = false;
    tailScheduling.allowPacketMerge = true;
    tailScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse tailUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc tailDesc;
    tailDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_unsplit_tail"))
        .setMarkerLabel("AVBOIT Unsplit Tail")
        .setQueue(graphicsQueue)
        .setScheduling(tailScheduling)
        .setDependencies(&occupancyTask, 1u)
        .setResourceUses(tailUses, LengthOf(tailUses))
    ;
    bool tailRecorded = false;
    const GpuTaskId tailTask = graph.addTask<NativePacketPrefixTask>(
        tailDesc,
        NativePacketPrefixTask::Payload{
            .buffer = coverage.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &tailRecorded,
        }
    );
    ASSERT_TRUE(tailTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_occupancy_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(clearTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(occupancyTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(tailTask), packet);
    const GpuCompiledTaskView compiledOccupancy = views.compiled.findTask(occupancyTask);
    const GpuCompiledTaskView compiledTail = views.compiled.findTask(tailTask);
    ASSERT_TRUE(compiledOccupancy.valid());
    ASSERT_TRUE(compiledTail.valid());
    ASSERT_EQ(compiledOccupancy.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledTail.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const occupancyBarrier = views.compiled.findTask(occupancyTask).prologueBarriers;
    const GpuCompiledBarrier* const tailBarrier = views.compiled.findTask(tailTask).prologueBarriers;
    ASSERT_NE(occupancyBarrier, nullptr);
    ASSERT_NE(tailBarrier, nullptr);
    EXPECT_EQ(occupancyBarrier[0].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(occupancyBarrier[0].resource, coverageResource);
    EXPECT_EQ(occupancyBarrier[0].before, ResourceStates::CopyDest);
    EXPECT_EQ(occupancyBarrier[0].after, ResourceStates::UnorderedAccess);
    EXPECT_EQ(tailBarrier[0].type, GpuCompiledBarrierType::BufferUav);
    EXPECT_EQ(tailBarrier[0].resource, coverageResource);
    EXPECT_EQ(tailBarrier[0].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(tailBarrier[0].after, ResourceStates::UnorderedAccess);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &commandIrCapture
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(occupancyRecorded);
    EXPECT_TRUE(tailRecorded);
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const clearCapture = commandIrCapture.recordAt(0u);
    ASSERT_NE(clearCapture, nullptr);
    EXPECT_EQ(clearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(clearCapture->task, clearTask);
    EXPECT_EQ(clearCapture->packet, packet);
    EXPECT_EQ(clearCapture->destination, coverageResource);
    EXPECT_EQ(clearCapture->uintClearValue, UIntColor(0u));

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
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
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Shadow Visibility receives G-buffer and preflight state handoffs from earlier packets. Its record callback must
// see the descriptor-visible states before it performs any renderer-owned work; this probe deliberately contains
// only getters, so a missing graph prologue or state seed fails packet recording.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedShadowVisibilityEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeConstantBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeStorageBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeGbufferTarget = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Unknown)
        );
    };
    const auto makeShadowTarget = [&device](const ResourceStates::Mask initialState = ResourceStates::Common){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(initialState)
        );
    };
    auto currentBindlessSlots = makeConstantBuffer();
    auto materialContextSlots = makeConstantBuffer();
    auto sceneShading = makeConstantBuffer();
    auto lights = makeStorageBuffer();
    auto softwareMeshNodes = makeStorageBuffer();
    auto worldPosition = makeGbufferTarget();
    auto normal = makeGbufferTarget();
    auto depth = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::D32S8)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Unknown)
    );
    auto shadowVisibility = makeShadowTarget(ResourceStates::Unknown);
    auto shadowSoftHalfA = makeShadowTarget();
    auto shadowCoarseTransmittance = makeShadowTarget();
    auto shadowSoftGeometry = makeShadowTarget();
    ASSERT_NE(currentBindlessSlots.get(), nullptr);
    ASSERT_NE(materialContextSlots.get(), nullptr);
    ASSERT_NE(sceneShading.get(), nullptr);
    ASSERT_NE(lights.get(), nullptr);
    ASSERT_NE(softwareMeshNodes.get(), nullptr);
    ASSERT_NE(worldPosition.get(), nullptr);
    ASSERT_NE(normal.get(), nullptr);
    ASSERT_NE(depth.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(shadowSoftHalfA.get(), nullptr);
    ASSERT_NE(shadowCoarseTransmittance.get(), nullptr);
    ASSERT_NE(shadowSoftGeometry.get(), nullptr);
    Texture* const initialTextures[] = {
        shadowSoftHalfA.get(),
        shadowCoarseTransmittance.get(),
        shadowSoftGeometry.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](
        const BufferHandle& buffer,
        const Name identity,
        const AStringView label
    ){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const auto importTexture = [&graph](
        const TextureHandle& texture,
        const Name identity,
        const AStringView label
    ){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    const GpuGraphResourceId currentBindlessSlotsResource = importBuffer(
        currentBindlessSlots,
        Name("tests/descriptor_buffer/shadow_visibility_bindless_slots"),
        "Deferred Bindless Slots"
    );
    const GpuGraphResourceId materialContextSlotsResource = importBuffer(
        materialContextSlots,
        Name("tests/descriptor_buffer/shadow_visibility_material_context_slots"),
        "Ray-Trace Material Context Slots"
    );
    const GpuGraphResourceId sceneShadingResource = importBuffer(
        sceneShading,
        Name("tests/descriptor_buffer/shadow_visibility_scene_shading"),
        "Scene Shading"
    );
    const GpuGraphResourceId lightsResource = importBuffer(
        lights,
        Name("tests/descriptor_buffer/shadow_visibility_lights"),
        "Lights"
    );
    const GpuGraphResourceId softwareMeshNodesResource = importBuffer(
        softwareMeshNodes,
        Name("tests/descriptor_buffer/shadow_visibility_software_mesh_nodes"),
        "Software Shadow Mesh Nodes"
    );
    const GpuGraphResourceId worldPositionResource = importTexture(
        worldPosition,
        Name("tests/descriptor_buffer/shadow_visibility_world_position"),
        "World Position"
    );
    const GpuGraphResourceId normalResource = importTexture(
        normal,
        Name("tests/descriptor_buffer/shadow_visibility_normal"),
        "Normal"
    );
    const GpuGraphResourceId depthResource = importTexture(
        depth,
        Name("tests/descriptor_buffer/shadow_visibility_depth"),
        "Depth"
    );
    const GpuGraphResourceId shadowVisibilityResource = importTexture(
        shadowVisibility,
        Name("tests/descriptor_buffer/shadow_visibility_output"),
        "Shadow Visibility"
    );
    const GpuGraphResourceId shadowSoftHalfAResource = importTexture(
        shadowSoftHalfA,
        Name("tests/descriptor_buffer/shadow_visibility_soft_half_a"),
        "Shadow Soft Half A"
    );
    const GpuGraphResourceId shadowCoarseTransmittanceResource = importTexture(
        shadowCoarseTransmittance,
        Name("tests/descriptor_buffer/shadow_visibility_coarse_transmittance"),
        "Shadow Coarse Transmittance"
    );
    const GpuGraphResourceId shadowSoftGeometryResource = importTexture(
        shadowSoftGeometry,
        Name("tests/descriptor_buffer/shadow_visibility_soft_geometry"),
        "Shadow Soft Geometry"
    );
    ASSERT_TRUE(currentBindlessSlotsResource.valid());
    ASSERT_TRUE(materialContextSlotsResource.valid());
    ASSERT_TRUE(sceneShadingResource.valid());
    ASSERT_TRUE(lightsResource.valid());
    ASSERT_TRUE(softwareMeshNodesResource.valid());
    ASSERT_TRUE(worldPositionResource.valid());
    ASSERT_TRUE(normalResource.valid());
    ASSERT_TRUE(depthResource.valid());
    ASSERT_TRUE(shadowVisibilityResource.valid());
    ASSERT_TRUE(shadowSoftHalfAResource.valid());
    ASSERT_TRUE(shadowCoarseTransmittanceResource.valid());
    ASSERT_TRUE(shadowSoftGeometryResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        true,
    };
    GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = GpuTaskCostHint::Medium;
    prepareScheduling.forceSubmissionBoundary = true;
    prepareScheduling.allowPacketMerge = false;
    const GpuTaskResourceUse prepareUses[] = {
        GpuTaskResourceUse{
            .resource = currentBindlessSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = materialContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_visibility_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsQueue)
        .setScheduling(prepareScheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    bool prepareAttempted = false;
    bool prepareShouldRecord = true;
    const GpuTaskId prepareTask = graph.addTask<NativePacketCaptureRetryTask>(
        prepareDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &prepareShouldRecord,
            .attempted = &prepareAttempted,
        }
    );
    ASSERT_TRUE(prepareTask.valid());

    const GpuTaskResourceUse prefixUses[] = {
        GpuTaskResourceUse{
            .resource = worldPositionResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = normalResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = depthResource,
            .range = {},
            .requiredState = ResourceStates::DepthWrite,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = sceneShadingResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = lightsResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint prefixScheduling = prepareScheduling;
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_visibility_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(prefixScheduling)
        .setDependencies(&prepareTask, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    bool prefixAttempted = false;
    bool prefixShouldRecord = true;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        prefixDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &prefixShouldRecord,
            .attempted = &prefixAttempted,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    const GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shadow_visibility_trace_geometry"))
            .setMarkerLabel("Shadow Visibility Trace Geometry")
            .setMembers(&softwareMeshNodesResource, 1u)
    );
    ASSERT_TRUE(softwareTraceGeometrySet.valid());
    const GpuTaskResourceSetUse softwareTraceGeometrySetUses[] = {
        {
            .resourceSet = softwareTraceGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse shadowUses[] = {
        GpuTaskResourceUse{
            .resource = worldPositionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = normalResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = depthResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = currentBindlessSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = sceneShadingResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = lightsResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = shadowSoftHalfAResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = shadowCoarseTransmittanceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = shadowSoftGeometryResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = GpuTaskCostHint::Large;
    shadowScheduling.forceSubmissionBoundary = true;
    shadowScheduling.allowPacketMerge = false;
    GpuTaskDesc shadowDesc;
    shadowDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeQueue)
        .setScheduling(shadowScheduling)
        .setDependencies(&prefixTask, 1u)
        .setResourceUses(shadowUses, LengthOf(shadowUses))
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    bool shadowRecorded = false;
    const GpuTaskId shadowTask = graph.addTask<NativePacketShadowVisibilityEntryProbeTask>(
        shadowDesc,
        NativePacketShadowVisibilityEntryProbeTask::Payload{
            .currentBindlessSlots = currentBindlessSlots.get(),
            .materialContextSlots = materialContextSlots.get(),
            .sceneShading = sceneShading.get(),
            .lights = lights.get(),
            .softwareMeshNodes = softwareMeshNodes.get(),
            .worldPosition = worldPosition.get(),
            .normal = normal.get(),
            .depth = depth.get(),
            .shadowVisibility = shadowVisibility.get(),
            .shadowSoftHalfA = shadowSoftHalfA.get(),
            .shadowCoarseTransmittance = shadowCoarseTransmittance.get(),
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .recorded = &shadowRecorded,
        }
    );
    ASSERT_TRUE(shadowTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = lightsResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint lightingScheduling = shadowScheduling;
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_shadow_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeQueue)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadowTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketPrefixTask>(
        lightingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = lights.get(),
            .expectedState = ResourceStates::ShaderResource,
            .texture = shadowVisibility.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shadow_visibility_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 4u);
    const GpuSubmissionPacketId preparePacket = views.compiled.packetForTask(prepareTask);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId shadowPacket = views.compiled.packetForTask(shadowTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    ASSERT_TRUE(preparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(preparePacket, prefixPacket);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_NE(shadowPacket, lightingPacket);
    const GpuCompiledTaskView compiledShadow = views.compiled.findTask(shadowTask);
    ASSERT_TRUE(compiledShadow.valid());
    const GpuCompiledBarrier* const shadowBarriers = views.compiled.findTask(shadowTask).prologueBarriers;
    ASSERT_NE(shadowBarriers, nullptr);
    bool transitionsDepthToShaderResource = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledShadow.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
        transitionsDepthToShaderResource = transitionsDepthToShaderResource || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == depthResource
            && barrier.before == ResourceStates::DepthWrite
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(transitionsDepthToShaderResource);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(prepareAttempted);
    EXPECT_TRUE(prefixAttempted);
    EXPECT_TRUE(shadowRecorded);
    EXPECT_TRUE(lightingRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
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
    EXPECT_TRUE(transaction.packetToken(lightingPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

