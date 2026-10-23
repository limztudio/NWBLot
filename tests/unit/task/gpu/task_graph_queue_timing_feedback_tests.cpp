// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_timing_feedback_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, AppliesHistoricalTimingFeedbackWithHysteresisAndCompileOptions){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/timing_feedback_hysteresis");
    const Graphics::GpuTaskTimingMetadata timingMetadata{ .variant = 7u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(graph, taskIdentity, "Timing Feedback Hysteresis", graphicsCommands, scheduling, timingMetadata);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));

    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Graphics::GpuTaskTimingKey timingKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Graphics, };
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(timingKey, auxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(timingKey, queues[0u].id, 0.010, frameIndex));

    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 10u;
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 9u;

    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    // Disabled remains an exact deterministic fallback even when a complete snapshot is present.
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.enabled = true;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    // The accepted primary route switched at frame five, so the configured dwell period still retains it.
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.minimumFramesBetweenSwitches = 0u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Graphics::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, compileOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

}

TEST(GpuTaskGraph, CalibratesOptInTimingFeedbackBeforeHysteresisSwitches){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/timing_feedback_calibration");
    const Graphics::GpuTaskTimingMetadata timingMetadata{ .variant = 9u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(graph, taskIdentity, "Timing Feedback Calibration", graphicsCommands, scheduling, timingMetadata);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[]{ GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));

    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena, 1u);
    timingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    timingPolicy.calibrationIntervalFrames = 1u;
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    const Graphics::GpuTaskTimingKey timingKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Graphics, };

    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 0u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, queues[0u].id, 0.010));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 1u;
    Graphics::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, compileOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;

        ASSERT_NE(compiledTask, nullptr);
        EXPECT_TRUE(compiledTask->recordsNonCommittingTimingSample);
    }

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.020));
    const Graphics::GpuTaskTimingAssignmentKey assignmentKey = Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(timingKey);
    EXPECT_EQ(timingHistory.findAssignment(assignmentKey), nullptr);
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = s_ExpectedDualCount;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    // Replacing the probe history with a faster observation makes the first adaptive choice from the implicit deterministic baseline.
    // Neither calibration observation becomes a committed dwell incumbent.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.001));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 3u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    // An under-sampled probe remains bounded to its configured interval.
    // Its first observation neither becomes the incumbent nor causes the following non-calibration frame to retain that route.
    Graphics::GpuTaskTimingHistoryStore intervalTimingHistory(testArena.arena, s_ExpectedDualCount);
    intervalTimingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    ASSERT_TRUE(intervalTimingHistory.recordSample(timingKey, queues[0u].id, 0.010, 10u));
    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(timingKey, queues[0u].id, 0.010));
    Graphics::GpuTaskTimingHistorySnapshot intervalTimingSnapshot(testArena.arena);
    intervalTimingHistory.snapshot(intervalTimingSnapshot);
    Graphics::GpuTaskTimingFeedbackPolicy intervalTimingPolicy = timingPolicy;
    intervalTimingPolicy.minimumSampleCount = s_ExpectedDualCount;
    intervalTimingPolicy.calibrationIntervalFrames = s_ExpectedDualCount;
    Graphics::GpuTaskGraphQueueAssignmentOptions intervalAssignmentOptions;
    intervalAssignmentOptions.timingHistory = &intervalTimingSnapshot;
    intervalAssignmentOptions.timingFeedbackPolicy = intervalTimingPolicy;
    intervalAssignmentOptions.timingFrameIndex = 12u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.020));
    const Graphics::GpuTaskTimingAssignmentState* const intervalAssignmentState = intervalTimingHistory.findAssignment(assignmentKey);
    ASSERT_NE(intervalAssignmentState, nullptr);
    EXPECT_EQ(intervalAssignmentState->lastAcceptedQueue, queues[0u].id);
    EXPECT_EQ(intervalAssignmentState->lastAcceptedFrameIndex, 10u);
    EXPECT_EQ(intervalAssignmentState->lastSwitchFrameIndex, 10u);
    intervalTimingHistory.snapshot(intervalTimingSnapshot);
    intervalAssignmentOptions.timingFrameIndex = 13u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    intervalAssignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
}

TEST(GpuTaskGraph, IgnoresTimingFeedbackWithoutAnyEnabledRoute){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowTimingFeedbackRouting = true;
    const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/timing_feedback_without_enabled_route"), "Timing Feedback Without Enabled Route", graphicsCommands, scheduling);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    timingHistory.resetForDeviceGeneration(queue.id.deviceGeneration);
    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());
    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.calibrationIntervalFrames = 1u;
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queue.id);
    EXPECT_EQ(assignment->queue, queue.id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, QueueTimingScoreUsesOnlyReducedIncomingDependencies){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto handoffResult = AddHazardDomain(graph, Name("tests/task_graph/reduced_timing_score_handoff"), "Reduced Timing Score Handoff");
    ASSERT_TRUE(handoffResult);
    const Graphics::GpuGraphResourceId handoff = *handoffResult;
    ASSERT_TRUE(handoff.valid());

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;

    const Name firstIdentity("tests/task_graph/reduced_timing_score_first");
    const Graphics::GpuTaskResourceUse firstUse{ .resource = handoff, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    Graphics::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(firstIdentity).setMarkerLabel("Reduced Timing Score First").setScheduling(scheduling).setResourceUses(&firstUse, 1u);
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Graphics::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    const Name secondIdentity("tests/task_graph/reduced_timing_score_second");
    Graphics::GpuTaskDesc secondDesc;
    secondDesc.setIdentity(secondIdentity).setMarkerLabel("Reduced Timing Score Second").setScheduling(scheduling).setDependencies(&first, 1u);
    const auto secondResult = graph.addTask(secondDesc, graphicsCommands);
    ASSERT_TRUE(secondResult);
    const Graphics::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    const Name thirdIdentity("tests/task_graph/reduced_timing_score_third");
    const Graphics::GpuTaskResourceUse thirdUse{ .resource = handoff, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
    Graphics::GpuTaskDesc thirdDesc;
    thirdDesc.setIdentity(thirdIdentity).setMarkerLabel("Reduced Timing Score Third").setScheduling(scheduling).setDependencies(&second, 1u).setResourceUses(&thirdUse, 1u);
    const auto thirdResult = graph.addTask(thirdDesc, graphicsCommands);
    ASSERT_TRUE(thirdResult);
    const Graphics::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(third.valid());

    Graphics::GpuPhysicalQueueInfo firstAuxiliaryQueue = GraphicsQueue(1u);
    firstAuxiliaryQueue.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliaryQueue = GraphicsQueue(s_ExpectedDualCount);
    secondAuxiliaryQueue.queueIndex = s_ExpectedDualCount;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), firstAuxiliaryQueue, secondAuxiliaryQueue, };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };

    const Graphics::GpuTaskTimingKey firstTimingKey{ .task = firstIdentity, .queue = Graphics::CommandQueue::Graphics, };
    const Graphics::GpuTaskTimingKey secondTimingKey{ .task = secondIdentity, .queue = Graphics::CommandQueue::Graphics, };
    const Graphics::GpuTaskTimingKey thirdTimingKey{ .task = thirdIdentity, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    // A and B each have one measured fast route, so history alone splits them across the auxiliary queues.
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex){
        ASSERT_TRUE(timingHistory.recordSample(firstTimingKey, firstAuxiliaryQueue.id, 0.001, frameIndex));
        ASSERT_TRUE(timingHistory.recordSample(secondTimingKey, secondAuxiliaryQueue.id, 0.001, frameIndex));
        ASSERT_TRUE(timingHistory.recordSample(thirdTimingKey, firstAuxiliaryQueue.id, 0.001, frameIndex));
    }
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex){
        ASSERT_TRUE(timingHistory.recordSample(firstTimingKey, queues[0u].id, 0.010, frameIndex));
        ASSERT_TRUE(timingHistory.recordSample(secondTimingKey, queues[0u].id, 0.010, frameIndex));
        ASSERT_TRUE(timingHistory.recordSample(thirdTimingKey, secondAuxiliaryQueue.id, 0.001, frameIndex));
    }
    for(u64 frameIndex = 9u; frameIndex <= 12u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(thirdTimingKey, queues[0u].id, 0.010, frameIndex));
    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.calibrationIntervalFrames = 0u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.timingHistory = &timingSnapshot;
    options.timingFeedbackPolicy = timingPolicy;
    options.timingFrameIndex = 12u;

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), s_ExpectedDualCount);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, options));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Graphics::GpuTaskQueueAssignment* const thirdAssignment = assignments.find(third);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(thirdAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, firstAuxiliaryQueue.id);
    EXPECT_EQ(secondAssignment->queue, secondAuxiliaryQueue.id);
    // Equal timing and queue load leave incoming crossings decisive.
    // Only the reduced B -> C edge participates, so C stays with B instead of tying on the redundant A -> C diagnostic and falling back to the lower queue ID.
    EXPECT_EQ(thirdAssignment->queue, secondAuxiliaryQueue.id);
}

TEST(GpuTaskGraph, RejectsInvalidGraphOwnedTimingPolicy){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskTimingMetadata invalidTiming;
    invalidTiming.policy = static_cast<Graphics::GpuTaskTimingPolicy::Enum>(Graphics::GpuTaskTimingPolicy::kCount);
    const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/timing_policy_invalid"), "Timing Policy Invalid", {}, {}, invalidTiming);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

