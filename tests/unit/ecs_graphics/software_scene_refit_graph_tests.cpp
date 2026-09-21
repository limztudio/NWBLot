// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_software_scene_refit.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>
#include <core/perf/timing.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_software_scene_refit_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct SoftwareSceneRefitGraphTestsTag>;

// The real runtime is intentionally never initialized; declaration/compilation uses only metadata buffers.
struct RefitContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GpuTaskScheduler gpuTasks;
    Core::Perf::TimingRecorder timing{ testArena.arena };
    Core::GraphicsRuntime graphics{ graphicsAllocator, cpuScheduler, gpuTasks, timing };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    Core::Alloc::ScratchArena scratch{ Name("tests/software_scene_refit/scratch") };
    SoftwareSceneRefitHandle snapshot;
    Core::BufferHandle root;
    Core::GpuGraphResourceId sceneResource;
    Core::GpuGraphResourceId rootResource;
    Core::GpuTaskId predecessor;

    RefitContext(){
        snapshot = SoftwareSceneRefitHandle(
            NewArenaObject<SoftwareSceneRefitControl>(testArena.arena, testArena.arena, graphics),
            ArenaRefDeleter<SoftwareSceneRefitControl, Core::Alloc::GlobalArena>(&testArena.arena), AdoptRef
        );
        snapshot->inputBuffer = makeBuffer(Name("software_scene_refit_inputs"));
        snapshot->sceneNodes = makeBuffer(Name("tests/software_scene_refit/scene"));
        root = makeBuffer(Name("tests/software_scene_refit/root"));
        snapshot->inputDescriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 1u);
        snapshot->sceneDescriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, s_ExpectedDualCount);
        snapshot->queue = { .index = 0u, .deviceGeneration = 1u };
        snapshot->nodeCount = 3u;
        for(u32 instance = 0u; instance < s_ExpectedDualCount; ++instance){
            snapshot->inputs.push_back({ {}, 3u, {} });
            snapshot->meshNodes.push_back(root);
        }
    }

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name identity){
        Core::BufferDesc desc;
        desc
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setDebugName(identity)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(testArena.arena, context, allocator, desc, true);
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] Core::GpuGraphResourceId importBuffer(const Core::BufferHandle& buffer){
        return graph.importBuffer(
            buffer,
            RendererTaskGraphDetail::BufferResourceDesc(buffer->getCreationDescription().debugName, "Scene Refit Test Buffer")
                .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common)
        );
    }

    void begin(const bool importRoot, const bool meshBuild){
        graph.reset();
        sceneResource = importBuffer(snapshot->sceneNodes);
        rootResource = importRoot ? importBuffer(root) : Core::GpuGraphResourceId{};
        const Core::GpuTaskResourceUse uses[] = {
            RendererTaskGraphDetail::WriteUse(sceneResource, Core::ResourceStates::Common),
            RendererTaskGraphDetail::ReadWriteUse(rootResource, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(Name("tests/software_scene_refit/predecessor"))
            .setMarkerLabel("Prepared Mesh and Scene Producer")
            .setQueue(RendererTaskGraphDetail::GraphicsComputeUploadQueueRequest())
            .setResourceUses(uses, importRoot && meshBuild ? s_ExpectedDualCount : 1u)
        ;
        predecessor = graph.addTask(desc);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectCompiledHandoff(RefitContext& context, const SoftwareSceneRefitGraphTasks tasks){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    const Core::GpuTaskResourceUse use = RendererTaskGraphDetail::WriteUse(context.sceneResource, Core::ResourceStates::ShaderResource);
    Core::GpuTaskDesc endpointDesc;
    endpointDesc
        .setIdentity(Name("tests/software_scene_refit/endpoint"))
        .setMarkerLabel("Accepting Scene Preparation")
        .setQueue(RendererTaskGraphDetail::GraphicsComputeUploadQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&tasks.refit, 1u)
        .setResourceUses(&use, 1u)
    ;
    const Core::GpuTaskId endpoint = context.graph.addTask(endpointDesc);
    ASSERT_TRUE(endpoint.valid());
    const Core::GpuPhysicalQueueInfo queue{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = { .index = 0u, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = static_cast<Core::GpuQueueCapability::Mask>(
            Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer
        ),
    };
    const Core::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Core::GpuTaskGraphAnalysis analysis(context.testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(context.testArena.arena);
    Core::GpuCompiledGraph compiled(context.testArena.arena);
    const Core::GpuTaskGraph::DeclarationReadView declarations(context.graph);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiled, context.scratch, options))
        << "analysis=" << static_cast<u32>(analysis.diagnostic().status)
        << ", queue=" << static_cast<u32>(assignments.diagnostic().status);
    const Core::GpuCompiledGraph::ReadView plan(compiled);
    EXPECT_TRUE(plan.tasksSharePacket(endpoint, tasks.inputUpload));
    EXPECT_TRUE(plan.tasksSharePacket(endpoint, tasks.refit));
    EXPECT_TRUE(plan.tasksSharePacket(endpoint, context.predecessor));
    const auto input = declarations.findImportedBuffer(context.snapshot->inputBuffer);
    const auto refit = plan.findTask(tasks.refit);
    ASSERT_TRUE(refit.valid());
    bool transitionsInput = false;
    for(u32 index = 0u; index < refit.plan->prologueBarrierCount; ++index){
        const auto& barrier = refit.prologueBarriers[index];
        if(barrier.resource == input && barrier.before == Core::ResourceStates::Common && barrier.after == Core::ResourceStates::ShaderResource)
            transitionsInput = true;
    }
    EXPECT_TRUE(transitionsInput);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SoftwareSceneRefitGraph, RetainedInputUploadCompilesAndOwnsTheSrvTransitionAcrossDeclarations){
    RefitContext context;
    for(u32 frame = 0u; frame < 3u; ++frame){
        SCOPED_TRACE(frame);
        context.snapshot->inputs[0u].objectToWorld.m[0u][3u] = static_cast<f32>(frame);
        context.begin(true, frame == 0u);
        ASSERT_TRUE(context.predecessor.valid());
        const auto tasks = DeclareSoftwareSceneRefit(context.graph, context.snapshot, context.sceneResource, context.predecessor, context.scratch);
        ASSERT_TRUE(tasks.inputUpload.valid());
        ASSERT_TRUE(tasks.refit.valid());
        {
            const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
            const auto input = view.findImportedBuffer(context.snapshot->inputBuffer);
            ASSERT_TRUE(input.valid());
            const auto upload = view.taskAt(tasks.inputUpload.index);
            ASSERT_EQ(upload.resourceUseCount, s_ExpectedDualCount);
            EXPECT_EQ(upload.resourceUses[0u].requiredState, Core::ResourceStates::CopyDest);
            EXPECT_EQ(upload.resourceUses[1u].requiredState, Core::ResourceStates::Common);
            EXPECT_EQ(view.resourceAt(input.index).externalFinalState, Core::ResourceStates::Common);
            const auto refit = view.taskAt(tasks.refit.index);
            ASSERT_EQ(refit.dependencyCount, 1u);
            EXPECT_EQ(refit.dependencies[0u], tasks.inputUpload);
            EXPECT_EQ(refit.resourceUseCount, 3u); // Repeated instances share one declared mesh-root read.
            usize byteSize = 0u;
            const void* const bytes = view.uploadBlobData({ .generation = view.generation(), .index = 0u }, byteSize);
            ASSERT_NE(bytes, nullptr);
            ASSERT_EQ(byteSize, context.snapshot->inputs.size() * sizeof(SoftwareSceneRefitInstanceGpu));
            EXPECT_EQ(NWB_MEMCMP(bytes, context.snapshot->inputs.data(), byteSize), 0);
        }
        ASSERT_NO_FATAL_FAILURE(ExpectCompiledHandoff(context, tasks));
    }
}

TEST(SoftwareSceneRefitGraph, MissingMeshRootImportCannotPublishAnUploadOrRefitProducer){
    RefitContext context;
    context.begin(false, false);
    ASSERT_TRUE(context.predecessor.valid());
    const auto tasks = DeclareSoftwareSceneRefit(context.graph, context.snapshot, context.sceneResource, context.predecessor, context.scratch);
    EXPECT_FALSE(tasks.inputUpload.valid());
    EXPECT_FALSE(tasks.refit.valid());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    EXPECT_EQ(view.taskCount(), 1u);
    EXPECT_EQ(view.uploadBlobCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

