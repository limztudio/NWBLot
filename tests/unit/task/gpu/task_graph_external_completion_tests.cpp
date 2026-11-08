// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

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
    Core::GpuTaskGraph graph(testArena.arena);
    const Name completionIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphMissingLatePacketCompletion;
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(completionIdentity).setMarkerLabel("Late Packet Completion"));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const auto firstResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTokenPreflightFirst), "Token Preflight First", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const auto secondResult = graph.addTask(
        Core::GpuTaskDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTokenPreflightSecond))
            .setMarkerLabel("Token Preflight Second")
            .setScheduling(scheduling)
            .setDependencies(Span(&first, 1u))
            .setExternalDependencies(Span(&completion, 1u))
    );
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());
    const Array<Core::GpuPhysicalQueueInfo, 1> queues = { GraphicsQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 2u);
        const Core::GpuSubmissionPacketRange firstRange = views.compiled.packetRangeForTasks(first, first);
        const Core::GpuSubmissionPacketRange secondRange = views.compiled.packetRangeForTasks(second, second);
        EXPECT_EQ(views.declarations.externalCompletionToken(completion), nullptr);
        EXPECT_TRUE(Core::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, firstRange));
        EXPECT_FALSE(Core::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, secondRange));
        EXPECT_FALSE(Core::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, views.compiled.allPacketRange()));
    }

    const Core::QueueSubmissionToken token{ .value = 17u, .physicalQueueIndex = queues[0u].id.index, .deviceGeneration = queues[0u].id.deviceGeneration, .queue = queues[0u].queueClass, };
    {
        const auto gpuTaskAdmission = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(completionIdentity).setMarkerLabel("Bound Late Packet Completion").setToken(token));
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, completion);
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(Core::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(declarations, compiledPlan, compiledPlan.allPacketRange()));
    }
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_TRUE(Core::GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(views.declarations, views.compiled, views.compiled.allPacketRange()));
    }
}


TEST(GpuTaskGraph, RetainsAuthoritativeExternalCompletionTokens){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::QueueSubmissionToken token{ .value = 31u, .physicalQueueIndex = 2u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Transfer, };
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Owned External Completion").setToken(token));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    u64 tokenFirstRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_EQ(declarations.externalCompletionCount(), 1u);

        const Core::QueueSubmissionToken* const storedToken = declarations.externalCompletionToken(completion);
        ASSERT_NE(storedToken, nullptr);
        EXPECT_TRUE(declarations.validForDeviceGeneration(token.deviceGeneration));
        tokenFirstRevision = declarations.declarationRevision();
    }
    const auto repeatedMetadataImportResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Compatible Metadata Reference"));
    ASSERT_TRUE(repeatedMetadataImportResult);
    const Core::GpuExternalCompletionId repeatedMetadataImport = *repeatedMetadataImportResult;
    EXPECT_EQ(repeatedMetadataImport, completion);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }
    const auto repeatedTokenImportResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Compatible Token Reference").setToken(token));
    ASSERT_TRUE(repeatedTokenImportResult);
    const Core::GpuExternalCompletionId repeatedTokenImport = *repeatedTokenImportResult;
    EXPECT_EQ(repeatedTokenImport, completion);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), tokenFirstRevision);
    }

    Core::QueueSubmissionToken conflictingToken = token;
    ++conflictingToken.value;
    EXPECT_FALSE(graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphOwnedExternalCompleti)).setMarkerLabel("Conflicting Token Reference").setToken(conflictingToken)).has_value());

    Core::QueueSubmissionToken staleToken = token;
    ++staleToken.deviceGeneration;
    EXPECT_FALSE(graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(NWB::Tests::TaskGraphTestUtils::s_TaskGraphCaseTestsTaskGraphStaleOwnedExternalCompletion))
        .setMarkerLabel("Stale Owned External Completion").setToken(staleToken)).has_value());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.validForDeviceGeneration(staleToken.deviceGeneration));
    }

    Core::QueueSubmissionToken missingPhysicalIdentity = token;
    missingPhysicalIdentity.physicalQueueIndex = Limit<u16>::s_Max;
    missingPhysicalIdentity.deviceGeneration = 0u;
    EXPECT_FALSE(graph.importExternalCompletion(
        Core::GpuExternalCompletionDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphUnidentifiedOwnedExternalCompletion))
            .setMarkerLabel("Unidentified Owned External Completion")
            .setToken(missingPhysicalIdentity)
    ).has_value()
    );

    const auto metadataCompletionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Metadata External Completion"));
    ASSERT_TRUE(metadataCompletionResult);
    const Core::GpuExternalCompletionId metadataCompletion = *metadataCompletionResult;
    ASSERT_TRUE(metadataCompletion.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        EXPECT_EQ(declarations.externalCompletionToken(metadataCompletion), nullptr);
    }

    const Array<Core::GpuExternalCompletionId, 2> taskExternalDependencies = { completion, metadataCompletion };
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOwnedExternalConsumer)).setMarkerLabel("Owned External Consumer").setExternalDependencies(taskExternalDependencies);
    const auto consumerResult = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());
    const Array<Core::GpuPhysicalQueueInfo, 3> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_TRUE(analysis.validFor(views.declarations));
        ASSERT_TRUE(assignments.validFor(views.declarations, views.compiled));
    }

    const Array<Core::GpuPhysicalQueueInfo, 2> missingProducerQueues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology missingProducerTopology{ .queues = missingProducerQueues.data(), .queueCount = LengthOf(missingProducerQueues), };
    Core::GpuTaskGraphAnalysis invalidTopologyAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments invalidTopologyAssignments(testArena.arena);
    Core::GpuCompiledGraph invalidTopologyCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(invalidTopologyAnalysis, invalidTopologyAssignments, invalidTopologyCompiledGraph, operationScratchArena, graph, missingProducerTopology));

    Array<Core::GpuPhysicalQueueInfo, 3> wrongProducerClassQueues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    wrongProducerClassQueues[2u].queueClass = Core::CommandQueue::Compute;
    wrongProducerClassQueues[2u].capabilities = QueueCapabilities(Core::GpuQueueCapability::Compute, Core::GpuQueueCapability::Transfer);
    const Core::GpuPhysicalQueueTopology wrongProducerClassTopology{ .queues = wrongProducerClassQueues.data(), .queueCount = LengthOf(wrongProducerClassQueues), };
    EXPECT_FALSE(Compile(invalidTopologyAnalysis, invalidTopologyAssignments, invalidTopologyCompiledGraph, operationScratchArena, graph, wrongProducerClassTopology));

    u64 metadataFirstRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        metadataFirstRevision = declarations.declarationRevision();
    }
    const auto upgradedCompletionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Late Token Upgrade").setToken(token));
    ASSERT_TRUE(upgradedCompletionResult);
    const Core::GpuExternalCompletionId upgradedCompletion = *upgradedCompletionResult;
    EXPECT_EQ(upgradedCompletion, metadataCompletion);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(compiledPlan.valid());
        EXPECT_NE(declarations.declarationRevision(), metadataFirstRevision);
        EXPECT_TRUE(declarations.externalCompletionAt(metadataCompletion.index).hasToken);
        const Core::QueueSubmissionToken* const upgradedToken = declarations.externalCompletionToken(metadataCompletion);
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
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        upgradedRevision = declarations.declarationRevision();
    }
    {
        const auto gpuTaskAdmission = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Upgraded Metadata Reference"));
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, metadataCompletion);
    }
    {
        const auto secondGpuTaskAdmission = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(s_TestsTaskGraphMetadataExternalCompl)).setMarkerLabel("Upgraded Token Reference").setToken(token));
        EXPECT_TRUE(secondGpuTaskAdmission);
        if(secondGpuTaskAdmission)
            EXPECT_EQ(*secondGpuTaskAdmission, metadataCompletion);
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.declarationRevision(), upgradedRevision);
    }

    Array<Core::GpuPhysicalQueueInfo, 3> staleTopologyQueues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    for(Core::GpuPhysicalQueueInfo& staleQueue : staleTopologyQueues)
        ++staleQueue.id.deviceGeneration;
    const Core::GpuPhysicalQueueTopology staleTopology{ .queues = staleTopologyQueues.data(), .queueCount = LengthOf(staleTopologyQueues), };
    Core::GpuTaskGraphAnalysis staleAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments staleAssignments(testArena.arena);
    Core::GpuCompiledGraph staleCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(staleAnalysis, staleAssignments, staleCompiledGraph, operationScratchArena, graph, staleTopology));
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_TRUE(views.compiled.validFor(views.declarations));
        const Core::QueueSubmissionToken* const storedToken = views.declarations.externalCompletionToken(completion);
        const Core::QueueSubmissionToken* const upgradedToken = views.declarations.externalCompletionToken(metadataCompletion);
        ASSERT_NE(storedToken, nullptr);
        ASSERT_NE(upgradedToken, nullptr);
        EXPECT_EQ(storedToken->value, token.value);
        EXPECT_EQ(upgradedToken->value, token.value);
        EXPECT_TRUE(storedToken->matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration));
        EXPECT_TRUE(upgradedToken->matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration));
    }

    graph.reset();
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
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

