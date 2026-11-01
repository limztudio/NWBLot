// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_telemetry_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

namespace Scenario{
    enum Enum : u8{
        Explicit,
        Inferred,
        Mixed,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CheckTelemetryEdges(const usize taskCount, const Scenario::Enum scenario){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);

    SCOPED_TRACE(taskCount);
    SCOPED_TRACE(scenario);
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuGraphResourceId resource;
    if(scenario != Scenario::Explicit){
        const auto resourceTaskAdmission = AddBufferMetadata(graph, Name("tests/telemetry_scaling/resource"), "Telemetry Buffer");
        ASSERT_TRUE(resourceTaskAdmission);
        resource = *resourceTaskAdmission;
        ASSERT_TRUE(resource.valid());
    }
    const Core::GpuTaskResourceUse resourceUse{ .resource = resource, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskId preceding;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        Array<char, 32u> identityText = {};
        const auto identityNameResult = DeriveName(Name("tests/telemetry_scaling/task/"), FormatDecimal(taskIndex, identityText));
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        const b8 explicitDependency = preceding.valid() && (scenario == Scenario::Explicit || (scenario == Scenario::Mixed && taskIndex % 2u != 0u));
        const auto precedingTaskAdmission = AddTask(identity, "Telemetry Task", explicitDependency ? Span(&preceding, 1u) : Span<const Core::GpuTaskId>{}, resource.valid() ? Span(&resourceUse, 1u) : Span<const Core::GpuTaskResourceUse>{}, {}, {}, {}, graph);
        ASSERT_TRUE(precedingTaskAdmission);
        preceding = *precedingTaskAdmission;
        ASSERT_TRUE(preceding.valid());
    }
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, scratchArena, analysis));
    ASSERT_EQ(analysis.edges().size(), taskCount - 1u);
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    nodes.reserve(taskCount + declarations.resourceCount());
    edges.reserve(taskCount + analysis.edges().size());
    Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges);
    ASSERT_TRUE(declarations.appendFrameGraphTelemetry(analysis, scratchArena, builder));
    ASSERT_EQ(nodes.size(), taskCount + declarations.resourceCount());
    ASSERT_EQ(edges.size(), analysis.edges().size() + (resource.valid() ? taskCount : 0u));
    const u32 nodeOffset = static_cast<u32>(declarations.resourceCount());
    usize dependencyIndex = 0u;
    for(const Telemetry::FrameGraphEdgeDesc& edge : edges){
        if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn){
            EXPECT_EQ(edge.kind, Telemetry::FrameGraphEdgeKind::Writes);
            EXPECT_EQ(edge.toNodeIndex, 0u);
            EXPECT_EQ(edge.flags, 0u);
            continue;
        }
        ASSERT_LT(dependencyIndex, analysis.edges().size());
        const auto& dependency = analysis.edges()[dependencyIndex];
        ++dependencyIndex;
        EXPECT_EQ(edge.fromNodeIndex, nodeOffset + dependency.producer.index);
        EXPECT_EQ(edge.toNodeIndex, nodeOffset + dependency.consumer.index);
        EXPECT_EQ(dependency.producer.index + 1u, dependency.consumer.index);
        u8 expectedFlags = resource.valid() ? Core::GpuTaskGraphTelemetryEdgeFlag::InferredDependency : 0u;
        if(scenario == Scenario::Explicit || (scenario == Scenario::Mixed && dependency.consumer.index % 2u != 0u))
            expectedFlags |= Core::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
        EXPECT_EQ(edge.flags, expectedFlags);
    }
    EXPECT_EQ(dependencyIndex, analysis.edges().size());

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTelemetry, PreservesExplicitInferredAndOverlappingEdgeFlags){
    using namespace __hidden_task_graph_telemetry_scaling_tests;
    ASSERT_NO_FATAL_FAILURE(CheckTelemetryEdges(3u, Scenario::Explicit));
    ASSERT_NO_FATAL_FAILURE(CheckTelemetryEdges(3u, Scenario::Inferred));
    ASSERT_NO_FATAL_FAILURE(CheckTelemetryEdges(3u, Scenario::Mixed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

