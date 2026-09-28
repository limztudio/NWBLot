// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

namespace Scenario{
    enum Enum : u8{
        SingleQueue,
        Conservative,
        Independent,
        MergeChain,
        SerialChain,
        SameClassBalance,
    };
};


void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

void CheckAutomaticPlacement(
    const usize taskCount,
    const Scenario::Enum scenario,
    const NotNull<const char*> durationProperty,
    const NotNull<const char*> scratchProperty){
    SCOPED_TRACE(scenario);
    SCOPED_TRACE(taskCount);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskId preceding;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/queue_scaling/task/"), FormatDecimal(taskIndex, identityText));
        const bool graphicsTask = scenario == Scenario::SingleQueue
            || (scenario == Scenario::Independent && taskIndex % 2u == 0u)
        ;
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Graphics::GpuTaskCostHint::Large;
        scheduling.overlapPreferred = scenario != Scenario::Conservative;
        scheduling.mergeWithPrevious = scenario == Scenario::MergeChain && preceding.valid();
        scheduling.allowSameClassQueueRouting = scenario == Scenario::SameClassBalance;
        const bool dependsOnPrevious = preceding.valid()
            && (scenario == Scenario::Conservative || scenario == Scenario::MergeChain || scenario == Scenario::SerialChain)
        ;
        preceding = AddTaskWithCommands(
            graph,
            identity,
            "Queue Scaling Task",
            graphicsTask ? GraphicsCommands() : ComputeCommands(),
            scheduling,
            {},
            dependsOnPrevious ? &preceding : nullptr,
            dependsOnPrevious ? 1u : 0u
        );
        ASSERT_TRUE(preceding.valid());
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuPhysicalQueueInfo auxiliaryCompute = DedicatedComputeQueue(2u);
    auxiliaryCompute.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), auxiliaryCompute };
    const Graphics::GpuPhysicalQueueTopology topology{
        .queues = queues,
        .queueCount = scenario == Scenario::SingleQueue ? 1u : scenario == Scenario::SameClassBalance ? LengthOf(queues) : 2u,
    };
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    const Graphics::GpuTaskGraphCompiler compiler;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    usize scratchBytes = 0u;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        const Timer begin = TimerNow();
        const bool assigned = compiler.assignQueues(view, analysis, topology, assignments, scratch);
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(assigned);
        if(iteration != 0u)
            minimumNanoseconds = Min(minimumNanoseconds, nanoseconds);
        scratchBytes = scratch.memoryStats().peakUsedBytes;
    }
    RecordUnsignedProperty(durationProperty, minimumNanoseconds);
    RecordUnsignedProperty(scratchProperty, scratchBytes);

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const Graphics::GpuTaskId task = view.taskAt(taskIndex).id;
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        const bool computeTask = scenario == Scenario::SameClassBalance
            || (scenario == Scenario::Independent && taskIndex % 2u != 0u)
        ;
        const bool auxiliaryTask = scenario == Scenario::SameClassBalance && taskIndex % 2u != 0u;
        EXPECT_EQ(assignment->queue, queues[auxiliaryTask ? 2u : computeTask ? 1u : 0u].id);
        EXPECT_EQ(assignment->initialQueue, assignment->queue);
        EXPECT_EQ(assignment->modifiers, auxiliaryTask
            ? Graphics::GpuTaskQueueAssignmentModifier::SameClassLoadBalance
            : Graphics::GpuTaskQueueAssignmentModifier::None
        );
        const bool balanced = scenario == Scenario::Independent || scenario == Scenario::SameClassBalance;
        const usize sameQueueTasks = balanced ? (taskIndex % 2u != 0u ? taskCount / 2u : (taskCount + 1u) / 2u) : taskCount;
        EXPECT_EQ(assignment->score.queueLoad, static_cast<i32>((sameQueueTasks - 1u) * 8u));
        EXPECT_EQ(assignment->score.overlap, static_cast<i32>(
            balanced ? (taskCount - sameQueueTasks) * 8u : 0u
        ));
        EXPECT_EQ(assignment->score.incomingCrossings, 0);
        EXPECT_EQ(assignment->score.outgoingCrossings, 0);
        EXPECT_EQ(assignment->score.ownershipTransfers, 0);
    }
}

void CheckScenarios(const usize taskCount){
    CheckAutomaticPlacement(
        taskCount, Scenario::SingleQueue, NotNull<const char*>("single_queue_ns"), NotNull<const char*>("single_queue_scratch_bytes")
    );
    CheckAutomaticPlacement(
        taskCount, Scenario::Conservative, NotNull<const char*>("conservative_ns"), NotNull<const char*>("conservative_scratch_bytes")
    );
    CheckAutomaticPlacement(
        taskCount, Scenario::Independent, NotNull<const char*>("independent_ns"), NotNull<const char*>("independent_scratch_bytes")
    );
    CheckAutomaticPlacement(
        taskCount, Scenario::MergeChain, NotNull<const char*>("merge_chain_ns"), NotNull<const char*>("merge_chain_scratch_bytes")
    );
    CheckAutomaticPlacement(
        taskCount, Scenario::SerialChain, NotNull<const char*>("serial_chain_ns"), NotNull<const char*>("serial_chain_scratch_bytes")
    );
    CheckAutomaticPlacement(
        taskCount,
        Scenario::SameClassBalance,
        NotNull<const char*>("same_class_balance_ns"),
        NotNull<const char*>("same_class_balance_scratch_bytes")
    );
}


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
        EXPECT_EQ(assignment->initialQueue, assignment->queue);
        EXPECT_EQ(assignment->score.queueLoad, expectedQueueLoads[taskIndex]);
        EXPECT_EQ(assignment->score.incomingCrossings, 0);
        EXPECT_EQ(assignment->score.outgoingCrossings, 0);
    }
    EXPECT_TRUE(assignments.find(tasks[6u])->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DirectDependencyAffinity);
}

TEST(GpuTaskGraph, DISABLED_AutomaticPlacementBenchmark1024Tasks){
    CheckScenarios(1024u);
}

TEST(GpuTaskGraph, DISABLED_AutomaticPlacementBenchmark4096Tasks){
    CheckScenarios(4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

