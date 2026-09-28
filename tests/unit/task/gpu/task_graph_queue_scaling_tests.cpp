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
        const bool dependsOnPrevious = preceding.valid()
            && (scenario == Scenario::Conservative || scenario == Scenario::MergeChain)
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
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = scenario == Scenario::SingleQueue ? 1u : LengthOf(queues),
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
        const bool computeTask = scenario == Scenario::Independent && taskIndex % 2u != 0u;
        EXPECT_EQ(assignment->queue, queues[computeTask ? 1u : 0u].id);
        EXPECT_EQ(assignment->initialQueue, assignment->queue);
        EXPECT_EQ(assignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
        const usize sameQueueTasks = scenario == Scenario::Independent
            ? (computeTask ? taskCount / 2u : (taskCount + 1u) / 2u)
            : taskCount
        ;
        EXPECT_EQ(assignment->score.queueLoad, static_cast<i32>((sameQueueTasks - 1u) * 8u));
        EXPECT_EQ(assignment->score.overlap, static_cast<i32>(
            scenario == Scenario::Independent ? (taskCount - sameQueueTasks) * 8u : 0u
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
}


TEST(GpuTaskGraph, PreservesAutomaticPlacementScoresAcrossScalingScenarios){
    CheckScenarios(129u);
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

