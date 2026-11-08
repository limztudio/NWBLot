// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_timing_feedback_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using namespace Core::GpuTaskGraphCompilerDetail;

constexpr Core::GpuTaskTimingFeedbackPolicy s_TimingPolicy{ .minimumAbsoluteBenefitSeconds = 0.0, .minimumRelativeBenefit = 0.1, .minimumFramesBetweenSwitches = 0u, .minimumSampleCount = 1u, .calibrationIntervalFrames = 0u, .enabled = true, };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTimingFeedback, RanksEqualDurationsAfterShorterCandidateAppears){
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GraphicsVector<Core::GpuTaskQueueAssignment> assignments(testArena.arena);
    Core::GraphicsVector<u32> assignmentIndices(testArena.arena);
    assignments.reserve(4u);
    assignmentIndices.reserve(4u);
    Array<Core::GpuPhysicalQueueInfo, 5u> queues = { GraphicsQueue(), GraphicsQueue(1u), GraphicsQueue(2u), GraphicsQueue(3u), GraphicsQueue(4u), };
    for(usize queueIndex = 1u; queueIndex < LengthOf(queues); ++queueIndex)
        queues[queueIndex].queueIndex = static_cast<u32>(queueIndex);
    const Array<Core::GpuTaskCostHint::Enum, 4u> costs = { Core::GpuTaskCostHint::Large, Core::GpuTaskCostHint::Large, Core::GpuTaskCostHint::Medium, Core::GpuTaskCostHint::Medium, };
    const Array<usize, 4u> assignedQueues = { 2u, 3u, 4u, 0u };
    Core::GpuTaskId target;
    for(usize taskIndex = 0u; taskIndex < LengthOf(costs); ++taskIndex){
        const auto identityNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseTimingScalingTied), taskIndex);
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = costs[taskIndex];
        scheduling.allowSameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        const auto targetTaskAdmission = AddTaskWithCommands(identity, "Tied Duration Timing Task", GraphicsCommands(), scheduling, {}, {}, graph);
        ASSERT_TRUE(targetTaskAdmission);
        target = *targetTaskAdmission;
        ASSERT_TRUE(target.valid());
        const Core::GpuPhysicalQueueId queue = queues[assignedQueues[taskIndex]].id;
        assignments.push_back({ .task = target, .initialQueue = queue, .queue = queue, .score = {} });
        assignmentIndices.push_back(static_cast<u32>(taskIndex));
    }
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, scratch, analysis));
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Core::GpuTaskGraphTaskView task = declarations.taskAt(target.index);
    const Core::GpuTaskTimingKey historyKey{ .task = task.identity, .queue = Core::CommandQueue::Graphics };
    const Core::GpuTaskTimingAssignmentKey assignmentKey{ .task = task.identity };
    const Array<f64, 5u> durations = { 0.010, 0.004, 0.004, 0.002, 0.002 };
    Core::GpuTaskTimingHistoryStore history(testArena.arena, 1u);
    for(usize queueIndex = 0u; queueIndex < LengthOf(queues); ++queueIndex)
        ASSERT_TRUE(history.recordNonCommittingSample(historyKey, queues[queueIndex].id, durations[queueIndex]));
    ASSERT_TRUE(history.noteAcceptedAssignment(historyKey, queues[0u].id, 1u));
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    GpuTaskSchedulingReachability reachability(scratch);
    ASSERT_TRUE(BuildGpuTaskSchedulingReachability(declarations, analysis, reachability));
    GpuTaskQueueScoringData scoringData(declarations, analysis, reachability, {}, scratch);
    const Array<Array<usize, 4u>, 5u> candidateOrders = { {
        { 1u, 2u, 3u, 4u },
        { 2u, 1u, 3u, 4u },
        { 4u, 3u, 2u, 1u },
        { 3u, 4u, 1u, 2u },
        { 1u, 3u, 2u, 4u },
    } };
    for(const auto& candidateOrder : candidateOrders){
        SCOPED_TRACE(candidateOrder[0u]);
        Array<Core::GpuPhysicalQueueInfo, LengthOf(queues)> reorderedQueues = {};
        reorderedQueues[0u] = queues[0u];
        for(usize queueIndex = 0u; queueIndex < LengthOf(candidateOrder); ++queueIndex)
            reorderedQueues[queueIndex + 1u] = queues[candidateOrder[queueIndex]];
        const Core::GpuPhysicalQueueTopology topology{ .queues = reorderedQueues.data(), .queueCount = LengthOf(reorderedQueues) };
        ASSERT_TRUE(IsValidQueueTopology(topology));
        scoringData.rebuildAssignmentLoads(assignments, topology);
        const Core::GpuPhysicalQueueInfo* const selected = FindTimingFeedbackQueue(declarations, analysis, assignments, assignmentIndices, topology, reachability, scoringData, task, reorderedQueues[0u], assignmentKey, snapshot, s_TimingPolicy, 100u);
        ASSERT_NE(selected, nullptr);
        // Queue one wins the slow tie while queue two wins the fast tie by lower load.
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

