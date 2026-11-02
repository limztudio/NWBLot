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
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);

    constexpr usize s_TaskCount = 129u;
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Array<Core::GpuTaskId, s_TaskCount> tasks = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const auto derivedNameResult = IndexedName(Name("tests/queue_scaling/serial/"), taskIndex);
        ASSERT_TRUE(derivedNameResult);
        const auto taskAdmission = AddTaskWithCommands(*derivedNameResult, "Serial Reachability Task", ComputeCommands(), {}, {}, taskIndex != 0u ? Span(&tasks[taskIndex - 1u], 1u) : Span<const Core::GpuTaskId>{}, graph);
        ASSERT_TRUE(taskAdmission);
        tasks[taskIndex] = *taskAdmission;
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, scratch, analysis));
    Core::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(scratch);
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    for(usize sourceIndex = 0u; sourceIndex < s_TaskCount; ++sourceIndex){
        for(usize destinationIndex = 0u; destinationIndex < s_TaskCount; ++destinationIndex){
            EXPECT_EQ(reachability.reaches(tasks[sourceIndex], tasks[destinationIndex]), sourceIndex < destinationIndex);
            EXPECT_FALSE(reachability.transitivelyIndependent(tasks[sourceIndex], tasks[destinationIndex]));
        }
    }
    const Array<Core::GpuTaskId, 3u> invalidTasks = {{
        {},
        { .generation = tasks[0u].generation + 1u, .index = tasks[0u].index },
        { .generation = tasks[0u].generation, .index = static_cast<u32>(s_TaskCount) },
    }};
    for(const Core::GpuTaskId& invalid : invalidTasks){
        EXPECT_FALSE(reachability.reaches(tasks[0u], invalid));
        EXPECT_FALSE(reachability.reaches(invalid, tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], invalid));
        EXPECT_FALSE(reachability.transitivelyIndependent(invalid, tasks[0u]));
    }

    Core::GpuTaskGraph partialGraph(testArena.arena);
    const auto producerResult = AddTask(Name("tests/queue_scaling/partial/producer"), "Partial Producer", {}, {}, {}, {}, {}, partialGraph);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    const auto consumerResult = AddTask(Name("tests/queue_scaling/partial/consumer"), "Partial Consumer", Span(&producer, 1u), {}, {}, {}, {}, partialGraph);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    const auto independentResult = AddTask(Name("tests/queue_scaling/partial/independent"), "Independent", {}, {}, {}, {}, {}, partialGraph);
    ASSERT_TRUE(independentResult);
    const Core::GpuTaskId independent = *independentResult;
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    ASSERT_TRUE(independent.valid());
    Core::GpuTaskGraphAnalysis partialAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(partialGraph, scratch, partialAnalysis));
    const Core::GpuTaskGraph::DeclarationReadView partialDeclarations(partialGraph);
    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(partialDeclarations, partialAnalysis, reachability));
    EXPECT_TRUE(reachability.reaches(producer, consumer));
    EXPECT_FALSE(reachability.reaches(consumer, producer));
    EXPECT_TRUE(reachability.transitivelyIndependent(producer, independent));
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));

    ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    EXPECT_TRUE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(Core::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(declarations, partialAnalysis, reachability));
    EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[s_TaskCount - 1u]));
    EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[s_TaskCount - 1u]));
}

TEST(GpuTaskGraph, SameClassBalancingCountsMergedAndUnroutedPrefixCosts){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuTaskCostHint::Enum, 8u> costs = {
        Core::GpuTaskCostHint::Large,
        Core::GpuTaskCostHint::Small,
        Core::GpuTaskCostHint::Medium,
        Core::GpuTaskCostHint::Large,
        Core::GpuTaskCostHint::Tiny,
        Core::GpuTaskCostHint::Large,
        Core::GpuTaskCostHint::Small,
        Core::GpuTaskCostHint::Medium,
    };
    Array<Core::GpuTaskId, LengthOf(costs)> tasks = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const auto identityNameResult = IndexedName(Name("tests/queue_scaling/prefix/"), taskIndex);
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = costs[taskIndex];
        scheduling.overlapPreferred = taskIndex != 3u;
        scheduling.allowSameClassQueueRouting = taskIndex == 2u || taskIndex >= 4u;
        scheduling.mergeWithPrevious = taskIndex == 1u;
        scheduling.preserveSameClassQueueWithDirectDependency = taskIndex == 6u;
        const Core::GpuTaskId dependency = taskIndex == 1u ? tasks[0u] : taskIndex == 6u ? tasks[2u] : Core::GpuTaskId{};
        const auto taskAdmission = AddTaskWithCommands(identity, "Weighted Prefix Task", GraphicsCommands(), scheduling, {}, dependency.valid() ? Span(&dependency, 1u) : Span<const Core::GpuTaskId>{}, graph);
        ASSERT_TRUE(taskAdmission);
        tasks[taskIndex] = *taskAdmission;
        ASSERT_TRUE(tasks[taskIndex].valid());
    }
    Core::GpuPhysicalQueueInfo firstAuxiliary = GraphicsQueue(7u);
    firstAuxiliary.queueIndex = 1u;
    Core::GpuPhysicalQueueInfo secondAuxiliary = GraphicsQueue(11u);
    secondAuxiliary.queueIndex = 2u;
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { secondAuxiliary, GraphicsQueue(), firstAuxiliary };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));

    // Keep merged work on queue zero and route task six to queue seven by affinity.
    const Array<u16, 8u> expectedQueueIndices = { 0u, 0u, 7u, 0u, 11u, 11u, 7u, 7u };
    const Array<i32, 8u> expectedQueueLoads = { 10, 16, 6, 10, 8, 1, 8, 6 };
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        const Core::GpuTaskQueueAssignment* const assignment = assignments.find(tasks[taskIndex]);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue.index, expectedQueueIndices[taskIndex]);
        EXPECT_EQ(assignment->score.queueLoad, expectedQueueLoads[taskIndex]);
    }
    EXPECT_TRUE(assignments.find(tasks[6u])->modifiers & Core::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

