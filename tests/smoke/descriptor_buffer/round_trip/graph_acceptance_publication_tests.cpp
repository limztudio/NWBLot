// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "submission_callbacks_test_support.h"
#include "submission_signals_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


#if !defined(NWB_FINAL)

struct NativeTaskSubmissionSerializationContext{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuRecordedGraph* recordedGraph = nullptr;
    GpuTaskId task;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    const GpuTaskGraphSubmitter* submitter = nullptr;
    Alloc::ScratchArena* scratchArena = nullptr;
    NativeTaskAcceptanceOrder* acceptanceOrder = nullptr;
    u32 taskCallbackOrderMarker = 0u;
    AtomicFlag taskCallbackEntered;
    AtomicFlag releaseTaskCallback;
    QueueSubmissionToken typedAcceptedToken;
    QueueSubmissionToken taskAcceptedToken;
    u32 typedAcceptedCount = 0u;
    u32 taskAcceptedCount = 0u;
    bool reentrantSubmissionResult = true;
    bool callbackInputsValid = false;
    bool taskTokenHiddenDuringCallback = false;
    bool acceptedFrontierHiddenDuringCallback = false;
    bool acceptanceOrderOverflow = false;
};

#endif


#if !defined(NWB_FINAL)

struct NativeTaskSubmissionSerializationTask{
    struct Payload{
        NativeTaskSubmissionSerializationContext* context = nullptr;
        u32 acceptanceOrderMarker = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.context && commandList.isRecording();
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        NativeTaskSubmissionSerializationContext* const context = payload.context;
        if(!context)
            return;

        context->typedAcceptedToken = token;
        ++context->typedAcceptedCount;
        if(context->acceptanceOrder){
            if(context->acceptanceOrder->invocationCount < LengthOf(context->acceptanceOrder->markers)){
                context->acceptanceOrder->markers[context->acceptanceOrder->invocationCount++] =
                    payload.acceptanceOrderMarker
                ;
            }
            else{
                context->acceptanceOrderOverflow = true;
            }
        }
    }
};

#endif


#if !defined(NWB_FINAL)

[[nodiscard]] static bool BlockNativeTaskAcceptedPublication(
    void* const rawContext,
    const QueueSubmissionToken& token
){
    NativeTaskSubmissionSerializationContext* const context =
        static_cast<NativeTaskSubmissionSerializationContext*>(rawContext)
    ;
    if(!context)
        return false;

    if(
        !context->graph
        || !context->compiledGraph
        || !context->recordedGraph
        || !context->transaction
        || !context->submitter
        || !context->scratchArena
    ){
        context->callbackInputsValid = false;
        return false;
    }
    const GpuTaskGraphReadViews views(*context->graph, *context->compiledGraph);
    context->callbackInputsValid =
        views.valid()
        && views.declarations.validTask(context->task)
        && views.compiled.findTask(context->task).valid()
        && token.valid()
    ;
    if(context->callbackInputsValid){
        context->taskAcceptedToken = token;
        ++context->taskAcceptedCount;
        context->taskTokenHiddenDuringCallback = !context->transaction->taskToken(
            views.compiled,
            context->task
        ).valid();
        context->acceptedFrontierHiddenDuringCallback = !context->transaction->hasAcceptedPackets();
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
            *context->scratchArena
        );
        if(context->acceptanceOrder){
            if(context->acceptanceOrder->invocationCount < LengthOf(context->acceptanceOrder->markers)){
                context->acceptanceOrder->markers[context->acceptanceOrder->invocationCount++] =
                    context->taskCallbackOrderMarker
                ;
            }
            else{
                context->acceptanceOrderOverflow = true;
            }
        }
    }
    context->taskCallbackEntered.test_and_set(MemoryOrder::release);
    context->taskCallbackEntered.notify_all();
    while(!context->releaseTaskCallback.test(MemoryOrder::acquire))
        context->releaseTaskCallback.wait(false, MemoryOrder::acquire);
    return false;
}

#endif


#if !defined(NWB_FINAL)

struct NativeCrossTransactionSubmissionContext{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuRecordedGraph* recordedGraph = nullptr;
    GpuTaskId nestedTask;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    const GpuTaskGraphSubmitter* submitter = nullptr;
    Alloc::ScratchArena* scratchArena = nullptr;
    AtomicFlag callbackEntered;
    AtomicFlag callbackReturned;
    bool nestedSubmissionResult = true;
};

#endif


#if !defined(NWB_FINAL)

[[nodiscard]] static bool SubmitCrossTransactionTaskFromAcceptance(
    void* const rawContext,
    const QueueSubmissionToken& token
){
    NativeCrossTransactionSubmissionContext* const context =
        static_cast<NativeCrossTransactionSubmissionContext*>(rawContext)
    ;
    if(
        !context
        || !token.valid()
        || !context->graph
        || !context->compiledGraph
        || !context->recordedGraph
        || !context->nestedTask.valid()
        || !context->transaction
        || !context->submitter
        || !context->scratchArena
    )
        return false;

    context->callbackEntered.test_and_set(MemoryOrder::release);
    context->callbackEntered.notify_all();
    context->nestedSubmissionResult = context->submitter->submitTaskRangeInCompileOrder(
        *context->graph,
        *context->compiledGraph,
        *context->recordedGraph,
        context->nestedTask,
        context->nestedTask,
        nullptr,
        0u,
        nullptr,
        0u,
        *context->transaction,
        *context->scratchArena
    );
    context->callbackReturned.test_and_set(MemoryOrder::release);
    context->callbackReturned.notify_all();
    return true;
}

#endif


#if !defined(NWB_FINAL)

// One transaction is held inside its accepted callback while another accepted callback attempts nested work in it.
// Cross-transaction nesting must fail before the held callback is released; bounded observation and unconditional
// cleanup make the test report a blocking regression instead of deadlocking the suite.
TEST_F(DescriptorBufferRoundTripTest, CrossTransactionAcceptanceReentryReturnsWhenTargetBusy){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
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

    GpuTaskGraph firstGraph(DescriptorBufferRoundTripTest::arena());
    bool firstOuterShouldRecord = true;
    bool firstOuterRecorded = false;
    const GpuTaskId firstOuterTask = firstGraph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_transaction_first_outer"))
            .setMarkerLabel("Cross Transaction First Outer")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstOuterShouldRecord,
            .attempted = &firstOuterRecorded,
        }
    );
    bool firstNestedShouldRecord = true;
    bool firstNestedRecorded = false;
    const GpuTaskId firstNestedTask = firstGraph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_transaction_first_nested"))
            .setMarkerLabel("Cross Transaction First Nested")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstNestedShouldRecord,
            .attempted = &firstNestedRecorded,
        }
    );
    ASSERT_TRUE(firstOuterTask.valid());
    ASSERT_TRUE(firstNestedTask.valid());

    GpuTaskGraph secondGraph(DescriptorBufferRoundTripTest::arena());
    bool secondOuterShouldRecord = true;
    bool secondOuterRecorded = false;
    const GpuTaskId secondOuterTask = secondGraph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_transaction_second_outer"))
            .setMarkerLabel("Cross Transaction Second Outer")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondOuterShouldRecord,
            .attempted = &secondOuterRecorded,
        }
    );
    bool secondNestedShouldRecord = true;
    bool secondNestedRecorded = false;
    const GpuTaskId secondNestedTask = secondGraph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_transaction_second_nested"))
            .setMarkerLabel("Cross Transaction Second Nested")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondNestedShouldRecord,
            .attempted = &secondNestedRecorded,
        }
    );
    ASSERT_TRUE(secondOuterTask.valid());
    ASSERT_TRUE(secondNestedTask.valid());

    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphAnalysis firstAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments firstAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph firstCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena firstCompileScratch(
        Name("tests/descriptor_buffer/cross_transaction_first_compile_scratch")
    );
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(firstGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            firstAnalysis,
            device.getPhysicalQueueTopology(),
            firstAssignments,
            firstCompiledGraph,
            firstCompileScratch
        ));
    }
    GpuTaskGraphAnalysis secondAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments secondAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph secondCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena secondCompileScratch(
        Name("tests/descriptor_buffer/cross_transaction_second_compile_scratch")
    );
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(secondGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            secondAnalysis,
            device.getPhysicalQueueTopology(),
            secondAssignments,
            secondCompiledGraph,
            secondCompileScratch
        ));
    }
    {
        const GpuTaskGraphReadViews firstViews(firstGraph, firstCompiledGraph);
        ASSERT_TRUE(firstViews.valid());
        ASSERT_EQ(firstViews.compiled.packetCount(), 2u);
    }
    {
        const GpuTaskGraphReadViews secondViews(secondGraph, secondCompiledGraph);
        ASSERT_TRUE(secondViews.valid());
        ASSERT_EQ(secondViews.compiled.packetCount(), 2u);
    }

    GpuRecordedGraph firstRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuRecordedGraph secondRecordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        firstGraph,
        firstCompiledGraph,
        firstOuterTask,
        firstNestedTask,
        firstRecordedGraph
    ));
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        secondGraph,
        secondCompiledGraph,
        secondOuterTask,
        secondNestedTask,
        secondRecordedGraph
    ));
    EXPECT_TRUE(firstOuterRecorded);
    EXPECT_TRUE(firstNestedRecorded);
    EXPECT_TRUE(secondOuterRecorded);
    EXPECT_TRUE(secondNestedRecorded);

    GpuGraphSubmissionTransaction firstTransaction(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction secondTransaction(DescriptorBufferRoundTripTest::arena());
    firstTransaction.reset(firstCompiledGraph);
    secondTransaction.reset(secondCompiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    NativeCrossTransactionSubmissionContext firstContext;
    firstContext.graph = &secondGraph;
    firstContext.compiledGraph = &secondCompiledGraph;
    firstContext.recordedGraph = &secondRecordedGraph;
    firstContext.nestedTask = secondNestedTask;
    firstContext.transaction = &secondTransaction;
    firstContext.submitter = &submitter;
    NativeTaskAcceptancePublicationBlocker secondPublicationBlocker;
    const GpuTaskGraphTaskAcceptedCallback firstCallback{
        .task = firstOuterTask,
        .context = &firstContext,
        .invoke = SubmitCrossTransactionTaskFromAcceptance,
    };
    const GpuTaskGraphTaskAcceptedCallback secondCallback{
        .task = secondOuterTask,
        .context = &secondPublicationBlocker,
        .invoke = BlockIndependentTaskAcceptancePublication,
    };

    bool firstOuterSubmissionResult = false;
    bool secondOuterSubmissionResult = false;
    Thread secondOuterSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/cross_transaction_second_outer_thread_scratch")
        );
        secondOuterSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            secondGraph,
            secondCompiledGraph,
            secondRecordedGraph,
            secondOuterTask,
            secondOuterTask,
            nullptr,
            0u,
            nullptr,
            0u,
            secondTransaction,
            submissionScratch,
            nullptr,
            &secondCallback,
            1u
        );
    });
    const Timer secondCallbackWaitBegin = TimerNow();
    while(
        !secondPublicationBlocker.callbackEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), secondCallbackWaitBegin) < 5.0
    )
        YieldThread();
    const bool secondCallbackEntered = secondPublicationBlocker.callbackEntered.test(MemoryOrder::acquire);
    if(!secondCallbackEntered){
        secondPublicationBlocker.releaseCallback.test_and_set(MemoryOrder::release);
        secondPublicationBlocker.releaseCallback.notify_all();
        secondOuterSubmissionThread.join();
        EXPECT_TRUE(secondCallbackEntered);
        return;
    }

    Thread firstOuterSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/cross_transaction_first_outer_thread_scratch")
        );
        firstContext.scratchArena = &submissionScratch;
        firstOuterSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            firstGraph,
            firstCompiledGraph,
            firstRecordedGraph,
            firstOuterTask,
            firstOuterTask,
            nullptr,
            0u,
            nullptr,
            0u,
            firstTransaction,
            submissionScratch,
            nullptr,
            &firstCallback,
            1u
        );
    });
    const Timer nestedAttemptWaitBegin = TimerNow();
    while(
        !firstContext.callbackReturned.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), nestedAttemptWaitBegin) < 5.0
    )
        YieldThread();
    const bool nestedAttemptReturnedBeforeRelease = firstContext.callbackReturned.test(MemoryOrder::acquire);

    secondPublicationBlocker.releaseCallback.test_and_set(MemoryOrder::release);
    secondPublicationBlocker.releaseCallback.notify_all();
    secondOuterSubmissionThread.join();
    firstOuterSubmissionThread.join();

    EXPECT_TRUE(nestedAttemptReturnedBeforeRelease);
    EXPECT_TRUE(firstOuterSubmissionResult);
    EXPECT_TRUE(secondOuterSubmissionResult);
    EXPECT_FALSE(firstContext.nestedSubmissionResult);
    EXPECT_TRUE(firstContext.callbackEntered.test(MemoryOrder::acquire));
    EXPECT_TRUE(firstContext.callbackReturned.test(MemoryOrder::acquire));
    {
        const GpuTaskGraphReadViews firstViews(firstGraph, firstCompiledGraph);
        ASSERT_TRUE(firstViews.valid());
        EXPECT_FALSE(firstTransaction.taskToken(firstViews.compiled, firstNestedTask).valid());
    }
    {
        const GpuTaskGraphReadViews secondViews(secondGraph, secondCompiledGraph);
        ASSERT_TRUE(secondViews.valid());
        EXPECT_FALSE(secondTransaction.taskToken(secondViews.compiled, secondNestedTask).valid());
    }

    Alloc::ScratchArena firstRetryScratch(
        Name("tests/descriptor_buffer/cross_transaction_first_retry_scratch")
    );
    Alloc::ScratchArena secondRetryScratch(
        Name("tests/descriptor_buffer/cross_transaction_second_retry_scratch")
    );
    EXPECT_TRUE(submitter.submitTaskRangeInCompileOrder(
        firstGraph,
        firstCompiledGraph,
        firstRecordedGraph,
        firstNestedTask,
        firstNestedTask,
        nullptr,
        0u,
        nullptr,
        0u,
        firstTransaction,
        firstRetryScratch
    ));
    EXPECT_TRUE(submitter.submitTaskRangeInCompileOrder(
        secondGraph,
        secondCompiledGraph,
        secondRecordedGraph,
        secondNestedTask,
        secondNestedTask,
        nullptr,
        0u,
        nullptr,
        0u,
        secondTransaction,
        secondRetryScratch
    ));
    {
        const GpuTaskGraphReadViews firstViews(firstGraph, firstCompiledGraph);
        ASSERT_TRUE(firstViews.valid());
        EXPECT_TRUE(firstTransaction.taskToken(firstViews.compiled, firstNestedTask).valid());
    }
    {
        const GpuTaskGraphReadViews secondViews(secondGraph, secondCompiledGraph);
        ASSERT_TRUE(secondViews.valid());
        EXPECT_TRUE(secondTransaction.taskToken(secondViews.compiled, secondNestedTask).valid());
    }
    EXPECT_TRUE(device.waitForIdle());
}

#endif


#if !defined(NWB_FINAL)

// The native Transfer packet has accepted and both typed hooks have reached graph Accepted before the first semantic
// task callback blocks. The transaction token/frontier must remain hidden until every task callback finishes;
// a concurrent Graphics recovery then joins the exact published Transfer packet. A false callback stops the later
// ordinary packet without losing that accepted publication, and same-transaction reentry remains fail-fast.
TEST_F(DescriptorBufferRoundTripTest, NativeTaskAcceptedCallbacksGateAcceptedFrontierPublication){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Submission serialization: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId transferQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    if(
        !device.getQueue(CommandQueue::Transfer)
        || !transferQueue.valid()
        || transferQueue == graphicsQueue
        || device.getQueueFamilyIndex(transferQueue) == device.getQueueFamilyIndex(graphicsQueue)
    ){
        GTEST_SKIP() << "Submission serialization: adapter has no dedicated transfer-only queue family.";
    }
    ASSERT_TRUE(graphicsQueue.valid());

    GpuTaskGraph graph(transferScope.arena());
    const GpuQueueRequest transferRequest{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        false,
        false,
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint transferScheduling;
    transferScheduling.cost = GpuTaskCostHint::Tiny;
    transferScheduling.allowPacketMerge = true;

    NativeTaskAcceptanceOrder acceptanceOrder;
    NativeTaskSubmissionSerializationContext serializationContext;
    serializationContext.acceptanceOrder = &acceptanceOrder;
    serializationContext.taskCallbackOrderMarker = 3u;
    const GpuTaskId transferTask = graph.addTask<NativeTaskSubmissionSerializationTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/submission_serialization_transfer"))
            .setMarkerLabel("Submission Serialization Transfer")
            .setQueue(transferRequest)
            .setScheduling(transferScheduling),
        NativeTaskSubmissionSerializationTask::Payload{
            .context = &serializationContext,
            .acceptanceOrderMarker = 1u,
        }
    );
    ASSERT_TRUE(transferTask.valid());

    const GpuTaskId mergedTransferDependencies[] = { transferTask };
    GpuTaskSchedulingHint mergedTransferScheduling = transferScheduling;
    mergedTransferScheduling.mergeWithPrevious = true;
    const GpuTaskId mergedTransferTask = graph.addTask<NativeTaskSubmissionSerializationTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/submission_serialization_merged_transfer"))
            .setMarkerLabel("Submission Serialization Merged Transfer")
            .setQueue(transferRequest)
            .setScheduling(mergedTransferScheduling)
            .setDependencies(mergedTransferDependencies, LengthOf(mergedTransferDependencies)),
        NativeTaskSubmissionSerializationTask::Payload{
            .context = &serializationContext,
            .acceptanceOrderMarker = 2u,
        }
    );
    ASSERT_TRUE(mergedTransferTask.valid());

    GpuTaskSchedulingHint packetScheduling;
    packetScheduling.cost = GpuTaskCostHint::Tiny;
    packetScheduling.forceSubmissionBoundary = true;
    packetScheduling.allowPacketMerge = false;
    const GpuTaskId suffixDependencies[] = { mergedTransferTask };
    bool suffixShouldRecord = true;
    bool suffixRecorded = false;
    const GpuTaskId suffixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/submission_serialization_suffix"))
            .setMarkerLabel("Submission Serialization Suffix")
            .setQueue(graphicsRequest)
            .setScheduling(packetScheduling)
            .setDependencies(suffixDependencies, LengthOf(suffixDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &suffixShouldRecord,
            .attempted = &suffixRecorded,
        }
    );
    ASSERT_TRUE(suffixTask.valid());

    bool recoveryShouldRecord = true;
    bool recoveryRecorded = false;
    GpuTaskSchedulingHint recoveryScheduling = packetScheduling;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const GpuTaskId recoveryTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/submission_serialization_recovery"))
            .setMarkerLabel("Submission Serialization Recovery")
            .setQueue(graphicsRequest)
            .setScheduling(recoveryScheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &recoveryShouldRecord,
            .attempted = &recoveryRecorded,
        }
    );
    ASSERT_TRUE(recoveryTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(transferScope.arena());
    GpuTaskGraphQueueAssignments assignments(transferScope.arena());
    GpuCompiledGraph compiledGraph(transferScope.arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/submission_serialization_compile_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, compileScratch));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);

    const GpuTaskQueueAssignment* const transferAssignment = assignments.find(transferTask);
    const GpuTaskQueueAssignment* const mergedTransferAssignment = assignments.find(mergedTransferTask);
    const GpuTaskQueueAssignment* const suffixAssignment = assignments.find(suffixTask);
    const GpuTaskQueueAssignment* const recoveryAssignment = assignments.find(recoveryTask);
    ASSERT_NE(transferAssignment, nullptr);
    ASSERT_NE(mergedTransferAssignment, nullptr);
    ASSERT_NE(suffixAssignment, nullptr);
    ASSERT_NE(recoveryAssignment, nullptr);
    EXPECT_EQ(transferAssignment->queue, transferQueue);
    EXPECT_EQ(mergedTransferAssignment->queue, transferQueue);
    EXPECT_EQ(suffixAssignment->queue, graphicsQueue);
    EXPECT_EQ(recoveryAssignment->queue, graphicsQueue);
    const GpuSubmissionPacketId transferPacket = views.compiled.packetForTask(transferTask);
    const GpuSubmissionPacketId suffixPacket = views.compiled.packetForTask(suffixTask);
    const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
    ASSERT_TRUE(transferPacket.valid());
    ASSERT_EQ(views.compiled.packetForTask(mergedTransferTask), transferPacket);
    ASSERT_TRUE(suffixPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    ASSERT_NE(transferPacket, suffixPacket);
    ASSERT_NE(transferPacket, recoveryPacket);
    ASSERT_NE(suffixPacket, recoveryPacket);
    const GpuTaskId* const transferPacketTasks = views.compiled.packet(transferPacket).tasks;
    ASSERT_NE(transferPacketTasks, nullptr);
    ASSERT_EQ(views.compiled.packet(transferPacket).plan->taskCount, 2u);
    EXPECT_EQ(transferPacketTasks[0u], transferTask);
    EXPECT_EQ(transferPacketTasks[1u], mergedTransferTask);
    ASSERT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);
    ASSERT_EQ(views.compiled.packet(recoveryPacket).plan->dependencyCount, 0u);
    const GpuSubmissionPacketRange ordinaryRange = views.compiled.packetRangeForTasks(transferTask, suffixTask);
    ASSERT_TRUE(ordinaryRange.valid());
    ASSERT_EQ(ordinaryRange.first, transferPacket);
    ASSERT_EQ(ordinaryRange.packetCount, 2u);

    GpuRecordedGraph recordedGraph(transferScope.arena());
    GpuGraphSubmissionTransaction transaction(transferScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        transferTask,
        suffixTask,
        recordedGraph
    ));
    EXPECT_TRUE(suffixRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    serializationContext.graph = &graph;
    serializationContext.compiledGraph = &compiledGraph;
    serializationContext.recordedGraph = &recordedGraph;
    serializationContext.task = transferTask;
    serializationContext.transaction = &transaction;
    serializationContext.submitter = &submitter;

    NativeTaskAcceptanceObserver secondTaskAcceptance{
        .acceptedCount = 0u,
        .lastToken = {},
        .order = &acceptanceOrder,
        .orderMarker = 4u,
    };
    const GpuTaskGraphTaskAcceptedCallback taskAcceptedCallbacks[] = {
        GpuTaskGraphTaskAcceptedCallback{
            .task = transferTask,
            .context = &serializationContext,
            .invoke = BlockNativeTaskAcceptedPublication,
        },
        GpuTaskGraphTaskAcceptedCallback{
            .task = mergedTransferTask,
            .context = &secondTaskAcceptance,
            .invoke = ObserveNativeTaskAcceptance,
        },
    };
    GpuSubmissionPacketId failedSubmissionPacket;
    bool rangeSubmissionResult = true;
    const VkQueue nativeTransferQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, transferQueue).pointer()
    );
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeTransferQueue, VK_NULL_HANDLE);
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    Thread rangeSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/submission_serialization_transfer_thread_scratch")
        );
        serializationContext.scratchArena = &submissionScratch;
        rangeSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            transferTask,
            suffixTask,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            &failedSubmissionPacket,
            taskAcceptedCallbacks,
            LengthOf(taskAcceptedCallbacks)
        );
    });

    while(!serializationContext.taskCallbackEntered.test(MemoryOrder::acquire))
        serializationContext.taskCallbackEntered.wait(false, MemoryOrder::acquire);
    EXPECT_TRUE(serializationContext.callbackInputsValid);
    EXPECT_FALSE(serializationContext.acceptanceOrderOverflow);
    EXPECT_EQ(serializationContext.typedAcceptedCount, 2u);
    EXPECT_EQ(serializationContext.taskAcceptedCount, 1u);
    EXPECT_TRUE(serializationContext.typedAcceptedToken.valid());
    EXPECT_TRUE(serializationContext.taskAcceptedToken.valid());
    EXPECT_TRUE(serializationContext.taskAcceptedToken.matchesPhysicalQueue(
        transferQueue.index,
        transferQueue.deviceGeneration
    ));
    EXPECT_EQ(serializationContext.typedAcceptedToken.value, serializationContext.taskAcceptedToken.value);
    EXPECT_FALSE(serializationContext.reentrantSubmissionResult);
    EXPECT_TRUE(serializationContext.taskTokenHiddenDuringCallback);
    EXPECT_TRUE(serializationContext.acceptedFrontierHiddenDuringCallback);
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    EXPECT_FALSE(transaction.packetToken(transferPacket).valid());
    EXPECT_EQ(secondTaskAcceptance.acceptedCount, 0u);
    ASSERT_EQ(acceptanceOrder.invocationCount, 3u);
    EXPECT_EQ(acceptanceOrder.markers[0u], 1u);
    EXPECT_EQ(acceptanceOrder.markers[1u], 2u);
    EXPECT_EQ(acceptanceOrder.markers[2u], 3u);

    AtomicFlag recoveryReadyToSubmit;
    bool recoverySubmissionResult = false;
    Thread recoverySubmissionThread([&](){
        Alloc::ScratchArena recoveryScratch(
            Name("tests/descriptor_buffer/submission_serialization_recovery_thread_scratch")
        );
        recoveryReadyToSubmit.test_and_set(MemoryOrder::release);
        recoveryReadyToSubmit.notify_all();
        recoverySubmissionResult = submitter.recordAndSubmitAcceptedFrontierTask(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            recoveryTask,
            transaction,
            recoveryScratch
        );
    });

    // The public transaction contract deliberately exposes no internal waiter state. Synchronize at the call
    // boundary, then prove after both threads join that recovery consumed the newly accepted exact-queue frontier.
    while(!recoveryReadyToSubmit.test(MemoryOrder::acquire))
        recoveryReadyToSubmit.wait(false, MemoryOrder::acquire);

    serializationContext.releaseTaskCallback.test_and_set(MemoryOrder::release);
    serializationContext.releaseTaskCallback.notify_all();
    rangeSubmissionThread.join();
    recoverySubmissionThread.join();

    EXPECT_FALSE(rangeSubmissionResult);
    EXPECT_EQ(failedSubmissionPacket, transferPacket);
    EXPECT_TRUE(recoverySubmissionResult);
    EXPECT_TRUE(recoveryRecorded);
    EXPECT_EQ(secondTaskAcceptance.acceptedCount, 1u);
    EXPECT_FALSE(serializationContext.acceptanceOrderOverflow);
    ASSERT_EQ(acceptanceOrder.invocationCount, 4u);
    EXPECT_EQ(acceptanceOrder.markers[0u], 1u);
    EXPECT_EQ(acceptanceOrder.markers[1u], 2u);
    EXPECT_EQ(acceptanceOrder.markers[2u], 3u);
    EXPECT_EQ(acceptanceOrder.markers[3u], 4u);
    const QueueSubmissionToken transferToken = transaction.packetToken(transferPacket);
    const QueueSubmissionToken suffixToken = transaction.packetToken(suffixPacket);
    const QueueSubmissionToken recoveryToken = transaction.packetToken(recoveryPacket);
    ASSERT_TRUE(transferToken.valid());
    EXPECT_FALSE(suffixToken.valid());
    ASSERT_TRUE(recoveryToken.valid());
    EXPECT_EQ(transferToken.value, serializationContext.typedAcceptedToken.value);
    EXPECT_EQ(transferToken.value, serializationContext.taskAcceptedToken.value);
    EXPECT_EQ(transferToken.value, secondTaskAcceptance.lastToken.value);
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 1u);
    VulkanTestQueueSubmit2Capture transferCapture;
    VulkanTestQueueSubmit2Capture recoveryCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(0u, transferCapture));
    ASSERT_TRUE(submissionObserver.capturedSubmission(1u, recoveryCapture));
    __hidden_descriptor_buffer_round_trip_tests::ExpectNativeTimelineDependency(
        transferCapture,
        nativeTransferQueue,
        transferToken,
        recoveryCapture,
        nativeGraphicsQueue,
        recoveryToken
    );

    const u32 acceptedOrderCount = acceptanceOrder.invocationCount;
    Alloc::ScratchArena retryScratch(Name("tests/descriptor_buffer/submission_serialization_retry_scratch"));
    GpuSubmissionPacketId retryFailedPacket;
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        transferTask,
        suffixTask,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        retryScratch,
        &retryFailedPacket,
        taskAcceptedCallbacks,
        LengthOf(taskAcceptedCallbacks)
    ));
    EXPECT_EQ(retryFailedPacket, transferPacket);
    EXPECT_EQ(serializationContext.typedAcceptedCount, 2u);
    EXPECT_EQ(serializationContext.taskAcceptedCount, 1u);
    EXPECT_EQ(secondTaskAcceptance.acceptedCount, 1u);
    EXPECT_EQ(acceptanceOrder.invocationCount, acceptedOrderCount);

    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_FALSE(transaction.packetToken(suffixPacket).valid());
    ASSERT_TRUE(device.waitForIdle());
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

