// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_timing_packets_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A recovery packet is declared with the normal frame graph but recorded only after a later packet rejects. It must
// remain independent of that rejected packet, join the accepted queue frontier, and retire the timing scope before
// the shared transaction rejects any remaining normal work.
TEST_F(DescriptorBufferRoundTripTest, NativePacketLateRecordsFrameRecoveryInSharedTransaction){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.supportsComparableGpuTimestamps(device.getPrimaryPhysicalQueue(CommandQueue::Graphics)))
        GTEST_SKIP() << "GPU timing native recovery: comparable 64-bit device timestamps are unavailable.";
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
    timing.beginFrame(120u);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId frameTimingDomain = graph.importHazardDomain(
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_frame_timing"))
            .setMarkerLabel("Frame Timing")
            .setType(GpuGraphResourceType::HazardDomain)
    );
    const GpuGraphResourceId recoveryDomain = graph.importHazardDomain(
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_domain"))
            .setMarkerLabel("Frame Recovery")
            .setType(GpuGraphResourceType::HazardDomain)
    );
    ASSERT_TRUE(frameTimingDomain.valid());
    ASSERT_TRUE(recoveryDomain.valid());

    const GpuTaskResourceUse frameTimingUses[] = {
        GpuTaskResourceUse{
            .resource = frameTimingDomain,
            .range = {},
            .requiredState = ResourceStates::Common,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse recoveryUses[] = {
        GpuTaskResourceUse{
            .resource = recoveryDomain,
            .range = {},
            .requiredState = ResourceStates::Common,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };

    GpuTimingFrameTransaction frameTransaction(timing);
    GpuTimingSubmissionTicket prefixTimingTicket(timing);
    GpuTimingSubmissionTicket finalTimingTicket(timing);
    bool prefixRecorded = false;
    bool finalRecorded = false;
    bool recoveryArmed = false;
    bool recoveryRetiresTiming = false;
    bool recoveryRecorded = false;
    bool recoveryAccepted = false;
    bool recoveryDiscarded = false;

    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_prefix"))
        .setMarkerLabel("Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(scheduling)
        .setResourceUses(frameTimingUses, LengthOf(frameTimingUses))
    ;
    const GpuTaskId prefixTask = graph.addTask<NativeFrameTimingPacketTask>(
        prefixDesc,
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &prefixTimingTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::Begin,
            .recorded = &prefixRecorded,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    const GpuTaskId finalDependencies[] = { prefixTask };
    GpuTaskDesc finalDesc;
    finalDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_rejected_final"))
        .setMarkerLabel("Rejected Final")
        .setQueue(graphicsQueue)
        .setScheduling(scheduling)
        .setDependencies(finalDependencies, LengthOf(finalDependencies))
        .setResourceUses(frameTimingUses, LengthOf(frameTimingUses))
    ;
    const GpuTaskId finalTask = graph.addTask<NativeFrameTimingPacketTask>(
        finalDesc,
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &finalTimingTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::End,
            .recorded = &finalRecorded,
        }
    );
    ASSERT_TRUE(finalTask.valid());

    GpuTaskSchedulingHint recoveryScheduling = scheduling;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_tail"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(graphicsQueue)
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryUses, LengthOf(recoveryUses))
    ;
    const GpuTaskId recoveryTask = graph.addTask<NativeFrameRecoveryPacketTask>(
        recoveryDesc,
        NativeFrameRecoveryPacketTask::Payload{
            .transaction = &frameTransaction,
            .armed = &recoveryArmed,
            .retiresTiming = &recoveryRetiresTiming,
            .recorded = &recoveryRecorded,
            .accepted = &recoveryAccepted,
            .discarded = &recoveryDiscarded,
        }
    );
    ASSERT_TRUE(recoveryTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/late_recovery_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId finalPacket = views.compiled.packetForTask(finalTask);
    const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(finalPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 3u);
    EXPECT_EQ(views.compiled.packetIdAt(0u), prefixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), finalPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), recoveryPacket);
    EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->externalDependencyCount, 0u);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->isRecoverySubmission);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = prefixPacket, .packetCount = 1u },
        recordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = finalPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(prefixRecorded);
    EXPECT_TRUE(finalRecorded);

    const GpuTaskScheduler submitter(device);
    const GpuTaskGraphTaskTimingTicket prefixTimingBinding{
        .task = prefixTask,
        .timingTicket = &prefixTimingTicket,
    };
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        prefixTask,
        prefixTask,
        nullptr,
        0u,
        &prefixTimingBinding,
        1u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken prefixToken = transaction.packetToken(prefixPacket);
    ASSERT_TRUE(prefixToken.valid());
    ASSERT_TRUE(frameTransaction.confirmBeginSubmission(prefixToken));

    const GpuTaskGraphTaskTimingTicket finalTimingBinding{
        .task = finalTask,
        .timingTicket = &finalTimingTicket,
    };
    {
        const GpuPhysicalQueueId rejectedQueue = views.compiled.packet(finalPacket).plan->queue;
        ASSERT_TRUE(rejectedQueue.valid());
        const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
        );
        ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeRejectedQueue));
        EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            finalTask,
            finalTask,
            nullptr,
            0u,
            &finalTimingBinding,
            1u,
            transaction,
            scratchArena
        ));
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(transaction.packetToken(finalPacket).valid());
    const GpuTaskGraphSubmissionStatistics rejectedSubmissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(rejectedSubmissionStatistics.valid());
    EXPECT_EQ(rejectedSubmissionStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.acceptedTaskCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.rejectedTaskCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.rejectedSubmissionCount, 1u);
    EXPECT_EQ(rejectedSubmissionStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(rejectedSubmissionStatistics.recoverySubmissionCount, 0u);
    EXPECT_FALSE(recoveryRecorded);
    EXPECT_FALSE(recoveryAccepted);
    EXPECT_FALSE(recoveryDiscarded);
    ASSERT_TRUE(frameTransaction.needsRetirement());

    ASSERT_TRUE(frameTransaction.prepareForRecovery());
    recoveryArmed = true;
    recoveryRetiresTiming = true;
    ASSERT_TRUE(submitter.recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recoveryTask,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(recoveryRecorded);
    EXPECT_TRUE(recoveryAccepted);
    EXPECT_FALSE(recoveryDiscarded);
    EXPECT_FALSE(recoveryArmed);
    EXPECT_FALSE(recoveryRetiresTiming);
    EXPECT_FALSE(frameTransaction.needsRetirement());
    const GpuTaskGraphSubmissionStatistics recoveredSubmissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(recoveredSubmissionStatistics.valid());
    EXPECT_EQ(recoveredSubmissionStatistics.acceptedPacketCount, 2u);
    EXPECT_EQ(recoveredSubmissionStatistics.acceptedTaskCount, 2u);
    EXPECT_EQ(recoveredSubmissionStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(recoveredSubmissionStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.rejectedTaskCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.rejectedSubmissionCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.recoverySubmissionCount, 1u);
    // The recovery packet joins the accepted frontier semantically, but its only accepted producer is on this same
    // physical queue, so queue order removes the otherwise redundant timeline wait before native submission.
    EXPECT_EQ(recoveredSubmissionStatistics.plannedWaitTokenCount, 0u);
    EXPECT_EQ(recoveredSubmissionStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(recoveredSubmissionStatistics.timelineWaitCount, 0u);

    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(transaction.packetToken(prefixPacket).valid());
    EXPECT_TRUE(transaction.packetToken(recoveryPacket).valid());
    EXPECT_FALSE(recoveryDiscarded);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 121u);
    EXPECT_FALSE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// The serial semantic range helper owns only ordinary graph work. Rejected normal task submission remains visible
// so the caller can record the late frontier task itself and then resolve its remaining transactional state.
TEST_F(DescriptorBufferRoundTripTest, TaskRangeHelperPreservesRecoveryOwnership){
    auto& device = DescriptorBufferRoundTripTest::device();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint normalScheduling;
    normalScheduling.forceSubmissionBoundary = true;
    normalScheduling.allowPacketMerge = false;

    bool prefixShouldRecord = true;
    bool prefixRecorded = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_range_helper_serial_prefix"))
            .setMarkerLabel("Task Range Helper Serial Prefix")
            .setQueue(graphicsQueue)
            .setScheduling(normalScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &prefixShouldRecord,
            .attempted = &prefixRecorded,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    bool rejectedShouldRecord = true;
    bool rejectedRecorded = false;
    const GpuTaskId rejectedDependencies[] = { prefixTask };
    const GpuTaskId rejectedTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_range_helper_serial_rejected"))
            .setMarkerLabel("Task Range Helper Serial Rejected")
            .setQueue(graphicsQueue)
            .setScheduling(normalScheduling)
            .setDependencies(rejectedDependencies, LengthOf(rejectedDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &rejectedShouldRecord,
            .attempted = &rejectedRecorded,
        }
    );
    ASSERT_TRUE(rejectedTask.valid());

    bool recoveryShouldRecord = true;
    bool recoveryRecorded = false;
    GpuTaskSchedulingHint recoveryScheduling = normalScheduling;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const GpuTaskId recoveryTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_range_helper_serial_recovery"))
            .setMarkerLabel("Task Range Helper Serial Recovery")
            .setQueue(graphicsQueue)
            .setScheduling(recoveryScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &recoveryShouldRecord,
            .attempted = &recoveryRecorded,
        }
    );
    ASSERT_TRUE(recoveryTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/task_range_helper_serial_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId rejectedPacket = views.compiled.packetForTask(rejectedTask);
    const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(rejectedPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 3u);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    GpuSubmissionPacketId failedPacket;

    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        {},
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        rejectedTask,
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);

    // Semantic endpoints that reach the late recovery task fail before any normal task recording begins.
    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        prefixTask,
        recoveryTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, recoveryPacket);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(prefixRecorded);
    EXPECT_FALSE(rejectedRecorded);
    EXPECT_FALSE(recoveryRecorded);
    EXPECT_FALSE(transaction.hasAcceptedPackets());

    ASSERT_TRUE(submitter.recordAndSubmitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        prefixTask,
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(prefixRecorded);
    ASSERT_TRUE(transaction.packetToken(prefixPacket).valid());

    // A rejected normal task remains recorded/rejected for caller-owned recovery; the helper does not touch the
    // late frontier task or cleanup state.
    {
        const GpuPhysicalQueueId rejectedQueue = views.compiled.packet(rejectedPacket).plan->queue;
        ASSERT_TRUE(rejectedQueue.valid());
        const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
        );
        ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeRejectedQueue));
        EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            rejectedTask,
            rejectedTask,
            transaction,
            scratchArena,
            &failedPacket
        ));
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_EQ(failedPacket, rejectedPacket);
    EXPECT_TRUE(rejectedRecorded);
    EXPECT_FALSE(recoveryRecorded);
    EXPECT_TRUE(transaction.packetToken(prefixPacket).valid());
    EXPECT_FALSE(transaction.packetToken(rejectedPacket).valid());
    EXPECT_FALSE(transaction.packetToken(recoveryPacket).valid());

    ASSERT_TRUE(submitter.recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recoveryTask,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(recoveryRecorded);
    ASSERT_TRUE(transaction.packetToken(recoveryPacket).valid());
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

