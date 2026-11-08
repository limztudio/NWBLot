// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, PreservesProducedVersionUntilAllConsumersFinish){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeResource));
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
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;

    const Core::GpuTaskResourceUse clobberResourceUse = ResourceUse(resource, BufferRange(16u, 16u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write);
    const auto clobberResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeClobber), Span(&clobberResourceUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(clobberResult);
    const Core::GpuTaskId clobber = *clobberResult;

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], consumer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], clobber);
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, version, Core::GpuTaskHazardType::VersionDependency));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, consumer, clobber, resource, version, Core::GpuTaskHazardType::VersionLifetime));
}

TEST(GpuTaskGraphResourceVersion, ReportsExplicitClobberCycleWithVersionLifetimeProvenance){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeCycleResource));
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
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeCycleProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;

    const Core::GpuTaskResourceUse clobberResourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::CopyDest, Core::GpuTaskResourceAccess::Write);
    const auto clobberResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeCycleClobber), Span(&clobberResourceUse, 1u), {}, Span(&producer, 1u), {}, graph);
    ASSERT_TRUE(clobberResult);
    const Core::GpuTaskId clobber = *clobberResult;

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionLifetimeCycleConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), Span(&clobber, 1u), {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_EQ(analysis.diagnostic().resource, resource);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    ExpectClosedCycle(analysis);
    b8 foundExplicit = false;
    b8 foundVersionLifetime = false;
    for(const Core::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        foundExplicit |= edge.hazard == Core::GpuTaskHazardType::Explicit;
        if(edge.hazard != Core::GpuTaskHazardType::VersionLifetime)
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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionEarlyClobberResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const Core::GpuTaskResourceUse clobberResourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::CopyDest, Core::GpuTaskResourceAccess::Write);
    const auto clobberResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionEarlyClobber), Span(&clobberResourceUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(clobberResult);
    const Core::GpuTaskId clobber = *clobberResult;

    const VersionProducerUses producerUses = ProducerUses(resource, version);
    const Core::GpuTaskResourceUse& producerResourceUse = producerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& producerVersionUse = producerUses.versionUse;
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionEarlyClobberProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), Span(&clobber, 1u), {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionEarlyClobberConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(clobber.valid());
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], clobber);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    EXPECT_FALSE(HasResourceVersionEdge(analysis, consumer, clobber, resource, version, Core::GpuTaskHazardType::VersionLifetime));
}

TEST(GpuTaskGraphResourceVersion, ProtectsImportedRootConsumerFromLaterClobber){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedLifetimeResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot, BufferRange(0u, 64u));
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());

    const VersionConsumerUses consumerUses = ConsumerUses(resource, version);
    const Core::GpuTaskResourceUse& consumerResourceUse = consumerUses.resourceUse;
    const Core::GpuTaskResourceVersionUse& consumerVersionUse = consumerUses.versionUse;
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedLifetimeConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    const Core::GpuTaskResourceUse clobberResourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write);
    const auto clobberResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedLifetimeClobber), Span(&clobberResourceUse, 1u), {}, {}, {}, graph);
    ASSERT_TRUE(clobberResult);
    const Core::GpuTaskId clobber = *clobberResult;
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(clobber.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], consumer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], clobber);
    EXPECT_TRUE(HasResourceVersionEdge(analysis, consumer, clobber, resource, version, Core::GpuTaskHazardType::VersionLifetime));
    for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.consumer == consumer)
            EXPECT_NE(edge.hazard, Core::GpuTaskHazardType::VersionDependency);
    }
}

TEST(GpuTaskGraphResourceVersion, TreatsImportedRootLifetimeWriterAsPreBirthForProducedVersion){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskResourceRange versionRange = BufferRange(0u, 64u);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedPrebirthResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto importedVersionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot, versionRange);
    ASSERT_TRUE(importedVersionResult);
    const Core::GpuGraphResourceVersionId importedVersion = *importedVersionResult;
    const auto producedVersionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced, versionRange);
    ASSERT_TRUE(producedVersionResult);
    const Core::GpuGraphResourceVersionId producedVersion = *producedVersionResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(importedVersion.valid());
    ASSERT_TRUE(producedVersion.valid());

    const Core::GpuTaskResourceUse importedConsumerResourceUse = ResourceUse(resource, versionRange, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::ReadWrite);
    const Core::GpuTaskResourceVersionUse importedConsumerVersionUse = VersionUse(importedVersion, Core::GpuTaskResourceVersionRole::Consume);
    const auto importedConsumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedPrebirthConsumer), Span(&importedConsumerResourceUse, 1u), Span(&importedConsumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(importedConsumerResult);
    const Core::GpuTaskId importedConsumer = *importedConsumerResult;

    const Core::GpuTaskResourceUse producerResourceUse = ResourceUse(resource, versionRange, Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write);
    const Core::GpuTaskResourceVersionUse producerVersionUse = VersionUse(producedVersion, Core::GpuTaskResourceVersionRole::Produce);
    const auto producerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedPrebirthProducer), Span(&producerResourceUse, 1u), Span(&producerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;

    const Core::GpuTaskResourceUse consumerResourceUse = ResourceUse(resource, versionRange, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    const Core::GpuTaskResourceVersionUse consumerVersionUse = VersionUse(producedVersion, Core::GpuTaskResourceVersionRole::Consume);
    const auto consumerResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedPrebirthProducedConsumer), Span(&consumerResourceUse, 1u), Span(&consumerVersionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(importedConsumer.valid());
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], importedConsumer);
    EXPECT_EQ(analysis.topologicalOrder()[1u], producer);
    EXPECT_EQ(analysis.topologicalOrder()[2u], consumer);
    EXPECT_TRUE(HasResourceVersionEdge(analysis, importedConsumer, producer, resource, importedVersion, Core::GpuTaskHazardType::VersionLifetime));
    EXPECT_TRUE(HasResourceVersionEdge(analysis, producer, consumer, resource, producedVersion, Core::GpuTaskHazardType::VersionDependency));
    EXPECT_FALSE(HasResourceVersionEdge(analysis, consumer, importedConsumer, resource, producedVersion, Core::GpuTaskHazardType::VersionLifetime));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

