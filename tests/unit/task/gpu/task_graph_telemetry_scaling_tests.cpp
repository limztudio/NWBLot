// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


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


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void CheckTelemetryEdges(const usize taskCount, const Scenario::Enum scenario){
    SCOPED_TRACE(taskCount);
    SCOPED_TRACE(scenario);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceId resource;
    if(scenario != Scenario::Explicit){
        resource = AddBufferMetadata(graph, Name("tests/telemetry_scaling/resource"), "Telemetry Buffer");
        ASSERT_TRUE(resource.valid());
    }
    const Graphics::GpuTaskResourceUse resourceUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskId preceding;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/telemetry_scaling/task/"), FormatDecimal(taskIndex, identityText));
        const bool explicitDependency = preceding.valid()
            && (scenario == Scenario::Explicit || (scenario == Scenario::Mixed && taskIndex % 2u != 0u))
        ;
        preceding = AddTask(
            graph,
            identity,
            "Telemetry Task",
            explicitDependency ? &preceding : nullptr,
            explicitDependency ? 1u : 0u,
            resource.valid() ? &resourceUse : nullptr,
            resource.valid() ? 1u : 0u
        );
        ASSERT_TRUE(preceding.valid());
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), taskCount - 1u);
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    nodes.reserve(taskCount + declarations.resourceCount());
    edges.reserve(taskCount + analysis.edges().size());
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    usize peakScratchBytes = 0u;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        nodes.clear();
        edges.clear();
        pendingEdges.clear();
        Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        const Timer begin = TimerNow();
        ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena));
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        if(iteration != 0u && nanoseconds < minimumNanoseconds)
            minimumNanoseconds = nanoseconds;
        const usize usedBytes = scratchArena.memoryStats().peakUsedBytes;
        if(usedBytes > peakScratchBytes)
            peakScratchBytes = usedBytes;
    }
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
        u8 expectedFlags = resource.valid() ? Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency : 0u;
        if(scenario == Scenario::Explicit || (scenario == Scenario::Mixed && dependency.consumer.index % 2u != 0u))
            expectedFlags |= Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
        EXPECT_EQ(edge.flags, expectedFlags);
    }
    EXPECT_EQ(dependencyIndex, analysis.edges().size());
    RecordUnsignedProperty(NotNull<const char*>{ "telemetry_task_count" }, taskCount);
    RecordUnsignedProperty(NotNull<const char*>{ "telemetry_edge_count" }, edges.size());
    RecordUnsignedProperty(NotNull<const char*>{ "telemetry_export_ns" }, minimumNanoseconds);
    RecordUnsignedProperty(NotNull<const char*>{ "telemetry_scratch_peak_bytes" }, peakScratchBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTelemetry, PreservesExplicitInferredAndOverlappingEdgeFlags){
    using namespace __hidden_task_graph_telemetry_scaling_tests;
    CheckTelemetryEdges(128u, Scenario::Explicit);
    CheckTelemetryEdges(128u, Scenario::Inferred);
    CheckTelemetryEdges(128u, Scenario::Mixed);
}

TEST(GpuTaskGraphTelemetry, DISABLED_ExportBenchmark8Tasks){
    __hidden_task_graph_telemetry_scaling_tests::CheckTelemetryEdges(8u, __hidden_task_graph_telemetry_scaling_tests::Scenario::Mixed);
}

TEST(GpuTaskGraphTelemetry, DISABLED_ExportBenchmark32Tasks){
    __hidden_task_graph_telemetry_scaling_tests::CheckTelemetryEdges(32u, __hidden_task_graph_telemetry_scaling_tests::Scenario::Mixed);
}

TEST(GpuTaskGraphTelemetry, DISABLED_ExportBenchmark1024Tasks){
    __hidden_task_graph_telemetry_scaling_tests::CheckTelemetryEdges(1024u, __hidden_task_graph_telemetry_scaling_tests::Scenario::Mixed);
}

TEST(GpuTaskGraphTelemetry, DISABLED_ExportBenchmark4096Tasks){
    __hidden_task_graph_telemetry_scaling_tests::CheckTelemetryEdges(4096u, __hidden_task_graph_telemetry_scaling_tests::Scenario::Mixed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

