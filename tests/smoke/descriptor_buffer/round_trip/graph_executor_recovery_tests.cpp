// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Recorded-state validation belongs to the ordinary graph transaction. A rejected candidate stops before the first
// native submit and identifies its semantic packet while the caller retains explicit discard ownership.
TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorStopsBeforeRejectedRecordedCallbackSubmission){
    auto& device = DescriptorBufferRoundTripTest::device();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    bool shouldRecord = true;
    bool recorded = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_recorded_callback_rejection"))
            .setMarkerLabel("Normal Executor Recorded Callback Rejection")
            .setQueue(graphicsQueue),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &recorded,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/normal_executor_recorded_callback_rejection_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    bool callbackInvoked = false;
    const auto rejectRecordedState = [](
        void* const rawContext,
        const CommandListResourceStateHandoff* const finalState
    ) -> bool {
        static_cast<void>(finalState);
        bool* const invoked = static_cast<bool*>(rawContext);
        if(!invoked)
            return false;
        *invoked = true;
        return false;
    };
    const GpuTaskGraphTaskRecordedCallback recordedCallback{
        .task = task,
        .context = &callbackInvoked,
        .invoke = rejectRecordedState,
    };
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskRecordedCallbacks = &recordedCallback;
    normalExecution.taskRecordedCallbackCount = 1u;

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_TRUE(recorded);
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(failedPacket, packet);
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    const GpuTaskGraphSubmissionStatistics beforeDiscard = transaction.submissionStatistics();
    EXPECT_EQ(beforeDiscard.nativeSubmissionCount, 0u);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    const GpuTaskGraphSubmissionStatistics afterDiscard = transaction.submissionStatistics();
    EXPECT_EQ(afterDiscard.rejectedPacketCount, beforeDiscard.rejectedPacketCount + 1u);
    EXPECT_EQ(afterDiscard.rejectedSubmissionCount, beforeDiscard.rejectedSubmissionCount);
    EXPECT_TRUE(device.waitForIdle());
}


// The graph-owned normal executor derives the ordinary prefix itself. A normal submission failure remains visible
// so the caller can record the terminal frontier task itself and then resolve its remaining transactional state.
TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorPreservesRecoveryOwnership){
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
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->isRecoverySubmission);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    GpuSubmissionPacketId failedPacket;

    const GpuPhysicalQueueId rejectedQueue = views.compiled.packet(rejectedPacket).plan->queue;
    ASSERT_TRUE(rejectedQueue.valid());
    const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
    );
    ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    struct FailureArm{
        VulkanTestQueueSubmit2Observer* observer = nullptr;
        VkQueue queue = VK_NULL_HANDLE;
        bool armed = false;
    };
    FailureArm failureArm{
        .observer = &submissionObserver,
        .queue = nativeRejectedQueue,
    };
    const GpuTaskGraphTaskAcceptedCallback acceptedCallback{
        .task = prefixTask,
        .context = &failureArm,
        .invoke = [](void* const rawContext, const QueueSubmissionToken& token){
            FailureArm* const context = static_cast<FailureArm*>(rawContext);
            if(!context || !context->observer || context->queue == VK_NULL_HANDLE || !token.valid())
                return false;
            context->armed = context->observer->armSubmissionFailures(context->queue);
            return context->armed;
        },
    };
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskAcceptedCallbacks = &acceptedCallback;
    normalExecution.taskAcceptedCallbackCount = 1u;

    // The first normal packet accepts, then the semantic callback arms a rejection for the second normal packet.
    // The executor must leave the terminal frontier declared for the caller's explicit recovery submission.
    EXPECT_FALSE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, rejectedPacket);
    EXPECT_TRUE(failureArm.armed);
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    EXPECT_TRUE(prefixRecorded);
    EXPECT_TRUE(rejectedRecorded);
    EXPECT_FALSE(recoveryRecorded);
    ASSERT_TRUE(transaction.packetToken(prefixPacket).valid());
    EXPECT_FALSE(transaction.packetToken(rejectedPacket).valid());
    EXPECT_FALSE(transaction.packetToken(recoveryPacket).valid());
    const GpuTaskGraphSubmissionStatistics rejectedSubmissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(rejectedSubmissionStatistics.valid());
    EXPECT_EQ(rejectedSubmissionStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(rejectedSubmissionStatistics.recoverySubmissionCount, 0u);

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
    const GpuTaskGraphSubmissionStatistics recoveredSubmissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(recoveredSubmissionStatistics.valid());
    EXPECT_EQ(recoveredSubmissionStatistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.recoverySubmissionCount, 1u);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(device.waitForIdle());
}


// The compiler preserves a frontier task as its own packet but deliberately does not impose terminal ordering.
// A normal executor must therefore fail closed if an ordinary packet follows that frontier in compile order.
TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorRejectsNonTerminalFrontier){
    auto& device = DescriptorBufferRoundTripTest::device();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint frontierScheduling;
    frontierScheduling.forceSubmissionBoundary = true;
    frontierScheduling.allowPacketMerge = false;
    frontierScheduling.joinsAcceptedQueueFrontier = true;

    bool frontierShouldRecord = true;
    bool frontierRecorded = false;
    const GpuTaskId frontierTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_nonterminal_frontier"))
            .setMarkerLabel("Normal Executor Nonterminal Frontier")
            .setQueue(graphicsQueue)
            .setScheduling(frontierScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &frontierShouldRecord,
            .attempted = &frontierRecorded,
        }
    );
    ASSERT_TRUE(frontierTask.valid());

    GpuTaskSchedulingHint normalScheduling = frontierScheduling;
    normalScheduling.joinsAcceptedQueueFrontier = false;
    bool normalShouldRecord = true;
    bool normalRecorded = false;
    const GpuTaskId normalDependencies[] = { frontierTask };
    const GpuTaskId normalTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_after_frontier"))
            .setMarkerLabel("Normal Executor After Frontier")
            .setQueue(graphicsQueue)
            .setScheduling(normalScheduling)
            .setDependencies(normalDependencies, LengthOf(normalDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &normalShouldRecord,
            .attempted = &normalRecorded,
        }
    );
    ASSERT_TRUE(normalTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/normal_executor_nonterminal_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId frontierPacket = views.compiled.packetForTask(frontierTask);
    const GpuSubmissionPacketId normalPacket = views.compiled.packetForTask(normalTask);
    ASSERT_TRUE(frontierPacket.valid());
    ASSERT_TRUE(normalPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 2u);
    EXPECT_EQ(views.compiled.packetIdAt(0u), frontierPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), normalPacket);
    EXPECT_TRUE(views.compiled.packet(frontierPacket).plan->joinsAcceptedQueueFrontier);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = normalTask;
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, frontierPacket);
    EXPECT_FALSE(frontierRecorded);
    EXPECT_FALSE(normalRecorded);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(transaction.hasAcceptedPackets());
}


// Ready-frontier semantic range execution owns only the ordinary graph prefix. A caller must retain its late
// recovery tail, including the choice to recover after one normal task rejects and the final transactional cleanup.
TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierTaskRangeHelperPreservesRecoveryOwnership){
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
            .setIdentity(Name("tests/descriptor_buffer/range_helper_prefix"))
            .setMarkerLabel("Range Helper Prefix")
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
            .setIdentity(Name("tests/descriptor_buffer/range_helper_rejected"))
            .setMarkerLabel("Range Helper Rejected")
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
            .setIdentity(Name("tests/descriptor_buffer/range_helper_recovery"))
            .setMarkerLabel("Range Helper Recovery")
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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/range_helper_scratch"));
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
    EXPECT_EQ(views.compiled.packetIdAt(0u), prefixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), rejectedPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), recoveryPacket);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);
    const GpuSubmissionPacketRange normalTaskRange = views.compiled.packetRangeForTasks(prefixTask, rejectedTask);
    ASSERT_TRUE(views.compiled.validPacketRange(normalTaskRange));
    EXPECT_EQ(normalTaskRange.packetCount, 2u);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    Alloc::ThreadPool recordingWorkers(2u, CpuAffinity::Any);
    const GpuTaskGraphSubmitter submitter(device);
    GpuSubmissionPacketId failedPacket;

    // Invalid and reversed semantic endpoints preserve the packet helper's empty failure result and do not record.
    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInReadyFrontiers(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recordingWorkers,
        {},
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInReadyFrontiers(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recordingWorkers,
        rejectedTask,
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);

    // The helper fails before beginning a recording attempt when semantic endpoints include the caller-owned
    // recovery packet.
    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInReadyFrontiers(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recordingWorkers,
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

    // Normal packets retain the recorder's serial fallback until every task opts in to worker recording.
    ASSERT_TRUE(submitter.recordAndSubmitTaskRangeInReadyFrontiers(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recordingWorkers,
        prefixTask,
        prefixTask,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(prefixRecorded);
    const QueueSubmissionToken prefixToken = transaction.packetToken(prefixPacket);
    ASSERT_TRUE(prefixToken.valid());

    // Submission failure remains visible to the caller. The helper neither discards the rejected normal packet nor
    // records the recovery tail, which leaves the accepted frontier available to the explicit recovery helper.
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
        EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInReadyFrontiers(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            recordingWorkers,
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

