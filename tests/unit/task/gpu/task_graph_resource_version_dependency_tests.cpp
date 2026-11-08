// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_task_graph_read_views.h"
#include "task_graph_resource_version_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


TEST(GpuTaskGraphResourceVersion, OrdersConsumerBeforeProducerAndPublishesCompileStatistics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionOrderedResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Core::GpuTaskResourceRange versionRange = BufferRange(16u, 32u);
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced, versionRange);
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());

    const Core::GpuTaskResourceUse consumerResourceUse = ResourceUse(resource, versionRange, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const Core::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Consume);
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionOrderedConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Core::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionOrderedProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph));
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(analysis.validFor(declarations));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1], consumer);
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, version, Core::GpuTaskHazardType::VersionDependency));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 1u);
    EXPECT_TRUE(analysis.hasInferredEdge(producer, consumer));

}

TEST(GpuTaskGraphResourceVersion, ExportsDependencyAndLifetimeReasonsWithExplicitOverlap){
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionTelemetryResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Core::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionTelemetryProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionTelemetryConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), Span(&producer, 1u), {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const Core::GpuTaskResourceUse overwriterResourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write);
    const auto overwriterResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionTelemetryOverwriter), Span(&overwriterResourceUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(overwriterResult);
    const Core::GpuTaskId overwriter = *overwriterResult;
    ASSERT_TRUE(overwriter.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, scratchArena, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, consumer));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, version, Core::GpuTaskHazardType::VersionDependency));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, consumer, overwriter, resource, version, Core::GpuTaskHazardType::VersionLifetime));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges);
    u32 taskNodeOffset = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(declarations.appendFrameGraphTelemetry(analysis, scratchArena, builder));
        taskNodeOffset = static_cast<u32>(declarations.resourceCount());
    }
    const u32 producerNode = taskNodeOffset + producer.index;
    const u32 consumerNode = taskNodeOffset + consumer.index;
    const u32 overwriterNode = taskNodeOffset + overwriter.index;
    b8 foundDependency = false;
    b8 foundLifetime = false;
    for(const Telemetry::FrameGraphEdgeDesc& edge : edges){
        if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn)
            continue;
        if(edge.fromNodeIndex == producerNode && edge.toNodeIndex == consumerNode){
            foundDependency = true;
            EXPECT_EQ(edge.flags, Core::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency | Core::GpuTaskGraphTelemetryEdgeFlag::InferredDependency | Core::GpuTaskGraphTelemetryEdgeFlag::VersionDependency);
        }
        if(edge.fromNodeIndex == consumerNode && edge.toNodeIndex == overwriterNode){
            foundLifetime = true;
            EXPECT_EQ(edge.flags, Core::GpuTaskGraphTelemetryEdgeFlag::InferredDependency | Core::GpuTaskGraphTelemetryEdgeFlag::VersionLifetime);
        }
    }
    EXPECT_TRUE(foundDependency);
    EXPECT_TRUE(foundLifetime);
}

TEST(GpuTaskGraphResourceVersion, DeduplicatesVersionAndPhysicalHazardsInQueueScore){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionQueueScoreResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Core::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuVersionScoreProducer)).setMarkerLabel("Resource Version Queue Score Producer").setResourceUses(Span(&producerResourceUse, 1u)).setResourceVersionUses(Span(&producerVersionUse, 1u));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;

    const Core::GpuTaskCommandRequirements computeCommands{ Core::GpuQueueCapability::Compute };
    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuVersionScoreConsumer)).setMarkerLabel("Resource Version Queue Score Consumer").setResourceUses(Span(&consumerResourceUse, 1u)).setResourceVersionUses(Span(&consumerVersionUse, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 2u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 1u);
    b8 foundVersionDependency = false;
    b8 foundPhysicalRaw = false;
    for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        foundVersionDependency |= edge.hazard == Core::GpuTaskHazardType::VersionDependency;
        foundPhysicalRaw |= edge.hazard == Core::GpuTaskHazardType::ReadAfterWrite;
    }
    EXPECT_TRUE(foundVersionDependency);
    EXPECT_TRUE(foundPhysicalRaw);

    // The graphics queue has no compute support, so the compute consumer must cross queues.
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { TaskGraphTestUtils::GraphicsQueue(0u, TaskGraphTestUtils::QueueCapabilities(Core::GpuQueueCapability::Graphics, Core::GpuQueueCapability::Transfer)), TaskGraphTestUtils::DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Core::CommandQueue::Graphics);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 1);
    EXPECT_EQ(consumerAssignment->queueClass, Core::CommandQueue::Compute);
    EXPECT_EQ(consumerAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(consumerAssignment->score.ownershipTransfers, 1);
}

TEST(GpuTaskGraphResourceVersion, RetainsExternalCompletionForImportedRootConsumerAndInvalidatesLateAnalysis){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedRootResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot);
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedRootCompletion)).setMarkerLabel("Imported Root Completion"));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const Core::GpuTaskResourceVersionUse versionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Consume);
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedRootConsumer), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, Span(&completion, 1u), graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 0u);
    ASSERT_EQ(analysis.externalDependencies().size(), 1u);
    u64 analyzedRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(analysis.validFor(declarations));
        analyzedRevision = declarations.declarationRevision();
    }
    const auto lateVersionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot);
    ASSERT_TRUE(lateVersionResult);
    const Core::GpuGraphResourceVersionId lateVersion = *lateVersionResult;
    ASSERT_TRUE(lateVersion.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_NE(declarations.declarationRevision(), analyzedRevision);
        EXPECT_FALSE(analysis.validFor(declarations));
    }
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedPureResourceVersionCycle){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceAResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPureCycleResourceA));
    ASSERT_TRUE(resourceAResult);
    const Core::GpuGraphResourceId resourceA = *resourceAResult;
    const auto resourceBResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPureCycleResourceB));
    ASSERT_TRUE(resourceBResult);
    const Core::GpuGraphResourceId resourceB = *resourceBResult;
    const auto versionAResult = AddVersion(graph, resourceA, Core::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionAResult);
    const Core::GpuGraphResourceVersionId versionA = *versionAResult;
    const auto versionBResult = AddVersion(graph, resourceB, Core::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionBResult);
    const Core::GpuGraphResourceVersionId versionB = *versionBResult;
    ASSERT_TRUE(resourceA.valid());
    ASSERT_TRUE(resourceB.valid());
    ASSERT_TRUE(versionA.valid());
    ASSERT_TRUE(versionB.valid());

    const Array<Core::GpuTaskResourceUse, 2u> resourceUsesA = {
        ResourceUse(resourceA, BufferRange(0u, 64u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write),
        ResourceUse(resourceB, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read),
    };
    const Array<Core::GpuTaskResourceVersionUse, 2u> versionUsesA = { VersionUse(versionA, Core::GpuTaskResourceVersionRole::Produce), VersionUse(versionB, Core::GpuTaskResourceVersionRole::Consume), };
    const auto taskAResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPureCycleTaskA), Span(resourceUsesA), Span(versionUsesA), {}, {}, graph);
    ASSERT_TRUE(taskAResult);
    const Core::GpuTaskId taskA = *taskAResult;

    const Array<Core::GpuTaskResourceUse, 2u> resourceUsesB = {
        ResourceUse(resourceB, BufferRange(0u, 64u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write),
        ResourceUse(resourceA, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read),
    };
    const Array<Core::GpuTaskResourceVersionUse, 2u> versionUsesB = { VersionUse(versionB, Core::GpuTaskResourceVersionRole::Produce), VersionUse(versionA, Core::GpuTaskResourceVersionRole::Consume), };
    const auto taskBResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPureCycleTaskB), Span(resourceUsesB), Span(versionUsesB), {}, {}, graph);
    ASSERT_TRUE(taskBResult);
    const Core::GpuTaskId taskB = *taskBResult;
    ASSERT_TRUE(taskA.valid());
    ASSERT_TRUE(taskB.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 2u);
    ExpectClosedCycle(analysis);
    for(const Core::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        EXPECT_EQ(edge.hazard, Core::GpuTaskHazardType::VersionDependency);
        EXPECT_TRUE(edge.resource.valid());
        EXPECT_TRUE(edge.resourceVersion.valid());
    }
    ExpectDiagnosticMatchesVersionCycleEdge(analysis);
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedMixedExplicitAndResourceVersionCycle){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionMixedCycleResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionMixedCycleConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Core::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionMixedCycleProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), Span(&consumer, 1u), {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 1u);
    ExpectClosedCycle(analysis);
    b8 foundExplicit = false;
    b8 foundVersion = false;
    for(const Core::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        foundExplicit |= edge.hazard == Core::GpuTaskHazardType::Explicit;
        foundVersion |= edge.hazard == Core::GpuTaskHazardType::VersionDependency;
    }
    EXPECT_TRUE(foundExplicit);
    EXPECT_TRUE(foundVersion);
    EXPECT_EQ(analysis.diagnostic().resource, resource);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    ExpectDiagnosticMatchesVersionCycleEdge(analysis);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

