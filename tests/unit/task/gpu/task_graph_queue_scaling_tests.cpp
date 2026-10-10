// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, SerialSchedulingReachabilityPreservesStrictQueriesAcrossReuse){
    constexpr usize s_TaskCount = 129u;
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskId tasks[s_TaskCount] = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        char identityText[32u] = {};
        tasks[taskIndex] = AddTaskWithCommands(
            graph,
            DeriveName(Name("tests/queue_scaling/serial/"), FormatDecimal(taskIndex, identityText)),
            "Serial Reachability Task",
            ComputeCommands(),
            {},
            {},
            taskIndex != 0u ? &tasks[taskIndex - 1u] : nullptr,
            taskIndex != 0u ? 1u : 0u
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    Graphics::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(scratch);
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    for(usize sourceIndex = 0u; sourceIndex < s_TaskCount; ++sourceIndex){
        for(usize destinationIndex = 0u; destinationIndex < s_TaskCount; ++destinationIndex){
            EXPECT_EQ(reachability.reaches(tasks[sourceIndex], tasks[destinationIndex]), sourceIndex < destinationIndex);
            EXPECT_FALSE(reachability.transitivelyIndependent(tasks[sourceIndex], tasks[destinationIndex]));
        }
    }
    const Graphics::GpuTaskId invalidTasks[] = {
        {},
        { .generation = tasks[0u].generation + 1u, .index = tasks[0u].index },
        { .generation = tasks[0u].generation, .index = static_cast<u32>(s_TaskCount) },
    };
    for(const Graphics::GpuTaskId& invalid : invalidTasks){
        EXPECT_FALSE(reachability.reaches(tasks[0u], invalid));
        EXPECT_FALSE(reachability.reaches(invalid, tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], invalid));
        EXPECT_FALSE(reachability.transitivelyIndependent(invalid, tasks[0u]));
    }

    Graphics::GpuTaskGraph partialGraph(testArena.arena);
    const Graphics::GpuTaskId producer = AddTask(
        partialGraph, Name("tests/queue_scaling/partial/producer"), "Partial Producer"
    );
    const Graphics::GpuTaskId consumer = AddTask(
        partialGraph, Name("tests/queue_scaling/partial/consumer"), "Partial Consumer", &producer, 1u
    );
    const Graphics::GpuTaskId independent = AddTask(
        partialGraph, Name("tests/queue_scaling/partial/independent"), "Independent"
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(independent.valid());
    Graphics::GpuTaskGraphAnalysis partialAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(partialGraph, partialAnalysis));
    const Graphics::GpuTaskGraph::DeclarationReadView partialDeclarations(partialGraph);
    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(
        partialDeclarations,
        partialAnalysis,
        reachability
    ));
    EXPECT_TRUE(reachability.reaches(producer, consumer));
    EXPECT_FALSE(reachability.reaches(consumer, producer));
    EXPECT_TRUE(reachability.transitivelyIndependent(producer, independent));
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));

    ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    EXPECT_TRUE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(
        declarations,
        partialAnalysis,
        reachability
    ));
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
}

TEST(GpuTaskGraph, SameClassBalancingCountsMergedAndUnroutedPrefixCosts){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskCostHint::Enum costs[] = {
        Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Small,
        Graphics::GpuTaskCostHint::Medium,
        Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Tiny,
        Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Small,
        Graphics::GpuTaskCostHint::Medium,
    };
    Graphics::GpuTaskId tasks[LengthOf(costs)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/queue_scaling/prefix/"), FormatDecimal(taskIndex, identityText));
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = costs[taskIndex];
        scheduling.overlapPreferred = taskIndex != 3u;
        scheduling.allowSameClassQueueRouting = taskIndex == 2u || taskIndex >= 4u;
        scheduling.mergeWithPrevious = taskIndex == 1u;
        scheduling.preserveSameClassQueueWithDirectDependency = taskIndex == 6u;
        const Graphics::GpuTaskId dependency = taskIndex == 1u ? tasks[0u] : taskIndex == 6u ? tasks[2u] : Graphics::GpuTaskId{};
        tasks[taskIndex] = AddTaskWithCommands(
            graph,
            identity,
            "Weighted Prefix Task",
            GraphicsCommands(),
            scheduling,
            {},
            dependency.valid() ? &dependency : nullptr,
            dependency.valid() ? 1u : 0u
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Graphics::GpuPhysicalQueueInfo firstAuxiliary = GraphicsQueue(7u);
    firstAuxiliary.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliary = GraphicsQueue(11u);
    secondAuxiliary.queueIndex = 2u;
    const Graphics::GpuPhysicalQueueInfo queues[] = { secondAuxiliary, GraphicsQueue(), firstAuxiliary };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));

    // The first merged group and later fixed task remain on queue zero; dependency affinity keeps task six on seven.
    const u16 expectedQueueIndices[] = { 0u, 0u, 7u, 0u, 11u, 11u, 7u, 7u };
    const i32 expectedQueueLoads[] = { 10, 16, 6, 10, 8, 1, 8, 6 };
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue.index, expectedQueueIndices[taskIndex]);
        EXPECT_EQ(assignment->score.queueLoad, expectedQueueLoads[taskIndex]);
    }
    EXPECT_TRUE(assignments.find(tasks[6u])->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

