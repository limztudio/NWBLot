// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, StoresDistinctDeclarationsAndInvalidatesThemOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/storage_resource"));
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceRange range = BufferRange(16u, 32u);
    const Graphics::GpuGraphResourceVersionDesc desc = Graphics::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(range).setOrigin(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);

    const u64 initialRevision = Graphics::GpuTaskGraph::DeclarationReadView(graph).declarationRevision();
    const auto firstResult = graph.declareResourceVersion(desc);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuGraphResourceVersionId first = *firstResult;
    const u64 firstRevision = Graphics::GpuTaskGraph::DeclarationReadView(graph).declarationRevision();
    const auto secondResult = graph.declareResourceVersion(desc);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuGraphResourceVersionId second = *secondResult;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
    EXPECT_EQ(Graphics::GpuTaskGraph::DeclarationReadView(graph).resourceVersionCount(), s_ExpectedDualCount);
    EXPECT_NE(firstRevision, initialRevision);
    EXPECT_NE(Graphics::GpuTaskGraph::DeclarationReadView(graph).declarationRevision(), firstRevision);

    const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, range, Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
    Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(first, Graphics::GpuTaskResourceVersionRole::Consume);
    const auto taskResult = AddTask(graph, Name("tests/task_graph_resource_version/storage_task"), &resourceUse, 1u, &versionUse, 1u);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    versionUse.version = second;
    versionUse.role = Graphics::GpuTaskResourceVersionRole::Produce;
    u64 generation = 0u;
    u64 declarationRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const Graphics::GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);
        ASSERT_EQ(taskView.resourceVersionUseCount, 1u);
        ASSERT_NE(taskView.resourceVersionUses, nullptr);
        EXPECT_EQ(taskView.resourceVersionUses[0].version, first);
        EXPECT_EQ(taskView.resourceVersionUses[0].role, Graphics::GpuTaskResourceVersionRole::Consume);
        generation = declarations.generation();
        declarationRevision = declarations.declarationRevision();
    }
    graph.reset();
    const Graphics::GpuTaskGraph::DeclarationReadView resetDeclarations(graph);
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
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/missing_producer_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
        const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume);
        {
            const auto gpuTaskAdmission = AddTask(graph, Name("tests/task_graph_resource_version/missing_producer_consumer"), &resourceUse, 1u, &versionUse, 1u);
            ASSERT_TRUE(gpuTaskAdmission);
            ASSERT_TRUE(gpuTaskAdmission->valid());
        }

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::MissingResourceVersionProducer);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/duplicate_producer_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::UnorderedAccess, Graphics::GpuTaskResourceAccess::Write);
        const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(version, Graphics::GpuTaskResourceVersionRole::Produce);
        const auto firstResult = AddTask(graph, Name("tests/task_graph_resource_version/duplicate_producer_first"), &resourceUse, 1u, &versionUse, 1u);
        ASSERT_TRUE(firstResult);
        const Graphics::GpuTaskId first = *firstResult;
        const auto secondResult = AddTask(graph, Name("tests/task_graph_resource_version/duplicate_producer_second"), &resourceUse, 1u, &versionUse, 1u);
        ASSERT_TRUE(secondResult);
        const Graphics::GpuTaskId second = *secondResult;
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::DuplicateResourceVersionProducer);
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
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/invalid_origin_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const auto versionResult = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(BufferRange(0u, 64u)));
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto staleResourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/stale_version_resource"));
        ASSERT_TRUE(staleResourceResult);
        const Graphics::GpuGraphResourceId staleResource = *staleResourceResult;
        ASSERT_TRUE(staleResource.valid());
        graph.reset();
        const auto versionResult = AddVersion(graph, staleResource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, staleResource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/invalid_range_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot, BufferRange(0u, 0u));
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/invalid_metadata_texture_range"))
                .setMarkerLabel("Invalid Metadata Texture Version Range")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        Graphics::GpuTaskResourceRange invalidRange;
        invalidRange.textureSubresources = Graphics::TextureSubresourceSet(0u, 0u, 0u, 0u);
        const auto versionResult = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(resource).setRange(invalidRange).setOrigin(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot));
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }
}

TEST(GpuTaskGraphResourceVersion, RejectsStaleRoleAccessAndRangeVersionUses){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto oldResourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/stale_use_old_resource"));
        ASSERT_TRUE(oldResourceResult);
        const Graphics::GpuGraphResourceId oldResource = *oldResourceResult;
        const auto staleVersionResult = AddVersion(graph, oldResource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
        ASSERT_TRUE(staleVersionResult);
        const Graphics::GpuGraphResourceVersionId staleVersion = *staleVersionResult;
        ASSERT_TRUE(oldResource.valid());
        ASSERT_TRUE(staleVersion.valid());
        graph.reset();
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/stale_use_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
        const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(staleVersion, Graphics::GpuTaskResourceVersionRole::Consume);
        const auto taskResult = AddTask(graph, Name("tests/task_graph_resource_version/stale_use_task"), &resourceUse, 1u, &versionUse, 1u);
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, staleVersion);
    }

    const usize duplicateBindingCounts[] = { 2u, 10u };
    for(const usize bindingCount : duplicateBindingCounts){
        SCOPED_TRACE(bindingCount);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = AddBuffer(graph, Name("tests/task_graph_resource_version/duplicate_binding_resource"));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        Graphics::GpuTaskResourceVersionUse versionUses[10u];
        ASSERT_LE(bindingCount, LengthOf(versionUses));
        for(usize index = 0u; index + 1u < bindingCount; ++index){
            const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
            ASSERT_TRUE(versionResult);
            const Graphics::GpuGraphResourceVersionId version = *versionResult;
            ASSERT_TRUE(version.valid());
            versionUses[index] = VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume);
        }
        versionUses[bindingCount - 1u] = versionUses[0u];
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(resource, BufferRange(0u, 64u), Graphics::ResourceStates::ShaderResource, Graphics::GpuTaskResourceAccess::Read);
        if(bindingCount == LengthOf(versionUses)){
            const auto precedingTaskResult = AddTask(graph, Name("tests/task_graph_resource_version/valid_bindings_before_duplicate"), &resourceUse, 1u, versionUses, bindingCount - 1u);
            ASSERT_TRUE(precedingTaskResult);
            const Graphics::GpuTaskId precedingTask = *precedingTaskResult;
            ASSERT_TRUE(precedingTask.valid());
        }
        const auto taskResult = AddTask(graph, Name("tests/task_graph_resource_version/duplicate_binding_task"), &resourceUse, 1u, versionUses, bindingCount);
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, versionUses[0u].version);
    }

    ExpectInvalidBoundUse(
        Name("tests/task_graph_resource_version/invalid_role"),
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Graphics::GpuTaskResourceAccess::Read,
        Graphics::GpuTaskResourceVersionRole::kCount
    );
    ExpectInvalidBoundUse(
        Name("tests/task_graph_resource_version/imported_root_producer"),
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Graphics::GpuTaskResourceAccess::Write,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    ExpectInvalidBoundUse(
        Name("tests/task_graph_resource_version/invalid_access"),
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
        BufferRange(0u, 64u),
        BufferRange(0u, 64u),
        Graphics::GpuTaskResourceAccess::Read,
        Graphics::GpuTaskResourceVersionRole::Produce
    );
    ExpectInvalidBoundUse(
        Name("tests/task_graph_resource_version/uncovered_range"),
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        BufferRange(16u, 32u),
        BufferRange(0u, 16u),
        Graphics::GpuTaskResourceAccess::Read,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

