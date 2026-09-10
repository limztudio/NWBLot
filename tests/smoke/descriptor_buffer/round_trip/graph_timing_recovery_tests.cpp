// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "frame_timing_packets_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


// Timing ownership follows semantic task anchors instead of packet IDs.  The two graph tasks deliberately force
// separate Graphics packets, but this caller never asks the compiler which packet owns either timing scope.
// A valid collected frame sample proves the submitter resolved both task bindings to the correct native submits.
TEST_F(DescriptorBufferRoundTripTest, NativePacketTimingBindingsResolveFromGraphTasks){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = device.getPhysicalQueueInfo(
        device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    ASSERT_NE(graphicsQueueInfo, nullptr);
    if(graphicsQueueInfo->timestampValidBits == 0u)
        GTEST_SKIP() << "GPU timing task bindings: Graphics queue timestamps are unavailable.";
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
    timing.beginFrame(122u);

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

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
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
    GpuTimingSubmissionTicket beginTicket(timing);
    GpuTimingSubmissionTicket endTicket(timing);
    bool beginRecorded = false;
    bool endRecorded = false;

    const GpuTaskId beginTask = graph.addTask<NativeFrameTimingPacketTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_timing_begin"))
            .setMarkerLabel("Task Timing Begin")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling),
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &beginTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::Begin,
            .recorded = &beginRecorded,
        }
    );
    ASSERT_TRUE(beginTask.valid());

    const GpuTaskId endDependencies[] = { beginTask };
    const GpuTaskId endTask = graph.addTask<NativeFrameTimingPacketTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_timing_end"))
            .setMarkerLabel("Task Timing End")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling)
            .setDependencies(endDependencies, LengthOf(endDependencies)),
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &endTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::End,
            .recorded = &endRecorded,
        }
    );
    ASSERT_TRUE(endTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/task_timing_binding_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId beginPacket;
    GpuSubmissionPacketId endPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 2u);
        beginPacket = views.compiled.packetForTask(beginTask);
        endPacket = views.compiled.packetForTask(endTask);
        ASSERT_NE(beginPacket, endPacket);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        beginTask,
        endTask,
        recordedGraph
    ));
    EXPECT_TRUE(beginRecorded);
    EXPECT_TRUE(endRecorded);
    CommandListResourceStateHandoff beginTaskFinalState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff endTaskFinalState(DescriptorBufferRoundTripTest::arena());
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
            compiledGraph,
            views.compiled,
            beginTask,
            beginTaskFinalState
        ));
        ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
            compiledGraph,
            views.compiled,
            endTask,
            endTaskFinalState
        ));
        EXPECT_FALSE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, GpuTaskId{}));
    }

    GpuTaskGraph unrelatedGraph(DescriptorBufferRoundTripTest::arena());
    bool unrelatedShouldRecord = true;
    bool unrelatedAttempted = false;
    const GpuTaskId unrelatedTask = unrelatedGraph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_timing_unrelated"))
            .setMarkerLabel("Task Timing Unrelated")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &unrelatedShouldRecord,
            .attempted = &unrelatedAttempted,
        }
    );
    ASSERT_TRUE(unrelatedTask.valid());
    GpuTaskGraphAnalysis unrelatedAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments unrelatedAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph unrelatedCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena unrelatedScratchArena(Name("tests/descriptor_buffer/task_timing_unrelated_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView unrelatedDeclarations(unrelatedGraph);
        ASSERT_TRUE(compiler.compile(
            unrelatedDeclarations,
            unrelatedAnalysis,
            topology,
            unrelatedAssignments,
            unrelatedCompiledGraph,
            unrelatedScratchArena
        ));
    }
    {
        const GpuTaskGraphReadViews unrelatedViews(unrelatedGraph, unrelatedCompiledGraph);
        ASSERT_TRUE(unrelatedViews.valid());
        EXPECT_FALSE(recordedGraph.hasTaskFinalStateSeed(
            unrelatedCompiledGraph,
            unrelatedViews.compiled,
            beginTask
        ));
    }

    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = beginTask, .timingTicket = &beginTicket },
        GpuTaskGraphTaskTimingTicket{ .task = endTask, .timingTicket = &endTicket },
    };
    NativeTaskAcceptanceObserver beginTaskAcceptance;
    NativeTaskAcceptanceObserver endTaskAcceptance;
    const GpuTaskGraphTaskAcceptedCallback taskAcceptedCallbacks[] = {
        GpuTaskGraphTaskAcceptedCallback{
            .task = beginTask,
            .context = &beginTaskAcceptance,
            .invoke = ObserveNativeTaskAcceptance,
        },
        GpuTaskGraphTaskAcceptedCallback{
            .task = endTask,
            .context = &endTaskAcceptance,
            .invoke = ObserveNativeTaskAcceptance,
        },
    };
    const GpuTaskScheduler submitter(device);
    const GpuTaskGraphTaskTimingTicket duplicateTaskTimingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = beginTask, .timingTicket = &beginTicket },
        GpuTaskGraphTaskTimingTicket{ .task = beginTask, .timingTicket = &beginTicket },
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        beginTask,
        endTask,
        nullptr,
        0u,
        duplicateTaskTimingTickets,
        LengthOf(duplicateTaskTimingTickets),
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    const GpuTaskGraphTaskTimingTicket aliasedTaskTimingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = beginTask, .timingTicket = &beginTicket },
        GpuTaskGraphTaskTimingTicket{ .task = endTask, .timingTicket = &beginTicket },
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        beginTask,
        endTask,
        nullptr,
        0u,
        aliasedTaskTimingTickets,
        LengthOf(aliasedTaskTimingTickets),
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    const GpuTaskGraphSubmissionStatistics aliasedTicketStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(aliasedTicketStatistics.valid());
    EXPECT_EQ(aliasedTicketStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(aliasedTicketStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(aliasedTicketStatistics.rejectedSubmissionCount, 0u);
    const GpuTaskGraphTaskAcceptedCallback duplicateTaskAcceptedCallbacks[] = {
        taskAcceptedCallbacks[0],
        taskAcceptedCallbacks[0],
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        beginTask,
        endTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena,
        nullptr,
        duplicateTaskAcceptedCallbacks,
        LengthOf(duplicateTaskAcceptedCallbacks)
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    EXPECT_EQ(beginTaskAcceptance.acceptedCount, 0u);
    EXPECT_EQ(endTaskAcceptance.acceptedCount, 0u);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        beginTask,
        endTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena,
        nullptr,
        taskAcceptedCallbacks,
        LengthOf(taskAcceptedCallbacks)
    ));
    EXPECT_TRUE(transaction.hasAcceptedPackets());
    QueueSubmissionToken beginTaskToken;
    QueueSubmissionToken endTaskToken;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        beginTaskToken = transaction.taskToken(views.compiled, beginTask);
        endTaskToken = transaction.taskToken(views.compiled, endTask);
        EXPECT_TRUE(beginTaskToken.valid());
        EXPECT_TRUE(endTaskToken.valid());
        EXPECT_NE(beginTaskToken.value, endTaskToken.value);
        EXPECT_EQ(beginTaskToken.value, transaction.packetToken(views.compiled.packetForTask(beginTask)).value);
        EXPECT_EQ(endTaskToken.value, transaction.packetToken(views.compiled.packetForTask(endTask)).value);
        EXPECT_FALSE(transaction.taskToken(views.compiled, GpuTaskId{}).valid());
        const GpuTaskGraphPacketSubmissionStatistics beginSubmissionStatistics =
            transaction.packetSubmissionStatistics(views.compiled, beginPacket)
        ;
        ASSERT_TRUE(beginSubmissionStatistics.valid());
        EXPECT_EQ(beginSubmissionStatistics.plannedWaitTokenCount, 1u);
        EXPECT_EQ(beginSubmissionStatistics.sameQueueWaitElisionCount, 1u);
        EXPECT_EQ(beginSubmissionStatistics.timelineWaitCount, 0u);
    }
    EXPECT_EQ(beginTaskAcceptance.acceptedCount, 1u);
    EXPECT_EQ(endTaskAcceptance.acceptedCount, 1u);
    EXPECT_EQ(beginTaskAcceptance.lastToken.value, beginTaskToken.value);
    EXPECT_EQ(endTaskAcceptance.lastToken.value, endTaskToken.value);
    {
        const GpuTaskGraphReadViews unrelatedViews(unrelatedGraph, unrelatedCompiledGraph);
        ASSERT_TRUE(unrelatedViews.valid());
        EXPECT_FALSE(transaction.taskToken(unrelatedViews.compiled, beginTask).valid());
    }
    ASSERT_TRUE(frameTransaction.confirmBeginSubmission(beginTaskToken));
    ASSERT_TRUE(frameTransaction.confirmEndSubmission(endTaskToken, true));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 122u);
    EXPECT_TRUE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// A renderer failure can leave an automatic setup upload accepted on a genuinely dedicated Transfer queue while a
// later Graphics packet rejects.  The late Graphics recovery packet has no graph dependency on that rejected
// suffix, so the transaction must turn the accepted Transfer completion into a submission-local frontier wait.
// Keep this on a fresh Transfer-enabled device: a host without the optional physical queue reports an environment
// skip, while a qualifying adapter exercises the actual VkQueue/physical-token path rather than a synthetic
// topology alone.
TEST_F(DescriptorBufferRoundTripTest, NativePacketRecoveryJoinsAcceptedDedicatedTransferFrontier){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Transfer recovery: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId transferQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    if(
        !device.getQueue(CommandQueue::Transfer)
        || !transferQueue.valid()
        || transferQueue == graphicsQueue
        || device.getQueueFamilyIndex(transferQueue) == device.getQueueFamilyIndex(graphicsQueue)
    ){
        GTEST_SKIP() << "Transfer recovery: adapter has no dedicated transfer-only queue family.";
    }
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.matchesPhysicalQueueIdentity(graphicsQueue));
    ASSERT_TRUE(device.matchesPhysicalQueueIdentity(transferQueue));
    const VkQueue nativeTransferQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, transferQueue).pointer()
    );
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeTransferQueue, VK_NULL_HANDLE);
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);

    static constexpr u32 s_TransferWords[] = {
        0x1e35a7c9u,
        0x73b4d2f0u,
        0x0badbeefu,
        0x9e3779b9u,
    };
    auto transferDestination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_TransferWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(transferDestination.get(), nullptr);

    GpuTaskGraph graph(transferScope.arena());
    const GpuGraphResourceId transferDestinationResource = graph.importBuffer(
        transferDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_transfer_destination"))
            .setMarkerLabel("Recovery Transfer Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    const GpuUploadBlobId transferBlob = graph.copyUploadData(
        s_TransferWords,
        sizeof(s_TransferWords),
        alignof(u32)
    );
    ASSERT_TRUE(transferDestinationResource.valid());
    ASSERT_TRUE(transferBlob.valid());

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
    GpuTaskSchedulingHint packetScheduling;
    packetScheduling.cost = GpuTaskCostHint::Large;
    packetScheduling.forceSubmissionBoundary = true;
    packetScheduling.allowPacketMerge = false;

    QueueSubmissionToken acceptedTransferToken;
    const GpuTaskId transferTask = graph.addUploadBufferTask(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_transfer_upload"))
            .setMarkerLabel("Recovery Transfer Upload")
            .setQueue(transferRequest)
            .setScheduling(packetScheduling),
        GpuUploadBufferTaskDesc{
            .source = transferBlob,
            .destination = transferDestinationResource,
            .finalState = ResourceStates::CopyDest,
            .acceptedToken = &acceptedTransferToken,
        }
    );
    ASSERT_TRUE(transferTask.valid());

    bool rejectedSuffixShouldRecord = true;
    bool rejectedSuffixRecorded = false;
    const GpuTaskId rejectedSuffixDependencies[] = { transferTask };
    const GpuTaskId rejectedSuffixTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_rejected_graphics_suffix"))
            .setMarkerLabel("Recovery Rejected Graphics Suffix")
            .setQueue(graphicsRequest)
            .setScheduling(packetScheduling)
            .setDependencies(rejectedSuffixDependencies, LengthOf(rejectedSuffixDependencies)),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &rejectedSuffixShouldRecord,
            .attempted = &rejectedSuffixRecorded,
        }
    );
    ASSERT_TRUE(rejectedSuffixTask.valid());

    bool recoveryShouldRecord = true;
    bool recoveryRecorded = false;
    GpuTaskSchedulingHint recoveryScheduling = packetScheduling;
    recoveryScheduling.cost = GpuTaskCostHint::Tiny;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const GpuTaskId recoveryTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_dedicated_transfer_tail"))
            .setMarkerLabel("Recovery Dedicated Transfer Tail")
            .setQueue(graphicsRequest)
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
    GpuTaskGraphAnalysis analysis(transferScope.arena());
    GpuTaskGraphQueueAssignments assignments(transferScope.arena());
    GpuCompiledGraph compiledGraph(transferScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recovery_dedicated_transfer_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskQueueAssignment* const transferAssignment = assignments.find(transferTask);
    const GpuTaskQueueAssignment* const rejectedSuffixAssignment = assignments.find(rejectedSuffixTask);
    const GpuTaskQueueAssignment* const recoveryAssignment = assignments.find(recoveryTask);
    ASSERT_NE(transferAssignment, nullptr);
    ASSERT_NE(rejectedSuffixAssignment, nullptr);
    ASSERT_NE(recoveryAssignment, nullptr);
    EXPECT_EQ(transferAssignment->queue, transferQueue);
    EXPECT_EQ(transferAssignment->queueClass, CommandQueue::Transfer);
    EXPECT_EQ(rejectedSuffixAssignment->queue, graphicsQueue);
    EXPECT_EQ(recoveryAssignment->queue, graphicsQueue);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId transferPacket = views.compiled.packetForTask(transferTask);
    const GpuSubmissionPacketId rejectedSuffixPacket = views.compiled.packetForTask(rejectedSuffixTask);
    const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
    ASSERT_TRUE(transferPacket.valid());
    ASSERT_TRUE(rejectedSuffixPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    ASSERT_EQ(views.compiled.packetCount(), 3u);
    EXPECT_EQ(views.compiled.packetIdAt(0u), transferPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), rejectedSuffixPacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), recoveryPacket);
    ASSERT_EQ(views.compiled.packet(rejectedSuffixPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(rejectedSuffixPacket).dependencies[0u].producer, transferPacket);
    EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->externalDependencyCount, 0u);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);
    EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->isRecoverySubmission);

    GpuRecordedGraph recordedGraph(transferScope.arena());
    GpuGraphSubmissionTransaction transaction(transferScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = transferPacket, .packetCount = 1u },
        recordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = rejectedSuffixPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(rejectedSuffixRecorded);

    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = transferPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken transferToken = transaction.packetToken(transferPacket);
    ASSERT_TRUE(transferToken.valid());
    ASSERT_TRUE(acceptedTransferToken.valid());
    EXPECT_EQ(acceptedTransferToken.value, transferToken.value);
    EXPECT_EQ(transferToken.queue, CommandQueue::Transfer);
    EXPECT_TRUE(transferToken.matchesPhysicalQueue(transferQueue.index, transferQueue.deviceGeneration));
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);

    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = rejectedSuffixPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.packetToken(rejectedSuffixPacket).valid());
    EXPECT_FALSE(transaction.packetToken(recoveryPacket).valid());
    const GpuTaskGraphPhysicalQueueSubmissionStatistics rejectedGraphicsQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            graphicsQueue
        );
    ASSERT_TRUE(rejectedGraphicsQueueStatistics.valid());
    EXPECT_EQ(rejectedGraphicsQueueStatistics.queue, graphicsQueue);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.acceptedPacketCount, 0u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.rejectedTaskCount, 1u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.rejectedSubmissionCount, 1u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.nativeCommandListCount, 0u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(rejectedGraphicsQueueStatistics.recoverySubmissionCount, 0u);
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    VulkanTestQueueSubmit2Capture rejectedSuffixCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(1u, rejectedSuffixCapture));
    EXPECT_EQ(rejectedSuffixCapture.queue, nativeGraphicsQueue);
    EXPECT_EQ(rejectedSuffixCapture.result, VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_FALSE(rejectedSuffixCapture.overflowed);

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
    const QueueSubmissionToken recoveryToken = transaction.packetToken(recoveryPacket);
    ASSERT_TRUE(recoveryToken.valid());
    EXPECT_EQ(recoveryToken.queue, CommandQueue::Graphics);
    EXPECT_TRUE(recoveryToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 3u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 1u);
    VulkanTestQueueSubmit2Capture transferCapture;
    VulkanTestQueueSubmit2Capture recoveryCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(0u, transferCapture));
    ASSERT_TRUE(submissionObserver.capturedSubmission(2u, recoveryCapture));
    __hidden_descriptor_buffer_round_trip_tests::ExpectNativeTimelineDependency(
        transferCapture,
        nativeTransferQueue,
        transferToken,
        recoveryCapture,
        nativeGraphicsQueue,
        recoveryToken
    );
    ASSERT_TRUE(device.waitForIdle());

    const GpuTaskGraphPhysicalQueueSubmissionStatistics recoveredGraphicsQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            graphicsQueue
        );
    ASSERT_TRUE(recoveredGraphicsQueueStatistics.valid());
    EXPECT_EQ(recoveredGraphicsQueueStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.acceptedTaskCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.rejectedPacketCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.rejectedTaskCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.rejectedSubmissionCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.nativeCommandListCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.timelineWaitCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(recoveredGraphicsQueueStatistics.recoverySubmissionCount, 1u);
    EXPECT_GE(recoveredGraphicsQueueStatistics.submissionSeconds, 0.0);

    const GpuTaskGraphPhysicalQueueSubmissionStatistics recoveredTransferQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            transferQueue
        );
    ASSERT_TRUE(recoveredTransferQueueStatistics.valid());
    EXPECT_EQ(recoveredTransferQueueStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(recoveredTransferQueueStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(recoveredTransferQueueStatistics.recoverySubmissionCount, 0u);

    const GpuTaskGraphSubmissionStatistics recoveredSubmissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(recoveredSubmissionStatistics.valid());
    EXPECT_EQ(recoveredSubmissionStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(recoveredSubmissionStatistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(recoveredSubmissionStatistics.recoverySubmissionCount, 1u);
    EXPECT_EQ(
        recoveredGraphicsQueueStatistics.acceptedFrontierSubmissionCount
            + recoveredTransferQueueStatistics.acceptedFrontierSubmissionCount,
        recoveredSubmissionStatistics.acceptedFrontierSubmissionCount
    );
    EXPECT_EQ(
        recoveredGraphicsQueueStatistics.recoverySubmissionCount
            + recoveredTransferQueueStatistics.recoverySubmissionCount,
        recoveredSubmissionStatistics.recoverySubmissionCount
    );

    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(transaction.packetToken(transferPacket).valid());
    EXPECT_TRUE(transaction.packetToken(recoveryPacket).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

