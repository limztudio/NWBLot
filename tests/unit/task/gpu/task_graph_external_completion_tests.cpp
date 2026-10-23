// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/packet_runtime_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_completion_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TestsTaskGraphOwnedExternalCompleti = "tests/task_graph/owned_external_completion";
static constexpr AStringView s_TestsTaskGraphMetadataExternalCompl = "tests/task_graph/metadata_external_completion";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RejectsSubmissionRangeWithMissingLatePacketCompletionToken){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Name completionIdentity("tests/task_graph/missing_late_packet_completion");
    const auto completionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(completionIdentity).setMarkerLabel("Late Packet Completion"));
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const auto firstResult = AddTask(graph, Name("tests/task_graph/token_preflight_first"), "Token Preflight First");
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const auto secondResult = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/token_preflight_second"))
            .setMarkerLabel("Token Preflight Second")
            .setScheduling(scheduling)
            .setDependencies(&first, 1u)
            .setExternalDependencies(&completion, 1u)
    );
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 2u);
        const Graphics::GpuSubmissionPacketRange firstRange = views.compiled.packetRangeForTasks(first, first);
        const Graphics::GpuSubmissionPacketRange secondRange = views.compiled.packetRangeForTasks(second, second);
        EXPECT_EQ(views.declarations.externalCompletionToken(completion), nullptr);
        EXPECT_TRUE(Graphics::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, firstRange));
        EXPECT_FALSE(Graphics::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, secondRange));
        EXPECT_FALSE(Graphics::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, views.compiled.allPacketRange()));
    }

    const Graphics::QueueSubmissionToken token{ .value = 17u, .physicalQueueIndex = queues[0u].id.index, .deviceGeneration = queues[0u].id.deviceGeneration, .queue = queues[0u].queueClass, };
    {
        const auto gpuTaskAdmission = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(completionIdentity).setMarkerLabel("Bound Late Packet Completion").setToken(token));
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, completion);
    }
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(Graphics::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(declarations, compiledPlan, compiledPlan.allPacketRange()));
    }
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_TRUE(Graphics::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, views.compiled.allPacketRange()));
    }
}


TEST(GpuTaskGraph, RetainsAuthoritativeExternalCompletionTokens){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::QueueSubmissionToken token{ .value = 31u, .physicalQueueIndex = 2u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Transfer, };
    const auto completionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Owned External Completion").setToken(token));
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    u64 tokenFirstRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_EQ(declarations.externalCompletionCount(), 1u);

        const Graphics::QueueSubmissionToken* const storedToken = declarations.externalCompletionToken(completion);
        ASSERT_NE(storedToken, nullptr);
        EXPECT_TRUE(declarations.validForDeviceGeneration(token.deviceGeneration));
        tokenFirstRevision = declarations.declarationRevision();
    }
    const auto repeatedMetadataImportResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Compatible Metadata Reference"));
    ASSERT_TRUE(repeatedMetadataImportResult);
    const Graphics::GpuExternalCompletionId repeatedMetadataImport = *repeatedMetadataImportResult;
    EXPECT_EQ(repeatedMetadataImport, completion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }
    const auto repeatedTokenImportResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Compatible Token Reference").setToken(token));
    ASSERT_TRUE(repeatedTokenImportResult);
    const Graphics::GpuExternalCompletionId repeatedTokenImport = *repeatedTokenImportResult;
    EXPECT_EQ(repeatedTokenImport, completion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }

    Graphics::QueueSubmissionToken conflictingToken = token;
    ++conflictingToken.value;
    EXPECT_FALSE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Conflicting Token Reference").setToken(conflictingToken)).has_value());

    Graphics::QueueSubmissionToken staleToken = token;
    ++staleToken.deviceGeneration;
    EXPECT_FALSE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/stale_owned_external_completion")).setMarkerLabel("Stale Owned External Completion").setToken(staleToken)).has_value());
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
    ).has_value()
    );

    const auto metadataCompletionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Metadata External Completion"));
    ASSERT_TRUE(metadataCompletionResult);
    const Graphics::GpuExternalCompletionId metadataCompletion = *metadataCompletionResult;
    ASSERT_TRUE(metadataCompletion.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        EXPECT_EQ(declarations.externalCompletionToken(metadataCompletion), nullptr);
    }

    const Graphics::GpuExternalCompletionId taskExternalDependencies[] = { completion, metadataCompletion };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name("tests/task_graph/owned_external_consumer")).setMarkerLabel("Owned External Consumer").setExternalDependencies(taskExternalDependencies, LengthOf(taskExternalDependencies));
    const auto consumerResult = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_TRUE(analysis.validFor(views.declarations));
        ASSERT_TRUE(assignments.validFor(views.declarations, views.compiled));
    }

    const Graphics::GpuPhysicalQueueInfo missingProducerQueues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology missingProducerTopology{ .queues = missingProducerQueues, .queueCount = LengthOf(missingProducerQueues), };
    Graphics::GpuTaskGraphAnalysis invalidTopologyAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments invalidTopologyAssignments(testArena.arena);
    Graphics::GpuCompiledGraph invalidTopologyCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(invalidTopologyAnalysis, invalidTopologyAssignments, invalidTopologyCompiledGraph, operationScratchArena, graph, missingProducerTopology));

    Graphics::GpuPhysicalQueueInfo wrongProducerClassQueues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    wrongProducerClassQueues[2u].queueClass = Graphics::CommandQueue::Compute;
    wrongProducerClassQueues[2u].capabilities = QueueCapabilities(Graphics::GpuQueueCapability::Compute, Graphics::GpuQueueCapability::Transfer);
    const Graphics::GpuPhysicalQueueTopology wrongProducerClassTopology{ .queues = wrongProducerClassQueues, .queueCount = LengthOf(wrongProducerClassQueues), };
    EXPECT_FALSE(Compile(invalidTopologyAnalysis, invalidTopologyAssignments, invalidTopologyCompiledGraph, operationScratchArena, graph, wrongProducerClassTopology));

    u64 metadataFirstRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        metadataFirstRevision = declarations.declarationRevision();
    }
    const auto upgradedCompletionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Late Token Upgrade").setToken(token));
    ASSERT_TRUE(upgradedCompletionResult);
    const Graphics::GpuExternalCompletionId upgradedCompletion = *upgradedCompletionResult;
    EXPECT_EQ(upgradedCompletion, metadataCompletion);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(compiledPlan.valid());
        EXPECT_NE(declarations.declarationRevision(), metadataFirstRevision);
        EXPECT_TRUE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        const Graphics::QueueSubmissionToken* const upgradedToken = declarations.externalCompletionToken(metadataCompletion);
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
    {
        const auto gpuTaskAdmission = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Upgraded Metadata Reference"));
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, metadataCompletion);
    }
    {
        const auto secondGpuTaskAdmission = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Upgraded Token Reference").setToken(token));
        EXPECT_TRUE(secondGpuTaskAdmission);
        if(secondGpuTaskAdmission)
            EXPECT_EQ(*secondGpuTaskAdmission, metadataCompletion);
    }
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), upgradedRevision);
    }

    Graphics::GpuPhysicalQueueInfo staleTopologyQueues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    for(Graphics::GpuPhysicalQueueInfo& staleQueue : staleTopologyQueues)
        ++staleQueue.id.deviceGeneration;
    const Graphics::GpuPhysicalQueueTopology staleTopology{ .queues = staleTopologyQueues, .queueCount = LengthOf(staleTopologyQueues), };
    Graphics::GpuTaskGraphAnalysis staleAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments staleAssignments(testArena.arena);
    Graphics::GpuCompiledGraph staleCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(staleAnalysis, staleAssignments, staleCompiledGraph, operationScratchArena, graph, staleTopology));
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_TRUE(views.compiled.validFor(views.declarations));
        const Graphics::QueueSubmissionToken* const storedToken = views.declarations.externalCompletionToken(completion);
        const Graphics::QueueSubmissionToken* const upgradedToken = views.declarations.externalCompletionToken(metadataCompletion);
        ASSERT_NE(storedToken, nullptr);
        ASSERT_NE(upgradedToken, nullptr);
        EXPECT_EQ(storedToken->value, token.value);
        EXPECT_EQ(upgradedToken->value, token.value);
        EXPECT_TRUE(storedToken->matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration));
        EXPECT_TRUE(upgradedToken->matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration));
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

