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


// An injected native-submit failure happens after the automatic ticket and three independent manual tickets are
// prepared. One failed native attempt must discard all four transactions, and the next frame reset must make every
// slot reusable.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAutomaticTimingRejectsAndReusesCompanionQueriesTogether){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
    s_scope->setGpuTimingEnabled(true);
    timing.beginFrame(270u);

    GpuTimingSubmissionTicket firstCompanionTimingTicket(timing);
    GpuTimingSubmissionTicket secondCompanionTimingTicket(timing);
    GpuTimingSubmissionTicket thirdCompanionTimingTicket(timing);
    GpuTimingSubmissionTicket* const companionTimingTickets[] = {
        &firstCompanionTimingTicket,
        &secondCompanionTimingTicket,
        &thirdCompanionTimingTicket,
    };
    bool shouldRecord = true;
    bool tasksRecorded[LengthOf(companionTimingTickets)] = {};
    const Name taskIdentities[] = {
        Name("tests/timing_graph_companion_rejection_a"),
        Name("tests/timing_graph_companion_rejection_b"),
        Name("tests/timing_graph_companion_rejection_c"),
    };
    const AStringView taskMarkers[] = {
        "Rejected Automatic Companion Timing A",
        "Rejected Automatic Companion Timing B",
        "Rejected Automatic Companion Timing C",
    };
    GpuTimingScopeDefinition packetTimingScope;
    packetTimingScope.identity = GpuTaskPacketTimingScopeName(taskIdentities[0u]);
    packetTimingScope.markerLabel = "Rejected Automatic Companion Timing Packet";
    ASSERT_TRUE(packetTimingScope.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.allowPacketMerge = true;
    GpuTaskSchedulingHint mergedScheduling = scheduling;
    mergedScheduling.mergeWithPrevious = true;
    mergedScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskId tasks[LengthOf(companionTimingTickets)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        GpuTaskDesc desc;
        desc
            .setIdentity(taskIdentities[taskIndex])
            .setMarkerLabel(taskMarkers[taskIndex])
            .setQueue(graphicsRequest)
            .setScheduling(taskIndex == 0u ? scheduling : mergedScheduling)
        ;
        if(taskIndex != 0u)
            desc.setDependencies(&tasks[taskIndex - 1u], 1u);
        if(taskIndex == 0u)
            desc.setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly });
        tasks[taskIndex] = graph.addTask<NativePacketCompanionTimingCaptureRetryTask>(
            desc,
            NativePacketCompanionTimingCaptureRetryTask::Payload{
                .shouldRecord = &shouldRecord,
                .attempted = &tasksRecorded[taskIndex],
                .device = &device,
                .timing = &timing,
                .timingTicket = companionTimingTickets[taskIndex],
                .timingScope = &s_GraphCompanionSubmissionTicketScope,
            }
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/rejected_companion_timing_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        device.getPhysicalQueueTopology(),
        assignments,
        compiledGraph,
        scratchArena
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(tasks[0u]);
    ASSERT_TRUE(packet.valid());
    for(const GpuTaskId task : tasks)
        EXPECT_EQ(views.compiled.packetForTask(task), packet);
    EXPECT_TRUE(views.compiled.packet(packet).plan->recordsTiming);

    ASSERT_TRUE(timing.prepareScopeQueries(packetTimingScope.identity, device, s_MaxFramesInFlight));
    ASSERT_TRUE(timing.prepareScopeQueries(
        s_GraphCompanionSubmissionTicketScope.identity,
        device,
        LengthOf(companionTimingTickets)
    ));
    auto resetCommandList = device.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    const QueueSubmissionToken resetToken = device.executeCommandLists(
        resetCommandLists,
        LengthOf(resetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(resetToken.valid());
    timing.confirmFrameReset(resetToken);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device, timing);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        tasks[0u],
        tasks[LengthOf(tasks) - 1u],
        recordedGraph
    ));
    for(const bool taskRecorded : tasksRecorded)
        ASSERT_TRUE(taskRecorded);
    const GpuTimingRecorderStatistics recordedStatistics = timing.statistics(device);
    ASSERT_TRUE(recordedStatistics.valid());

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    GpuTaskGraphTaskTimingTicket timingTickets[LengthOf(tasks)] = {};
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex){
        timingTickets[taskIndex] = GpuTaskGraphTaskTimingTicket{
            .task = tasks[taskIndex],
            .timingTicket = companionTimingTickets[taskIndex],
        };
    }
    {
        const GpuPhysicalQueueId rejectedQueue = views.compiled.packet(packet).plan->queue;
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
            tasks[0u],
            tasks[LengthOf(tasks) - 1u],
            nullptr,
            0u,
            timingTickets,
            LengthOf(timingTickets),
            transaction,
            scratchArena
        ));
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(submissionStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(submissionStatistics.rejectedSubmissionCount, 1u);
    const GpuTimingRecorderStatistics rejectedStatistics = timing.statistics(device);
    ASSERT_TRUE(rejectedStatistics.valid());
    EXPECT_EQ(rejectedStatistics.discardedScopeCount, recordedStatistics.discardedScopeCount + 4u);
    timing.collect(device, 271u);
    EXPECT_FALSE(timingSink.stats(packetTimingScope.identity).valid());
    EXPECT_FALSE(timingSink.stats(s_GraphCompanionSubmissionTicketScope.identity).valid());

    timing.beginFrame(271u);
    auto retryResetCommandList = device.createCommandList();
    ASSERT_NE(retryResetCommandList.get(), nullptr);
    retryResetCommandList->open();
    timing.recordFrameReset(*retryResetCommandList);
    retryResetCommandList->close();
    CommandList* retryResetCommandLists[] = { retryResetCommandList.get() };
    const QueueSubmissionToken retryResetToken = device.executeCommandLists(
        retryResetCommandLists,
        LengthOf(retryResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(retryResetToken.valid());
    timing.confirmFrameReset(retryResetToken);

    auto retryCommandList = device.createCommandList();
    ASSERT_NE(retryCommandList.get(), nullptr);
    GpuTimingSubmissionTicket retryTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(retryTicket);
        retryCommandList->open();
        {
            GpuTimingMeasure packetTiming(timing, packetTimingScope, device, *retryCommandList);
            EXPECT_TRUE(packetTiming.valid());
        }
        for(usize timingIndex = 0u; timingIndex < LengthOf(companionTimingTickets); ++timingIndex){
            GpuTimingMeasure companionTiming(timing, s_GraphCompanionSubmissionTicketScope, device, *retryCommandList);
            EXPECT_TRUE(companionTiming.valid());
        }
        retryCommandList->close();
    }
    CommandList* retryCommandLists[] = { retryCommandList.get() };
    ASSERT_TRUE(retryTicket.submit(device, retryCommandLists, LengthOf(retryCommandLists)));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 272u);
    EXPECT_EQ(timingSink.stats(packetTimingScope.identity).sampleCount, 1u);
    EXPECT_EQ(
        timingSink.stats(s_GraphCompanionSubmissionTicketScope.identity).sampleCount,
        LengthOf(companionTimingTickets)
    );

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

