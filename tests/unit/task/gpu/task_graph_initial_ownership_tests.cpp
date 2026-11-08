// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const Core::GpuTaskCommandRequirements computeCommands{ Core::GpuQueueCapability::Compute };
    const auto addFirstUse = [&](Core::GpuTaskGraph& graph, const Core::GpuGraphResourceId resource, const Name& identity, const AStringView label, const Core::GpuTaskCommandRequirements& commands){
        const Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
        Core::GpuTaskDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setResourceUses(Span(&use, 1u));
        return graph.addTask(desc, commands);
    };
    const auto addBuffer = [&](Core::GpuTaskGraph& graph, const Name& identity, const AStringView label, const Core::GpuPhysicalQueueId owner, const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive){
        Core::GpuGraphResourceDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphResourceType::Buffer).setInitialState(Core::ResourceStates::Common).setInitialOwnerQueue(owner).setQueueSharing(queueSharing);
        return graph.importResource(desc);
    };

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = addBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerGraphics), "Initial Owner Graphics", queues[0u].id);
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const auto taskResult = addFirstUse(graph, resource, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerGraphicsUse), "Initial Owner Graphics Use", graphicsCommands);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;

        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->queue, queues[0u].id);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = addBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerCrossQueue), "Initial Owner Cross Queue", queues[0u].id);
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        {
            const auto gpuTaskAdmission = addFirstUse(graph, resource, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerComputeUse), "Initial Owner Compute Use", computeCommands);
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = addBuffer(graph, Name(TaskGraphTestUtils::s_GpuInitialOwnerStale), "Initial Owner Stale", Core::GpuPhysicalQueueId{ .index = queues[0u].id.index, .deviceGeneration = static_cast<u16>(queues[0u].id.deviceGeneration + 1u) });
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        {
            const auto secondGpuTaskAdmission = addFirstUse(graph, resource, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerStaleUse), "Initial Owner Stale Use", graphicsCommands);
            ASSERT_TRUE(secondGpuTaskAdmission);
            ASSERT_TRUE(secondGpuTaskAdmission->valid());
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = addBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerConcurrent), "Initial Owner Concurrent", queues[0u].id, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        {
            const auto thirdGpuTaskAdmission = addFirstUse(graph, resource, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInitialOwnerConcurrentUse), "Initial Owner Concurrent Use", graphicsCommands);
            ASSERT_TRUE(thirdGpuTaskAdmission);
            ASSERT_TRUE(thirdGpuTaskAdmission->valid());
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
    }
}

TEST(GpuTaskGraph, RejectsInvalidSingleSourceInitialOwnerHandoffsForBufferAndAccelStruct){
    const Core::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo destinationQueue = DedicatedComputeQueue();
    const Array<GpuTaskInitialOwnerCase, 2> resourceCases = {
        GpuTaskInitialOwnerCase(Core::GpuGraphResourceType::Buffer, Core::ResourceStates::ShaderResource, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidInitialOwnerBuffer), "Invalid Initial Owner Buffer"),
        GpuTaskInitialOwnerCase(Core::GpuGraphResourceType::AccelStruct, Core::ResourceStates::AccelStructRead, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidInitialOwnerAccelStruct), "Invalid Initial Owner Accel Struct"),
    };
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidTypedInitialOwnerCompletion)).setMarkerLabel("Invalid Typed Initial Owner Completion"));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const Core::QueueSubmissionToken minimumCompletionToken{ .value = 7u, .physicalQueueIndex = sourceQueue.id.index, .deviceGeneration = sourceQueue.id.deviceGeneration, .queue = Core::CommandQueue::Graphics, };
    Core::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());
    const Array<Core::GpuGraphInitialOwnerHandoffSourceDesc, 1> sources = {
        Core::GpuGraphInitialOwnerHandoffSourceDesc{ .range = {}, .sourceQueue = sourceQueue.id, .destinationQueue = destinationQueue.id, .completion = completion, .minimumCompletionToken = minimumCompletionToken, .stateSource = &stateSource, },
    };

    for(const GpuTaskInitialOwnerCase& resourceCase : resourceCases){
        EXPECT_FALSE(graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(resourceCase.identity)
                .setMarkerLabel(resourceCase.markerLabel)
                .setType(resourceCase.resourceType)
                .setInitialState(resourceCase.initialState)
                .setInitialOwnerHandoffSources(sources)
        ).has_value()
        );
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceCount(), 0u);
    }
}

TEST(GpuTaskGraph, RejectsMultiSourceInitialOwnerCompletionAcrossQueues){
    const Core::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto sharedCompletionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMultiOwnerSharedCompletion)).setMarkerLabel("Multi Owner Shared Completion"));
    ASSERT_TRUE(sharedCompletionResult);
    const Core::GpuExternalCompletionId sharedCompletion = *sharedCompletionResult;
    ASSERT_TRUE(sharedCompletion.valid());
    Core::CommandListResourceStateHandoff graphicsState(testArena.arena);
    Core::CommandListResourceStateHandoff computeState(testArena.arena);
    const Array<Core::GpuGraphInitialOwnerHandoffSourceDesc, 2> sources = {
        Core::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), },
            .sourceQueue = graphicsQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = sharedCompletion,
            .minimumCompletionToken = Core::QueueSubmissionToken{ .value = 7u, .physicalQueueIndex = graphicsQueue.id.index, .deviceGeneration = graphicsQueue.id.deviceGeneration, .queue = Core::CommandQueue::Graphics, },
            .stateSource = &graphicsState,
        },
        Core::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u), },
            .sourceQueue = computeQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = sharedCompletion,
            .minimumCompletionToken = Core::QueueSubmissionToken{ .value = 11u, .physicalQueueIndex = computeQueue.id.index, .deviceGeneration = computeQueue.id.deviceGeneration, .queue = Core::CommandQueue::Compute, },
            .stateSource = &computeState,
        },
    };
    EXPECT_FALSE(graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMultiOwnerSharedCompletionTexture))
            .setMarkerLabel("Multi Owner Shared Completion Texture")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::ShaderResource)
            .setInitialOwnerHandoffSources(sources)
    ).has_value()
    );

    EXPECT_FALSE(graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphConcurrentExternalReleaseTexture))
            .setMarkerLabel("Concurrent External Release Texture")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::ShaderResource)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(graphicsQueue.id)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ).has_value()
    );
}

TEST(GpuTaskGraph, RejectsInvalidInitialOwnerStateSourceWithBoundCompletionToken){
    const Core::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo destinationQueue = DedicatedComputeQueue();
    const Core::QueueSubmissionToken completionToken{ .value = 7u, .physicalQueueIndex = sourceQueue.id.index, .deviceGeneration = sourceQueue.id.deviceGeneration, .queue = Core::CommandQueue::Graphics, };
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto completionResult = graph.importExternalCompletion(
        Core::GpuExternalCompletionDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphBoundInvalidInitialOwnerCompletion))
            .setMarkerLabel("Bound Invalid Initial Owner Completion")
            .setToken(completionToken)
    );
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    Core::CommandListResourceStateHandoff stateSource(testArena.arena);
    ASSERT_FALSE(stateSource.valid());
    const Array<Core::GpuGraphInitialOwnerHandoffSourceDesc, 1> sources = {
        Core::GpuGraphInitialOwnerHandoffSourceDesc{ .range = {}, .sourceQueue = sourceQueue.id, .destinationQueue = destinationQueue.id, .completion = completion, .minimumCompletionToken = completionToken, .stateSource = &stateSource, },
    };

    EXPECT_FALSE(graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphBoundInvalidInitialOwnerBuffer))
            .setMarkerLabel("Bound Invalid Initial Owner Buffer")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setInitialOwnerHandoffSources(sources)
    ).has_value()
    );
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), 0u);
}


TEST(GpuTaskGraph, RejectsInvalidMultiSourceInitialTextureOwnershipHandoffAtDeclaration){
    const Core::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Core::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto graphicsCompletionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidMultiOwnerGraphicsCompletion)).setMarkerLabel("Invalid Multi Owner Graphics Completion"));
    ASSERT_TRUE(graphicsCompletionResult);
    const Core::GpuExternalCompletionId graphicsCompletion = *graphicsCompletionResult;
    const auto computeCompletionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidMultiOwnerComputeCompletion)).setMarkerLabel("Invalid Multi Owner Compute Completion"));
    ASSERT_TRUE(computeCompletionResult);
    const Core::GpuExternalCompletionId computeCompletion = *computeCompletionResult;
    ASSERT_TRUE(graphicsCompletion.valid());
    ASSERT_TRUE(computeCompletion.valid());

    Core::CommandListResourceStateHandoff graphicsState(testArena.arena);
    Core::CommandListResourceStateHandoff computeState(testArena.arena);
    ASSERT_FALSE(graphicsState.valid());
    ASSERT_FALSE(computeState.valid());
    const Array<Core::GpuGraphInitialOwnerHandoffSourceDesc, 2> sources = {
        Core::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), },
            .sourceQueue = graphicsQueue.id,
            .destinationQueue = computeQueue.id,
            .completion = graphicsCompletion,
            .minimumCompletionToken = Core::QueueSubmissionToken{ .value = 7u, .physicalQueueIndex = graphicsQueue.id.index, .deviceGeneration = graphicsQueue.id.deviceGeneration, .queue = Core::CommandQueue::Graphics, },
            .stateSource = &graphicsState,
        },
        Core::GpuGraphInitialOwnerHandoffSourceDesc{
            .range = Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u), },
            .sourceQueue = computeQueue.id,
            .destinationQueue = graphicsQueue.id,
            .completion = computeCompletion,
            .minimumCompletionToken = Core::QueueSubmissionToken{ .value = 11u, .physicalQueueIndex = computeQueue.id.index, .deviceGeneration = computeQueue.id.deviceGeneration, .queue = Core::CommandQueue::Compute, },
            .stateSource = &computeState,
        },
    };

    EXPECT_FALSE(graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphInvalidMultiOwnerTexture))
            .setMarkerLabel("Invalid Multi Owner Texture")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::ShaderResource)
            .setInitialOwnerHandoffSources(sources)
    ).has_value()
    );
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.resourceCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

