// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_declaration_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, CopiesCallerMetadataAndDestroysTypedPayloadOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(graph, Name("tests/task_graph/resource"), "Resource");
    ASSERT_TRUE(resource.valid());

    const Graphics::GpuTaskId predecessor = AddTask(graph, Name("tests/task_graph/predecessor"), "Predecessor");
    ASSERT_TRUE(predecessor.valid());

    Graphics::GpuTaskId dependencies[] = { predecessor };
    Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    char markerLabel[] = "Stack Marker";
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/payload"))
        .setMarkerLabel(AStringView(markerLabel))
        .setDependencies(dependencies, LengthOf(dependencies))
        .setResourceUses(uses, LengthOf(uses))
    ;

    u32 destructionCount = 0u;
    PayloadDestroyTask::Payload payload(&destructionCount);
    const Graphics::GpuTaskId task = graph.addTask<PayloadDestroyTask>(desc, Move(payload));
    ASSERT_TRUE(task.valid());

    dependencies[0] = {};
    uses[0].resource = {};
    markerLabel[0] = 'X';
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphTaskView stored = declarations.taskAt(task.index);
        ASSERT_EQ(stored.dependencyCount, 1u);
        ASSERT_EQ(stored.resourceUseCount, 1u);
        EXPECT_EQ(stored.dependencies[0], predecessor);
        EXPECT_EQ(stored.resourceUses[0].resource, resource);
        EXPECT_EQ(stored.markerLabel, AStringView("Stack Marker"));
        EXPECT_TRUE(stored.hasPayload);
    }

    graph.reset();
    EXPECT_EQ(destructionCount, 1u);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(declarations.validTask(task));
        EXPECT_FALSE(declarations.validResource(resource));
    }
}

TEST(GpuTaskGraph, RejectsInvalidExternalStateSourceWithoutDeclarationMutation){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::CommandListResourceStateHandoff externalStateSource(testArena.arena);
    ASSERT_FALSE(externalStateSource.valid());
    const Graphics::GpuTaskExternalStateSource externalStateSources[] = {
        Graphics::GpuTaskExternalStateSource{
            .states = &externalStateSource,
            .applicableConsumerQueueClass = Graphics::CommandQueue::Compute,
        },
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/invalid_external_state_source"))
        .setMarkerLabel("Invalid External State Source")
        .setExternalStateSources(externalStateSources, LengthOf(externalStateSources))
    ;

    EXPECT_FALSE(graph.addTask(desc).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(declarations.taskCount(), 0u);
}

TEST(GpuTaskGraph, RejectsReentrantCompilerReadDuringDeclarationMutation){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    bool compileAttempted = false;
    bool compileSucceeded = true;

    ReentrantCompileDuringDeclarationTask::Payload payload;
    payload.graph = &graph;
    payload.analysis = &analysis;
    payload.scratchArena = &scratchArena;
    payload.compileAttempted = &compileAttempted;
    payload.compileSucceeded = &compileSucceeded;

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/reentrant_compile_during_declaration"))
        .setMarkerLabel("Reentrant Compile During Declaration")
    ;
    const Graphics::GpuTaskId task = graph.addTask<ReentrantCompileDuringDeclarationTask>(desc, Move(payload));

    ASSERT_TRUE(task.valid());
    EXPECT_TRUE(compileAttempted);
    EXPECT_FALSE(compileSucceeded);
    EXPECT_FALSE(analysis.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), 1u);
    }
    EXPECT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_TRUE(analysis.validFor(declarations));
}

TEST(GpuTaskGraph, RejectsReentrantTelemetryReadsDuringDeclarationMutationWithoutChangingOutputs){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId existingTask = AddTask(
        graph,
        Name("tests/task_graph/reentrant_telemetry_existing"),
        "Reentrant Telemetry Existing"
    );
    ASSERT_TRUE(existingTask.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    transaction.reset(compiledGraph);

    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Graphics::GpuTaskGraphQueueAssignmentTelemetryTracker tracker(testArena.arena);
    Graphics::GpuTaskQueueAssignmentTelemetry savedTelemetry;

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        ASSERT_TRUE(reads.valid());
        ASSERT_TRUE(tracker.update(reads.declarations, assignments, reads.compiled, transaction, scratchArena));
        ASSERT_TRUE(tracker.validFor(reads.declarations, assignments, reads.compiled));
        const Graphics::GpuTaskQueueAssignmentTelemetry* const initialTelemetry = tracker.find(existingTask);
        ASSERT_NE(initialTelemetry, nullptr);
        savedTelemetry = *initialTelemetry;
        const Graphics::GpuTaskGraphTelemetryOptions options{
            .queueAssignments = &assignments,
            .compiledPlan = &reads.compiled,
            .queueAssignmentTelemetry = &tracker,
        };
        ASSERT_TRUE(reads.declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, options));
    }
    const usize nodeCount = nodes.size();
    const usize edgeCount = edges.size();
    const usize pendingEdgeCount = pendingEdges.size();

    ReentrantTelemetryDuringDeclarationState state{
        .graph = &graph,
        .analysis = &analysis,
        .assignments = &assignments,
        .compiledGraph = &compiledGraph,
        .transaction = &transaction,
        .tracker = &tracker,
        .builder = &builder,
        .scratchArena = &scratchArena,
    };
    const Graphics::GpuTaskId probeTask = graph.addTask<ReentrantTelemetryDuringDeclarationTask>(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/reentrant_telemetry_during_declaration"))
            .setMarkerLabel("Reentrant Telemetry During Declaration"),
        ReentrantTelemetryDuringDeclarationTask::Payload(state)
    );

    ASSERT_TRUE(probeTask.valid());
    EXPECT_TRUE(state.trackerValidationAttempted);
    EXPECT_FALSE(state.trackerValidationSucceeded);
    EXPECT_TRUE(state.trackerUpdateAttempted);
    EXPECT_FALSE(state.trackerUpdateSucceeded);
    EXPECT_TRUE(state.appendAttempted);
    EXPECT_FALSE(state.appendSucceeded);
    EXPECT_EQ(nodes.size(), nodeCount);
    EXPECT_EQ(edges.size(), edgeCount);
    EXPECT_EQ(pendingEdges.size(), pendingEdgeCount);
    const Graphics::GpuTaskQueueAssignmentTelemetry* const retainedTelemetry = tracker.find(existingTask);
    ASSERT_NE(retainedTelemetry, nullptr);
    EXPECT_EQ(retainedTelemetry->assignment.task, savedTelemetry.assignment.task);
    EXPECT_EQ(retainedTelemetry->assignment.queue, savedTelemetry.assignment.queue);
    EXPECT_EQ(retainedTelemetry->assignment.reason, savedTelemetry.assignment.reason);
    EXPECT_EQ(retainedTelemetry->assignment.modifiers, savedTelemetry.assignment.modifiers);
    EXPECT_EQ(retainedTelemetry->acceptedQueue, savedTelemetry.acceptedQueue);
    EXPECT_EQ(retainedTelemetry->previousAcceptedQueue, savedTelemetry.previousAcceptedQueue);
    EXPECT_EQ(retainedTelemetry->acceptance, savedTelemetry.acceptance);

    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    transaction.reset(compiledGraph);
    Telemetry::FrameGraphNodeDescs recoveredNodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs recoveredEdges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges recoveredPendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder recoveredBuilder(recoveredNodes, recoveredEdges, recoveredPendingEdges);
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
    ASSERT_TRUE(reads.valid());
    ASSERT_TRUE(tracker.update(reads.declarations, assignments, reads.compiled, transaction, scratchArena));
    EXPECT_TRUE(tracker.validFor(reads.declarations, assignments, reads.compiled));
    const Graphics::GpuTaskGraphTelemetryOptions options{
        .queueAssignments = &assignments,
        .compiledPlan = &reads.compiled,
        .queueAssignmentTelemetry = &tracker,
    };
    EXPECT_TRUE(reads.declarations.appendFrameGraphTelemetry(recoveredBuilder, analysis, scratchArena, options));
}

TEST(GpuTaskGraph, OwnsUploadBlobsAndInvalidatesThemOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u8 sourceBytes[] = { 0x17u, 0x3au, 0x5cu, 0x8eu };

    EXPECT_FALSE(graph.copyUploadData(nullptr, sizeof(sourceBytes), alignof(u8)).valid());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, 0u, alignof(u8)).valid());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, sizeof(sourceBytes), 3u).valid());

    const Graphics::GpuUploadBlobId blob = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(blob.valid());
    usize byteSize = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.validUploadBlob(blob));
        EXPECT_EQ(declarations.uploadBlobCount(), 1u);

        sourceBytes[0u] = 0u;
        const auto* const storedBytes = static_cast<const u8*>(declarations.uploadBlobData(blob, byteSize));
        ASSERT_NE(storedBytes, nullptr);
        ASSERT_EQ(byteSize, sizeof(sourceBytes));
        EXPECT_EQ(storedBytes[0u], 0x17u);
        EXPECT_EQ(storedBytes[1u], 0x3au);
        EXPECT_EQ(storedBytes[2u], 0x5cu);
        EXPECT_EQ(storedBytes[3u], 0x8eu);
    }

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(declarations.validUploadBlob(blob));
        EXPECT_EQ(declarations.uploadBlobCount(), 0u);
        byteSize = Limit<usize>::s_Max;
        EXPECT_EQ(declarations.uploadBlobData(blob, byteSize), nullptr);
        EXPECT_EQ(byteSize, 0u);
    }

    const Graphics::GpuUploadBlobId replacement = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, blob.generation);
}

TEST(GpuTaskGraph, OwnsPipelineMetadataAndInvalidatesPipelineIdsOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphPipelineDesc invalidDesc;
    invalidDesc
        .setIdentity(Name("tests/task_graph/invalid_pipeline"))
        .setMarkerLabel("Invalid Pipeline")
    ;
    EXPECT_FALSE(graph.importPipeline(invalidDesc).valid());
    EXPECT_FALSE(graph.importComputePipeline(
        Graphics::ComputePipelineHandle{},
        Graphics::GpuGraphPipelineDesc{}
            .setIdentity(Name("tests/task_graph/null_compute_pipeline"))
            .setMarkerLabel("Null Compute Pipeline")
            .setType(Graphics::GpuGraphPipelineType::Compute)
    ).valid());

    char markerLabel[] = "Deferred Lighting Pipeline";
    Graphics::GpuGraphPipelineDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/deferred_lighting_pipeline"))
        .setMarkerLabel(AStringView(markerLabel))
        .setType(Graphics::GpuGraphPipelineType::Compute)
    ;
    const Graphics::GpuGraphPipelineId pipeline = graph.importPipeline(desc);
    ASSERT_TRUE(pipeline.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.validPipeline(pipeline));
        EXPECT_EQ(declarations.pipelineCount(), 1u);
    }
    EXPECT_EQ(graph.importPipeline(desc), pipeline);

    markerLabel[0] = 'X';
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphPipelineView stored = declarations.pipelineAt(pipeline.index);
        EXPECT_EQ(stored.id, pipeline);
        EXPECT_EQ(stored.identity, desc.identity);
        EXPECT_EQ(stored.markerLabel, AStringView("Deferred Lighting Pipeline"));
        EXPECT_EQ(stored.type, Graphics::GpuGraphPipelineType::Compute);
        EXPECT_FALSE(stored.hasBackendPipeline);
        EXPECT_EQ(declarations.graphicsPipelineFor(pipeline), nullptr);
        EXPECT_EQ(declarations.computePipelineFor(pipeline), nullptr);
        EXPECT_EQ(declarations.meshletPipelineFor(pipeline), nullptr);
        EXPECT_EQ(declarations.rayTracingPipelineFor(pipeline), nullptr);
    }

    Graphics::GpuGraphPipelineDesc mismatchedType = desc;
    mismatchedType.setType(Graphics::GpuGraphPipelineType::Graphics);
    EXPECT_FALSE(graph.importPipeline(mismatchedType).valid());

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.pipelineCount(), 0u);
        EXPECT_FALSE(declarations.validPipeline(pipeline));
        EXPECT_EQ(declarations.computePipelineFor(pipeline), nullptr);
    }

    const Graphics::GpuGraphPipelineId replacement = AddPipelineMetadata(
        graph,
        Name("tests/task_graph/deferred_lighting_pipeline"),
        "Replacement Pipeline",
        Graphics::GpuGraphPipelineType::Compute
    );
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, pipeline.generation);
    EXPECT_NE(replacement, pipeline);
}

TEST(GpuTaskGraph, DeclarationStorageMutationsInvalidateCompilerSnapshots){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/declaration_revision_base"),
        "Declaration Revision Base"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        ASSERT_TRUE(reads.valid());
        ASSERT_TRUE(analysis.validFor(reads.declarations));
        ASSERT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
    }

    const auto recompileAfterMutation = [&](){
        {
            const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
            EXPECT_FALSE(analysis.validFor(reads.declarations));
            EXPECT_FALSE(assignments.validFor(reads.declarations));
            EXPECT_FALSE(reads.compiled.validFor(reads.declarations));
        }
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        EXPECT_TRUE(reads.valid());
        EXPECT_TRUE(analysis.validFor(reads.declarations));
        EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
    };

    u64 previousRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/declaration_revision_task"),
        "Declaration Revision Task"
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const Graphics::GpuGraphResourceId resource = AddBufferMetadata(
        graph,
        Name("tests/task_graph/declaration_revision_resource"),
        "Declaration Revision Resource"
    );
    ASSERT_TRUE(resource.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    const Graphics::GpuGraphResourceId resourceSetMembers[] = { resource };
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const Graphics::GpuGraphResourceSetId resourceSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/declaration_revision_resource_set"))
            .setMarkerLabel("Declaration Revision Resource Set")
        .setMembers(resourceSetMembers, LengthOf(resourceSetMembers))
    );
    ASSERT_TRUE(resourceSet.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const Graphics::GpuGraphPipelineId pipeline = AddPipelineMetadata(
        graph,
        Name("tests/task_graph/declaration_revision_pipeline"),
        "Declaration Revision Pipeline",
        Graphics::GpuGraphPipelineType::Compute
    );
    ASSERT_TRUE(pipeline.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/declaration_revision_completion"))
            .setMarkerLabel("Declaration Revision Completion")
    );
    ASSERT_TRUE(completion.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    const u8 uploadBytes[] = { 0x17u, 0x3au, 0x5cu, 0x8eu };
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const Graphics::GpuUploadBlobId upload = graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u32));
    ASSERT_TRUE(upload.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();
}

TEST(GpuTaskGraph, FailedAndIdempotentDeclarationsPreserveCompilerSnapshots){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const Graphics::GpuGraphResourceDesc resourceDesc = Graphics::GpuGraphResourceDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_resource"))
        .setMarkerLabel("Stable Declaration Resource")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const Graphics::GpuGraphResourceId resource = graph.importResource(resourceDesc);
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuGraphResourceId resourceSetMembers[] = { resource };
    const Graphics::GpuGraphResourceSetDesc resourceSetDesc = Graphics::GpuGraphResourceSetDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_resource_set"))
        .setMarkerLabel("Stable Declaration Resource Set")
        .setMembers(resourceSetMembers, LengthOf(resourceSetMembers))
    ;
    const Graphics::GpuGraphResourceSetId resourceSet = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSet.valid());
    const Graphics::GpuGraphPipelineDesc pipelineDesc = Graphics::GpuGraphPipelineDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_pipeline"))
        .setMarkerLabel("Stable Declaration Pipeline")
        .setType(Graphics::GpuGraphPipelineType::Compute)
    ;
    const Graphics::GpuGraphPipelineId pipeline = graph.importPipeline(pipelineDesc);
    ASSERT_TRUE(pipeline.valid());
    const Graphics::GpuExternalCompletionDesc completionDesc = Graphics::GpuExternalCompletionDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_completion"))
        .setMarkerLabel("Stable Declaration Completion")
    ;
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(completionDesc);
    ASSERT_TRUE(completion.valid());
    const u8 uploadBytes[] = { 0x17u };
    ASSERT_TRUE(graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u8)).valid());
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/stable_declaration_task"),
        "Stable Declaration Task"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    u64 compiledRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        compiledRevision = declarations.declarationRevision();
    }

    EXPECT_EQ(graph.importResource(resourceDesc), resource);
    EXPECT_EQ(graph.importResourceSet(resourceSetDesc), resourceSet);
    EXPECT_EQ(graph.importPipeline(pipelineDesc), pipeline);
    EXPECT_EQ(graph.importExternalCompletion(completionDesc), completion);
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        EXPECT_EQ(reads.declarations.declarationRevision(), compiledRevision);
        EXPECT_TRUE(analysis.validFor(reads.declarations));
        EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
        EXPECT_TRUE(reads.compiled.validFor(reads.declarations));
    }

    EXPECT_FALSE(graph.addTask(Graphics::GpuTaskDesc{}).valid());
    EXPECT_FALSE(graph.importResource(Graphics::GpuGraphResourceDesc{}).valid());
    EXPECT_FALSE(graph.importResourceSet(Graphics::GpuGraphResourceSetDesc{}).valid());
    EXPECT_FALSE(graph.importPipeline(Graphics::GpuGraphPipelineDesc{}).valid());
    EXPECT_FALSE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}).valid());
    EXPECT_FALSE(graph.copyUploadData(nullptr, sizeof(uploadBytes), alignof(u8)).valid());
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
    EXPECT_EQ(reads.declarations.declarationRevision(), compiledRevision);
    EXPECT_TRUE(analysis.validFor(reads.declarations));
    EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
    EXPECT_TRUE(reads.compiled.validFor(reads.declarations));
}

TEST(GpuTaskGraph, ExternalFinalResourceDeclarationInvalidatesPriorCompiledPlan){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/external_final_revision_base"),
        "External Final Revision Base"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis priorAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments priorAssignments(testArena.arena);
    Graphics::GpuCompiledGraph priorCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, priorAnalysis, topology, priorAssignments, priorCompiledGraph));
    u64 compiledRevision = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, priorCompiledGraph);
        ASSERT_TRUE(reads.valid());
        compiledRevision = reads.declarations.declarationRevision();
    }
    const Graphics::GpuGraphResourceId externalFinalResource = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_revision_resource"))
            .setMarkerLabel("External Final Revision Resource")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(externalFinalResource.valid());
    {
        const Tests::GpuTaskGraphReadViews reads(graph, priorCompiledGraph);
        EXPECT_NE(reads.declarations.declarationRevision(), compiledRevision);
        EXPECT_FALSE(priorAnalysis.validFor(reads.declarations));
        EXPECT_FALSE(priorAssignments.validFor(reads.declarations));
        EXPECT_FALSE(reads.compiled.validFor(reads.declarations));
    }

    Graphics::GpuTaskGraphAnalysis freshAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments freshAssignments(testArena.arena);
    Graphics::GpuCompiledGraph freshCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, freshAnalysis, topology, freshAssignments, freshCompiledGraph));
    const Graphics::GpuCompiledGraph::ReadView freshCompiledPlan(freshCompiledGraph);
    EXPECT_FALSE(freshCompiledPlan.valid());
}

TEST(GpuTaskGraph, RejectsStaleDependencyHandlesDuringAnalysis){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId staleTask = AddTask(graph, Name("tests/task_graph/old"), "Old");
    ASSERT_TRUE(staleTask.valid());

    graph.reset();
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/new"),
        "New",
        &staleTask,
        1u
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidTaskDependency);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_EQ(analysis.diagnostic().relatedTask, staleTask);
}

TEST(GpuTaskGraph, RejectsEmptyLabelsAndStaleResourceAndExternalHandles){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unlabeledResource;
    unlabeledResource
        .setIdentity(Name("tests/task_graph/unlabeled_resource"))
        .setType(Graphics::GpuGraphResourceType::HazardDomain)
    ;
    EXPECT_FALSE(graph.importHazardDomain(unlabeledResource).valid());
    EXPECT_FALSE(AddTask(graph, Name("tests/task_graph/unlabeled_task"), "").valid());
    EXPECT_FALSE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/unlabeled_external"))
    ).valid());

    const Graphics::GpuGraphResourceId staleResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/stale_resource"),
        "Stale Resource"
    );
    const Graphics::GpuExternalCompletionId staleExternal = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/stale_external"))
            .setMarkerLabel("Stale External")
    );
    ASSERT_TRUE(staleResource.valid());
    ASSERT_TRUE(staleExternal.valid());

    graph.reset();
    const Graphics::GpuTaskResourceUse staleResourceUse{
        .resource = staleResource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId resourceTask = AddTask(
        graph,
        Name("tests/task_graph/stale_resource_consumer"),
        "Stale Resource Consumer",
        nullptr,
        0u,
        &staleResourceUse,
        1u
    );
    ASSERT_TRUE(resourceTask.valid());
    Graphics::GpuTaskGraphAnalysis resourceAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, resourceAnalysis));
    EXPECT_EQ(resourceAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
    EXPECT_EQ(resourceAnalysis.diagnostic().task, resourceTask);
    EXPECT_EQ(resourceAnalysis.diagnostic().resource, staleResource);

    graph.reset();
    Graphics::GpuTaskDesc externalDesc;
    externalDesc
        .setIdentity(Name("tests/task_graph/stale_external_consumer"))
        .setMarkerLabel("Stale External Consumer")
        .setExternalDependencies(&staleExternal, 1u)
    ;
    const Graphics::GpuTaskId externalTask = graph.addTask(externalDesc);
    ASSERT_TRUE(externalTask.valid());
    Graphics::GpuTaskGraphAnalysis externalAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, externalAnalysis));
    EXPECT_EQ(externalAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency);
    EXPECT_EQ(externalAnalysis.diagnostic().task, externalTask);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

