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


TEST(GpuTaskGraphResourceVersion, StoresDistinctDeclarationsAndInvalidatesThemOnReset){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStorageResource));
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Core::GpuTaskResourceRange range = BufferRange(16u, 32u);
    const Core::GpuGraphResourceVersionDesc desc = Core::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(range).setOrigin(Core::GpuGraphResourceVersionOrigin::ImportedRoot);

    const u64 initialRevision = Core::GpuTaskGraph::DeclarationReadView(graph).declarationRevision();
    const auto firstResult = graph.declareResourceVersion(desc);
    ASSERT_TRUE(firstResult);
    const Core::GpuGraphResourceVersionId first = *firstResult;
    const u64 firstRevision = Core::GpuTaskGraph::DeclarationReadView(graph).declarationRevision();
    const auto secondResult = graph.declareResourceVersion(desc);
    ASSERT_TRUE(secondResult);
    const Core::GpuGraphResourceVersionId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
    EXPECT_EQ(Core::GpuTaskGraph::DeclarationReadView(graph).resourceVersionCount(), 2u);
    EXPECT_NE(firstRevision, initialRevision);
    EXPECT_NE(Core::GpuTaskGraph::DeclarationReadView(graph).declarationRevision(), firstRevision);

    const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, range, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
    Core::GpuTaskResourceVersionUse versionUse = VersionUse(first, Core::GpuTaskResourceVersionRole::Consume);
    const auto taskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStorageTask), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    versionUse.version = second;
    versionUse.role = Core::GpuTaskResourceVersionRole::Produce;
    u64 generation = 0u;
    u64 declarationRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const Core::GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);
        ASSERT_EQ(taskView.resourceVersionUseCount, 1u);
        ASSERT_NE(taskView.resourceVersionUses, nullptr);
        EXPECT_EQ(taskView.resourceVersionUses[0].version, first);
        EXPECT_EQ(taskView.resourceVersionUses[0].role, Core::GpuTaskResourceVersionRole::Consume);
        generation = declarations.generation();
        declarationRevision = declarations.declarationRevision();
    }
    graph.reset();
    const Core::GpuTaskGraph::DeclarationReadView resetDeclarations(graph);
    ASSERT_TRUE(resetDeclarations.valid());
    EXPECT_NE(resetDeclarations.generation(), generation);
    EXPECT_NE(resetDeclarations.declarationRevision(), declarationRevision);
    EXPECT_EQ(resetDeclarations.taskCount(), 0u);
    EXPECT_EQ(resetDeclarations.resourceCount(), 0u);
    EXPECT_EQ(resetDeclarations.resourceVersionCount(), 0u);
    EXPECT_FALSE(resetDeclarations.validTask(task));
    EXPECT_FALSE(resetDeclarations.validResourceVersion(first));
    EXPECT_FALSE(resetDeclarations.validResourceVersion(second));
}

TEST(GpuTaskGraphResourceVersion, RejectsMissingAndDuplicateTaskProducedVersionProducers){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionMissingProducerResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced);
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
        const Core::GpuTaskResourceVersionUse versionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Consume);
        {
            const auto gpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionMissingProducerConsumer), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::MissingResourceVersionProducer);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionDuplicateProducerResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::TaskProduced);
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::UnorderedAccess, Core::GpuTaskResourceAccess::Write);
        const Core::GpuTaskResourceVersionUse versionUse = VersionUse(version, Core::GpuTaskResourceVersionRole::Produce);
        const auto firstResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionDuplicateProducerFirst), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
        ASSERT_TRUE(firstResult);
        const Core::GpuTaskId first = *firstResult;
        const auto secondResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionDuplicateProducerSecond), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
        ASSERT_TRUE(secondResult);
        const Core::GpuTaskId second = *secondResult;
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::DuplicateResourceVersionProducer);
        EXPECT_EQ(analysis.diagnostic().task, second);
        EXPECT_EQ(analysis.diagnostic().relatedTask, first);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }
}

TEST(GpuTaskGraphResourceVersion, RejectsInvalidVersionDeclarations){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionInvalidOriginResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(BufferRange(0u, 64u)));
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto staleResourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStaleVersionResource));
        ASSERT_TRUE(staleResourceResult);
        const Core::GpuGraphResourceId staleResource = *staleResourceResult;
        ASSERT_TRUE(staleResource.valid());
        graph.reset();
        const auto versionResult = AddVersion(graph, staleResource, Core::GpuGraphResourceVersionOrigin::ImportedRoot);
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, staleResource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionInvalidRangeResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot, BufferRange(0u, 0u));
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionInvalidMetadataTextureRange))
                .setMarkerLabel("Invalid Metadata Texture Version Range")
                .setType(Core::GpuGraphResourceType::Texture)
                .setInitialState(Core::ResourceStates::Common)
        );
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        Core::GpuTaskResourceRange invalidRange;
        invalidRange.textureSubresources = Core::TextureSubresourceSet(0u, 0u, 0u, 0u);
        const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(invalidRange).setOrigin(Core::GpuGraphResourceVersionOrigin::ImportedRoot));
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }
}

TEST(GpuTaskGraphResourceVersion, RejectsStaleRoleAccessAndRangeVersionUses){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto oldResourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStaleUseOldResource));
        ASSERT_TRUE(oldResourceResult);
        const Core::GpuGraphResourceId oldResource = *oldResourceResult;
        const auto staleVersionResult = AddVersion(graph, oldResource, Core::GpuGraphResourceVersionOrigin::ImportedRoot);
        ASSERT_TRUE(staleVersionResult);
        const Core::GpuGraphResourceVersionId staleVersion = *staleVersionResult;
        ASSERT_TRUE(oldResource.valid());
        ASSERT_TRUE(staleVersion.valid());
        graph.reset();
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStaleUseResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
        const Core::GpuTaskResourceVersionUse versionUse = VersionUse(staleVersion, Core::GpuTaskResourceVersionRole::Consume);
        const auto taskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionStaleUseTask), Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(task.valid());
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, staleVersion);
    }

    const Array<usize, 2u> duplicateBindingCounts = { 2u, 10u };
    for(const usize bindingCount : duplicateBindingCounts){
        SCOPED_TRACE(bindingCount);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionDuplicateBindingResource));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        Array<Core::GpuTaskResourceVersionUse, 10u> versionUses;
        ASSERT_LE(bindingCount, LengthOf(versionUses));
        for(usize index = 0u; index + 1u < bindingCount; ++index){
            const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot);
            ASSERT_TRUE(versionResult);
            const Core::GpuGraphResourceVersionId version = *versionResult;
            ASSERT_TRUE(version.valid());
            versionUses[index] = VersionUse(version, Core::GpuTaskResourceVersionRole::Consume);
        }
        versionUses[bindingCount - 1u] = versionUses[0u];
        const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
        if(bindingCount == LengthOf(versionUses)){
            const auto precedingTaskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionValidBindingsBeforeDuplicate), Span(&resourceUse, 1u), Span(versionUses.data(), bindingCount - 1u), {}, {}, graph);
            ASSERT_TRUE(precedingTaskResult);
            const Core::GpuTaskId precedingTask = *precedingTaskResult;
            ASSERT_TRUE(precedingTask.valid());
        }
        const auto taskResult = AddTask(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionDuplicateBindingTask), Span(&resourceUse, 1u), Span(versionUses.data(), bindingCount), {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, versionUses[0u].version);
    }

    ExpectInvalidBoundUse(
        Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionInvalidRole),
        Core::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Core::GpuTaskResourceAccess::Read,
        Core::GpuTaskResourceVersionRole::Count
    );
    ExpectInvalidBoundUse(
        Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionImportedRootProducer),
        Core::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Core::GpuTaskResourceAccess::Write,
        Core::GpuTaskResourceVersionRole::Produce
    );
    ExpectInvalidBoundUse(
        Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionInvalidAccess),
        Core::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Core::GpuTaskResourceAccess::Read,
        Core::GpuTaskResourceVersionRole::Produce
    );
    ExpectInvalidBoundUse(
        Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionUncoveredRange),
        Core::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(16u, 32u),
        BufferRange(0u, 16u),
        Core::GpuTaskResourceAccess::Read,
        Core::GpuTaskResourceVersionRole::Consume
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

