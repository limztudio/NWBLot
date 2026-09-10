// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_payload_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RejectsNonRecordableTasksDuringNativeCompilation){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/non_recordable"))
        .setMarkerLabel("Non-recordable Task")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
    ;
    const Graphics::GpuTaskId task = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{ .discardedCount = &discardedCount }
    );
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(compiledPlan.valid());
    }

    Graphics::GpuTaskGraphCompileOptions metadataOptions;
    metadataOptions.allowMetadataOnlyTasks = true;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(compiler.compile(
            declarations,
            analysis,
            topology,
            assignments,
            compiledGraph,
            scratchArena,
            metadataOptions
        ));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_TRUE(compiledPlan.validFor(declarations));
    }

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, RegistersOnlyExactTypedPayloadLifecycleSignatures){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u32 recordCount = 0u;
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/malformed_lifecycle_signatures"))
        .setMarkerLabel("Malformed Lifecycle Signatures")
    ;
    const Graphics::GpuTaskId task = graph.addTask<MalformedLifecycleTask>(
        desc,
        MalformedLifecycleTask::Payload{
            .recordCount = &recordCount,
            .acceptedCount = &acceptedCount,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    graph.reset();

    EXPECT_EQ(recordCount, 0u);
    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, 0u);
}

TEST(GpuTaskGraph, RegistersNoexceptTypedAcceptedPayloadLifecycle){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/noexcept_accepted_lifecycle"))
        .setMarkerLabel("Noexcept Accepted Lifecycle")
    ;
    const Graphics::GpuTaskId task = graph.addTask<NoexceptAcceptedLifecycleTask>(
        desc,
        NoexceptAcceptedLifecycleTask::Payload{}
    );
    ASSERT_TRUE(task.valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
    EXPECT_TRUE(declarations.taskAt(task.index).hasAcceptedPayload);
}

TEST(GpuTaskGraph, RegistersNoexceptTypedRecordAndDiscardPayloadLifecycle){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u32 recordCount = 0u;
    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/noexcept_record_discard_lifecycle"))
        .setMarkerLabel("Noexcept Record And Discard Lifecycle")
    ;
    const Graphics::GpuTaskId task = graph.addTask<NoexceptRecordDiscardLifecycleTask>(
        desc,
        NoexceptRecordDiscardLifecycleTask::Payload{
            .recordCount = &recordCount,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_TRUE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    graph.reset();
    EXPECT_EQ(recordCount, 0u);
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, RejectsMetadataResourceUsesBeforeNativeRecording){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    const Graphics::GpuTaskGraphCompiler compiler;

    const auto expectMissingTypedImport = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceType::Enum type,
        const Graphics::ResourceStates::Mask requiredState
    ){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(type)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());

        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = requiredState,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setResourceUses(&use, 1u)
        ;
        u32 recordCount = 0u;
        const Graphics::GpuTaskId task = graph.addTask<NativeRecordProbeTask>(
            desc,
            NativeRecordProbeTask::Payload{ .recordCount = &recordCount }
        );
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_TRUE(declarations.taskAt(task.index).hasRecordPayload);
        EXPECT_FALSE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_FALSE(compiledPlan.valid());
        }
        EXPECT_EQ(recordCount, 0u);

        Graphics::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        EXPECT_TRUE(compiler.compile(
            declarations,
            analysis,
            topology,
            assignments,
            compiledGraph,
            scratchArena,
            metadataOptions
        ));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
        }
        EXPECT_EQ(recordCount, 0u);
    };

    expectMissingTypedImport(
        Name("tests/task_graph/native_metadata_texture"),
        "Native Metadata Texture",
        Graphics::GpuGraphResourceType::Texture,
        Graphics::ResourceStates::ShaderResource
    );
    expectMissingTypedImport(
        Name("tests/task_graph/native_metadata_buffer"),
        "Native Metadata Buffer",
        Graphics::GpuGraphResourceType::Buffer,
        Graphics::ResourceStates::ShaderResource
    );
    expectMissingTypedImport(
        Name("tests/task_graph/native_metadata_accel_struct"),
        "Native Metadata Accel Struct",
        Graphics::GpuGraphResourceType::AccelStruct,
        Graphics::ResourceStates::AccelStructRead
    );

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId unusedTexture = AddTextureMetadata(
            graph,
            Name("tests/task_graph/native_unused_metadata_texture"),
            "Native Unused Metadata Texture"
        );
        const Graphics::GpuGraphResourceId hazardDomain = AddHazardDomain(
            graph,
            Name("tests/task_graph/native_hazard_domain"),
            "Native Hazard Domain"
        );
        ASSERT_TRUE(unusedTexture.valid());
        ASSERT_TRUE(hazardDomain.valid());

        const Graphics::GpuTaskResourceUse use{
            .resource = hazardDomain,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(Name("tests/task_graph/native_hazard_domain_task"))
            .setMarkerLabel("Native Hazard Domain Task")
            .setResourceUses(&use, 1u)
        ;
        u32 recordCount = 0u;
        const Graphics::GpuTaskId task = graph.addTask<NativeRecordProbeTask>(
            desc,
            NativeRecordProbeTask::Payload{ .recordCount = &recordCount }
        );
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena));
            EXPECT_TRUE(analysis.valid());
            EXPECT_TRUE(assignments.valid());
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
            EXPECT_EQ(recordCount, 0u);
        }

        const Graphics::GpuTaskResourceUse invalidTypedUse{
            .resource = unusedTexture,
            .range = {},
            .requiredState = Graphics::ResourceStates::Unknown,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc invalidTypedDesc;
        invalidTypedDesc
            .setIdentity(Name("tests/task_graph/native_unknown_typed_state_task"))
            .setMarkerLabel("Native Unknown Typed State Task")
            .setResourceUses(&invalidTypedUse, 1u)
        ;
        const Graphics::GpuTaskId invalidTypedTask = graph.addTask<NativeRecordProbeTask>(
            invalidTypedDesc,
            NativeRecordProbeTask::Payload{ .recordCount = &recordCount }
        );
        ASSERT_TRUE(invalidTypedTask.valid());

        Graphics::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_FALSE(compiler.compile(
                declarations,
                analysis,
                topology,
                assignments,
                compiledGraph,
                scratchArena,
                metadataOptions
            ));
            EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
            EXPECT_EQ(analysis.diagnostic().task, invalidTypedTask);
            EXPECT_EQ(analysis.diagnostic().resource, unusedTexture);
            EXPECT_EQ(recordCount, 0u);
        }
    }
}

TEST(GpuTaskGraph, DiscardsTypedPayloadLifecycleOnlyOnceWhenGraphIsAbandoned){
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(Name("tests/task_graph/lifecycle_discarded"))
            .setMarkerLabel("Discarded Lifecycle Task")
        ;
        const Graphics::GpuTaskId task = graph.addTask<PacketLifecycleTask>(
            desc,
            PacketLifecycleTask::Payload{
                .acceptedCount = &acceptedCount,
                .discardedCount = &discardedCount,
            }
        );
        ASSERT_TRUE(task.valid());

        graph.reset();
        graph.reset();

        EXPECT_EQ(acceptedCount, 0u);
        EXPECT_EQ(discardedCount, 1u);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(Name("tests/task_graph/lifecycle_unresolved"))
            .setMarkerLabel("Unresolved Lifecycle Task")
        ;
        const Graphics::GpuTaskId task = graph.addTask<PacketLifecycleTask>(
            desc,
            PacketLifecycleTask::Payload{
                .acceptedCount = &acceptedCount,
                .discardedCount = &discardedCount,
            }
        );
        ASSERT_TRUE(task.valid());
    }

    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, 2u);
}

TEST(GpuTaskGraph, DiscardsTypedPayloadWhenDeclarationIsRejected){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc invalidDesc;
    const Graphics::GpuTaskId task = graph.addTask<PacketLifecycleTask>(
        invalidDesc,
        PacketLifecycleTask::Payload{ .discardedCount = &discardedCount }
    );

    EXPECT_FALSE(task.valid());
    EXPECT_EQ(discardedCount, 1u);

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, ClearsPrimitiveAcceptedTokensWhenDeclarationFails){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskDesc desc;
    Graphics::QueueSubmissionToken token{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };

    Graphics::GpuCopyBufferTaskDesc copyBuffer;
    copyBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyBufferTask(desc, copyBuffer).valid());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuCopyTextureTaskDesc copyTexture;
    copyTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyTextureTask(desc, copyTexture).valid());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuResolveTextureTaskDesc resolveTexture;
    resolveTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addResolveTextureTask(desc, resolveTexture).valid());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuClearBufferTaskDesc clearBuffer;
    clearBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearBufferTask(desc, clearBuffer).valid());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearTextureTask(desc, clearTexture).valid());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuClearTextureRectUIntTaskDesc clearTextureRect;
    clearTextureRect.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(desc, clearTextureRect).valid());
    EXPECT_FALSE(token.valid());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

