// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_native_resource_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RejectsMetadataResourceUsesBeforeNativeRecording){
    TestArena testArena;
    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    const Core::GpuTaskGraphCompiler compiler;

    const auto expectMissingTypedImport = [&](const Name& identity, const AStringView label, const Core::GpuGraphResourceType::Enum type, const Core::ResourceStates::Mask requiredState){
        Core::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(Core::GpuGraphResourceDesc{}.setIdentity(identity).setMarkerLabel(label).setType(type).setInitialState(Core::ResourceStates::Common));
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());

        const Core::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = requiredState, .access = Core::GpuTaskResourceAccess::Read, };
        Core::GpuTaskDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setResourceUses(Span(&use, 1u));
        u32 recordCount = 0u;
        const auto taskResult = graph.addTask<NativeRecordProbeTask>(desc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_TRUE(declarations.taskAt(task.index).hasRecordPayload);
        EXPECT_FALSE(compiler.compile(declarations, topology, Core::GpuTaskGraphCompileOptions{}, scratchArena, analysis, assignments, compiledGraph));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        {
            const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_FALSE(compiledPlan.valid());
        }
        EXPECT_EQ(recordCount, 0u);

        Core::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        EXPECT_TRUE(compiler.compile(declarations, topology, metadataOptions, scratchArena, analysis, assignments, compiledGraph));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        {
            const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
        }
        EXPECT_EQ(recordCount, 0u);
    };

    expectMissingTypedImport(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeMetadataTexture), "Native Metadata Texture", Core::GpuGraphResourceType::Texture, Core::ResourceStates::ShaderResource);
    expectMissingTypedImport(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeMetadataBuffer), "Native Metadata Buffer", Core::GpuGraphResourceType::Buffer, Core::ResourceStates::ShaderResource);
    expectMissingTypedImport(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeMetadataAccelStruct), "Native Metadata Accel Struct", Core::GpuGraphResourceType::AccelStruct, Core::ResourceStates::AccelStructRead);

    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto unusedTextureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeUnusedMetadataTexture), "Native Unused Metadata Texture");
        ASSERT_TRUE(unusedTextureResult);
        const Core::GpuGraphResourceId unusedTexture = *unusedTextureResult;
        const auto hazardDomainResult = AddHazardDomain(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeHazardDomain), "Native Hazard Domain");
        ASSERT_TRUE(hazardDomainResult);
        const Core::GpuGraphResourceId hazardDomain = *hazardDomainResult;
        ASSERT_TRUE(unusedTexture.valid());
        ASSERT_TRUE(hazardDomain.valid());

        const Core::GpuTaskResourceUse use{ .resource = hazardDomain, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::Write, };
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeHazardDomainTask)).setMarkerLabel("Native Hazard Domain Task").setResourceUses(Span(&use, 1u));
        u32 recordCount = 0u;
        const auto taskResult = graph.addTask<NativeRecordProbeTask>(desc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_TRUE(compiler.compile(declarations, topology, Core::GpuTaskGraphCompileOptions{}, scratchArena, analysis, assignments, compiledGraph));
            EXPECT_TRUE(analysis.valid());
            EXPECT_TRUE(assignments.valid());
            const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
            EXPECT_EQ(recordCount, 0u);
        }

        const Core::GpuTaskResourceUse invalidTypedUse{ .resource = unusedTexture, .range = {}, .requiredState = Core::ResourceStates::Unknown, .access = Core::GpuTaskResourceAccess::Read, };
        Core::GpuTaskDesc invalidTypedDesc;
        invalidTypedDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNativeUnknownTypedStateTask)).setMarkerLabel("Native Unknown Typed State Task").setResourceUses(Span(&invalidTypedUse, 1u));
        const auto invalidTypedTaskResult = graph.addTask<NativeRecordProbeTask>(invalidTypedDesc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(invalidTypedTaskResult);
        const Core::GpuTaskId invalidTypedTask = *invalidTypedTaskResult;
        ASSERT_TRUE(invalidTypedTask.valid());

        Core::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_FALSE(compiler.compile(declarations, topology, metadataOptions, scratchArena, analysis, assignments, compiledGraph));
            EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
            EXPECT_EQ(analysis.diagnostic().task, invalidTypedTask);
            EXPECT_EQ(analysis.diagnostic().resource, unusedTexture);
            EXPECT_EQ(recordCount, 0u);
        }
    }
}

TEST(GpuTaskGraph, RejectsPresentationEndpointWithoutNativeBackBuffer){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resource = graph.importResource(Core::GpuGraphResourceDesc{}
        .setIdentity(TaskGraphTestUtils::s_TaskGraphCasePresentMetadata)
        .setMarkerLabel("Present Metadata")
        .setType(Core::GpuGraphResourceType::Texture)
        .setInitialState(Core::ResourceStates::Unknown)
        .setExternalFinalState(Core::ResourceStates::Present));
    ASSERT_TRUE(resource);
    const auto producer = AddTask(TaskGraphTestUtils::s_TaskGraphCasePresentMetadataProducer, "Present Metadata Producer", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(producer);
    ASSERT_TRUE(graph.declarePresentEndpoint({ *producer, *resource }));

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, scratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint);
    EXPECT_EQ(analysis.diagnostic().task, *producer);
    EXPECT_EQ(analysis.diagnostic().resource, *resource);
    EXPECT_FALSE(analysis.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

