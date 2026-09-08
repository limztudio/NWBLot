// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/timing_feedback_hysteresis");
    const Graphics::GpuTaskTimingMetadata timingMetadata{
        .variant = 7u,
        .resolutionClass = 0x07800438u,
    };
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        taskIdentity,
        "Timing Feedback Hysteresis",
        graphicsRequest,
        scheduling,
        timingMetadata
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));

    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Graphics::GpuTaskTimingKey timingKey{
        .task = taskIdentity,
        .variant = timingMetadata.variant,
        .resolutionClass = timingMetadata.resolutionClass,
        .queue = Graphics::CommandQueue::Graphics,
    };
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
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.enabled = true;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    // The accepted primary route switched at frame five, so the configured dwell period still retains it.
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
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
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
        .queueAssignments = &assignments,
        .compiledPlan = nullptr,
        .queueAssignmentTelemetry = nullptr,
    };
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(
        nodes[0u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
        | Graphics::GpuTaskGraphTelemetryNodeFlag::QueueAssignmentSameClassRouting
        | Graphics::GpuTaskGraphTelemetryNodeFlag::QueueAssignmentTimingRouting
    );
}

TEST(GpuTaskGraph, CalibratesOptInTimingFeedbackBeforeHysteresisSwitches){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/timing_feedback_calibration");
    const Graphics::GpuTaskTimingMetadata timingMetadata{
        .variant = 9u,
        .resolutionClass = 0x07800438u,
    };
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        taskIdentity,
        "Timing Feedback Calibration",
        graphicsRequest,
        scheduling,
        timingMetadata
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    auxiliaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[]{
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));

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
    const Graphics::GpuTaskTimingKey timingKey{
        .task = taskIdentity,
        .variant = timingMetadata.variant,
        .resolutionClass = timingMetadata.resolutionClass,
        .queue = Graphics::CommandQueue::Graphics,
    };

    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 0u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
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
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
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
    const Graphics::GpuTaskTimingAssignmentKey assignmentKey =
        Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(timingKey)
    ;
    EXPECT_EQ(timingHistory.findAssignment(assignmentKey), nullptr);
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 2u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
        .queueAssignments = &assignments,
        .compiledPlan = nullptr,
        .queueAssignmentTelemetry = nullptr,
    };
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));
    }
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes[0u].flags, Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue);

    // Replacing the probe history with a faster observation makes the first adaptive choice from the implicit
    // deterministic baseline. Neither calibration observation becomes a committed dwell incumbent.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(timingKey, auxiliaryGraphicsQueue.id, 0.001));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 3u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[0u].id);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    // An under-sampled probe remains bounded to its configured interval. Its first observation neither becomes
    // the incumbent nor causes the following non-calibration frame to retain that route.
    Graphics::GpuTaskTimingHistoryStore intervalTimingHistory(testArena.arena, 2u);
    intervalTimingHistory.resetForDeviceGeneration(queues[0u].id.deviceGeneration);
    ASSERT_TRUE(intervalTimingHistory.recordSample(timingKey, queues[0u].id, 0.010, 10u));
    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(timingKey, queues[0u].id, 0.010));
    Graphics::GpuTaskTimingHistorySnapshot intervalTimingSnapshot(testArena.arena);
    intervalTimingHistory.snapshot(intervalTimingSnapshot);
    Graphics::GpuTaskTimingFeedbackPolicy intervalTimingPolicy = timingPolicy;
    intervalTimingPolicy.minimumSampleCount = 2u;
    intervalTimingPolicy.calibrationIntervalFrames = 2u;
    Graphics::GpuTaskGraphQueueAssignmentOptions intervalAssignmentOptions;
    intervalAssignmentOptions.timingHistory = &intervalTimingSnapshot;
    intervalAssignmentOptions.timingFeedbackPolicy = intervalTimingPolicy;
    intervalAssignmentOptions.timingFrameIndex = 12u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(intervalTimingHistory.recordNonCommittingSample(
        timingKey,
        auxiliaryGraphicsQueue.id,
        0.020
    ));
    const Graphics::GpuTaskTimingAssignmentState* const intervalAssignmentState = intervalTimingHistory.findAssignment(
        assignmentKey
    );
    ASSERT_NE(intervalAssignmentState, nullptr);
    EXPECT_EQ(intervalAssignmentState->lastAcceptedQueue, queues[0u].id);
    EXPECT_EQ(intervalAssignmentState->lastAcceptedFrameIndex, 10u);
    EXPECT_EQ(intervalAssignmentState->lastSwitchFrameIndex, 10u);
    intervalTimingHistory.snapshot(intervalTimingSnapshot);
    intervalAssignmentOptions.timingFrameIndex = 13u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    intervalAssignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, intervalAssignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue.id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
}

TEST(GpuTaskGraph, RoutesOptInTimingFeedbackAcrossGraphicsAndComputeClasses){
    const auto runCase = [](const bool staticCompute){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);

        Graphics::GpuQueueRequest graphicsRequest;
        graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
        graphicsRequest.allowFallback = false;
        graphicsRequest.compilerMayOverridePreference = false;
        if(staticCompute){
            const Graphics::GpuTaskId independentGraphics = AddTaskWithQueue(
                graph,
                Name("tests/task_graph/cross_class_timing_independent_graphics"),
                "Cross Class Timing Independent Graphics",
                graphicsRequest
            );
            ASSERT_TRUE(independentGraphics.valid());
        }

        Graphics::GpuQueueRequest computeRequest;
        computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
        computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
        computeRequest.allowFallback = true;
        computeRequest.compilerMayOverridePreference = true;
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.allowSameClassQueueRouting = false;
        scheduling.allowCrossFamilySameClassQueueRouting = true;
        scheduling.allowTimingFeedbackRouting = true;
        scheduling.allowCrossClassTimingFeedbackRouting = true;
        const Name taskIdentity(
            staticCompute
                ? "tests/task_graph/cross_class_timing_compute_to_graphics"
                : "tests/task_graph/cross_class_timing_graphics_to_compute"
        );
        const Graphics::GpuTaskTimingMetadata timingMetadata{
            .variant = staticCompute ? 11u : 12u,
            .resolutionClass = 0x07800438u,
        };
        const Graphics::GpuTaskId task = AddTaskWithQueue(
            graph,
            taskIdentity,
            "Cross Class Timing Target",
            computeRequest,
            scheduling,
            timingMetadata
        );
        ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
        const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        const Graphics::GpuPhysicalQueueInfo& incumbentQueue = staticCompute ? queues[1u] : queues[0u];
        const Graphics::GpuPhysicalQueueInfo& candidateQueue = staticCompute ? queues[0u] : queues[1u];
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, incumbentQueue.id);

        const Graphics::GpuTaskTimingKey graphicsKey{
            .task = taskIdentity,
            .variant = timingMetadata.variant,
            .resolutionClass = timingMetadata.resolutionClass,
            .queue = Graphics::CommandQueue::Graphics,
        };
        const Graphics::GpuTaskTimingKey computeKey{
            .task = taskIdentity,
            .variant = timingMetadata.variant,
            .resolutionClass = timingMetadata.resolutionClass,
            .queue = Graphics::CommandQueue::Compute,
        };
        const Graphics::GpuTaskTimingKey& incumbentKey = staticCompute ? computeKey : graphicsKey;
        const Graphics::GpuTaskTimingKey& candidateKey = staticCompute ? graphicsKey : computeKey;
        Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena, 1u);
        ASSERT_TRUE(timingHistory.recordSample(candidateKey, candidateQueue.id, 0.001, 1u));
        ASSERT_TRUE(timingHistory.recordSample(incumbentKey, incumbentQueue.id, 0.010, 2u));

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
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        ASSERT_TRUE(timingHistory.noteAcceptedAssignment(candidateKey, candidateQueue.id, 10u));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(incumbentKey, incumbentQueue.id, 0.001));
        ASSERT_TRUE(timingHistory.recordNonCommittingSample(candidateKey, candidateQueue.id, 0.010));
        const Graphics::GpuTaskTimingAssignmentKey assignmentKey =
            Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(candidateKey)
        ;
        const Graphics::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(
            assignmentKey
        );
        ASSERT_NE(assignmentState, nullptr);
        EXPECT_EQ(assignmentState->lastAcceptedQueue, candidateQueue.id);
        EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 10u);
        timingHistory.snapshot(timingSnapshot);
        timingPolicy.minimumFramesBetweenSwitches = 10u;
        assignmentOptions.timingFeedbackPolicy = timingPolicy;
        assignmentOptions.timingFrameIndex = 12u;
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
        assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, incumbentQueue.id);
        EXPECT_EQ(assignment->queue, candidateQueue.id);
        EXPECT_EQ(assignment->queueClass, candidateQueue.queueClass);
        EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        assignmentOptions.timingFrameIndex = 20u;
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
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
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    const Graphics::GpuTaskId independentGraphics = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/cross_class_calibration_independent_graphics"),
        "Cross Class Calibration Independent Graphics",
        graphicsRequest
    );
    ASSERT_TRUE(independentGraphics.valid());

    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = true;
    computeRequest.compilerMayOverridePreference = true;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowCrossFamilySameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    scheduling.allowCrossClassTimingFeedbackRouting = true;
    const Name taskIdentity("tests/task_graph/cross_class_timing_calibration");
    const Graphics::GpuTaskTimingMetadata timingMetadata{
        .variant = 13u,
        .resolutionClass = 0x07800438u,
    };
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        taskIdentity,
        "Cross Class Timing Calibration",
        computeRequest,
        scheduling,
        timingMetadata
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
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
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    const Graphics::GpuTaskTimingKey graphicsKey{
        .task = taskIdentity,
        .variant = timingMetadata.variant,
        .resolutionClass = timingMetadata.resolutionClass,
        .queue = Graphics::CommandQueue::Graphics,
    };
    const Graphics::GpuTaskTimingKey computeKey{
        .task = taskIdentity,
        .variant = timingMetadata.variant,
        .resolutionClass = timingMetadata.resolutionClass,
        .queue = Graphics::CommandQueue::Compute,
    };
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 1u));
    timingHistory.snapshot(timingSnapshot);
    assignmentOptions.timingFrameIndex = 1u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);

    ASSERT_TRUE(timingHistory.recordNonCommittingSample(graphicsKey, queues[0u].id, 0.004));
    ASSERT_TRUE(timingHistory.recordSample(computeKey, queues[1u].id, 0.010, 3u));
    EXPECT_EQ(timingHistory.historyCount(), 2u);
    const Graphics::GpuTaskTimingAssignmentKey assignmentKey =
        Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(computeKey)
    ;
    const Graphics::GpuTaskTimingAssignmentState* const assignmentState = timingHistory.findAssignment(assignmentKey);
    ASSERT_NE(assignmentState, nullptr);
    EXPECT_EQ(assignmentState->lastAcceptedQueue, queues[1u].id);
    EXPECT_EQ(assignmentState->lastAcceptedFrameIndex, 3u);
    EXPECT_EQ(assignmentState->lastSwitchFrameIndex, 1u);
    timingHistory.snapshot(timingSnapshot);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.007;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 20u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    timingPolicy.minimumAbsoluteBenefitSeconds = 0.001;
    timingPolicy.minimumFramesBetweenSwitches = 10u;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingFrameIndex = 5u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, queues[1u].id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    assignmentOptions.timingFrameIndex = 13u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queues[1u].id);
    EXPECT_EQ(assignment->queue, queues[0u].id);
    EXPECT_TRUE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, IgnoresTimingFeedbackWithoutAnyEnabledRoute){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowTimingFeedbackRouting = true;
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/timing_feedback_without_enabled_route"),
        "Timing Feedback Without Enabled Route",
        graphicsRequest,
        scheduling
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
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
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, queue.id);
    EXPECT_EQ(assignment->queue, queue.id);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingCalibration);
    EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);
}

TEST(GpuTaskGraph, RejectsCrossClassTimingRoutesWithoutEveryRequiredOptIn){
    const auto runCase = [](
        const bool allowCrossClassRouting,
        const bool allowCrossFamilyRouting,
        const bool flexibleQueueRequest,
        const bool requireGraphicsCapability
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);

        Graphics::GpuQueueRequest graphicsRequest;
        graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
        graphicsRequest.allowFallback = false;
        graphicsRequest.compilerMayOverridePreference = false;
        const Graphics::GpuTaskId independentGraphics = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/rejected_cross_class_independent_graphics"),
            "Rejected Cross Class Independent Graphics",
            graphicsRequest
        );
        ASSERT_TRUE(independentGraphics.valid());

        Graphics::GpuQueueRequest queueRequest;
        queueRequest.requiredCapabilities = requireGraphicsCapability
            ? Graphics::GpuQueueCapability::Graphics
            : Graphics::GpuQueueCapability::Compute
        ;
        queueRequest.preferredQueue = requireGraphicsCapability
            ? Graphics::GpuQueuePreference::Graphics
            : Graphics::GpuQueuePreference::Compute
        ;
        queueRequest.allowFallback = flexibleQueueRequest;
        queueRequest.compilerMayOverridePreference = flexibleQueueRequest;
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
        else if(!flexibleQueueRequest)
            taskIdentity = Name("tests/task_graph/rejected_strict_cross_class_timing");
        const Graphics::GpuTaskId task = AddTaskWithQueue(
            graph,
            taskIdentity,
            "Rejected Cross Class Timing",
            queueRequest,
            scheduling
        );
        ASSERT_TRUE(task.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        const Graphics::GpuTaskTimingKey graphicsKey{
            .task = taskIdentity,
            .queue = Graphics::CommandQueue::Graphics,
        };
        const Graphics::GpuTaskTimingKey computeKey{
            .task = taskIdentity,
            .queue = Graphics::CommandQueue::Compute,
        };
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
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->initialQueue, queues[incumbentQueueIndex].id);
        EXPECT_EQ(assignment->queue, queues[incumbentQueueIndex].id);
        EXPECT_FALSE(assignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

        const Graphics::GpuTaskTimingQueueOverride forcedOverride{
            .key = incumbentKey,
            .queue = queues[candidateQueueIndex].id,
        };
        assignmentOptions.timingHistory = nullptr;
        assignmentOptions.timingFeedbackPolicy = {};
        assignmentOptions.timingQueueOverrides = &forcedOverride;
        assignmentOptions.timingQueueOverrideCount = 1u;
        EXPECT_FALSE(Assign(graph, analysis, topology, assignments, assignmentOptions));
        EXPECT_EQ(
            assignments.diagnostic().status,
            Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback
        );
        EXPECT_EQ(assignments.diagnostic().task, task);
        EXPECT_FALSE(assignments.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_FALSE(assignments.validFor(declarations));
        }
        EXPECT_EQ(assignments.find(task), nullptr);
    };

    runCase(false, true, true, false);
    runCase(true, false, true, false);
    runCase(true, true, false, false);
    runCase(true, true, true, true);
}

TEST(GpuTaskGraph, QueueTimingScoreUsesOnlyReducedIncomingDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId handoff = AddHazardDomain(
        graph,
        Name("tests/task_graph/reduced_timing_score_handoff"),
        "Reduced Timing Score Handoff"
    );
    ASSERT_TRUE(handoff.valid());

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;

    const Name firstIdentity("tests/task_graph/reduced_timing_score_first");
    const Graphics::GpuTaskResourceUse firstUse{
        .resource = handoff,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(firstIdentity)
        .setMarkerLabel("Reduced Timing Score First")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(&firstUse, 1u)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    const Name secondIdentity("tests/task_graph/reduced_timing_score_second");
    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(secondIdentity)
        .setMarkerLabel("Reduced Timing Score Second")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId second = graph.addTask(secondDesc);
    ASSERT_TRUE(second.valid());

    const Name thirdIdentity("tests/task_graph/reduced_timing_score_third");
    const Graphics::GpuTaskResourceUse thirdUse{
        .resource = handoff,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc thirdDesc;
    thirdDesc
        .setIdentity(thirdIdentity)
        .setMarkerLabel("Reduced Timing Score Third")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&second, 1u)
        .setResourceUses(&thirdUse, 1u)
    ;
    const Graphics::GpuTaskId third = graph.addTask(thirdDesc);
    ASSERT_TRUE(third.valid());

    Graphics::GpuPhysicalQueueInfo firstAuxiliaryQueue = GraphicsQueue(1u);
    firstAuxiliaryQueue.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliaryQueue = GraphicsQueue(2u);
    secondAuxiliaryQueue.queueIndex = 2u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        firstAuxiliaryQueue,
        secondAuxiliaryQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    const Graphics::GpuTaskTimingKey thirdTimingKey{
        .task = thirdIdentity,
        .queue = Graphics::CommandQueue::Graphics,
    };
    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    for(u64 frameIndex = 1u; frameIndex <= 4u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(thirdTimingKey, firstAuxiliaryQueue.id, 0.001, frameIndex));
    for(u64 frameIndex = 5u; frameIndex <= 8u; ++frameIndex)
        ASSERT_TRUE(timingHistory.recordSample(thirdTimingKey, secondAuxiliaryQueue.id, 0.001, frameIndex));
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
    const Graphics::GpuTaskTimingQueueOverride timingOverrides[] = {
        Graphics::GpuTaskTimingQueueOverride{
            .key = Graphics::GpuTaskTimingKey{
                .task = firstIdentity,
                .queue = Graphics::CommandQueue::Graphics,
            },
            .queue = firstAuxiliaryQueue.id,
        },
        Graphics::GpuTaskTimingQueueOverride{
            .key = Graphics::GpuTaskTimingKey{
                .task = secondIdentity,
                .queue = Graphics::CommandQueue::Graphics,
            },
            .queue = secondAuxiliaryQueue.id,
        },
    };
    Graphics::GpuTaskGraphQueueAssignmentOptions options;
    options.timingHistory = &timingSnapshot;
    options.timingFeedbackPolicy = timingPolicy;
    options.timingQueueOverrides = timingOverrides;
    options.timingQueueOverrideCount = LengthOf(timingOverrides);
    options.timingFrameIndex = 12u;

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, options));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = assignments.find(first);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = assignments.find(second);
    const Graphics::GpuTaskQueueAssignment* const thirdAssignment = assignments.find(third);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    ASSERT_NE(thirdAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, firstAuxiliaryQueue.id);
    EXPECT_EQ(secondAssignment->queue, secondAuxiliaryQueue.id);
    // Equal timing and queue load leave incoming crossings decisive. Only the reduced B -> C edge participates, so
    // C stays with B instead of tying on the redundant A -> C diagnostic and falling back to the lower queue ID.
    EXPECT_EQ(thirdAssignment->queue, secondAuxiliaryQueue.id);
}

TEST(GpuTaskGraph, RanksEqualTimingRoutesDeterministicallyAndValidatesForcedQueueRoutes){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint preloadScheduling;
    preloadScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preloadScheduling.allowSameClassQueueRouting = true;
    preloadScheduling.preferNonPrimarySameClassQueue = true;
    const Graphics::GpuTaskId preload = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/timing_feedback_preload"),
        "Timing Feedback Preload",
        graphicsRequest,
        preloadScheduling
    );
    ASSERT_TRUE(preload.valid());

    Graphics::GpuTaskSchedulingHint targetScheduling;
    targetScheduling.allowSameClassQueueRouting = true;
    targetScheduling.allowTimingFeedbackRouting = true;
    const Name targetIdentity("tests/task_graph/timing_feedback_equal_candidates");
    const Graphics::GpuTaskId target = AddTaskWithQueue(
        graph,
        targetIdentity,
        "Timing Feedback Equal Candidates",
        graphicsRequest,
        targetScheduling
    );
    ASSERT_TRUE(target.valid());

    Graphics::GpuPhysicalQueueInfo firstAuxiliaryGraphicsQueue = GraphicsQueue(1u);
    firstAuxiliaryGraphicsQueue.queueIndex = 1u;
    Graphics::GpuPhysicalQueueInfo secondAuxiliaryGraphicsQueue = GraphicsQueue(2u);
    secondAuxiliaryGraphicsQueue.queueIndex = 2u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        firstAuxiliaryGraphicsQueue,
        secondAuxiliaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));

    Graphics::GpuTaskTimingHistoryStore timingHistory(testArena.arena);
    const Graphics::GpuTaskTimingKey targetTimingKey{
        .task = targetIdentity,
        .queue = Graphics::CommandQueue::Graphics,
    };
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
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const preloadAssignment = assignments.find(preload);
    const Graphics::GpuTaskQueueAssignment* const targetAssignment = assignments.find(target);
    ASSERT_NE(preloadAssignment, nullptr);
    ASSERT_NE(targetAssignment, nullptr);
    EXPECT_EQ(preloadAssignment->queue, firstAuxiliaryGraphicsQueue.id);
    // Both auxiliary histories are equally fast. The first already owns the expensive preload, so the score must
    // choose the second route instead of depending on topology order alone.
    EXPECT_EQ(targetAssignment->queue, secondAuxiliaryGraphicsQueue.id);

    const Graphics::GpuTaskTimingQueueOverride forcedOverride[]{
        Graphics::GpuTaskTimingQueueOverride{
            .key = targetTimingKey,
            .queue = firstAuxiliaryGraphicsQueue.id,
        },
    };
    assignmentOptions.timingHistory = nullptr;
    assignmentOptions.timingFeedbackPolicy = {};
    assignmentOptions.timingQueueOverrides = forcedOverride;
    assignmentOptions.timingQueueOverrideCount = LengthOf(forcedOverride);
    Graphics::GpuTaskGraphCompileOptions forcedCompileOptions;
    forcedCompileOptions.queueAssignmentOptions = assignmentOptions;
    Graphics::GpuCompiledGraph forcedCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, forcedCompiledGraph, forcedCompileOptions));
    const Graphics::GpuTaskQueueAssignment* const forcedAssignment = assignments.find(target);
    ASSERT_NE(forcedAssignment, nullptr);
    EXPECT_EQ(forcedAssignment->queue, firstAuxiliaryGraphicsQueue.id);
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(forcedCompiledGraph);
        const Graphics::GpuCompiledTask* const forcedCompiledTask = compiledPlan.findTask(target).plan;

        ASSERT_NE(forcedCompiledTask, nullptr);
        EXPECT_TRUE(forcedCompiledTask->recordsNonCommittingTimingSample);
    }

    // Debug forcing collects a route observation without replacing the committed policy incumbent. Once the
    // override disappears, ordinary dwell continues from the last adaptive/static assignment.
    ASSERT_TRUE(timingHistory.recordNonCommittingSample(targetTimingKey, firstAuxiliaryGraphicsQueue.id, 0.020));
    const Graphics::GpuTaskTimingAssignmentKey targetAssignmentKey =
        Graphics::GpuTaskTimingAssignmentKeyFromHistoryKey(targetTimingKey)
    ;
    const Graphics::GpuTaskTimingAssignmentState* const targetAssignmentState = timingHistory.findAssignment(
        targetAssignmentKey
    );
    ASSERT_NE(targetAssignmentState, nullptr);
    EXPECT_EQ(targetAssignmentState->lastAcceptedQueue, queues[0u].id);
    EXPECT_EQ(targetAssignmentState->lastSwitchFrameIndex, 9u);
    timingHistory.snapshot(timingSnapshot);
    timingPolicy.minimumFramesBetweenSwitches = 30u;
    assignmentOptions.timingHistory = &timingSnapshot;
    assignmentOptions.timingFeedbackPolicy = timingPolicy;
    assignmentOptions.timingQueueOverrides = nullptr;
    assignmentOptions.timingQueueOverrideCount = 0u;
    assignmentOptions.timingFrameIndex = 14u;
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments, assignmentOptions));
    const Graphics::GpuTaskQueueAssignment* const restoredAssignment = assignments.find(target);
    ASSERT_NE(restoredAssignment, nullptr);
    EXPECT_EQ(restoredAssignment->queue, queues[0u].id);
    EXPECT_FALSE(restoredAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DebugTimingOverride);
    EXPECT_FALSE(restoredAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::TimingFeedback);

    Graphics::GpuPhysicalQueueInfo crossFamilyGraphicsQueue = GraphicsQueue(1u);
    crossFamilyGraphicsQueue.familyIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo crossFamilyQueues[] = {
        GraphicsQueue(),
        crossFamilyGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology crossFamilyTopology{
        .queues = crossFamilyQueues,
        .queueCount = LengthOf(crossFamilyQueues),
    };
    const Graphics::GpuTaskTimingQueueOverride invalidForcedOverride[]{
        Graphics::GpuTaskTimingQueueOverride{
            .key = targetTimingKey,
            .queue = crossFamilyGraphicsQueue.id,
        },
    };
    assignmentOptions.timingQueueOverrides = invalidForcedOverride;
    assignmentOptions.timingQueueOverrideCount = LengthOf(invalidForcedOverride);
    EXPECT_FALSE(Assign(graph, analysis, crossFamilyTopology, assignments, assignmentOptions));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback
    );
    EXPECT_EQ(assignments.diagnostic().task, target);

    // Cross-family timing routes remain rejected without the task's separate cross-family scheduling opt-in.
    Graphics::GpuTaskGraph crossFamilyGraph(testArena.arena);
    Graphics::GpuTaskSchedulingHint crossFamilyScheduling;
    crossFamilyScheduling.allowSameClassQueueRouting = true;
    crossFamilyScheduling.allowCrossFamilySameClassQueueRouting = true;
    crossFamilyScheduling.allowTimingFeedbackRouting = true;
    const Name crossFamilyIdentity("tests/task_graph/timing_feedback_cross_family_override");
    const Graphics::GpuTaskId crossFamilyTask = AddTaskWithQueue(
        crossFamilyGraph,
        crossFamilyIdentity,
        "Timing Feedback Cross Family Override",
        graphicsRequest,
        crossFamilyScheduling
    );
    ASSERT_TRUE(crossFamilyTask.valid());
    Graphics::GpuTaskGraphAnalysis crossFamilyAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(crossFamilyGraph, crossFamilyAnalysis));
    const Graphics::GpuTaskTimingQueueOverride crossFamilyOverride[]{
        Graphics::GpuTaskTimingQueueOverride{
            .key = Graphics::GpuTaskTimingKey{
                .task = crossFamilyIdentity,
                .queue = Graphics::CommandQueue::Graphics,
            },
            .queue = crossFamilyGraphicsQueue.id,
        },
    };
    Graphics::GpuTaskGraphQueueAssignmentOptions crossFamilyOptions;
    crossFamilyOptions.timingQueueOverrides = crossFamilyOverride;
    crossFamilyOptions.timingQueueOverrideCount = LengthOf(crossFamilyOverride);
    Graphics::GpuTaskGraphQueueAssignments crossFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(
        crossFamilyGraph,
        crossFamilyAnalysis,
        crossFamilyTopology,
        crossFamilyAssignments,
        crossFamilyOptions
    ));
    const Graphics::GpuTaskQueueAssignment* const crossFamilyAssignment = crossFamilyAssignments.find(crossFamilyTask);
    ASSERT_NE(crossFamilyAssignment, nullptr);
    EXPECT_EQ(crossFamilyAssignment->queue, crossFamilyGraphicsQueue.id);
    EXPECT_EQ(crossFamilyAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_TRUE(crossFamilyAssignment->modifiers & Graphics::GpuTaskQueueAssignmentModifier::DebugTimingOverride);
}

TEST(GpuTaskGraph, RoutesOptedInCrossFamilyTimingFeedbackWithExclusiveOwnershipHandoffs){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/timing_feedback_cross_family_buffer"),
        "Timing Feedback Cross Family Buffer"
    );
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/timing_feedback_cross_family_producer"))
        .setMarkerLabel("Timing Feedback Cross Family Producer")
        .setQueue(graphicsRequest)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowSameClassQueueRouting = true;
    consumerScheduling.preserveSameClassQueueWithDirectDependency = true;
    consumerScheduling.allowCrossFamilySameClassQueueRouting = true;
    consumerScheduling.allowTimingFeedbackRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Name consumerIdentity("tests/task_graph/timing_feedback_cross_family_consumer");
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(consumerIdentity)
        .setMarkerLabel("Timing Feedback Cross Family Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphicsQueue = GraphicsQueue(1u);
    auxiliaryGraphicsQueue.familyIndex = 3u;
    const Graphics::GpuPhysicalQueueInfo queues[]{
        GraphicsQueue(),
        auxiliaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    const Graphics::GpuTaskTimingKey consumerTimingKey{
        .task = consumerIdentity,
        .queue = Graphics::CommandQueue::Graphics,
    };
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
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
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
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 2u);

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

TEST(GpuTaskGraph, CompilesHierarchicalGraphOwnedTimingPolicies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskTimingMetadata packetTiming;
    packetTiming.policy = Graphics::GpuTaskTimingPolicy::PacketOnly;
    Graphics::GpuTaskTimingMetadata taskTiming;
    taskTiming.policy = Graphics::GpuTaskTimingPolicy::Task;

    const Name untimedIdentity("tests/task_graph/timing_policy_untimed");
    const Graphics::GpuTaskId untimed = AddTaskWithQueue(
        graph,
        untimedIdentity,
        "Timing Policy Untimed",
        graphicsRequest
    );
    ASSERT_TRUE(untimed.valid());

    const Name packetOnlyIdentity("tests/task_graph/timing_policy_packet_only");
    const Graphics::GpuTaskId packetOnly = AddTaskWithQueue(
        graph,
        packetOnlyIdentity,
        "Timing Policy Packet Only",
        graphicsRequest,
        {},
        packetTiming,
        &untimed,
        1u
    );
    ASSERT_TRUE(packetOnly.valid());

    Graphics::GpuTaskSchedulingHint mergedTaskScheduling;
    mergedTaskScheduling.mergeWithPrevious = true;
    const Name taskIdentity("tests/task_graph/timing_policy_task");
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        taskIdentity,
        "Timing Policy Task",
        graphicsRequest,
        mergedTaskScheduling,
        taskTiming,
        &packetOnly,
        1u
    );
    ASSERT_TRUE(task.valid());

    const Name finalUntimedIdentity("tests/task_graph/timing_policy_final_untimed");
    const Graphics::GpuTaskId finalUntimed = AddTaskWithQueue(
        graph,
        finalUntimedIdentity,
        "Timing Policy Final Untimed",
        graphicsRequest,
        {},
        {},
        &task,
        1u
    );
    ASSERT_TRUE(finalUntimed.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId untimedPacket = compiledPlan.packetForTask(untimed);
    const Graphics::GpuSubmissionPacketId timedPacket = compiledPlan.packetForTask(packetOnly);
    const Graphics::GpuSubmissionPacketId taskPacket = compiledPlan.packetForTask(task);
    const Graphics::GpuSubmissionPacketId finalUntimedPacket = compiledPlan.packetForTask(finalUntimed);
    ASSERT_TRUE(untimedPacket.valid());
    ASSERT_TRUE(timedPacket.valid());
    ASSERT_TRUE(finalUntimedPacket.valid());
    EXPECT_NE(untimedPacket, timedPacket);
    EXPECT_EQ(timedPacket, taskPacket);
    EXPECT_NE(timedPacket, finalUntimedPacket);
    EXPECT_FALSE(compiledPlan.packet(untimedPacket).plan->recordsTiming);
    EXPECT_TRUE(compiledPlan.packet(timedPacket).plan->recordsTiming);
    EXPECT_FALSE(compiledPlan.packet(finalUntimedPacket).plan->recordsTiming);

    const Graphics::GpuCompiledTask* const compiledUntimed = compiledPlan.findTask(untimed).plan;
    const Graphics::GpuCompiledTask* const compiledPacketOnly = compiledPlan.findTask(packetOnly).plan;
    const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
    const Graphics::GpuCompiledTask* const compiledFinalUntimed = compiledPlan.findTask(finalUntimed).plan;
    ASSERT_NE(compiledUntimed, nullptr);
    ASSERT_NE(compiledPacketOnly, nullptr);
    ASSERT_NE(compiledTask, nullptr);
    ASSERT_NE(compiledFinalUntimed, nullptr);
    EXPECT_EQ(compiledUntimed->timingPolicy, Graphics::GpuTaskTimingPolicy::None);
    EXPECT_EQ(compiledPacketOnly->timingPolicy, Graphics::GpuTaskTimingPolicy::PacketOnly);
    EXPECT_EQ(compiledTask->timingPolicy, Graphics::GpuTaskTimingPolicy::Task);
    EXPECT_EQ(compiledFinalUntimed->timingPolicy, Graphics::GpuTaskTimingPolicy::None);

    const Name packetScope = Graphics::GpuTaskPacketTimingScopeName(packetOnlyIdentity);
    EXPECT_TRUE(packetScope);
    EXPECT_NE(packetScope, packetOnlyIdentity);
    EXPECT_NE(packetScope, taskIdentity);
    EXPECT_EQ(packetScope, Graphics::GpuTaskPacketTimingScopeName(packetOnlyIdentity));
}

TEST(GpuTaskGraph, RejectsInvalidGraphOwnedTimingPolicy){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskTimingMetadata invalidTiming;
    invalidTiming.policy = static_cast<Graphics::GpuTaskTimingPolicy::Enum>(
        Graphics::GpuTaskTimingPolicy::kCount
    );
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/timing_policy_invalid"),
        "Timing Policy Invalid",
        {},
        {},
        invalidTiming
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

