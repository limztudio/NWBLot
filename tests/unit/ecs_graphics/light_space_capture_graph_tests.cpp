// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/task_graph_light_space_shadow.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>
#include <core/perf/timing.h>
#include <core/task/gpu/compiler_internal.h>
#include <core/task/gpu/scheduler.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>
#include <tests/common/vulkan_test_sync.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_capture_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct LightSpaceCaptureGraphTestsTag>;
using Access = Core::GraphicsBackend::VulkanTestDispatchAccess;

// This fixture declares the production graph with metadata-only resources; no native device or GPU work is created.
struct CaptureContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GpuTaskScheduler gpuTasks;
    Core::Perf::TimingRecorder timing{ testArena.arena };
    Core::GraphicsRuntime graphics{ graphicsAllocator, cpuScheduler, gpuTasks, timing };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    Core::Alloc::ScratchArena scratch{ Name("tests/light_space_capture_graph/scratch") };
    Core::CommandListResourceStateHandoff acceptedState{ testArena.arena };
    LightSpaceShadowSnapshot snapshot;
    LightSpaceShadowCaster caster;
    Core::GpuTaskId prefix;
    Core::GpuTaskId receiver;
    bool shadowPrepared = true;

    CaptureContext(){
        snapshot.counts = makeBuffer(Name("tests/light_space/counts"));
        snapshot.events = makeBuffer(Name("tests/light_space/events"));
        snapshot.views = makeBuffer(Name("tests/light_space/views"));
        snapshot.drawArguments = makeBuffer(Name("tests/light_space/draw_arguments"));
        Core::TextureDesc depth;
        depth
            .setName(Name("tests/light_space/depth")).setWidth(32u).setHeight(32u).setArraySize(1u)
            .setDimension(Core::TextureDimension::Texture2DArray).setFormat(Core::Format::D32)
            .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
        ;
        Core::Texture* const texture = Tests::NewMetadataOnlyTexture(testArena.arena, context, allocator, depth);
        snapshot.depth = Core::TextureHandle(texture, Core::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
        snapshot.plan.viewCount = 1u;
        snapshot.plan.textureResolution = 32u;
        snapshot.captureTicket = { 2u, true };
        snapshot.casters = &caster;
        snapshot.casterCount = 1u;
        snapshot.ready = true;
        auto& states = Access::stateHandoffTextures(acceptedState);
        states.push_back({ .texture = snapshot.depth.get(), .mipLevel = 0u, .arraySlice = 0u,
            .state = Core::ResourceStates::ShaderResource, .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = { .index = 0u, .deviceGeneration = 1u }, .releaseDestinationQueue = {} });
        Access::validateStateHandoff(acceptedState, 1u);
    }

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name identity){
        Core::BufferDesc description;
        description
            .setByteSize(256u).setCanHaveRawViews(true).setCanHaveUAVs(true).setDebugName(identity)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(testArena.arena, context, allocator, description, true);
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] LightSpaceShadowGraph declareReuse(){
        Core::GpuTaskDesc prefixDesc;
        prefixDesc
            .setIdentity(Name("tests/light_space/prefix")).setMarkerLabel("Current Frame Prefix")
            .setQueue(RendererTaskGraphDetail::GraphicsComputeUploadQueueRequest())
        ;
        prefix = graph.addTask(prefixDesc);
        const Core::GpuTaskExternalStateSource sources[] = { { .states = &acceptedState } };
        const LightSpaceShadowGraph maps = DeclareLightSpaceShadowMaps(graph, LightSpaceShadowGraphInputs{
            .graphics = graphics, .arena = testArena.arena, .scratchArena = scratch, .shadowPrepared = shadowPrepared,
            .snapshot = snapshot, .dependency = prefix, .stateSources = sources, .stateSourceCount = 1u,
        });
        if(!maps.valid())
            return maps;
        const Core::GpuTaskResourceUse uses[] = {
            RendererTaskGraphDetail::ReadUse(maps.counts, Core::ResourceStates::ShaderResource),
            RendererTaskGraphDetail::ReadUse(maps.events, Core::ResourceStates::ShaderResource),
            RendererTaskGraphDetail::ReadUse(maps.views, Core::ResourceStates::ShaderResource),
            RendererTaskGraphDetail::ReadTextureUse(maps.depth, { 0u, 1u, 0u, 1u }, Core::ResourceStates::ShaderResource),
        };
        Core::GpuTaskDesc receiverDesc;
        receiverDesc
            .setIdentity(Name("tests/light_space/receiver")).setMarkerLabel("Current Receiver Shading")
            .setQueue(RendererTaskGraphDetail::GraphicsComputeUploadQueueRequest()).setDependencies(&maps.ready, 1u)
            .setExternalStateSources(sources, 1u).setResourceUses(uses, LengthOf(uses))
        ;
        receiver = graph.addTask(receiverDesc);
        return maps;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectCompilation(CaptureContext& context, const bool expected){
    const Core::GpuPhysicalQueueInfo queue{
        .familyIndex = 0u, .queueIndex = 0u, .id = { .index = 0u, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = static_cast<Core::GpuQueueCapability::Mask>(
            Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer),
    };
    const Core::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::GpuTaskGraphAnalysis analysis(context.testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(context.testArena.arena);
    Core::GpuCompiledGraph compiled(context.testArena.arena);
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    const bool success = compiler.compile(declarations, analysis, topology, assignments, compiled, context.scratch, options);
    ASSERT_EQ(success, expected) << "analysis=" << static_cast<u32>(analysis.diagnostic().status)
        << ", queue=" << static_cast<u32>(assignments.diagnostic().status);
    if(!success)
        return;
    const Core::GpuCompiledGraph::ReadView plan(compiled);
    const auto receiver = plan.findTask(context.receiver);
    ASSERT_TRUE(receiver.valid());
    const auto depth = declarations.findImportedTexture(context.snapshot.depth);
    bool requiresAcceptedDepthState = false;
    for(u32 index = 0u; index < receiver.plan->prologueBarrierCount; ++index){
        const auto& barrier = receiver.prologueBarriers[index];
        if(barrier.resource == depth && barrier.before == Core::ResourceStates::Unknown && barrier.isGraphInitialState)
            requiresAcceptedDepthState = true;
    }
    EXPECT_TRUE(requiresAcceptedDepthState);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(LightSpaceCaptureGraph, ReuseCompilesWithoutCaptureOnlyImportsAndRetainsAcceptedDepthProof){
    CaptureContext context;
    const auto maps = context.declareReuse();
    ASSERT_TRUE(maps.valid());
    ASSERT_TRUE(context.receiver.valid());
    EXPECT_EQ(maps.ready, context.prefix);
    EXPECT_FALSE(maps.drawArguments.valid());
    EXPECT_FALSE(maps.viewUpload.valid());
    EXPECT_FALSE(maps.countsClear.valid());
    EXPECT_FALSE(maps.viewFit.valid());
    EXPECT_FALSE(maps.opaqueCapture.valid());
    EXPECT_FALSE(maps.transparentCapture.valid());
    EXPECT_FALSE(maps.shade.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
        EXPECT_FALSE(declarations.findImportedBuffer(context.snapshot.drawArguments).valid());
        EXPECT_EQ(declarations.uploadBlobCount(), 0u);
        EXPECT_EQ(declarations.taskCount(), 2u);
        const auto receiver = declarations.taskAt(context.receiver.index);
        ASSERT_EQ(receiver.externalStateSourceCount, 1u);
        const auto* retained = receiver.externalStateSources[0u].states;
        ASSERT_NE(retained, nullptr);
        EXPECT_NE(retained, &context.acceptedState);
        // Graph declarations own their handoff snapshot; invalidating the caller's source cannot change this proof.
        context.acceptedState.reset();
        ASSERT_TRUE(retained->validForDeviceGeneration(1u));
        const auto& states = Access::stateHandoffTextures(*retained);
        ASSERT_EQ(states.size(), 1u);
        EXPECT_EQ(states[0u].texture, context.snapshot.depth.get());
        EXPECT_EQ(states[0u].mipLevel, 0u);
        EXPECT_EQ(states[0u].arraySlice, 0u);
        EXPECT_EQ(states[0u].state, Core::ResourceStates::ShaderResource);
        EXPECT_EQ(states[0u].queueSharing, Core::ResourceQueueSharing::Exclusive);
        const Core::GpuPhysicalQueueId owner{ .index = 0u, .deviceGeneration = 1u };
        EXPECT_EQ(states[0u].ownerQueue, owner);
        EXPECT_FALSE(states[0u].releaseDestinationQueue.valid());
    }
    ASSERT_NO_FATAL_FAILURE(ExpectCompilation(context, true));
}

TEST(LightSpaceCaptureGraph, UntouchedDrawArgumentExportReproducesTheRejectedReuseGraph){
    CaptureContext context;
    ASSERT_TRUE(context.declareReuse().valid());
    const auto unused = context.graph.importBuffer(context.snapshot.drawArguments,
        RendererTaskGraphDetail::BufferResourceDesc(Name("tests/light_space/unused_arguments"), "Unused Capture Draw Arguments")
            .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common));
    ASSERT_TRUE(unused.valid());
    ASSERT_NO_FATAL_FAILURE(ExpectCompilation(context, false));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

