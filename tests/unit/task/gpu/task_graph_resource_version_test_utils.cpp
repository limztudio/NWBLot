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


[[nodiscard]] Core::GpuTaskResourceRange BufferRange(const u64 byteOffset, const u64 byteSize)noexcept{
    Core::GpuTaskResourceRange range;
    range.bufferRange = Core::BufferRange(byteOffset, byteSize);
    return range;
}

[[nodiscard]] b8 Compile(Core::GpuTaskGraphAnalysis& analysis, Core::GpuTaskGraphQueueAssignments& assignments, Core::GpuCompiledGraph& compiledGraph, Core::Alloc::ScratchArena& scratchArena, const Core::GpuTaskGraph& graph){
    const Core::GpuPhysicalQueueInfo queue = TaskGraphTestUtils::GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    return compiler.compile(declarations, topology, scratchArena, analysis, assignments, compiledGraph, options);
}

void ExpectClosedCycle(const Core::GpuTaskGraphAnalysis& analysis){
    ASSERT_GE(analysis.cyclePath().size(), 3u);
    EXPECT_EQ(analysis.cyclePath().front(), analysis.cyclePath().back());
    ASSERT_EQ(analysis.cycleEdges().size(), analysis.cyclePath().size() - 1u);
    for(usize edgeIndex = 0u; edgeIndex < analysis.cycleEdges().size(); ++edgeIndex){
        EXPECT_EQ(analysis.cycleEdges()[edgeIndex].producer, analysis.cyclePath()[edgeIndex]);
        EXPECT_EQ(analysis.cycleEdges()[edgeIndex].consumer, analysis.cyclePath()[edgeIndex + 1u]);
    }
}

[[nodiscard]] b8 HasResourceVersionEdge(
    const Core::GpuTaskGraphAnalysis& analysis,
    const Core::GpuTaskId producer,
    const Core::GpuTaskId consumer,
    const Core::GpuGraphResourceId resource,
    const Core::GpuGraphResourceVersionId version,
    const Core::GpuTaskHazardType::Enum hazard
)noexcept{
    for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.producer == producer && edge.consumer == consumer && edge.resource == resource && edge.resourceVersion == version && edge.hazard == hazard)
            return true;
    }
    return false;
}

void ExpectDiagnosticMatchesVersionCycleEdge(const Core::GpuTaskGraphAnalysis& analysis){
    const Core::GpuTaskGraphAnalysisDiagnostic& diagnostic = analysis.diagnostic();
    ASSERT_TRUE(diagnostic.resource.valid());
    ASSERT_TRUE(diagnostic.resourceVersion.valid());
    b8 matched = false;
    for(const Core::GpuTaskDependencyEdge& edge : analysis.cycleEdges()){
        if(
            edge.producer == diagnostic.relatedTask
            && edge.consumer == diagnostic.task
            && edge.resource == diagnostic.resource
            && edge.resourceVersion == diagnostic.resourceVersion
            && (edge.hazard == Core::GpuTaskHazardType::VersionDependency || edge.hazard == Core::GpuTaskHazardType::VersionLifetime)
        ){
            matched = true;
            break;
        }
    }
    EXPECT_TRUE(matched);
}

void ExpectInvalidBoundUse(
    const Name& identity,
    const Core::GpuGraphResourceVersionOrigin::Enum origin,
    const Core::GpuTaskResourceRange& versionRange,
    const Core::GpuTaskResourceRange& physicalRange,
    const Core::GpuTaskResourceAccess::Enum access,
    const Core::GpuTaskResourceVersionRole::Enum role
){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto resourceResult = AddBuffer(graph, identity);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    const auto versionResult = AddVersion(graph, resource, origin, versionRange);
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    const Core::ResourceStates::Mask state = access == Core::GpuTaskResourceAccess::Read ? Core::ResourceStates::ShaderResource : Core::ResourceStates::UnorderedAccess;
    const Core::GpuTaskResourceUse resourceUse = ResourceUse(resource, physicalRange, state, access);
    const Core::GpuTaskResourceVersionUse versionUse = VersionUse(version, role);
    const auto taskResult = AddTask(identity, Span(&resourceUse, 1u), Span(&versionUse, 1u), {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(version.valid());
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_EQ(analysis.diagnostic().resource, resource);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

