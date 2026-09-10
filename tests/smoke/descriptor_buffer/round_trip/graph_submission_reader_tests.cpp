// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ReentrantGraphSubmissionContext{
    CommandList* commandList = nullptr;
    QueueSubmissionNativeSignal signal;
    QueueSubmissionToken nestedToken;
    u32 invocationCount = 0u;
    bool publicStatusDenied = false;
    bool publicMutationAttempted = false;
};


[[nodiscard]] static bool ReenterPublicGraphCommandListSubmission(
    void* const rawContext,
    const u64,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    ReentrantGraphSubmissionContext* const context = static_cast<ReentrantGraphSubmissionContext*>(rawContext);
    if(!context || !context->commandList || !executionQueue.valid() || !context->signal.valid())
        return false;

    ++context->invocationCount;
    context->publicStatusDenied = !context->commandList->hasCommandBuffer()
        && !context->commandList->isRecording()
        && !context->commandList->commandRecordingFailed()
        && context->commandList->recordingLeaseSerial() == 0u
        && !context->commandList->isRenderPassActive()
    ;
    context->publicMutationAttempted = true;
    context->commandList->open();
    context->commandList->clearState();
    context->commandList->endRenderPass();
    context->commandList->endMarker();
    context->commandList->abandonMarker();
    context->commandList->dispatch(1u, 1u, 1u);
    context->commandList->setPushConstants(nullptr, sizeof(u32));
    context->commandList->close();
    CommandList* const commandLists[]{ context->commandList };
    context->nestedToken = context->commandList->getDevice().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        executionQueue,
        QueueSubmissionDesc{}
    );
    outSignal = context->signal;
    return true;
}


TEST_F(DescriptorBufferRoundTripTest, PublishedGraphListReaderSerializesSubmissionAndRejectsReentrantMutation){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/task_submission_hook"))
        .setMarkerLabel("Task Submission Hook")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
    ;
    bool shouldRecord = true;
    bool attempted = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        taskDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &attempted,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = graphicsQueue,
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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/task_submission_hook_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(views.compiled.packet(packet).plan->queue, graphicsQueue);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        task,
        task,
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(attempted);
    const Optional<GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
    ASSERT_TRUE(recordedPacket.has_value());
    ASSERT_EQ(recordedPacket->commandListCount, 1u);
    ASSERT_NE(recordedPacket->commandLists[0u], nullptr);

    VulkanTestBinarySemaphore signal(device);
    ASSERT_TRUE(signal.valid());
    ReentrantGraphSubmissionContext hookContext{
        .commandList = recordedPacket->commandLists[0u],
        .signal = signal.nativeSignal(),
        .nestedToken = {},
        .invocationCount = 0u,
    };
    const GpuTaskGraphTaskSubmissionHook taskSubmissionHook{
        .task = task,
        .hook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = ReenterPublicGraphCommandListSubmission,
        },
    };
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    AtomicFlag readerEntered;
    AtomicFlag releaseReader;
    Atomic<bool> submissionStarted{ false };
    Atomic<bool> submissionReturned{ false };
    bool readerSucceeded = false;
    bool submissionResult = false;
    Thread readerThread([&](){
        readerSucceeded = GraphicsBackend::VulkanTestDispatchAccess::holdGraphCommandListPublicationRead(
            *recordedPacket->commandLists[0u],
            readerEntered,
            releaseReader
        );
    });
    const Timer readerWaitBegin = TimerNow();
    while(
        !readerEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), readerWaitBegin) < 5.0
    )
        YieldThread();
    const bool readerAcquired = readerEntered.test(MemoryOrder::acquire);
    if(!readerAcquired){
        releaseReader.test_and_set(MemoryOrder::release);
        releaseReader.notify_all();
        readerThread.join();
        EXPECT_TRUE(readerAcquired);
        return;
    }

    Thread submissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/published_graph_reader_submission_scratch")
        );
        submissionStarted.store(true, MemoryOrder::release);
        submissionStarted.notify_all();
        submissionResult = submitter.submitTaskRangeInCompileOrder(
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
            &failedPacket,
            nullptr,
            0u,
            &taskSubmissionHook,
            1u
        );
        submissionReturned.store(true, MemoryOrder::release);
        submissionReturned.notify_all();
    });
    const Timer submissionStartWaitBegin = TimerNow();
    while(
        !submissionStarted.load(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), submissionStartWaitBegin) < 5.0
    )
        YieldThread();
    const bool submissionDidStart = submissionStarted.load(MemoryOrder::acquire);
    if(!submissionDidStart){
        releaseReader.test_and_set(MemoryOrder::release);
        releaseReader.notify_all();
        readerThread.join();
        submissionThread.join();
        EXPECT_TRUE(submissionDidStart);
        return;
    }

    const Timer serializationObservationBegin = TimerNow();
    while(
        !submissionReturned.load(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), serializationObservationBegin) < 0.05
    )
        YieldThread();
    const bool submissionReturnedWhileReaderHeld = submissionReturned.load(MemoryOrder::acquire);
    releaseReader.test_and_set(MemoryOrder::release);
    releaseReader.notify_all();
    readerThread.join();
    submissionThread.join();

    EXPECT_TRUE(readerSucceeded);
    EXPECT_FALSE(submissionReturnedWhileReaderHeld);
    ASSERT_TRUE(submissionResult) << "failed packet " << failedPacket.index;
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(hookContext.publicStatusDenied);
    EXPECT_TRUE(hookContext.publicMutationAttempted);
    EXPECT_FALSE(hookContext.nestedToken.valid());
    const QueueSubmissionToken token = transaction.taskToken(views.compiled, task);
    EXPECT_TRUE(token.valid());
    EXPECT_EQ(transaction.packetToken(packet).value, token.value);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

