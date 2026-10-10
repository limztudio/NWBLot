// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

