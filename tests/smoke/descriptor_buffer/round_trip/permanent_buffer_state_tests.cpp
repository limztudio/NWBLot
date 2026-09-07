// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Permanent state is an exact native contract, not merely a hint that suppresses transitions. Matching UAV use
// still records its dependency barrier, while conflicting graph entry/export requests reject at the barrier
// boundary. Invalid permanent setters must not leak contradictory state into a packet handoff.
TEST_F(DescriptorBufferRoundTripTest, PermanentBufferStateValidatesMatchingUavAndRejectsGraphContradictions){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferHandle matchingBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle transitionMismatchBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle exportMismatchBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle unknownCandidate = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle retainedCandidate = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(matchingBuffer.get(), nullptr);
    ASSERT_NE(transitionMismatchBuffer.get(), nullptr);
    ASSERT_NE(exportMismatchBuffer.get(), nullptr);
    ASSERT_NE(unknownCandidate.get(), nullptr);
    ASSERT_NE(retainedCandidate.get(), nullptr);

    CommandListResourceStateHandoff handoff(DescriptorBufferRoundTripTest::arena());
    const CommandListHandle producer = device.createCommandList();
    const CommandListHandle handoffVerifier = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(handoffVerifier.get(), nullptr);
    producer->open();
    producer->setPermanentBufferState(matchingBuffer.get(), ResourceStates::UnorderedAccess);
    producer->setPermanentBufferState(transitionMismatchBuffer.get(), ResourceStates::UnorderedAccess);
    producer->setPermanentBufferState(exportMismatchBuffer.get(), ResourceStates::UnorderedAccess);
    EXPECT_EQ(producer->getPermanentBufferState(matchingBuffer.get()), ResourceStates::UnorderedAccess);

    // This matching repeated call must emit the same-state UAV dependency while retaining the permanent state.
    // Invalid setter attempts have their own disposable recording-attempt test above; this producer remains valid
    // so its handoff and accepted submission continue proving the matching contract.
    producer->setPermanentBufferState(matchingBuffer.get(), ResourceStates::UnorderedAccess);
    EXPECT_EQ(producer->getPermanentBufferState(matchingBuffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(producer->getPermanentBufferState(matchingBuffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(producer->getBufferState(matchingBuffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(producer->getPermanentBufferState(unknownCandidate.get()), ResourceStates::Unknown);
    EXPECT_EQ(producer->getPermanentBufferState(retainedCandidate.get()), ResourceStates::Unknown);
    EXPECT_EQ(producer->getBufferState(retainedCandidate.get()), ResourceStates::Common);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());

    handoffVerifier->open(&handoff);
    EXPECT_EQ(handoffVerifier->getPermanentBufferState(matchingBuffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(handoffVerifier->getPermanentBufferState(unknownCandidate.get()), ResourceStates::Unknown);
    EXPECT_EQ(handoffVerifier->getPermanentBufferState(retainedCandidate.get()), ResourceStates::Unknown);
    EXPECT_EQ(handoffVerifier->getBufferState(retainedCandidate.get()), ResourceStates::Common);
    handoffVerifier->close();

    CommandList* const producerLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists,
        LengthOf(producerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId matchingResource = graph.importBuffer(
        matchingBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/permanent_matching_uav"))
            .setMarkerLabel("Permanent Matching UAV")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::UnorderedAccess)
    );
    const GpuGraphResourceId transitionMismatchResource = graph.importBuffer(
        transitionMismatchBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/permanent_transition_mismatch"))
            .setMarkerLabel("Permanent Transition Mismatch")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::CopySource)
    );
    const GpuGraphResourceId exportMismatchResource = graph.importBuffer(
        exportMismatchBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/permanent_export_mismatch"))
            .setMarkerLabel("Permanent Export Mismatch")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::UnorderedAccess)
            .setExternalFinalState(ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(matchingResource.valid());
    ASSERT_TRUE(transitionMismatchResource.valid());
    ASSERT_TRUE(exportMismatchResource.valid());

    const GpuTaskExternalStateSource stateSources[] = {
        GpuTaskExternalStateSource{ .states = &handoff },
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskResourceUse matchingUse{
        .resource = matchingResource,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::ReadWrite,
    };
    const GpuTaskResourceUse transitionMismatchUse{
        .resource = transitionMismatchResource,
        .range = {},
        .requiredState = ResourceStates::CopySource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskResourceUse exportMismatchUse{
        .resource = exportMismatchResource,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::ReadWrite,
    };

    GpuTaskDesc matchingDesc;
    matchingDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_matching_uav_task"))
        .setMarkerLabel("Permanent Matching UAV Task")
        .setQueue(graphicsRequest)
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(&matchingUse, 1u)
    ;
    GpuTaskDesc transitionMismatchDesc;
    transitionMismatchDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_transition_mismatch_task"))
        .setMarkerLabel("Permanent Transition Mismatch Task")
        .setQueue(graphicsRequest)
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(&transitionMismatchUse, 1u)
    ;
    GpuTaskDesc exportMismatchDesc;
    exportMismatchDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_export_mismatch_task"))
        .setMarkerLabel("Permanent Export Mismatch Task")
        .setQueue(graphicsRequest)
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(&exportMismatchUse, 1u)
    ;
    GpuTaskDesc exportPrefixDesc;
    exportPrefixDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_export_prefix_task"))
        .setMarkerLabel("Permanent Export Prefix Task")
        .setQueue(graphicsRequest)
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(&exportMismatchUse, 1u)
    ;

    bool matchingRecorded = false;
    bool transitionMismatchAttempted = false;
    bool exportPrefixRecorded = false;
    bool exportMismatchRecorded = false;
    u32 exportPrefixDiscardedCount = 0u;
    u32 exportMismatchDiscardedCount = 0u;
    const bool shouldRecord = true;
    const GpuTaskId matchingTask = graph.addTask<NativePacketPrefixTask>(
        matchingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = matchingBuffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &matchingRecorded,
        }
    );
    const GpuTaskId transitionMismatchTask = graph.addTask<NativePacketCaptureRetryTask>(
        transitionMismatchDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &transitionMismatchAttempted,
        }
    );
    const GpuTaskId exportPrefixTask = graph.addTask<NativePacketPrefixTask>(
        exportPrefixDesc,
        NativePacketPrefixTask::Payload{
            .buffer = exportMismatchBuffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &exportPrefixRecorded,
            .discardedCount = &exportPrefixDiscardedCount,
        }
    );
    const GpuTaskId exportDependencies[] = { exportPrefixTask };
    GpuTaskSchedulingHint exportScheduling;
    exportScheduling.mergeWithPrevious = true;
    exportMismatchDesc
        .setDependencies(exportDependencies, LengthOf(exportDependencies))
        .setScheduling(exportScheduling)
    ;
    const GpuTaskId exportMismatchTask = graph.addTask<NativePacketPrefixTask>(
        exportMismatchDesc,
        NativePacketPrefixTask::Payload{
            .buffer = exportMismatchBuffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &exportMismatchRecorded,
            .discardedCount = &exportMismatchDiscardedCount,
        }
    );
    ASSERT_TRUE(matchingTask.valid());
    ASSERT_TRUE(transitionMismatchTask.valid());
    ASSERT_TRUE(exportPrefixTask.valid());
    ASSERT_TRUE(exportMismatchTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/permanent_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }

    {
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    const GpuSubmissionPacketId matchingPacket = views.compiled.packetForTask(matchingTask);
    const GpuSubmissionPacketId transitionMismatchPacket = views.compiled.packetForTask(transitionMismatchTask);
    const GpuSubmissionPacketId exportPrefixPacket = views.compiled.packetForTask(exportPrefixTask);
    const GpuSubmissionPacketId exportMismatchPacket = views.compiled.packetForTask(exportMismatchTask);
    ASSERT_TRUE(matchingPacket.valid());
    ASSERT_TRUE(transitionMismatchPacket.valid());
    ASSERT_TRUE(exportPrefixPacket.valid());
    ASSERT_TRUE(exportMismatchPacket.valid());
    ASSERT_NE(matchingPacket, transitionMismatchPacket);
    ASSERT_NE(matchingPacket, exportMismatchPacket);
    ASSERT_NE(transitionMismatchPacket, exportMismatchPacket);
    ASSERT_EQ(exportPrefixPacket, exportMismatchPacket);
    ASSERT_TRUE(views.compiled.taskPrecedesInSamePacket(exportPrefixTask, exportMismatchTask));

    const GpuCompiledTaskView compiledMatching = views.compiled.findTask(matchingTask);
    const GpuCompiledTaskView compiledTransitionMismatch = views.compiled.findTask(transitionMismatchTask);
    const GpuCompiledTaskView compiledExportMismatch = views.compiled.findTask(exportMismatchTask);
    ASSERT_TRUE(compiledMatching.valid());
    ASSERT_TRUE(compiledTransitionMismatch.valid());
    ASSERT_TRUE(compiledExportMismatch.valid());
    ASSERT_EQ(compiledMatching.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledTransitionMismatch.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledExportMismatch.plan->epilogueBarrierCount, 1u);
    const GpuCompiledBarrier* const matchingBarriers = compiledMatching.prologueBarriers;
    const GpuCompiledBarrier* const transitionMismatchBarriers = compiledTransitionMismatch.prologueBarriers;
    const GpuCompiledBarrier* const exportMismatchBarriers = compiledExportMismatch.epilogueBarriers;
    ASSERT_NE(matchingBarriers, nullptr);
    ASSERT_NE(transitionMismatchBarriers, nullptr);
    ASSERT_NE(exportMismatchBarriers, nullptr);
    EXPECT_EQ(matchingBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(matchingBarriers[0u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(matchingBarriers[0u].after, ResourceStates::UnorderedAccess);
    EXPECT_TRUE(matchingBarriers[0u].isGraphInitialState);
    EXPECT_EQ(transitionMismatchBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(transitionMismatchBarriers[0u].after, ResourceStates::CopySource);
    EXPECT_EQ(exportMismatchBarriers[0u].type, GpuCompiledBarrierType::BufferStateExport);
    EXPECT_EQ(exportMismatchBarriers[0u].after, ResourceStates::ShaderResource);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = transitionMismatchPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_FALSE(transitionMismatchAttempted);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = exportMismatchPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_FALSE(exportPrefixRecorded);
    EXPECT_FALSE(exportMismatchRecorded);
    EXPECT_EQ(exportPrefixDiscardedCount, 1u);
    EXPECT_EQ(exportMismatchDiscardedCount, 1u);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = matchingPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(matchingRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = matchingPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(device.waitForIdle());
    EXPECT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    }
    EXPECT_TRUE(graph.tryReset());
}


// Declaration-time state snapshots are immutable. A malformed source therefore cannot become usable after task
// publication and must fail atomically even when its queue-class predicate would exclude one eventual assignment.
TEST_F(DescriptorBufferRoundTripTest, DeclarationRejectsInvalidStateSourceRegardlessOfQueueApplicability){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "State-source applicability: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "State-source applicability: adapter has no dedicated compute-only queue family.";

    const BufferHandle graphicsBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    const BufferHandle computeBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(graphicsBuffer.get(), nullptr);
    ASSERT_NE(computeBuffer.get(), nullptr);

    CommandListResourceStateHandoff invalidHandoff(asyncScope.arena());
    ASSERT_FALSE(invalidHandoff.valid());

    GpuTaskGraph graph(asyncScope.arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
    };
    const GpuGraphResourceId graphicsResource = importBuffer(
        graphicsBuffer,
        Name("tests/descriptor_buffer/state_source_applicability_graphics"),
        "State Source Applicability Graphics"
    );
    const GpuGraphResourceId computeResource = importBuffer(
        computeBuffer,
        Name("tests/descriptor_buffer/state_source_applicability_compute"),
        "State Source Applicability Compute"
    );
    ASSERT_TRUE(graphicsResource.valid());
    ASSERT_TRUE(computeResource.valid());
    usize taskCountBeforeInvalidSources = 0u;
    u64 revisionBeforeInvalidSources = 0u;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        taskCountBeforeInvalidSources = declarations.taskCount();
        revisionBeforeInvalidSources = declarations.declarationRevision();
    }

    const GpuTaskExternalStateSource stateSources[] = {
        GpuTaskExternalStateSource{
            .states = &invalidHandoff,
            .applicableConsumerQueueClass = CommandQueue::Compute,
        },
    };
    const auto addProbe = [&graph, &stateSources](
        const Name& identity,
        const AStringView label,
        const GpuGraphResourceId resource,
        const GpuQueueRequest& queue,
        const bool& shouldRecord,
        bool& attempted
    ){
        const GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::Common,
            .access = GpuTaskResourceAccess::Read,
        };
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(scheduling)
            .setExternalStateSources(stateSources, LengthOf(stateSources))
            .setResourceUses(&use, 1u)
        ;
        return graph.addTask<NativePacketCaptureRetryTask>(
            desc,
            NativePacketCaptureRetryTask::Payload{
                .shouldRecord = &shouldRecord,
                .attempted = &attempted,
            }
        );
    };
    const bool shouldRecord = true;
    bool graphicsAttempted = false;
    bool computeAttempted = false;
    const GpuTaskId graphicsTask = addProbe(
        Name("tests/descriptor_buffer/state_source_applicability_graphics_task"),
        "State Source Applicability Graphics Task",
        graphicsResource,
        GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        },
        shouldRecord,
        graphicsAttempted
    );
    const GpuTaskId computeTask = addProbe(
        Name("tests/descriptor_buffer/state_source_applicability_compute_task"),
        "State Source Applicability Compute Task",
        computeResource,
        GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            false,
            false,
        },
        shouldRecord,
        computeAttempted
    );
    EXPECT_FALSE(graphicsTask.valid());
    EXPECT_FALSE(computeTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), taskCountBeforeInvalidSources);
        EXPECT_EQ(declarations.declarationRevision(), revisionBeforeInvalidSources);
    }
    EXPECT_FALSE(graphicsAttempted);
    EXPECT_FALSE(computeAttempted);
}


// A permanent resource cannot participate in a Vulkan queue-family ownership handoff. The graph recorder must
// reject the compiler-planned release before recording the producer thunk or publishing any native packet work.
TEST_F(DescriptorBufferRoundTripTest, PermanentBufferOwnershipReleaseFailsClosedAcrossDedicatedQueues){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Permanent ownership release: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Permanent ownership release: adapter has no dedicated compute-only queue family.";

    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    const GpuPhysicalQueueId computeQueue = BackendQueueId(device, CommandQueue::Compute);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(computeQueue.valid());
    ASSERT_NE(graphicsQueue, computeQueue);

    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    CommandListResourceStateHandoff handoff(asyncScope.arena());
    const CommandListHandle nativeProducer = device.createCommandList();
    ASSERT_NE(nativeProducer.get(), nullptr);
    nativeProducer->open();
    nativeProducer->setPermanentBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    nativeProducer->close(&handoff);
    ASSERT_TRUE(handoff.valid());
    CommandList* const nativeProducerLists[] = { nativeProducer.get() };
    const QueueSubmissionToken nativeProducerToken = device.executeCommandLists(
        nativeProducerLists,
        LengthOf(nativeProducerLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(nativeProducerToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    GpuTaskGraph graph(asyncScope.arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/permanent_ownership_release_buffer"))
            .setMarkerLabel("Permanent Ownership Release Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::UnorderedAccess)
    );
    ASSERT_TRUE(resource.valid());

    const GpuTaskExternalStateSource stateSources[] = {
        GpuTaskExternalStateSource{ .states = &handoff },
    };
    const GpuTaskResourceUse producerUse{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::ReadWrite,
    };
    const GpuTaskResourceUse consumerUse{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_ownership_release_producer"))
        .setMarkerLabel("Permanent Ownership Release Producer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setExternalStateSources(stateSources, LengthOf(stateSources))
        .setResourceUses(&producerUse, 1u)
    ;
    bool producerRecorded = false;
    u32 producerDiscardedCount = 0u;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        producerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &producerRecorded,
            .discardedCount = &producerDiscardedCount,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/descriptor_buffer/permanent_ownership_release_consumer"))
        .setMarkerLabel("Permanent Ownership Release Consumer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            false,
            false,
        })
        .setDependencies(&producerTask, 1u)
        .setResourceUses(&consumerUse, 1u)
    ;
    bool consumerRecorded = false;
    const GpuTaskId consumerTask = graph.addTask<NativePacketPrefixTask>(
        consumerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &consumerRecorded,
        }
    );
    ASSERT_TRUE(consumerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/permanent_ownership_release_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId producerPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
        const GpuCompiledTaskView compiledConsumer = views.compiled.findTask(consumerTask);
        ASSERT_TRUE(compiledProducer.valid());
        ASSERT_TRUE(compiledConsumer.valid());
        EXPECT_EQ(compiledProducer.plan->queue, graphicsQueue);
        EXPECT_EQ(compiledConsumer.plan->queue, computeQueue);

        bool foundOwnershipRelease = false;
        const GpuCompiledBarrier* const epilogueBarriers = compiledProducer.epilogueBarriers;
        ASSERT_NE(epilogueBarriers, nullptr);
        for(u32 barrierIndex = 0u; barrierIndex < compiledProducer.plan->epilogueBarrierCount; ++barrierIndex){
            if(epilogueBarriers[barrierIndex].type == GpuCompiledBarrierType::BufferOwnershipRelease){
                foundOwnershipRelease = true;
                break;
            }
        }
        ASSERT_TRUE(foundOwnershipRelease);

        producerPacket = views.compiled.packetForTask(producerTask);
        ASSERT_TRUE(producerPacket.valid());
    }
    GpuRecordedGraph recordedGraph(asyncScope.arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = producerPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_FALSE(producerRecorded);
    EXPECT_EQ(producerDiscardedCount, 1u);
    EXPECT_FALSE(consumerRecorded);
    EXPECT_FALSE(recordedGraph.packetSnapshot(producerPacket).has_value());
    EXPECT_FALSE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    recordedGraph.reset(compiledGraph);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

