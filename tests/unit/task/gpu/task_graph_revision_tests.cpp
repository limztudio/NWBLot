// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_revision_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, DeclarationStorageMutationsInvalidateCompilerSnapshots){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    {
        const auto gpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionBase), "Declaration Revision Base", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
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
        ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        EXPECT_TRUE(reads.valid());
        EXPECT_TRUE(analysis.validFor(reads.declarations));
        EXPECT_TRUE(assignments.validFor(reads.declarations, reads.compiled));
    };

    u64 previousRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    {
        const auto secondGpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionTask), "Declaration Revision Task", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto resourceResult = AddBufferMetadata(graph, Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionResource), "Declaration Revision Resource");
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    const Array<Core::GpuGraphResourceId, 1> resourceSetMembers = { resource };
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto resourceSetResult = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionResourceSet))
            .setMarkerLabel("Declaration Revision Resource Set")
        .setMembers(resourceSetMembers)
    );
    ASSERT_TRUE(resourceSetResult);
    const Core::GpuGraphResourceSetId resourceSet = *resourceSetResult;
    ASSERT_TRUE(resourceSet.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto pipelineResult = AddPipelineMetadata(graph, Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionPipeline), "Declaration Revision Pipeline", Core::GpuGraphPipelineType::Compute);
    ASSERT_TRUE(pipelineResult);
    const Core::GpuGraphPipelineId pipeline = *pipelineResult;
    ASSERT_TRUE(pipeline.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto completionResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseDeclarationRevisionCompletion)).setMarkerLabel("Declaration Revision Completion"));
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();

    const Array<u8, 4> uploadBytes = { 0x17u, 0x3au, 0x5cu, 0x8eu };
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        previousRevision = declarations.declarationRevision();
    }
    const auto uploadResult = graph.copyUploadData(uploadBytes.data(), sizeof(uploadBytes), alignof(u32));
    ASSERT_TRUE(uploadResult);
    const Core::GpuUploadBlobId upload = *uploadResult;
    ASSERT_TRUE(upload.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_NE(declarations.declarationRevision(), previousRevision);
    }
    recompileAfterMutation();
}

TEST(GpuTaskGraph, FailedAndIdempotentDeclarationsPreserveCompilerSnapshots){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    const Core::GpuGraphResourceDesc resourceDesc = Core::GpuGraphResourceDesc{}
        .setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseStableDeclarationResource))
        .setMarkerLabel("Stable Declaration Resource")
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(Core::ResourceStates::Common)
    ;
    const auto resourceResult = graph.importResource(resourceDesc);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());
    const Array<Core::GpuGraphResourceId, 1> resourceSetMembers = { resource };
    const Core::GpuGraphResourceSetDesc resourceSetDesc = Core::GpuGraphResourceSetDesc{}
        .setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseStableDeclarationResourceSet))
        .setMarkerLabel("Stable Declaration Resource Set")
        .setMembers(resourceSetMembers)
    ;
    const auto resourceSetResult = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSetResult);
    const Core::GpuGraphResourceSetId resourceSet = *resourceSetResult;
    ASSERT_TRUE(resourceSet.valid());
    const Core::GpuGraphPipelineDesc pipelineDesc = Core::GpuGraphPipelineDesc{}.setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseStableDeclarationPipeline)).setMarkerLabel("Stable Declaration Pipeline").setType(Core::GpuGraphPipelineType::Compute);
    const auto pipelineResult = graph.importPipeline(pipelineDesc);
    ASSERT_TRUE(pipelineResult);
    const Core::GpuGraphPipelineId pipeline = *pipelineResult;
    ASSERT_TRUE(pipeline.valid());
    const Core::GpuExternalCompletionDesc completionDesc = Core::GpuExternalCompletionDesc{}.setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseStableDeclarationCompletion)).setMarkerLabel("Stable Declaration Completion");
    const auto completionResult = graph.importExternalCompletion(completionDesc);
    ASSERT_TRUE(completionResult);
    const Core::GpuExternalCompletionId completion = *completionResult;
    ASSERT_TRUE(completion.valid());
    const Array<u8, 1> uploadBytes = { 0x17u };
    {
        const auto gpuTaskAdmission = graph.copyUploadData(uploadBytes.data(), sizeof(uploadBytes), alignof(u8));
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }
    {
        const auto secondGpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_DeclarationCaseStableDeclarationTask), "Stable Declaration Task", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(secondGpuTaskAdmission);
        ASSERT_TRUE(secondGpuTaskAdmission->valid());
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    u64 compiledRevision = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
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

    EXPECT_FALSE(graph.addTask(Core::GpuTaskDesc{}).has_value());
    EXPECT_FALSE(graph.importResource(Core::GpuGraphResourceDesc{}).has_value());
    EXPECT_FALSE(graph.importResourceSet(Core::GpuGraphResourceSetDesc{}).has_value());
    EXPECT_FALSE(graph.importPipeline(Core::GpuGraphPipelineDesc{}).has_value());
    EXPECT_FALSE(graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}).has_value());
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
    Core::GpuTaskGraph graph(testArena.arena);
    {
        const auto gpuTaskAdmission = AddTask(Name(TaskGraphTestUtils::s_DeclarationCaseExternalFinalRevisionBase), "External Final Revision Base", {}, {}, {}, {}, {}, graph);
        ASSERT_TRUE(gpuTaskAdmission);
        ASSERT_TRUE(gpuTaskAdmission->valid());
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis priorAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments priorAssignments(testArena.arena);
    Core::GpuCompiledGraph priorCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(priorAnalysis, priorAssignments, priorCompiledGraph, operationScratchArena, graph, topology));
    u64 compiledRevision = 0u;
    {
        const Tests::GpuTaskGraphReadViews reads(graph, priorCompiledGraph);
        ASSERT_TRUE(reads.valid());
        compiledRevision = reads.declarations.declarationRevision();
    }
    const auto externalFinalResourceResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name(TaskGraphTestUtils::s_DeclarationCaseExternalFinalRevisionResource))
            .setMarkerLabel("External Final Revision Resource")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(externalFinalResourceResult);
    const Core::GpuGraphResourceId externalFinalResource = *externalFinalResourceResult;
    ASSERT_TRUE(externalFinalResource.valid());
    {
        const Tests::GpuTaskGraphReadViews reads(graph, priorCompiledGraph);
        EXPECT_NE(reads.declarations.declarationRevision(), compiledRevision);
        EXPECT_FALSE(priorAnalysis.validFor(reads.declarations));
        EXPECT_FALSE(priorAssignments.validFor(reads.declarations));
        EXPECT_FALSE(reads.compiled.validFor(reads.declarations));
    }

    Core::GpuTaskGraphAnalysis freshAnalysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments freshAssignments(testArena.arena);
    Core::GpuCompiledGraph freshCompiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(freshAnalysis, freshAssignments, freshCompiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView freshCompiledPlan(freshCompiledGraph);
    EXPECT_FALSE(freshCompiledPlan.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

