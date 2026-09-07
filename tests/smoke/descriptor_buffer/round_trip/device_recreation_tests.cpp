// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The graph runtime can outlive one native device in the renderer. Keep a retired CPU-only plan, recording attempt,
// transaction, queue identity, and completion token across real device recreation while releasing its native command
// list first. The replacement recorder, submitter, token validator, and arena registry must all reject that state
// before a newly compiled plan can acquire replacement-generation native storage.
TEST_F(DescriptorBufferRoundTripTest, RecreatesGraphPacketRecordingStateAcrossActualDeviceLifetime){
    HeadlessGraphicsScope recoveryScope;
    if(!recoveryScope.initialize())
        GTEST_SKIP() << "Graph packet recreation: no usable headless Vulkan device on this host.";

    auto& firstDevice = recoveryScope.graphics().getDevice();
    const u16 firstDeviceGeneration = firstDevice.getDeviceGeneration();

    GpuTaskGraph retiredGraph(recoveryScope.arena());
    GpuTaskGraphAnalysis retiredAnalysis(recoveryScope.arena());
    GpuTaskGraphQueueAssignments retiredAssignments(recoveryScope.arena());
    GpuCompiledGraph retiredCompiledGraph(recoveryScope.arena());
    GpuRecordedGraph nativeRecordedGraph(recoveryScope.arena());
    GpuGraphSubmissionTransaction retiredTransaction(recoveryScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recreate_graph_packet_scratch"));
    const GpuTaskGraphCompiler compiler;

    GpuQueueRequest graphicsQueueRequest;
    graphicsQueueRequest.requiredCapabilities = GpuQueueCapability::Graphics;
    graphicsQueueRequest.preferredQueue = GpuQueuePreference::Graphics;
    graphicsQueueRequest.allowFallback = false;
    graphicsQueueRequest.compilerMayOverridePreference = false;
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool firstShouldRecord = true;
    bool firstAttempted = false;
    GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/descriptor_buffer/recreate_graph_packet_first"))
        .setMarkerLabel("Recreate Graph Packet First")
        .setQueue(graphicsQueueRequest)
        .setScheduling(scheduling)
    ;
    const GpuTaskId firstTask = retiredGraph.addTask<NativePacketCaptureRetryTask>(
        firstDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstShouldRecord,
            .attempted = &firstAttempted,
        }
    );
    ASSERT_TRUE(firstTask.valid());

    bool secondShouldRecord = true;
    bool secondAttempted = false;
    GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/descriptor_buffer/recreate_graph_packet_second"))
        .setMarkerLabel("Recreate Graph Packet Second")
        .setQueue(graphicsQueueRequest)
        .setScheduling(scheduling)
    ;
    const GpuTaskId secondTask = retiredGraph.addTask<NativePacketCaptureRetryTask>(
        secondDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondShouldRecord,
            .attempted = &secondAttempted,
        }
    );
    ASSERT_TRUE(secondTask.valid());

    bool thirdShouldRecord = true;
    bool thirdAttempted = false;
    GpuTaskDesc thirdDesc;
    thirdDesc
        .setIdentity(Name("tests/descriptor_buffer/recreate_graph_packet_third"))
        .setMarkerLabel("Recreate Graph Packet Third")
        .setQueue(graphicsQueueRequest)
        .setScheduling(scheduling)
    ;
    const GpuTaskId thirdTask = retiredGraph.addTask<NativePacketCaptureRetryTask>(
        thirdDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &thirdShouldRecord,
            .attempted = &thirdAttempted,
        }
    );
    ASSERT_TRUE(thirdTask.valid());

    bool disposableShouldRecord = false;
    bool disposableAttempted = false;
    GpuTaskDesc disposableDesc;
    disposableDesc
        .setIdentity(Name("tests/descriptor_buffer/recreate_graph_packet_disposable"))
        .setMarkerLabel("Recreate Graph Packet Disposable")
        .setQueue(graphicsQueueRequest)
        .setScheduling(scheduling)
    ;
    const GpuTaskId disposableTask = retiredGraph.addTask<NativePacketCaptureRetryTask>(
        disposableDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &disposableShouldRecord,
            .attempted = &disposableAttempted,
        }
    );
    ASSERT_TRUE(disposableTask.valid());

    const GpuPhysicalQueueTopology retiredTopology = firstDevice.getPhysicalQueueTopology();
    ASSERT_NE(retiredTopology.queues, nullptr);
    ASSERT_GT(retiredTopology.queueCount, 0u);
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(retiredGraph);
        ASSERT_TRUE(compiler.compile(
            compilationDeclarations,
            retiredAnalysis,
            retiredTopology,
            retiredAssignments,
            retiredCompiledGraph,
            scratchArena
        ));
    }
    GpuSubmissionPacketId firstPacket;
    GpuSubmissionPacketId secondPacket;
    GpuSubmissionPacketId thirdPacket;
    GpuSubmissionPacketId disposablePacket;
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        ASSERT_TRUE(retiredViews.valid());
        ASSERT_EQ(retiredViews.compiled.packetCount(), 4u);
        firstPacket = retiredViews.compiled.packetForTask(firstTask);
        secondPacket = retiredViews.compiled.packetForTask(secondTask);
        thirdPacket = retiredViews.compiled.packetForTask(thirdTask);
        disposablePacket = retiredViews.compiled.packetForTask(disposableTask);
    }
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(thirdPacket.valid());
    ASSERT_TRUE(disposablePacket.valid());
    EXPECT_NE(firstPacket, secondPacket);
    EXPECT_NE(secondPacket, thirdPacket);
    EXPECT_NE(firstPacket, thirdPacket);
    EXPECT_NE(thirdPacket, disposablePacket);

    {
        const GpuNativePacketRecorder recorder(firstDevice);
        ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
            retiredGraph,
            retiredCompiledGraph,
            GpuSubmissionPacketRange{ .first = firstPacket, .packetCount = 1u },
            nativeRecordedGraph
        ));
    }
    EXPECT_TRUE(firstAttempted);
    EXPECT_FALSE(secondAttempted);
    EXPECT_FALSE(thirdAttempted);
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        ASSERT_TRUE(nativeRecordedGraph.validFor(
            retiredGraph,
            retiredViews.declarations,
            retiredCompiledGraph,
            retiredViews.compiled
        ));
    }
    Optional<GpuRecordedPacket> retiredRecordedPacket = nativeRecordedGraph.packetSnapshot(firstPacket);
    ASSERT_TRUE(retiredRecordedPacket.has_value());
    ASSERT_EQ(retiredRecordedPacket->commandListCount, 1u);
    ASSERT_NE(retiredRecordedPacket->commandLists[0u], nullptr);
    const GpuPhysicalQueueId retiredQueue = retiredRecordedPacket->commandLists[0u]->getDescription().physicalQueue;
    ASSERT_TRUE(retiredQueue.valid());
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        ASSERT_TRUE(retiredViews.valid());
        EXPECT_EQ(retiredQueue, retiredViews.compiled.packet(firstPacket).plan->queue);
    }
    const GpuCommandArenaStatistics retiredArenaStatistics = firstDevice.getCommandArenaStatistics(retiredQueue);
    ASSERT_TRUE(retiredArenaStatistics.valid());
    EXPECT_EQ(retiredArenaStatistics.queue, retiredQueue);
    retiredRecordedPacket.reset();

    // Continue the active attempt through the exact artifact that originated it. The second recording must append
    // the submitter probe without replacing the already-published first packet.
    {
        const GpuNativePacketRecorder recorder(firstDevice);
        ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
            retiredGraph,
            retiredCompiledGraph,
            GpuSubmissionPacketRange{ .first = thirdPacket, .packetCount = 1u },
            nativeRecordedGraph
        ));
    }
    EXPECT_TRUE(thirdAttempted);
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        ASSERT_TRUE(nativeRecordedGraph.validFor(
            retiredGraph,
            retiredViews.declarations,
            retiredCompiledGraph,
            retiredViews.compiled
        ));
    }
    const u64 retiredRecordingAttemptGeneration = nativeRecordedGraph.recordingAttemptGeneration();
    ASSERT_NE(retiredRecordingAttemptGeneration, 0u);
    ASSERT_TRUE(nativeRecordedGraph.packetSnapshot(firstPacket).has_value());
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(secondPacket).has_value());
    ASSERT_TRUE(nativeRecordedGraph.packetSnapshot(thirdPacket).has_value());
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(disposablePacket).has_value());

    // A failed packet recording rolls back only that packet inside the same artifact and attempt.
    {
        const GpuNativePacketRecorder recorder(firstDevice);
        EXPECT_FALSE(recorder.recordTaskRangeInCompileOrder(
            retiredGraph,
            retiredCompiledGraph,
            disposableTask,
            disposableTask,
            nativeRecordedGraph
        ));
    }
    EXPECT_TRUE(disposableAttempted);
    EXPECT_EQ(nativeRecordedGraph.recordingAttemptGeneration(), retiredRecordingAttemptGeneration);
    ASSERT_TRUE(nativeRecordedGraph.packetSnapshot(firstPacket).has_value());
    ASSERT_TRUE(nativeRecordedGraph.packetSnapshot(thirdPacket).has_value());
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(disposablePacket).has_value());

    retiredTransaction.reset(retiredCompiledGraph);
    {
        const GpuTaskGraphSubmitter submitter(firstDevice);
        ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
            retiredGraph,
            retiredCompiledGraph,
            nativeRecordedGraph,
            GpuSubmissionPacketRange{ .first = firstPacket, .packetCount = 1u },
            nullptr,
            0u,
            nullptr,
            0u,
            retiredTransaction,
            scratchArena
        ));
    }
    const QueueSubmissionToken retiredToken = retiredTransaction.packetToken(firstPacket);
    ASSERT_TRUE(retiredToken.valid());
    ASSERT_TRUE(retiredToken.matchesPhysicalQueue(
        retiredQueue.index,
        firstDeviceGeneration
    ));
    ASSERT_TRUE(firstDevice.validateSubmissionWaitToken(retiredToken));
    ASSERT_TRUE(firstDevice.waitForIdle());

    // Resolve every remaining packet in the originating attempt before releasing its native lists or destroying
    // their Device. The terminal transaction retains the accepted token for stale-device validation.
    ASSERT_TRUE(retiredTransaction.discardUnaccepted(
        retiredGraph,
        retiredCompiledGraph,
        retiredRecordingAttemptGeneration
    ));
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        ASSERT_TRUE(retiredTransaction.validFor(retiredViews.compiled));
    }
    EXPECT_EQ(retiredTransaction.packetToken(firstPacket).value, retiredToken.value);
    EXPECT_EQ(retiredTransaction.packetToken(firstPacket).deviceGeneration, retiredToken.deviceGeneration);
    EXPECT_FALSE(retiredTransaction.packetToken(secondPacket).valid());
    EXPECT_FALSE(retiredTransaction.packetToken(thirdPacket).valid());
    EXPECT_FALSE(retiredTransaction.packetToken(disposablePacket).valid());

    nativeRecordedGraph.reset(retiredCompiledGraph);
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        EXPECT_TRUE(nativeRecordedGraph.validFor(retiredCompiledGraph, retiredViews.compiled));
        EXPECT_FALSE(nativeRecordedGraph.validFor(
            retiredGraph,
            retiredViews.declarations,
            retiredCompiledGraph,
            retiredViews.compiled
        ));
    }
    EXPECT_EQ(nativeRecordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(firstPacket).has_value());
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(thirdPacket).has_value());

    ASSERT_TRUE(recoveryScope.graphics().destroy());
    ASSERT_TRUE(recoveryScope.graphics().createHeadlessDevice());
    auto& secondDevice = recoveryScope.graphics().getDevice();
    const u16 secondDeviceGeneration = secondDevice.getDeviceGeneration();
    EXPECT_NE(secondDeviceGeneration, firstDeviceGeneration);
    {
        const GpuTaskGraphReadViews retiredViews(retiredGraph, retiredCompiledGraph);
        EXPECT_TRUE(retiredViews.declarations.validForDeviceGeneration(secondDeviceGeneration));
        EXPECT_TRUE(retiredViews.compiled.validFor(retiredViews.declarations));
        EXPECT_EQ(retiredViews.compiled.deviceGeneration(), firstDeviceGeneration);
        EXPECT_TRUE(nativeRecordedGraph.validFor(retiredCompiledGraph, retiredViews.compiled));
        EXPECT_FALSE(nativeRecordedGraph.validFor(
            retiredGraph,
            retiredViews.declarations,
            retiredCompiledGraph,
            retiredViews.compiled
        ));
        EXPECT_TRUE(retiredTransaction.validFor(retiredViews.compiled));
    }
    EXPECT_FALSE(secondDevice.matchesPhysicalQueueIdentity(
        retiredToken.queue,
        retiredToken.physicalQueueIndex,
        retiredToken.deviceGeneration
    ));
    EXPECT_FALSE(secondDevice.validateSubmissionWaitToken(retiredToken));
    EXPECT_FALSE(secondDevice.getCommandArenaStatistics(retiredQueue).valid());

    const GpuPhysicalQueueId replacementQueue = secondDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(replacementQueue.valid());
    EXPECT_NE(replacementQueue, retiredQueue);
    EXPECT_EQ(replacementQueue.deviceGeneration, secondDeviceGeneration);
    const GpuCommandArenaStatistics beforeStaleStatistics =
        secondDevice.getCommandArenaStatistics(replacementQueue)
    ;
    ASSERT_TRUE(beforeStaleStatistics.valid());

    {
        const GpuNativePacketRecorder recorder(secondDevice);
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            retiredGraph,
            retiredCompiledGraph,
            GpuSubmissionPacketRange{ .first = secondPacket, .packetCount = 1u },
            nativeRecordedGraph
        ));
    }
    EXPECT_FALSE(secondAttempted);
    EXPECT_EQ(nativeRecordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(nativeRecordedGraph.packetSnapshot(secondPacket).has_value());
    EXPECT_EQ(retiredTransaction.packetToken(firstPacket).value, retiredToken.value);
    EXPECT_EQ(retiredTransaction.packetToken(firstPacket).deviceGeneration, retiredToken.deviceGeneration);
    EXPECT_FALSE(retiredTransaction.packetToken(secondPacket).valid());
    EXPECT_FALSE(retiredTransaction.packetToken(thirdPacket).valid());

    const GpuCommandArenaStatistics afterStaleStatistics =
        secondDevice.getCommandArenaStatistics(replacementQueue)
    ;
    ASSERT_TRUE(afterStaleStatistics.valid());
    EXPECT_EQ(afterStaleStatistics.workerArenaCount, beforeStaleStatistics.workerArenaCount);
    EXPECT_EQ(afterStaleStatistics.commandPoolEpochCount, beforeStaleStatistics.commandPoolEpochCount);
    EXPECT_EQ(afterStaleStatistics.growthEventCount, beforeStaleStatistics.growthEventCount);
    EXPECT_EQ(afterStaleStatistics.resetEventCount, beforeStaleStatistics.resetEventCount);
    EXPECT_EQ(afterStaleStatistics.currentCommandBufferCount, beforeStaleStatistics.currentCommandBufferCount);

    GpuTaskGraph replacementGraph(recoveryScope.arena());
    GpuTaskGraphAnalysis replacementAnalysis(recoveryScope.arena());
    GpuTaskGraphQueueAssignments replacementAssignments(recoveryScope.arena());
    GpuCompiledGraph replacementCompiledGraph(recoveryScope.arena());
    GpuRecordedGraph replacementRecordedGraph(recoveryScope.arena());
    GpuGraphSubmissionTransaction replacementTransaction(recoveryScope.arena());
    bool replacementShouldRecord = true;
    bool replacementAttempted = false;
    GpuTaskDesc replacementDesc;
    replacementDesc
        .setIdentity(Name("tests/descriptor_buffer/recreate_graph_packet_replacement"))
        .setMarkerLabel("Recreate Graph Packet Replacement")
        .setQueue(graphicsQueueRequest)
        .setScheduling(scheduling)
    ;
    const GpuTaskId replacementTask = replacementGraph.addTask<NativePacketCaptureRetryTask>(
        replacementDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &replacementShouldRecord,
            .attempted = &replacementAttempted,
        }
    );
    ASSERT_TRUE(replacementTask.valid());

    const GpuPhysicalQueueTopology replacementTopology = secondDevice.getPhysicalQueueTopology();
    ASSERT_NE(replacementTopology.queues, nullptr);
    ASSERT_GT(replacementTopology.queueCount, 0u);
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(replacementGraph);
        ASSERT_TRUE(compiler.compile(
            compilationDeclarations,
            replacementAnalysis,
            replacementTopology,
            replacementAssignments,
            replacementCompiledGraph,
            scratchArena
        ));
    }
    const GpuTaskGraphReadViews replacementViews(replacementGraph, replacementCompiledGraph);
    ASSERT_TRUE(replacementViews.valid());
    ASSERT_EQ(replacementViews.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId replacementPacket = replacementViews.compiled.packetForTask(replacementTask);
    ASSERT_TRUE(replacementPacket.valid());
    EXPECT_EQ(replacementViews.compiled.deviceGeneration(), secondDeviceGeneration);
    EXPECT_EQ(replacementViews.compiled.packet(replacementPacket).plan->queue, replacementQueue);

    {
        const GpuNativePacketRecorder recorder(secondDevice);
        ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
            replacementGraph,
            replacementCompiledGraph,
            GpuSubmissionPacketRange{ .first = replacementPacket, .packetCount = 1u },
            replacementRecordedGraph
        ));
    }
    EXPECT_TRUE(replacementAttempted);
    const Optional<GpuRecordedPacket> replacementRecordedPacket = replacementRecordedGraph.packetSnapshot(
        replacementPacket
    );
    ASSERT_TRUE(replacementRecordedPacket.has_value());
    ASSERT_EQ(replacementRecordedPacket->commandListCount, 1u);
    ASSERT_NE(replacementRecordedPacket->commandLists[0u], nullptr);
    const GpuPhysicalQueueId recordedReplacementQueue =
        replacementRecordedPacket->commandLists[0u]->getDescription().physicalQueue
    ;
    EXPECT_EQ(recordedReplacementQueue, replacementQueue);
    EXPECT_EQ(recordedReplacementQueue, replacementViews.compiled.packet(replacementPacket).plan->queue);
    EXPECT_EQ(recordedReplacementQueue.deviceGeneration, secondDeviceGeneration);
    EXPECT_NE(recordedReplacementQueue, retiredQueue);

    const GpuCommandArenaStatistics replacementRecordingStatistics =
        secondDevice.getCommandArenaStatistics(replacementQueue)
    ;
    ASSERT_TRUE(replacementRecordingStatistics.valid());
    EXPECT_GT(
        replacementRecordingStatistics.growthEventCount + replacementRecordingStatistics.resetEventCount,
        afterStaleStatistics.growthEventCount + afterStaleStatistics.resetEventCount
    );
    EXPECT_GT(
        replacementRecordingStatistics.leasedCommandBufferCount,
        afterStaleStatistics.leasedCommandBufferCount
    );

    replacementTransaction.reset(replacementCompiledGraph);
    {
        const GpuTaskGraphSubmitter submitter(secondDevice);
        ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
            replacementGraph,
            replacementCompiledGraph,
            replacementRecordedGraph,
            GpuSubmissionPacketRange{ .first = replacementPacket, .packetCount = 1u },
            nullptr,
            0u,
            nullptr,
            0u,
            replacementTransaction,
            scratchArena
        ));
    }
    const QueueSubmissionToken replacementToken = replacementTransaction.packetToken(replacementPacket);
    ASSERT_TRUE(replacementToken.valid());
    ASSERT_TRUE(replacementToken.matchesPhysicalQueue(
        replacementQueue.index,
        secondDeviceGeneration
    ));
    EXPECT_TRUE(secondDevice.validateSubmissionWaitToken(replacementToken));
    EXPECT_FALSE(replacementToken.matchesPhysicalQueue(
        retiredToken.physicalQueueIndex,
        retiredToken.deviceGeneration
    ));
    EXPECT_NE(replacementToken.deviceGeneration, retiredToken.deviceGeneration);
    ASSERT_TRUE(secondDevice.waitForIdle());
}


// A native state handoff retains raw resource pointers. After its producer device is retired those pointers may
// already be dangling, so graph declaration must reject the otherwise-valid snapshot by generation before packet
// recording can inspect its resource state.
TEST_F(DescriptorBufferRoundTripTest, RejectsStaleResourceStateHandoffAfterActualDeviceRecreation){
    HeadlessGraphicsScope recoveryScope;
    if(!recoveryScope.initialize())
        GTEST_SKIP() << "Resource-state handoff recreation: no usable headless Vulkan device on this host.";

    auto& firstDevice = recoveryScope.graphics().getDevice();
    const u16 firstDeviceGeneration = firstDevice.getDeviceGeneration();
    CommandListResourceStateHandoff staleStates(recoveryScope.arena());
    {
        const BufferHandle oldBuffer = firstDevice.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setInitialState(ResourceStates::Common)
        );
        ASSERT_NE(oldBuffer.get(), nullptr);

        const CommandListHandle producer = firstDevice.createCommandList();
        ASSERT_NE(producer.get(), nullptr);
        producer->open();
        producer->setBufferState(oldBuffer.get(), ResourceStates::CopyDest);
        producer->close(&staleStates);
        ASSERT_TRUE(staleStates.valid());
        EXPECT_EQ(staleStates.deviceGeneration(), firstDeviceGeneration);
    }

    ASSERT_TRUE(recoveryScope.graphics().destroy());
    ASSERT_TRUE(recoveryScope.graphics().createHeadlessDevice());
    auto& secondDevice = recoveryScope.graphics().getDevice();
    const u16 secondDeviceGeneration = secondDevice.getDeviceGeneration();
    ASSERT_NE(secondDeviceGeneration, firstDeviceGeneration);
    EXPECT_TRUE(staleStates.valid());
    EXPECT_FALSE(staleStates.validForDeviceGeneration(secondDeviceGeneration));

    const BufferHandle replacementBuffer = secondDevice.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(replacementBuffer.get(), nullptr);

    GpuTaskGraph graph(recoveryScope.arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        replacementBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/stale_handoff_replacement_buffer"))
            .setMarkerLabel("Stale Handoff Replacement Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(resource.valid());

    const GpuTaskResourceUse uses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskExternalStateSource stateSources[] = {
        GpuTaskExternalStateSource{ .states = &staleStates },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/descriptor_buffer/stale_handoff_consumer"))
        .setMarkerLabel("Stale Handoff Consumer")
        .setQueue(graphicsQueue)
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(uses, LengthOf(uses))
    ;
    bool shouldRecord = true;
    bool attempted = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        desc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &attempted,
        }
    );
    ASSERT_TRUE(task.valid());

    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        const GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);
        ASSERT_EQ(taskView.externalStateSourceCount, 1u);
        ASSERT_NE(taskView.externalStateSources, nullptr);
        ASSERT_NE(taskView.externalStateSources[0u].states, &staleStates);
        ASSERT_NE(taskView.externalStateSources[0u].states, nullptr);
        EXPECT_EQ(taskView.externalStateSources[0u].states->deviceGeneration(), firstDeviceGeneration);
        EXPECT_FALSE(declarations.validForDeviceGeneration(secondDeviceGeneration));
    }

    const GpuPhysicalQueueTopology topology = secondDevice.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(recoveryScope.arena());
    GpuTaskGraphQueueAssignments assignments(recoveryScope.arena());
    GpuCompiledGraph compiledGraph(recoveryScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/stale_handoff_recreate_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        EXPECT_FALSE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    EXPECT_FALSE(attempted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

