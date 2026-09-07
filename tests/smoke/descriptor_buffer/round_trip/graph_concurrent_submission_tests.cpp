// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "submission_callbacks_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_FINAL)

struct NativeRecordedCallbackOperationContext{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuRecordedGraph* recordedGraph = nullptr;
    GpuTaskId task;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    const GpuTaskGraphSubmitter* submitter = nullptr;
    Alloc::ScratchArena* scratchArena = nullptr;
    AtomicFlag callbackEntered;
    AtomicFlag releaseCallback;
    GpuSubmissionPacketId reentrantFailedPacket;
    bool reentrantSubmissionResult = true;
};

#endif


#if !defined(NWB_FINAL)

[[nodiscard]] static bool RejectAfterBlockingNativeTaskRecordedOperation(
    void* const rawContext,
    const CommandListResourceStateHandoff* const finalState
){
    static_cast<void>(finalState);
    NativeRecordedCallbackOperationContext* const context =
        static_cast<NativeRecordedCallbackOperationContext*>(rawContext)
    ;
    if(!context)
        return false;

    if(
        context->graph
        && context->compiledGraph
        && context->recordedGraph
        && context->task.valid()
        && context->transaction
        && context->submitter
        && context->scratchArena
    ){
        context->reentrantSubmissionResult = context->submitter->submitTaskRangeInCompileOrder(
            *context->graph,
            *context->compiledGraph,
            *context->recordedGraph,
            context->task,
            context->task,
            nullptr,
            0u,
            nullptr,
            0u,
            *context->transaction,
            *context->scratchArena,
            &context->reentrantFailedPacket
        );
    }
    context->callbackEntered.test_and_set(MemoryOrder::release);
    context->callbackEntered.notify_all();
    while(!context->releaseCallback.test(MemoryOrder::acquire))
        context->releaseCallback.wait(false, MemoryOrder::acquire);
    return false;
}

#endif


#if !defined(NWB_FINAL)

// Independent ordinary packets may enter Vulkan while another packet is resolving its accepted callback. The
// transaction keeps the irreversible CPU resolution tail atomic, so neither packet token publishes until the
// blocked callback releases even though both native submissions have already completed their host call.
TEST_F(DescriptorBufferRoundTripTest, IndependentPacketsOverlapNativeSubmitBeforeSerializedPublication){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool firstShouldRecord = true;
    bool firstRecorded = false;
    const GpuTaskId firstTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/concurrent_native_submit_first"))
            .setMarkerLabel("Concurrent Native Submit First")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstShouldRecord,
            .attempted = &firstRecorded,
        }
    );
    bool secondShouldRecord = true;
    bool secondRecorded = false;
    const GpuTaskId secondTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/concurrent_native_submit_second"))
            .setMarkerLabel("Concurrent Native Submit Second")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondShouldRecord,
            .attempted = &secondRecorded,
        }
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/concurrent_native_submit_compile_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        device.getPhysicalQueueTopology(),
        assignments,
        compiledGraph,
        compileScratch
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(firstPacket, secondPacket);
    EXPECT_EQ(views.compiled.packet(firstPacket).plan->queue, graphicsQueue);
    EXPECT_EQ(views.compiled.packet(secondPacket).plan->queue, graphicsQueue);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        secondTask,
        recordedGraph
    ));
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    NativeTaskAcceptancePublicationBlocker publicationBlocker;
    const GpuTaskGraphTaskAcceptedCallback firstAcceptedCallback{
        .task = firstTask,
        .context = &publicationBlocker,
        .invoke = BlockIndependentTaskAcceptancePublication,
    };
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    bool firstSubmissionResult = false;
    bool secondSubmissionResult = false;
    GpuSubmissionPacketId firstFailedPacket;
    GpuSubmissionPacketId secondFailedPacket;
    Thread firstSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/concurrent_native_submit_first_thread_scratch")
        );
        firstSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            firstTask,
            firstTask,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            &firstFailedPacket,
            &firstAcceptedCallback,
            1u
        );
    });
    const Timer firstCallbackWaitBegin = TimerNow();
    while(
        !publicationBlocker.callbackEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), firstCallbackWaitBegin) < 5.0
    )
        YieldThread();
    const bool firstCallbackEntered = publicationBlocker.callbackEntered.test(MemoryOrder::acquire);
    if(!firstCallbackEntered){
        publicationBlocker.releaseCallback.test_and_set(MemoryOrder::release);
        publicationBlocker.releaseCallback.notify_all();
        firstSubmissionThread.join();
        EXPECT_TRUE(firstCallbackEntered);
        return;
    }
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    EXPECT_FALSE(transaction.packetToken(firstPacket).valid());

    Thread secondSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/concurrent_native_submit_second_thread_scratch")
        );
        secondSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            secondTask,
            secondTask,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            &secondFailedPacket
        );
    });
    const Timer overlapWaitBegin = TimerNow();
    while(
        submissionObserver.capturedSubmissionCount() < 2u
        && DurationInSeconds<f64>(TimerNow(), overlapWaitBegin) < 5.0
    )
        YieldThread();
    const bool nativeSubmissionsOverlapped = submissionObserver.capturedSubmissionCount() >= 2u;

    EXPECT_FALSE(transaction.packetToken(firstPacket).valid());
    EXPECT_FALSE(transaction.packetToken(secondPacket).valid());
    publicationBlocker.releaseCallback.test_and_set(MemoryOrder::release);
    publicationBlocker.releaseCallback.notify_all();
    firstSubmissionThread.join();
    secondSubmissionThread.join();

    EXPECT_TRUE(nativeSubmissionsOverlapped);
    EXPECT_TRUE(firstSubmissionResult);
    EXPECT_TRUE(secondSubmissionResult);
    EXPECT_FALSE(firstFailedPacket.valid());
    EXPECT_FALSE(secondFailedPacket.valid());
    const QueueSubmissionToken firstToken = transaction.packetToken(firstPacket);
    const QueueSubmissionToken secondToken = transaction.packetToken(secondPacket);
    ASSERT_TRUE(firstToken.valid());
    ASSERT_TRUE(secondToken.valid());
    EXPECT_EQ(firstToken.value, publicationBlocker.acceptedToken.value);
    EXPECT_TRUE(firstToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_TRUE(secondToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_GT(secondToken.value, firstToken.value);
    EXPECT_FALSE(submissionObserver.overflowed());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.acceptedPacketCount, 2u);
    EXPECT_EQ(statistics.nativeSubmissionCount, 2u);
    EXPECT_TRUE(device.waitForIdle());
}

#endif


#if !defined(NWB_FINAL)

// A composite retains one cross-thread transaction operation from recording-attempt binding through its recorded
// callback decision. Same-thread and worker-equivalent cross-thread reentry fail immediately, so a false callback
// terminally rejects the task before any native submission can overtake it.
TEST_F(DescriptorBufferRoundTripTest, CompositeRecordedCallbackRejectionOwnsCrossThreadTransactionAdmission){
    auto& device = DescriptorBufferRoundTripTest::device();
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool shouldRecord = true;
    bool recorded = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/composite_recorded_callback_operation"))
            .setMarkerLabel("Composite Recorded Callback Operation")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &recorded,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(
        Name("tests/descriptor_buffer/composite_recorded_callback_operation_compile_scratch")
    );
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId packet;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 1u);
        packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    NativeRecordedCallbackOperationContext callbackContext;
    callbackContext.graph = &graph;
    callbackContext.compiledGraph = &compiledGraph;
    callbackContext.recordedGraph = &recordedGraph;
    callbackContext.task = task;
    callbackContext.transaction = &transaction;
    callbackContext.submitter = &submitter;
    const GpuTaskGraphTaskRecordedCallback recordedCallback{
        .task = task,
        .context = &callbackContext,
        .invoke = RejectAfterBlockingNativeTaskRecordedOperation,
    };

    bool compositeResult = true;
    GpuSubmissionPacketId compositeFailedPacket;
    Thread compositeThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/composite_recorded_callback_operation_thread_scratch")
        );
        callbackContext.scratchArena = &submissionScratch;
        compositeResult = submitter.recordAndSubmitTask(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            task,
            &recordedCallback,
            transaction,
            submissionScratch,
            &compositeFailedPacket
        );
    });
    const Timer callbackWaitBegin = TimerNow();
    while(
        !callbackContext.callbackEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), callbackWaitBegin) < 5.0
    )
        YieldThread();
    const bool callbackEntered = callbackContext.callbackEntered.test(MemoryOrder::acquire);
    if(!callbackEntered){
        callbackContext.releaseCallback.test_and_set(MemoryOrder::release);
        callbackContext.releaseCallback.notify_all();
        compositeThread.join();
        EXPECT_TRUE(callbackEntered);
        return;
    }

    EXPECT_TRUE(recorded);
    EXPECT_FALSE(callbackContext.reentrantSubmissionResult);
    EXPECT_FALSE(callbackContext.reentrantFailedPacket.valid());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 0u);

    AtomicFlag competingStarted;
    AtomicFlag competingReturned;
    bool competingResult = true;
    GpuSubmissionPacketId competingFailedPacket;
    Thread competingThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/composite_recorded_callback_competing_thread_scratch")
        );
        competingStarted.test_and_set(MemoryOrder::release);
        competingStarted.notify_all();
        competingResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            task,
            task,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            &competingFailedPacket
        );
        competingReturned.test_and_set(MemoryOrder::release);
        competingReturned.notify_all();
    });
    const Timer competingStartWaitBegin = TimerNow();
    while(
        !competingStarted.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), competingStartWaitBegin) < 5.0
    )
        YieldThread();
    const bool competitorStarted = competingStarted.test(MemoryOrder::acquire);
    if(!competitorStarted){
        callbackContext.releaseCallback.test_and_set(MemoryOrder::release);
        callbackContext.releaseCallback.notify_all();
        compositeThread.join();
        competingThread.join();
        EXPECT_TRUE(competitorStarted);
        return;
    }

    const Timer competingReturnWaitBegin = TimerNow();
    while(
        !competingReturned.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), competingReturnWaitBegin) < 5.0
    )
        YieldThread();
    const bool competitorReturnedBeforeRelease = competingReturned.test(MemoryOrder::acquire);
    EXPECT_TRUE(competitorReturnedBeforeRelease);
    EXPECT_FALSE(competingResult);
    EXPECT_FALSE(competingFailedPacket.valid());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 0u);

    callbackContext.releaseCallback.test_and_set(MemoryOrder::release);
    callbackContext.releaseCallback.notify_all();
    compositeThread.join();
    competingThread.join();

    EXPECT_FALSE(compositeResult);
    EXPECT_EQ(compositeFailedPacket, packet);
    EXPECT_FALSE(competingResult);
    EXPECT_FALSE(competingFailedPacket.valid());
    EXPECT_TRUE(competingReturned.test(MemoryOrder::acquire));
    const QueueSubmissionToken token = transaction.packetToken(packet);
    EXPECT_FALSE(token.valid());
    EXPECT_FALSE(submissionObserver.overflowed());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 0u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 0u);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.acceptedPacketCount, 0u);
    EXPECT_EQ(statistics.rejectedPacketCount, 1u);
    EXPECT_EQ(statistics.rejectedTaskCount, 1u);
    EXPECT_EQ(statistics.nativeSubmissionCount, 0u);
    EXPECT_TRUE(device.waitForIdle());
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

