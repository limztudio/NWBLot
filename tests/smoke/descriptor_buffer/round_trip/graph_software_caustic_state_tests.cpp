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


// The opaque temporal merge has the same selected history/moment contract before it starts its local geometry
// handoff. Its stable previous-geometry and world-position reads share that graph-owned prologue. This callback
// only reads state tracking, so it cannot mask a missing graph-owned entry transition.
struct NativePacketSoftOpaqueTemporalMergeProbeTask{
    struct Payload{
        Texture* historyInput = nullptr;
        Texture* momentsInput = nullptr;
        Texture* historyOutput = nullptr;
        Texture* momentsOutput = nullptr;
        Texture* previousGeometry = nullptr;
        Texture* worldPosition = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.historyInput
            && payload.momentsInput
            && payload.historyOutput
            && payload.momentsOutput
            && payload.previousGeometry
            && payload.worldPosition
            && commandList.getTextureSubresourceState(payload.historyInput, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.momentsInput, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.historyOutput, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.momentsOutput, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.previousGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Software caustics follows Shadow Visibility and uses the same descriptor-selected geometry plus its own
// emission/mesh-view inputs. Keep this callback getter-only so a missing graph state seed cannot be hidden by
// renderer-native setup. The graph-owned irradiance and fresh-accumulator clears must hand off CopyDest to UAV
// before this callback; warm temporal decay has its own preceding graph task. The graph also provides the
// geometry-downsample's world/depth SRV and geometry-cache UAV entry states; only subsequent resolve handoffs stay
// task-local.
struct NativePacketSoftwareCausticsEntryProbeTask{
    static constexpr u32 s_ShaderBufferCount = 11u;
    static constexpr u32 s_ConstantBufferCount = 4u;
    static constexpr u32 s_ShaderTextureCount = 2u;
    static constexpr u32 s_UavTextureCount = 3u;

    struct Payload{
        Buffer* shaderBuffers[s_ShaderBufferCount] = {};
        Buffer* constantBuffers[s_ConstantBufferCount] = {};
        Texture* shaderTextures[s_ShaderTextureCount] = {};
        Texture* uavTextures[s_UavTextureCount] = {};
        Texture* irradiance = nullptr;
        Texture* accumulator = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        bool ready = true;
        for(u32 bufferIndex = 0u; bufferIndex < s_ShaderBufferCount; ++bufferIndex){
            ready = ready
                && payload.shaderBuffers[bufferIndex]
                && commandList.getBufferState(payload.shaderBuffers[bufferIndex]) == ResourceStates::ShaderResource
            ;
        }
        for(u32 bufferIndex = 0u; bufferIndex < s_ConstantBufferCount; ++bufferIndex){
            ready = ready
                && payload.constantBuffers[bufferIndex]
                && commandList.getBufferState(payload.constantBuffers[bufferIndex]) == ResourceStates::ConstantBuffer
            ;
        }
        for(u32 textureIndex = 0u; textureIndex < s_ShaderTextureCount; ++textureIndex){
            ready = ready
                && payload.shaderTextures[textureIndex]
                && commandList.getTextureSubresourceState(payload.shaderTextures[textureIndex], 0u, 0u)
                    == ResourceStates::ShaderResource
            ;
        }
        for(u32 textureIndex = 0u; textureIndex < s_UavTextureCount; ++textureIndex){
            ready = ready
                && payload.uavTextures[textureIndex]
                && commandList.getTextureSubresourceState(payload.uavTextures[textureIndex], 0u, 0u)
                    == ResourceStates::UnorderedAccess
            ;
        }
        ready = ready
            && payload.irradiance
            && commandList.getTextureSubresourceState(payload.irradiance, 0u, 0u) == ResourceStates::UnorderedAccess
            && payload.accumulator
            && commandList.getTextureSubresourceState(payload.accumulator, 0u, 0u) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The opaque soft merge freezes its own A/B selector in the graph. Its history/moment and stable-read entry states
// are independent of the geometry downsample-to-merge handoff that remains inside the renderer callback.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedOpaqueSoftTemporalMergeEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeShadowTarget = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto historyA = makeShadowTarget();
    auto momentsA = makeShadowTarget();
    auto historyB = makeShadowTarget();
    auto momentsB = makeShadowTarget();
    auto previousGeometry = makeShadowTarget();
    auto worldPosition = makeShadowTarget();
    ASSERT_NE(historyA.get(), nullptr);
    ASSERT_NE(momentsA.get(), nullptr);
    ASSERT_NE(historyB.get(), nullptr);
    ASSERT_NE(momentsB.get(), nullptr);
    ASSERT_NE(previousGeometry.get(), nullptr);
    ASSERT_NE(worldPosition.get(), nullptr);
    Texture* const initialTextures[] = {
        historyA.get(),
        momentsA.get(),
        historyB.get(),
        momentsB.get(),
        previousGeometry.get(),
        worldPosition.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importTexture = [&graph](const TextureHandle& texture, const Name identity, const AStringView label){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    const GpuGraphResourceId historyAResource = importTexture(
        historyA,
        Name("tests/descriptor_buffer/opaque_soft_history_a"),
        "Opaque Soft History A"
    );
    const GpuGraphResourceId momentsAResource = importTexture(
        momentsA,
        Name("tests/descriptor_buffer/opaque_soft_moments_a"),
        "Opaque Soft Moments A"
    );
    const GpuGraphResourceId historyBResource = importTexture(
        historyB,
        Name("tests/descriptor_buffer/opaque_soft_history_b"),
        "Opaque Soft History B"
    );
    const GpuGraphResourceId momentsBResource = importTexture(
        momentsB,
        Name("tests/descriptor_buffer/opaque_soft_moments_b"),
        "Opaque Soft Moments B"
    );
    const GpuGraphResourceId previousGeometryResource = importTexture(
        previousGeometry,
        Name("tests/descriptor_buffer/opaque_soft_geometry_previous"),
        "Previous Opaque Soft Geometry"
    );
    const GpuGraphResourceId worldPositionResource = importTexture(
        worldPosition,
        Name("tests/descriptor_buffer/opaque_soft_world_position"),
        "Opaque Soft World Position"
    );
    ASSERT_TRUE(historyAResource.valid());
    ASSERT_TRUE(momentsAResource.valid());
    ASSERT_TRUE(historyBResource.valid());
    ASSERT_TRUE(momentsBResource.valid());
    ASSERT_TRUE(previousGeometryResource.valid());
    ASSERT_TRUE(worldPositionResource.valid());

    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        false,
    };
    const GpuTaskResourceUse mergeUses[] = {
        {
            .resource = historyBResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = momentsBResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = previousGeometryResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPositionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = historyAResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = momentsAResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc mergeDesc;
    mergeDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_opaque_soft_temporal_merge"))
        .setMarkerLabel("Opaque Soft Temporal Merge")
        .setQueue(computeQueue)
        .setResourceUses(mergeUses, LengthOf(mergeUses))
    ;
    bool mergeRecorded = false;
    const GpuTaskId mergeTask = graph.addTask<NativePacketSoftOpaqueTemporalMergeProbeTask>(
        mergeDesc,
        NativePacketSoftOpaqueTemporalMergeProbeTask::Payload{
            .historyInput = historyB.get(),
            .momentsInput = momentsB.get(),
            .historyOutput = historyA.get(),
            .momentsOutput = momentsA.get(),
            .previousGeometry = previousGeometry.get(),
            .worldPosition = worldPosition.get(),
            .recorded = &mergeRecorded,
        }
    );
    ASSERT_TRUE(mergeTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/opaque_soft_temporal_merge_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledMerge = views.compiled.findTask(mergeTask);
    ASSERT_TRUE(compiledMerge.valid());
    const GpuCompiledBarrier* const mergeBarriers = views.compiled.findTask(mergeTask).prologueBarriers;
    ASSERT_NE(mergeBarriers, nullptr);
    const auto hasMergeInputTransition = [&](const GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledMerge.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = mergeBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::Common
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasMergeInputTransition(historyBResource));
    EXPECT_TRUE(hasMergeInputTransition(momentsBResource));
    EXPECT_TRUE(hasMergeInputTransition(previousGeometryResource));
    EXPECT_TRUE(hasMergeInputTransition(worldPositionResource));

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
    EXPECT_TRUE(mergeRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
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
    EXPECT_TRUE(transaction.packetToken(views.compiled.packetForTask(mergeTask)).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The shared deferred path supplies all software-caustics descriptor inputs before the renderer begins its local
// clear/temporal/resolve sequence. This packet chain uses getters only at the caustics callback seam, making the
// graph prologue and cross-packet state handoffs observable without any renderer-owned transition masking them.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSoftwareCausticsEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 shaderBufferCount = NativePacketSoftwareCausticsEntryProbeTask::s_ShaderBufferCount;
    constexpr u32 constantBufferCount = NativePacketSoftwareCausticsEntryProbeTask::s_ConstantBufferCount;
    constexpr u32 shaderTextureCount = NativePacketSoftwareCausticsEntryProbeTask::s_ShaderTextureCount;
    constexpr u32 uavTextureCount = NativePacketSoftwareCausticsEntryProbeTask::s_UavTextureCount;
    const auto makeStorageBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeConstantBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeUavTexture = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    BufferHandle shaderBuffers[shaderBufferCount];
    BufferHandle constantBuffers[constantBufferCount];
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        shaderBuffers[bufferIndex] = makeStorageBuffer();
        ASSERT_NE(shaderBuffers[bufferIndex].get(), nullptr);
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex){
        constantBuffers[bufferIndex] = makeConstantBuffer();
        ASSERT_NE(constantBuffers[bufferIndex].get(), nullptr);
    }
    TextureHandle shaderTextures[shaderTextureCount];
    shaderTextures[0u] = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Unknown)
    );
    shaderTextures[1u] = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::D32S8)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Unknown)
    );
    TextureHandle uavTextures[uavTextureCount];
    for(u32 textureIndex = 0u; textureIndex < uavTextureCount; ++textureIndex)
        uavTextures[textureIndex] = makeUavTexture();
    auto causticIrradiance = makeUavTexture();
    auto causticAccumulator = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    for(u32 textureIndex = 0u; textureIndex < shaderTextureCount; ++textureIndex)
        ASSERT_NE(shaderTextures[textureIndex].get(), nullptr);
    for(u32 textureIndex = 0u; textureIndex < uavTextureCount; ++textureIndex)
        ASSERT_NE(uavTextures[textureIndex].get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(causticAccumulator.get(), nullptr);
    Texture* const initialTextures[] = {
        uavTextures[0u].get(),
        uavTextures[1u].get(),
        uavTextures[2u].get(),
        causticIrradiance.get(),
        causticAccumulator.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const auto importTexture = [&graph](const TextureHandle& texture, const Name identity, const AStringView label){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    struct BufferImportDesc{
        Name identity;
        AStringView label;
    };
    const BufferImportDesc shaderBufferImports[shaderBufferCount] = {
        { Name("tests/descriptor_buffer/software_caustics_emission_targets"), "Caustic Emission Targets" },
        { Name("tests/descriptor_buffer/software_caustics_mesh_nodes"), "Software Mesh Nodes" },
        { Name("tests/descriptor_buffer/software_caustics_mesh_positions"), "Software Mesh Positions" },
        { Name("tests/descriptor_buffer/software_caustics_mesh_indices"), "Software Mesh Indices" },
        { Name("tests/descriptor_buffer/software_caustics_mesh_attributes"), "Software Mesh Attributes" },
        { Name("tests/descriptor_buffer/software_caustics_scene_bvh_nodes"), "Scene BVH Nodes" },
        { Name("tests/descriptor_buffer/software_caustics_scene_instances"), "Scene Instances" },
        { Name("tests/descriptor_buffer/software_caustics_instance_materials"), "Shadow Instance Materials" },
        { Name("tests/descriptor_buffer/software_caustics_material_typed"), "Shadow Typed Materials" },
        { Name("tests/descriptor_buffer/software_caustics_instances"), "Shadow Instances" },
        { Name("tests/descriptor_buffer/software_caustics_lights"), "Deferred Lights" },
    };
    const BufferImportDesc constantBufferImports[constantBufferCount] = {
        { Name("tests/descriptor_buffer/software_caustics_mesh_view"), "Mesh View" },
        { Name("tests/descriptor_buffer/software_caustics_bindless_slots"), "Deferred Bindless Slots" },
        { Name("tests/descriptor_buffer/software_caustics_material_context_slots"), "Ray-Trace Material Context Slots" },
        { Name("tests/descriptor_buffer/software_caustics_scene_shading"), "Scene Shading" },
    };
    GpuGraphResourceId shaderBufferResources[shaderBufferCount] = {};
    GpuGraphResourceId constantBufferResources[constantBufferCount] = {};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        shaderBufferResources[bufferIndex] = importBuffer(
            shaderBuffers[bufferIndex],
            shaderBufferImports[bufferIndex].identity,
            shaderBufferImports[bufferIndex].label
        );
        ASSERT_TRUE(shaderBufferResources[bufferIndex].valid());
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex){
        constantBufferResources[bufferIndex] = importBuffer(
            constantBuffers[bufferIndex],
            constantBufferImports[bufferIndex].identity,
            constantBufferImports[bufferIndex].label
        );
        ASSERT_TRUE(constantBufferResources[bufferIndex].valid());
    }
    const GpuGraphResourceId worldPositionResource = importTexture(
        shaderTextures[0u],
        Name("tests/descriptor_buffer/software_caustics_world_position"),
        "World Position"
    );
    const GpuGraphResourceId depthResource = importTexture(
        shaderTextures[1u],
        Name("tests/descriptor_buffer/software_caustics_depth"),
        "Depth"
    );
    const GpuGraphResourceId causticHistoryResource = importTexture(
        uavTextures[0u],
        Name("tests/descriptor_buffer/software_caustics_history"),
        "Caustic History"
    );
    const GpuGraphResourceId causticResolveHalfResource = importTexture(
        uavTextures[1u],
        Name("tests/descriptor_buffer/software_caustics_resolve_half"),
        "Caustic Resolve Half"
    );
    const GpuGraphResourceId causticResolveGeometryResource = importTexture(
        uavTextures[2u],
        Name("tests/descriptor_buffer/software_caustics_resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const GpuGraphResourceId causticIrradianceResource = importTexture(
        causticIrradiance,
        Name("tests/descriptor_buffer/software_caustics_irradiance"),
        "Caustic Irradiance"
    );
    const GpuGraphResourceId causticAccumulatorResource = importTexture(
        causticAccumulator,
        Name("tests/descriptor_buffer/software_caustics_accumulator"),
        "Caustic Accumulator"
    );
    ASSERT_TRUE(worldPositionResource.valid());
    ASSERT_TRUE(depthResource.valid());
    ASSERT_TRUE(causticHistoryResource.valid());
    ASSERT_TRUE(causticResolveHalfResource.valid());
    ASSERT_TRUE(causticResolveGeometryResource.valid());
    ASSERT_TRUE(causticIrradianceResource.valid());
    ASSERT_TRUE(causticAccumulatorResource.valid());

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
    const GpuQueueRequest computeTransferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Compute,
        true,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse shadowPrepareUses[] = {
        { .resource = constantBufferResources[1u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[0u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[1u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = shaderBufferResources[2u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = shaderBufferResources[3u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = shaderBufferResources[4u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = shaderBufferResources[5u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[6u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[7u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[8u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[9u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/descriptor_buffer/software_caustics_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    bool shadowPrepareAttempted = false;
    bool shouldRecord = true;
    const GpuTaskId shadowPrepareTask = graph.addTask<NativePacketCaptureRetryTask>(
        shadowPrepareDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &shadowPrepareAttempted,
        }
    );
    ASSERT_TRUE(shadowPrepareTask.valid());

    const GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::RenderTarget, .access = GpuTaskResourceAccess::Write },
        { .resource = depthResource, .range = {}, .requiredState = ResourceStates::DepthWrite, .access = GpuTaskResourceAccess::Write },
        { .resource = constantBufferResources[0u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Write },
        { .resource = constantBufferResources[3u], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write },
        { .resource = shaderBufferResources[10u], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/software_caustics_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepareTask, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    bool prefixAttempted = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        prefixDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &prefixAttempted,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    const GpuTaskResourceUse shadowVisibilityUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = depthResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[1u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[3u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[1u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[2u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[3u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[4u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[5u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[6u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[7u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[8u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[9u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[10u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
    };
    GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/descriptor_buffer/software_caustics_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefixTask, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    bool shadowAttempted = false;
    const GpuTaskId shadowVisibilityTask = graph.addTask<NativePacketCaptureRetryTask>(
        shadowVisibilityDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &shadowAttempted,
        }
    );
    ASSERT_TRUE(shadowVisibilityTask.valid());

    GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_software_caustics_irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&shadowVisibilityTask, 1u)
    ;
    GpuClearTextureTaskDesc irradianceClear;
    irradianceClear.destination = causticIrradianceResource;
    irradianceClear.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    irradianceClear.valueType = GpuClearTextureTaskValueType::Float;
    irradianceClear.floatValue = Color(0.f, 0.f, 0.f, 0.f);
    const GpuTaskId irradianceClearTask = graph.addClearTextureTask(
        irradianceClearDesc,
        irradianceClear
    );
    ASSERT_TRUE(irradianceClearTask.valid());

    GpuTaskDesc accumulatorBootstrapClearDesc;
    GpuTaskSchedulingHint accumulatorBootstrapClearScheduling = irradianceClearScheduling;
    accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
    accumulatorBootstrapClearDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_software_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
    ;
    QueueSubmissionToken accumulatorBootstrapClearAcceptedToken;
    GpuClearTextureTaskDesc accumulatorBootstrapClear;
    accumulatorBootstrapClear.destination = causticAccumulatorResource;
    accumulatorBootstrapClear.subresources = TextureSubresourceSet(0u, 1u, 0u, 3u);
    accumulatorBootstrapClear.valueType = GpuClearTextureTaskValueType::UInt;
    accumulatorBootstrapClear.uintValue = UIntColor(0u);
    accumulatorBootstrapClear.acceptedToken = &accumulatorBootstrapClearAcceptedToken;
    const GpuTaskId accumulatorBootstrapClearTask = graph.addClearTextureTask(
        accumulatorBootstrapClearDesc,
        accumulatorBootstrapClear
    );
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    const GpuGraphResourceId softwareTraceGeometryMembers[] = {
        shaderBufferResources[1u],
        shaderBufferResources[2u],
        shaderBufferResources[3u],
        shaderBufferResources[4u],
    };
    const GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/software_caustics_trace_geometry"))
            .setMarkerLabel("Software Caustics Trace Geometry")
            .setMembers(softwareTraceGeometryMembers, LengthOf(softwareTraceGeometryMembers))
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
    const GpuTaskResourceUse causticsUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = depthResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[0u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[1u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[3u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[0u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[5u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[6u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[7u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[8u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[9u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[10u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = causticAccumulatorResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = causticHistoryResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveHalfResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveGeometryResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = causticIrradianceResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    GpuTaskDesc causticsDesc;
    causticsDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(computeQueue)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(causticsUses, LengthOf(causticsUses))
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    NativePacketSoftwareCausticsEntryProbeTask::Payload causticsPayload{};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        causticsPayload.shaderBuffers[bufferIndex] = shaderBuffers[bufferIndex].get();
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        causticsPayload.constantBuffers[bufferIndex] = constantBuffers[bufferIndex].get();
    for(u32 textureIndex = 0u; textureIndex < shaderTextureCount; ++textureIndex)
        causticsPayload.shaderTextures[textureIndex] = shaderTextures[textureIndex].get();
    for(u32 textureIndex = 0u; textureIndex < uavTextureCount; ++textureIndex)
        causticsPayload.uavTextures[textureIndex] = uavTextures[textureIndex].get();
    causticsPayload.irradiance = causticIrradiance.get();
    causticsPayload.accumulator = causticAccumulator.get();
    bool causticsRecorded = false;
    causticsPayload.recorded = &causticsRecorded;
    const GpuTaskId causticsTask = graph.addTask<NativePacketSoftwareCausticsEntryProbeTask>(
        causticsDesc,
        Move(causticsPayload)
    );
    ASSERT_TRUE(causticsTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        { .resource = causticIrradianceResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = shaderBufferResources[10u], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
    };
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/software_caustics_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(&causticsTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketPrefixTask>(
        lightingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = shaderBuffers[10u].get(),
            .expectedState = ResourceStates::ShaderResource,
            .texture = causticIrradiance.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/software_caustics_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 5u);
    const GpuSubmissionPacketId shadowPreparePacket = views.compiled.packetForTask(shadowPrepareTask);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId shadowPacket = views.compiled.packetForTask(shadowVisibilityTask);
    const GpuSubmissionPacketId irradianceClearPacket = views.compiled.packetForTask(irradianceClearTask);
    const GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        views.compiled.packetForTask(accumulatorBootstrapClearTask);
    const GpuSubmissionPacketId causticsPacket = views.compiled.packetForTask(causticsTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    const GpuCompiledTaskView compiledShadow = views.compiled.findTask(shadowVisibilityTask);
    const GpuCompiledTaskView compiledCaustics = views.compiled.findTask(causticsTask);
    ASSERT_TRUE(compiledShadow.valid());
    ASSERT_TRUE(compiledCaustics.valid());
    const GpuCompiledBarrier* const shadowBarriers = views.compiled.findTask(shadowVisibilityTask).prologueBarriers;
    const GpuCompiledBarrier* const causticsBarriers = views.compiled.findTask(causticsTask).prologueBarriers;
    ASSERT_NE(shadowBarriers, nullptr);
    ASSERT_NE(causticsBarriers, nullptr);
    bool shadowTransitionsDepthToShaderResource = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledShadow.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
        shadowTransitionsDepthToShaderResource = shadowTransitionsDepthToShaderResource || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == depthResource
            && barrier.before == ResourceStates::DepthWrite
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(shadowTransitionsDepthToShaderResource);
    const auto hasCausticsTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCaustics.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasCausticsTransition(causticHistoryResource, ResourceStates::Common));
    EXPECT_TRUE(hasCausticsTransition(causticResolveHalfResource, ResourceStates::Common));
    EXPECT_TRUE(hasCausticsTransition(causticResolveGeometryResource, ResourceStates::Common));
    EXPECT_TRUE(hasCausticsTransition(causticAccumulatorResource, ResourceStates::CopyDest));
    EXPECT_TRUE(hasCausticsTransition(causticIrradianceResource, ResourceStates::CopyDest));
    for(u32 barrierIndex = 0u; barrierIndex < compiledCaustics.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
        if(barrier.resource == depthResource)
            EXPECT_NE(barrier.after, ResourceStates::DepthRead);
    }
    const GpuCompiledPacketView causticsCompiledPacket = views.compiled.packet(causticsPacket);
    ASSERT_TRUE(causticsCompiledPacket.valid());
    const GpuPacketDependency* const causticsDependencies = views.compiled.packet(causticsPacket).dependencies;
    ASSERT_NE(causticsDependencies, nullptr);
    bool causticsWaitsForShadow = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < causticsCompiledPacket.plan->dependencyCount; ++dependencyIndex)
        causticsWaitsForShadow = causticsWaitsForShadow || causticsDependencies[dependencyIndex].producer == shadowPacket;
    EXPECT_TRUE(causticsWaitsForShadow);

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
    EXPECT_TRUE(shadowPrepareAttempted);
    EXPECT_TRUE(prefixAttempted);
    EXPECT_TRUE(shadowAttempted);
    EXPECT_TRUE(causticsRecorded);
    EXPECT_TRUE(lightingRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
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
    EXPECT_TRUE(accumulatorBootstrapClearAcceptedToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

