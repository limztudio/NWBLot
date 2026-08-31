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


TEST(GpuTaskGraphResourceVersion, StoresDistinctDeclarationsAndInvalidatesThemOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddBuffer(
        graph,
        Name("tests/task_graph_resource_version/storage_resource")
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceRange range = BufferRange(16u, 32u);
    const Graphics::GpuGraphResourceVersionDesc desc = Graphics::GpuGraphResourceVersionDesc{}
        .setResource(resource)
        .setRange(range)
        .setOrigin(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot)
    ;

    const u64 initialRevision = graph.declarationRevision();
    const Graphics::GpuGraphResourceVersionId first = graph.declareResourceVersion(desc);
    const u64 firstRevision = graph.declarationRevision();
    const Graphics::GpuGraphResourceVersionId second = graph.declareResourceVersion(desc);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
    EXPECT_EQ(graph.resourceVersionCount(), 2u);
    EXPECT_NE(firstRevision, initialRevision);
    EXPECT_NE(graph.declarationRevision(), firstRevision);

    const Graphics::GpuTaskGraphResourceVersionView firstView = graph.resourceVersionAt(first.index);
    EXPECT_EQ(firstView.id, first);
    EXPECT_EQ(firstView.resource, resource);
    EXPECT_EQ(firstView.range.bufferRange, range.bufferRange);
    EXPECT_EQ(firstView.origin, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);

    const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(
        resource,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::GpuTaskResourceAccess::Read
    );
    Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(
        first,
        Graphics::GpuTaskResourceVersionRole::Consume
    );
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph_resource_version/storage_task"),
        &resourceUse,
        1u,
        &versionUse,
        1u
    );
    ASSERT_TRUE(task.valid());
    versionUse.version = second;
    versionUse.role = Graphics::GpuTaskResourceVersionRole::Produce;
    const Graphics::GpuTaskGraphTaskView taskView = graph.taskAt(task.index);
    ASSERT_EQ(taskView.resourceVersionUseCount, 1u);
    ASSERT_NE(taskView.resourceVersionUses, nullptr);
    EXPECT_EQ(taskView.resourceVersionUses[0].version, first);
    EXPECT_EQ(taskView.resourceVersionUses[0].role, Graphics::GpuTaskResourceVersionRole::Consume);

    const u64 generation = graph.generation();
    const u64 declarationRevision = graph.declarationRevision();
    graph.reset();
    EXPECT_NE(graph.generation(), generation);
    EXPECT_NE(graph.declarationRevision(), declarationRevision);
    EXPECT_EQ(graph.taskCount(), 0u);
    EXPECT_EQ(graph.resourceCount(), 0u);
    EXPECT_EQ(graph.resourceVersionCount(), 0u);
    EXPECT_FALSE(graph.validTask(task));
    EXPECT_FALSE(graph.validResourceVersion(first));
    EXPECT_FALSE(graph.validResourceVersion(second));
}

TEST(GpuTaskGraphResourceVersion, RejectsMissingAndDuplicateTaskProducedVersionProducers){
    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/missing_producer_resource")
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::TaskProduced
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
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
        ASSERT_TRUE(AddTask(
            graph,
            Name("tests/task_graph_resource_version/missing_producer_consumer"),
            &resourceUse,
            1u,
            &versionUse,
            1u
        ).valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::MissingResourceVersionProducer
        );
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/duplicate_producer_resource")
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::TaskProduced
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(
            resource,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::UnorderedAccess,
            Graphics::GpuTaskResourceAccess::Write
        );
        const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(
            version,
            Graphics::GpuTaskResourceVersionRole::Produce
        );
        const Graphics::GpuTaskId first = AddTask(
            graph,
            Name("tests/task_graph_resource_version/duplicate_producer_first"),
            &resourceUse,
            1u,
            &versionUse,
            1u
        );
        const Graphics::GpuTaskId second = AddTask(
            graph,
            Name("tests/task_graph_resource_version/duplicate_producer_second"),
            &resourceUse,
            1u,
            &versionUse,
            1u
        );
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(
            analysis.diagnostic().status,
            Graphics::GpuTaskGraphAnalysisStatus::DuplicateResourceVersionProducer
        );
        EXPECT_EQ(analysis.diagnostic().task, second);
        EXPECT_EQ(analysis.diagnostic().relatedTask, first);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

}

TEST(GpuTaskGraphResourceVersion, RejectsInvalidVersionDeclarations){
    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/invalid_origin_resource")
        );
        ASSERT_TRUE(resource.valid());
        const Graphics::GpuGraphResourceVersionId version = graph.declareResourceVersion(
            Graphics::GpuGraphResourceVersionDesc{}
                .setResource(resource)
                .setRange(BufferRange(0u, 64u))
        );
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId staleResource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/stale_version_resource")
        );
        ASSERT_TRUE(staleResource.valid());
        graph.reset();
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            staleResource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot
        );
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, staleResource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/invalid_range_resource")
        );
        ASSERT_TRUE(resource.valid());
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            BufferRange(0u, 0u)
        );
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/invalid_metadata_texture_range"))
                .setMarkerLabel("Invalid Metadata Texture Version Range")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        Graphics::GpuTaskResourceRange invalidRange;
        invalidRange.textureSubresources = Graphics::TextureSubresourceSet(0u, 0u, 0u, 0u);
        const Graphics::GpuGraphResourceVersionId version = graph.declareResourceVersion(
            Graphics::GpuGraphResourceVersionDesc{}
                .setResource(resource)
                .setRange(invalidRange)
                .setOrigin(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot)
        );
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }
}

TEST(GpuTaskGraphResourceVersion, RejectsStaleRoleAccessAndRangeVersionUses){
    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId oldResource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/stale_use_old_resource")
        );
        const Graphics::GpuGraphResourceVersionId staleVersion = AddVersion(
            graph,
            oldResource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot
        );
        ASSERT_TRUE(oldResource.valid());
        ASSERT_TRUE(staleVersion.valid());
        graph.reset();
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/stale_use_resource")
        );
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(
            resource,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::ShaderResource,
            Graphics::GpuTaskResourceAccess::Read
        );
        const Graphics::GpuTaskResourceVersionUse versionUse = VersionUse(
            staleVersion,
            Graphics::GpuTaskResourceVersionRole::Consume
        );
        const Graphics::GpuTaskId task = AddTask(
            graph,
            Name("tests/task_graph_resource_version/stale_use_task"),
            &resourceUse,
            1u,
            &versionUse,
            1u
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, staleVersion);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = AddBuffer(
            graph,
            Name("tests/task_graph_resource_version/duplicate_binding_resource")
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot
        );
        const Graphics::GpuTaskResourceUse resourceUse = ResourceUse(
            resource,
            BufferRange(0u, 64u),
            Graphics::ResourceStates::ShaderResource,
            Graphics::GpuTaskResourceAccess::Read
        );
        const Graphics::GpuTaskResourceVersionUse versionUses[] = {
            VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume),
            VersionUse(version, Graphics::GpuTaskResourceVersionRole::Consume),
        };
        const Graphics::GpuTaskId task = AddTask(
            graph,
            Name("tests/task_graph_resource_version/duplicate_binding_task"),
            &resourceUse,
            1u,
            versionUses,
            LengthOf(versionUses)
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
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

