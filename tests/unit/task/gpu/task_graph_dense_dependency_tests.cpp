// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


#include <tests/common/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_dense_dependency_tests{


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


static void DeclareDenseDependencies(
    Graphics::GpuTaskGraph& graph,
    const usize taskCount,
    const Scenario::Enum scenario,
    Graphics::Alloc::ScratchArena& scratch
){
    Vector<Graphics::GpuGraphResourceId, Graphics::Alloc::ScratchArena> resources(scratch);
    Vector<Graphics::GpuTaskId, Graphics::Alloc::ScratchArena> tasks(scratch);
    Vector<Graphics::GpuTaskResourceUse, Graphics::Alloc::ScratchArena> uses(scratch);
    resources.reserve(scenario == Scenario::Explicit ? 0u : taskCount);
    tasks.reserve(taskCount);
    uses.reserve(scenario == Scenario::Explicit ? 0u : taskCount);
    if(scenario != Scenario::Explicit){
        for(usize index = 0u; index < taskCount; ++index){
            char text[32u] = {};
            resources.push_back(AddHazardDomain(
                graph, DeriveName(Name("tests/dense_dependencies/resource/"), FormatDecimal(index, text)), "Dense Hazard"
            ));
            ASSERT_TRUE(resources.back().valid());
        }
    }
    for(usize index = 0u; index < taskCount; ++index){
        uses.clear();
        if(scenario != Scenario::Explicit){
            for(usize resourceIndex = 0u; resourceIndex <= index; ++resourceIndex){
                uses.push_back({
                    .resource = resources[resourceIndex],
                    .range = {},
                    .requiredState = Graphics::ResourceStates::UnorderedAccess,
                    .access = resourceIndex == index ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read,
                });
            }
        }
        char text[32u] = {};
        const auto task = AddTask(
            graph,
            DeriveName(Name("tests/dense_dependencies/task/"), FormatDecimal(index, text)),
            "Dense Dependency Task",
            scenario == Scenario::Inferred ? nullptr : tasks.data(),
            scenario == Scenario::Inferred ? 0u : tasks.size(),
            uses.data(),
            uses.size()
        );
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }
}

static void CheckDenseDependencies(const usize taskCount, const Scenario::Enum scenario){
    SCOPED_TRACE(scenario);
    TestArena arena;
    Graphics::GpuTaskGraph graph(arena.arena);
    Graphics::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    ASSERT_NO_FATAL_FAILURE(DeclareDenseDependencies(graph, taskCount, scenario, declarationScratch));
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    Graphics::GpuTaskGraphAnalysis analysis(arena.arena);
    const Graphics::GpuTaskGraphCompiler compiler;
    const usize pairCount = taskCount * (taskCount - 1u) / 2u;
    Graphics::Alloc::ScratchArena analysisScratch(s_TaskGraphScratchArena);
    ASSERT_TRUE(compiler.analyze(view, analysis, analysisScratch));
    const usize peakScratch = analysisScratch.memoryStats().peakUsedBytes;
    ASSERT_EQ(analysis.edges().size(), pairCount);
    ASSERT_EQ(analysis.schedulingEdges().size(), taskCount - 1u);
    ASSERT_EQ(analysis.inferredEdges().size(), scenario == Scenario::Explicit ? 0u : pairCount);
    for(const auto& edge : analysis.edges()){
        EXPECT_LT(edge.producer.index, edge.consumer.index);
        EXPECT_EQ(edge.hazard == Graphics::GpuTaskHazardType::Explicit, scenario != Scenario::Inferred);
    }
    for(const auto& edge : analysis.schedulingEdges())
        EXPECT_EQ(edge.consumer.index, edge.producer.index + 1u);
    {
        constexpr usize s_MaxScratchBytes = 32u * 1024u * 1024u;
        EXPECT_LT(peakScratch, s_MaxScratchBytes);
        Telemetry::FrameGraphNodeDescs nodes(arena.arena);
        Telemetry::FrameGraphEdgeDescs edges(arena.arena);
        Telemetry::FrameGraphPendingNameEdges pending(arena.arena);
        const usize resourceUseCount = scenario == Scenario::Explicit ? 0u : pairCount + taskCount;
        nodes.reserve(taskCount + view.resourceCount());
        edges.reserve(pairCount + resourceUseCount);
        Telemetry::FrameGraphBuilder builder(nodes, edges, pending);
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        ASSERT_TRUE(view.appendFrameGraphTelemetry(builder, analysis, scratch));
        EXPECT_LT(scratch.memoryStats().peakUsedBytes, s_MaxScratchBytes);
        EXPECT_EQ(nodes.size(), taskCount + view.resourceCount());
        EXPECT_EQ(edges.size(), pairCount + resourceUseCount);
        usize dependencyIndex = 0u;
        for(const auto& edge : edges){
            if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn)
                continue;
            ASSERT_LT(dependencyIndex, analysis.edges().size());
            const auto& dependency = analysis.edges()[dependencyIndex++];
            EXPECT_EQ(edge.fromNodeIndex, view.resourceCount() + dependency.producer.index);
            EXPECT_EQ(edge.toNodeIndex, view.resourceCount() + dependency.consumer.index);
            u8 flags = scenario == Scenario::Inferred ? 0u : Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
            if(scenario != Scenario::Explicit)
                flags |= Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
            EXPECT_EQ(edge.flags, flags);
        }
        EXPECT_EQ(dependencyIndex, pairCount);
    }

}


TEST(GpuTaskGraphAnalysis, DenseSequentialDependencyPairsBoundStorageAndPreserveTelemetryFlags){
    constexpr Scenario::Enum s_Scenarios[] = { Scenario::Explicit, Scenario::Inferred, Scenario::Mixed };
    for(const auto scenario : s_Scenarios)
        CheckDenseDependencies(256u, scenario);
}

TEST(GpuTaskGraphAnalysis, ReductionRetainsAnUnreachedMiddleTargetAfterReachingTheLastTarget){
    TestArena arena;
    Graphics::GpuTaskGraph graph(arena.arena);
    const auto root = AddTask(graph, Name("tests/reduction_gap/root"), "Root");
    const auto left = AddTask(graph, Name("tests/reduction_gap/left"), "Left", &root, 1u);
    const auto middle = AddTask(graph, Name("tests/reduction_gap/middle"), "Middle", &root, 1u);
    const Graphics::GpuTaskId lastDependencies[] = { root, left };
    const auto last = AddTask(graph, Name("tests/reduction_gap/last"), "Last", lastDependencies, LengthOf(lastDependencies));
    ASSERT_TRUE(root.valid());
    ASSERT_TRUE(left.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(last.valid());
    Graphics::GpuTaskGraphAnalysis analysis(arena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.schedulingEdges().size(), 3u);
    bool retainsMiddle = false;
    bool retainsRedundantLast = false;
    for(const auto& edge : analysis.schedulingEdges()){
        retainsMiddle = retainsMiddle || (edge.producer == root && edge.consumer == middle);
        retainsRedundantLast = retainsRedundantLast || (edge.producer == root && edge.consumer == last);
    }
    EXPECT_TRUE(retainsMiddle);
    EXPECT_FALSE(retainsRedundantLast);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

