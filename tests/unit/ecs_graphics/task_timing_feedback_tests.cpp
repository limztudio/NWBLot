// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include <impl/ecs_render/kernel/task_timing_feedback.h>

#include <core/perf/timing.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_timing_feedback_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskTimingFeedbackTestsTag>;


[[nodiscard]] static Core::QueueSubmissionToken AcceptedToken(
    const Core::GpuTaskTimingKey& key,
    const Core::GpuPhysicalQueueId& queue
){
    return Core::QueueSubmissionToken{
        .queue = key.queue,
        .value = 1u,
        .physicalQueueIndex = queue.index,
        .deviceGeneration = queue.deviceGeneration,
    };
}

[[nodiscard]] static Core::GpuTimingSample PublishedSample(
    const Core::GpuTimingSampleAttribution attribution,
    const Name& scopeName,
    const Core::GpuPhysicalQueueId& queue,
    const u64 sourceFrameIndex,
    const f64 durationSeconds
){
    return Core::GpuTimingSample{
        .scopeName = scopeName,
        .sourceFrameIndex = sourceFrameIndex,
        .durationSeconds = durationSeconds,
        .physicalQueue = queue,
        .attribution = attribution,
        .published = true,
        .comparableRange = {},
    };
}

[[nodiscard]] static Core::GpuTimingSample RetiredSample(
    const Core::GpuTimingSampleAttribution attribution,
    const Core::GpuPhysicalQueueId& queue
){
    return Core::GpuTimingSample{
        .scopeName = NAME_NONE,
        .sourceFrameIndex = 0u,
        .durationSeconds = 0.0,
        .physicalQueue = queue,
        .attribution = attribution,
        .published = false,
        .comparableRange = {},
    };
}

static void ExpectArenaMemoryUnchanged(const ArenaMemoryStats& before, const ArenaMemoryStats& after){
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
}

static void ExpectPolicyEqual(
    const Core::GpuTaskTimingFeedbackPolicy& actual,
    const Core::GpuTaskTimingFeedbackPolicy& expected
){
    EXPECT_EQ(actual.enabled, expected.enabled);
    EXPECT_EQ(actual.minimumSampleCount, expected.minimumSampleCount);
    EXPECT_EQ(actual.calibrationIntervalFrames, expected.calibrationIntervalFrames);
    EXPECT_EQ(actual.minimumAbsoluteBenefitSeconds, expected.minimumAbsoluteBenefitSeconds);
    EXPECT_EQ(actual.minimumRelativeBenefit, expected.minimumRelativeBenefit);
    EXPECT_EQ(actual.minimumFramesBetweenSwitches, expected.minimumFramesBetweenSwitches);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RendererTaskTimingFeedbackState, PublishedSampleBeforeAcceptanceDrainsAssignmentAndDurationWithoutAllocation){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.sample_first");
    const Core::GpuPhysicalQueueId queue{ .index = 2u, .deviceGeneration = 7u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.sample_first.task"),
        .variant = 3u,
        .resolutionClass = 4u,
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, queue, 42u, false));
    state.completeSample(PublishedSample(attribution, scopeName, queue, 42u, 0.004), true);

    const ArenaMemoryStats beforeAcceptance = testArena.arena.memoryStats();
    state.acceptSubmission(attribution, AcceptedToken(key, queue), true);
    const ArenaMemoryStats afterAcceptance = testArena.arena.memoryStats();
    ExpectArenaMemoryUnchanged(beforeAcceptance, afterAcceptance);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 1u);
    EXPECT_EQ(drain.recordedSampleCount, 1u);
    EXPECT_EQ(drain.retiredSampleCount, 1u);
    EXPECT_EQ(drain.rejectedAssignmentCount, 0u);
    EXPECT_EQ(drain.rejectedSampleCount, 0u);

    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    const Core::GpuTaskTimingAssignmentState* const assignment = snapshot.findAssignment(
        Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)
    );
    const Core::GpuTaskTimingHistory* const timing = snapshot.find(key, queue);
    ASSERT_NE(assignment, nullptr);
    ASSERT_NE(timing, nullptr);
    EXPECT_EQ(assignment->lastAcceptedQueue, queue);
    EXPECT_EQ(assignment->lastAcceptedFrameIndex, 42u);
    EXPECT_EQ(timing->sampleCount, 1u);
}

TEST(RendererTaskTimingFeedbackState, PolicyTransitionHelpersPreserveEntirePolicyAcrossCollectionUpdates){
    const Core::GpuTaskTimingFeedbackPolicy originalPolicy{
        .enabled = false,
        .minimumSampleCount = 3u,
        .calibrationIntervalFrames = 5u,
        .minimumAbsoluteBenefitSeconds = 0.001,
        .minimumRelativeBenefit = 0.2,
        .minimumFramesBetweenSwitches = 11u,
    };
    const Core::GpuTaskTimingFeedbackPolicy enabledPolicy{
        .enabled = true,
        .minimumSampleCount = 7u,
        .calibrationIntervalFrames = 9u,
        .minimumAbsoluteBenefitSeconds = 0.002,
        .minimumRelativeBenefit = 0.3,
        .minimumFramesBetweenSwitches = 13u,
    };
    const Core::GpuTaskTimingFeedbackPolicy disabledPolicy{
        .enabled = false,
        .minimumSampleCount = 17u,
        .calibrationIntervalFrames = 19u,
        .minimumAbsoluteBenefitSeconds = 0.004,
        .minimumRelativeBenefit = 0.4,
        .minimumFramesBetweenSwitches = 23u,
    };
    Core::GpuTaskTimingFeedbackPolicy currentPolicy = originalPolicy;

    const Impl::RendererTaskTimingFeedbackPolicyTransition enableTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, enabledPolicy, true);
    EXPECT_EQ(enableTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::Enable);
    ExpectPolicyEqual(currentPolicy, originalPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, enableTransition, true);
    ExpectPolicyEqual(currentPolicy, enabledPolicy);

    const Impl::RendererTaskTimingFeedbackPolicyTransition disableTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, disabledPolicy, true);
    EXPECT_EQ(disableTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::Disable);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, disableTransition, true);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);

    const Impl::RendererTaskTimingFeedbackPolicyTransition failedEnableTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, enabledPolicy, true);
    EXPECT_EQ(failedEnableTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::Enable);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, failedEnableTransition, false);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);

    currentPolicy = enabledPolicy;
    const Impl::RendererTaskTimingFeedbackPolicyTransition failedDisableTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, disabledPolicy, true);
    EXPECT_EQ(failedDisableTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::Disable);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, failedDisableTransition, false);
    ExpectPolicyEqual(currentPolicy, enabledPolicy);

    currentPolicy = originalPolicy;
    const Impl::RendererTaskTimingFeedbackPolicyTransition sameStateTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, disabledPolicy, true);
    EXPECT_EQ(sameStateTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::None);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, sameStateTransition, false);
    ExpectPolicyEqual(currentPolicy, disabledPolicy);

    currentPolicy = enabledPolicy;
    const Impl::RendererTaskTimingFeedbackPolicyTransition inactiveRendererTransition =
        Impl::PrepareRendererTaskTimingFeedbackPolicyTransition(currentPolicy, originalPolicy, false);
    EXPECT_EQ(inactiveRendererTransition.action, Impl::RendererTaskTimingFeedbackCollectionAction::None);
    ExpectPolicyEqual(currentPolicy, originalPolicy);
    Impl::ResolveRendererTaskTimingFeedbackPolicyTransition(currentPolicy, inactiveRendererTransition, false);
    ExpectPolicyEqual(currentPolicy, originalPolicy);
}

TEST(RendererTaskTimingFeedbackState, RejectedAndDiscardedCallbacksDoNotAllocate){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    const Name scopeName("tests.renderer_timing.callback_allocation");
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 2u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.callback_allocation.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution rejectedAttribution = timingRecorder.allocateSampleAttribution();
    const Core::GpuTimingSampleAttribution discardedAttribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(rejectedAttribution, scopeName, key, queue, 1u, false));
    ASSERT_TRUE(state.trackSample(discardedAttribution, scopeName, key, queue, 2u, false));

    const ArenaMemoryStats beforeRejection = testArena.arena.memoryStats();
    state.acceptSubmission(rejectedAttribution, {}, true);
    const ArenaMemoryStats afterRejection = testArena.arena.memoryStats();
    ExpectArenaMemoryUnchanged(beforeRejection, afterRejection);

    const ArenaMemoryStats beforeDiscard = testArena.arena.memoryStats();
    state.discardRecording(discardedAttribution);
    const ArenaMemoryStats afterDiscard = testArena.arena.memoryStats();
    ExpectArenaMemoryUnchanged(beforeDiscard, afterDiscard);
}

TEST(RendererTaskTimingFeedbackState, AcceptancePublishesAssignmentBeforeLateDurationWithoutDoubleCommit){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.accept_first");
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 5u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.accept_first.task"),
        .variant = 1u,
        .resolutionClass = 2u,
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, queue, 18u, false));

    const ArenaMemoryStats beforeAcceptance = testArena.arena.memoryStats();
    state.acceptSubmission(attribution, AcceptedToken(key, queue), true);
    const ArenaMemoryStats afterAcceptance = testArena.arena.memoryStats();
    ExpectArenaMemoryUnchanged(beforeAcceptance, afterAcceptance);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult assignmentDrain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(assignmentDrain.acceptedAssignmentCount, 1u);
    EXPECT_EQ(assignmentDrain.recordedSampleCount, 0u);
    EXPECT_EQ(assignmentDrain.retiredSampleCount, 0u);
    history.snapshot(snapshot);
    ASSERT_NE(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)), nullptr);
    EXPECT_EQ(snapshot.find(key, queue), nullptr);

    const ArenaMemoryStats beforeCompletion = testArena.arena.memoryStats();
    state.completeSample(PublishedSample(attribution, scopeName, queue, 18u, 0.006), true);
    const ArenaMemoryStats afterCompletion = testArena.arena.memoryStats();
    ExpectArenaMemoryUnchanged(beforeCompletion, afterCompletion);

    const Impl::RendererTaskTimingFeedbackDrainResult sampleDrain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(sampleDrain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(sampleDrain.recordedSampleCount, 1u);
    EXPECT_EQ(sampleDrain.retiredSampleCount, 1u);
    history.snapshot(snapshot);
    const Core::GpuTaskTimingAssignmentState* const assignment = snapshot.findAssignment(
        Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)
    );
    const Core::GpuTaskTimingHistory* const timing = snapshot.find(key, queue);
    ASSERT_NE(assignment, nullptr);
    ASSERT_NE(timing, nullptr);
    EXPECT_EQ(assignment->lastAcceptedFrameIndex, 18u);
    EXPECT_EQ(assignment->lastSwitchFrameIndex, 18u);
    EXPECT_EQ(timing->sampleCount, 1u);
}

TEST(RendererTaskTimingFeedbackState, UnpublishedSamplesRetireInEitherRaceOrderWithoutLosingAssignments){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Core::GpuPhysicalQueueId queue{ .index = 0u, .deviceGeneration = 3u };
    const Name firstScope("tests.renderer_timing.unpublished_first");
    const Name secondScope("tests.renderer_timing.unpublished_second");
    const Core::GpuTaskTimingKey firstKey{
        .task = Name("tests.renderer_timing.unpublished_first.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTaskTimingKey secondKey{
        .task = Name("tests.renderer_timing.unpublished_second.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution firstAttribution = timingRecorder.allocateSampleAttribution();
    const Core::GpuTimingSampleAttribution secondAttribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(firstAttribution, firstScope, firstKey, queue, 10u, false));
    ASSERT_TRUE(state.trackSample(secondAttribution, secondScope, secondKey, queue, 11u, false));

    state.completeSample(RetiredSample(firstAttribution, queue), true);
    state.acceptSubmission(firstAttribution, AcceptedToken(firstKey, queue), true);
    state.acceptSubmission(secondAttribution, AcceptedToken(secondKey, queue), true);
    state.completeSample(RetiredSample(secondAttribution, queue), true);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 2u);
    EXPECT_EQ(drain.recordedSampleCount, 0u);
    EXPECT_EQ(drain.retiredSampleCount, 2u);
    history.snapshot(snapshot);
    EXPECT_NE(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(firstKey)), nullptr);
    EXPECT_NE(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(secondKey)), nullptr);
    EXPECT_EQ(snapshot.find(firstKey, queue), nullptr);
    EXPECT_EQ(snapshot.find(secondKey, queue), nullptr);
}

TEST(RendererTaskTimingFeedbackState, InvalidPublishedMetadataIsTerminalButKeepsAcceptedAssignment){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.invalid_metadata");
    const Core::GpuPhysicalQueueId queue{ .index = 3u, .deviceGeneration = 9u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.invalid_metadata.task"),
        .queue = Core::CommandQueue::Graphics,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, queue, 70u, false));
    state.completeSample(PublishedSample(attribution, Name("tests.renderer_timing.wrong_scope"), queue, 70u, 0.002), true);
    state.acceptSubmission(attribution, AcceptedToken(key, queue), true);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 1u);
    EXPECT_EQ(drain.recordedSampleCount, 0u);
    EXPECT_EQ(drain.retiredSampleCount, 1u);
    history.snapshot(snapshot);
    EXPECT_NE(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)), nullptr);
    EXPECT_EQ(snapshot.find(key, queue), nullptr);

    const Impl::RendererTaskTimingFeedbackDrainResult repeatedDrain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(repeatedDrain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(repeatedDrain.recordedSampleCount, 0u);
    EXPECT_EQ(repeatedDrain.retiredSampleCount, 0u);
}

TEST(RendererTaskTimingFeedbackState, NonFiniteDurationIsTerminalButKeepsAcceptedAssignment){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.non_finite_duration");
    const Core::GpuPhysicalQueueId queue{ .index = 2u, .deviceGeneration = 10u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.non_finite_duration.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, queue, 80u, false));
    state.completeSample(PublishedSample(attribution, scopeName, queue, 80u, Limit<f64>::s_QuietNaN), true);
    state.acceptSubmission(attribution, AcceptedToken(key, queue), true);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 1u);
    EXPECT_EQ(drain.recordedSampleCount, 0u);
    EXPECT_EQ(drain.retiredSampleCount, 1u);
    history.snapshot(snapshot);
    EXPECT_NE(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)), nullptr);
    EXPECT_EQ(snapshot.find(key, queue), nullptr);
}

TEST(RendererTaskTimingFeedbackState, NonCommittingSampleRecordsDurationWithoutAssignment){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.calibration");
    const Core::GpuPhysicalQueueId queue{ .index = 1u, .deviceGeneration = 4u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.calibration.task"),
        .variant = 8u,
        .queue = Core::CommandQueue::Graphics,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, queue, 25u, true));
    state.completeSample(PublishedSample(attribution, scopeName, queue, 25u, 0.003), true);
    state.acceptSubmission(attribution, AcceptedToken(key, queue), true);

    history.resetForDeviceGeneration(queue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, queue.deviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(drain.recordedSampleCount, 1u);
    EXPECT_EQ(drain.retiredSampleCount, 1u);
    history.snapshot(snapshot);
    EXPECT_EQ(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)), nullptr);
    const Core::GpuTaskTimingHistory* const timing = snapshot.find(key, queue);
    ASSERT_NE(timing, nullptr);
    EXPECT_EQ(timing->sampleCount, 1u);
}

TEST(RendererTaskTimingFeedbackState, DeviceGenerationChangeDropsAcceptedEntryAwaitingOldSample){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    Core::GpuTaskTimingHistorySnapshot snapshot(testArena.arena);
    const Name scopeName("tests.renderer_timing.device_change");
    const Core::GpuPhysicalQueueId oldQueue{ .index = 0u, .deviceGeneration = 6u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.device_change.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, oldQueue, 31u, false));
    state.acceptSubmission(attribution, AcceptedToken(key, oldQueue), true);

    history.resetForDeviceGeneration(oldQueue.deviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult assignmentDrain = state.drain(
        history,
        oldQueue.deviceGeneration
    );
    EXPECT_EQ(assignmentDrain.acceptedAssignmentCount, 1u);
    EXPECT_EQ(assignmentDrain.retiredSampleCount, 0u);

    const u16 newDeviceGeneration = 7u;
    history.resetForDeviceGeneration(newDeviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult resetDrain = state.drain(history, newDeviceGeneration);
    EXPECT_EQ(resetDrain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(resetDrain.recordedSampleCount, 0u);
    EXPECT_EQ(resetDrain.retiredSampleCount, 1u);
    history.snapshot(snapshot);
    ASSERT_TRUE(snapshot.valid());
    EXPECT_EQ(snapshot.deviceGeneration(), newDeviceGeneration);
    EXPECT_EQ(snapshot.findAssignment(Core::GpuTaskTimingAssignmentKeyFromHistoryKey(key)), nullptr);

    state.completeSample(PublishedSample(attribution, scopeName, oldQueue, 31u, 0.005), true);
    const Impl::RendererTaskTimingFeedbackDrainResult lateDrain = state.drain(history, newDeviceGeneration);
    EXPECT_EQ(lateDrain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(lateDrain.recordedSampleCount, 0u);
    EXPECT_EQ(lateDrain.retiredSampleCount, 0u);
}

TEST(RendererTaskTimingFeedbackState, DeviceGenerationChangeDropsEntryBeforeSubmissionResolution){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder timingRecorder(testArena.arena, timingSink);
    Impl::RendererTaskTimingFeedbackState state(testArena.arena);
    Core::GpuTaskTimingHistoryStore history(testArena.arena);
    const Name scopeName("tests.renderer_timing.unresolved_device_change");
    const Core::GpuPhysicalQueueId oldQueue{ .index = 0u, .deviceGeneration = 12u };
    const Core::GpuTaskTimingKey key{
        .task = Name("tests.renderer_timing.unresolved_device_change.task"),
        .queue = Core::CommandQueue::Compute,
    };
    const Core::GpuTimingSampleAttribution attribution = timingRecorder.allocateSampleAttribution();
    ASSERT_TRUE(state.trackSample(attribution, scopeName, key, oldQueue, 40u, false));

    const u16 newDeviceGeneration = 13u;
    history.resetForDeviceGeneration(newDeviceGeneration);
    const Impl::RendererTaskTimingFeedbackDrainResult drain = state.drain(history, newDeviceGeneration);
    EXPECT_EQ(drain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(drain.recordedSampleCount, 0u);
    EXPECT_EQ(drain.retiredSampleCount, 1u);

    state.acceptSubmission(attribution, AcceptedToken(key, oldQueue), true);
    state.completeSample(PublishedSample(attribution, scopeName, oldQueue, 40u, 0.005), true);
    const Impl::RendererTaskTimingFeedbackDrainResult lateDrain = state.drain(history, newDeviceGeneration);
    EXPECT_EQ(lateDrain.acceptedAssignmentCount, 0u);
    EXPECT_EQ(lateDrain.recordedSampleCount, 0u);
    EXPECT_EQ(lateDrain.retiredSampleCount, 0u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

