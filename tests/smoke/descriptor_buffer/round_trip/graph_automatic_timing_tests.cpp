// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_GraphPacketEnvelopeOverlapScope("tests/timing_graph_packet_envelope_overlap");


inline constexpr GpuTimingScopeDefinition s_GraphPacketEnvelopeInternalIdleScope("tests/timing_graph_packet_envelope_internal_idle");


inline constexpr GpuTimingScopeDefinition s_GraphPacketPairOverlapScope("tests/timing_graph_packet_pair_overlap");


// Automatic graph timing owns one submission ticket per timed packet. A legacy recorder must reject the plan before
// invoking any thunk, while an independently recorded compatibility ticket shares the packet's native token. The
// timing-aware path publishes both that manual scope and the packet/task scopes selected by compiler-owned policy.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAutomaticTimingPublishesPolicyScopesAndOwnsPacketTickets){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    const bool comparableTimestamps = device.supportsComparableGpuTimestamps(graphicsQueue);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "Graph-owned automatic timing: primary Graphics queue exposes no timestamp bits.";

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    timing.beginFrame(260u);

    const Name outsidePrefixIdentity("tests/descriptor_buffer/automatic_timing_outside_prefix");
    const Name envelopeOnlyIdentity("tests/descriptor_buffer/automatic_timing_envelope_only");
    const Name packetOnlyIdentity("tests/descriptor_buffer/automatic_timing_packet_only");
    const Name taskIdentity("tests/descriptor_buffer/automatic_timing_task");
    const Name outsideSuffixIdentity("tests/descriptor_buffer/automatic_timing_outside_suffix");
    const Name outsidePrefixPacketScope = GpuTaskPacketTimingScopeName(outsidePrefixIdentity);
    const Name envelopeOnlyPacketScope = GpuTaskPacketTimingScopeName(envelopeOnlyIdentity);
    const Name packetOnlyPacketScope = GpuTaskPacketTimingScopeName(packetOnlyIdentity);
    const Name taskPacketScope = GpuTaskPacketTimingScopeName(taskIdentity);
    const Name outsideSuffixPacketScope = GpuTaskPacketTimingScopeName(outsideSuffixIdentity);
    GpuTimingSubmissionTicket companionTimingTicket(timing);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint forcedPacketScheduling;
    forcedPacketScheduling.forceSubmissionBoundary = true;
    forcedPacketScheduling.allowPacketMerge = false;

    bool shouldRecord = true;
    bool outsidePrefixRecorded = false;
    bool envelopeOnlyRecorded = false;
    bool packetOnlyRecorded = false;
    bool taskRecorded = false;
    bool outsideSuffixRecorded = false;
    const GpuTaskId outsidePrefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(outsidePrefixIdentity)
            .setMarkerLabel("Automatic Timing Outside Prefix")
            .setQueue(graphicsRequest)
            .setScheduling(forcedPacketScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &outsidePrefixRecorded,
        }
    );
    ASSERT_TRUE(outsidePrefixTask.valid());

    const GpuTaskId envelopeOnlyDependencies[] = { outsidePrefixTask };
    const GpuTaskId envelopeOnlyTask = graph.addTask<NativePacketCompanionTimingCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(envelopeOnlyIdentity)
            .setMarkerLabel("Automatic Timing Envelope Only")
            .setQueue(graphicsRequest)
            .setScheduling(forcedPacketScheduling)
            .setDependencies(envelopeOnlyDependencies, LengthOf(envelopeOnlyDependencies)),
        NativePacketCompanionTimingCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &envelopeOnlyRecorded,
            .device = &device,
            .timing = &timing,
            .timingTicket = &companionTimingTicket,
            .timingScope = &s_GraphCompanionSubmissionTicketScope,
        }
    );
    ASSERT_TRUE(envelopeOnlyTask.valid());

    const GpuTaskId packetOnlyDependencies[] = { envelopeOnlyTask };
    const GpuTaskId packetOnlyTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(packetOnlyIdentity)
            .setMarkerLabel("Automatic Timing Packet Only")
            .setQueue(graphicsRequest)
            .setScheduling(forcedPacketScheduling)
            .setDependencies(packetOnlyDependencies, LengthOf(packetOnlyDependencies))
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly }),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &packetOnlyRecorded,
        }
    );
    ASSERT_TRUE(packetOnlyTask.valid());

    const GpuTaskId taskDependencies[] = { packetOnlyTask };
    const GpuTaskId timedTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(taskIdentity)
            .setMarkerLabel("Automatic Timing Task")
            .setQueue(graphicsRequest)
            .setScheduling(forcedPacketScheduling)
            .setDependencies(taskDependencies, LengthOf(taskDependencies))
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::Task }),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &taskRecorded,
        }
    );
    ASSERT_TRUE(timedTask.valid());

    const GpuTaskId outsideSuffixDependencies[] = { timedTask };
    const GpuTaskId outsideSuffixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(outsideSuffixIdentity)
            .setMarkerLabel("Automatic Timing Outside Suffix")
            .setQueue(graphicsRequest)
            .setScheduling(forcedPacketScheduling)
            .setDependencies(outsideSuffixDependencies, LengthOf(outsideSuffixDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &outsideSuffixRecorded,
        }
    );
    ASSERT_TRUE(outsideSuffixTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/automatic_timing_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetTimingEnvelope.firstTask = envelopeOnlyTask;
    compileOptions.packetTimingEnvelope.lastTask = timedTask;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        topology,
        assignments,
        compiledGraph,
        scratchArena,
        compileOptions
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 5u);

    const GpuSubmissionPacketId outsidePrefixPacket = views.compiled.packetForTask(outsidePrefixTask);
    const GpuSubmissionPacketId envelopeOnlyPacket = views.compiled.packetForTask(envelopeOnlyTask);
    const GpuSubmissionPacketId packetOnlyPacket = views.compiled.packetForTask(packetOnlyTask);
    const GpuSubmissionPacketId taskPacket = views.compiled.packetForTask(timedTask);
    const GpuSubmissionPacketId outsideSuffixPacket = views.compiled.packetForTask(outsideSuffixTask);
    ASSERT_TRUE(outsidePrefixPacket.valid());
    ASSERT_TRUE(envelopeOnlyPacket.valid());
    ASSERT_TRUE(packetOnlyPacket.valid());
    ASSERT_TRUE(taskPacket.valid());
    ASSERT_TRUE(outsideSuffixPacket.valid());
    EXPECT_EQ(views.compiled.packetIdAt(0u), outsidePrefixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), envelopeOnlyPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), packetOnlyPacket);
    EXPECT_EQ(views.compiled.packetIdAt(3u), taskPacket);
    EXPECT_EQ(views.compiled.packetIdAt(4u), outsideSuffixPacket);
    EXPECT_NE(outsidePrefixPacket, envelopeOnlyPacket);
    EXPECT_NE(envelopeOnlyPacket, packetOnlyPacket);
    EXPECT_NE(packetOnlyPacket, taskPacket);
    EXPECT_NE(taskPacket, outsideSuffixPacket);
    EXPECT_FALSE(views.compiled.packet(outsidePrefixPacket).plan->recordsPacketEnvelopeTiming);
    EXPECT_FALSE(views.compiled.packet(outsidePrefixPacket).plan->recordsTiming);
    EXPECT_TRUE(views.compiled.packet(envelopeOnlyPacket).plan->recordsPacketEnvelopeTiming);
    EXPECT_TRUE(views.compiled.packet(envelopeOnlyPacket).plan->recordsTiming);
    EXPECT_TRUE(views.compiled.packet(packetOnlyPacket).plan->recordsPacketEnvelopeTiming);
    EXPECT_TRUE(views.compiled.packet(packetOnlyPacket).plan->recordsTiming);
    EXPECT_TRUE(views.compiled.packet(taskPacket).plan->recordsPacketEnvelopeTiming);
    EXPECT_TRUE(views.compiled.packet(taskPacket).plan->recordsTiming);
    EXPECT_FALSE(views.compiled.packet(outsideSuffixPacket).plan->recordsPacketEnvelopeTiming);
    EXPECT_FALSE(views.compiled.packet(outsideSuffixPacket).plan->recordsTiming);
    const GpuCompiledTaskView compiledEnvelopeOnlyTask = views.compiled.findTask(envelopeOnlyTask);
    const GpuCompiledTaskView compiledPacketOnlyTask = views.compiled.findTask(packetOnlyTask);
    const GpuCompiledTaskView compiledTimedTask = views.compiled.findTask(timedTask);
    ASSERT_TRUE(compiledEnvelopeOnlyTask.valid());
    ASSERT_TRUE(compiledPacketOnlyTask.valid());
    ASSERT_TRUE(compiledTimedTask.valid());
    EXPECT_EQ(compiledEnvelopeOnlyTask.plan->timingPolicy, GpuTaskTimingPolicy::None);
    EXPECT_EQ(compiledPacketOnlyTask.plan->timingPolicy, GpuTaskTimingPolicy::PacketOnly);
    EXPECT_EQ(compiledTimedTask.plan->timingPolicy, GpuTaskTimingPolicy::Task);
    const GpuSubmissionPacketRange range = views.compiled.packetRangeForTasks(envelopeOnlyTask, timedTask);
    ASSERT_TRUE(range.valid());
    EXPECT_EQ(range.first, envelopeOnlyPacket);
    EXPECT_EQ(range.packetCount, 3u);
    EXPECT_EQ(views.compiled.packetTimingEnvelopeRange().first, range.first);
    EXPECT_EQ(views.compiled.packetTimingEnvelopeRange().packetCount, range.packetCount);

    const GpuPacketEnvelopeMetricScope packetEnvelopeScopes[] = {
        { .scopeName = envelopeOnlyPacketScope, .physicalQueue = graphicsQueue },
        { .scopeName = packetOnlyPacketScope, .physicalQueue = graphicsQueue },
        { .scopeName = taskPacketScope, .physicalQueue = graphicsQueue },
    };
    const GpuPacketEnvelopeMetricQueueOutput packetEnvelopeOutputs[] = {
        {
            .physicalQueue = graphicsQueue,
            .internalIdleScopeName = s_GraphPacketEnvelopeInternalIdleScope.identity,
        },
    };
    ASSERT_TRUE(timing.prepareOverlapMetric(
        envelopeOnlyPacketScope,
        packetOnlyPacketScope,
        s_GraphPacketPairOverlapScope.identity
    ));
    ASSERT_TRUE(timing.prepareOverlapMetric(
        packetOnlyPacketScope,
        envelopeOnlyPacketScope,
        s_GraphPacketPairOverlapScope.identity
    ));
    ASSERT_TRUE(timing.preparePacketEnvelopeMetrics(
        260u,
        MakeNotNull(&packetEnvelopeScopes[0u]),
        LengthOf(packetEnvelopeScopes),
        s_GraphPacketEnvelopeOverlapScope.identity,
        MakeNotNull(&packetEnvelopeOutputs[0u]),
        LengthOf(packetEnvelopeOutputs)
    ));

    ASSERT_TRUE(timing.prepareScopeQueries(envelopeOnlyPacketScope, device, s_MaxFramesInFlight));
    ASSERT_TRUE(timing.prepareScopeQueries(packetOnlyPacketScope, device, s_MaxFramesInFlight));
    ASSERT_TRUE(timing.prepareScopeQueries(taskPacketScope, device, s_MaxFramesInFlight));
    ASSERT_TRUE(timing.prepareScopeQueries(taskIdentity, device, s_MaxFramesInFlight));
    ASSERT_TRUE(timing.prepareScopeQueries(s_GraphCompanionSubmissionTicketScope.identity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuSubmissionPacketId failedRecordingPacket;
    const GpuNativePacketRecorder legacyRecorder(device);
    EXPECT_FALSE(legacyRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        outsidePrefixTask,
        outsideSuffixTask,
        recordedGraph,
        &failedRecordingPacket
    ));
    EXPECT_FALSE(failedRecordingPacket.valid());
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(outsidePrefixRecorded);
    EXPECT_FALSE(envelopeOnlyRecorded);
    EXPECT_FALSE(packetOnlyRecorded);
    EXPECT_FALSE(taskRecorded);
    EXPECT_FALSE(outsideSuffixRecorded);

    const GpuNativePacketRecorder timingRecorder(device, timing);
    {
        GpuTimingRecorder alternateTiming(DescriptorBufferRoundTripTest::arena(), timingSink);
        alternateTiming.setQueryCollectionEnabled(true);
        alternateTiming.beginFrame(260u);
        ASSERT_TRUE(timingRecorder.recordTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            outsidePrefixTask,
            envelopeOnlyTask,
            recordedGraph,
            &failedRecordingPacket
        ));
        EXPECT_FALSE(failedRecordingPacket.valid());
        EXPECT_TRUE(outsidePrefixRecorded);
        EXPECT_TRUE(envelopeOnlyRecorded);
        EXPECT_FALSE(packetOnlyRecorded);
        EXPECT_FALSE(taskRecorded);
        EXPECT_FALSE(outsideSuffixRecorded);
        EXPECT_TRUE(recordedGraph.packetSnapshot(outsidePrefixPacket).has_value());
        EXPECT_TRUE(recordedGraph.packetSnapshot(envelopeOnlyPacket).has_value());
        EXPECT_FALSE(recordedGraph.packetSnapshot(packetOnlyPacket).has_value());
        EXPECT_FALSE(recordedGraph.packetSnapshot(taskPacket).has_value());
        const u64 firstRecorderAttemptGeneration = recordedGraph.recordingAttemptGeneration();
        EXPECT_GT(firstRecorderAttemptGeneration, 0u);

        const GpuNativePacketRecorder alternateRecorder(device, alternateTiming);
        EXPECT_FALSE(alternateRecorder.recordTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            packetOnlyTask,
            packetOnlyTask,
            recordedGraph,
            &failedRecordingPacket
        ));
        EXPECT_FALSE(failedRecordingPacket.valid());
        EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), firstRecorderAttemptGeneration);
        EXPECT_TRUE(outsidePrefixRecorded);
        EXPECT_TRUE(envelopeOnlyRecorded);
        EXPECT_FALSE(packetOnlyRecorded);
        EXPECT_FALSE(taskRecorded);
        EXPECT_FALSE(outsideSuffixRecorded);
        EXPECT_TRUE(recordedGraph.packetSnapshot(outsidePrefixPacket).has_value());
        EXPECT_TRUE(recordedGraph.packetSnapshot(envelopeOnlyPacket).has_value());
        EXPECT_FALSE(recordedGraph.packetSnapshot(packetOnlyPacket).has_value());
        EXPECT_FALSE(recordedGraph.packetSnapshot(taskPacket).has_value());
        ASSERT_TRUE(alternateTiming.materializeRequestedQueries(device));
        const GpuTimingRecorderStatistics alternateTimingStatistics = alternateTiming.statistics(device);
        ASSERT_TRUE(alternateTimingStatistics.valid());
        EXPECT_EQ(alternateTimingStatistics.preparedScopeCount, 4u);
        EXPECT_GT(alternateTimingStatistics.requestedQueryCount, 0u);
        EXPECT_EQ(
            alternateTimingStatistics.materializedQueryCount,
            alternateTimingStatistics.requestedQueryCount
        );
        EXPECT_EQ(alternateTimingStatistics.queryMaterializationFailureCount, 0u);
        EXPECT_EQ(alternateTimingStatistics.scopeAttemptCount, 0u);
        EXPECT_EQ(alternateTimingStatistics.recordedScopeCount, 0u);
        alternateTiming.setQueryCollectionEnabled(false);
        alternateTiming.resetQueries();
    }

    ASSERT_TRUE(timingRecorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        packetOnlyTask,
        outsideSuffixTask,
        recordedGraph,
        &failedRecordingPacket
    ));
    EXPECT_FALSE(failedRecordingPacket.valid());
    EXPECT_GT(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_TRUE(outsidePrefixRecorded);
    EXPECT_TRUE(envelopeOnlyRecorded);
    EXPECT_TRUE(packetOnlyRecorded);
    EXPECT_TRUE(taskRecorded);
    EXPECT_TRUE(outsideSuffixRecorded);
    EXPECT_TRUE(recordedGraph.packetSnapshot(outsidePrefixPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(envelopeOnlyPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(packetOnlyPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(taskPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(outsideSuffixPacket).has_value());

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    GpuSubmissionPacketId failedSubmissionPacket;
    GpuTimingSubmissionTicket resolvedCompanionTicket(timing);
    resolvedCompanionTicket.discard();
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = outsidePrefixPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const GpuTaskGraphTaskTimingTicket resolvedCompanionBinding{
        .task = envelopeOnlyTask,
        .timingTicket = &resolvedCompanionTicket,
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        envelopeOnlyTask,
        envelopeOnlyTask,
        nullptr,
        0u,
        &resolvedCompanionBinding,
        1u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.packetToken(envelopeOnlyPacket).valid());
    const GpuTaskGraphSubmissionStatistics retryableStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(retryableStatistics.valid());
    EXPECT_EQ(retryableStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(retryableStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(retryableStatistics.rejectedSubmissionCount, 0u);

    const GpuTaskGraphTaskTimingTicket companionTimingBinding{
        .task = envelopeOnlyTask,
        .timingTicket = &companionTimingTicket,
    };
    const GpuSubmissionPacketRange submissionRange = views.compiled.packetRangeForTasks(
        envelopeOnlyTask,
        outsideSuffixTask
    );
    ASSERT_TRUE(submissionRange.valid());
    ASSERT_EQ(submissionRange.packetCount, 4u);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        envelopeOnlyTask,
        outsideSuffixTask,
        nullptr,
        0u,
        &companionTimingBinding,
        1u,
        transaction,
        scratchArena,
        &failedSubmissionPacket
    ));
    EXPECT_FALSE(failedSubmissionPacket.valid());
    EXPECT_TRUE(transaction.hasAcceptedPackets());
    const GpuTaskGraphSubmissionStatistics acceptedStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(acceptedStatistics.valid());
    EXPECT_EQ(acceptedStatistics.acceptedPacketCount, 5u);
    EXPECT_EQ(acceptedStatistics.acceptedTaskCount, 5u);
    EXPECT_EQ(acceptedStatistics.nativeSubmissionCount, 5u);
    EXPECT_TRUE(transaction.packetToken(outsidePrefixPacket).valid());
    EXPECT_TRUE(transaction.packetToken(envelopeOnlyPacket).valid());
    EXPECT_TRUE(transaction.packetToken(packetOnlyPacket).valid());
    EXPECT_TRUE(transaction.packetToken(taskPacket).valid());
    EXPECT_TRUE(transaction.packetToken(outsideSuffixPacket).valid());
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 261u);

    const auto envelopeOnlyPacketStatistics = timingSink.stats(envelopeOnlyPacketScope);
    const auto packetOnlyPacketStatistics = timingSink.stats(packetOnlyPacketScope);
    const auto taskPacketStatistics = timingSink.stats(taskPacketScope);
    const auto taskStatistics = timingSink.stats(taskIdentity);
    const auto companionTimingStatistics = timingSink.stats(s_GraphCompanionSubmissionTicketScope.identity);
    const auto envelopeOverlapStatistics = timingSink.stats(s_GraphPacketEnvelopeOverlapScope.identity);
    const auto envelopeInternalIdleStatistics = timingSink.stats(s_GraphPacketEnvelopeInternalIdleScope.identity);
    const auto packetPairOverlapStatistics = timingSink.stats(s_GraphPacketPairOverlapScope.identity);
    ASSERT_TRUE(envelopeOnlyPacketStatistics.valid());
    ASSERT_TRUE(packetOnlyPacketStatistics.valid());
    ASSERT_TRUE(taskPacketStatistics.valid());
    ASSERT_TRUE(taskStatistics.valid());
    ASSERT_TRUE(companionTimingStatistics.valid());
    EXPECT_EQ(envelopeOnlyPacketStatistics.sampleCount, 1u);
    EXPECT_EQ(packetOnlyPacketStatistics.sampleCount, 1u);
    EXPECT_EQ(taskPacketStatistics.sampleCount, 1u);
    EXPECT_EQ(taskStatistics.sampleCount, 1u);
    EXPECT_EQ(companionTimingStatistics.sampleCount, 1u);
    // Partial-width Vulkan timestamps retain queue-local durations but cannot safely correlate absolute endpoints
    // across submissions. Only the derived metrics depend on that stronger calibrated 64-bit timestamp contract.
    if(comparableTimestamps){
        ASSERT_TRUE(envelopeOverlapStatistics.valid());
        ASSERT_TRUE(envelopeInternalIdleStatistics.valid());
        ASSERT_TRUE(packetPairOverlapStatistics.valid());
        EXPECT_EQ(envelopeOverlapStatistics.sampleCount, 1u);
        EXPECT_EQ(envelopeInternalIdleStatistics.sampleCount, 1u);
        EXPECT_EQ(packetPairOverlapStatistics.sampleCount, 1u);
        EXPECT_DOUBLE_EQ(envelopeOverlapStatistics.seconds, 0.0);
        EXPECT_GE(envelopeInternalIdleStatistics.seconds, 0.0);
        EXPECT_GE(packetPairOverlapStatistics.seconds, 0.0);
        EXPECT_EQ(envelopeOverlapStatistics.firstSampleFrameIndex, 260u);
        EXPECT_EQ(envelopeInternalIdleStatistics.firstSampleFrameIndex, 260u);
        EXPECT_EQ(packetPairOverlapStatistics.firstSampleFrameIndex, 260u);
    }
    else{
        EXPECT_FALSE(envelopeOverlapStatistics.valid());
        EXPECT_FALSE(envelopeInternalIdleStatistics.valid());
        EXPECT_FALSE(packetPairOverlapStatistics.valid());
    }
    EXPECT_FALSE(timingSink.stats(outsidePrefixPacketScope).valid());
    EXPECT_FALSE(timingSink.stats(outsidePrefixIdentity).valid());
    EXPECT_FALSE(timingSink.stats(envelopeOnlyIdentity).valid());
    EXPECT_FALSE(timingSink.stats(packetOnlyIdentity).valid());
    EXPECT_FALSE(timingSink.stats(outsideSuffixPacketScope).valid());
    EXPECT_FALSE(timingSink.stats(outsideSuffixIdentity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Query accumulators and derived metric outputs own mutually exclusive timing-scope names. Exercise both ownership
// directions through the supported recorder API so neither registration order can silently merge the roles.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingQueryAndMetricOutputNamesRemainDisjoint){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();

    const Name firstOverlapInput("tests/timing_role_disjoint/overlap_first");
    const Name secondOverlapInput("tests/timing_role_disjoint/overlap_second");
    const Name queryOwnedOverlapOutput("tests/timing_role_disjoint/query_owned_overlap_output");
    const Name metricOwnedOverlapOutput("tests/timing_role_disjoint/metric_owned_overlap_output");
    ASSERT_TRUE(timing.prepareScopeQueries(queryOwnedOverlapOutput, device, 1u));
    EXPECT_FALSE(timing.prepareOverlapMetric(firstOverlapInput, secondOverlapInput, queryOwnedOverlapOutput));
    ASSERT_TRUE(timing.prepareOverlapMetric(firstOverlapInput, secondOverlapInput, metricOwnedOverlapOutput));
    EXPECT_FALSE(timing.prepareScopeQueries(metricOwnedOverlapOutput, device, 1u));

    const Name packetInput("tests/timing_role_disjoint/packet_input");
    const Name queryOwnedPacketOverlapOutput("tests/timing_role_disjoint/query_owned_packet_overlap_output");
    const Name queryOwnedPacketIdleOutput("tests/timing_role_disjoint/query_owned_packet_idle_output");
    const Name metricOwnedPacketOverlapOutput("tests/timing_role_disjoint/metric_owned_packet_overlap_output");
    const Name metricOwnedPacketIdleOutput("tests/timing_role_disjoint/metric_owned_packet_idle_output");
    const GpuPacketEnvelopeMetricScope packetScopes[] = {
        { .scopeName = packetInput, .physicalQueue = graphicsQueue },
    };
    const GpuPacketEnvelopeMetricQueueOutput queryOwnedIdleOutputs[] = {
        { .physicalQueue = graphicsQueue, .internalIdleScopeName = queryOwnedPacketIdleOutput },
    };
    const GpuPacketEnvelopeMetricQueueOutput metricOwnedIdleOutputs[] = {
        { .physicalQueue = graphicsQueue, .internalIdleScopeName = metricOwnedPacketIdleOutput },
    };
    ASSERT_TRUE(timing.prepareScopeQueries(queryOwnedPacketOverlapOutput, device, 1u));
    ASSERT_TRUE(timing.prepareScopeQueries(queryOwnedPacketIdleOutput, device, 1u));
    EXPECT_FALSE(timing.preparePacketEnvelopeMetrics(
        270u,
        MakeNotNull(&packetScopes[0u]),
        LengthOf(packetScopes),
        queryOwnedPacketOverlapOutput,
        MakeNotNull(&metricOwnedIdleOutputs[0u]),
        LengthOf(metricOwnedIdleOutputs)
    ));
    EXPECT_FALSE(timing.preparePacketEnvelopeMetrics(
        270u,
        MakeNotNull(&packetScopes[0u]),
        LengthOf(packetScopes),
        metricOwnedPacketOverlapOutput,
        MakeNotNull(&queryOwnedIdleOutputs[0u]),
        LengthOf(queryOwnedIdleOutputs)
    ));
    ASSERT_TRUE(timing.preparePacketEnvelopeMetrics(
        270u,
        MakeNotNull(&packetScopes[0u]),
        LengthOf(packetScopes),
        metricOwnedPacketOverlapOutput,
        MakeNotNull(&metricOwnedIdleOutputs[0u]),
        LengthOf(metricOwnedIdleOutputs)
    ));
    EXPECT_FALSE(timing.prepareScopeQueries(metricOwnedPacketOverlapOutput, device, 1u));
    EXPECT_FALSE(timing.prepareScopeQueries(metricOwnedPacketIdleOutput, device, 1u));

    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

