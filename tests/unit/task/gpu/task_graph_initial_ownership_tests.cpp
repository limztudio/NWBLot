// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_initial_ownership_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ValidatesInitialExclusiveOwnerBeforeFirstUse){
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
    const Graphics::GpuTaskCommandRequirements computeCommands{ Graphics::GpuQueueCapability::Compute };
    const auto addFirstUse = [&](
        Graphics::GpuTaskGraph& graph,
        const Graphics::GpuGraphResourceId resource,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuTaskCommandRequirements& commands){
        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setResourceUses(&use, 1u)
        ;
        return graph.addTask(desc, commands);
    };
    const auto addBuffer = [&](
        Graphics::GpuTaskGraph& graph,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuPhysicalQueueId owner,
        const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerQueue(owner)
            .setQueueSharing(queueSharing)
        ;
        return graph.importResource(desc);
    };

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_graphics"),
            "Initial Owner Graphics",
            queues[0u].id
        );
        ASSERT_TRUE(resource.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_EQ(declarations.resourceAt(resource.index).initialOwnerQueue, queues[0u].id);
        }
        const Graphics::GpuTaskId task = addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_graphics_use"),
            "Initial Owner Graphics Use",
            graphicsCommands
        );
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;

        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->queue, queues[0u].id);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_cross_queue"),
            "Initial Owner Cross Queue",
            queues[0u].id
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_compute_use"),
            "Initial Owner Compute Use",
            computeCommands
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_stale"),
            "Initial Owner Stale",
            Graphics::GpuPhysicalQueueId{ .index = queues[0u].id.index, .deviceGeneration = static_cast<u16>(queues[0u].id.deviceGeneration + 1u) }
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_stale_use"),
            "Initial Owner Stale Use",
            graphicsCommands
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_concurrent"),
            "Initial Owner Concurrent",
            queues[0u].id,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_concurrent_use"),
            "Initial Owner Concurrent Use",
            graphicsCommands
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }
}

TEST(GpuTaskGraph, RejectsInvalidSingleSourceInitialOwnerHandoffAtDeclaration){
    const Graphics::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo destinationQueue = DedicatedComputeQueue();
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/invalid_initial_owner_completion"))
            .setMarkerLabel("Invalid Initial Owner Completion")
    );
    ASSERT_TRUE(completion.valid());
    const Graphics::QueueSubmissionToken minimumCompletionToken{
        .value = 7u,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
        .queue = Graphics::CommandQueue::Graphics,
    };
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());
    const Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = sourceQueue.id,
            .destinationQueue = destinationQueue.id,
            .completion = completion,
            .minimumCompletionToken = minimumCompletionToken,
            .stateSource = &stateSource,
        },
    };

    usize resourceCount = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        resourceCount = declarations.resourceCount();
    }
    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/invalid_initial_owner_handoff"))
            .setMarkerLabel("Invalid Initial Owner Handoff")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), resourceCount);
}

TEST(GpuTaskGraph, RejectsInvalidSingleSourceInitialOwnerHandoffsForBufferAndAccelStruct){
    struct ResourceCase{
        Graphics::GpuGraphResourceType::Enum type;
        Graphics::ResourceStates::Mask initialState;
        Name identity;
        AStringView markerLabel;
    };

    const Graphics::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo destinationQueue = DedicatedComputeQueue();
    const ResourceCase resourceCases[] = {
        ResourceCase{
            .type = Graphics::GpuGraphResourceType::Buffer,
            .initialState = Graphics::ResourceStates::ShaderResource,
            .identity = Name("tests/task_graph/invalid_initial_owner_buffer"),
            .markerLabel = "Invalid Initial Owner Buffer",
        },
        ResourceCase{
            .type = Graphics::GpuGraphResourceType::AccelStruct,
            .initialState = Graphics::ResourceStates::AccelStructRead,
            .identity = Name("tests/task_graph/invalid_initial_owner_accel_struct"),
            .markerLabel = "Invalid Initial Owner Accel Struct",
        },
    };
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/invalid_typed_initial_owner_completion"))
            .setMarkerLabel("Invalid Typed Initial Owner Completion")
    );
    ASSERT_TRUE(completion.valid());
    const Graphics::QueueSubmissionToken minimumCompletionToken{
        .value = 7u,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
        .queue = Graphics::CommandQueue::Graphics,
    };
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());
    const Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = sourceQueue.id,
            .destinationQueue = destinationQueue.id,
            .completion = completion,
            .minimumCompletionToken = minimumCompletionToken,
            .stateSource = &stateSource,
        },
    };

    for(const ResourceCase& resourceCase : resourceCases){
        EXPECT_FALSE(graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(resourceCase.identity)
                .setMarkerLabel(resourceCase.markerLabel)
                .setType(resourceCase.type)
                .setInitialState(resourceCase.initialState)
                .setInitialOwnerHandoffSources(sources, LengthOf(sources))
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceCount(), 0u);
    }
}

TEST(GpuTaskGraph, RejectsMultiSourceInitialOwnerCompletionAcrossQueues){
    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId sharedCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/multi_owner_shared_completion"))
            .setMarkerLabel("Multi Owner Shared Completion")
    );
    ASSERT_TRUE(sharedCompletion.valid());
    Graphics::CommandListResourceStateHandoff graphicsState(testArena.arena);
    Graphics::CommandListResourceStateHandoff computeState(testArena.arena);
    const Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            },
            .sourceQueue = graphicsQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = sharedCompletion,
            .minimumCompletionToken = Graphics::QueueSubmissionToken{
                .value = 7u,
                .physicalQueueIndex = graphicsQueue.id.index,
                .deviceGeneration = graphicsQueue.id.deviceGeneration,
                .queue = Graphics::CommandQueue::Graphics,
            },
            .stateSource = &graphicsState,
        },
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
            },
            .sourceQueue = computeQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = sharedCompletion,
            .minimumCompletionToken = Graphics::QueueSubmissionToken{
                .value = 11u,
                .physicalQueueIndex = computeQueue.id.index,
                .deviceGeneration = computeQueue.id.deviceGeneration,
                .queue = Graphics::CommandQueue::Compute,
            },
            .stateSource = &computeState,
        },
    };
    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/multi_owner_shared_completion_texture"))
            .setMarkerLabel("Multi Owner Shared Completion Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ShaderResource)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    ).valid());

    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/concurrent_external_release_texture"))
            .setMarkerLabel("Concurrent External Release Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(graphicsQueue.id)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ).valid());
}

TEST(GpuTaskGraph, RejectsInvalidInitialOwnerStateSourceWithBoundCompletionToken){
    const Graphics::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo destinationQueue = DedicatedComputeQueue();
    const Graphics::QueueSubmissionToken completionToken{
        .value = 7u,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
        .queue = Graphics::CommandQueue::Graphics,
    };
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/bound_invalid_initial_owner_completion"))
            .setMarkerLabel("Bound Invalid Initial Owner Completion")
            .setToken(completionToken)
    );
    ASSERT_TRUE(completion.valid());
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());
    const Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = sourceQueue.id,
            .destinationQueue = destinationQueue.id,
            .completion = completion,
            .minimumCompletionToken = completionToken,
            .stateSource = &stateSource,
        },
    };

    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/bound_invalid_initial_owner_buffer"))
            .setMarkerLabel("Bound Invalid Initial Owner Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), 0u);
}

TEST(GpuTaskGraph, CompilesWholeAccelStructInitialOwnerHandoffFromImmutableSnapshot){
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::QueueSubmissionToken completionToken{
        .value = 7u,
        .physicalQueueIndex = queues[0u].id.index,
        .deviceGeneration = queues[0u].id.deviceGeneration,
        .queue = queues[0u].queueClass,
    };
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/whole_accel_initial_owner_completion"))
            .setMarkerLabel("Whole Accel Initial Owner Completion")
            .setToken(completionToken)
    );
    ASSERT_TRUE(completion.valid());
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    Graphics::GraphicsBackend::VulkanTestDispatchAccess::validateStateHandoff(stateSource, queues[0u].id.deviceGeneration);
    Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = queues[0u].id,
            .destinationQueue = queues[1u].id,
            .completion = completion,
            .minimumCompletionToken = completionToken,
            .stateSource = &stateSource,
        },
    };
    const Graphics::GpuGraphResourceId resource = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/whole_accel_initial_owner"))
            .setMarkerLabel("Whole Accel Initial Owner")
            .setType(Graphics::GpuGraphResourceType::AccelStruct)
            .setInitialState(Graphics::ResourceStates::AccelStructRead)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    );
    ASSERT_TRUE(resource.valid());
    stateSource.reset();
    const u16 staleGeneration = static_cast<u16>(queues[0u].id.deviceGeneration + 1u);
    Graphics::GraphicsBackend::VulkanTestDispatchAccess::validateStateHandoff(stateSource, staleGeneration);
    sources[0u].destinationQueue = queues[0u].id;
    sources[0u].minimumCompletionToken = {};
    sources[0u].stateSource = nullptr;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphResourceView imported = declarations.resourceAt(resource.index);
        ASSERT_EQ(imported.initialOwnerHandoffSourceCount, 1u);
        ASSERT_NE(imported.initialOwnerHandoffSources, nullptr);
        const Graphics::GpuTaskGraphInitialOwnerHandoffSourceView& frozen = imported.initialOwnerHandoffSources[0u];
        EXPECT_EQ(frozen.sourceQueue, queues[0u].id);
        EXPECT_EQ(frozen.destinationQueue, queues[1u].id);
        EXPECT_EQ(frozen.minimumCompletionToken.value, completionToken.value);
        ASSERT_NE(frozen.stateSource, nullptr);
        EXPECT_NE(frozen.stateSource, &stateSource);
        EXPECT_TRUE(frozen.stateSource->validForDeviceGeneration(queues[0u].id.deviceGeneration));
        EXPECT_FALSE(frozen.stateSource->validForDeviceGeneration(staleGeneration));
    }

    const Graphics::GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::AccelStructRead,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/whole_accel_initial_owner_use"))
        .setMarkerLabel("Whole Accel Initial Owner Use")
        .setResourceUses(&use, 1u)
    ;
    const Graphics::GpuTaskCommandRequirements computeCommands{ Graphics::GpuQueueCapability::Compute };
    const Graphics::GpuTaskId task = graph.addTask(desc, computeCommands);
    ASSERT_TRUE(task.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    const Graphics::GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
    ASSERT_TRUE(compiledTask.valid());
    EXPECT_EQ(compiledTask.plan->queue, queues[1u].id);
    const Graphics::GpuCompiledBarrier* acquire = nullptr;
    for(usize barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = compiledTask.prologueBarriers[barrierIndex];
        if(barrier.type == Graphics::GpuCompiledBarrierType::AccelStructOwnershipAcquire){
            ASSERT_EQ(acquire, nullptr);
            acquire = &barrier;
        }
    }
    ASSERT_NE(acquire, nullptr);
    EXPECT_TRUE(acquire->isInitialOwnerHandoff);
    EXPECT_EQ(acquire->resource, resource);
    EXPECT_EQ(acquire->range.textureSubresources, Graphics::s_AllSubresources);
    EXPECT_EQ(acquire->range.bufferRange, Graphics::s_EntireBuffer);
    EXPECT_EQ(acquire->sourceQueue, queues[0u].id);
    EXPECT_EQ(acquire->destinationQueue, queues[1u].id);
    const Graphics::GpuCompiledPacketView packet = views.compiled.packet(compiledTask.plan->packet);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(packet.plan->externalDependencyCount, 1u);
    EXPECT_EQ(packet.externalDependencies[0u], completion);
    EXPECT_EQ(views.compiled.compileStatistics().initialOwnershipExternalDependencyCount, 1u);
    ASSERT_EQ(views.compiled.logicalOwnershipTransferCount(), 1u);
    const Graphics::GpuCompiledOwnershipTransfer* const transfer = views.compiled.logicalOwnershipTransferAt(0u);
    ASSERT_NE(transfer, nullptr);
    EXPECT_EQ(transfer->route, Graphics::GpuOwnershipTransferRoute::ExternalImport);
    EXPECT_EQ(transfer->resourceType, Graphics::GpuGraphResourceType::AccelStruct);
    EXPECT_EQ(transfer->sourceQueue, queues[0u].id);
    EXPECT_EQ(transfer->destinationQueue, queues[1u].id);
}

TEST(GpuTaskGraph, RejectsNonWholeOrOverlappingAccelStructInitialOwnerHandoffs){
    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    const Graphics::QueueSubmissionToken graphicsToken{
        .value = 7u,
        .physicalQueueIndex = graphicsQueue.id.index,
        .deviceGeneration = graphicsQueue.id.deviceGeneration,
        .queue = graphicsQueue.queueClass,
    };
    const Graphics::QueueSubmissionToken computeToken{
        .value = 11u,
        .physicalQueueIndex = computeQueue.id.index,
        .deviceGeneration = computeQueue.id.deviceGeneration,
        .queue = computeQueue.queueClass,
    };
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId graphicsCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/non_whole_accel_graphics_completion"))
            .setMarkerLabel("Non Whole Accel Graphics Completion")
            .setToken(graphicsToken)
    );
    const Graphics::GpuExternalCompletionId computeCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/non_whole_accel_compute_completion"))
            .setMarkerLabel("Non Whole Accel Compute Completion")
            .setToken(computeToken)
    );
    ASSERT_TRUE(graphicsCompletion.valid());
    ASSERT_TRUE(computeCompletion.valid());
    Graphics::CommandListResourceStateHandoff graphicsState(testArena.arena);
    Graphics::CommandListResourceStateHandoff computeState(testArena.arena);
    Graphics::GraphicsBackend::VulkanTestDispatchAccess::validateStateHandoff(graphicsState, graphicsQueue.id.deviceGeneration);
    Graphics::GraphicsBackend::VulkanTestDispatchAccess::validateStateHandoff(computeState, computeQueue.id.deviceGeneration);
    Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = graphicsQueue.id,
            .destinationQueue = computeQueue.id,
            .completion = graphicsCompletion,
            .minimumCompletionToken = graphicsToken,
            .stateSource = &graphicsState,
        },
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = {},
            .sourceQueue = computeQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = computeCompletion,
            .minimumCompletionToken = computeToken,
            .stateSource = &computeState,
        },
    };
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/non_whole_accel_initial_owner"))
        .setMarkerLabel("Non Whole Accel Initial Owner")
        .setType(Graphics::GpuGraphResourceType::AccelStruct)
        .setInitialState(Graphics::ResourceStates::AccelStructRead)
        .setInitialOwnerHandoffSources(sources, 1u)
    ;
    const Graphics::GpuTaskResourceRange invalidRanges[] = {
        Graphics::GpuTaskResourceRange{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u) },
        Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(0u, 64u) },
    };
    for(const Graphics::GpuTaskResourceRange& range : invalidRanges){
        sources[0u].range = range;
        EXPECT_FALSE(graph.importResource(desc).valid());
    }
    sources[0u].range = {};
    const Graphics::QueueSubmissionToken invalidTokens[] = {
        Graphics::QueueSubmissionToken{},
        Graphics::QueueSubmissionToken{
            .value = 7u,
            .physicalQueueIndex = computeQueue.id.index,
            .deviceGeneration = graphicsQueue.id.deviceGeneration,
            .queue = graphicsQueue.queueClass,
        },
        Graphics::QueueSubmissionToken{
            .value = 7u,
            .physicalQueueIndex = graphicsQueue.id.index,
            .deviceGeneration = static_cast<u16>(graphicsQueue.id.deviceGeneration + 1u),
            .queue = graphicsQueue.queueClass,
        },
    };
    for(const Graphics::QueueSubmissionToken& token : invalidTokens){
        sources[0u].minimumCompletionToken = token;
        EXPECT_FALSE(graph.importResource(desc).valid());
    }
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), 0u);
    }
    sources[0u].minimumCompletionToken = graphicsToken;
    ASSERT_TRUE(graph.importResource(desc).valid());
    desc.setIdentity(Name("tests/task_graph/second_whole_accel_initial_owner")).setInitialOwnerHandoffSources(sources + 1u, 1u);
    ASSERT_TRUE(graph.importResource(desc).valid());
    desc
        .setIdentity(Name("tests/task_graph/multiple_whole_accel_initial_owners"))
        .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    ;
    EXPECT_FALSE(graph.importResource(desc).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(declarations.resourceCount(), 2u);
}

TEST(GpuTaskGraph, RejectsInvalidMultiSourceInitialTextureOwnershipHandoffAtDeclaration){
    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId graphicsCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/invalid_multi_owner_graphics_completion"))
            .setMarkerLabel("Invalid Multi Owner Graphics Completion")
    );
    const Graphics::GpuExternalCompletionId computeCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/invalid_multi_owner_compute_completion"))
            .setMarkerLabel("Invalid Multi Owner Compute Completion")
    );
    ASSERT_TRUE(graphicsCompletion.valid());
    ASSERT_TRUE(computeCompletion.valid());

    Graphics::CommandListResourceStateHandoff graphicsState(testArena.arena);
    Graphics::CommandListResourceStateHandoff computeState(testArena.arena);
    ASSERT_FALSE(graphicsState.valid());
    ASSERT_FALSE(computeState.valid());
    const Graphics::GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            },
            .sourceQueue = graphicsQueue.id,
            .destinationQueue = computeQueue.id,
            .completion = graphicsCompletion,
            .minimumCompletionToken = Graphics::QueueSubmissionToken{
                .value = 7u,
                .physicalQueueIndex = graphicsQueue.id.index,
                .deviceGeneration = graphicsQueue.id.deviceGeneration,
                .queue = Graphics::CommandQueue::Graphics,
            },
            .stateSource = &graphicsState,
        },
        Graphics::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
            },
            .sourceQueue = computeQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = computeCompletion,
            .minimumCompletionToken = Graphics::QueueSubmissionToken{
                .value = 11u,
                .physicalQueueIndex = computeQueue.id.index,
                .deviceGeneration = computeQueue.id.deviceGeneration,
                .queue = Graphics::CommandQueue::Compute,
            },
            .stateSource = &computeState,
        },
    };

    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/invalid_multi_owner_texture"))
            .setMarkerLabel("Invalid Multi Owner Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ShaderResource)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

