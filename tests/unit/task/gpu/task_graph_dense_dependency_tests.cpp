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


static void DeclareDenseDependencies(Core::GpuTaskGraph& graph, const usize taskCount, const Scenario::Enum scenario, Core::Alloc::ScratchArena& scratch){
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(scratch);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> tasks(scratch);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(scratch);
    resources.reserve(scenario == Scenario::Explicit ? 0u : taskCount);
    tasks.reserve(taskCount);
    uses.reserve(scenario == Scenario::Explicit ? 0u : taskCount);
    if(scenario != Scenario::Explicit){
        for(usize index = 0u; index < taskCount; ++index){
            const auto resourceNameResult = IndexedName(Name("tests/dense_dependencies/resource/"), index);
            ASSERT_TRUE(resourceNameResult);
            const auto gpuTaskAdmission = AddHazardDomain(graph, *resourceNameResult, "Dense Hazard");
            ASSERT_TRUE(gpuTaskAdmission);
            resources.push_back(*gpuTaskAdmission);
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
                    .requiredState = Core::ResourceStates::UnorderedAccess,
                    .access = resourceIndex == index ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read,
                });
            }
        }
        const auto derivedNameResult = IndexedName(Name("tests/dense_dependencies/task/"), index);
        ASSERT_TRUE(derivedNameResult);
        const auto taskResult = AddTask(*derivedNameResult, "Dense Dependency Task", scenario == Scenario::Inferred ? Span<const Core::GpuTaskId>{} : Span(tasks.data(), tasks.size()), Span(uses.data(), uses.size()), {}, {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const auto task = *taskResult;
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }
}

static void CheckDenseDependencies(const usize taskCount, const Scenario::Enum scenario){
    SCOPED_TRACE(scenario);
    TestArena arena;
    Core::GpuTaskGraph graph(arena.arena);
    Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    ASSERT_NO_FATAL_FAILURE(DeclareDenseDependencies(graph, taskCount, scenario, declarationScratch));
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    Core::GpuTaskGraphAnalysis analysis(arena.arena);
    const Core::GpuTaskGraphCompiler compiler;
    const usize pairCount = taskCount * (taskCount - 1u) / 2u;
    Core::Alloc::ScratchArena analysisScratch(s_TaskGraphScratchArena);
    ASSERT_TRUE(compiler.analyze(view, analysisScratch, analysis));
    const usize peakScratch = analysisScratch.memoryStats().peakUsedBytes;
    ASSERT_EQ(analysis.edges().size(), pairCount);
    ASSERT_EQ(analysis.schedulingEdges().size(), taskCount - 1u);
    ASSERT_EQ(analysis.inferredEdges().size(), scenario == Scenario::Explicit ? 0u : pairCount);
    for(const auto& edge : analysis.edges()){
        EXPECT_LT(edge.producer.index, edge.consumer.index);
        EXPECT_EQ(edge.hazard == Core::GpuTaskHazardType::Explicit, scenario != Scenario::Inferred);
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
        Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        Telemetry::FrameGraphBuilder builder(scratch, nodes, edges, pending);
        ASSERT_TRUE(view.appendFrameGraphTelemetry(analysis, scratch, builder));
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
            u8 flags = scenario == Scenario::Inferred ? 0u : Core::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
            if(scenario != Scenario::Explicit)
                flags |= Core::GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
            EXPECT_EQ(edge.flags, flags);
        }
        EXPECT_EQ(dependencyIndex, pairCount);
    }

}


TEST(GpuTaskGraphAnalysis, DenseSequentialDependencyPairsBoundStorageAndPreserveTelemetryFlags){
    constexpr Array<Scenario::Enum, 3> s_Scenarios = { Scenario::Explicit, Scenario::Inferred, Scenario::Mixed };
    for(const auto scenario : s_Scenarios)
        ASSERT_NO_FATAL_FAILURE(CheckDenseDependencies(256u, scenario));
}

TEST(GpuTaskGraphAnalysis, ReductionRetainsAnUnreachedMiddleTargetAfterReachingTheLastTarget){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena arena;
    Core::GpuTaskGraph graph(arena.arena);
    const auto rootResult = AddTask(Name("tests/reduction_gap/root"), "Root", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(rootResult);
    const auto root = *rootResult;
    const auto leftResult = AddTask(Name("tests/reduction_gap/left"), "Left", Span(&root, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(leftResult);
    const auto left = *leftResult;
    const auto middleResult = AddTask(Name("tests/reduction_gap/middle"), "Middle", Span(&root, 1u), {}, {}, {}, {}, graph);
    ASSERT_TRUE(middleResult);
    const auto middle = *middleResult;
    const Array<Core::GpuTaskId, 2> lastDependencies = { root, left };
    const auto lastResult = AddTask(Name("tests/reduction_gap/last"), "Last", Span(lastDependencies), {}, {}, {}, {}, graph);
    ASSERT_TRUE(lastResult);
    const auto last = *lastResult;
    ASSERT_TRUE(root.valid());
    ASSERT_TRUE(left.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(last.valid());
    Core::GpuTaskGraphAnalysis analysis(arena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.schedulingEdges().size(), 3u);
    b8 retainsMiddle = false;
    b8 retainsRedundantLast = false;
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

