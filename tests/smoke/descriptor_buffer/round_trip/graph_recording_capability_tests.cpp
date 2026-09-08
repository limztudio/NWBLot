// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


// Reset is a nonblocking lifecycle probe. A submitter blocked after it borrows packet/list/timing storage keeps the
// exact artifact intact; after native acceptance releases that operation lease, reset may clear it even while the
// accepted native command buffer remains in flight under queue ownership.
TEST_F(DescriptorBufferRoundTripTest, RecordedGraphResetRefusesWhilePreSubmitOperationBorrowsArtifact){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    bool shouldRecord = true;
    bool attempted = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recorded_graph_reset_during_pre_submit"))
            .setMarkerLabel("Recorded Graph Reset During Pre Submit")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            }),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &attempted,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recorded_graph_reset_pre_submit_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        const GpuCompiledPacketView packetView = views.compiled.packet(packet);
        ASSERT_TRUE(packetView.valid());
        queue = packetView.plan->queue;
    }
    ASSERT_TRUE(queue.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(attempted);
    const Optional<GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
    ASSERT_TRUE(recordedPacket.has_value());
    ASSERT_EQ(recordedPacket->commandListCount, 1u);

    __hidden_descriptor_buffer_round_trip_tests::BlockingPresentationSubmissionSignal signal(device, queue);
    ASSERT_TRUE(signal.valid());
    const GpuTaskGraphTaskSubmissionHook taskSubmissionHook{
        .task = task,
        .hook = signal.hook(),
    };
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuTaskScheduler submitter(device);
    GpuSubmissionPacketId failedPacket;
    bool submissionResult = false;
    Thread submissionThread([&](){
        Alloc::ScratchArena submissionScratch(Name("tests/descriptor_buffer/recorded_graph_reset_pre_submit_submit"));
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
        signal.markExecuteReturned();
    });
    const Timer queuedWaitBegin = TimerNow();
    while(
        !signal.isQueued()
        && !signal.executeReturned()
        && DurationInSeconds<f64>(TimerNow(), queuedWaitBegin) < 5.0
    )
        YieldThread();
    const bool queued = signal.isQueued();
    if(!queued){
        signal.releasePrepare();
        submissionThread.join();
    }
    ASSERT_TRUE(queued) << "submission did not reach the pre-submit hook; failed packet " << failedPacket.index;

    EXPECT_FALSE(recordedGraph.tryReset(compiledGraph));
    const Optional<GpuRecordedPacket> retainedRecordedPacket = recordedGraph.packetSnapshot(packet);
    ASSERT_TRUE(retainedRecordedPacket.has_value());
    EXPECT_EQ(retainedRecordedPacket->packet, recordedPacket->packet);
    EXPECT_EQ(retainedRecordedPacket->commandLists[0u], recordedPacket->commandLists[0u]);
    EXPECT_EQ(
        retainedRecordedPacket->commandListRecordingLeaseSerials[0u],
        recordedPacket->commandListRecordingLeaseSerials[0u]
    );
    EXPECT_FALSE(signal.executeReturned());

    signal.releasePrepare();
    submissionThread.join();

    ASSERT_TRUE(submissionResult) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(signal.executeReturned());
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(recordedGraph.tryReset(compiledGraph));
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_TRUE(recordedGraph.validFor(compiledGraph, views.compiled));
    }
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
}


TEST_F(DescriptorBufferRoundTripTest, GraphRecordingCompletionRejectionPreservesCrashMarkerHistory){
    auto& device = DescriptorBufferRoundTripTest::device();
    CommandListHandle recorderCommandList = device.createCommandList();
    ASSERT_TRUE(recorderCommandList);
    recorderCommandList->open();
    ASSERT_TRUE(recorderCommandList->isRecording());
    ASSERT_TRUE(recorderCommandList->hasCommandBuffer());
    const usize rejectedMarkerHash = GraphicsBackend::VulkanTestDispatchAccess::recordGpuCrashMarkerHistory(
        *recorderCommandList,
        "Graph recording before rejected publication"
    );

    CommandListHandle retainedCommandList = recorderCommandList;
    EXPECT_TRUE(GraphicsBackend::VulkanTestDispatchAccess::rejectGraphCommandListPublicationAfterSemanticClose(*recorderCommandList));
    recorderCommandList.reset();

    ASSERT_TRUE(retainedCommandList);
    EXPECT_FALSE(retainedCommandList->isRecording());
    EXPECT_FALSE(retainedCommandList->hasCommandBuffer());
    EXPECT_TRUE(retainedCommandList->commandRecordingFailed());
    const Core::ResolvedMarker rejectedMarker = GraphicsBackend::VulkanTestDispatchAccess::resolveGpuCrashMarkerHistory(
        *retainedCommandList,
        rejectedMarkerHash
    );
    ASSERT_TRUE(rejectedMarker.first());
    EXPECT_EQ(rejectedMarker.second(), AStringView("Graph recording before rejected publication"));

    retainedCommandList->open();
    ASSERT_TRUE(retainedCommandList->isRecording());
    ASSERT_TRUE(retainedCommandList->hasCommandBuffer());
    const usize retryMarkerHash = GraphicsBackend::VulkanTestDispatchAccess::recordGpuCrashMarkerHistory(
        *retainedCommandList,
        "Graph recording retry after rejected publication"
    );
    retainedCommandList->beginMarker("Graph recording retry after rejected publication");
    retainedCommandList->endMarker();
    retainedCommandList->close();
    EXPECT_TRUE(retainedCommandList->hasCommandBuffer());
    EXPECT_FALSE(retainedCommandList->commandRecordingFailed());
    const Core::ResolvedMarker retryMarker = GraphicsBackend::VulkanTestDispatchAccess::resolveGpuCrashMarkerHistory(
        *retainedCommandList,
        retryMarkerHash
    );
    ASSERT_TRUE(retryMarker.first());
    EXPECT_EQ(retryMarker.second(), AStringView("Graph recording retry after rejected publication"));
    EXPECT_EQ(rejectedMarker.second(), AStringView("Graph recording before rejected publication"));
}


TEST_F(DescriptorBufferRoundTripTest, GraphRecordingCapabilityDeniesRetainedForeignThreadWithoutInvalidatingOwner){
    auto& device = DescriptorBufferRoundTripTest::device();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLeaseSerial = commandList->recordingLeaseSerial();
    ASSERT_NE(recordingLeaseSerial, 0u);

    CommandListHandle retainedCommandList = commandList;
    AtomicFlag ownerEntered;
    AtomicFlag beginOwnerProbe;
    AtomicFlag ownerProbeFinished;
    AtomicFlag releaseOwner;
    bool ownerStateValid = false;
    Thread ownerThread([&](){
        ownerStateValid = GraphicsBackend::VulkanTestDispatchAccess::holdGraphCommandListRecording(
            *commandList,
            recordingLeaseSerial,
            nullptr,
            ownerEntered,
            beginOwnerProbe,
            ownerProbeFinished,
            releaseOwner
        );
    });

    const Timer ownerEntryWaitBegin = TimerNow();
    while(
        !ownerEntered.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), ownerEntryWaitBegin) < 5.0
    )
        YieldThread();
    const bool ownerDidEnter = ownerEntered.test(MemoryOrder::acquire);
    if(!ownerDidEnter){
        beginOwnerProbe.test_and_set(MemoryOrder::release);
        beginOwnerProbe.notify_all();
        releaseOwner.test_and_set(MemoryOrder::release);
        releaseOwner.notify_all();
        ownerThread.join();
        EXPECT_TRUE(ownerDidEnter);
        return;
    }

    EXPECT_FALSE(retainedCommandList->isRecording());
    EXPECT_FALSE(retainedCommandList->hasCommandBuffer());
    EXPECT_FALSE(retainedCommandList->commandRecordingFailed());
    EXPECT_EQ(retainedCommandList->recordingLeaseSerial(), 0u);
    retainedCommandList->close();
    retainedCommandList->open();

    beginOwnerProbe.test_and_set(MemoryOrder::release);
    beginOwnerProbe.notify_all();
    const Timer ownerProbeWaitBegin = TimerNow();
    while(
        !ownerProbeFinished.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), ownerProbeWaitBegin) < 5.0
    )
        YieldThread();
    const bool ownerDidProbe = ownerProbeFinished.test(MemoryOrder::acquire);
    releaseOwner.test_and_set(MemoryOrder::release);
    releaseOwner.notify_all();
    ownerThread.join();

    EXPECT_TRUE(ownerDidProbe);
    EXPECT_TRUE(ownerStateValid);
    EXPECT_FALSE(retainedCommandList->isRecording());
    EXPECT_FALSE(retainedCommandList->hasCommandBuffer());
    EXPECT_TRUE(retainedCommandList->commandRecordingFailed());
}


TEST_F(DescriptorBufferRoundTripTest, GraphRecordingCapabilityMatchesExactPointerAcrossParallelSameSerialLists){
    auto& device = DescriptorBufferRoundTripTest::device();
    CommandListHandle firstCommandList = device.createCommandList();
    CommandListHandle secondCommandList = device.createCommandList();
    ASSERT_TRUE(firstCommandList);
    ASSERT_TRUE(secondCommandList);
    firstCommandList->open();
    secondCommandList->open();
    ASSERT_TRUE(firstCommandList->isRecording());
    ASSERT_TRUE(secondCommandList->isRecording());
    const u64 recordingLeaseSerial = firstCommandList->recordingLeaseSerial();
    ASSERT_NE(recordingLeaseSerial, 0u);
    ASSERT_EQ(secondCommandList->recordingLeaseSerial(), recordingLeaseSerial);

    AtomicFlag firstOwnerEntered;
    AtomicFlag secondOwnerEntered;
    AtomicFlag beginProbe;
    AtomicFlag firstProbeFinished;
    AtomicFlag secondProbeFinished;
    AtomicFlag releaseOwners;
    bool firstOwnerStateValid = false;
    bool secondOwnerStateValid = false;
    Thread firstOwnerThread([&](){
        firstOwnerStateValid = GraphicsBackend::VulkanTestDispatchAccess::holdGraphCommandListRecording(
            *firstCommandList,
            recordingLeaseSerial,
            secondCommandList.get(),
            firstOwnerEntered,
            beginProbe,
            firstProbeFinished,
            releaseOwners
        );
    });
    Thread secondOwnerThread([&](){
        secondOwnerStateValid = GraphicsBackend::VulkanTestDispatchAccess::holdGraphCommandListRecording(
            *secondCommandList,
            recordingLeaseSerial,
            firstCommandList.get(),
            secondOwnerEntered,
            beginProbe,
            secondProbeFinished,
            releaseOwners
        );
    });

    const Timer ownerEntryWaitBegin = TimerNow();
    while(
        (!firstOwnerEntered.test(MemoryOrder::acquire) || !secondOwnerEntered.test(MemoryOrder::acquire))
        && DurationInSeconds<f64>(TimerNow(), ownerEntryWaitBegin) < 5.0
    )
        YieldThread();
    const bool ownersDidEnter = firstOwnerEntered.test(MemoryOrder::acquire)
        && secondOwnerEntered.test(MemoryOrder::acquire)
    ;
    if(!ownersDidEnter){
        beginProbe.test_and_set(MemoryOrder::release);
        beginProbe.notify_all();
        releaseOwners.test_and_set(MemoryOrder::release);
        releaseOwners.notify_all();
        firstOwnerThread.join();
        secondOwnerThread.join();
        EXPECT_TRUE(ownersDidEnter);
        return;
    }

    beginProbe.test_and_set(MemoryOrder::release);
    beginProbe.notify_all();
    const Timer probeWaitBegin = TimerNow();
    while(
        (!firstProbeFinished.test(MemoryOrder::acquire) || !secondProbeFinished.test(MemoryOrder::acquire))
        && DurationInSeconds<f64>(TimerNow(), probeWaitBegin) < 5.0
    )
        YieldThread();
    const bool ownersDidProbe = firstProbeFinished.test(MemoryOrder::acquire)
        && secondProbeFinished.test(MemoryOrder::acquire)
    ;
    releaseOwners.test_and_set(MemoryOrder::release);
    releaseOwners.notify_all();
    firstOwnerThread.join();
    secondOwnerThread.join();

    EXPECT_TRUE(ownersDidProbe);
    EXPECT_TRUE(firstOwnerStateValid);
    EXPECT_TRUE(secondOwnerStateValid);
    EXPECT_FALSE(firstCommandList->isRecording());
    EXPECT_FALSE(secondCommandList->isRecording());
    EXPECT_TRUE(firstCommandList->commandRecordingFailed());
    EXPECT_TRUE(secondCommandList->commandRecordingFailed());
}


TEST_F(DescriptorBufferRoundTripTest, GraphRecordingCapabilityRejectsOwnerOpenAndCloseBoundaries){
    auto& device = DescriptorBufferRoundTripTest::device();
    CommandListHandle openBoundaryCommandList = device.createCommandList();
    CommandListHandle closeBoundaryCommandList = device.createCommandList();
    ASSERT_TRUE(openBoundaryCommandList);
    ASSERT_TRUE(closeBoundaryCommandList);
    openBoundaryCommandList->open();
    closeBoundaryCommandList->open();
    ASSERT_TRUE(openBoundaryCommandList->isRecording());
    ASSERT_TRUE(closeBoundaryCommandList->isRecording());

    EXPECT_TRUE(GraphicsBackend::VulkanTestDispatchAccess::rejectGraphCommandListOwnerOpenAndCloseBoundaries(
        *openBoundaryCommandList,
        *closeBoundaryCommandList
    ));
    EXPECT_FALSE(openBoundaryCommandList->isRecording());
    EXPECT_FALSE(closeBoundaryCommandList->isRecording());
    EXPECT_TRUE(openBoundaryCommandList->commandRecordingFailed());
    EXPECT_TRUE(closeBoundaryCommandList->commandRecordingFailed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

