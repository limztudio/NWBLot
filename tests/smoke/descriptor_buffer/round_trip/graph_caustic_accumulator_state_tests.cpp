// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware caustics shares the descriptor-visible static producer batch with its Graphics-prefix dependency. Its
// fresh accumulator clear and warm temporal decay are graph-owned. Both typed clears must reach this producer as
// UnorderedAccess handoffs.
struct NativePacketHardwareCausticsEntryProbeTask{
    static constexpr u32 s_ShaderBufferCount = 6u;
    static constexpr u32 s_ConstantBufferCount = 4u;
    static constexpr u32 s_ShaderTextureCount = 2u;

    struct Payload{
        Buffer* shaderBuffers[s_ShaderBufferCount] = {};
        Buffer* constantBuffers[s_ConstantBufferCount] = {};
        Texture* shaderTextures[s_ShaderTextureCount] = {};
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


// Hardware caustics' ray-tracing producer receives its static heap inputs from the Graphics-prefix packet. The
// callback seam is getter-only, so this verifies that the normal graph route cannot depend on native transitions to
// repair the descriptor-visible states after an incompatible producer layout.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedHardwareCausticsEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 shaderBufferCount = NativePacketHardwareCausticsEntryProbeTask::s_ShaderBufferCount;
    constexpr u32 constantBufferCount = NativePacketHardwareCausticsEntryProbeTask::s_ConstantBufferCount;
    constexpr u32 shaderTextureCount = NativePacketHardwareCausticsEntryProbeTask::s_ShaderTextureCount;
    constexpr u32 meshAttributesBufferIndex = 0u;
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
    TextureHandle shaderTextures[shaderTextureCount];
    shaderTextures[0u] = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    shaderTextures[1u] = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::D32S8)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    for(u32 textureIndex = 0u; textureIndex < shaderTextureCount; ++textureIndex)
        ASSERT_NE(shaderTextures[textureIndex].get(), nullptr);
    const TextureHandle irradiance = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    const TextureHandle accumulator = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(irradiance.get(), nullptr);
    ASSERT_NE(accumulator.get(), nullptr);
    Texture* const initialTextures[] = {
        shaderTextures[0u].get(),
        shaderTextures[1u].get(),
        irradiance.get(),
        accumulator.get(),
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
        { Name("tests/descriptor_buffer/hardware_caustics_mesh_attributes"), "Hardware Mesh Attributes" },
        { Name("tests/descriptor_buffer/hardware_caustics_instance_materials"), "Shadow Instance Materials" },
        { Name("tests/descriptor_buffer/hardware_caustics_typed_materials"), "Shadow Typed Materials" },
        { Name("tests/descriptor_buffer/hardware_caustics_instances"), "Shadow Instances" },
        { Name("tests/descriptor_buffer/hardware_caustics_emission_targets"), "Caustic Emission Targets" },
        { Name("tests/descriptor_buffer/hardware_caustics_lights"), "Deferred Lights" },
    };
    const BufferImportDesc constantBufferImports[constantBufferCount] = {
        { Name("tests/descriptor_buffer/hardware_caustics_mesh_view"), "Mesh View" },
        { Name("tests/descriptor_buffer/hardware_caustics_bindless_slots"), "Deferred Bindless Slots" },
        { Name("tests/descriptor_buffer/hardware_caustics_material_context_slots"), "Ray-Trace Material Context Slots" },
        { Name("tests/descriptor_buffer/hardware_caustics_scene_shading"), "Scene Shading" },
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
        Name("tests/descriptor_buffer/hardware_caustics_world_position"),
        "World Position"
    );
    const GpuGraphResourceId depthResource = importTexture(
        shaderTextures[1u],
        Name("tests/descriptor_buffer/hardware_caustics_depth"),
        "Depth"
    );
    const GpuGraphResourceId irradianceResource = importTexture(
        irradiance,
        Name("tests/descriptor_buffer/hardware_caustics_irradiance"),
        "Caustic Irradiance"
    );
    const GpuGraphResourceId accumulatorResource = importTexture(
        accumulator,
        Name("tests/descriptor_buffer/hardware_caustics_accumulator"),
        "Caustic Accumulator"
    );
    ASSERT_TRUE(worldPositionResource.valid());
    ASSERT_TRUE(depthResource.valid());
    ASSERT_TRUE(irradianceResource.valid());
    ASSERT_TRUE(accumulatorResource.valid());

    const GpuGraphResourceSetId meshAttributesSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/hardware_caustics_mesh_attributes"))
            .setMarkerLabel("Hardware Caustics Mesh Attributes")
            .setMembers(&shaderBufferResources[meshAttributesBufferIndex], 1u)
    );
    ASSERT_TRUE(meshAttributesSet.valid());
    const GpuTaskResourceSetUse meshAttributesSetUses[] = {
        {
            .resourceSet = meshAttributesSet,
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
    const GpuQueueRequest graphicsTransferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Alloc::ScratchArena resourceUseArena(Name("tests/descriptor_buffer/hardware_caustics_entry_state_resource_uses"));
    Vector<GpuTaskResourceUse, Alloc::ScratchArena> prefixUses(resourceUseArena);
    prefixUses.reserve(shaderBufferCount + constantBufferCount + shaderTextureCount);
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    prefixUses.push_back({ .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::RenderTarget, .access = GpuTaskResourceAccess::Write });
    prefixUses.push_back({ .resource = depthResource, .range = {}, .requiredState = ResourceStates::DepthWrite, .access = GpuTaskResourceAccess::Write });
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/hardware_caustics_prefix"))
        .setMarkerLabel("Hardware Caustics Prefix")
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

    GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_hardware_caustics_irradiance_clear"))
        .setMarkerLabel("Hardware Caustics Irradiance Clear")
        .setQueue(graphicsTransferQueue)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&prefixTask, 1u)
    ;
    GpuClearTextureTaskDesc irradianceClear;
    irradianceClear.destination = irradianceResource;
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
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_hardware_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
        .setQueue(graphicsTransferQueue)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
    ;
    QueueSubmissionToken accumulatorBootstrapClearAcceptedToken;
    GpuClearTextureTaskDesc accumulatorBootstrapClear;
    accumulatorBootstrapClear.destination = accumulatorResource;
    accumulatorBootstrapClear.subresources = TextureSubresourceSet(0u, 1u, 0u, 3u);
    accumulatorBootstrapClear.valueType = GpuClearTextureTaskValueType::UInt;
    accumulatorBootstrapClear.uintValue = UIntColor(0u);
    accumulatorBootstrapClear.acceptedToken = &accumulatorBootstrapClearAcceptedToken;
    const GpuTaskId accumulatorBootstrapClearTask = graph.addClearTextureTask(
        accumulatorBootstrapClearDesc,
        accumulatorBootstrapClear
    );
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    Vector<GpuTaskResourceUse, Alloc::ScratchArena> causticsUses(resourceUseArena);
    causticsUses.reserve(shaderBufferCount - 1u + constantBufferCount + shaderTextureCount);
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        if(bufferIndex != meshAttributesBufferIndex)
            causticsUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read });
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        causticsUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read });
    causticsUses.push_back({ .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read });
    causticsUses.push_back({ .resource = depthResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read });
    causticsUses.push_back({ .resource = accumulatorResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite });
    causticsUses.push_back({ .resource = irradianceResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write });
    GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    GpuTaskDesc causticsDesc;
    causticsDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsQueue)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(causticsUses.data(), causticsUses.size())
        .setResourceSetUses(meshAttributesSetUses, LengthOf(meshAttributesSetUses))
    ;
    NativePacketHardwareCausticsEntryProbeTask::Payload causticsPayload{};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        causticsPayload.shaderBuffers[bufferIndex] = shaderBuffers[bufferIndex].get();
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        causticsPayload.constantBuffers[bufferIndex] = constantBuffers[bufferIndex].get();
    for(u32 textureIndex = 0u; textureIndex < shaderTextureCount; ++textureIndex)
        causticsPayload.shaderTextures[textureIndex] = shaderTextures[textureIndex].get();
    causticsPayload.irradiance = irradiance.get();
    causticsPayload.accumulator = accumulator.get();
    bool causticsRecorded = false;
    causticsPayload.recorded = &causticsRecorded;
    const GpuTaskId causticsTask = graph.addTask<NativePacketHardwareCausticsEntryProbeTask>(
        causticsDesc,
        Move(causticsPayload)
    );
    ASSERT_TRUE(causticsTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/hardware_caustics_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId irradianceClearPacket = views.compiled.packetForTask(irradianceClearTask);
    const GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        views.compiled.packetForTask(accumulatorBootstrapClearTask);
    const GpuSubmissionPacketId causticsPacket = views.compiled.packetForTask(causticsTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    const GpuCompiledTaskView compiledCaustics = views.compiled.findTask(causticsTask);
    ASSERT_TRUE(compiledCaustics.valid());
    const GpuCompiledBarrier* const causticsBarriers = views.compiled.findTask(causticsTask).prologueBarriers;
    ASSERT_NE(causticsBarriers, nullptr);
    const auto hasCausticsTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCaustics.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasCausticsTransition(depthResource, ResourceStates::DepthWrite, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasCausticsTransition(worldPositionResource, ResourceStates::RenderTarget, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasCausticsTransition(shaderBufferResources[0u], ResourceStates::CopyDest, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasCausticsTransition(constantBufferResources[0u], ResourceStates::CopyDest, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasCausticsTransition(constantBufferResources[3u], ResourceStates::CopyDest, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasCausticsTransition(accumulatorResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasCausticsTransition(irradianceResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    ASSERT_EQ(views.compiled.packet(causticsPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(causticsPacket).dependencies[0u].producer, prefixPacket);

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
    EXPECT_TRUE(causticsRecorded);

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
    EXPECT_TRUE(transaction.packetToken(causticsPacket).valid());
    EXPECT_TRUE(accumulatorBootstrapClearAcceptedToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// A non-temporal accumulator begins each producer packet as ShaderResource history. The typed clear must transition
// it to CopyDest, and the producer callback must receive the compiler-owned CopyDest -> UAV handoff without a local
// bridge. Successful Vulkan recording proves the native clear and the selected photon producer share that packet.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedNonTemporalCausticAccumulatorClearRecordsBeforePhotonProducer){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 layerCount = 3u;
    const TextureHandle accumulator = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(layerCount)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(accumulator.get(), nullptr);
    Texture* const initialTextures[] = { accumulator.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::ShaderResource
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId accumulatorResource = graph.importTexture(
        accumulator,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/non_temporal_caustics_accumulator"))
            .setMarkerLabel("Non-Temporal Caustic Accumulator")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(accumulatorResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsTransferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const TextureSubresourceSet accumulatorSubresources(0u, 1u, 0u, layerCount);
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/non_temporal_caustics_accumulator_clear"))
        .setMarkerLabel("Caustic Accumulator Clear")
        .setQueue(graphicsTransferQueue)
        .setScheduling(clearScheduling)
    ;
    QueueSubmissionToken accumulatorClearAcceptedToken;
    GpuClearTextureTaskDesc clear;
    clear.destination = accumulatorResource;
    clear.subresources = accumulatorSubresources;
    clear.valueType = GpuClearTextureTaskValueType::UInt;
    clear.uintValue = UIntColor(0u);
    clear.acceptedToken = &accumulatorClearAcceptedToken;
    const GpuTaskId clearTask = graph.addClearTextureTask(clearDesc, clear);
    ASSERT_TRUE(clearTask.valid());

    const GpuTaskResourceUse accumulatorUses[] = {
        GpuTaskResourceUse{
            .resource = accumulatorResource,
            .range = GpuTaskResourceRange{ .textureSubresources = accumulatorSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint producerScheduling = clearScheduling;
    producerScheduling.mergeWithPrevious = true;
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/non_temporal_caustics_photon_producer"))
        .setMarkerLabel("Hardware Caustic Photons")
        .setQueue(graphicsQueue)
        .setScheduling(producerScheduling)
        .setDependencies(&clearTask, 1u)
        .setResourceUses(accumulatorUses, LengthOf(accumulatorUses))
    ;
    bool producerRecorded = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketCausticAccumulatorDecayProbeTask>(
        producerDesc,
        NativePacketCausticAccumulatorDecayProbeTask::Payload{
            .accumulator = accumulator.get(),
            .layerCount = layerCount,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producerTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/non_temporal_caustics_clear_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId clearPacket = views.compiled.packetForTask(clearTask);
    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    ASSERT_TRUE(clearPacket.valid());
    EXPECT_EQ(producerPacket, clearPacket);
    const GpuTaskQueueAssignment* const clearAssignment = assignments.find(clearTask);
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    ASSERT_NE(clearAssignment, nullptr);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);

    const GpuCompiledTaskView compiledClear = views.compiled.findTask(clearTask);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    ASSERT_TRUE(compiledClear.valid());
    ASSERT_TRUE(compiledProducer.valid());
    const GpuCompiledBarrier* const clearBarriers = views.compiled.findTask(clearTask).prologueBarriers;
    const GpuCompiledBarrier* const producerBarriers = views.compiled.findTask(producerTask).prologueBarriers;
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(producerBarriers, nullptr);
    bool hasClearTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledClear.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = clearBarriers[barrierIndex];
        hasClearTransition = hasClearTransition || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulatorResource
            && barrier.range.textureSubresources == accumulatorSubresources
            && barrier.before == ResourceStates::ShaderResource
            && barrier.after == ResourceStates::CopyDest
        );
    }
    bool hasProducerTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledProducer.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = producerBarriers[barrierIndex];
        hasProducerTransition = hasProducerTransition || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulatorResource
            && barrier.range.textureSubresources == accumulatorSubresources
            && barrier.before == ResourceStates::CopyDest
            && barrier.after == ResourceStates::UnorderedAccess
        );
    }
    EXPECT_TRUE(hasClearTransition);
    EXPECT_TRUE(hasProducerTransition);

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
    EXPECT_TRUE(producerRecorded);

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
    EXPECT_TRUE(transaction.packetToken(clearPacket).valid());
    EXPECT_TRUE(accumulatorClearAcceptedToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// A warm temporal accumulator enters this packet as last frame's ShaderResource history. The graph-owned decay
// transitions it to UAV, then the selected photon producer must receive a compiler-owned UAV-to-UAV ordering fence.
// Both callbacks are getter-only, so successful Vulkan recording proves there is no local transition bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedWarmCausticAccumulatorDecayRecordsBeforePhotonProducer){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 layerCount = 3u;
    const TextureHandle accumulator = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(layerCount)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(accumulator.get(), nullptr);
    Texture* const initialTextures[] = { accumulator.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::ShaderResource
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId accumulatorResource = graph.importTexture(
        accumulator,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/warm_caustics_accumulator"))
            .setMarkerLabel("Warm Caustic Accumulator")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(accumulatorResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const TextureSubresourceSet accumulatorSubresources(0u, 1u, 0u, layerCount);
    const GpuTaskResourceUse accumulatorUses[] = {
        GpuTaskResourceUse{
            .resource = accumulatorResource,
            .range = GpuTaskResourceRange{ .textureSubresources = accumulatorSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint decayScheduling;
    decayScheduling.cost = GpuTaskCostHint::Tiny;
    decayScheduling.allowPacketMerge = true;
    GpuTaskDesc decayDesc;
    decayDesc
        .setIdentity(Name("tests/descriptor_buffer/warm_caustics_accumulator_decay"))
        .setMarkerLabel("Caustic Accumulator Decay")
        .setQueue(graphicsQueue)
        .setScheduling(decayScheduling)
        .setResourceUses(accumulatorUses, LengthOf(accumulatorUses))
    ;
    bool decayRecorded = false;
    const GpuTaskId decayTask = graph.addTask<NativePacketCausticAccumulatorDecayProbeTask>(
        decayDesc,
        NativePacketCausticAccumulatorDecayProbeTask::Payload{
            .accumulator = accumulator.get(),
            .layerCount = layerCount,
            .recorded = &decayRecorded,
        }
    );
    ASSERT_TRUE(decayTask.valid());

    GpuTaskSchedulingHint producerScheduling = decayScheduling;
    producerScheduling.mergeWithPrevious = true;
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/warm_caustics_photon_producer"))
        .setMarkerLabel("Hardware Caustic Photons")
        .setQueue(graphicsQueue)
        .setScheduling(producerScheduling)
        .setDependencies(&decayTask, 1u)
        .setResourceUses(accumulatorUses, LengthOf(accumulatorUses))
    ;
    bool producerRecorded = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketCausticAccumulatorDecayProbeTask>(
        producerDesc,
        NativePacketCausticAccumulatorDecayProbeTask::Payload{
            .accumulator = accumulator.get(),
            .layerCount = layerCount,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producerTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/warm_caustics_decay_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId decayPacket = views.compiled.packetForTask(decayTask);
    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    ASSERT_TRUE(decayPacket.valid());
    EXPECT_EQ(producerPacket, decayPacket);
    const GpuTaskQueueAssignment* const decayAssignment = assignments.find(decayTask);
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    ASSERT_NE(decayAssignment, nullptr);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(decayAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);

    const GpuCompiledTaskView compiledDecay = views.compiled.findTask(decayTask);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    ASSERT_TRUE(compiledDecay.valid());
    ASSERT_TRUE(compiledProducer.valid());
    const GpuCompiledBarrier* const decayBarriers = views.compiled.findTask(decayTask).prologueBarriers;
    const GpuCompiledBarrier* const producerBarriers = views.compiled.findTask(producerTask).prologueBarriers;
    ASSERT_NE(decayBarriers, nullptr);
    ASSERT_NE(producerBarriers, nullptr);
    bool hasDecayTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledDecay.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = decayBarriers[barrierIndex];
        hasDecayTransition = hasDecayTransition || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulatorResource
            && barrier.range.textureSubresources == accumulatorSubresources
            && barrier.before == ResourceStates::ShaderResource
            && barrier.after == ResourceStates::UnorderedAccess
        );
    }
    bool hasProducerUavFence = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledProducer.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = producerBarriers[barrierIndex];
        hasProducerUavFence = hasProducerUavFence || (
            barrier.type == GpuCompiledBarrierType::TextureUav
            && barrier.resource == accumulatorResource
            && barrier.range.textureSubresources == accumulatorSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::UnorderedAccess
        );
    }
    EXPECT_TRUE(hasDecayTransition);
    EXPECT_TRUE(hasProducerUavFence);

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
    EXPECT_TRUE(decayRecorded);
    EXPECT_TRUE(producerRecorded);

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
    EXPECT_TRUE(transaction.packetToken(decayPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

