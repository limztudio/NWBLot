// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"

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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/ordered_resource")
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceRange versionRange = BufferRange(16u, 32u);
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        versionRange
    );
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        versionRange,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/ordered_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/ordered_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, assignments, compiledGraph));
    ASSERT_TRUE(analysis.validFor(graph));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1], consumer);
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        producer,
        consumer,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionDependency
    ));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 1u);
    EXPECT_TRUE(analysis.hasInferredEdge(producer, consumer));

    const Graphics::GpuTaskGraphCompileStatistics& statistics = compiledGraph.compileStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.resourceVersionCount, 1u);
    EXPECT_EQ(statistics.resourceVersionEdgeCount, 1u);
}

TEST(GpuTaskGraphResourceVersion, ExportsDependencyAndLifetimeReasonsWithExplicitOverlap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/telemetry_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 64u)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/telemetry_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );
    ASSERT_TRUE(producer.valid());

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/telemetry_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u,
        &producer,
        1u
    );
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuTaskResourceUse overwriterResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId overwriter = AddTask(
        graph,
        Name("tests/task_graph_resource_version/telemetry_overwriter"),
        &overwriterResourceUse,
        1u,
        nullptr,
        0u
    );
    ASSERT_TRUE(overwriter.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_TRUE(analysis.hasExplicitEdge(producer, consumer));
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        producer,
        consumer,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionDependency
    ));
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        consumer,
        overwriter,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_ResourceVersionScratchArena);
    ASSERT_TRUE(graph.appendFrameGraphTelemetry(builder, analysis, scratchArena));

    const u32 taskNodeOffset = static_cast<u32>(graph.resourceCount());
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
            EXPECT_EQ(
                edge.flags,
                Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency
                | Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency
                | Graphics::GpuTaskGraphTelemetryEdgeFlag::VersionDependency
            );
        }
        if(edge.fromNodeIndex == consumerNode && edge.toNodeIndex == overwriterNode){
            foundLifetime = true;
            EXPECT_EQ(
                edge.flags,
                Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency
                | Graphics::GpuTaskGraphTelemetryEdgeFlag::VersionLifetime
            );
        }
    }
    EXPECT_TRUE(foundDependency);
    EXPECT_TRUE(foundLifetime);
}

TEST(GpuTaskGraphResourceVersion, DeduplicatesVersionAndPhysicalHazardsInQueueScore){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/queue_score_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 64u)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph_resource_version/queue_score_producer"))
        .setMarkerLabel("Resource Version Queue Score Producer")
        .setQueue(graphicsRequest)
        .setResourceUses(&producerResourceUse, 1u)
        .setResourceVersionUses(&producerVersionUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph_resource_version/queue_score_consumer"))
        .setMarkerLabel("Resource Version Queue Score Consumer")
        .setQueue(computeRequest)
        .setResourceUses(&consumerResourceUse, 1u)
        .setResourceVersionUses(&consumerVersionUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 2u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 1u);
    bool foundVersionDependency = false;
    bool foundPhysicalRaw = false;
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        foundVersionDependency |= edge.hazard == Graphics::GpuTaskHazardType::VersionDependency;
        foundPhysicalRaw |= edge.hazard == Graphics::GpuTaskHazardType::ReadAfterWrite;
    }
    EXPECT_TRUE(foundVersionDependency);
    EXPECT_TRUE(foundPhysicalRaw);

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/imported_root_resource")
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot
    );
    ASSERT_TRUE(version.valid());
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph_resource_version/imported_root_completion"))
            .setMarkerLabel("Imported Root Completion")
    );
    ASSERT_TRUE(completion.valid());
    const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/imported_root_consumer"),
        &resourceUse,
        1u,
        &versionUse,
        1u,
        nullptr,
        0u,
        &completion,
        1u
    );
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 0u);
    ASSERT_EQ(analysis.topologicalOrder().size(), 1u);
    EXPECT_EQ(analysis.topologicalOrder()[0], consumer);
    ASSERT_EQ(analysis.externalDependencies().size(), 1u);
    EXPECT_EQ(analysis.externalDependencies()[0].completion, completion);
    EXPECT_EQ(analysis.externalDependencies()[0].consumer, consumer);
    ASSERT_TRUE(analysis.validFor(graph));

    const u64 analyzedRevision = graph.declarationRevision();
    const Graphics::GpuGraphResourceVersionId lateVersion = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot
    );
    ASSERT_TRUE(lateVersion.valid());
    EXPECT_NE(graph.declarationRevision(), analyzedRevision);
    EXPECT_FALSE(analysis.validFor(graph));
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedPureResourceVersionCycle){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resourceA = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/pure_cycle_resource_a")
    );
    const Graphics::GpuGraphResourceId resourceB = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/pure_cycle_resource_b")
    );
    const Graphics::GpuGraphResourceVersionId versionA = AddVersion(
        graph,
        resourceA,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced
    );
    const Graphics::GpuGraphResourceVersionId versionB = AddVersion(
        graph,
        resourceB,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced
    );
    ASSERT_TRUE(resourceA.valid());
    ASSERT_TRUE(resourceB.valid());
    ASSERT_TRUE(versionA.valid());
    ASSERT_TRUE(versionB.valid());

    const Graphics::GpuTaskResourceUse resourceUsesA[] = {
        ResourceUse(
            resourceA,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::GpuTaskResourceAccess::Write
        ),
        ResourceUse(
            resourceB,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::ShaderResource,
            Graphics::GpuTaskResourceAccess::Read
        ),
    };
    const Graphics::GpuTaskResourceVersionUse versionUsesA[] = {
        VersionUse(versionA, Graphics::GpuTaskResourceVersionRole::Produce),
        VersionUse(versionB, Graphics::GpuTaskResourceVersionRole::Consume),
    };
    const Graphics::GpuTaskId taskA = AddTask(
        graph,
        Name("tests/task_graph_resource_version/pure_cycle_task_a"),
        resourceUsesA,
        LengthOf(resourceUsesA),
        versionUsesA,
        LengthOf(versionUsesA)
    );

    const Graphics::GpuTaskResourceUse resourceUsesB[] = {
        ResourceUse(
            resourceB,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::GpuTaskResourceAccess::Write
        ),
        ResourceUse(
            resourceA,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::ShaderResource,
            Graphics::GpuTaskResourceAccess::Read
        ),
    };
    const Graphics::GpuTaskResourceVersionUse versionUsesB[] = {
        VersionUse(versionB, Graphics::GpuTaskResourceVersionRole::Produce),
        VersionUse(versionA, Graphics::GpuTaskResourceVersionRole::Consume),
    };
    const Graphics::GpuTaskId taskB = AddTask(
        graph,
        Name("tests/task_graph_resource_version/pure_cycle_task_b"),
        resourceUsesB,
        LengthOf(resourceUsesB),
        versionUsesB,
        LengthOf(versionUsesB)
    );
    ASSERT_TRUE(taskA.valid());
    ASSERT_TRUE(taskB.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), 2u);
    ExpectClosedCycle(analysis);
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::VersionDependency);
        EXPECT_TRUE(edge.resource.valid());
        EXPECT_TRUE(edge.resourceVersion.valid());
    }
    ExpectDiagnosticMatchesVersionCycleEdge(analysis);
}

TEST(GpuTaskGraphResourceVersion, ReportsClosedMixedExplicitAndResourceVersionCycle){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/mixed_cycle_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/mixed_cycle_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/mixed_cycle_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u,
        &consumer,
        1u
    );
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
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

