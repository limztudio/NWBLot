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


namespace __hidden_task_graph_queue_timing_routing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RoutesOptInTimingFeedbackAcrossGraphicsAndComputeClasses){
    const auto runCase = [](const bool staticCompute){
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);

        Graphics::GpuTaskCommandRequirements graphicsCommands;
        graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        if(staticCompute){
            const auto independentGraphicsResult = AddTaskWithCommands(graph, Name("tests/task_graph/cross_class_timing_independent_graphics"), "Cross Class Timing Independent Graphics", graphicsCommands);
            ASSERT_TRUE(independentGraphicsResult);
            const Graphics::GpuTaskId independentGraphics = *independentGraphicsResult;
            ASSERT_TRUE(independentGraphics.valid());
        }

        Graphics::GpuTaskCommandRequirements computeCommands;
        computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.allowSameClassQueueRouting = false;
        scheduling.allowCrossFamilySameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        scheduling.allowCrossClassTimingFeedbackRouting = true;
        const Name taskIdentity(staticCompute ? "tests/task_graph/cross_class_timing_compute_to_graphics" : "tests/task_graph/cross_class_timing_graphics_to_compute");
        const Graphics::GpuTaskTimingMetadata timingMetadata{ .variant = staticCompute ? 11u : 12u, .resolutionClass = 0x07800438u, };
        const auto taskResult = AddTaskWithCommands(graph, taskIdentity, "Cross Class Timing Target", computeCommands, scheduling, timingMetadata);
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments));
        const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        const Graphics::GpuPhysicalQueueInfo& incumbentQueue = staticCompute ? queues[1u] : queues[0u];
        const Graphics::GpuPhysicalQueueInfo& candidateQueue = staticCompute ? queues[0u] : queues[1u];
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, incumbentQueue.id);

        const Graphics::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Graphics, };
        const Graphics::GpuTaskTimingKey computeKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Compute, };
        const Graphics::GpuTaskTimingKey& incumbentKey = staticCompute ? computeKey : graphicsKey;
        const Graphics::GpuTaskTimingKey& candidateKey = staticCompute ? graphicsKey : computeKey;
        Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena, 1u);
        ASSERT_TRUE(timingHistory.recordSample(candidateKey, candidateQueue.id, 0.001, 1u));
        ASSERT_TRUE(timingHistory.recordSample(incumbentKey, incumbentQueue.id, 0.010, s_ExpectedDualCount));

        Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
        timingHistory.snapshot(timingSnapshot);
        ASSERT_TRUE(timingSnapshot.valid());
        Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
        timingPolicy.enabled = true;
        timingPolicy.minimumSampleCount = 1u;
        timingPolicy.calibrationIntervalFrames = 0u;
        timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
        timingPolicy.minimumRelativeBenefit = 0.1;
        timingPolicy.minimumFramesBetweenSwitches = 0u;
        Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
        assignmentOptions.timingHistory = &timingSnapshot;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 10u;
        ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        ASSERT_TRUE(timingHistory.noteAcceptedAssignment(candidateKey, candidateQueue.id, 10u));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(incumbentKey, incumbentQueue.id, 0.001));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(candidateKey, candidateQueue.id, 0.010));
        const Graphics::GpuTaskTimingAssignmentKey assignmentKey = Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(candidateKey);
        const Graphics::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(assignmentKey);
        ASSERT_NE(assignmentState, nullptr);
        EXPECT_EQ(assignmentState->lastAcceptedQueue, candidateQueue.id);
        EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 10u);
        timingHistory.snapshot(timingSnapshot);
        timingPolicy.minimumFramesBetweenSwitches = 10u;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 12u;
        ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        assignmentOptions.timingFrameIndex = 20u;
        ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, incumbentQueue.id);
        EXPECT_EQ(assignment->queueClass, incumbentQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
    };

    runCase(false);
    runCase(true);
}

TEST(GpuTaskGraph, CrossClassTimingCalibrationPreservesStaticBaselineAndHonorsHysteresisAndDwell){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    const auto independentGraphicsResult = AddTaskWithCommands(graph, Name("tests/task_graph/cross_class_calibration_independent_graphics"), "Cross Class Calibration Independent Graphics", graphicsCommands);
    ASSERT_TRUE(independentGraphicsResult);
    const Graphics::GpuTaskId independentGraphics = *independentGraphicsResult;
    ASSERT_TRUE(independentGraphics.valid());

    Graphics::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    scheduling.allowCrossClassTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/cross_class_timing_calibration");
    const Graphics::GpuTaskTimingMetadata timingMetadata{ .variant = 13u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(graph, taskIdentity, "Cross Class Timing Calibration", computeCommands, scheduling, timingMetadata);
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    timingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.calibrationIntervalFrames = 1u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 0u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    const Graphics::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Graphics, };
    const Graphics::GpuTaskTimingKey computeKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Graphics::CommandQueue::Compute, };
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 1u));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 1u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(graphicsKey, queues[0u].id, 0.004));
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 3u));
    EXPECT_EQ(timingHistory.historyCount(), s_ExpectedDualCount);
    const Graphics::GpuTaskTimingAssignmentKey assignmentKey = Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(computeKey);
    const Graphics::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(assignmentKey);
    ASSERT_NE(assignmentState, nullptr);
    EXPECT_EQ(assignmentState->lastAcceptedQueue, queues[1u].id);
    EXPECT_EQ(assignmentState->lastAcceptedFrameIndex, 3u);
    EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 1u);
    timingHistory.snapshot(timingSnapshot);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.007;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 20u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumFramesBetweenSwitches = 10u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 5u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    assignmentOptions.timingFrameIndex = 13u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, RejectsCrossClassTimingRoutesWithoutEveryRequiredOptIn){
    const auto runCase = [](const bool allowCrossClassRouting, const bool allowCrossFamilyRouting, const bool requireGraphicsCapability){
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);

        Graphics::GpuTaskCommandRequirements graphicsCommands;
        graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        const auto independentGraphicsResult = AddTaskWithCommands(graph, Name("tests/task_graph/rejected_cross_class_independent_graphics"), "Rejected Cross Class Independent Graphics", graphicsCommands);
        ASSERT_TRUE(independentGraphicsResult);
        const Graphics::GpuTaskId independentGraphics = *independentGraphicsResult;
        ASSERT_TRUE(independentGraphics.valid());

        Graphics::GpuTaskCommandRequirements commands;
        commands.requiredCapabilities = requireGraphicsCapability ? Graphics::GpuQueueCapability::Graphics : Graphics::GpuQueueCapability::Compute;
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.allowSameClassQueueRouting = true;
        scheduling.allowCrossFamilySameClassQueueRouting = allowCrossFamilyRouting;
        scheduling.allowTimingFeedbackRouting = true;
        scheduling.allowCrossClassTimingFeedbackRouting = allowCrossClassRouting;
        Name taskIdentity("tests/task_graph/rejected_capability_cross_class_timing");
        if(!allowCrossClassRouting)
            taskIdentity = Name("tests/task_graph/rejected_missing_cross_class_timing_opt_in");
        else if(!allowCrossFamilyRouting)
            taskIdentity = Name("tests/task_graph/rejected_cross_family_cross_class_timing");
        const auto taskResult = AddTaskWithCommands(graph, taskIdentity, "Rejected Cross Class Timing", commands, scheduling);
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
        const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
        const Graphics::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .queue = Graphics::CommandQueue::Graphics, };
        const Graphics::GpuTaskTimingKey computeKey{ .task = taskIdentity, .queue = Graphics::CommandQueue::Compute, };
        const usize incumbentQueueIndex = requireGraphicsCapability ? 0u : 1u;
        const usize candidateQueueIndex = requireGraphicsCapability ? 1u : 0u;
        const Graphics::GpuTaskTimingKey& incumbentKey = requireGraphicsCapability ? graphicsKey : computeKey;
        const Graphics::GpuTaskTimingKey& candidateKey = requireGraphicsCapability ? computeKey : graphicsKey;
        Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
        for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
            ASSERT_TRUE(timingHistory.recordSample(candidateKey, queues[candidateQueueIndex].id, 0.001, frameIndex));
        for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
            ASSERT_TRUE(timingHistory.recordSample(incumbentKey, queues[incumbentQueueIndex].id, 0.010, frameIndex));
        Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
        timingHistory.snapshot(timingSnapshot);
        ASSERT_TRUE(timingSnapshot.valid());
        Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
        timingPolicy.enabled = true;
        timingPolicy.minimumSampleCount = 4u;
        timingPolicy.calibrationIntervalFrames = 0u;
        timingPolicy.minimumFramesBetweenSwitches = 0u;
        Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
        assignmentOptions.timingHistory = &timingSnapshot;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 20u;
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, queues[incumbentQueueIndex].id);
        EXPECT_EQ(assignment->queue, queues[incumbentQueueIndex].id);
        EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        // A rejected policy must also drop the earlier valid result.
        timingPolicy.minimumRelativeBenefit = 2.0;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        EXPECT_FALSE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
        EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);
        EXPECT_FALSE(assignments.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_FALSE(assignments.validFor(declarations));
        }
        EXPECT_EQ(assignments.find(task), nullptr);
    };

    runCase(false, true, false);
    runCase(true, false, false);
    runCase(true, true, true);
}

TEST(GpuTaskGraph, RanksEqualTimingRoutesDeterministicallyAndKeepsDwellAndFamilyOptIn){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    Graphics::GpuTaskSchedulingHint preloadScheduling;
    preloadScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preloadScheduling.allowSameClassQueueRouting = true;
    preloadScheduling.preferNonPrimarySameClassQueue = true;
    const auto preloadResult = AddTaskWithCommands(graph, Name("tests/task_graph/timing_feedback_preload"), "Timing Feedback Preload", graphicsCommands, preloadScheduling);
    ASSERT_TRUE(preloadResult);
    const Graphics::GpuTaskId preload = *preloadResult;
    ASSERT_TRUE(preload.valid());

    Graphics::GpuTaskSchedulingHint targetScheduling;
    targetScheduling.allowSameClassQueueRouting = true;
    targetScheduling.allowTimingFeedbackRouting = true;
    const Name targetIdentity("tests/task_graph/timing_feedback_equal_candidates");
    const auto targetResult = AddTaskWithCommands(graph, targetIdentity, "Timing Feedback Equal Candidates", graphicsCommands, targetScheduling);
    ASSERT_TRUE(targetResult);
    const Graphics::GpuTaskId target = *targetResult;
    ASSERT_TRUE(target.valid());

    Graphics::GpuPhysicalQueueInfo firstAuxiliaryGraphicsQueue = GraphicsQueue(1u);
    firstAuxiliaryGraphicsQueue.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliaryGraphicsQueue = GraphicsQueue(s_ExpectedDualCount);
    secondAuxiliaryGraphicsQueue.queueIndex = s_ExpectedDualCount;
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), firstAuxiliaryGraphicsQueue, secondAuxiliaryGraphicsQueue, };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));

    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Graphics::GpuTaskTimingKey targetTimingKey{ .task = targetIdentity, .queue = Graphics::CommandQueue::Graphics, };
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, firstAuxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, secondAuxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 9u; frameIndex <= 12u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, queues[0u].id, 0.010, frameIndex));

    Graphics::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Graphics::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 12u;

    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const preloadAssignment = assignments.find(preload);
    const Graphics::GpuTaskQueueAssignment* const targetAssignment = assignments.find(target);
    ASSERT_NE(preloadAssignment, nullptr);
    ASSERT_NE(targetAssignment, nullptr);
    EXPECT_EQ(preloadAssignment->queue, firstAuxiliaryGraphicsQueue.id);
    // Both auxiliary histories are equally fast.
    // The first already owns the expensive preload, so the score must choose the second route instead of depending on topology order alone.
    EXPECT_EQ(targetAssignment->queue, secondAuxiliaryGraphicsQueue.id);

    // A non-committing sample adds route history but must not move the committed incumbent or restart its dwell.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(targetTimingKey, firstAuxiliaryGraphicsQueue.id, 0.020));
    const Graphics::GpuTaskTimingAssignmentKey targetAssignmentKey = Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(targetTimingKey);
    const Graphics::GpuTaskTimingAssignmentState* const targetAssignmentState = timingHistory.findAssignment(targetAssignmentKey);
    ASSERT_NE(targetAssignmentState, nullptr);
    EXPECT_EQ(targetAssignmentState->lastAcceptedQueue, queues[0u].id);
    EXPECT_EQ(targetAssignmentState->lastSwitchFrameIndex, 9u);
    timingHistory.snapshot(timingSnapshot);
    timingPolicy.minimumFramesBetweenSwitches = 30u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const dwellAssignment = assignments.find(target);
    ASSERT_NE(dwellAssignment, nullptr);
    EXPECT_EQ(dwellAssignment->queue, queues[0u].id);
    EXPECT_FALSE(dwellAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    // The faster history on queue ID 1 now names another family, and the target has no cross-family opt-in.
    Graphics::GpuPhysicalQueueInfo crossFamilyGraphicsQueue = GraphicsQueue(1u);
    crossFamilyGraphicsQueue.familyIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo crossFamilyQueues[] = { GraphicsQueue(), crossFamilyGraphicsQueue, };
    const Graphics::GpuPhysicalQueueTopology crossFamilyTopology{ .queues = crossFamilyQueues, .queueCount = LengthOf(crossFamilyQueues), };
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    ASSERT_TRUE(Assign(operationScratchArena, graph, analysis, crossFamilyTopology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const crossFamilyAssignment = assignments.find(target);
    ASSERT_NE(crossFamilyAssignment, nullptr);
    EXPECT_EQ(crossFamilyAssignment->queue, queues[0u].id);
    EXPECT_FALSE(crossFamilyAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, RoutesOptedInCrossFamilyTimingFeedbackWithExclusiveOwnershipHandoffs){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/timing_feedback_cross_family_buffer"), "Timing Feedback Cross Family Buffer");
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;

    const Graphics::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write, };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/timing_feedback_cross_family_producer")).setMarkerLabel("Timing Feedback Cross Family Producer").setResourceUses(&producerUse, 1u);
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Graphics::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    consumerScheduling.allowTimingFeedbackRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Graphics::ResourceStates::CopySource, .access = Graphics::GpuTaskResourceAccess::Read, };
    const Name consumerIdentity("tests/task_graph/timing_feedback_cross_family_consumer");
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(consumerIdentity).setMarkerLabel("Timing Feedback Cross Family Consumer").setScheduling(consumerScheduling).setDependencies(consumerDependencies, LengthOf(consumerDependencies)).setResourceUses(&consumerUse, 1u);
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Graphics::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[]{ GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };

    const Graphics::GpuTaskTimingKey consumerTimingKey{ .task = consumerIdentity, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(consumerTimingKey, auxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(consumerTimingKey, queues[0u].id, 0.010, frameIndex));

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
    Graphics::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 8u;
    Graphics::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(operationScratchArena, graph, analysis, topology, assignments, compiledGraph, compileOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(consumerAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    ASSERT_NE(producerPacket, consumerPacket);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
    ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, s_ExpectedDualCount);

    const Graphics::GpuCompiledBarrier& release = compiledPlan.findTask(producer).epilogueBarriers[0u];
    EXPECT_EQ(release.type, Graphics::GpuCompiledBarrierType::BufferOwnershipRelease);
    EXPECT_EQ(release.resource, buffer);
    EXPECT_EQ(release.sourceQueue, queues[0u].id);
    EXPECT_EQ(release.destinationQueue, auxiliaryGraphicsQueue.id);

    const Graphics::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(acquireAndTransition, nullptr);
    EXPECT_EQ(acquireAndTransition[0u].type, Graphics::GpuCompiledBarrierType::BufferOwnershipAcquire);
    EXPECT_EQ(acquireAndTransition[0u].resource, buffer);
    EXPECT_EQ(acquireAndTransition[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(acquireAndTransition[0u].destinationQueue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(acquireAndTransition[1u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

