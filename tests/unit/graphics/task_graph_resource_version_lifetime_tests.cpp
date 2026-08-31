// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, PreservesProducedVersionUntilAllConsumersFinish){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/lifetime_resource")
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
        Name("tests/task_graph_resource_version/lifetime_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );

    const Graphics::GpuTaskResourceUse clobberResourceUse = ResourceUse(
        resource,
        BufferRange(16u, 16u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId clobber = AddTask(
        graph,
        Name("tests/task_graph_resource_version/lifetime_clobber"),
        &clobberResourceUse,
        1u,
        nullptr,
        0u
    );

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
        Name("tests/task_graph_resource_version/lifetime_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], consumer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clobber);
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
        clobber,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
}

TEST(GpuTaskGraphResourceVersion, ReportsExplicitClobberCycleWithVersionLifetimeProvenance){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/lifetime_cycle_resource")
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
        Name("tests/task_graph_resource_version/lifetime_cycle_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );

    const Graphics::GpuTaskResourceUse clobberResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::CopyDest,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId clobber = AddTask(
        graph,
        Name("tests/task_graph_resource_version/lifetime_cycle_clobber"),
        &clobberResourceUse,
        1u,
        nullptr,
        0u,
        &producer,
        1u
    );

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
        Name("tests/task_graph_resource_version/lifetime_cycle_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u,
        &clobber,
        1u
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.diagnostic().resource, resource);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    ExpectClosedCycle(analysis);
    bool foundExplicit = false;
    bool foundVersionLifetime = false;
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        foundExplicit |= edge.hazard == Graphics::GpuTaskHazardType::Explicit;
        if(edge.hazard != Graphics::GpuTaskHazardType::VersionLifetime)
            continue;
        foundVersionLifetime = true;
        EXPECT_EQ(edge.resource, resource);
        EXPECT_EQ(edge.resourceVersion, version);
    }
    EXPECT_TRUE(foundExplicit);
    EXPECT_TRUE(foundVersionLifetime);
    ExpectDiagnosticMatchesVersionCycleEdge(analysis);
}

TEST(GpuTaskGraphResourceVersion, HonorsExplicitClobberBeforeVersionProducer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/early_clobber_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 64u)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse clobberResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::CopyDest,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId clobber = AddTask(
        graph,
        Name("tests/task_graph_resource_version/early_clobber"),
        &clobberResourceUse,
        1u,
        nullptr,
        0u
    );

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
        Name("tests/task_graph_resource_version/early_clobber_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u,
        &clobber,
        1u
    );

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
        Name("tests/task_graph_resource_version/early_clobber_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], clobber);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    EXPECT_FALSE(HasResourceVersionEdge(
        analysis,
        consumer,
        clobber,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
}

TEST(GpuTaskGraphResourceVersion, ProtectsImportedRootConsumerFromLaterClobber){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/imported_lifetime_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(0u, 64u)
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
        Name("tests/task_graph_resource_version/imported_lifetime_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    const Graphics::GpuTaskResourceUse clobberResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 64u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId clobber = AddTask(
        graph,
        Name("tests/task_graph_resource_version/imported_lifetime_clobber"),
        &clobberResourceUse,
        1u,
        nullptr,
        0u
    );
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(clobber.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], consumer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], clobber);
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        consumer,
        clobber,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.consumer == consumer)
            EXPECT_NE(edge.hazard, Graphics::GpuTaskHazardType::VersionDependency);
    }
}

TEST(GpuTaskGraphResourceVersion, TreatsImportedRootLifetimeWriterAsPreBirthForProducedVersion){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange versionRange = BufferRange(0u, 64u);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/imported_prebirth_resource")
    );
    const Graphics::GpuGraphResourceVersionId importedVersion = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        versionRange
    );
    const Graphics::GpuGraphResourceVersionId producedVersion = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        versionRange
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(importedVersion.valid());
    ASSERT_TRUE(producedVersion.valid());

    const Graphics::GpuTaskResourceUse importedConsumerResourceUse = ResourceUse(
        resource,
        versionRange,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::ReadWrite
    );
    const Graphics::GpuTaskResourceVersionUse importedConsumerVersionUse = VersionUse(
        importedVersion,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId importedConsumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/imported_prebirth_consumer"),
        &importedConsumerResourceUse,
        1u,
        &importedConsumerVersionUse,
        1u
    );

    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        versionRange,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        producedVersion,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/imported_prebirth_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        versionRange,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        producedVersion,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/imported_prebirth_produced_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    ASSERT_TRUE(importedConsumer.valid());
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], importedConsumer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        importedConsumer,
        producer,
        resource,
        importedVersion,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
    EXPECT_TRUE(HasResourceVersionEdge(
        analysis,
        producer,
        consumer,
        resource,
        producedVersion,
        Graphics::GpuTaskHazardType::VersionDependency
    ));
    EXPECT_FALSE(HasResourceVersionEdge(
        analysis,
        consumer,
        importedConsumer,
        resource,
        producedVersion,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
}

TEST(GpuTaskGraphResourceVersion, LeavesDisjointBufferWriterOutsideVersionLifetime){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/disjoint_lifetime_resource")
    );
    const Graphics::GpuGraphResourceVersionId version = AddVersion(
        graph,
        resource,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 32u)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Graphics::GpuTaskResourceUse producerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 32u),
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskResourceVersionUse producerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    const Graphics::GpuTaskId producer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/disjoint_lifetime_producer"),
        &producerResourceUse,
        1u,
        &producerVersionUse,
        1u
    );

    const Graphics::GpuTaskResourceUse consumerResourceUse = ResourceUse(
        resource,
        BufferRange(0u, 32u),
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    const Graphics::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(
        version,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId consumer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/disjoint_lifetime_consumer"),
        &consumerResourceUse,
        1u,
        &consumerVersionUse,
        1u
    );
    const Graphics::GpuTaskResourceUse writerResourceUse = ResourceUse(
        resource,
        BufferRange(64u, 32u),
        Graphics::ResourceStates::CopyDest,
        Graphics::GpuTaskResourceAccess::Write
    );
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph_resource_version/disjoint_lifetime_writer"),
        &writerResourceUse,
        1u,
        nullptr,
        0u
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(writer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_FALSE(HasResourceVersionEdge(
        analysis,
        consumer,
        writer,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
    EXPECT_FALSE(HasResourceVersionEdge(
        analysis,
        writer,
        producer,
        resource,
        version,
        Graphics::GpuTaskHazardType::VersionLifetime
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

