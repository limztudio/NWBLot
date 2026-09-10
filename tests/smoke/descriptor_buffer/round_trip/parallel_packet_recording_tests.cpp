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


// With two logical recording slots, the long packet holds one slot while the other records both short packets in
// sequence. This creates one interval that overlaps two mutually non-overlapping intervals without timer sleeps.
struct RecordingOverlapBridgeTask{
    struct State{
        AtomicFlag longRecordingEntered;
        AtomicFlag secondShortRecordingEntered;
    };

    struct Payload{
        State* state = nullptr;
        u32 sequenceIndex = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(!payload.state || payload.sequenceIndex > 2u || !commandList.isRecording())
            return false;

        State& state = *payload.state;
        if(payload.sequenceIndex == 0u){
            if(state.longRecordingEntered.test_and_set(MemoryOrder::release))
                return false;
            state.longRecordingEntered.notify_all();
            state.secondShortRecordingEntered.wait(false, MemoryOrder::acquire);
        }
        else{
            state.longRecordingEntered.wait(false, MemoryOrder::acquire);
            if(payload.sequenceIndex == 2u){
                if(state.secondShortRecordingEntered.test_and_set(MemoryOrder::release))
                    return false;
                state.secondShortRecordingEntered.notify_all();
            }
        }
        return commandList.isRecording();
    }
};


// All three packets enter their task thunks before one intentionally fails. The two published peers still overlap
// and must remain visible through recording telemetry even though the ready-frontier operation returns false.
struct RecordingOverlapResultTask{
    struct Payload{
        Latch* recordingStarted = nullptr;
        bool shouldRecord = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(!payload.recordingStarted || !commandList.isRecording())
            return false;
        payload.recordingStarted->count_down();
        payload.recordingStarted->wait();
        return payload.shouldRecord && commandList.isRecording();
    }
};


// A ready frontier may contain one worker-eligible packet and one serial packet. Use real graph-owned uploads to
// prove that worker routing remains observable without falsely reporting overlap for the caller-serialized batch.
TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecorderReportsWorkerRoutingWithoutInventingOverlap){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_FirstWords[] = {
        0x4ef8a219u,
        0x13c0ffeeu,
        0x7f4a7c15u,
        0x9e3779b9u,
    };
    static constexpr u32 s_SecondWords[] = {
        0xfeedfaceu,
        0x0badf00du,
        0x2c1b3a49u,
        0xd1cebeefu,
    };
    const auto createDestination = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(s_FirstWords))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
    };
    auto firstDestination = createDestination();
    auto secondDestination = createDestination();
    ASSERT_NE(firstDestination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importDestination = [&graph](const BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId firstDestinationResource = importDestination(
        firstDestination,
        Name("tests/descriptor_buffer/parallel_upload_first_destination"),
        "Parallel Upload First Destination"
    );
    const GpuGraphResourceId secondDestinationResource = importDestination(
        secondDestination,
        Name("tests/descriptor_buffer/parallel_upload_second_destination"),
        "Parallel Upload Second Destination"
    );
    const GpuUploadBlobId firstSource = graph.copyUploadData(s_FirstWords, sizeof(s_FirstWords), alignof(u32));
    const GpuUploadBlobId secondSource = graph.copyUploadData(s_SecondWords, sizeof(s_SecondWords), alignof(u32));
    ASSERT_TRUE(firstDestinationResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());
    ASSERT_TRUE(firstSource.valid());
    ASSERT_TRUE(secondSource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const auto addUpload = [&](const Name& identity,
        const AStringView label,
        const GpuUploadBlobId source,
        const GpuGraphResourceId destination,
        const bool allowParallelRecording,
        QueueSubmissionToken* const acceptedToken
    ){
        GpuTaskSchedulingHint taskScheduling = scheduling;
        taskScheduling.allowParallelRecording = allowParallelRecording;
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(taskScheduling)
        ;
        return graph.addUploadBufferTask(
            desc,
            GpuUploadBufferTaskDesc{
                .source = source,
                .destination = destination,
                .finalState = ResourceStates::Common,
                .acceptedToken = acceptedToken,
            }
        );
    };
    QueueSubmissionToken firstAcceptedToken;
    QueueSubmissionToken secondAcceptedToken;
    const GpuTaskId firstUpload = addUpload(
        Name("tests/descriptor_buffer/parallel_upload_first"),
        "Parallel Upload First",
        firstSource,
        firstDestinationResource,
        true,
        &firstAcceptedToken
    );
    const GpuTaskId secondUpload = addUpload(
        Name("tests/descriptor_buffer/parallel_upload_second"),
        "Parallel Upload Second",
        secondSource,
        secondDestinationResource,
        false,
        &secondAcceptedToken
    );
    ASSERT_TRUE(firstUpload.valid());
    ASSERT_TRUE(secondUpload.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
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
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/parallel_upload_recording_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstUpload);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondUpload);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_EQ(views.compiled.packet(firstPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(views.compiled.packet(secondPacket).plan->recordingFrontier, 0u);

    CpuTaskScheduler recordingWorkers(2u);
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
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, firstUpload));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, secondUpload));
    EXPECT_EQ(firstRecorded->recordingWorkerDomain, recordingWorkers.domainIdentity());
    EXPECT_NE(firstRecorded->recordingWorkerIndex, 0u);
    EXPECT_EQ(secondRecorded->recordingWorkerDomain, 0u);
    EXPECT_EQ(secondRecorded->recordingWorkerIndex, 0u);
    EXPECT_LT(firstRecorded->recordingBeginNanoseconds, firstRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondRecorded->recordingBeginNanoseconds, secondRecorded->recordingEndNanoseconds);
    EXPECT_LE(secondRecorded->recordingEndNanoseconds, firstRecorded->recordingBeginNanoseconds);
    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 1u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 0u);
    EXPECT_EQ(recordingStatistics.recordingElapsedSeconds, recordingStatistics.readyFrontierElapsedSeconds);
    EXPECT_EQ(
        recordingStatistics.readyFrontierWorkerBusySeconds,
        firstRecorded->recordingSeconds + secondRecorded->recordingSeconds
    );
    EXPECT_EQ(
        recordingStatistics.readyFrontierWorkerCapacitySeconds,
        recordingStatistics.readyFrontierElapsedSeconds * 3.0
    );
    ASSERT_GT(recordingStatistics.readyFrontierWorkerCapacitySeconds, 0.0);
    EXPECT_DOUBLE_EQ(
        recordingStatistics.readyFrontierWorkerUtilization(),
        recordingStatistics.readyFrontierWorkerBusySeconds
            / recordingStatistics.readyFrontierWorkerCapacitySeconds
    );

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
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
    EXPECT_TRUE(firstAcceptedToken.valid());
    EXPECT_TRUE(secondAcceptedToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    const u32* const firstWords = static_cast<const u32*>(device.mapBuffer(*firstDestination, CpuAccessMode::Read));
    const u32* const secondWords = static_cast<const u32*>(device.mapBuffer(*secondDestination, CpuAccessMode::Read));
    ASSERT_NE(firstWords, nullptr);
    ASSERT_NE(secondWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        EXPECT_EQ(firstWords[wordIndex], s_FirstWords[wordIndex]);
        EXPECT_EQ(secondWords[wordIndex], s_SecondWords[wordIndex]);
    }
    device.unmapBuffer(*firstDestination);
    device.unmapBuffer(*secondDestination);
}


// An explicit GPU submission edge does not import producer command-list state.  Its packets can therefore record
// together on worker leases, while compile-order submission still publishes the producer token before its consumer.
TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecorderRecordsExplicitGpuDependencyPacketsInParallel){
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
        Name("tests/descriptor_buffer/explicit_gpu_dependency_first"),
        "Explicit GPU Dependency First"
    );
    const GpuGraphResourceId secondResource = importBuffer(
        secondBuffer,
        Name("tests/descriptor_buffer/explicit_gpu_dependency_second"),
        "Explicit GPU Dependency Second"
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
        const GpuTaskId* const dependencies,
        const usize dependencyCount,
        Latch& recordingStarted,
        u32& observedWorkerIndex,
        QueueSubmissionToken& acceptedToken
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
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(uses, LengthOf(uses))
        ;
        return graph.addTask<WorkerAffinedPacketTask>(
            desc,
            WorkerAffinedPacketTask::Payload{
                .recordingStarted = &recordingStarted,
                .observedWorkerIndex = &observedWorkerIndex,
                .expectedBuffer = expectedBuffer,
                .expectedBufferState = ResourceStates::CopyDest,
                .acceptedToken = &acceptedToken,
            }
        );
    };

    Latch recordingStarted(2);
    u32 firstWorkerIndex = 0u;
    u32 secondWorkerIndex = 0u;
    QueueSubmissionToken firstAcceptedToken;
    QueueSubmissionToken secondAcceptedToken;
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/explicit_gpu_dependency_first_task"),
        "Explicit GPU Dependency First Task",
        firstResource,
        firstBuffer.get(),
        nullptr,
        0u,
        recordingStarted,
        firstWorkerIndex,
        firstAcceptedToken
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/explicit_gpu_dependency_second_task"),
        "Explicit GPU Dependency Second Task",
        secondResource,
        secondBuffer.get(),
        &firstTask,
        1u,
        recordingStarted,
        secondWorkerIndex,
        secondAcceptedToken
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
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
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/explicit_gpu_dependency_recording_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(firstPacket, secondPacket);
    EXPECT_LT(firstPacket.index, secondPacket.index);
    const GpuCompiledPacketView secondPacketPlan = views.compiled.packet(secondPacket);
    ASSERT_TRUE(secondPacketPlan.valid());
    ASSERT_EQ(secondPacketPlan.plan->dependencyCount, 1u);
    const GpuPacketDependency* const secondDependencies = views.compiled.packet(secondPacket).dependencies;
    ASSERT_NE(secondDependencies, nullptr);
    EXPECT_EQ(secondDependencies[0u].producer, firstPacket);
    EXPECT_EQ(secondDependencies[0u].consumer, secondPacket);
    const GpuCompiledTaskView firstCompiledTask = views.compiled.findTask(firstTask);
    const GpuCompiledTaskView secondCompiledTask = views.compiled.findTask(secondTask);
    ASSERT_TRUE(firstCompiledTask.valid());
    ASSERT_TRUE(secondCompiledTask.valid());
    EXPECT_EQ(firstCompiledTask.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(secondCompiledTask.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(views.compiled.packet(firstPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(views.compiled.packet(secondPacket).plan->recordingFrontier, 0u);

    CpuTaskScheduler recordingWorkers(1u);
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
    EXPECT_GT(recordingStatistics.readyFrontierWorkerUtilization(), 0.0);
    EXPECT_LE(recordingStatistics.readyFrontierWorkerUtilization(), 1.0);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
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
    const QueueSubmissionToken firstPacketToken = transaction.packetToken(firstPacket);
    const QueueSubmissionToken secondPacketToken = transaction.packetToken(secondPacket);
    ASSERT_TRUE(firstPacketToken.valid());
    ASSERT_TRUE(secondPacketToken.valid());
    ASSERT_TRUE(firstAcceptedToken.valid());
    ASSERT_TRUE(secondAcceptedToken.valid());
    EXPECT_EQ(firstAcceptedToken.queue, firstPacketToken.queue);
    EXPECT_EQ(firstAcceptedToken.value, firstPacketToken.value);
    EXPECT_EQ(secondAcceptedToken.queue, secondPacketToken.queue);
    EXPECT_EQ(secondAcceptedToken.value, secondPacketToken.value);
    EXPECT_EQ(firstAcceptedToken.queue, secondAcceptedToken.queue);
    EXPECT_EQ(firstAcceptedToken.physicalQueueIndex, secondAcceptedToken.physicalQueueIndex);
    EXPECT_EQ(firstAcceptedToken.deviceGeneration, secondAcceptedToken.deviceGeneration);
    EXPECT_LT(firstAcceptedToken.value, secondAcceptedToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecordingOverlapCacheHandlesOneLongAndTwoSequentialIntervals){
    auto& device = DescriptorBufferRoundTripTest::device();
    RecordingOverlapBridgeTask::State state;
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
    const auto addTask = [&](const Name& identity, const AStringView label, const u32 sequenceIndex){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queueRequest)
            .setScheduling(scheduling)
        ;
        return graph.addTask<RecordingOverlapBridgeTask>(
            desc,
            RecordingOverlapBridgeTask::Payload{
                .state = &state,
                .sequenceIndex = sequenceIndex,
            }
        );
    };
    const GpuTaskId longTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_bridge_long"),
        "Recording Overlap Bridge Long",
        0u
    );
    const GpuTaskId firstShortTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_bridge_first_short"),
        "Recording Overlap Bridge First Short",
        1u
    );
    const GpuTaskId secondShortTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_bridge_second_short"),
        "Recording Overlap Bridge Second Short",
        2u
    );
    ASSERT_TRUE(longTask.valid());
    ASSERT_TRUE(firstShortTask.valid());
    ASSERT_TRUE(secondShortTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recording_overlap_bridge_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId longPacket = views.compiled.packetForTask(longTask);
    const GpuSubmissionPacketId firstShortPacket = views.compiled.packetForTask(firstShortTask);
    const GpuSubmissionPacketId secondShortPacket = views.compiled.packetForTask(secondShortTask);
    ASSERT_TRUE(longPacket.valid());
    ASSERT_TRUE(firstShortPacket.valid());
    ASSERT_TRUE(secondShortPacket.valid());
    ASSERT_EQ(views.compiled.packetIdAt(0u), longPacket);
    ASSERT_EQ(views.compiled.packetIdAt(1u), firstShortPacket);
    ASSERT_EQ(views.compiled.packetIdAt(2u), secondShortPacket);
    ASSERT_EQ(views.compiled.packet(longPacket).plan->recordingFrontier, 0u);
    ASSERT_EQ(views.compiled.packet(firstShortPacket).plan->recordingFrontier, 0u);
    ASSERT_EQ(views.compiled.packet(secondShortPacket).plan->recordingFrontier, 0u);

    CpuTaskScheduler recordingWorkers(1u);
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        recordingWorkers
    ));
    const Optional<GpuRecordedPacket> longRecorded = recordedGraph.packetSnapshot(longPacket);
    const Optional<GpuRecordedPacket> firstShortRecorded = recordedGraph.packetSnapshot(firstShortPacket);
    const Optional<GpuRecordedPacket> secondShortRecorded = recordedGraph.packetSnapshot(secondShortPacket);
    ASSERT_TRUE(longRecorded.has_value());
    ASSERT_TRUE(firstShortRecorded.has_value());
    ASSERT_TRUE(secondShortRecorded.has_value());
    EXPECT_LT(longRecorded->recordingBeginNanoseconds, longRecorded->recordingEndNanoseconds);
    EXPECT_LT(firstShortRecorded->recordingBeginNanoseconds, firstShortRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondShortRecorded->recordingBeginNanoseconds, secondShortRecorded->recordingEndNanoseconds);
    EXPECT_LE(firstShortRecorded->recordingEndNanoseconds, secondShortRecorded->recordingBeginNanoseconds);
    EXPECT_LT(longRecorded->recordingBeginNanoseconds, firstShortRecorded->recordingEndNanoseconds);
    EXPECT_LT(firstShortRecorded->recordingBeginNanoseconds, longRecorded->recordingEndNanoseconds);
    EXPECT_LT(longRecorded->recordingBeginNanoseconds, secondShortRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondShortRecorded->recordingBeginNanoseconds, longRecorded->recordingEndNanoseconds);

    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.packetCount, 3u);
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 3u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 3u);
    const GpuTaskGraphPhysicalQueueRecordingStatistics queueStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, graphicsQueue.id)
    ;
    ASSERT_TRUE(queueStatistics.valid());
    EXPECT_EQ(queueStatistics.packetCount, 3u);
    EXPECT_EQ(queueStatistics.parallelPacketCount, 3u);
    EXPECT_EQ(recordedGraph.recordingStatistics(compiledGraph, views.compiled).parallelPacketCount, 3u);
    EXPECT_EQ(
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, graphicsQueue.id).parallelPacketCount,
        3u
    );

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    recordedGraph.reset(compiledGraph);
    const GpuTaskGraphRecordingStatistics resetStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    const GpuTaskGraphPhysicalQueueRecordingStatistics resetQueueStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, graphicsQueue.id)
    ;
    ASSERT_TRUE(resetStatistics.valid());
    ASSERT_TRUE(resetQueueStatistics.valid());
    EXPECT_EQ(resetStatistics.packetCount, 0u);
    EXPECT_EQ(resetStatistics.parallelPacketCount, 0u);
    EXPECT_EQ(resetQueueStatistics.packetCount, 0u);
    EXPECT_EQ(resetQueueStatistics.parallelPacketCount, 0u);
}


TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecordingOverlapCacheKeepsPublishedPeersAfterPartialFailure){
    auto& device = DescriptorBufferRoundTripTest::device();
    Latch recordingStarted(3);
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
    const auto addTask = [&](const Name& identity, const AStringView label, const bool shouldRecord){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queueRequest)
            .setScheduling(scheduling)
        ;
        return graph.addTask<RecordingOverlapResultTask>(
            desc,
            RecordingOverlapResultTask::Payload{
                .recordingStarted = &recordingStarted,
                .shouldRecord = shouldRecord,
            }
        );
    };
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_partial_first"),
        "Recording Overlap Partial First",
        true
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_partial_second"),
        "Recording Overlap Partial Second",
        true
    );
    const GpuTaskId failedTask = addTask(
        Name("tests/descriptor_buffer/recording_overlap_partial_failed"),
        "Recording Overlap Partial Failed",
        false
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(failedTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recording_overlap_partial_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    const GpuSubmissionPacketId expectedFailedPacket = views.compiled.packetForTask(failedTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(expectedFailedPacket.valid());
    ASSERT_EQ(views.compiled.packet(firstPacket).plan->recordingFrontier, 0u);
    ASSERT_EQ(views.compiled.packet(secondPacket).plan->recordingFrontier, 0u);
    ASSERT_EQ(views.compiled.packet(expectedFailedPacket).plan->recordingFrontier, 0u);

    CpuTaskScheduler recordingWorkers(2u);
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
    EXPECT_EQ(failedPacket, expectedFailedPacket);
    const Optional<GpuRecordedPacket> firstRecorded = recordedGraph.packetSnapshot(firstPacket);
    const Optional<GpuRecordedPacket> secondRecorded = recordedGraph.packetSnapshot(secondPacket);
    ASSERT_TRUE(firstRecorded.has_value());
    ASSERT_TRUE(secondRecorded.has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(expectedFailedPacket).has_value());
    EXPECT_LT(firstRecorded->recordingBeginNanoseconds, secondRecorded->recordingEndNanoseconds);
    EXPECT_LT(secondRecorded->recordingBeginNanoseconds, firstRecorded->recordingEndNanoseconds);

    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.packetCount, 2u);
    EXPECT_EQ(recordingStatistics.commandListCount, 2u);
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 2u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 2u);
    const GpuTaskGraphPhysicalQueueRecordingStatistics queueStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, graphicsQueue.id)
    ;
    ASSERT_TRUE(queueStatistics.valid());
    EXPECT_EQ(queueStatistics.packetCount, 2u);
    EXPECT_EQ(queueStatistics.parallelPacketCount, 2u);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

