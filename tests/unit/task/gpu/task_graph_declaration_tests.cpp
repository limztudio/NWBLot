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


constexpr AStringView s_DeferredLightingPipelinePath = "tests/task_graph/deferred_lighting_pipeline";

using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, CopiesCallerMetadataAndDestroysTypedPayloadOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddHazardDomain(graph, Name("tests/task_graph/resource"), "Resource");
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());

    const auto predecessorResult = AddTask(graph, Name("tests/task_graph/predecessor"), "Predecessor");
    ASSERT_TRUE(predecessorResult);
    const Graphics::GpuTaskId predecessor = *predecessorResult;
    ASSERT_TRUE(predecessor.valid());

    Graphics::GpuTaskId dependencies[] = { predecessor };
    Graphics::GpuTaskResourceUse uses[] = { Graphics::GpuTaskResourceUse{ .resource = resource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, }, };
    char markerLabel[] = "Stack Marker";
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/payload")).setMarkerLabel(AStringView(markerLabel)).setDependencies(dependencies, LengthOf(dependencies)).setResourceUses(uses, LengthOf(uses));

    u32 destructionCount = 0u;
    PayloadDestroyTask::Payload payload(&destructionCount);
    const auto taskResult = graph.addTask<PayloadDestroyTask>(desc, Move(payload));
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
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
    const Graphics::GpuTaskExternalStateSource externalStateSources[] = { Graphics::GpuTaskExternalStateSource{ .states = &externalStateSource, .applicableConsumerQueueClass = Graphics::CommandQueue::Compute, }, };
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/invalid_external_state_source")).setMarkerLabel("Invalid External State Source").setExternalStateSources(externalStateSources, LengthOf(externalStateSources));

    EXPECT_FALSE(graph.addTask(desc).has_value());
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
    desc.setIdentity(Name("tests/task_graph/reentrant_compile_during_declaration")).setMarkerLabel("Reentrant Compile During Declaration");
    const auto taskResult = graph.addTask<ReentrantCompileDuringDeclarationTask>(desc, Move(payload));
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;

    ASSERT_TRUE(task.valid());
    EXPECT_TRUE(compileAttempted);
    EXPECT_FALSE(compileSucceeded);
    EXPECT_FALSE(analysis.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), 1u);
    }
    EXPECT_TRUE(Analyze(scratchArena, graph, analysis));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_TRUE(analysis.validFor(declarations));
}

TEST(GpuTaskGraph, RejectsReentrantTelemetryReadsDuringDeclarationMutationWithoutChangingOutputs){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto existingTaskResult = AddTask(graph, Name("tests/task_graph/reentrant_telemetry_existing"), "Reentrant Telemetry Existing");
    ASSERT_TRUE(existingTaskResult);
    const Graphics::GpuTaskId existingTask = *existingTaskResult;
    ASSERT_TRUE(existingTask.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(scratchArena, graph, analysis, topology, assignments, compiledGraph));
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    transaction.reset(compiledGraph);

    Graphics::GpuTaskGraphQueueAssignmentTelemetryTracker tracker(testArena.arena);
    Graphics::GpuTaskQueueAssignmentTelemetry savedTelemetry;

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges);
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        ASSERT_TRUE(reads.valid());
        ASSERT_TRUE(tracker.update(reads.declarations, assignments, reads.compiled, transaction, scratchArena));
        ASSERT_TRUE(tracker.validFor(reads.declarations, assignments, reads.compiled));
        const Graphics::GpuTaskQueueAssignmentTelemetry* const initialTelemetry = tracker.find(existingTask);
        ASSERT_NE(initialTelemetry, nullptr);
        savedTelemetry = *initialTelemetry;
        const Graphics::GpuTaskGraphTelemetryOptions options{ .queueAssignments = &assignments, .compiledPlan = &reads.compiled, .queueAssignmentTelemetry = &tracker, };
        ASSERT_TRUE(reads.declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, options));
    }
    const usize nodeCount = nodes.size();
    const usize edgeCount = edges.size();
    const usize pendingEdgeCount = pendingEdges.size();

    ReentrantTelemetryDuringDeclarationState state{ .graph = &graph, .analysis = &analysis, .assignments = &assignments, .compiledGraph = &compiledGraph, .transaction = &transaction, .tracker = &tracker, .builder = &builder, .scratchArena = &scratchArena, };
    const auto probeTaskResult = graph.addTask<ReentrantTelemetryDuringDeclarationTask>(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/task_graph/reentrant_telemetry_during_declaration"))
            .setMarkerLabel("Reentrant Telemetry During Declaration"),
        ReentrantTelemetryDuringDeclarationTask::Payload(state)
    );
    ASSERT_TRUE(probeTaskResult);
    const Graphics::GpuTaskId probeTask = *probeTaskResult;

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

    ASSERT_TRUE(Compile(scratchArena, graph, analysis, topology, assignments, compiledGraph));
    transaction.reset(compiledGraph);
    Telemetry::FrameGraphNodeDescs recoveredNodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs recoveredEdges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges recoveredPendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder recoveredBuilder(scratchArena, recoveredNodes, recoveredEdges, recoveredPendingEdges);
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
    ASSERT_TRUE(reads.valid());
    ASSERT_TRUE(tracker.update(reads.declarations, assignments, reads.compiled, transaction, scratchArena));
    EXPECT_TRUE(tracker.validFor(reads.declarations, assignments, reads.compiled));
    const Graphics::GpuTaskGraphTelemetryOptions options{ .queueAssignments = &assignments, .compiledPlan = &reads.compiled, .queueAssignmentTelemetry = &tracker, };
    EXPECT_TRUE(reads.declarations.appendFrameGraphTelemetry(recoveredBuilder, analysis, scratchArena, options));
}

TEST(GpuTaskGraph, OwnsUploadBlobsAndInvalidatesThemOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u8 sourceBytes[] = { 0x17u, 0x3au, 0x5cu, 0x8eu };

    EXPECT_FALSE(graph.copyUploadData(nullptr, sizeof(sourceBytes), alignof(u8)).has_value());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, 0u, alignof(u8)).has_value());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, sizeof(sourceBytes), 3u).has_value());

    const auto blobResult = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(blobResult);
    const Graphics::GpuUploadBlobId blob = *blobResult;
    ASSERT_TRUE(blob.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.validUploadBlob(blob));
        EXPECT_EQ(declarations.uploadBlobCount(), 1u);

        sourceBytes[0u] = 0u;
        const auto storedBytes = declarations.uploadBlobData(blob);
        ASSERT_TRUE(storedBytes);
        ASSERT_EQ(storedBytes->size(), sizeof(sourceBytes));
        EXPECT_EQ((*storedBytes)[0u], 0x17u);
    }

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(declarations.validUploadBlob(blob));
        EXPECT_EQ(declarations.uploadBlobCount(), 0u);
        EXPECT_FALSE(declarations.uploadBlobData(blob));
    }

    const auto replacementResult = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(replacementResult);
    const Graphics::GpuUploadBlobId replacement = *replacementResult;
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, blob.generation);
}

TEST(GpuTaskGraph, OwnsPipelineMetadataAndInvalidatesPipelineIdsOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphPipelineDesc invalidDesc;
    invalidDesc.setIdentity(Name("tests/task_graph/invalid_pipeline")).setMarkerLabel("Invalid Pipeline");
    EXPECT_FALSE(graph.importPipeline(invalidDesc).has_value());
    EXPECT_FALSE(graph.importComputePipeline(
        Graphics::ComputePipelineHandle{},
        Graphics::GpuGraphPipelineDesc{}
            .setIdentity(Name("tests/task_graph/null_compute_pipeline"))
            .setMarkerLabel("Null Compute Pipeline")
            .setType(Graphics::GpuGraphPipelineType::Compute)
    ).has_value()
    );

    char markerLabel[] = "Deferred Lighting Pipeline";
    Graphics::GpuGraphPipelineDesc desc;
    desc.setIdentity(Name(s_DeferredLightingPipelinePath)).setMarkerLabel(AStringView(markerLabel)).setType(Graphics::GpuGraphPipelineType::Compute);
    const auto pipelineResult = graph.importPipeline(desc);
    ASSERT_TRUE(pipelineResult);
    const Graphics::GpuGraphPipelineId pipeline = *pipelineResult;
    ASSERT_TRUE(pipeline.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.validPipeline(pipeline));
        EXPECT_EQ(declarations.pipelineCount(), 1u);
    }
    {
        const auto gpuTaskAdmission = graph.importPipeline(desc);
        EXPECT_TRUE(gpuTaskAdmission);
        if(gpuTaskAdmission)
            EXPECT_EQ(*gpuTaskAdmission, pipeline);
    }

    markerLabel[0] = 'X';
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphPipelineView stored = declarations.pipelineAt(pipeline.index);
        EXPECT_EQ(stored.markerLabel, AStringView("Deferred Lighting Pipeline"));
    }

    Graphics::GpuGraphPipelineDesc mismatchedType = desc;
    mismatchedType.setType(Graphics::GpuGraphPipelineType::Graphics);
    EXPECT_FALSE(graph.importPipeline(mismatchedType).has_value());

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.pipelineCount(), 0u);
        EXPECT_FALSE(declarations.validPipeline(pipeline));
        EXPECT_EQ(declarations.computePipelineFor(pipeline), nullptr);
    }

    const auto replacementResult = AddPipelineMetadata(graph, Name(s_DeferredLightingPipelinePath), "Replacement Pipeline", Graphics::GpuGraphPipelineType::Compute);
    ASSERT_TRUE(replacementResult);
    const Graphics::GpuGraphPipelineId replacement = *replacementResult;
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, pipeline.generation);
    EXPECT_NE(replacement, pipeline);
}

TEST(GpuTaskGraph, DeclarationStorageMutationsInvalidateCompilerSnapshots){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    {
        const auto gpuTaskAdmission = AddTask(graph, Name("tests/task_graph/declaration_revision_base"), "Declaration Revision Base");
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
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
        ASSERT_TRUE(Compile(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
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
    {
        const auto secondGpuTaskAdmission = AddTask(graph, Name("tests/task_graph/declaration_revision_task"), "Declaration Revision Task");
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto resourceResult = AddBufferMetadata(graph, Name("tests/task_graph/declaration_revision_resource"), "Declaration Revision Resource");
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
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
    const auto resourceSetResult = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/declaration_revision_resource_set"))
            .setMarkerLabel("Declaration Revision Resource Set")
        .setMembers(resourceSetMembers, LengthOf(resourceSetMembers))
    );
    ASSERT_TRUE(resourceSetResult);
    const Graphics::GpuGraphResourceSetId resourceSet = *resourceSetResult;
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
    const auto pipelineResult = AddPipelineMetadata(graph, Name("tests/task_graph/declaration_revision_pipeline"), "Declaration Revision Pipeline", Graphics::GpuGraphPipelineType::Compute);
    ASSERT_TRUE(pipelineResult);
    const Graphics::GpuGraphPipelineId pipeline = *pipelineResult;
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
    const auto completionResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/declaration_revision_completion")).setMarkerLabel("Declaration Revision Completion"));
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
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
    const auto uploadResult = graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u32));
    ASSERT_TRUE(uploadResult);
    const Graphics::GpuUploadBlobId upload = *uploadResult;
    ASSERT_TRUE(upload.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();
}

TEST(GpuTaskGraph, FailedAndIdempotentDeclarationsPreserveCompilerSnapshots){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const Graphics::GpuGraphResourceDesc resourceDesc = Graphics::GpuGraphResourceDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_resource"))
        .setMarkerLabel("Stable Declaration Resource")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const auto resourceResult = graph.importResource(resourceDesc);
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuGraphResourceId resourceSetMembers[] = { resource };
    const Graphics::GpuGraphResourceSetDesc resourceSetDesc = Graphics::GpuGraphResourceSetDesc{}
        .setIdentity(Name("tests/task_graph/stable_declaration_resource_set"))
        .setMarkerLabel("Stable Declaration Resource Set")
        .setMembers(resourceSetMembers, LengthOf(resourceSetMembers))
    ;
    const auto resourceSetResult = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSetResult);
    const Graphics::GpuGraphResourceSetId resourceSet = *resourceSetResult;
    ASSERT_TRUE(resourceSet.valid());
    const Graphics::GpuGraphPipelineDesc pipelineDesc = Graphics::GpuGraphPipelineDesc{}.setIdentity(Name("tests/task_graph/stable_declaration_pipeline")).setMarkerLabel("Stable Declaration Pipeline").setType(Graphics::GpuGraphPipelineType::Compute);
    const auto pipelineResult = graph.importPipeline(pipelineDesc);
    ASSERT_TRUE(pipelineResult);
    const Graphics::GpuGraphPipelineId pipeline = *pipelineResult;
    ASSERT_TRUE(pipeline.valid());
    const Graphics::GpuExternalCompletionDesc completionDesc = Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/stable_declaration_completion")).setMarkerLabel("Stable Declaration Completion");
    const auto completionResult = graph.importExternalCompletion(completionDesc);
    ASSERT_TRUE(completionResult);
    const Graphics::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const u8 uploadBytes[] = { 0x17u };
    {
        const auto gpuTaskAdmission = graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u8));
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }
    {
        const auto secondGpuTaskAdmission = AddTask(graph, Name("tests/task_graph/stable_declaration_task"), "Stable Declaration Task");
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
    u64 compiledRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        compiledRevision = declarations.declarationRevision();
    }

    {
        const auto thirdGpuTaskAdmission = graph.importResource(resourceDesc);
        EXPECT_TRUE(thirdGpuTaskAdmission);
        if(thirdGpuTaskAdmission)
            EXPECT_EQ(*thirdGpuTaskAdmission, resource);
    }
    {
        const auto fourthGpuTaskAdmission = graph.importResourceSet(resourceSetDesc);
        EXPECT_TRUE(fourthGpuTaskAdmission);
        if(fourthGpuTaskAdmission)
            EXPECT_EQ(*fourthGpuTaskAdmission, resourceSet);
    }
    {
        const auto fifthGpuTaskAdmission = graph.importPipeline(pipelineDesc);
        EXPECT_TRUE(fifthGpuTaskAdmission);
        if(fifthGpuTaskAdmission)
            EXPECT_EQ(*fifthGpuTaskAdmission, pipeline);
    }
    {
        const auto sixthGpuTaskAdmission = graph.importExternalCompletion(completionDesc);
        EXPECT_TRUE(sixthGpuTaskAdmission);
        if(sixthGpuTaskAdmission)
            EXPECT_EQ(*sixthGpuTaskAdmission, completion);
    }
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        EXPECT_EQ(reads.declarations.declarationRevision(), compiledRevision);
        EXPECT_TRUE(analysis.validFor(reads.declarations));
        EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
        EXPECT_TRUE(reads.compiled.validFor(reads.declarations));
    }

    EXPECT_FALSE(graph.addTask(Graphics::GpuTaskDesc{}).has_value());
    EXPECT_FALSE(graph.importResource(Graphics::GpuGraphResourceDesc{}).has_value());
    EXPECT_FALSE(graph.importResourceSet(Graphics::GpuGraphResourceSetDesc{}).has_value());
    EXPECT_FALSE(graph.importPipeline(Graphics::GpuGraphPipelineDesc{}).has_value());
    EXPECT_FALSE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}).has_value());
    EXPECT_FALSE(graph.copyUploadData(nullptr, sizeof(uploadBytes), alignof(u8)).has_value());
    const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
    EXPECT_EQ(reads.declarations.declarationRevision(), compiledRevision);
    EXPECT_TRUE(analysis.validFor(reads.declarations));
    EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
    EXPECT_TRUE(reads.compiled.validFor(reads.declarations));
}

TEST(GpuTaskGraph, ExternalFinalResourceDeclarationInvalidatesPriorCompiledPlan){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    {
        const auto gpuTaskAdmission = AddTask(graph, Name("tests/task_graph/external_final_revision_base"), "External Final Revision Base");
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis priorAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments priorAssignments(testArena.arena);
    Graphics::GpuCompiledGraph priorCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(operationScratchArena, graph, priorAnalysis, topology, priorAssignments, priorCompiledGraph));
    u64 compiledRevision = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, priorCompiledGraph);
        ASSERT_TRUE(reads.valid());
        compiledRevision = reads.declarations.declarationRevision();
    }
    const auto externalFinalResourceResult = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_revision_resource"))
            .setMarkerLabel("External Final Revision Resource")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(externalFinalResourceResult);
    const Graphics::GpuGraphResourceId externalFinalResource = *externalFinalResourceResult;
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
    EXPECT_FALSE(Compile(operationScratchArena, graph, freshAnalysis, topology, freshAssignments, freshCompiledGraph));
    const Graphics::GpuCompiledGraph::ReadView freshCompiledPlan(freshCompiledGraph);
    EXPECT_FALSE(freshCompiledPlan.valid());
}

TEST(GpuTaskGraph, RejectsStaleDependencyHandlesDuringAnalysis){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto staleTaskResult = AddTask(graph, Name("tests/task_graph/old"), "Old");
    ASSERT_TRUE(staleTaskResult);
    const Graphics::GpuTaskId staleTask = *staleTaskResult;
    ASSERT_TRUE(staleTask.valid());

    graph.reset();
    const auto taskResult = AddTask(graph, Name("tests/task_graph/new"), "New", &staleTask, 1u);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidTaskDependency);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_EQ(analysis.diagnostic().relatedTask, staleTask);
}

TEST(GpuTaskGraph, RejectsEmptyLabelsAndStaleResourceAndExternalHandles){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unlabeledResource;
    unlabeledResource.setIdentity(Name("tests/task_graph/unlabeled_resource")).setType(Graphics::GpuGraphResourceType::HazardDomain);
    EXPECT_FALSE(graph.importHazardDomain(unlabeledResource).has_value());
    EXPECT_FALSE(AddTask(graph, Name("tests/task_graph/unlabeled_task"), "").has_value());
    EXPECT_FALSE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/unlabeled_external"))).has_value());

    const auto staleResourceResult = AddHazardDomain(graph, Name("tests/task_graph/stale_resource"), "Stale Resource");
    ASSERT_TRUE(staleResourceResult);
    const Graphics::GpuGraphResourceId staleResource = *staleResourceResult;
    const auto staleExternalResult = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/task_graph/stale_external")).setMarkerLabel("Stale External"));
    ASSERT_TRUE(staleExternalResult);
    const Graphics::GpuExternalCompletionId staleExternal = *staleExternalResult;
    ASSERT_TRUE(staleResource.valid());
    ASSERT_TRUE(staleExternal.valid());

    graph.reset();
    const Graphics::GpuTaskResourceUse staleResourceUse{ .resource = staleResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    const auto resourceTaskResult = AddTask(graph, Name("tests/task_graph/stale_resource_consumer"), "Stale Resource Consumer", nullptr, 0u, &staleResourceUse, 1u);
    ASSERT_TRUE(resourceTaskResult);
    const Graphics::GpuTaskId resourceTask = *resourceTaskResult;
    ASSERT_TRUE(resourceTask.valid());
    Graphics::GpuTaskGraphAnalysis resourceAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, resourceAnalysis));
    EXPECT_EQ(resourceAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
    EXPECT_EQ(resourceAnalysis.diagnostic().task, resourceTask);
    EXPECT_EQ(resourceAnalysis.diagnostic().resource, staleResource);

    graph.reset();
    Graphics::GpuTaskDesc externalDesc;
    externalDesc.setIdentity(Name("tests/task_graph/stale_external_consumer")).setMarkerLabel("Stale External Consumer").setExternalDependencies(&staleExternal, 1u);
    const auto externalTaskResult = graph.addTask(externalDesc);
    ASSERT_TRUE(externalTaskResult);
    const Graphics::GpuTaskId externalTask = *externalTaskResult;
    ASSERT_TRUE(externalTask.valid());
    Graphics::GpuTaskGraphAnalysis externalAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, externalAnalysis));
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

