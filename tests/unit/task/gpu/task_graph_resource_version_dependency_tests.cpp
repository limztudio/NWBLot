// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"

#include <core/telemetry/frame_graph_contributor.h>

#include <tests/common/gpu_task_graph_read_views.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Telemetry = Core::Telemetry;


TEST(GpuTaskGraphResourceVersion, OrdersConsumerBeforeProducerAndPublishesCompileStatistics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/ordered_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceRange versionRange = BufferRange(16u, 32u);
    const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced, versionRange);
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(resource, versionRange, Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume);
    const auto consumerResult = AddTask(graph, Name("tests/task_graph_resource_version/ordered_consumer"), &consumerResourceUse, 1u, &consumerVersionUse, 1u);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Graphics::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(graph, Name("tests/task_graph_resource_version/ordered_producer"), &producerResourceUse, 1u, &producerVersionUse, 1u);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph));
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(analysis.validFor(declarations));
    }
    ASSERT_EQ(analysis.topologicalOrder().size(), s_ExpectedDualCount);
    EXPECT_EQ(analysis.topologicalOrder()[0], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1], consumer);
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, version, Graphics::GpuTaskHazardType::VersionDependency));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 1u);
    EXPECT_TRUE(analysis.hasInferredEdge(producer, consumer));

}

TEST(GpuTaskGraphResourceVersion, ExportsDependencyAndLifetimeReasonsWithExplicitOverlap){
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/telemetry_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Graphics::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(graph, Name("tests/task_graph_resource_version/telemetry_producer"), &producerResourceUse, 1u, &producerVersionUse, 1u);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Graphics::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(graph, Name("tests/task_graph_resource_version/telemetry_consumer"), &consumerResourceUse, 1u, &consumerVersionUse, 1u, &producer, 1u);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuTaskResourceUse overwriterResourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::UnorderedAccess, Graphics::GpuTaskResourceAccess::Write);
    const auto overwriterResult = AddTask(graph, Name("tests/task_graph_resource_version/telemetry_overwriter"), &overwriterResourceUse, 1u, nullptr, 0u);
    ASSERT_TRUE(overwriterResult);
    const Graphics::GpuTaskId overwriter = *overwriterResult;
    ASSERT_TRUE(overwriter.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(scratchArena, graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, consumer));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, version, Graphics::GpuTaskHazardType::VersionDependency));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, consumer, overwriter, resource, version, Graphics::GpuTaskHazardType::VersionLifetime));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges);
    u32 taskNodeOffset = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena));
        taskNodeOffset = static_cast<u32>(declarations.resourceCount());
    }
    const u32 producerNode = taskNodeOffset + producer.index;
    const u32 consumerNode = taskNodeOffset + consumer.index;
    const u32 overwriterNode = taskNodeOffset + overwriter.index;
    bool foundDependency = false;
    bool foundLifetime = false;
    for(const Telemetry::FrameGraphEdgeDesc& edge : edges){
        if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn)
            continue;
        if(edge.fromNodeIndex == producerNode && edge.toNodeIndex == consumerNode){
            foundDependency = true;
            EXPECT_EQ(edge.flags, Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency | Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency | Graphics::GpuTaskGraphTelemetryEdgeFlag::VersionDependency);
        }
        if(edge.fromNodeIndex == consumerNode && edge.toNodeIndex == overwriterNode){
            foundLifetime = true;
            EXPECT_EQ(edge.flags, Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency | Graphics::GpuTaskGraphTelemetryEdgeFlag::VersionLifetime);
        }
    }
    EXPECT_TRUE(foundDependency);
    EXPECT_TRUE(foundLifetime);
}

TEST(GpuTaskGraphResourceVersion, DeduplicatesVersionAndPhysicalHazardsInQueueScore){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/queue_score_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskCommandRequirements graphicsCommands{ Graphics::GpuQueueCapability::Graphics };
    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Graphics::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph_resource_version/queue_score_producer")).setMarkerLabel("Resource Version Queue Score Producer").setResourceUses(&producerResourceUse, 1u).setResourceVersionUses(&producerVersionUse, 1u);
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;

    const Graphics::GpuTaskCommandRequirements computeCommands{ Graphics::GpuQueueCapability::Compute };
    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Graphics::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name("tests/task_graph_resource_version/queue_score_consumer")).setMarkerLabel("Resource Version Queue Score Consumer").setResourceUses(&consumerResourceUse, 1u).setResourceVersionUses(&consumerVersionUse, 1u);
    const auto consumerResult = graph.addTask(consumerDesc, computeCommands);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), s_ExpectedDualCount);
    ASSERT_EQ(analysis.schedulingEdges().size(), 1u);
    bool foundVersionDependency = false;
    bool foundPhysicalRaw = false;
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        foundVersionDependency |= edge.hazard == Graphics::GpuTaskHazardType::VersionDependency;
        foundPhysicalRaw |= edge.hazard == Graphics::GpuTaskHazardType::ReadAfterWrite;
    }
    EXPECT_TRUE(foundVersionDependency);
    EXPECT_TRUE(foundPhysicalRaw);

    // The graphics queue has no compute support, so the compute consumer must cross queues.
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(0u, TaskGraphTestUtils::QueueCapabilities(Graphics::GpuQueueCapability::Graphics, Graphics::GpuQueueCapability::Transfer)), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 1);
    EXPECT_EQ(consumerAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(consumerAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(consumerAssignment->score.ownershipTransfers, 1);
}

TEST(GpuTaskGraphResourceVersion, RetainsExternalCompletionForImportedRootConsumerAndInvalidatesLateAnalysis){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/imported_root_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());
    const auto completionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph_resource_version/imported_root_completion")).setMarkerLabel("Imported Root Completion"));
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
    const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume);
    const auto consumerResult = AddTask(graph, Name("tests/task_graph_resource_version/imported_root_consumer"), &resourceUse, 1u, &versionUse, 1u, nullptr, 0u, &completion, 1u);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 0u);
    ASSERT_EQ(analysis.externalDependencies().size(), 1u);
    u64 analyzedRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_TRUE(analysis.validFor(declarations));
        analyzedRevision = declarations.declarationRevision();
    }
    const auto lateVersionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
    ASSERT_TRUE(lateVersionResult);
    const Graphics::GpuGraphResourceVersionId lateVersion = *lateVersionResult;
    ASSERT_TRUE(lateVersion.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_NE(declarations.declarationRevision(), analyzedRevision);
        EXPECT_FALSE(analysis.validFor(declarations));
    }
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedPureResourceVersionCycle){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceAResult = AddBuffer(graph, Name("tests/task_graph_resource_version/pure_cycle_resource_a"));
    ASSERT_TRUE(resourceAResult);
    const Graphics::GpuGraphResourceId resourceA = *resourceAResult;
    const auto resourceBResult = AddBuffer(graph, Name("tests/task_graph_resource_version/pure_cycle_resource_b"));
    ASSERT_TRUE(resourceBResult);
    const Graphics::GpuGraphResourceId resourceB = *resourceBResult;
    const auto versionAResult = AddVersion(graph, resourceA, Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionAResult);
    const Graphics::GpuGraphResourceVersionId versionA = *versionAResult;
    const auto versionBResult = AddVersion(graph, resourceB, Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionBResult);
    const Graphics::GpuGraphResourceVersionId versionB = *versionBResult;
    ASSERT_TRUE(resourceA.valid());
    ASSERT_TRUE(resourceB.valid());
    ASSERT_TRUE(versionA.valid());
    ASSERT_TRUE(versionB.valid());

    const Graphics::GpuTaskResourceUse resourceUsesA[] = {
        ResourceUse(resourceA, BufferRange(0u, 64u), Graphics::ResourceStates::UnorderedAccess, Graphics::GpuTaskResourceAccess::Write),
        ResourceUse(resourceB, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read),
    };
    const Graphics::GpuTaskResourceVersionUse versionUsesA[] = { VersionUse(versionA, Graphics::GpuTaskResourceVersionRole::Produce), VersionUse(versionB, Graphics::GpuTaskResourceVersionRole::Consume), };
    const auto taskAResult = AddTask(graph, Name("tests/task_graph_resource_version/pure_cycle_task_a"), resourceUsesA, LengthOf(resourceUsesA), versionUsesA, LengthOf(versionUsesA));
    ASSERT_TRUE(taskAResult);
    const Graphics::GpuTaskId taskA = *taskAResult;

    const Graphics::GpuTaskResourceUse resourceUsesB[] = {
        ResourceUse(resourceB, BufferRange(0u, 64u), Graphics::ResourceStates::UnorderedAccess, Graphics::GpuTaskResourceAccess::Write),
        ResourceUse(resourceA, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read),
    };
    const Graphics::GpuTaskResourceVersionUse versionUsesB[] = { VersionUse(versionB, Graphics::GpuTaskResourceVersionRole::Produce), VersionUse(versionA, Graphics::GpuTaskResourceVersionRole::Consume), };
    const auto taskBResult = AddTask(graph, Name("tests/task_graph_resource_version/pure_cycle_task_b"), resourceUsesB, LengthOf(resourceUsesB), versionUsesB, LengthOf(versionUsesB));
    ASSERT_TRUE(taskBResult);
    const Graphics::GpuTaskId taskB = *taskBResult;
    ASSERT_TRUE(taskA.valid());
    ASSERT_TRUE(taskB.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), s_ExpectedDualCount);
    ExpectClosedCycle(analysis);
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::VersionDependency);
        EXPECT_TRUE(edge.resource.valid());
        EXPECT_TRUE(edge.resourceVersion.valid());
    }
    ExpectDiagnosticMatchesVersionCycleEdge(analysis);
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedMixedExplicitAndResourceVersionCycle){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/mixed_cycle_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Graphics::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(graph, Name("tests/task_graph_resource_version/mixed_cycle_consumer"), &consumerResourceUse, 1u, &consumerVersionUse, 1u);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Graphics::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Graphics::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(graph, Name("tests/task_graph_resource_version/mixed_cycle_producer"), &producerResourceUse, 1u, &producerVersionUse, 1u, &consumer, 1u);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 1u);
    ExpectClosedCycle(analysis);
    bool foundExplicit = false;
    bool foundVersion = false;
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        foundExplicit |= edge.hazard == Graphics::GpuTaskHazardType::Explicit;
        foundVersion |= edge.hazard == Graphics::GpuTaskHazardType::VersionDependency;
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

