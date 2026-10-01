// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_feedback_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using namespace Graphics::GpuTaskGraphCompilerDetail;

constexpr Graphics::GpuTaskTimingFeedbackPolicy s_TimingPolicy{
    .minimumAbsoluteBenefitSeconds = 0.0,
    .minimumRelativeBenefit = 0.1,
    .minimumFramesBetweenSwitches = 0u,
    .minimumSampleCount = 1u,
    .calibrationIntervalFrames = 0u,
    .enabled = true,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

void CheckDistinctDurationRoutes(const usize taskCount){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskTimingHistoryStore history(testArena.arena, 1u);
    Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
    Graphics::GraphicsVector<u32> assignmentIndices(testArena.arena);
    assignments.reserve(taskCount);
    assignmentIndices.reserve(taskCount);
    Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), GraphicsQueue(1u), GraphicsQueue(2u), GraphicsQueue(3u) };
    for(usize queueIndex = 1u; queueIndex < LengthOf(queues); ++queueIndex)
        queues[queueIndex].queueIndex = static_cast<u32>(queueIndex);
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    ASSERT_TRUE(IsValidQueueTopology(topology));
    const f64 durations[] = { 0.010, 0.003, 0.002, 0.001 };
    Graphics::GpuTaskId preceding;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/timing_scaling/task/"), FormatDecimal(taskIndex, identityText));
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Graphics::GpuTaskCostHint::Large;
        scheduling.allowSameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        const bool hasDependency = taskIndex % 2u != 0u;
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph,
            identity,
            "Distinct Duration Timing Task",
            GraphicsCommands(),
            scheduling,
            {},
            hasDependency ? &preceding : nullptr,
            hasDependency ? 1u : 0u
        );
        ASSERT_TRUE(task.valid());
        assignments.push_back({ .task = task, .initialQueue = queues[0u].id, .queue = queues[0u].id, .score = {} });
        assignmentIndices.push_back(static_cast<u32>(taskIndex));
        preceding = task;
        const Graphics::GpuTaskTimingKey key{ .task = identity, .queue = Graphics::CommandQueue::Graphics };
        for(usize queueIndex = 0u; queueIndex < LengthOf(queues); ++queueIndex)
            ASSERT_TRUE(history.recordNonCommittingSample(key, queues[queueIndex].id, durations[queueIndex]));
        ASSERT_TRUE(history.noteAcceptedAssignment(key, queues[0u].id, 1u));
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_FALSE(analysis.schedulingEdges().empty());
    Graphics::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    GpuTaskQueueScoringData scoringData(declarations, analysis, reachability, {}, scratch);
    scoringData.rebuildAssignmentLoads(assignments, topology);
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        usize mismatches = 0u;
        const Timer begin = TimerNow();
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const Graphics::GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
            const Graphics::GpuTaskTimingAssignmentKey key{ .task = task.identity };
            const Graphics::GpuPhysicalQueueInfo* const selected = FindTimingFeedbackQueue(
                declarations,
                analysis,
                assignments,
                assignmentIndices,
                topology,
                reachability,
                scoringData,
                task,
                queues[0u],
                key,
                snapshot,
                s_TimingPolicy,
                100u
            );
            if(selected != &queues[3u])
                ++mismatches;
        }
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_EQ(mismatches, 0u);
        if(iteration != 0u)
            minimumNanoseconds = Min(minimumNanoseconds, nanoseconds);
    }
    RecordUnsignedProperty(NotNull<const char*>("timing_distinct_routes_ns"), minimumNanoseconds);
    RecordUnsignedProperty(NotNull<const char*>("timing_graph_scratch_bytes"), scratch.memoryStats().peakUsedBytes);
}


TEST(GpuTaskGraphTimingFeedback, RanksEqualDurationsAfterShorterCandidateAppears){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GraphicsVector<Graphics::GpuTaskQueueAssignment> assignments(testArena.arena);
    Graphics::GraphicsVector<u32> assignmentIndices(testArena.arena);
    assignments.reserve(4u);
    assignmentIndices.reserve(4u);
    Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(), GraphicsQueue(1u), GraphicsQueue(2u), GraphicsQueue(3u), GraphicsQueue(4u),
    };
    for(usize queueIndex = 1u; queueIndex < LengthOf(queues); ++queueIndex)
        queues[queueIndex].queueIndex = static_cast<u32>(queueIndex);
    const Graphics::GpuTaskCostHint::Enum costs[] = {
        Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Large,
        Graphics::GpuTaskCostHint::Medium,
        Graphics::GpuTaskCostHint::Medium,
    };
    const usize assignedQueues[] = { 2u, 3u, 4u, 0u };
    Graphics::GpuTaskId target;
    for(usize taskIndex = 0u; taskIndex < LengthOf(costs); ++taskIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/timing_scaling/tied/"), FormatDecimal(taskIndex, identityText));
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = costs[taskIndex];
        scheduling.allowSameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        target = AddTaskWithCommands(graph, identity, "Tied Duration Timing Task", GraphicsCommands(), scheduling);
        ASSERT_TRUE(target.valid());
        const Graphics::GpuPhysicalQueueId queue = queues[assignedQueues[taskIndex]].id;
        assignments.push_back({ .task = target, .initialQueue = queue, .queue = queue, .score = {} });
        assignmentIndices.push_back(static_cast<u32>(taskIndex));
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuTaskGraphTaskView task = declarations.taskAt(target.index);
    const Graphics::GpuTaskTimingKey historyKey{ .task = task.identity, .queue = Graphics::CommandQueue::Graphics };
    const Graphics::GpuTaskTimingAssignmentKey assignmentKey{ .task = task.identity };
    const f64 durations[] = { 0.010, 0.004, 0.004, 0.002, 0.002 };
    Graphics::GpuTaskTimingHistoryStore history(testArena.arena, 1u);
    for(usize queueIndex = 0u; queueIndex < LengthOf(queues); ++queueIndex)
        ASSERT_TRUE(history.recordNonCommittingSample(historyKey, queues[queueIndex].id, durations[queueIndex]));
    ASSERT_TRUE(history.noteAcceptedAssignment(historyKey, queues[0u].id, 1u));
    Graphics::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    GpuTaskQueueScoringData scoringData(declarations, analysis, reachability, {}, scratch);
    const usize candidateOrders[][4u] = {
        { 1u, 2u, 3u, 4u },
        { 2u, 1u, 3u, 4u },
        { 4u, 3u, 2u, 1u },
        { 3u, 4u, 1u, 2u },
        { 1u, 3u, 2u, 4u },
    };
    for(const auto& candidateOrder : candidateOrders){
        SCOPED_TRACE(candidateOrder[0u]);
        Graphics::GpuPhysicalQueueInfo reorderedQueues[LengthOf(queues)] = {};
        reorderedQueues[0u] = queues[0u];
        for(usize queueIndex = 0u; queueIndex < LengthOf(candidateOrder); ++queueIndex)
            reorderedQueues[queueIndex + 1u] = queues[candidateOrder[queueIndex]];
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = reorderedQueues, .queueCount = LengthOf(reorderedQueues) };
        ASSERT_TRUE(IsValidQueueTopology(topology));
        scoringData.rebuildAssignmentLoads(assignments, topology);
        const Graphics::GpuPhysicalQueueInfo* const selected = FindTimingFeedbackQueue(
            declarations,
            analysis,
            assignments,
            assignmentIndices,
            topology,
            reachability,
            scoringData,
            task,
            reorderedQueues[0u],
            assignmentKey,
            snapshot,
            s_TimingPolicy,
            100u
        );
        ASSERT_NE(selected, nullptr);
        // Queue one wins the first slower tie; the later fast tie must use its own loads (eight versus four).
        EXPECT_EQ(selected->id, queues[4u].id);
    }
}

TEST(GpuTaskGraphTimingFeedback, DISABLED_DistinctDurationRoutingBenchmark1024Tasks){
    CheckDistinctDurationRoutes(1024u);
}

TEST(GpuTaskGraphTimingFeedback, DISABLED_DistinctDurationRoutingBenchmark4096Tasks){
    CheckDistinctDurationRoutes(4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

