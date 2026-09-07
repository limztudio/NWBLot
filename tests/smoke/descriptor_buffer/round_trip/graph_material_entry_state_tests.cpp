// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "external_state_test_support.h"
#include "graph_resources_test_support.h"
#include "material_probes_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CSG clip/cap/interval callbacks receive their receiver/cutter SRVs and clip/sample CBVs from graph declarations.
// This probe intentionally only reads the command-list tracker before any renderer-native state setup can run.
struct NativePacketCsgClipBufferEntryProbeTask{
    struct Payload{
        Buffer* receiverRanges = nullptr;
        Buffer* cutters = nullptr;
        Buffer* clipContextSlots = nullptr;
        Buffer* intervalSampleState = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.receiverRanges
            || !payload.cutters
            || !payload.clipContextSlots
            || !payload.intervalSampleState
        )
            return false;
        const bool ready =
            commandList.getBufferState(payload.receiverRanges) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.cutters) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.clipContextSlots) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.intervalSampleState) == ResourceStates::ConstantBuffer
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Prepared opaque and AVBOIT draw streams share this mesh-view/material entry batch. The probe deliberately uses
// only state getters, ensuring the graph has established all three descriptor-visible states before native draw
// code could reintroduce its compatibility bridge.
struct NativePacketMaterialFrameEntryProbeTask{
    struct Payload{
        Buffer* meshView = nullptr;
        Buffer* materialInstances = nullptr;
        Buffer* materialTyped = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.meshView
            && payload.materialInstances
            && payload.materialTyped
            && commandList.getBufferState(payload.meshView) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.materialInstances) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.materialTyped) == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// CSG peel, receiver-surface, cap, and material callbacks no longer restate the four heap-selected clip-buffer
// entry states on graph-owned paths. All four imported buffers begin Common; this getter-only callback proves the
// packet runtime lowers their declared entry transitions before native renderer work can run.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedCsgClipBufferEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeStorageBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setStructStride(16u)
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
    auto receiverRanges = makeStorageBuffer();
    auto cutters = makeStorageBuffer();
    auto clipContextSlots = makeConstantBuffer();
    auto intervalSampleState = makeConstantBuffer();
    ASSERT_NE(receiverRanges.get(), nullptr);
    ASSERT_NE(cutters.get(), nullptr);
    ASSERT_NE(clipContextSlots.get(), nullptr);
    ASSERT_NE(intervalSampleState.get(), nullptr);

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
    const GpuGraphResourceId receiverRangesResource = importBuffer(
        receiverRanges,
        Name("tests/descriptor_buffer/csg_clip_receiver_ranges"),
        "CSG Receiver Ranges"
    );
    const GpuGraphResourceId cuttersResource = importBuffer(
        cutters,
        Name("tests/descriptor_buffer/csg_clip_cutters"),
        "CSG Cutters"
    );
    const GpuGraphResourceId clipContextSlotsResource = importBuffer(
        clipContextSlots,
        Name("tests/descriptor_buffer/csg_clip_context_slots"),
        "CSG Clip Context Slots"
    );
    const GpuGraphResourceId intervalSampleStateResource = importBuffer(
        intervalSampleState,
        Name("tests/descriptor_buffer/csg_clip_interval_sample_state"),
        "CSG Interval Sample State"
    );
    ASSERT_TRUE(receiverRangesResource.valid());
    ASSERT_TRUE(cuttersResource.valid());
    ASSERT_TRUE(clipContextSlotsResource.valid());
    ASSERT_TRUE(intervalSampleStateResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint csgClipScheduling;
    csgClipScheduling.cost = GpuTaskCostHint::Medium;
    csgClipScheduling.forceSubmissionBoundary = true;
    csgClipScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse csgClipUses[] = {
        GpuTaskResourceUse{
            .resource = receiverRangesResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = cuttersResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = clipContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = intervalSampleStateResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc csgClipDesc;
    csgClipDesc
        .setIdentity(Name("tests/descriptor_buffer/csg_clip_entry"))
        .setMarkerLabel("CSG Clip Entry")
        .setQueue(graphicsQueue)
        .setScheduling(csgClipScheduling)
        .setResourceUses(csgClipUses, LengthOf(csgClipUses))
    ;
    bool csgClipRecorded = false;
    const GpuTaskId csgClipTask = graph.addTask<NativePacketCsgClipBufferEntryProbeTask>(
        csgClipDesc,
        NativePacketCsgClipBufferEntryProbeTask::Payload{
            .receiverRanges = receiverRanges.get(),
            .cutters = cutters.get(),
            .clipContextSlots = clipContextSlots.get(),
            .intervalSampleState = intervalSampleState.get(),
            .recorded = &csgClipRecorded,
        }
    );
    ASSERT_TRUE(csgClipTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/csg_clip_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId csgClipPacket = views.compiled.packetForTask(csgClipTask);
    ASSERT_TRUE(csgClipPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledCsgClip = views.compiled.findTask(csgClipTask);
    ASSERT_TRUE(compiledCsgClip.valid());
    ASSERT_EQ(compiledCsgClip.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCsgClip.plan->prologueBarrierCount, 4u);
    const GpuCompiledBarrier* const csgClipBarriers = views.compiled.findTask(csgClipTask).prologueBarriers;
    ASSERT_NE(csgClipBarriers, nullptr);
    const auto hasTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask expectedState){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCsgClip.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = csgClipBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::Common
                && barrier.after == expectedState
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(receiverRangesResource, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(cuttersResource, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(clipContextSlotsResource, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasTransition(intervalSampleStateResource, ResourceStates::ConstantBuffer));
    EXPECT_EQ(views.compiled.packet(csgClipPacket).plan->dependencyCount, 0u);

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
    EXPECT_TRUE(csgClipRecorded);

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
    EXPECT_TRUE(transaction.packetToken(csgClipPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The prepared material draw routes now receive their common mesh-view/material states from graph declarations.
// This real-Vulkan packet contains a getter-only callback, so any native state setup would be unable to mask a
// missing ConstantBuffer/ShaderResource transition.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedMaterialFrameEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto meshView = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    const auto makeMaterialBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto materialInstances = makeMaterialBuffer();
    auto materialTyped = makeMaterialBuffer();
    ASSERT_NE(meshView.get(), nullptr);
    ASSERT_NE(materialInstances.get(), nullptr);
    ASSERT_NE(materialTyped.get(), nullptr);

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
    const GpuGraphResourceId meshViewResource = importBuffer(
        meshView,
        Name("tests/descriptor_buffer/material_frame_mesh_view"),
        "Material Frame Mesh View"
    );
    const GpuGraphResourceId materialInstancesResource = importBuffer(
        materialInstances,
        Name("tests/descriptor_buffer/material_frame_instances"),
        "Material Frame Instances"
    );
    const GpuGraphResourceId materialTypedResource = importBuffer(
        materialTyped,
        Name("tests/descriptor_buffer/material_frame_typed"),
        "Material Frame Typed"
    );
    ASSERT_TRUE(meshViewResource.valid());
    ASSERT_TRUE(materialInstancesResource.valid());
    ASSERT_TRUE(materialTypedResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint materialScheduling;
    materialScheduling.cost = GpuTaskCostHint::Medium;
    materialScheduling.forceSubmissionBoundary = true;
    materialScheduling.allowPacketMerge = false;
    const GpuTaskResourceUse materialUses[] = {
        GpuTaskResourceUse{
            .resource = meshViewResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialInstancesResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = materialTypedResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc materialDesc;
    materialDesc
        .setIdentity(Name("tests/descriptor_buffer/material_frame_entry"))
        .setMarkerLabel("Material Frame Entry")
        .setQueue(graphicsQueue)
        .setScheduling(materialScheduling)
        .setResourceUses(materialUses, LengthOf(materialUses))
    ;
    bool materialRecorded = false;
    const GpuTaskId materialTask = graph.addTask<NativePacketMaterialFrameEntryProbeTask>(
        materialDesc,
        NativePacketMaterialFrameEntryProbeTask::Payload{
            .meshView = meshView.get(),
            .materialInstances = materialInstances.get(),
            .materialTyped = materialTyped.get(),
            .recorded = &materialRecorded,
        }
    );
    ASSERT_TRUE(materialTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/material_frame_entry_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId materialPacket = views.compiled.packetForTask(materialTask);
    ASSERT_TRUE(materialPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledMaterial = views.compiled.findTask(materialTask);
    ASSERT_TRUE(compiledMaterial.valid());
    ASSERT_EQ(compiledMaterial.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial.plan->prologueBarrierCount, 3u);
    const GpuCompiledBarrier* const materialBarriers = views.compiled.findTask(materialTask).prologueBarriers;
    ASSERT_NE(materialBarriers, nullptr);
    const auto hasTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask expectedState){
        for(u32 barrierIndex = 0u; barrierIndex < compiledMaterial.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = materialBarriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::Common
                && barrier.after == expectedState
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(meshViewResource, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasTransition(materialInstancesResource, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(materialTypedResource, ResourceStates::ShaderResource));

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
    EXPECT_TRUE(materialRecorded);

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
    EXPECT_TRUE(transaction.packetToken(materialPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// A prepared material stream must reuse a source buffer imported earlier by preflight even when that producer chose
// a different graph identity. The real packet only reads the tracker, proving both the reuse and Common-to-SRV
// transition occur before material-native code could invoke its compatibility normalizer.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedMaterialGeometryEntryStatesReusePreflightImport){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto geometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(geometry.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId preflightGeometryResource = graph.importBuffer(
        geometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/material_geometry_preflight"))
            .setMarkerLabel("Preflight Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(preflightGeometryResource.valid());
    GpuGraphResourceId materialGeometryResource;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        materialGeometryResource = declarations.findImportedBuffer(geometry);
    }
    ASSERT_TRUE(materialGeometryResource.valid());
    EXPECT_EQ(materialGeometryResource, preflightGeometryResource);

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint materialScheduling;
    materialScheduling.cost = GpuTaskCostHint::Medium;
    materialScheduling.forceSubmissionBoundary = true;
    materialScheduling.allowPacketMerge = false;
    const GpuGraphResourceSetId materialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/material_geometry_set"))
            .setMarkerLabel("Material Geometry Set")
            .setMembers(&materialGeometryResource, 1u)
    );
    ASSERT_TRUE(materialGeometrySet.valid());
    const GpuTaskResourceSetUse materialSetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = materialGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc materialDesc;
    materialDesc
        .setIdentity(Name("tests/descriptor_buffer/material_geometry_entry"))
        .setMarkerLabel("Material Geometry Entry")
        .setQueue(graphicsQueue)
        .setScheduling(materialScheduling)
        .setResourceSetUses(materialSetUses, LengthOf(materialSetUses))
    ;
    bool materialRecorded = false;
    const GpuTaskId materialTask = graph.addTask<NativePacketMaterialGeometryEntryProbeTask>(
        materialDesc,
        NativePacketMaterialGeometryEntryProbeTask::Payload{
            .geometry = geometry.get(),
            .recorded = &materialRecorded,
        }
    );
    ASSERT_TRUE(materialTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/material_geometry_entry_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId materialPacket = views.compiled.packetForTask(materialTask);
    ASSERT_TRUE(materialPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledMaterial = views.compiled.findTask(materialTask);
    ASSERT_TRUE(compiledMaterial.valid());
    ASSERT_EQ(compiledMaterial.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const materialBarrier = views.compiled.findTask(materialTask).prologueBarriers;
    ASSERT_NE(materialBarrier, nullptr);
    EXPECT_EQ(materialBarrier[0].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(materialBarrier[0].resource, materialGeometryResource);
    EXPECT_EQ(materialBarrier[0].before, ResourceStates::Common);
    EXPECT_EQ(materialBarrier[0].after, ResourceStates::ShaderResource);

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
    EXPECT_TRUE(materialRecorded);

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
    EXPECT_TRUE(transaction.packetToken(materialPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Material sampled textures are selected through global heap slots just like geometry. A preflight producer may
// import one under its own identity, so the prepared material set must reuse that typed texture and establish the
// ShaderResource state before native material code can sample the descriptor.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedMaterialSampledTextureEntryStatesReusePreflightImport){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);
    Texture* const initialTextures[] = { texture.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId preflightTextureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/material_sampled_texture_preflight"))
            .setMarkerLabel("Preflight Material Sampled Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(preflightTextureResource.valid());
    GpuGraphResourceId materialSampledTextureResource;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        materialSampledTextureResource = declarations.findImportedTexture(texture);
    }
    ASSERT_TRUE(materialSampledTextureResource.valid());
    EXPECT_EQ(materialSampledTextureResource, preflightTextureResource);

    const GpuGraphResourceSetId materialSampledTextureSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/material_sampled_texture_set"))
            .setMarkerLabel("Material Sampled Texture Set")
            .setMembers(&materialSampledTextureResource, 1u)
    );
    ASSERT_TRUE(materialSampledTextureSet.valid());
    const GpuTaskResourceSetUse materialSetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = materialSampledTextureSet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint materialScheduling;
    materialScheduling.cost = GpuTaskCostHint::Medium;
    materialScheduling.forceSubmissionBoundary = true;
    materialScheduling.allowPacketMerge = false;
    GpuTaskDesc materialDesc;
    materialDesc
        .setIdentity(Name("tests/descriptor_buffer/material_sampled_texture_entry"))
        .setMarkerLabel("Material Sampled Texture Entry")
        .setQueue(graphicsQueue)
        .setScheduling(materialScheduling)
        .setResourceSetUses(materialSetUses, LengthOf(materialSetUses))
    ;
    ResourceStates::Mask observedTextureState = ResourceStates::Unknown;
    bool materialRecorded = false;
    const GpuTaskId materialTask = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        materialDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &observedTextureState,
            .recorded = &materialRecorded,
        }
    );
    ASSERT_TRUE(materialTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/material_sampled_texture_entry_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId materialPacket = views.compiled.packetForTask(materialTask);
    ASSERT_TRUE(materialPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuCompiledTaskView compiledMaterial = views.compiled.findTask(materialTask);
    ASSERT_TRUE(compiledMaterial.valid());
    ASSERT_EQ(compiledMaterial.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const materialBarrier = views.compiled.findTask(materialTask).prologueBarriers;
    ASSERT_NE(materialBarrier, nullptr);
    EXPECT_EQ(materialBarrier[0].type, GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(materialBarrier[0].resource, materialSampledTextureResource);
    EXPECT_EQ(materialBarrier[0].before, ResourceStates::Common);
    EXPECT_EQ(materialBarrier[0].after, ResourceStates::ShaderResource);

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
    EXPECT_TRUE(materialRecorded);
    EXPECT_EQ(observedTextureState, ResourceStates::ShaderResource);

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
    EXPECT_TRUE(transaction.packetToken(materialPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

