// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The graph-owned normal executor derives and submits every ordinary packet, leaving the terminal frontier unrecorded
// for the caller's explicit finalization/recovery policy.
TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorSubmitsOrdinaryPrefix){
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

    bool firstShouldRecord = true;
    bool firstRecorded = false;
    const GpuTaskId firstTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_success_first"))
            .setMarkerLabel("Normal Executor Success First")
            .setQueue(graphicsQueue)
            .setScheduling(normalScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstShouldRecord,
            .attempted = &firstRecorded,
        }
    );
    ASSERT_TRUE(firstTask.valid());

    bool secondShouldRecord = true;
    bool secondRecorded = false;
    const GpuTaskId secondDependencies[] = { firstTask };
    const GpuTaskId secondTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_success_second"))
            .setMarkerLabel("Normal Executor Success Second")
            .setQueue(graphicsQueue)
            .setScheduling(normalScheduling)
            .setDependencies(secondDependencies, LengthOf(secondDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondShouldRecord,
            .attempted = &secondRecorded,
        }
    );
    ASSERT_TRUE(secondTask.valid());

    bool frontierShouldRecord = true;
    bool frontierRecorded = false;
    GpuTaskSchedulingHint frontierScheduling = normalScheduling;
    frontierScheduling.joinsAcceptedQueueFrontier = true;
    const GpuTaskId frontierTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_success_frontier"))
            .setMarkerLabel("Normal Executor Success Frontier")
            .setQueue(graphicsQueue)
            .setScheduling(frontierScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &frontierShouldRecord,
            .attempted = &frontierRecorded,
        }
    );
    ASSERT_TRUE(frontierTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/normal_executor_success_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    const GpuSubmissionPacketId frontierPacket = views.compiled.packetForTask(frontierTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(frontierPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 3u);
    EXPECT_EQ(views.compiled.packetIdAt(0u), firstPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), secondPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), frontierPacket);
    EXPECT_TRUE(views.compiled.packet(frontierPacket).plan->joinsAcceptedQueueFrontier);
    EXPECT_FALSE(views.compiled.packet(frontierPacket).plan->isRecoverySubmission);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    struct NormalGraphRecordedContext{
        GpuGraphSubmissionTransaction* transaction = nullptr;
        bool* firstRecorded = nullptr;
        bool* secondRecorded = nullptr;
        GpuSubmissionPacketId firstPacket;
        GpuSubmissionPacketId secondPacket;
        bool invokedBeforeSubmission = false;
    } recordedContext{
        .transaction = &transaction,
        .firstRecorded = &firstRecorded,
        .secondRecorded = &secondRecorded,
        .firstPacket = firstPacket,
        .secondPacket = secondPacket,
    };
    const auto observeNormalGraphRecording = [](
        void* const rawContext,
        const CommandListResourceStateHandoff* const finalState
    ) -> bool {
        static_cast<void>(finalState);
        NormalGraphRecordedContext* const context = static_cast<NormalGraphRecordedContext*>(rawContext);
        if(!context || !context->transaction || !context->firstRecorded || !context->secondRecorded)
            return false;
        context->invokedBeforeSubmission = *context->firstRecorded
            && *context->secondRecorded
            && !context->transaction->packetToken(context->firstPacket).valid()
            && !context->transaction->packetToken(context->secondPacket).valid()
        ;
        return context->invokedBeforeSubmission;
    };
    const GpuTaskGraphTaskRecordedCallback recordedCallback{
        .task = secondTask,
        .context = &recordedContext,
        .invoke = observeNormalGraphRecording,
    };
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.taskRecordedCallbacks = &recordedCallback;
    normalExecution.taskRecordedCallbackCount = 1u;
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(recordedContext.invokedBeforeSubmission);
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);
    EXPECT_FALSE(frontierRecorded);
    ASSERT_TRUE(transaction.packetToken(firstPacket).valid());
    ASSERT_TRUE(transaction.packetToken(secondPacket).valid());
    EXPECT_FALSE(transaction.packetToken(frontierPacket).valid());
    ASSERT_TRUE(submitter.recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        frontierTask,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(frontierRecorded);
    ASSERT_TRUE(transaction.packetToken(frontierPacket).valid());
    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(submissionStatistics.recoverySubmissionCount, 0u);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(device.waitForIdle());
}


// A semantic endpoint lets the normal executor stop before ordinary late tails without retaining a compiler packet
// identity. The endpoint includes its complete packet; every later ordinary/frontier packet remains caller-owned.
TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorStopsAtSemanticTerminalTask){
    auto& device = DescriptorBufferRoundTripTest::device();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool prefixShouldRecord = true;
    bool prefixRecorded = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_endpoint_prefix"))
            .setMarkerLabel("Normal Executor Endpoint Prefix")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &prefixShouldRecord,
            .attempted = &prefixRecorded,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    bool terminalShouldRecord = true;
    bool terminalRecorded = false;
    const GpuTaskId terminalDependencies[] = { prefixTask };
    const GpuTaskId terminalTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_endpoint_terminal"))
            .setMarkerLabel("Normal Executor Endpoint Terminal")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling)
            .setDependencies(terminalDependencies, LengthOf(terminalDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &terminalShouldRecord,
            .attempted = &terminalRecorded,
        }
    );
    ASSERT_TRUE(terminalTask.valid());

    bool lateTailShouldRecord = true;
    bool lateTailRecorded = false;
    const GpuTaskId lateTailDependencies[] = { terminalTask };
    const GpuTaskId lateTailTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_endpoint_late_tail"))
            .setMarkerLabel("Normal Executor Endpoint Late Tail")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling)
            .setDependencies(lateTailDependencies, LengthOf(lateTailDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &lateTailShouldRecord,
            .attempted = &lateTailRecorded,
        }
    );
    ASSERT_TRUE(lateTailTask.valid());

    bool frontierShouldRecord = true;
    bool frontierRecorded = false;
    GpuTaskSchedulingHint frontierScheduling = scheduling;
    frontierScheduling.joinsAcceptedQueueFrontier = true;
    const GpuTaskId frontierTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/normal_executor_endpoint_frontier"))
            .setMarkerLabel("Normal Executor Endpoint Frontier")
            .setQueue(graphicsQueue)
            .setScheduling(frontierScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &frontierShouldRecord,
            .attempted = &frontierRecorded,
        }
    );
    ASSERT_TRUE(frontierTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/normal_executor_endpoint_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId terminalPacket = views.compiled.packetForTask(terminalTask);
    const GpuSubmissionPacketId lateTailPacket = views.compiled.packetForTask(lateTailTask);
    const GpuSubmissionPacketId frontierPacket = views.compiled.packetForTask(frontierTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_TRUE(lateTailPacket.valid());
    ASSERT_TRUE(frontierPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 4u);
    EXPECT_EQ(views.compiled.packetIdAt(0u), prefixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), terminalPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), lateTailPacket);
    EXPECT_EQ(views.compiled.packetIdAt(3u), frontierPacket);

    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    GpuTaskGraphNormalExecutionDesc staleExecution;
    staleExecution.terminalTask = terminalTask;
    ++staleExecution.terminalTask.generation;
    GpuRecordedGraph staleRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction staleTransaction(DescriptorBufferRoundTripTest::arena());
    staleTransaction.reset(compiledGraph);
    GpuSubmissionPacketId staleFailedPacket;
    EXPECT_FALSE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        staleRecordedGraph,
        staleExecution,
        staleTransaction,
        scratchArena,
        &staleFailedPacket
    ));
    EXPECT_FALSE(staleFailedPacket.valid());
    EXPECT_EQ(staleRecordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(staleTransaction.hasAcceptedPackets());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = terminalTask;
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(prefixRecorded);
    EXPECT_TRUE(terminalRecorded);
    EXPECT_FALSE(lateTailRecorded);
    EXPECT_FALSE(frontierRecorded);
    ASSERT_TRUE(transaction.packetToken(prefixPacket).valid());
    ASSERT_TRUE(transaction.packetToken(terminalPacket).valid());
    EXPECT_FALSE(transaction.packetToken(lateTailPacket).valid());
    EXPECT_FALSE(transaction.packetToken(frontierPacket).valid());

    ASSERT_TRUE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        lateTailTask,
        nullptr,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(lateTailRecorded);
    EXPECT_TRUE(transaction.packetToken(lateTailPacket).valid());
    EXPECT_FALSE(frontierRecorded);
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

