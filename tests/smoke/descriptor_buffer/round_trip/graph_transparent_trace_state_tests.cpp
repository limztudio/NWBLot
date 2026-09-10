// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The transparent software trace begins after opaque soft-shadow work, but its traversal/material/selector inputs
// remain unchanged descriptor-visible reads. Its shared visibility image/UAV boundary is covered by the fold probe
// below, so this entry-state probe remains focused on the descriptor-visible buffers.
struct NativePacketSoftTransparentTraceEntryProbeTask{
    static constexpr u32 s_ShaderBufferCount = 10u;
    static constexpr u32 s_ConstantBufferCount = 3u;

    struct Payload{
        Buffer* shaderBuffers[s_ShaderBufferCount] = {};
        Buffer* constantBuffers[s_ConstantBufferCount] = {};
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
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Each prepared soft-transparent callback deliberately performs getters only, so a renderer-native bridge cannot
// hide a missing compiler-lowered handoff.
struct NativePacketSoftTransparentHandoffProbeTask{
    struct Payload{
        Texture* shadowVisibility = nullptr;
        Texture* transparentSoftHalf = nullptr;
        ResourceStates::Mask transparentSoftHalfState = ResourceStates::UnorderedAccess;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.shadowVisibility
            && payload.transparentSoftHalf
            && commandList.getTextureSubresourceState(payload.shadowVisibility, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.transparentSoftHalf, 0u, 0u)
                == payload.transparentSoftHalfState
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The first opaque wavelet reads command-list state tracking only. It proves the compiler supplied both sampled
// inputs before renderer code can issue its temporal merge and wavelet dispatches.
struct NativePacketSoftOpaqueFirstWaveletHandoffProbeTask{
    struct Payload{
        Texture* shadowSoftGeometry = nullptr;
        Texture* opaqueSoftHalf = nullptr;
        Texture* opaqueWaveletHalf = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.shadowSoftGeometry
            && payload.opaqueSoftHalf
            && payload.opaqueWaveletHalf
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.opaqueSoftHalf, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.opaqueWaveletHalf, 0u, 0u)
                == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The opaque resolve tail samples the first-wavelet output and leaves visibility writable for transparent tracing.
// It too performs getters only, so its state cannot be hidden by a renderer-native bridge.
struct NativePacketSoftOpaqueResolveTailHandoffProbeTask{
    struct Payload{
        Texture* shadowVisibility = nullptr;
        Texture* shadowSoftGeometry = nullptr;
        Texture* opaqueWaveletHalf = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.shadowVisibility
            && payload.shadowSoftGeometry
            && payload.opaqueWaveletHalf
            && commandList.getTextureSubresourceState(payload.shadowVisibility, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.opaqueWaveletHalf, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The transparent temporal merge receives every graph-declared input/output state without a renderer-native bridge.
struct NativePacketSoftTransparentTemporalMergeProbeTask{
    struct Payload{
        Texture* transparentSoftHalf = nullptr;
        Texture* shadowSoftGeometry = nullptr;
        Texture* previousGeometry = nullptr;
        Texture* worldPosition = nullptr;
        Texture* historyInput = nullptr;
        Texture* momentsInput = nullptr;
        Texture* historyOutput = nullptr;
        Texture* momentsOutput = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.transparentSoftHalf
            && payload.shadowSoftGeometry
            && payload.previousGeometry
            && payload.worldPosition
            && payload.historyInput
            && payload.momentsInput
            && payload.historyOutput
            && payload.momentsOutput
            && commandList.getTextureSubresourceState(payload.transparentSoftHalf, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.previousGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.historyInput, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.momentsInput, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.historyOutput, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.momentsOutput, 0u, 0u)
                == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The first wavelet samples the pair emitted by temporal merge and publishes half-A for the terminal resolve tail.
// It uses state getters only, so neither merge output transition can be hidden by a native bridge.
struct NativePacketSoftTransparentFirstWaveletProbeTask{
    struct Payload{
        Texture* opaqueSoftHalf = nullptr;
        Texture* shadowSoftGeometry = nullptr;
        Texture* historyOutput = nullptr;
        Texture* momentsOutput = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.opaqueSoftHalf
            && payload.shadowSoftGeometry
            && payload.historyOutput
            && payload.momentsOutput
            && commandList.getTextureSubresourceState(payload.opaqueSoftHalf, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.historyOutput, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.momentsOutput, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The terminal transparent resolve tail samples the first wavelet and retains the existing visibility-output owner.
// It also performs getters only, so the compiler must provide the sampled/UAV handoff.
struct NativePacketSoftTransparentResolveTailProbeTask{
    struct Payload{
        Texture* shadowVisibility = nullptr;
        Texture* opaqueSoftHalf = nullptr;
        Texture* shadowSoftGeometry = nullptr;
        Texture* worldPosition = nullptr;
        Texture* normal = nullptr;
        Texture* depth = nullptr;
        Buffer* sceneShading = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.shadowVisibility
            && payload.opaqueSoftHalf
            && payload.shadowSoftGeometry
            && payload.worldPosition
            && payload.normal
            && payload.depth
            && payload.sceneShading
            && commandList.getTextureSubresourceState(payload.shadowVisibility, 0u, 0u)
                == ResourceStates::UnorderedAccess
            && commandList.getTextureSubresourceState(payload.opaqueSoftHalf, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.shadowSoftGeometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.normal, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.depth, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.sceneShading)
                == ResourceStates::ConstantBuffer
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The post-G-buffer normalizer must see both raster and software-build trace geometry in graph-declared
// ShaderResource state. It deliberately performs no native transition itself.
struct NativePacketPostGbufferTraceGeometryProbeTask{
    struct Payload{
        Buffer* rasterGeometry = nullptr;
        Buffer* softwareBvh = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.rasterGeometry
            && payload.softwareBvh
            && commandList.getBufferState(payload.rasterGeometry) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.softwareBvh) == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The normal deferred graph declares the selected trace geometry at the post-G-buffer boundary. Verify that its
// native callback can remain getter-only while the compiler lowers both raster and software-BVH predecessor states.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedPostGbufferTraceGeometryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeBuffer = [&device](const bool canHaveUavs){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(canHaveUavs)
                .setIsVertexBuffer(!canHaveUavs)
                .setInitialState(ResourceStates::Common)
        );
    };
    BufferHandle rasterGeometry = makeBuffer(false);
    BufferHandle softwareBvh = makeBuffer(true);
    ASSERT_NE(rasterGeometry.get(), nullptr);
    ASSERT_NE(softwareBvh.get(), nullptr);

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
    const GpuGraphResourceId rasterGeometryResource = importBuffer(
        rasterGeometry,
        Name("tests/descriptor_buffer/post_gbuffer_raster_geometry"),
        "Post-G-Buffer Raster Geometry"
    );
    const GpuGraphResourceId softwareBvhResource = importBuffer(
        softwareBvh,
        Name("tests/descriptor_buffer/post_gbuffer_software_bvh"),
        "Post-G-Buffer Software BVH"
    );
    ASSERT_TRUE(rasterGeometryResource.valid());
    ASSERT_TRUE(softwareBvhResource.valid());

    const GpuGraphResourceId traceGeometryMembers[] = { rasterGeometryResource, softwareBvhResource };
    const GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/post_gbuffer_trace_geometry"))
            .setMarkerLabel("Post-G-Buffer Trace Geometry")
            .setMembers(traceGeometryMembers, LengthOf(traceGeometryMembers))
    );
    ASSERT_TRUE(traceGeometrySet.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = GpuTaskCostHint::Medium;
    gbufferScheduling.allowPacketMerge = true;
    const GpuTaskResourceUse gbufferUses[] = {
        {
            .resource = rasterGeometryResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = softwareBvhResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("tests/descriptor_buffer/post_gbuffer_gbuffer"))
        .setMarkerLabel("G-Buffer")
        .setQueue(graphicsQueue)
        .setScheduling(gbufferScheduling)
        .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    ;
    bool shouldRecord = true;
    bool gbufferAttempted = false;
    const GpuTaskId gbufferTask = graph.addTask<NativePacketCaptureRetryTask>(
        gbufferDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &gbufferAttempted,
        }
    );
    ASSERT_TRUE(gbufferTask.valid());

    GpuTaskSchedulingHint normalizeScheduling = gbufferScheduling;
    normalizeScheduling.mergeWithPrevious = true;
    const GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/post_gbuffer_normalize_trace_geometry"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(graphicsQueue)
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferTask, 1u)
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    bool normalizeRecorded = false;
    const GpuTaskId normalizeTask = graph.addTask<NativePacketPostGbufferTraceGeometryProbeTask>(
        normalizeDesc,
        NativePacketPostGbufferTraceGeometryProbeTask::Payload{
            .rasterGeometry = rasterGeometry.get(),
            .softwareBvh = softwareBvh.get(),
            .recorded = &normalizeRecorded,
        }
    );
    ASSERT_TRUE(normalizeTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/post_gbuffer_trace_geometry_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledNormalize = views.compiled.findTask(normalizeTask);
    ASSERT_TRUE(compiledNormalize.valid());
    const GpuCompiledBarrier* const normalizeBarriers = views.compiled.findTask(normalizeTask).prologueBarriers;
    ASSERT_NE(normalizeBarriers, nullptr);
    const auto hasNormalizeTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledNormalize.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = normalizeBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasNormalizeTransition(rasterGeometryResource, ResourceStates::VertexBuffer));
    EXPECT_TRUE(hasNormalizeTransition(softwareBvhResource, ResourceStates::UnorderedAccess));

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
    EXPECT_TRUE(gbufferAttempted);
    EXPECT_TRUE(normalizeRecorded);

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
    EXPECT_TRUE(transaction.packetToken(views.compiled.packetForTask(normalizeTask)).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The transparent software trace runs after the opaque soft resolve, but its descriptor-selected traversal/material
// buffers retain the static graph-declared states. This packet proof is getter-only at that late trace seam; the
// opaque-to-transparent image/UAV boundary is deliberately excluded because the renderer continues to own it.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSoftTransparentTraceEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 shaderBufferCount = NativePacketSoftTransparentTraceEntryProbeTask::s_ShaderBufferCount;
    constexpr u32 constantBufferCount = NativePacketSoftTransparentTraceEntryProbeTask::s_ConstantBufferCount;
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
    struct BufferImportDesc{
        Name identity;
        AStringView label;
    };
    const BufferImportDesc shaderBufferImports[shaderBufferCount] = {
        { Name("tests/descriptor_buffer/soft_transparent_mesh_nodes"), "Software Mesh Nodes" },
        { Name("tests/descriptor_buffer/soft_transparent_mesh_positions"), "Software Mesh Positions" },
        { Name("tests/descriptor_buffer/soft_transparent_mesh_indices"), "Software Mesh Indices" },
        { Name("tests/descriptor_buffer/soft_transparent_mesh_attributes"), "Software Mesh Attributes" },
        { Name("tests/descriptor_buffer/soft_transparent_scene_bvh_nodes"), "Scene BVH Nodes" },
        { Name("tests/descriptor_buffer/soft_transparent_scene_instances"), "Scene Instances" },
        { Name("tests/descriptor_buffer/soft_transparent_instance_materials"), "Shadow Instance Materials" },
        { Name("tests/descriptor_buffer/soft_transparent_material_typed"), "Shadow Typed Materials" },
        { Name("tests/descriptor_buffer/soft_transparent_instances"), "Shadow Instances" },
        { Name("tests/descriptor_buffer/soft_transparent_lights"), "Deferred Lights" },
    };
    const BufferImportDesc constantBufferImports[constantBufferCount] = {
        { Name("tests/descriptor_buffer/soft_transparent_material_context_slots"), "Ray-Trace Material Context Slots" },
        { Name("tests/descriptor_buffer/soft_transparent_bindless_slots"), "Deferred Bindless Slots" },
        { Name("tests/descriptor_buffer/soft_transparent_scene_shading"), "Scene Shading" },
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

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Alloc::ScratchArena resourceUseArena(Name("tests/descriptor_buffer/soft_transparent_entry_state_resource_uses"));
    Vector<GpuTaskResourceUse, Alloc::ScratchArena> prefixUses(resourceUseArena);
    prefixUses.reserve(shaderBufferCount + constantBufferCount);
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_prefix"))
        .setMarkerLabel("Transparent Shadow Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses.data(), prefixUses.size())
    ;
    bool shouldRecord = true;
    bool prefixAttempted = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        prefixDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &prefixAttempted,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    constexpr u32 softwareTraceGeometryBufferCount = 4u;
    const GpuGraphResourceId softwareTraceGeometryMembers[] = {
        shaderBufferResources[0u],
        shaderBufferResources[1u],
        shaderBufferResources[2u],
        shaderBufferResources[3u],
    };
    const GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/soft_transparent_trace_geometry"))
            .setMarkerLabel("Transparent Shadow Trace Geometry")
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
    Vector<GpuTaskResourceUse, Alloc::ScratchArena> traceUses(resourceUseArena);
    traceUses.reserve(shaderBufferCount - softwareTraceGeometryBufferCount + constantBufferCount);
    for(u32 bufferIndex = softwareTraceGeometryBufferCount; bufferIndex < shaderBufferCount; ++bufferIndex)
        traceUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read });
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        traceUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read });
    GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_soft_transparent_trace"))
        .setMarkerLabel("Transparent Shadow Trace")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefixTask, 1u)
        .setResourceUses(traceUses.data(), traceUses.size())
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    NativePacketSoftTransparentTraceEntryProbeTask::Payload tracePayload{};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        tracePayload.shaderBuffers[bufferIndex] = shaderBuffers[bufferIndex].get();
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        tracePayload.constantBuffers[bufferIndex] = constantBuffers[bufferIndex].get();
    bool traceRecorded = false;
    tracePayload.recorded = &traceRecorded;
    const GpuTaskId traceTask = graph.addTask<NativePacketSoftTransparentTraceEntryProbeTask>(
        traceDesc,
        Move(tracePayload)
    );
    ASSERT_TRUE(traceTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/soft_transparent_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId tracePacket = views.compiled.packetForTask(traceTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    EXPECT_NE(prefixPacket, tracePacket);
    const GpuCompiledTaskView compiledTrace = views.compiled.findTask(traceTask);
    ASSERT_TRUE(compiledTrace.valid());
    EXPECT_EQ(compiledTrace.plan->prologueStateSeedCount, shaderBufferCount + constantBufferCount);
    const GpuPacketStateSeed* const traceSeeds = views.compiled.findTask(traceTask).prologueStateSeeds;
    ASSERT_NE(traceSeeds, nullptr);
    const auto hasTraceSeed = [&](const GpuGraphResourceId resource){
        for(u32 seedIndex = 0u; seedIndex < compiledTrace.plan->prologueStateSeedCount; ++seedIndex){
            if(traceSeeds[seedIndex].resource == resource && traceSeeds[seedIndex].sourcePacket == prefixPacket)
                return true;
        }
        return false;
    };
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        EXPECT_TRUE(hasTraceSeed(shaderBufferResources[bufferIndex]));
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        EXPECT_TRUE(hasTraceSeed(constantBufferResources[bufferIndex]));
    const GpuCompiledBarrier* const traceBarriers = views.compiled.findTask(traceTask).prologueBarriers;
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTrace.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::CopyDest
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        EXPECT_TRUE(hasTraceTransition(shaderBufferResources[bufferIndex], ResourceStates::ShaderResource));
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        EXPECT_TRUE(hasTraceTransition(constantBufferResources[bufferIndex], ResourceStates::ConstantBuffer));
    ASSERT_EQ(views.compiled.packet(tracePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(tracePacket).dependencies[0u].producer, prefixPacket);

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
    EXPECT_TRUE(prefixAttempted);
    EXPECT_TRUE(traceRecorded);

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
    EXPECT_TRUE(transaction.packetToken(tracePacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The normal deferred soft-transparent route records opaque production, first wavelet, resolve tail, transparent
// trace, transparent first wavelet, and terminal resolve tail in one packet. Prove every split handoff without a
// renderer-native state bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSoftTransparentTraceToResolveRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
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
    auto shadowVisibility = makeShadowTarget(ResourceStates::Unknown);
    auto transparentSoftHalf = makeShadowTarget(ResourceStates::Unknown);
    auto transparentHistoryA = makeShadowTarget(ResourceStates::Unknown);
    auto transparentMomentsA = makeShadowTarget(ResourceStates::Unknown);
    auto transparentHistoryB = makeShadowTarget(ResourceStates::Unknown);
    auto transparentMomentsB = makeShadowTarget(ResourceStates::Unknown);
    auto opaqueSoftHalf = makeShadowTarget(ResourceStates::Unknown);
    auto opaqueWaveletHalf = makeShadowTarget(ResourceStates::Unknown);
    auto shadowSoftGeometry = makeShadowTarget(ResourceStates::Unknown);
    auto previousGeometry = makeShadowTarget();
    auto worldPosition = makeShadowTarget();
    auto normal = makeShadowTarget();
    auto depth = makeShadowTarget();
    auto sceneShading = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(transparentSoftHalf.get(), nullptr);
    ASSERT_NE(transparentHistoryA.get(), nullptr);
    ASSERT_NE(transparentMomentsA.get(), nullptr);
    ASSERT_NE(transparentHistoryB.get(), nullptr);
    ASSERT_NE(transparentMomentsB.get(), nullptr);
    ASSERT_NE(opaqueSoftHalf.get(), nullptr);
    ASSERT_NE(opaqueWaveletHalf.get(), nullptr);
    ASSERT_NE(shadowSoftGeometry.get(), nullptr);
    ASSERT_NE(previousGeometry.get(), nullptr);
    ASSERT_NE(worldPosition.get(), nullptr);
    ASSERT_NE(normal.get(), nullptr);
    ASSERT_NE(depth.get(), nullptr);
    ASSERT_NE(sceneShading.get(), nullptr);
    Texture* const initialTextures[] = {
        previousGeometry.get(),
        worldPosition.get(),
        normal.get(),
        depth.get(),
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
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const GpuGraphResourceId shadowVisibilityResource = importTexture(
        shadowVisibility,
        Name("tests/descriptor_buffer/soft_transparent_fold_shadow_visibility"),
        "Shadow Visibility"
    );
    const GpuGraphResourceId transparentSoftHalfResource = importTexture(
        transparentSoftHalf,
        Name("tests/descriptor_buffer/soft_transparent_fold_half"),
        "Transparent Shadow Soft Half"
    );
    const GpuGraphResourceId transparentHistoryAResource = importTexture(
        transparentHistoryA,
        Name("tests/descriptor_buffer/soft_transparent_history_a"),
        "Transparent Shadow History A"
    );
    const GpuGraphResourceId transparentMomentsAResource = importTexture(
        transparentMomentsA,
        Name("tests/descriptor_buffer/soft_transparent_moments_a"),
        "Transparent Shadow Moments A"
    );
    const GpuGraphResourceId transparentHistoryBResource = importTexture(
        transparentHistoryB,
        Name("tests/descriptor_buffer/soft_transparent_history_b"),
        "Transparent Shadow History B"
    );
    const GpuGraphResourceId transparentMomentsBResource = importTexture(
        transparentMomentsB,
        Name("tests/descriptor_buffer/soft_transparent_moments_b"),
        "Transparent Shadow Moments B"
    );
    const GpuGraphResourceId shadowSoftGeometryResource = importTexture(
        shadowSoftGeometry,
        Name("tests/descriptor_buffer/soft_transparent_fold_geometry"),
        "Shadow Soft Geometry"
    );
    const GpuGraphResourceId opaqueSoftHalfResource = importTexture(
        opaqueSoftHalf,
        Name("tests/descriptor_buffer/soft_transparent_fold_opaque_half"),
        "Opaque Shadow Soft Half"
    );
    const GpuGraphResourceId opaqueWaveletHalfResource = importTexture(
        opaqueWaveletHalf,
        Name("tests/descriptor_buffer/soft_transparent_fold_opaque_wavelet_half"),
        "Opaque Shadow First Wavelet Half"
    );
    const GpuGraphResourceId previousGeometryResource = importTexture(
        previousGeometry,
        Name("tests/descriptor_buffer/soft_transparent_fold_geometry_previous"),
        "Previous Shadow Soft Geometry"
    );
    const GpuGraphResourceId worldPositionResource = importTexture(
        worldPosition,
        Name("tests/descriptor_buffer/soft_transparent_fold_world_position"),
        "World Position"
    );
    const GpuGraphResourceId normalResource = importTexture(
        normal,
        Name("tests/descriptor_buffer/soft_transparent_fold_normal"),
        "G-buffer Normal"
    );
    const GpuGraphResourceId depthResource = importTexture(
        depth,
        Name("tests/descriptor_buffer/soft_transparent_fold_depth"),
        "G-buffer Depth"
    );
    const GpuGraphResourceId sceneShadingResource = importBuffer(
        sceneShading,
        Name("tests/descriptor_buffer/soft_transparent_fold_scene_shading"),
        "Scene Shading"
    );
    ASSERT_TRUE(shadowVisibilityResource.valid());
    ASSERT_TRUE(transparentSoftHalfResource.valid());
    ASSERT_TRUE(transparentHistoryAResource.valid());
    ASSERT_TRUE(transparentMomentsAResource.valid());
    ASSERT_TRUE(transparentHistoryBResource.valid());
    ASSERT_TRUE(transparentMomentsBResource.valid());
    ASSERT_TRUE(shadowSoftGeometryResource.valid());
    ASSERT_TRUE(opaqueSoftHalfResource.valid());
    ASSERT_TRUE(opaqueWaveletHalfResource.valid());
    ASSERT_TRUE(previousGeometryResource.valid());
    ASSERT_TRUE(worldPositionResource.valid());
    ASSERT_TRUE(normalResource.valid());
    ASSERT_TRUE(depthResource.valid());
    ASSERT_TRUE(sceneShadingResource.valid());

    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        false,
    };
    GpuTaskSchedulingHint opaqueScheduling;
    opaqueScheduling.cost = GpuTaskCostHint::Large;
    opaqueScheduling.forceSubmissionBoundary = false;
    opaqueScheduling.allowPacketMerge = true;
    opaqueScheduling.mergeWithPrevious = false;
    const GpuTaskResourceUse opaqueUses[] = {
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometryResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = opaqueSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentHistoryAResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsAResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentHistoryBResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsBResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_fold_opaque"))
        .setMarkerLabel("Shadow Visibility Opaque")
        .setQueue(computeQueue)
        .setScheduling(opaqueScheduling)
        .setResourceUses(opaqueUses, LengthOf(opaqueUses))
    ;
    bool shouldRecord = true;
    bool opaqueRecorded = false;
    const GpuTaskId opaqueTask = graph.addTask<NativePacketCaptureRetryTask>(
        opaqueDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &opaqueRecorded,
        }
    );
    ASSERT_TRUE(opaqueTask.valid());

    GpuTaskSchedulingHint traceScheduling = opaqueScheduling;
    traceScheduling.cost = GpuTaskCostHint::Medium;
    traceScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse opaqueFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalfResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometryResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc opaqueFirstWaveletDesc;
    opaqueFirstWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_opaque_first_wavelet"))
        .setMarkerLabel("Shadow Opaque First Wavelet")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueTask, 1u)
        .setResourceUses(opaqueFirstWaveletUses, LengthOf(opaqueFirstWaveletUses))
    ;
    bool opaqueFirstWaveletRecorded = false;
    const GpuTaskId opaqueFirstWaveletTask = graph.addTask<NativePacketSoftOpaqueFirstWaveletHandoffProbeTask>(
        opaqueFirstWaveletDesc,
        NativePacketSoftOpaqueFirstWaveletHandoffProbeTask::Payload{
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .opaqueSoftHalf = opaqueSoftHalf.get(),
            .opaqueWaveletHalf = opaqueWaveletHalf.get(),
            .recorded = &opaqueFirstWaveletRecorded,
        }
    );
    ASSERT_TRUE(opaqueFirstWaveletTask.valid());

    const GpuTaskResourceUse opaqueResolveUses[] = {
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometryResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalfResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc opaqueResolveDesc;
    opaqueResolveDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_opaque_resolve"))
        .setMarkerLabel("Shadow Opaque Soft Resolve")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueFirstWaveletTask, 1u)
        .setResourceUses(opaqueResolveUses, LengthOf(opaqueResolveUses))
    ;
    bool opaqueResolveRecorded = false;
    const GpuTaskId opaqueResolveTask = graph.addTask<NativePacketSoftOpaqueResolveTailHandoffProbeTask>(
        opaqueResolveDesc,
        NativePacketSoftOpaqueResolveTailHandoffProbeTask::Payload{
            .shadowVisibility = shadowVisibility.get(),
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .opaqueWaveletHalf = opaqueWaveletHalf.get(),
            .recorded = &opaqueResolveRecorded,
        }
    );
    ASSERT_TRUE(opaqueResolveTask.valid());

    const GpuTaskResourceUse traceUses[] = {
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = opaqueSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = transparentSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_trace"))
        .setMarkerLabel("Shadow Transparent Soft Trace")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueResolveTask, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
    ;
    bool traceRecorded = false;
    const GpuTaskId traceTask = graph.addTask<NativePacketSoftTransparentHandoffProbeTask>(
        traceDesc,
        NativePacketSoftTransparentHandoffProbeTask::Payload{
            .shadowVisibility = shadowVisibility.get(),
            .transparentSoftHalf = transparentSoftHalf.get(),
            .transparentSoftHalfState = ResourceStates::UnorderedAccess,
            .recorded = &traceRecorded,
        }
    );
    ASSERT_TRUE(traceTask.valid());

    const GpuTaskResourceUse transparentTemporalMergeUses[] = {
        {
            .resource = transparentSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometryResource,
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
            .resource = transparentHistoryAResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsAResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryBResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsBResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc transparentTemporalMergeDesc;
    transparentTemporalMergeDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_temporal_merge"))
        .setMarkerLabel("Shadow Transparent Temporal Merge")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&traceTask, 1u)
        .setResourceUses(transparentTemporalMergeUses, LengthOf(transparentTemporalMergeUses))
    ;
    bool transparentTemporalMergeRecorded = false;
    const GpuTaskId transparentTemporalMergeTask = graph.addTask<NativePacketSoftTransparentTemporalMergeProbeTask>(
        transparentTemporalMergeDesc,
        NativePacketSoftTransparentTemporalMergeProbeTask::Payload{
            .transparentSoftHalf = transparentSoftHalf.get(),
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .previousGeometry = previousGeometry.get(),
            .worldPosition = worldPosition.get(),
            .historyInput = transparentHistoryA.get(),
            .momentsInput = transparentMomentsA.get(),
            .historyOutput = transparentHistoryB.get(),
            .momentsOutput = transparentMomentsB.get(),
            .recorded = &transparentTemporalMergeRecorded,
        }
    );
    ASSERT_TRUE(transparentTemporalMergeTask.valid());

    const GpuTaskResourceUse transparentFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometryResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryBResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsBResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc transparentFirstWaveletDesc;
    transparentFirstWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_first_wavelet"))
        .setMarkerLabel("Shadow Transparent First Wavelet")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentTemporalMergeTask, 1u)
        .setResourceUses(transparentFirstWaveletUses, LengthOf(transparentFirstWaveletUses))
    ;
    bool transparentFirstWaveletRecorded = false;
    const GpuTaskId transparentFirstWaveletTask = graph.addTask<NativePacketSoftTransparentFirstWaveletProbeTask>(
        transparentFirstWaveletDesc,
        NativePacketSoftTransparentFirstWaveletProbeTask::Payload{
            .opaqueSoftHalf = opaqueSoftHalf.get(),
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .historyOutput = transparentHistoryB.get(),
            .momentsOutput = transparentMomentsB.get(),
            .recorded = &transparentFirstWaveletRecorded,
        }
    );
    ASSERT_TRUE(transparentFirstWaveletTask.valid());

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
    const GpuTaskResourceUse foldUses[] = {
        {
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = opaqueSoftHalfResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometryResource,
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
            .resource = normalResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = depthResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        {
            .resource = sceneShadingResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc foldDesc;
    foldDesc
        .setIdentity(Name("tests/descriptor_buffer/soft_transparent_fold"))
        .setMarkerLabel("Shadow Transparent Soft Fold")
        .setQueue(computeQueue)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentFirstWaveletTask, 1u)
        .setResourceUses(foldUses, LengthOf(foldUses))
    ;
    bool foldRecorded = false;
    const GpuTaskId foldTask = graph.addTask<NativePacketSoftTransparentResolveTailProbeTask>(
        foldDesc,
        NativePacketSoftTransparentResolveTailProbeTask::Payload{
            .shadowVisibility = shadowVisibility.get(),
            .opaqueSoftHalf = opaqueSoftHalf.get(),
            .shadowSoftGeometry = shadowSoftGeometry.get(),
            .worldPosition = worldPosition.get(),
            .normal = normal.get(),
            .depth = depth.get(),
            .sceneShading = sceneShading.get(),
            .recorded = &foldRecorded,
        }
    );
    ASSERT_TRUE(foldTask.valid());

    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/soft_transparent_fold_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(opaqueTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(opaqueFirstWaveletTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(opaqueResolveTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(traceTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(transparentTemporalMergeTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(transparentFirstWaveletTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(foldTask), packet);
    EXPECT_EQ(views.compiled.packet(packet).plan->taskCount, 7u);
    const GpuCompiledTaskView compiledTrace = views.compiled.findTask(traceTask);
    ASSERT_TRUE(compiledTrace.valid());
    const GpuCompiledBarrier* const traceBarriers = views.compiled.findTask(traceTask).prologueBarriers;
    ASSERT_NE(traceBarriers, nullptr);
    bool hasVisibilityUav = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTrace.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::TextureUav
            && barrier.resource == shadowVisibilityResource
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::UnorderedAccess
        ){
            hasVisibilityUav = true;
            break;
        }
    }
    EXPECT_TRUE(hasVisibilityUav);

    const GpuCompiledTaskView compiledOpaqueFirstWavelet = views.compiled.findTask(opaqueFirstWaveletTask);
    ASSERT_TRUE(compiledOpaqueFirstWavelet.valid());
    const GpuCompiledBarrier* const opaqueFirstWaveletBarriers = views.compiled.findTask(opaqueFirstWaveletTask).prologueBarriers;
    ASSERT_NE(opaqueFirstWaveletBarriers, nullptr);
    const auto hasOpaqueFirstWaveletTransition = [&](const GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueFirstWavelet.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = opaqueFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(opaqueSoftHalfResource));
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(shadowSoftGeometryResource));

    const GpuCompiledTaskView compiledOpaqueResolve = views.compiled.findTask(opaqueResolveTask);
    ASSERT_TRUE(compiledOpaqueResolve.valid());
    const GpuCompiledBarrier* const opaqueResolveBarriers = views.compiled.findTask(opaqueResolveTask).prologueBarriers;
    ASSERT_NE(opaqueResolveBarriers, nullptr);
    bool hasOpaqueWaveletResolveTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueResolve.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = opaqueResolveBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == opaqueWaveletHalfResource
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        ){
            hasOpaqueWaveletResolveTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasOpaqueWaveletResolveTransition);

    const GpuCompiledTaskView compiledTransparentTemporalMerge = views.compiled.findTask(transparentTemporalMergeTask);
    ASSERT_TRUE(compiledTransparentTemporalMerge.valid());
    const GpuCompiledBarrier* const transparentTemporalMergeBarriers =
        views.compiled.findTask(transparentTemporalMergeTask).prologueBarriers
    ;
    ASSERT_NE(transparentTemporalMergeBarriers, nullptr);
    const auto hasTransparentTemporalMergeShaderResourceTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentTemporalMerge.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = transparentTemporalMergeBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentSoftHalfResource, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentHistoryAResource, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentMomentsAResource, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(previousGeometryResource, ResourceStates::Common));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(worldPositionResource, ResourceStates::Common));

    const GpuCompiledTaskView compiledTransparentFirstWavelet = views.compiled.findTask(transparentFirstWaveletTask);
    ASSERT_TRUE(compiledTransparentFirstWavelet.valid());
    const GpuCompiledBarrier* const transparentFirstWaveletBarriers =
        views.compiled.findTask(transparentFirstWaveletTask).prologueBarriers
    ;
    ASSERT_NE(transparentFirstWaveletBarriers, nullptr);
    const auto hasTransparentFirstWaveletShaderResourceTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentFirstWavelet.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = transparentFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentHistoryBResource, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentMomentsBResource, ResourceStates::UnorderedAccess));

    const GpuCompiledTaskView compiledFold = views.compiled.findTask(foldTask);
    ASSERT_TRUE(compiledFold.valid());
    const GpuCompiledBarrier* const foldBarriers = views.compiled.findTask(foldTask).prologueBarriers;
    ASSERT_NE(foldBarriers, nullptr);
    const auto hasFoldShaderResourceTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledFold.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasFoldShaderResourceTransition(opaqueSoftHalfResource, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasFoldShaderResourceTransition(normalResource, ResourceStates::Common));
    EXPECT_TRUE(hasFoldShaderResourceTransition(depthResource, ResourceStates::Common));
    bool hasSceneShadingTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFold.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == sceneShadingResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ConstantBuffer
        ){
            hasSceneShadingTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasSceneShadingTransition);

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
    EXPECT_TRUE(opaqueRecorded);
    EXPECT_TRUE(opaqueFirstWaveletRecorded);
    EXPECT_TRUE(opaqueResolveRecorded);
    EXPECT_TRUE(traceRecorded);
    EXPECT_TRUE(transparentTemporalMergeRecorded);
    EXPECT_TRUE(transparentFirstWaveletRecorded);
    EXPECT_TRUE(foldRecorded);

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
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

