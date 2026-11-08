// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_timing_feedback_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, AppliesHistoricalTimingFeedbackWithHysteresisAndCompileOptions){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphTimingFeedbackHysteresis;
    const Core::GpuTaskTimingMetadata timingMetadata{ .variant = 7u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(taskIdentity, "Timing Feedback Hysteresis", graphicsCommands, scheduling, timingMetadata, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));

    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Core::GpuTaskTimingKey timingKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Graphics, };
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(timingKey, auxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(timingKey, queues[0u].id, 0.010, frameIndex));

    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 10u;
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 9u;

    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    // Disabled remains an exact deterministic fallback even when a complete snapshot is present.
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.enabled = true;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    // The accepted primary route switched at frame five, so the configured dwell period still retains it.
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.minimumFramesBetweenSwitches = 0u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, compileOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Core::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

}

TEST(GpuTaskGraph, CalibratesOptInTimingFeedbackBeforeHysteresisSwitches){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphTimingFeedbackCalibration;
    const Core::GpuTaskTimingMetadata timingMetadata{ .variant = 9u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(taskIdentity, "Timing Feedback Calibration", graphicsCommands, scheduling, timingMetadata, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));

    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena, 1u);
    timingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    timingPolicy.calibrationIntervalFrames = 1u;
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    const Core::GpuTaskTimingKey timingKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Graphics, };

    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 0u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, queues[0u].id, 0.010));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 1u;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, compileOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Core::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);
    {
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;

        ASSERT_NE(compiledTask, nullptr);
        EXPECT_TRUE(compiledTask->recordsNonCommittingTimingSample);
    }

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.020));
    const Core::GpuTaskTimingAssignmentKey assignmentKey = Core::GpuTaskTimingAssignmentKeyFromHistoryKey(timingKey);
    EXPECT_EQ(timingHistory.findAssignment(assignmentKey), nullptr);
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 2u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);

    // Calibration observations update route history without becoming the committed incumbent.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.001));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 3u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Core::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    // Under-sampled probe frames do not commit the probed route as the incumbent.
    Core::GpuTaskTimingHistoryStore intervalTimingHistory(testArena.arena, 2u);
    intervalTimingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    ASSERT_TRUE(intervalTimingHistory.recordSample(timingKey, queues[0u].id, 0.010, 10u));
    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(timingKey, queues[0u].id, 0.010));
    Core::GpuTaskTimingHistorySnapshot intervalTimingSnapshot(testArena.arena);
    intervalTimingHistory.snapshot(intervalTimingSnapshot);
    Core::GpuTaskTimingFeedbackPolicy intervalTimingPolicy = timingPolicy;
    intervalTimingPolicy.minimumSampleCount = 2u;
    intervalTimingPolicy.calibrationIntervalFrames = 2u;
    Core::GpuTaskGraphQueueAssignmentOptions intervalAssignmentOptions;
    intervalAssignmentOptions.timingHistory = &intervalTimingSnapshot;
    intervalAssignmentOptions.timingFeedbackPolicy = intervalTimingPolicy;
    intervalAssignmentOptions.timingFrameIndex = 12u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.020));
    const Core::GpuTaskTimingAssignmentState* const intervalAssignmentState = intervalTimingHistory.findAssignment(assignmentKey);
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
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    intervalAssignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);
}

TEST(GpuTaskGraph, IgnoresTimingFeedbackWithoutAnyEnabledRoute){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowTimingFeedbackRouting = true;
    const auto taskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingFeedbackWithoutEnabledRoute), "Timing Feedback Without Enabled Route", graphicsCommands, scheduling, {}, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    timingHistory.resetForDeviceGeneration(queue.id.deviceGeneration);
    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());
    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.calibrationIntervalFrames = 1u;
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* const assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queue.id);
    EXPECT_EQ(assignment->queue, queue.id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, QueueTimingScoreUsesOnlyReducedIncomingDependencies){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto handoffResult = AddHazardDomain(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphReducedTimingScoreHandoff), "Reduced Timing Score Handoff");
    ASSERT_TRUE(handoffResult);
    const Core::GpuGraphResourceId handoff = *handoffResult;
    ASSERT_TRUE(handoff.valid());

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;

    const Name firstIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphReducedTimingScoreFirst;
    const Core::GpuTaskResourceUse firstUse{ .resource = handoff, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(firstIdentity).setMarkerLabel("Reduced Timing Score First").setScheduling(scheduling).setResourceUses(Span(&firstUse, 1u));
    const auto firstResult = graph.addTask(firstDesc, graphicsCommands);
    ASSERT_TRUE(firstResult);
    const Core::GpuTaskId first = *firstResult;
    ASSERT_TRUE(first.valid());

    const Name secondIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphReducedTimingScoreSecond;
    Core::GpuTaskDesc secondDesc;
    secondDesc.setIdentity(secondIdentity).setMarkerLabel("Reduced Timing Score Second").setScheduling(scheduling).setDependencies(Span(&first, 1u));
    const auto secondResult = graph.addTask(secondDesc, graphicsCommands);
    ASSERT_TRUE(secondResult);
    const Core::GpuTaskId second = *secondResult;
    ASSERT_TRUE(second.valid());

    const Name thirdIdentity = NWB::Tests::TaskGraphTestUtils::s_TaskGraphReducedTimingScoreThird;
    const Core::GpuTaskResourceUse thirdUse{ .resource = handoff, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskDesc thirdDesc;
    thirdDesc.setIdentity(thirdIdentity).setMarkerLabel("Reduced Timing Score Third").setScheduling(scheduling).setDependencies(Span(&second, 1u)).setResourceUses(Span(&thirdUse, 1u));
    const auto thirdResult = graph.addTask(thirdDesc, graphicsCommands);
    ASSERT_TRUE(thirdResult);
    const Core::GpuTaskId third = *thirdResult;
    ASSERT_TRUE(third.valid());

    Core::GpuPhysicalQueueInfo firstAuxiliaryQueue = GraphicsQueue(1u);
    firstAuxiliaryQueue.queueIndex = 1u;
    Core::GpuPhysicalQueueInfo secondAuxiliaryQueue = GraphicsQueue(2u);
    secondAuxiliaryQueue.queueIndex = 2u;
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), firstAuxiliaryQueue, secondAuxiliaryQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };

    const Core::GpuTaskTimingKey firstTimingKey{ .task = firstIdentity, .queue = Core::CommandQueue::Graphics, };
    const Core::GpuTaskTimingKey secondTimingKey{ .task = secondIdentity, .queue = Core::CommandQueue::Graphics, };
    const Core::GpuTaskTimingKey thirdTimingKey{ .task = thirdIdentity, .queue = Core::CommandQueue::Graphics, };
    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
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
    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.calibrationIntervalFrames = 0u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Core::GpuTaskGraphQueueAssignmentOptions options;
    options.timingHistory = &timingSnapshot;
    options.timingFeedbackPolicy = timingPolicy;
    options.timingFrameIndex = 12u;

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, options));
    const Core::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Core::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Core::GpuTaskQueueAssignment* const thirdAssignment = assignments.find(third);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(thirdAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, firstAuxiliaryQueue.id);
    EXPECT_EQ(secondAssignment->queue, secondAuxiliaryQueue.id);
    // Transitive reduction drops the redundant A-to-C edge so C stays on B's queue.
    EXPECT_EQ(thirdAssignment->queue, secondAuxiliaryQueue.id);
}

TEST(GpuTaskGraph, RejectsInvalidGraphOwnedTimingPolicy){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskTimingMetadata invalidTiming;
    invalidTiming.policy = static_cast<Core::GpuTaskTimingPolicy::Enum>(Core::GpuTaskTimingPolicy::Count);
    const auto taskResult = AddTaskWithCommands(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphTimingPolicyInvalid), "Timing Policy Invalid", {}, {}, invalidTiming, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

