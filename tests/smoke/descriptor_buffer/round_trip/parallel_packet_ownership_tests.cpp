// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "parallel_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParallelRecordingDiscardState{
    u32 expectedThreadId = 0u;
    Atomic<u32> recordingArrivalCount{ 0u };
    Atomic<u32> completedRecordingPhaseCount{ 0u };
    Atomic<u32> nextDiscardIndex{ 0u };
    Atomic<u32> activeDiscardCount{ 0u };
    Atomic<bool> foreignThreadObserved{ false };
    Atomic<bool> outOfOrderObserved{ false };
    Atomic<bool> overlappingDiscardObserved{ false };
};


struct ParallelRecordingDiscardTask{
    struct Payload{
        ParallelRecordingDiscardState* state = nullptr;
        u32 discardIndex = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(!payload.state || !commandList.isRecording())
            return false;
        const u32 arrivalIndex = payload.state->recordingArrivalCount.fetch_add(1u, MemoryOrder::acq_rel);
        const u32 recordingPhase = arrivalIndex / 2u;
        if((arrivalIndex & 1u) != 0u){
            payload.state->completedRecordingPhaseCount.store(recordingPhase + 1u, MemoryOrder::release);
            payload.state->completedRecordingPhaseCount.notify_all();
        }
        else{
            u32 completedPhaseCount = payload.state->completedRecordingPhaseCount.load(MemoryOrder::acquire);
            while(completedPhaseCount <= recordingPhase){
                payload.state->completedRecordingPhaseCount.wait(completedPhaseCount, MemoryOrder::relaxed);
                completedPhaseCount = payload.state->completedRecordingPhaseCount.load(MemoryOrder::acquire);
            }
        }
        return false;
    }

    static void discarded(Payload& payload){
        if(!payload.state)
            return;
        if(payload.state->activeDiscardCount.fetch_add(1u, MemoryOrder::acq_rel) != 0u)
            payload.state->overlappingDiscardObserved.store(true, MemoryOrder::relaxed);
        if(CurrentThreadId() != payload.state->expectedThreadId)
            payload.state->foreignThreadObserved.store(true, MemoryOrder::relaxed);
        const u32 discardIndex = payload.state->nextDiscardIndex.fetch_add(1u, MemoryOrder::relaxed);
        if(discardIndex % 2u != payload.discardIndex)
            payload.state->outOfOrderObserved.store(true, MemoryOrder::relaxed);
        payload.state->activeDiscardCount.fetch_sub(1u, MemoryOrder::release);
    }
};


TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierSerializesFailedPacketDiscardCallbacksAfterWorkerJoin){
    auto& device = DescriptorBufferRoundTripTest::device();
    ParallelRecordingDiscardState state;
    state.expectedThreadId = CurrentThreadId();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto addTask = [&](const Name& identity, const AStringView label, const u32 discardIndex){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queueRequest)
            .setScheduling(scheduling)
        ;
        return graph.addTask<ParallelRecordingDiscardTask>(
            desc,
            ParallelRecordingDiscardTask::Payload{
                .state = &state,
                .discardIndex = discardIndex,
            }
        );
    };
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/parallel_recording_discard_first"),
        "Parallel Recording Discard First",
        0u
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/parallel_recording_discard_second"),
        "Parallel Recording Discard Second",
        1u
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/parallel_recording_discard_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_EQ(views.compiled.packet(firstPacket).plan->recordingFrontier, 0u);
    ASSERT_EQ(views.compiled.packet(secondPacket).plan->recordingFrontier, 0u);

    Alloc::CpuTaskScheduler recordingWorkers(1u);
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        recordingWorkers,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, firstPacket);
    EXPECT_FALSE(recordedGraph.packetSnapshot(firstPacket).has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(secondPacket).has_value());
    EXPECT_EQ(state.nextDiscardIndex.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(state.activeDiscardCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_FALSE(state.foreignThreadObserved.load(MemoryOrder::relaxed));
    EXPECT_FALSE(state.outOfOrderObserved.load(MemoryOrder::relaxed));
    EXPECT_FALSE(state.overlappingDiscardObserved.load(MemoryOrder::relaxed));

    const u64 firstRecordingAttempt = recordedGraph.recordingAttemptGeneration();
    failedPacket = {};
    EXPECT_FALSE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        recordingWorkers,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, firstPacket);
    EXPECT_NE(recordedGraph.recordingAttemptGeneration(), firstRecordingAttempt);
    EXPECT_FALSE(recordedGraph.packetSnapshot(firstPacket).has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(secondPacket).has_value());
    EXPECT_EQ(state.recordingArrivalCount.load(MemoryOrder::relaxed), 4u);
    EXPECT_EQ(state.nextDiscardIndex.load(MemoryOrder::relaxed), 4u);
    EXPECT_EQ(state.activeDiscardCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_FALSE(state.foreignThreadObserved.load(MemoryOrder::relaxed));
    EXPECT_FALSE(state.outOfOrderObserved.load(MemoryOrder::relaxed));
    EXPECT_FALSE(state.overlappingDiscardObserved.load(MemoryOrder::relaxed));
}


TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecorderUsesWorkerAffinedCommandArenaLeasesForGraphStateSources){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto createBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto firstBuffer = createBuffer();
    auto secondBuffer = createBuffer();
    ASSERT_NE(firstBuffer.get(), nullptr);
    ASSERT_NE(secondBuffer.get(), nullptr);

    CommandListResourceStateHandoff firstSourceState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff secondSourceState(DescriptorBufferRoundTripTest::arena());
    const CommandListHandle firstSourceProducer = device.createCommandList();
    const CommandListHandle secondSourceProducer = device.createCommandList();
    ASSERT_NE(firstSourceProducer.get(), nullptr);
    ASSERT_NE(secondSourceProducer.get(), nullptr);
    firstSourceProducer->open();
    firstSourceProducer->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
    firstSourceProducer->close(&firstSourceState);
    secondSourceProducer->open();
    secondSourceProducer->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
    secondSourceProducer->close(&secondSourceState);
    ASSERT_TRUE(firstSourceState.valid());
    ASSERT_TRUE(secondSourceState.valid());
    CommandList* const sourceProducerLists[] = {
        firstSourceProducer.get(),
        secondSourceProducer.get(),
    };
    bool sourceProducersSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        sourceProducerLists,
        LengthOf(sourceProducerLists),
        CommandQueue::Graphics,
        &sourceProducersSubmitted
    ), 0u);
    ASSERT_TRUE(sourceProducersSubmitted);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId firstResource = importBuffer(
        firstBuffer,
        Name("tests/descriptor_buffer/worker_affined_first"),
        "Worker-Affined First"
    );
    const GpuGraphResourceId secondResource = importBuffer(
        secondBuffer,
        Name("tests/descriptor_buffer/worker_affined_second"),
        "Worker-Affined Second"
    );
    ASSERT_TRUE(firstResource.valid());
    ASSERT_TRUE(secondResource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const auto addTask = [&](const Name& identity,
        const AStringView label,
        const GpuGraphResourceId resource,
        Buffer* const expectedBuffer,
        const GpuTaskExternalStateSource* const externalStateSources,
        Latch& recordingStarted,
        u32& observedWorkerIndex
    ){
        const GpuTaskResourceUse uses[] = {
            GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
        };
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
            .setExternalStateSources(externalStateSources, 1u)
            .setResourceUses(uses, LengthOf(uses))
        ;
        return graph.addTask<WorkerAffinedPacketTask>(
            desc,
            WorkerAffinedPacketTask::Payload{
                .recordingStarted = &recordingStarted,
                .observedWorkerIndex = &observedWorkerIndex,
                .expectedBuffer = expectedBuffer,
                .expectedBufferState = ResourceStates::CopyDest,
            }
        );
    };

    Latch recordingStarted(2);
    u32 firstWorkerIndex = 0u;
    u32 secondWorkerIndex = 0u;
    const GpuTaskExternalStateSource firstExternalStateSources[] = {
        GpuTaskExternalStateSource{ .states = &firstSourceState },
    };
    const GpuTaskExternalStateSource secondExternalStateSources[] = {
        GpuTaskExternalStateSource{ .states = &secondSourceState },
    };
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/worker_affined_first_task"),
        "Worker-Affined First Task",
        firstResource,
        firstBuffer.get(),
        firstExternalStateSources,
        recordingStarted,
        firstWorkerIndex
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/worker_affined_second_task"),
        "Worker-Affined Second Task",
        secondResource,
        secondBuffer.get(),
        secondExternalStateSources,
        recordingStarted,
        secondWorkerIndex
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        const GpuTaskGraphTaskView firstTaskView = declarations.taskAt(firstTask.index);
        const GpuTaskGraphTaskView secondTaskView = declarations.taskAt(secondTask.index);
        ASSERT_EQ(firstTaskView.externalStateSourceCount, 1u);
        ASSERT_EQ(secondTaskView.externalStateSourceCount, 1u);
        ASSERT_NE(firstTaskView.externalStateSources, nullptr);
        ASSERT_NE(secondTaskView.externalStateSources, nullptr);
        EXPECT_NE(firstTaskView.externalStateSources[0u].states, &firstSourceState);
        EXPECT_NE(secondTaskView.externalStateSources[0u].states, &secondSourceState);
        firstSourceState.reset();
        secondSourceState.reset();
        EXPECT_TRUE(firstTaskView.externalStateSources[0u].states->valid());
        EXPECT_TRUE(secondTaskView.externalStateSources[0u].states->valid());
    }

    const GpuPhysicalQueueInfo graphicsQueue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/worker_affined_recording_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_EQ(views.compiled.packet(firstPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(views.compiled.packet(secondPacket).plan->recordingFrontier, 0u);

    Alloc::CpuTaskScheduler recordingWorkers(1u);
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        recordingWorkers
    ));
    const Optional<GpuRecordedPacket> firstRecorded = recordedGraph.packetSnapshot(firstPacket);
    const Optional<GpuRecordedPacket> secondRecorded = recordedGraph.packetSnapshot(secondPacket);
    ASSERT_TRUE(firstRecorded.has_value());
    ASSERT_TRUE(secondRecorded.has_value());
    EXPECT_NE(firstWorkerIndex, 0u);
    EXPECT_NE(secondWorkerIndex, 0u);
    EXPECT_NE(firstWorkerIndex, secondWorkerIndex);
    EXPECT_EQ(firstRecorded->recordingWorkerDomain, recordingWorkers.domainIdentity());
    EXPECT_EQ(secondRecorded->recordingWorkerDomain, recordingWorkers.domainIdentity());
    EXPECT_EQ(firstRecorded->recordingWorkerIndex, firstWorkerIndex);
    EXPECT_EQ(secondRecorded->recordingWorkerIndex, secondWorkerIndex);
    EXPECT_LT(firstRecorded->recordingBeginNanoseconds, firstRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondRecorded->recordingBeginNanoseconds, secondRecorded->recordingEndNanoseconds);
    EXPECT_LT(firstRecorded->recordingBeginNanoseconds, secondRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondRecorded->recordingBeginNanoseconds, firstRecorded->recordingEndNanoseconds);
    EXPECT_GE(firstRecorded->commandListAcquisitionSeconds, 0.0);
    EXPECT_GE(firstRecorded->graphBarrierRecordingSeconds, 0.0);
    EXPECT_GE(firstRecorded->taskRecordSeconds, 0.0);
    EXPECT_GE(secondRecorded->commandListAcquisitionSeconds, 0.0);
    EXPECT_GE(secondRecorded->graphBarrierRecordingSeconds, 0.0);
    EXPECT_GE(secondRecorded->taskRecordSeconds, 0.0);
    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.packetCount, 2u);
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 2u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 2u);
    EXPECT_EQ(recordingStatistics.recordingElapsedSeconds, recordingStatistics.readyFrontierElapsedSeconds);
    EXPECT_EQ(
        recordingStatistics.readyFrontierWorkerBusySeconds,
        firstRecorded->recordingSeconds + secondRecorded->recordingSeconds
    );
    EXPECT_EQ(
        recordingStatistics.readyFrontierWorkerCapacitySeconds,
        recordingStatistics.readyFrontierElapsedSeconds * 2.0
    );
    ASSERT_GT(recordingStatistics.readyFrontierWorkerCapacitySeconds, 0.0);
    EXPECT_DOUBLE_EQ(
        recordingStatistics.readyFrontierWorkerUtilization(),
        recordingStatistics.readyFrontierWorkerBusySeconds
            / recordingStatistics.readyFrontierWorkerCapacitySeconds
    );
    // recordPacketRangeInReadyFrontiers() completed synchronously, so no worker can still publish a slot while this
    // exact physical-queue snapshot reads the aggregate's ordinary per-packet counters.
    const GpuTaskGraphPhysicalQueueRecordingStatistics graphicsQueueRecordingStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, graphicsQueue.id)
    ;
    ASSERT_TRUE(graphicsQueueRecordingStatistics.valid());
    EXPECT_EQ(graphicsQueueRecordingStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(graphicsQueueRecordingStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(
        graphicsQueueRecordingStatistics.recordingAttemptGeneration,
        recordedGraph.recordingAttemptGeneration()
    );
    EXPECT_EQ(graphicsQueueRecordingStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(graphicsQueueRecordingStatistics.queue, graphicsQueue.id);
    EXPECT_EQ(graphicsQueueRecordingStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(graphicsQueueRecordingStatistics.packetCount, 2u);
    EXPECT_EQ(graphicsQueueRecordingStatistics.workerRoutedPacketCount, 2u);
    EXPECT_EQ(graphicsQueueRecordingStatistics.parallelPacketCount, 2u);
    EXPECT_EQ(graphicsQueueRecordingStatistics.packetCount, recordingStatistics.packetCount);
    EXPECT_EQ(graphicsQueueRecordingStatistics.taskCount, recordingStatistics.taskCount);
    EXPECT_EQ(graphicsQueueRecordingStatistics.commandListCount, recordingStatistics.commandListCount);
    EXPECT_EQ(graphicsQueueRecordingStatistics.barrierCount, recordingStatistics.barrierCount);
    EXPECT_EQ(graphicsQueueRecordingStatistics.workerRoutedPacketCount, recordingStatistics.workerRoutedPacketCount);
    EXPECT_EQ(graphicsQueueRecordingStatistics.parallelPacketCount, recordingStatistics.parallelPacketCount);
    EXPECT_NEAR(
        graphicsQueueRecordingStatistics.commandListAcquisitionSeconds,
        recordingStatistics.commandListAcquisitionSeconds,
        0.000001
    );
    EXPECT_NEAR(
        graphicsQueueRecordingStatistics.graphBarrierRecordingSeconds,
        recordingStatistics.graphBarrierRecordingSeconds,
        0.000001
    );
    EXPECT_NEAR(
        graphicsQueueRecordingStatistics.taskRecordSeconds,
        recordingStatistics.taskRecordSeconds,
        0.000001
    );
    EXPECT_NEAR(
        graphicsQueueRecordingStatistics.recordingSeconds,
        recordingStatistics.recordingSeconds,
        0.000001
    );
    EXPECT_EQ(
        recordingStatistics.commandListAcquisitionSeconds,
        firstRecorded->commandListAcquisitionSeconds + secondRecorded->commandListAcquisitionSeconds
    );
    EXPECT_EQ(
        recordingStatistics.graphBarrierRecordingSeconds,
        firstRecorded->graphBarrierRecordingSeconds + secondRecorded->graphBarrierRecordingSeconds
    );
    EXPECT_EQ(
        recordingStatistics.taskRecordSeconds,
        firstRecorded->taskRecordSeconds + secondRecorded->taskRecordSeconds
    );

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

