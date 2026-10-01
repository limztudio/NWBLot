// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_capability_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

inline constexpr usize s_Samples = 5u;
inline constexpr AStringView s_SampleKeys[] = {
    "sample_0_ns", "sample_1_ns", "sample_2_ns", "sample_3_ns", "sample_4_ns"
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeclareGraphicsPairs(Graphics::GpuTaskGraph& graph, const usize taskCount){
    Graphics::GpuTaskId previous;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    for(usize index = 0u; index < taskCount; ++index){
        const bool dependent = index % 2u != 0u;
        char identity[32u] = {};
        previous = AddTaskWithCommands(
            graph,
            DeriveName(Name("tests/task_graph/capability_pairs/"), FormatDecimal(index, identity)),
            "Capability Pair",
            GraphicsCommands(),
            scheduling,
            {},
            dependent ? &previous : nullptr,
            dependent ? 1u : 0u
        );
        ASSERT_TRUE(previous.valid());
    }
}

void ExpectSameAssignment(const Graphics::GpuTaskQueueAssignment& actual, const Graphics::GpuTaskQueueAssignment& expected){
    EXPECT_EQ(actual.initialQueue, expected.initialQueue);
    EXPECT_EQ(actual.queue, expected.queue);
    EXPECT_EQ(actual.queueClass, expected.queueClass);
    EXPECT_EQ(actual.reason, expected.reason);
    EXPECT_EQ(actual.dedicated, expected.dedicated);
    EXPECT_EQ(actual.modifiers, expected.modifiers);
    EXPECT_EQ(actual.score.queueLoad, expected.score.queueLoad);
    EXPECT_EQ(actual.score.overlap, expected.score.overlap);
    EXPECT_EQ(actual.score.incomingCrossings, expected.score.incomingCrossings);
    EXPECT_EQ(actual.score.outgoingCrossings, expected.score.outgoingCrossings);
    EXPECT_EQ(actual.score.ownershipTransfers, expected.score.ownershipTransfers);
}

void RecordUnsigned(const AStringView key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(AInteropString(key), text);
}

void BenchmarkCapabilityRoute(const usize taskCount, const usize queueCount){
    TestArena arena;
    Graphics::GpuTaskGraph graph(arena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareGraphicsPairs(graph, taskCount));
    Graphics::GpuTaskGraphAnalysis analysis(arena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = queueCount };
    const Graphics::GpuPhysicalQueueTopology singleRoute{ .queues = queues, .queueCount = 1u };
    Graphics::GpuTaskGraphQueueAssignments expected(arena.arena);
    ASSERT_TRUE(Assign(graph, analysis, singleRoute, expected));
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    const Graphics::GpuTaskGraphCompiler compiler;
    Graphics::GpuTaskGraphQueueAssignments assignments(arena.arena);
    usize scratchBytes = 0u;
    for(usize sample = 0u; sample <= s_Samples; ++sample){
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        const Timer begin = TimerNow();
        const bool assigned = compiler.assignQueues(view, analysis, topology, assignments, scratch);
        const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(assigned);
        if(sample != 0u)
            RecordUnsigned(s_SampleKeys[sample - 1u], elapsed);
        scratchBytes = scratch.memoryStats().peakUsedBytes;
        for(usize index = 0u; index < taskCount; ++index){
            const auto task = view.taskAt(index).id;
            const auto* const actual = assignments.find(task);
            const auto* const reference = expected.find(task);
            ASSERT_NE(actual, nullptr);
            ASSERT_NE(reference, nullptr);
            ExpectSameAssignment(*actual, *reference);
        }
    }
    RecordUnsigned("task_count", taskCount);
    RecordUnsigned("queue_count", queueCount);
    RecordUnsigned("scratch_bytes", scratchBytes);
}

TEST(GpuTaskGraph, UnusedQueueCapabilitiesPreserveSingleRouteDiagnostics){
    constexpr usize s_TaskCount = 130u;
    TestArena arena;
    Graphics::GpuTaskGraph graph(arena.arena);
    ASSERT_NO_FATAL_FAILURE(DeclareGraphicsPairs(graph, s_TaskCount));
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    const Graphics::GpuPhysicalQueueTopology singleRoute{ .queues = queues, .queueCount = 1u };
    const Graphics::GpuTaskQueueLoad load{ .queue = queues[0u].id, .estimatedCost = 23u };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.queueLoads = &load;
    options.queueLoadCount = 1u;
    Graphics::GpuTaskGraphAnalysis analysis(arena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments actual(arena.arena);
    Graphics::GpuTaskGraphQueueAssignments expected(arena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, actual, options));
    ASSERT_TRUE(Assign(graph, analysis, singleRoute, expected, options));
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    for(usize index = 0u; index < s_TaskCount; ++index){
        const auto task = view.taskAt(index).id;
        ASSERT_NE(actual.find(task), nullptr);
        ASSERT_NE(expected.find(task), nullptr);
        ExpectSameAssignment(*actual.find(task), *expected.find(task));
    }
}

TEST(GpuTaskGraph, CapabilitySingletonGroupsOnDifferentPhysicalQueuesRetainOverlapDiagnostics){
    constexpr usize s_TaskCount = 4u;
    constexpr i32 s_ExpectedOverlap = 16;
    for(const bool sameClass : { false, true }){
        SCOPED_TRACE(sameClass);
        TestArena arena;
        Graphics::GpuTaskGraph graph(arena.arena);
        Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), sameClass ? GraphicsQueue(1u) : DedicatedComputeQueue() };
        if(sameClass)
            queues[1u].queueIndex = 1u;
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Graphics::GpuTaskCostHint::Large;
        Graphics::GpuTaskId tasks[s_TaskCount];
        Graphics::GpuTaskDiagnosticQueueOverride routes[s_TaskCount];
        for(usize index = 0u; index < s_TaskCount; ++index){
            char identity[32u] = {};
            tasks[index] = AddTaskWithCommands(
                graph,
                DeriveName(Name("tests/task_graph/capability_routes/"), FormatDecimal(index, identity)),
                "Capability Route",
                sameClass || index < s_TaskCount / 2u ? GraphicsCommands() : ComputeCommands(),
                scheduling,
                {},
                index % 2u != 0u ? &tasks[index - 1u] : nullptr,
                index % 2u != 0u ? 1u : 0u
            );
            ASSERT_TRUE(tasks[index].valid());
            routes[index] = { .task = tasks[index], .queue = queues[index / 2u].id };
        }
        Graphics::GpuTaskGraphQueueAssignmentOptions options;
        options.diagnosticQueueOverrides = routes;
        options.diagnosticQueueOverrideCount = s_TaskCount;
        Graphics::GpuTaskGraphAnalysis analysis(arena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        Graphics::GpuTaskGraphQueueAssignments assignments(arena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
        for(usize index = 0u; index < s_TaskCount; ++index){
            const auto* const assignment = assignments.find(tasks[index]);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queue, routes[index].queue);
            EXPECT_EQ(assignment->score.overlap, s_ExpectedOverlap);
            EXPECT_EQ(assignment->score.incomingCrossings, 0);
            EXPECT_EQ(assignment->score.outgoingCrossings, 0);
        }
    }
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark1024Tasks1Queues){
    BenchmarkCapabilityRoute(1024u, 1u);
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark1024Tasks2Queues){
    BenchmarkCapabilityRoute(1024u, 2u);
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark4096Tasks1Queues){
    BenchmarkCapabilityRoute(4096u, 1u);
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark4096Tasks2Queues){
    BenchmarkCapabilityRoute(4096u, 2u);
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark16384Tasks1Queues){
    BenchmarkCapabilityRoute(16384u, 1u);
}

TEST(GpuTaskGraph, DISABLED_CapabilityRouteBenchmark16384Tasks2Queues){
    BenchmarkCapabilityRoute(16384u, 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

