// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "recording_capability_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NativePacketRecordedStateProducerTask{
    struct Payload{
        Buffer* prerequisiteBuffer = nullptr;
        ResourceStates::Mask expectedPrerequisiteState = ResourceStates::Unknown;
        Buffer* producedBuffer = nullptr;
        ResourceStates::Mask producedState = ResourceStates::Unknown;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.producedBuffer
            || payload.producedState == ResourceStates::Unknown
            || (
                payload.prerequisiteBuffer
                && commandList.getBufferState(payload.prerequisiteBuffer) != payload.expectedPrerequisiteState
            )
        )
            return false;

        commandList.setBufferState(payload.producedBuffer, payload.producedState);
        const bool ready = commandList.getBufferState(payload.producedBuffer) == payload.producedState;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


TEST_F(DescriptorBufferRoundTripTest, NativePacketTraversesCompilerPacketRanges){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/compile_order_buffer"))
            .setMarkerLabel("Compile Order Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(resource.valid());

    // This mirrors the graph-owned software effects sequence: Shadow Visibility -> Software Caustics.
    // A single Graphics family remains a valid fallback for each compute-designated packet.
    const GpuTaskResourceUse writerUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint writerScheduling;
    writerScheduling.forceSubmissionBoundary = true;
    writerScheduling.allowPacketMerge = false;
    GpuTaskDesc writerDesc;
    writerDesc
        .setIdentity(Name("tests/descriptor_buffer/software_effects_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(writerScheduling)
        .setResourceUses(writerUses, LengthOf(writerUses))
    ;
    bool writerRecorded = false;
    const GpuTaskId writer = graph.addTask<NativePacketPrefixTask>(
        writerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &writerRecorded,
        }
    );
    ASSERT_TRUE(writer.valid());

    const GpuTaskResourceUse readerUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint readerScheduling;
    readerScheduling.forceSubmissionBoundary = true;
    readerScheduling.allowPacketMerge = false;
    GpuTaskDesc readerDesc;
    readerDesc
        .setIdentity(Name("tests/descriptor_buffer/software_effects_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(readerScheduling)
        .setDependencies(&writer, 1u)
        .setResourceUses(readerUses, LengthOf(readerUses))
    ;
    bool readerRecorded = false;
    const GpuTaskId reader = graph.addTask<NativePacketPrefixTask>(
        readerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &readerRecorded,
        }
    );
    ASSERT_TRUE(reader.valid());

    const GpuPhysicalQueueInfo queue{
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
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/software_effects_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuTaskGraphCompileStatistics& compileStatistics = views.compiled.compileStatistics();
    ASSERT_TRUE(compileStatistics.valid());
    EXPECT_EQ(compileStatistics.taskCount, 2u);
    EXPECT_EQ(compileStatistics.resourceCount, 1u);
    EXPECT_EQ(compileStatistics.packetCount, 2u);
    EXPECT_EQ(compileStatistics.explicitDependencyCount, 1u);
    EXPECT_GE(compileStatistics.inferredDependencyCount, 1u);
    EXPECT_EQ(compileStatistics.packetDependencyCount, 1u);
    EXPECT_EQ(compileStatistics.crossQueuePacketDependencyCount, 0u);
    EXPECT_GE(compileStatistics.prologueBarrierCount, 1u);
    EXPECT_EQ(compileStatistics.taskCountByQueueClass[CommandQueue::Graphics], 2u);
    EXPECT_EQ(compileStatistics.packetCountByQueueClass[CommandQueue::Graphics], 2u);
    EXPECT_GE(compileStatistics.analysisSeconds, 0.0);
    EXPECT_GE(compileStatistics.queueAssignmentSeconds, 0.0);
    EXPECT_GE(compileStatistics.planningSeconds, 0.0);
    EXPECT_GE(compileStatistics.totalSeconds, 0.0);
    const GpuSubmissionPacketId writerPacket = views.compiled.packetForTask(writer);
    const GpuSubmissionPacketId readerPacket = views.compiled.packetForTask(reader);
    ASSERT_TRUE(writerPacket.valid());
    ASSERT_TRUE(readerPacket.valid());
    EXPECT_EQ(views.compiled.packetIdAt(0u), writerPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), readerPacket);
    ASSERT_EQ(views.compiled.packet(readerPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(readerPacket).dependencies[0].producer, writerPacket);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    // Each one-packet ready call deliberately takes the serial fallback while preserving incremental graph-owned
    // recording and compiler state propagation.
    const GpuNativePacketRecorder recorder(device);
    CpuTaskScheduler recordingWorkers(1u);
    ASSERT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = writerPacket, .packetCount = 1u },
        recordedGraph,
        recordingWorkers
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = readerPacket, .packetCount = 1u },
        recordedGraph,
        recordingWorkers
    ));
    EXPECT_TRUE(writerRecorded);
    EXPECT_TRUE(readerRecorded);
    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(recordingStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(recordingStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(recordingStatistics.packetCount, 2u);
    EXPECT_EQ(recordingStatistics.taskCount, 2u);
    EXPECT_EQ(recordingStatistics.commandListCount, 2u);
    EXPECT_EQ(
        recordingStatistics.barrierCount,
        compileStatistics.prologueBarrierCount + compileStatistics.epilogueBarrierCount
    );
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 0u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 0u);
    const Optional<GpuRecordedPacket> writerRecordedPacket = recordedGraph.packetSnapshot(writerPacket);
    const Optional<GpuRecordedPacket> readerRecordedPacket = recordedGraph.packetSnapshot(readerPacket);
    ASSERT_TRUE(writerRecordedPacket.has_value());
    ASSERT_TRUE(readerRecordedPacket.has_value());
    EXPECT_LT(writerRecordedPacket->recordingBeginNanoseconds, writerRecordedPacket->recordingEndNanoseconds);
    EXPECT_LT(readerRecordedPacket->recordingBeginNanoseconds, readerRecordedPacket->recordingEndNanoseconds);
    EXPECT_LE(writerRecordedPacket->recordingEndNanoseconds, readerRecordedPacket->recordingBeginNanoseconds);
    EXPECT_GE(writerRecordedPacket->commandListAcquisitionSeconds, 0.0);
    EXPECT_GE(writerRecordedPacket->graphBarrierRecordingSeconds, 0.0);
    EXPECT_GE(writerRecordedPacket->taskRecordSeconds, 0.0);
    EXPECT_GE(readerRecordedPacket->commandListAcquisitionSeconds, 0.0);
    EXPECT_GE(readerRecordedPacket->graphBarrierRecordingSeconds, 0.0);
    EXPECT_GE(readerRecordedPacket->taskRecordSeconds, 0.0);
    EXPECT_EQ(
        recordingStatistics.commandListAcquisitionSeconds,
        writerRecordedPacket->commandListAcquisitionSeconds + readerRecordedPacket->commandListAcquisitionSeconds
    );
    EXPECT_EQ(
        recordingStatistics.graphBarrierRecordingSeconds,
        writerRecordedPacket->graphBarrierRecordingSeconds + readerRecordedPacket->graphBarrierRecordingSeconds
    );
    EXPECT_EQ(
        recordingStatistics.taskRecordSeconds,
        writerRecordedPacket->taskRecordSeconds + readerRecordedPacket->taskRecordSeconds
    );
    EXPECT_EQ(
        recordingStatistics.recordingSeconds,
        writerRecordedPacket->recordingSeconds + readerRecordedPacket->recordingSeconds
    );
    EXPECT_GE(recordingStatistics.recordingSeconds, 0.0);
    EXPECT_EQ(recordingStatistics.recordingElapsedSeconds, recordingStatistics.readyFrontierElapsedSeconds);
    EXPECT_EQ(recordingStatistics.readyFrontierWorkerBusySeconds, recordingStatistics.recordingSeconds);
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

    // A recording artifact alone cannot be combined with an unbound transaction snapshot from this plan: aggregate
    // telemetry is attempt-scoped, just like packet submission itself.
    EXPECT_FALSE(CollectGpuTaskGraphRuntimeStatistics(
        compiledGraph,
        views.compiled,
        recordedGraph,
        transaction
    ).valid());

    const GpuTaskScheduler submitter(device);
    NativeTaskAcceptanceObserver writerAcceptance;
    writerAcceptance.continueSubmission = false;
    const GpuTaskGraphTaskAcceptedCallback writerAcceptedCallback{
        .task = writer,
        .context = &writerAcceptance,
        .invoke = ObserveNativeTaskAcceptance,
    };
    GpuSubmissionPacketId stoppedPacket;
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        writer,
        writer,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &stoppedPacket,
        &writerAcceptedCallback,
        1u
    ));
    EXPECT_EQ(stoppedPacket, writerPacket);
    EXPECT_EQ(writerAcceptance.acceptedCount, 1u);
    const QueueSubmissionToken writerToken = transaction.taskToken(views.compiled, writer);
    EXPECT_TRUE(writerToken.valid());
    EXPECT_EQ(writerAcceptance.lastToken.value, writerToken.value);
    EXPECT_FALSE(transaction.packetToken(readerPacket).valid());

    NativePacketSubmissionHookObserver hookObserver;
    const QueueSubmissionPreSubmitHook rejectedHook{
        .context = &hookObserver,
        .invoke = RejectNativePacketSubmissionHook,
    };
    const GpuTaskGraphTaskSubmissionHook outsideRangeTaskSubmissionHook[] = {
        GpuTaskGraphTaskSubmissionHook{
            .task = writer,
            .hook = rejectedHook,
        },
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        reader,
        reader,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        nullptr,
        0u,
        outsideRangeTaskSubmissionHook,
        LengthOf(outsideRangeTaskSubmissionHook)
    ));
    EXPECT_EQ(hookObserver.invocationCount, 0u);

    const GpuTaskGraphTaskSubmissionHook readerTaskSubmissionHook[] = {
        GpuTaskGraphTaskSubmissionHook{
            .task = reader,
            .hook = rejectedHook,
        },
    };
    const GpuTaskGraphTaskSubmissionHook duplicateTaskSubmissionHooks[] = {
        readerTaskSubmissionHook[0],
        readerTaskSubmissionHook[0],
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        reader,
        reader,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        nullptr,
        0u,
        duplicateTaskSubmissionHooks,
        LengthOf(duplicateTaskSubmissionHooks)
    ));
    EXPECT_EQ(hookObserver.invocationCount, 0u);

    NativeTaskAcceptanceObserver readerAcceptance;
    const GpuTaskGraphTaskAcceptedCallback readerAcceptedCallback{
        .task = reader,
        .context = &readerAcceptance,
        .invoke = ObserveNativeTaskAcceptance,
    };
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        reader,
        reader,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        &readerAcceptedCallback,
        1u
    ));
    EXPECT_EQ(writerAcceptance.acceptedCount, 1u);
    EXPECT_EQ(readerAcceptance.acceptedCount, 1u);
    const QueueSubmissionToken readerToken = transaction.taskToken(views.compiled, reader);
    EXPECT_TRUE(readerToken.valid());
    EXPECT_EQ(readerAcceptance.lastToken.value, readerToken.value);
    EXPECT_TRUE(device.waitForIdle());

    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(submissionStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(submissionStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(submissionStatistics.acceptedPacketCount, 2u);
    EXPECT_EQ(submissionStatistics.acceptedTaskCount, 2u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(submissionStatistics.rejectedSubmissionCount, 0u);
    EXPECT_EQ(submissionStatistics.nativeCommandListCount, 2u);
    EXPECT_EQ(submissionStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(submissionStatistics.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(submissionStatistics.timelineWaitCount, 0u);
    EXPECT_EQ(submissionStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(submissionStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCountByQueueClass[CommandQueue::Graphics], 2u);
    EXPECT_EQ(submissionStatistics.nativeCommandListCountByQueueClass[CommandQueue::Graphics], 2u);
    EXPECT_EQ(submissionStatistics.timelineWaitCountByQueueClass[CommandQueue::Graphics], 0u);
    EXPECT_GE(submissionStatistics.submissionSeconds, 0.0);

    const GpuTaskGraphPhysicalQueueSubmissionStatistics queueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            queue.id
        );
    ASSERT_TRUE(queueStatistics.valid());
    EXPECT_EQ(queueStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(queueStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(queueStatistics.recordingAttemptGeneration, recordedGraph.recordingAttemptGeneration());
    EXPECT_EQ(queueStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(queueStatistics.queue, queue.id);
    EXPECT_EQ(queueStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(queueStatistics.acceptedPacketCount, 2u);
    EXPECT_EQ(queueStatistics.acceptedTaskCount, 2u);
    EXPECT_EQ(queueStatistics.rejectedPacketCount, 0u);
    EXPECT_EQ(queueStatistics.rejectedTaskCount, 0u);
    EXPECT_EQ(queueStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(queueStatistics.rejectedSubmissionCount, 0u);
    EXPECT_EQ(queueStatistics.nativeCommandListCount, 2u);
    EXPECT_EQ(queueStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(queueStatistics.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(queueStatistics.timelineWaitCount, 0u);
    EXPECT_EQ(queueStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(queueStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_GE(queueStatistics.submissionSeconds, 0.0);

    const GpuTaskGraphRuntimeStatistics runtimeStatistics = CollectGpuTaskGraphRuntimeStatistics(
        compiledGraph,
        views.compiled,
        recordedGraph,
        transaction
    );
    ASSERT_TRUE(runtimeStatistics.valid());
    EXPECT_EQ(runtimeStatistics.compile.packetCount, 2u);
    EXPECT_EQ(runtimeStatistics.recording.commandListCount, 2u);
    EXPECT_EQ(runtimeStatistics.submission.nativeSubmissionCount, 2u);
}


TEST_F(DescriptorBufferRoundTripTest, NormalGraphExecutorOrdersNonmonotonicReadyFrontiersByCompilerStateSeeds){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto createBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle frontierBuffer = createBuffer();
    const BufferHandle recordedStateBuffer = createBuffer();
    const BufferHandle independentBuffer = createBuffer();
    ASSERT_NE(frontierBuffer.get(), nullptr);
    ASSERT_NE(recordedStateBuffer.get(), nullptr);
    ASSERT_NE(independentBuffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId frontierResource = graph.importBuffer(
        frontierBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_source"))
            .setMarkerLabel("Runtime Frontier Source")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId recordedStateResource = graph.importBuffer(
        recordedStateBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_recorded_state"))
            .setMarkerLabel("Runtime Frontier Recorded State")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Unknown)
    );
    const GpuGraphResourceId independentResource = graph.importBuffer(
        independentBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_independent"))
            .setMarkerLabel("Runtime Frontier Independent")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(frontierResource.valid());
    ASSERT_TRUE(recordedStateResource.valid());
    ASSERT_TRUE(independentResource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskResourceUse frontierWriteUse{
        .resource = frontierResource,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    bool frontierRecorded = false;
    const GpuTaskId frontierTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_first"))
            .setMarkerLabel("Runtime Frontier First")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setResourceUses(&frontierWriteUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = frontierBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &frontierRecorded,
        }
    );
    ASSERT_TRUE(frontierTask.valid());

    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = frontierResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = recordedStateResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool producerRecorded = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketRecordedStateProducerTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_producer"))
            .setMarkerLabel("Runtime Frontier Producer")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setDependencies(&frontierTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        NativePacketRecordedStateProducerTask::Payload{
            .prerequisiteBuffer = frontierBuffer.get(),
            .expectedPrerequisiteState = ResourceStates::ShaderResource,
            .producedBuffer = recordedStateBuffer.get(),
            .producedState = ResourceStates::ShaderResource,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    const GpuTaskResourceUse consumerUse{
        .resource = recordedStateResource,
        .range = {},
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    bool consumerRecorded = false;
    const GpuTaskId consumerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_consumer"))
            .setMarkerLabel("Runtime Frontier Consumer")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(&consumerUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = recordedStateBuffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &consumerRecorded,
        }
    );
    ASSERT_TRUE(consumerTask.valid());

    // This late-declared packet is independent, so compiler order remains 0,1,2,3 while recording depth becomes
    // 0,1,2,0. The ready recorder must group the final packet back into frontier zero without quadratic rescans.
    const GpuTaskResourceUse independentUse{
        .resource = independentResource,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    bool independentRecorded = false;
    const GpuTaskId independentTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/runtime_frontier_independent_task"))
            .setMarkerLabel("Runtime Frontier Independent Task")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setResourceUses(&independentUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = independentBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &independentRecorded,
        }
    );
    ASSERT_TRUE(independentTask.valid());

    const GpuPhysicalQueueInfo queue{
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
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/runtime_frontier_recorded_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 4u);
    const GpuSubmissionPacketId frontierPacket = views.compiled.packetForTask(frontierTask);
    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId consumerPacket = views.compiled.packetForTask(consumerTask);
    const GpuSubmissionPacketId independentPacket = views.compiled.packetForTask(independentTask);
    ASSERT_TRUE(frontierPacket.valid());
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_TRUE(independentPacket.valid());
    EXPECT_LT(consumerPacket.index, independentPacket.index);
    EXPECT_EQ(views.compiled.packet(frontierPacket).plan->recordingFrontier, 0u);
    EXPECT_EQ(views.compiled.packet(producerPacket).plan->recordingFrontier, 1u);
    EXPECT_EQ(views.compiled.packet(consumerPacket).plan->recordingFrontier, 2u);
    EXPECT_EQ(views.compiled.packet(independentPacket).plan->recordingFrontier, 0u);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledConsumer = views.compiled.findTask(consumerTask);
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledConsumer.valid());
    ASSERT_EQ(compiledProducer.plan->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledConsumer.plan->prologueStateSeedCount, 1u);
    const GpuPacketStateSeed* const consumerStateSeeds = views.compiled.findTask(consumerTask).prologueStateSeeds;
    ASSERT_NE(consumerStateSeeds, nullptr);
    EXPECT_EQ(consumerStateSeeds[0u].resource, recordedStateResource);
    EXPECT_EQ(consumerStateSeeds[0u].sourcePacket, producerPacket);

    CpuTaskScheduler recordingWorkers(1u);
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = independentTask;
    normalExecution.readyFrontierScheduler = &recordingWorkers;
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(submitter.submit(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_TRUE(frontierRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(consumerRecorded);
    EXPECT_TRUE(independentRecorded);
    EXPECT_TRUE(transaction.taskToken(views.compiled, frontierTask).valid());
    EXPECT_TRUE(transaction.taskToken(views.compiled, producerTask).valid());
    EXPECT_TRUE(transaction.taskToken(views.compiled, consumerTask).valid());
    EXPECT_TRUE(transaction.taskToken(views.compiled, independentTask).valid());
    const Optional<GpuRecordedPacket> producerRecording = recordedGraph.packetSnapshot(producerPacket);
    const Optional<GpuRecordedPacket> consumerRecording = recordedGraph.packetSnapshot(consumerPacket);
    const Optional<GpuRecordedPacket> independentRecording = recordedGraph.packetSnapshot(independentPacket);
    ASSERT_TRUE(producerRecording.has_value());
    ASSERT_TRUE(consumerRecording.has_value());
    ASSERT_TRUE(independentRecording.has_value());
    EXPECT_LE(independentRecording->recordingEndNanoseconds, producerRecording->recordingBeginNanoseconds);
    EXPECT_LE(producerRecording->recordingEndNanoseconds, consumerRecording->recordingBeginNanoseconds);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

