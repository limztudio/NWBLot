// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GraphOwnedShadowPrepareTask{
    struct Payload{
        Buffer* bindlessSlots = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.bindlessSlots)
            return false;
        // The packet prologue owns selector state. This task deliberately emits no native transition: its retained
        // descriptor-visible state must already be available through the compiled graph handoff.
        const bool ready = commandList.getBufferState(payload.bindlessSlots) == ResourceStates::ConstantBuffer;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// A normalized prelude transitions shared inputs once before independently recorded primary command lists begin.
// Each branch can therefore import the same ShaderResource state without emitting another stale RenderTarget ->
// ShaderResource barrier. The fan-in preserves their disjoint output states for the later ordered consumer.
TEST_F(DescriptorBufferRoundTripTest, NormalizedStatePreludeFansInIndependentBranches){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sharedInput = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    auto firstOutput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto secondOutput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sharedInput.get(), nullptr);
    ASSERT_NE(firstOutput.get(), nullptr);
    ASSERT_NE(secondOutput.get(), nullptr);

    CommandListResourceStateHandoff producerState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff normalizedState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff firstBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff secondBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff fanInState(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    auto prelude = device.createCommandList();
    auto firstBranch = device.createCommandList();
    auto secondBranch = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(prelude.get(), nullptr);
    ASSERT_NE(firstBranch.get(), nullptr);
    ASSERT_NE(secondBranch.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    producer->open();
    producer->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::RenderTarget);
    producer->close(&producerState);
    ASSERT_TRUE(producerState.valid());

    prelude->open(&producerState);
    prelude->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prelude->close(&normalizedState);
    ASSERT_TRUE(normalizedState.valid());

    Latch recordingStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::JobHandle firstJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        firstBranch->open(&normalizedState);
        firstBranch->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
        firstBranch->setBufferState(firstOutput.get(), ResourceStates::UnorderedAccess);
        firstBranch->close(&firstBranchState);
        firstRecorded = firstBranchState.valid() && firstBranch->hasCommandBuffer();
    });
    const Graphics::JobHandle secondJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        secondBranch->open(&normalizedState);
        secondBranch->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
        secondBranch->setBufferState(secondOutput.get(), ResourceStates::UnorderedAccess);
        secondBranch->close(&secondBranchState);
        secondRecorded = secondBranchState.valid() && secondBranch->hasCommandBuffer();
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitJob(firstJob);
    graphics.waitJob(secondJob);
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    const CommandListResourceStateHandoff* branchStates[] = { &firstBranchState, &secondBranchState };
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/state_fan_in_scratch"));
    ASSERT_TRUE(fanInState.buildFanIn(normalizedState, branchStates, 2u, fanInScratchArena));
    ASSERT_TRUE(fanInState.valid());

    const ArenaMemoryStats warmedOwningArenaStats = DescriptorBufferRoundTripTest::arena().memoryStats();
    const ArenaMemoryStats warmedScratchArenaStats = fanInScratchArena.memoryStats();
    EXPECT_EQ(warmedScratchArenaStats.usedBytes, 0u);
    for(u32 repeat = 0u; repeat < 32u; ++repeat)
        ASSERT_TRUE(fanInState.buildFanIn(normalizedState, branchStates, 2u, fanInScratchArena));
    const ArenaMemoryStats reusedOwningArenaStats = DescriptorBufferRoundTripTest::arena().memoryStats();
    const ArenaMemoryStats reusedScratchArenaStats = fanInScratchArena.memoryStats();
    EXPECT_EQ(reusedOwningArenaStats.reservedBytes, warmedOwningArenaStats.reservedBytes);
    EXPECT_EQ(reusedOwningArenaStats.usedBytes, warmedOwningArenaStats.usedBytes);
    EXPECT_EQ(reusedOwningArenaStats.peakUsedBytes, warmedOwningArenaStats.peakUsedBytes);
    EXPECT_EQ(reusedOwningArenaStats.allocationCount, warmedOwningArenaStats.allocationCount);
    EXPECT_EQ(reusedOwningArenaStats.reallocationCount, warmedOwningArenaStats.reallocationCount);
    EXPECT_EQ(reusedOwningArenaStats.deallocationCount, warmedOwningArenaStats.deallocationCount);
    EXPECT_EQ(reusedScratchArenaStats.reservedBytes, warmedScratchArenaStats.reservedBytes);
    EXPECT_EQ(reusedScratchArenaStats.usedBytes, 0u);
    EXPECT_EQ(reusedScratchArenaStats.peakUsedBytes, warmedScratchArenaStats.peakUsedBytes);
    EXPECT_EQ(reusedScratchArenaStats.reallocationCount, warmedScratchArenaStats.reallocationCount);
    EXPECT_GT(reusedScratchArenaStats.allocationCount, warmedScratchArenaStats.allocationCount);
    EXPECT_EQ(
        reusedScratchArenaStats.allocationCount - warmedScratchArenaStats.allocationCount,
        reusedScratchArenaStats.deallocationCount - warmedScratchArenaStats.deallocationCount
    );

    consumer->open(&fanInState);
    consumer->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
    EXPECT_EQ(consumer->getBufferState(firstOutput.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(consumer->getBufferState(secondOutput.get()), ResourceStates::UnorderedAccess);
    consumer->setBufferState(firstOutput.get(), ResourceStates::ShaderResource);
    consumer->setBufferState(secondOutput.get(), ResourceStates::ShaderResource);
    consumer->close();

    CommandList* commandLists[] = {
        producer.get(),
        prelude.get(),
        firstBranch.get(),
        secondBranch.get(),
        consumer.get()
    };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 5u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// One compiled graph owns Shadow Prepare, the Graphics Prefix, every effect, and Present. The selector retains its
// descriptor-visible ConstantBuffer state at native packet close, so graph-owned packet seeds carry that state
// through the prefix and effects without any renderer-owned serial state snapshot or record-time state bridge.
TEST_F(DescriptorBufferRoundTripTest, RendererGraphShadowPrepareStateChainThroughComputePresent){
    auto& device = DescriptorBufferRoundTripTest::device();
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
    const auto makeStorageTarget = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Unknown)
        );
    };
    const auto makeSetupBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeBindlessSlotsBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsConstantBuffer(true)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
                .enableAutomaticStateTracking(ResourceStates::ConstantBuffer)
        );
    };

    auto prefixBuffer = makeSetupBuffer();
    auto slotsBuffer = makeBindlessSlotsBuffer();
    auto gbuffer = makeGbufferTarget();
    auto shadowVisibility = makeStorageTarget();
    auto causticIrradiance = makeStorageTarget();
    auto surfelIrradiance = makeStorageTarget();
    auto opaqueColor = makeStorageTarget();
    auto compositeColor = makeStorageTarget();
    ASSERT_NE(prefixBuffer.get(), nullptr);
    ASSERT_NE(slotsBuffer.get(), nullptr);
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const auto& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const auto importTexture = [&graph](const auto& texture, const Name& identity, const AStringView label){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    const GpuGraphResourceId prefixBufferResource = importBuffer(
        prefixBuffer,
        Name("tests/descriptor_buffer/shadow_chain_prefix_buffer"),
        "Graphics Prefix Buffer"
    );
    const GpuGraphResourceId slotsResource = graph.importBuffer(
        slotsBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shadow_chain_bindless_slots"))
            .setMarkerLabel("Bindless Slots")
            .setType(GpuGraphResourceType::Buffer)
            // This is both the allocation-time and retained packet-boundary state of the current selector.
            .setInitialState(ResourceStates::ConstantBuffer)
    );
    const GpuGraphResourceId gbufferResource = importTexture(
        gbuffer,
        Name("tests/descriptor_buffer/shadow_chain_gbuffer"),
        "G-Buffer"
    );
    const GpuGraphResourceId shadowVisibilityResource = importTexture(
        shadowVisibility,
        Name("tests/descriptor_buffer/shadow_chain_shadow_visibility"),
        "Shadow Visibility"
    );
    const GpuGraphResourceId causticIrradianceResource = importTexture(
        causticIrradiance,
        Name("tests/descriptor_buffer/shadow_chain_caustic_irradiance"),
        "Caustic Irradiance"
    );
    const GpuGraphResourceId surfelIrradianceResource = importTexture(
        surfelIrradiance,
        Name("tests/descriptor_buffer/shadow_chain_surfel_irradiance"),
        "Surfel Irradiance"
    );
    const GpuGraphResourceId opaqueColorResource = importTexture(
        opaqueColor,
        Name("tests/descriptor_buffer/shadow_chain_opaque_color"),
        "Opaque Color"
    );
    const GpuGraphResourceId compositeColorResource = importTexture(
        compositeColor,
        Name("tests/descriptor_buffer/shadow_chain_composite_color"),
        "Composite Color"
    );
    ASSERT_TRUE(prefixBufferResource.valid());
    ASSERT_TRUE(slotsResource.valid());
    ASSERT_TRUE(gbufferResource.valid());
    ASSERT_TRUE(shadowVisibilityResource.valid());
    ASSERT_TRUE(causticIrradianceResource.valid());
    ASSERT_TRUE(surfelIrradianceResource.valid());
    ASSERT_TRUE(opaqueColorResource.valid());
    ASSERT_TRUE(compositeColorResource.valid());

    const GpuQueueRequest graphicsQueueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueueRequest{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        true,
    };
    GpuTaskSchedulingHint packetScheduling;
    packetScheduling.cost = GpuTaskCostHint::Large;
    packetScheduling.forceSubmissionBoundary = true;
    packetScheduling.allowPacketMerge = false;

    const auto addProbeTask = [
        &graph,
        &packetScheduling
    ](
        const Name& identity,
        const AStringView label,
        const GpuQueueRequest& queue,
        const GpuTaskId* const dependencies,
        const usize dependencyCount,
        const GpuTaskResourceUse* const resourceUses,
        const usize resourceUseCount,
        Buffer* const buffer,
        const ResourceStates::Mask expectedBufferState,
        Texture* const texture,
        const ResourceStates::Mask expectedTextureState,
        bool* const recorded
    ){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(packetScheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(resourceUses, resourceUseCount)
        ;
        return graph.addTask<NativePacketPrefixTask>(
            desc,
            NativePacketPrefixTask::Payload{
                .buffer = buffer,
                .expectedState = expectedBufferState,
                .texture = texture,
                .expectedTextureState = expectedTextureState,
                .recorded = recorded,
            }
        );
    };

    const GpuTaskResourceUse shadowPrepareUses[] = {
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_chain_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsQueueRequest)
        .setScheduling(packetScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    bool shadowPrepareRecorded = false;
    const GpuTaskId shadowPrepareTask = graph.addTask<GraphOwnedShadowPrepareTask>(
        shadowPrepareDesc,
        GraphOwnedShadowPrepareTask::Payload{
            .bindlessSlots = slotsBuffer.get(),
            .recorded = &shadowPrepareRecorded,
        }
    );
    ASSERT_TRUE(shadowPrepareTask.valid());

    // Prefix receives the exact final slot snapshot from Shadow Prepare through its declared read, not through an
    // opaque serial seed. Subsequent effects continue the same compiler-owned chain.
    const GpuTaskResourceUse graphicsPrefixUses[] = {
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = prefixBufferResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool graphicsPrefixRecorded = false;
    const GpuTaskId graphicsPrefixTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_graphics_prefix"),
        "Graphics Prefix",
        graphicsQueueRequest,
        &shadowPrepareTask,
        1u,
        graphicsPrefixUses,
        LengthOf(graphicsPrefixUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &graphicsPrefixRecorded
    );
    ASSERT_TRUE(graphicsPrefixTask.valid());

    const GpuTaskResourceUse shadowVisibilityUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool shadowVisibilityRecorded = false;
    const GpuTaskId shadowVisibilityTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_shadow_visibility"),
        "Shadow Visibility",
        computeQueueRequest,
        &graphicsPrefixTask,
        1u,
        shadowVisibilityUses,
        LengthOf(shadowVisibilityUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &shadowVisibilityRecorded
    );
    ASSERT_TRUE(shadowVisibilityTask.valid());

    const GpuTaskResourceUse causticsUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = causticIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool causticsRecorded = false;
    const GpuTaskId causticsTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_caustics"),
        "Software Caustics",
        computeQueueRequest,
        &shadowVisibilityTask,
        1u,
        causticsUses,
        LengthOf(causticsUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &causticsRecorded
    );
    ASSERT_TRUE(causticsTask.valid());

    const GpuTaskResourceUse surfelGiUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = surfelIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool surfelGiRecorded = false;
    const GpuTaskId surfelGiTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_surfel_gi"),
        "Surfel GI",
        computeQueueRequest,
        &causticsTask,
        1u,
        surfelGiUses,
        LengthOf(surfelGiUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &surfelGiRecorded
    );
    ASSERT_TRUE(surfelGiTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = causticIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = surfelIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = opaqueColorResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_lighting"),
        "Deferred Lighting",
        computeQueueRequest,
        &surfelGiTask,
        1u,
        lightingUses,
        LengthOf(lightingUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        shadowVisibility.get(),
        ResourceStates::ShaderResource,
        &lightingRecorded
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuTaskResourceUse compositeUses[] = {
        GpuTaskResourceUse{
            .resource = opaqueColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = compositeColorResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool compositeRecorded = false;
    const GpuTaskId compositeTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_composite"),
        "Deferred Composite",
        computeQueueRequest,
        &lightingTask,
        1u,
        compositeUses,
        LengthOf(compositeUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        opaqueColor.get(),
        ResourceStates::ShaderResource,
        &compositeRecorded
    );
    ASSERT_TRUE(compositeTask.valid());

    const GpuTaskResourceUse presentUses[] = {
        GpuTaskResourceUse{
            .resource = compositeColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool presentRecorded = false;
    const GpuTaskId presentTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_present"),
        "Deferred Present",
        graphicsQueueRequest,
        &compositeTask,
        1u,
        presentUses,
        LengthOf(presentUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        compositeColor.get(),
        ResourceStates::ShaderResource,
        &presentRecorded
    );
    ASSERT_TRUE(presentTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shadow_chain_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    ASSERT_TRUE(analysis.hasExplicitEdge(shadowPrepareTask, graphicsPrefixTask));
    ASSERT_TRUE(analysis.hasInferredEdge(shadowPrepareTask, shadowVisibilityTask));
    ASSERT_TRUE(analysis.hasInferredEdge(shadowPrepareTask, presentTask));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 8u);
    const GpuSubmissionPacketId shadowPreparePacket = views.compiled.packetForTask(shadowPrepareTask);
    const GpuSubmissionPacketId graphicsPrefixPacket = views.compiled.packetForTask(graphicsPrefixTask);
    const GpuSubmissionPacketId shadowVisibilityPacket = views.compiled.packetForTask(shadowVisibilityTask);
    const GpuSubmissionPacketId causticsPacket = views.compiled.packetForTask(causticsTask);
    const GpuSubmissionPacketId surfelGiPacket = views.compiled.packetForTask(surfelGiTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    const GpuSubmissionPacketId compositePacket = views.compiled.packetForTask(compositeTask);
    const GpuSubmissionPacketId presentPacket = views.compiled.packetForTask(presentTask);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(graphicsPrefixPacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    ASSERT_TRUE(surfelGiPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    EXPECT_EQ(views.compiled.packetIdAt(0u), shadowPreparePacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), graphicsPrefixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), shadowVisibilityPacket);
    EXPECT_EQ(views.compiled.packetIdAt(3u), causticsPacket);
    EXPECT_EQ(views.compiled.packetIdAt(4u), surfelGiPacket);
    EXPECT_EQ(views.compiled.packetIdAt(5u), lightingPacket);
    EXPECT_EQ(views.compiled.packetIdAt(6u), compositePacket);
    EXPECT_EQ(views.compiled.packetIdAt(7u), presentPacket);

    const GpuCompiledTaskView compiledShadowPrepare = views.compiled.findTask(shadowPrepareTask);
    const GpuCompiledTaskView compiledPrefix = views.compiled.findTask(graphicsPrefixTask);
    ASSERT_TRUE(compiledShadowPrepare.valid());
    ASSERT_TRUE(compiledPrefix.valid());
    // The retained selector state matches the graph import. Preparation emits a graph-initial no-op marker to seed
    // the native tracker, rather than a stale Common -> ConstantBuffer transition, before seeding Prefix.
    EXPECT_TRUE(slotsBuffer->getDescription().keepInitialState);
    EXPECT_EQ(slotsBuffer->getDescription().initialState, ResourceStates::ConstantBuffer);
    EXPECT_EQ(views.declarations.resourceAt(slotsResource.index).initialState, ResourceStates::ConstantBuffer);
    ASSERT_EQ(compiledShadowPrepare.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const shadowPrepareBarrier = views.compiled.findTask(shadowPrepareTask).prologueBarriers;
    ASSERT_NE(shadowPrepareBarrier, nullptr);
    EXPECT_EQ(shadowPrepareBarrier[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(shadowPrepareBarrier[0u].resource, slotsResource);
    EXPECT_EQ(shadowPrepareBarrier[0u].before, ResourceStates::ConstantBuffer);
    EXPECT_EQ(shadowPrepareBarrier[0u].after, ResourceStates::ConstantBuffer);
    EXPECT_TRUE(shadowPrepareBarrier[0u].isGraphInitialState);
    ASSERT_GT(compiledPrefix.plan->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const graphicsPrefixStateSeeds = views.compiled.findTask(
        graphicsPrefixTask
    ).prologueStateSeeds;
    ASSERT_NE(graphicsPrefixStateSeeds, nullptr);
    bool graphicsPrefixImportsPreparedSlots = false;
    for(usize index = 0u; index < compiledPrefix.plan->prologueStateSeedCount; ++index){
        graphicsPrefixImportsPreparedSlots = graphicsPrefixImportsPreparedSlots
            || (
                graphicsPrefixStateSeeds[index].resource == slotsResource
                && graphicsPrefixStateSeeds[index].sourcePacket == shadowPreparePacket
            )
        ;
    }
    EXPECT_TRUE(graphicsPrefixImportsPreparedSlots);
    ASSERT_EQ(views.compiled.packet(graphicsPrefixPacket).plan->dependencyCount, 1u);
    const GpuPacketDependency* const graphicsPrefixDependencies = views.compiled.packet(
        graphicsPrefixPacket
    ).dependencies;
    ASSERT_NE(graphicsPrefixDependencies, nullptr);
    EXPECT_EQ(graphicsPrefixDependencies[0u].producer, shadowPreparePacket);

    const GpuCompiledTaskView compiledShadowVisibility = views.compiled.findTask(shadowVisibilityTask);
    ASSERT_TRUE(compiledShadowVisibility.valid());
    ASSERT_GT(compiledShadowVisibility.plan->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const shadowVisibilityStateSeeds = views.compiled.findTask(
        shadowVisibilityTask
    ).prologueStateSeeds;
    ASSERT_NE(shadowVisibilityStateSeeds, nullptr);
    bool shadowVisibilityImportsPrefixSlots = false;
    for(usize index = 0u; index < compiledShadowVisibility.plan->prologueStateSeedCount; ++index){
        shadowVisibilityImportsPrefixSlots = shadowVisibilityImportsPrefixSlots
            || (
                shadowVisibilityStateSeeds[index].resource == slotsResource
                && shadowVisibilityStateSeeds[index].sourcePacket == graphicsPrefixPacket
            )
        ;
    }
    EXPECT_TRUE(shadowVisibilityImportsPrefixSlots);
    ASSERT_EQ(views.compiled.packet(shadowVisibilityPacket).plan->dependencyCount, 1u);
    const GpuPacketDependency* const shadowVisibilityDependencies = views.compiled.packet(
        shadowVisibilityPacket
    ).dependencies;
    ASSERT_NE(shadowVisibilityDependencies, nullptr);
    EXPECT_EQ(shadowVisibilityDependencies[0u].producer, graphicsPrefixPacket);

    const GpuCompiledTaskView compiledPresent = views.compiled.findTask(presentTask);
    ASSERT_TRUE(compiledPresent.valid());
    ASSERT_GT(compiledPresent.plan->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const presentStateSeeds = views.compiled.findTask(presentTask).prologueStateSeeds;
    ASSERT_NE(presentStateSeeds, nullptr);
    bool presentImportsCompositeState = false;
    for(usize index = 0u; index < compiledPresent.plan->prologueStateSeedCount; ++index){
        presentImportsCompositeState = presentImportsCompositeState
            || (
                presentStateSeeds[index].resource == compositeColorResource
                && presentStateSeeds[index].sourcePacket == compositePacket
            )
        ;
    }
    EXPECT_TRUE(presentImportsCompositeState);
    ASSERT_EQ(views.compiled.packet(presentPacket).plan->dependencyCount, 1u);
    const GpuPacketDependency* const presentDependencies = views.compiled.packet(presentPacket).dependencies;
    ASSERT_NE(presentDependencies, nullptr);
    EXPECT_EQ(presentDependencies[0u].producer, compositePacket);

    const GpuSubmissionPacketRange packetRange = views.compiled.allPacketRange();
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, views.compiled.packetCount());
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph
    ));
    CommandListResourceStateHandoff shadowPrepareFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        shadowPrepareTask,
        shadowPrepareFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const shadowPrepareFinalState = &shadowPrepareFinalStateStorage;
    // Opening an empty handoff would also report the selector's retained ConstantBuffer state from its descriptor.
    // Select the buffer explicitly to prove Shadow Prepare exported a tracked state for Prefix to import.
    Buffer* const shadowPrepareSelectorBuffers[] = { slotsBuffer.get() };
    CommandListResourceStateHandoff shadowPrepareSelectorState(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(shadowPrepareSelectorState.buildResourceSubset(
        *shadowPrepareFinalState,
        nullptr,
        0u,
        shadowPrepareSelectorBuffers,
        LengthOf(shadowPrepareSelectorBuffers),
        scratchArena
    ));
    ASSERT_FALSE(shadowPrepareSelectorState.empty());
    auto shadowPrepareStateProbe = device.createCommandList();
    ASSERT_NE(shadowPrepareStateProbe.get(), nullptr);
    shadowPrepareStateProbe->open(&shadowPrepareSelectorState);
    EXPECT_EQ(shadowPrepareStateProbe->getBufferState(slotsBuffer.get()), ResourceStates::ConstantBuffer);
    shadowPrepareStateProbe->close();
    EXPECT_TRUE(shadowPrepareRecorded);
    EXPECT_TRUE(graphicsPrefixRecorded);
    EXPECT_TRUE(shadowVisibilityRecorded);
    EXPECT_TRUE(causticsRecorded);
    EXPECT_TRUE(surfelGiRecorded);
    EXPECT_TRUE(lightingRecorded);
    EXPECT_TRUE(compositeRecorded);
    EXPECT_TRUE(presentRecorded);

    CommandListResourceStateHandoff presentFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        presentTask,
        presentFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const presentFinalState = &presentFinalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(presentFinalState);
    EXPECT_EQ(stateProbe->getBufferState(slotsBuffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(
        stateProbe->getTextureSubresourceState(compositeColor.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        packetRange,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(shadowPreparePacket).valid());
    EXPECT_TRUE(transaction.packetToken(graphicsPrefixPacket).valid());
    EXPECT_TRUE(transaction.packetToken(presentPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

