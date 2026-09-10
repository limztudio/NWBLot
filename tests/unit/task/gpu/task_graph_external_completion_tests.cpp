// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_completion_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RetainsAuthoritativeExternalCompletionTokens){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::QueueSubmissionToken token{
        .value = 31u,
        .queue = Graphics::CommandQueue::Transfer,
        .physicalQueueIndex = 2u,
        .deviceGeneration = 1u,
    };
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/owned_external_completion"))
            .setMarkerLabel("Owned External Completion")
            .setToken(token)
    );
    ASSERT_TRUE(completion.valid());
    u64 tokenFirstRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_EQ(declarations.externalCompletionCount(), 1u);

        const Graphics::GpuTaskGraphExternalCompletionView view = declarations.externalCompletionAt(completion.index);
        EXPECT_EQ(view.id, completion);
        EXPECT_TRUE(view.hasToken);
        EXPECT_EQ(view.token.queue, token.queue);
        EXPECT_EQ(view.token.value, token.value);
        EXPECT_EQ(view.token.physicalQueueIndex, token.physicalQueueIndex);
        EXPECT_EQ(view.token.deviceGeneration, token.deviceGeneration);
        const Graphics::QueueSubmissionToken* const storedToken = declarations.externalCompletionToken(completion);
        ASSERT_NE(storedToken, nullptr);
        EXPECT_EQ(storedToken->queue, token.queue);
        EXPECT_EQ(storedToken->value, token.value);
        EXPECT_EQ(storedToken->physicalQueueIndex, token.physicalQueueIndex);
        EXPECT_EQ(storedToken->deviceGeneration, token.deviceGeneration);
        EXPECT_TRUE(declarations.validForDeviceGeneration(token.deviceGeneration));
        tokenFirstRevision = declarations.declarationRevision();
    }
    const Graphics::GpuExternalCompletionId repeatedMetadataImport = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/owned_external_completion"))
            .setMarkerLabel("Compatible Metadata Reference")
    );
    EXPECT_EQ(repeatedMetadataImport, completion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }
    const Graphics::GpuExternalCompletionId repeatedTokenImport = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/owned_external_completion"))
            .setMarkerLabel("Compatible Token Reference")
            .setToken(token)
    );
    EXPECT_EQ(repeatedTokenImport, completion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }

    Graphics::QueueSubmissionToken conflictingToken = token;
    ++conflictingToken.value;
    EXPECT_FALSE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/owned_external_completion"))
            .setMarkerLabel("Conflicting Token Reference")
            .setToken(conflictingToken)
    ).valid());

    Graphics::QueueSubmissionToken staleToken = token;
    ++staleToken.deviceGeneration;
    EXPECT_FALSE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/stale_owned_external_completion"))
            .setMarkerLabel("Stale Owned External Completion")
            .setToken(staleToken)
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.validForDeviceGeneration(staleToken.deviceGeneration));
    }

    Graphics::QueueSubmissionToken missingPhysicalIdentity = token;
    missingPhysicalIdentity.physicalQueueIndex = Limit<u16>::s_Max;
    missingPhysicalIdentity.deviceGeneration = 0u;
    EXPECT_FALSE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/unidentified_owned_external_completion"))
            .setMarkerLabel("Unidentified Owned External Completion")
            .setToken(missingPhysicalIdentity)
    ).valid());

    const Graphics::GpuExternalCompletionId metadataCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/metadata_external_completion"))
            .setMarkerLabel("Metadata External Completion")
    );
    ASSERT_TRUE(metadataCompletion.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        EXPECT_EQ(declarations.externalCompletionToken(metadataCompletion), nullptr);
    }

    const Graphics::GpuExternalCompletionId taskExternalDependencies[] = { completion, metadataCompletion };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/owned_external_consumer"))
        .setMarkerLabel("Owned External Consumer")
        .setExternalDependencies(taskExternalDependencies, LengthOf(taskExternalDependencies))
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_TRUE(analysis.validFor(views.declarations));
        ASSERT_TRUE(assignments.validFor(views.declarations, views.compiled));
        const Graphics::GpuCompiledTaskView compiledConsumer = views.compiled.findTask(consumer);
        ASSERT_TRUE(compiledConsumer.valid());
        EXPECT_EQ(compiledConsumer.plan->queue, queues[0u].id);
    }

    const Graphics::GpuPhysicalQueueInfo missingProducerQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology missingProducerTopology{
        .queues = missingProducerQueues,
        .queueCount = LengthOf(missingProducerQueues),
    };
    Graphics::GpuTaskGraphAnalysis invalidTopologyAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments invalidTopologyAssignments(testArena.arena);
    Graphics::GpuCompiledGraph invalidTopologyCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(
        graph,
        invalidTopologyAnalysis,
        missingProducerTopology,
        invalidTopologyAssignments,
        invalidTopologyCompiledGraph
    ));

    Graphics::GpuPhysicalQueueInfo wrongProducerClassQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    wrongProducerClassQueues[2u].queueClass = Graphics::CommandQueue::Compute;
    wrongProducerClassQueues[2u].capabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueueCapability::Transfer
    );
    const Graphics::GpuTaskGraphQueueTopology wrongProducerClassTopology{
        .queues = wrongProducerClassQueues,
        .queueCount = LengthOf(wrongProducerClassQueues),
    };
    EXPECT_FALSE(Compile(
        graph,
        invalidTopologyAnalysis,
        wrongProducerClassTopology,
        invalidTopologyAssignments,
        invalidTopologyCompiledGraph
    ));

    const Graphics::GpuTaskGraphExternalCompletionToken storedCompletionFallback{
        .completion = completion,
        .token = token,
    };
    const Graphics::GpuTaskGraphExternalCompletionToken metadataCompletionFallback{
        .completion = metadataCompletion,
        .token = token,
    };
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_FALSE(storedCompletionFallback.validFallbackFor(
            graph,
            views.declarations,
            compiledGraph,
            views.compiled
        ));
        EXPECT_TRUE(metadataCompletionFallback.validFallbackFor(
            graph,
            views.declarations,
            compiledGraph,
            views.compiled
        ));
    }

    u64 metadataFirstRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        metadataFirstRevision = declarations.declarationRevision();
    }
    const Graphics::GpuExternalCompletionId upgradedCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/metadata_external_completion"))
            .setMarkerLabel("Late Token Upgrade")
            .setToken(token)
    );
    EXPECT_EQ(upgradedCompletion, metadataCompletion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(compiledPlan.valid());
        EXPECT_NE(declarations.declarationRevision(), metadataFirstRevision);
        EXPECT_TRUE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        const Graphics::QueueSubmissionToken* const upgradedToken = declarations.externalCompletionToken(
            metadataCompletion
        );
        ASSERT_NE(upgradedToken, nullptr);
        EXPECT_EQ(upgradedToken->queue, token.queue);
        EXPECT_EQ(upgradedToken->value, token.value);
        EXPECT_EQ(upgradedToken->physicalQueueIndex, token.physicalQueueIndex);
        EXPECT_EQ(upgradedToken->deviceGeneration, token.deviceGeneration);
        EXPECT_FALSE(analysis.validFor(declarations));
        EXPECT_FALSE(assignments.validFor(declarations, compiledPlan));
        EXPECT_FALSE(compiledPlan.validFor(declarations));
    }

    u64 upgradedRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        upgradedRevision = declarations.declarationRevision();
    }
    EXPECT_EQ(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/metadata_external_completion"))
            .setMarkerLabel("Upgraded Metadata Reference")
    ), metadataCompletion);
    EXPECT_EQ(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/metadata_external_completion"))
            .setMarkerLabel("Upgraded Token Reference")
            .setToken(token)
    ), metadataCompletion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), upgradedRevision);
    }

    Graphics::GpuPhysicalQueueInfo staleTopologyQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    for(Graphics::GpuPhysicalQueueInfo& staleQueue : staleTopologyQueues)
        ++staleQueue.id.deviceGeneration;
    const Graphics::GpuTaskGraphQueueTopology staleTopology{
        .queues = staleTopologyQueues,
        .queueCount = LengthOf(staleTopologyQueues),
    };
    Graphics::GpuTaskGraphAnalysis staleAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments staleAssignments(testArena.arena);
    Graphics::GpuCompiledGraph staleCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, staleAnalysis, staleTopology, staleAssignments, staleCompiledGraph));
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_FALSE(storedCompletionFallback.validFallbackFor(
            graph,
            views.declarations,
            compiledGraph,
            views.compiled
        ));
        EXPECT_FALSE(metadataCompletionFallback.validFallbackFor(
            graph,
            views.declarations,
            compiledGraph,
            views.compiled
        ));
    }

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.externalCompletionToken(completion), nullptr);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

