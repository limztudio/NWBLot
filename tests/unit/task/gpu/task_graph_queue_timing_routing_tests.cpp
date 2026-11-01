// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_timing_routing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RoutesOptInTimingFeedbackAcrossGraphicsAndComputeClasses){
    const auto runCase = [](const b8 staticCompute){
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);

        Core::GpuTaskCommandRequirements graphicsCommands;
        graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
        if(staticCompute){
            const auto independentGraphicsResult = AddTaskWithCommands(Name("tests/task_graph/cross_class_timing_independent_graphics"), "Cross Class Timing Independent Graphics", graphicsCommands, {}, {}, {}, graph);
            ASSERT_TRUE(independentGraphicsResult);
            const Core::GpuTaskId independentGraphics = *independentGraphicsResult;
            ASSERT_TRUE(independentGraphics.valid());
        }

        Core::GpuTaskCommandRequirements computeCommands;
        computeCommands.requiredCapabilities = Core::GpuQueueCapability::Compute;
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.allowSameClassQueueRouting = false;
        scheduling.allowCrossFamilySameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        scheduling.allowCrossClassTimingFeedbackRouting = true;
        const Name taskIdentity(staticCompute ? "tests/task_graph/cross_class_timing_compute_to_graphics" : "tests/task_graph/cross_class_timing_graphics_to_compute");
        const Core::GpuTaskTimingMetadata timingMetadata{ .variant = staticCompute ? 11u : 12u, .resolutionClass = 0x07800438u, };
        const auto taskResult = AddTaskWithCommands(taskIdentity, "Cross Class Timing Target", computeCommands, scheduling, timingMetadata, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology));
        const Core::GpuTaskQueueAssignment* assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        const Core::GpuPhysicalQueueInfo& incumbentQueue = staticCompute ? queues[1u] : queues[0u];
        const Core::GpuPhysicalQueueInfo& candidateQueue = staticCompute ? queues[0u] : queues[1u];
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, incumbentQueue.id);

        const Core::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Graphics, };
        const Core::GpuTaskTimingKey computeKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Compute, };
        const Core::GpuTaskTimingKey& incumbentKey = staticCompute ? computeKey : graphicsKey;
        const Core::GpuTaskTimingKey& candidateKey = staticCompute ? graphicsKey : computeKey;
        Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena, 1u);
        ASSERT_TRUE(timingHistory.recordSample(candidateKey, candidateQueue.id, 0.001, 1u));
        ASSERT_TRUE(timingHistory.recordSample(incumbentKey, incumbentQueue.id, 0.010, 2u));

        Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
        timingHistory.snapshot(timingSnapshot);
        ASSERT_TRUE(timingSnapshot.valid());
        Core::GpuTaskTimingFeedbackPolicy timingPolicy;
        timingPolicy.enabled = true;
        timingPolicy.minimumSampleCount = 1u;
        timingPolicy.calibrationIntervalFrames = 0u;
        timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
        timingPolicy.minimumRelativeBenefit = 0.1;
        timingPolicy.minimumFramesBetweenSwitches = 0u;
        Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
        assignmentOptions.timingHistory = &timingSnapshot;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 10u;
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

        ASSERT_TRUE(timingHistory.noteAcceptedAssignment(candidateKey, candidateQueue.id, 10u));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(incumbentKey, incumbentQueue.id, 0.001));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(candidateKey, candidateQueue.id, 0.010));
        const Core::GpuTaskTimingAssignmentKey assignmentKey = Core::GpuTaskTimingAssignmentKeyFromHistoryKey(candidateKey);
        const Core::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(assignmentKey);
        ASSERT_NE(assignmentState, nullptr);
        EXPECT_EQ(assignmentState->lastAcceptedQueue, candidateQueue.id);
        EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 10u);
        timingHistory.snapshot(timingSnapshot);
        timingPolicy.minimumFramesBetweenSwitches = 10u;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 12u;
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

        assignmentOptions.timingFrameIndex = 20u;
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, incumbentQueue.id);
        EXPECT_EQ(assignment->queueClass, incumbentQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);
    };

    runCase(false);
    runCase(true);
}

TEST(GpuTaskGraph, CrossClassTimingCalibrationPreservesStaticBaselineAndHonorsHysteresisAndDwell){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    const auto independentGraphicsResult = AddTaskWithCommands(Name("tests/task_graph/cross_class_calibration_independent_graphics"), "Cross Class Calibration Independent Graphics", graphicsCommands, {}, {}, {}, graph);
    ASSERT_TRUE(independentGraphicsResult);
    const Core::GpuTaskId independentGraphics = *independentGraphicsResult;
    ASSERT_TRUE(independentGraphics.valid());

    Core::GpuTaskCommandRequirements computeCommands;
    computeCommands.requiredCapabilities = Core::GpuQueueCapability::Compute;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    scheduling.allowCrossClassTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/cross_class_timing_calibration");
    const Core::GpuTaskTimingMetadata timingMetadata{ .variant = 13u, .resolutionClass = 0x07800438u, };
    const auto taskResult = AddTaskWithCommands(taskIdentity, "Cross Class Timing Calibration", computeCommands, scheduling, timingMetadata, {}, graph);
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    timingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 1u;
    timingPolicy.calibrationIntervalFrames = 1u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 0u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    const Core::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Graphics, };
    const Core::GpuTaskTimingKey computeKey{ .task = taskIdentity, .variant = timingMetadata.variant, .resolutionClass = timingMetadata.resolutionClass, .queue = Core::CommandQueue::Compute, };
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 1u));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 1u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(graphicsKey, queues[0u].id, 0.004));
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 3u));
    EXPECT_EQ(timingHistory.historyCount(), 2u);
    const Core::GpuTaskTimingAssignmentKey assignmentKey = Core::GpuTaskTimingAssignmentKeyFromHistoryKey(computeKey);
    const Core::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(assignmentKey);
    ASSERT_NE(assignmentState, nullptr);
    EXPECT_EQ(assignmentState->lastAcceptedQueue, queues[1u].id);
    EXPECT_EQ(assignmentState->lastAcceptedFrameIndex, 3u);
    EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 1u);
    timingHistory.snapshot(timingSnapshot);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.007;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 20u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumFramesBetweenSwitches = 10u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 5u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    assignmentOptions.timingFrameIndex = 13u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, RejectsCrossClassTimingRoutesWithoutEveryRequiredOptIn){
    const auto runCase = [](const b8 allowCrossClassRouting, const b8 allowCrossFamilyRouting, const b8 requireGraphicsCapability){
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);

        Core::GpuTaskCommandRequirements graphicsCommands;
        graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
        const auto independentGraphicsResult = AddTaskWithCommands(Name("tests/task_graph/rejected_cross_class_independent_graphics"), "Rejected Cross Class Independent Graphics", graphicsCommands, {}, {}, {}, graph);
        ASSERT_TRUE(independentGraphicsResult);
        const Core::GpuTaskId independentGraphics = *independentGraphicsResult;
        ASSERT_TRUE(independentGraphics.valid());

        Core::GpuTaskCommandRequirements commands;
        commands.requiredCapabilities = requireGraphicsCapability ? Core::GpuQueueCapability::Graphics : Core::GpuQueueCapability::Compute;
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.allowSameClassQueueRouting = true;
        scheduling.allowCrossFamilySameClassQueueRouting = allowCrossFamilyRouting;
        scheduling.allowTimingFeedbackRouting = true;
        scheduling.allowCrossClassTimingFeedbackRouting = allowCrossClassRouting;
        Name taskIdentity("tests/task_graph/rejected_capability_cross_class_timing");
        if(!allowCrossClassRouting)
            taskIdentity = Name("tests/task_graph/rejected_missing_cross_class_timing_opt_in");
        else if(!allowCrossFamilyRouting)
            taskIdentity = Name("tests/task_graph/rejected_cross_family_cross_class_timing");
        const auto taskResult = AddTaskWithCommands(taskIdentity, "Rejected Cross Class Timing", commands, scheduling, {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
        const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        const Core::GpuTaskTimingKey graphicsKey{ .task = taskIdentity, .queue = Core::CommandQueue::Graphics, };
        const Core::GpuTaskTimingKey computeKey{ .task = taskIdentity, .queue = Core::CommandQueue::Compute, };
        const usize incumbentQueueIndex = requireGraphicsCapability ? 0u : 1u;
        const usize candidateQueueIndex = requireGraphicsCapability ? 1u : 0u;
        const Core::GpuTaskTimingKey& incumbentKey = requireGraphicsCapability ? graphicsKey : computeKey;
        const Core::GpuTaskTimingKey& candidateKey = requireGraphicsCapability ? computeKey : graphicsKey;
        Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
        for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
            ASSERT_TRUE(timingHistory.recordSample(candidateKey, queues[candidateQueueIndex].id, 0.001, frameIndex));
        for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
            ASSERT_TRUE(timingHistory.recordSample(incumbentKey, queues[incumbentQueueIndex].id, 0.010, frameIndex));
        Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
        timingHistory.snapshot(timingSnapshot);
        ASSERT_TRUE(timingSnapshot.valid());
        Core::GpuTaskTimingFeedbackPolicy timingPolicy;
        timingPolicy.enabled = true;
        timingPolicy.minimumSampleCount = 4u;
        timingPolicy.calibrationIntervalFrames = 0u;
        timingPolicy.minimumFramesBetweenSwitches = 0u;
        Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
        assignmentOptions.timingHistory = &timingSnapshot;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 20u;
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
        const Core::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, queues[incumbentQueueIndex].id);
        EXPECT_EQ(assignment->queue, queues[incumbentQueueIndex].id);
        EXPECT_FALSE(assignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

        // A rejected policy must also drop the earlier valid result.
        timingPolicy.minimumRelativeBenefit = 2.0;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        EXPECT_FALSE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
        EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);
        EXPECT_FALSE(assignments.valid());
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);

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
    Core::GpuTaskGraph graph(testArena.arena);

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    Core::GpuTaskSchedulingHint preloadScheduling;
    preloadScheduling.cost = Core::GpuTaskCostHint::Large;
    preloadScheduling.allowSameClassQueueRouting = true;
    preloadScheduling.preferNonPrimarySameClassQueue = true;
    const auto preloadResult = AddTaskWithCommands(Name("tests/task_graph/timing_feedback_preload"), "Timing Feedback Preload", graphicsCommands, preloadScheduling, {}, {}, graph);
    ASSERT_TRUE(preloadResult);
    const Core::GpuTaskId preload = *preloadResult;
    ASSERT_TRUE(preload.valid());

    Core::GpuTaskSchedulingHint targetScheduling;
    targetScheduling.allowSameClassQueueRouting = true;
    targetScheduling.allowTimingFeedbackRouting = true;
    const Name targetIdentity("tests/task_graph/timing_feedback_equal_candidates");
    const auto targetResult = AddTaskWithCommands(targetIdentity, "Timing Feedback Equal Candidates", graphicsCommands, targetScheduling, {}, {}, graph);
    ASSERT_TRUE(targetResult);
    const Core::GpuTaskId target = *targetResult;
    ASSERT_TRUE(target.valid());

    Core::GpuPhysicalQueueInfo firstAuxiliaryGraphicsQueue = GraphicsQueue(1u);
    firstAuxiliaryGraphicsQueue.queueIndex = 1u;
    Core::GpuPhysicalQueueInfo secondAuxiliaryGraphicsQueue = GraphicsQueue(2u);
    secondAuxiliaryGraphicsQueue.queueIndex = 2u;
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), firstAuxiliaryGraphicsQueue, secondAuxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));

    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Core::GpuTaskTimingKey targetTimingKey{ .task = targetIdentity, .queue = Core::CommandQueue::Graphics, };
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, firstAuxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, secondAuxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 9u; frameIndex <= 12u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(targetTimingKey, queues[0u].id, 0.010, frameIndex));

    Core::GpuTaskTimingHistorySnapshot timingSnapshot(testArena.arena);
    timingHistory.snapshot(timingSnapshot);
    ASSERT_TRUE(timingSnapshot.valid());

    Core::GpuTaskTimingFeedbackPolicy timingPolicy;
    timingPolicy.enabled = true;
    timingPolicy.minimumSampleCount = 4u;
    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumRelativeBenefit = 0.1;
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 12u;

    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* const preloadAssignment = assignments.find(preload);
    const Core::GpuTaskQueueAssignment* const targetAssignment = assignments.find(target);
    ASSERT_NE(preloadAssignment, nullptr);
    ASSERT_NE(targetAssignment, nullptr);
    EXPECT_EQ(preloadAssignment->queue, firstAuxiliaryGraphicsQueue.id);
    // Choose the unloaded second auxiliary queue when both histories are equally fast.
    EXPECT_EQ(targetAssignment->queue, secondAuxiliaryGraphicsQueue.id);

    // A non-committing sample adds route history but must not move the committed incumbent or restart its dwell.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(targetTimingKey, firstAuxiliaryGraphicsQueue.id, 0.020));
    const Core::GpuTaskTimingAssignmentKey targetAssignmentKey = Core::GpuTaskTimingAssignmentKeyFromHistoryKey(targetTimingKey);
    const Core::GpuTaskTimingAssignmentState* const targetAssignmentState = timingHistory.findAssignment(targetAssignmentKey);
    ASSERT_NE(targetAssignmentState, nullptr);
    EXPECT_EQ(targetAssignmentState->lastAcceptedQueue, queues[0u].id);
    EXPECT_EQ(targetAssignmentState->lastSwitchFrameIndex, 9u);
    timingHistory.snapshot(timingSnapshot);
    timingPolicy.minimumFramesBetweenSwitches = 30u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, topology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* const dwellAssignment = assignments.find(target);
    ASSERT_NE(dwellAssignment, nullptr);
    EXPECT_EQ(dwellAssignment->queue, queues[0u].id);
    EXPECT_FALSE(dwellAssignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    // The faster history on queue ID 1 now names another family, and the target has no cross-family opt-in.
    Core::GpuPhysicalQueueInfo crossFamilyGraphicsQueue = GraphicsQueue(1u);
    crossFamilyGraphicsQueue.familyIndex = 1u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> crossFamilyQueues = { GraphicsQueue(), crossFamilyGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology crossFamilyTopology{ .queues = crossFamilyQueues.data(), .queueCount = LengthOf(crossFamilyQueues), };
    timingPolicy.minimumFramesBetweenSwitches = 0u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    ASSERT_TRUE(Assign(assignments, operationScratchArena, graph, analysis, crossFamilyTopology, assignmentOptions));
    const Core::GpuTaskQueueAssignment* const crossFamilyAssignment = assignments.find(target);
    ASSERT_NE(crossFamilyAssignment, nullptr);
    EXPECT_EQ(crossFamilyAssignment->queue, queues[0u].id);
    EXPECT_FALSE(crossFamilyAssignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, RoutesOptedInCrossFamilyTimingFeedbackWithExclusiveOwnershipHandoffs){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/task_graph/timing_feedback_cross_family_buffer"), "Timing Feedback Cross Family Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());

    Core::GpuTaskCommandRequirements graphicsCommands;
    graphicsCommands.requiredCapabilities = Core::GpuQueueCapability::Graphics;

    const Core::GpuTaskResourceUse producerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    Core::GpuTaskDesc producerDesc;
    producerDesc.setIdentity(Name("tests/task_graph/timing_feedback_cross_family_producer")).setMarkerLabel("Timing Feedback Cross Family Producer").setResourceUses(Span(&producerUse, 1u));
    const auto producerResult = graph.addTask(producerDesc, graphicsCommands);
    ASSERT_TRUE(producerResult);
    const Core::GpuTaskId producer = *producerResult;
    ASSERT_TRUE(producer.valid());

    Core::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    consumerScheduling.allowTimingFeedbackRouting = true;
    const Array<Core::GpuTaskId, 1u> consumerDependencies = { producer };
    const Core::GpuTaskResourceUse consumerUse{ .resource = buffer, .range = {}, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, };
    const Name consumerIdentity("tests/task_graph/timing_feedback_cross_family_consumer");
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(consumerIdentity).setMarkerLabel("Timing Feedback Cross Family Consumer").setScheduling(consumerScheduling).setDependencies(consumerDependencies).setResourceUses(Span(&consumerUse, 1u));
    const auto consumerResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerResult);
    const Core::GpuTaskId consumer = *consumerResult;
    ASSERT_TRUE(consumer.valid());

    Core::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), auxiliaryGraphicsQueue, };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };

    const Core::GpuTaskTimingKey consumerTimingKey{ .task = consumerIdentity, .queue = Core::CommandQueue::Graphics, };
    Core::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(consumerTimingKey, auxiliaryGraphicsQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(consumerTimingKey, queues[0u].id, 0.010, frameIndex));

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
    Core::GpuTaskGraphQueueAssignmentOptions assignmentOptions;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 8u;
    Core::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions = assignmentOptions;

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, compileOptions));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Core::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(consumerAssignment->reason, Core::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(consumerAssignment->modifiers & Core::GpuTaskQueueAssignmentModifier::TimingFeedback);

    const Core::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
    const Core::GpuSubmissionPacketId consumerPacket = compiledPlan.packetForTask(consumer);
    const Core::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumer).plan;
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    ASSERT_NE(producerPacket, consumerPacket);
    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(consumerPacket).dependencies[0u].producer, producerPacket);
    ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

    const Core::GpuCompiledBarrier& release = compiledPlan.findTask(producer).epilogueBarriers[0u];
    EXPECT_EQ(release.type, Core::GpuCompiledBarrierType::BufferOwnershipRelease);
    EXPECT_EQ(release.resource, buffer);
    EXPECT_EQ(release.sourceQueue, queues[0u].id);
    EXPECT_EQ(release.destinationQueue, auxiliaryGraphicsQueue.id);

    const Core::GpuCompiledBarrier* const acquireAndTransition = compiledPlan.findTask(consumer).prologueBarriers;
    ASSERT_NE(acquireAndTransition, nullptr);
    EXPECT_EQ(acquireAndTransition[0u].type, Core::GpuCompiledBarrierType::BufferOwnershipAcquire);
    EXPECT_EQ(acquireAndTransition[0u].resource, buffer);
    EXPECT_EQ(acquireAndTransition[0u].sourceQueue, queues[0u].id);
    EXPECT_EQ(acquireAndTransition[0u].destinationQueue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(acquireAndTransition[1u].type, Core::GpuCompiledBarrierType::BufferTransition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

