// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

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
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeQueue{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const auto addFirstUse = [&](
        Graphics::GpuTaskGraph& graph,
        const Graphics::GpuGraphResourceId resource,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuQueueRequest& queue
    ){
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
            .setQueue(queue)
            .setResourceUses(&use, 1u)
        ;
        return graph.addTask(desc);
    };
    const auto addBuffer = [&](
        Graphics::GpuTaskGraph& graph,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuPhysicalQueueId owner,
        const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
    ){
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
            graphicsQueue
        );
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
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
            computeQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
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
            Graphics::GpuPhysicalQueueId{ queues[0u].id.index, static_cast<u16>(queues[0u].id.deviceGeneration + 1u) }
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_stale_use"),
            "Initial Owner Stale Use",
            graphicsQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
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
            graphicsQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
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
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
    };
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());

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
            .setInitialOwnerQueue(sourceQueue.id)
            .setInitialOwnerReleaseDestinationQueue(destinationQueue.id)
            .setInitialOwnerCompletion(completion)
            .setInitialOwnerMinimumCompletionToken(minimumCompletionToken)
            .setInitialOwnerStateSource(&stateSource)
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
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
    };
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());

    for(const ResourceCase& resourceCase : resourceCases){
        EXPECT_FALSE(graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(resourceCase.identity)
                .setMarkerLabel(resourceCase.markerLabel)
                .setType(resourceCase.type)
                .setInitialState(resourceCase.initialState)
                .setInitialOwnerQueue(sourceQueue.id)
                .setInitialOwnerReleaseDestinationQueue(destinationQueue.id)
                .setInitialOwnerCompletion(completion)
                .setInitialOwnerMinimumCompletionToken(minimumCompletionToken)
                .setInitialOwnerStateSource(&stateSource)
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
                .queue = Graphics::CommandQueue::Graphics,
                .physicalQueueIndex = graphicsQueue.id.index,
                .deviceGeneration = graphicsQueue.id.deviceGeneration,
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
                .queue = Graphics::CommandQueue::Compute,
                .physicalQueueIndex = computeQueue.id.index,
                .deviceGeneration = computeQueue.id.deviceGeneration,
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
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = sourceQueue.id.index,
        .deviceGeneration = sourceQueue.id.deviceGeneration,
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

    EXPECT_FALSE(graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/bound_invalid_initial_owner_buffer"))
            .setMarkerLabel("Bound Invalid Initial Owner Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerQueue(sourceQueue.id)
            .setInitialOwnerReleaseDestinationQueue(destinationQueue.id)
            .setInitialOwnerCompletion(completion)
            .setInitialOwnerMinimumCompletionToken(completionToken)
            .setInitialOwnerStateSource(&stateSource)
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), 0u);
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
                .queue = Graphics::CommandQueue::Graphics,
                .physicalQueueIndex = graphicsQueue.id.index,
                .deviceGeneration = graphicsQueue.id.deviceGeneration,
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
                .queue = Graphics::CommandQueue::Compute,
                .physicalQueueIndex = computeQueue.id.index,
                .deviceGeneration = computeQueue.id.deviceGeneration,
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

