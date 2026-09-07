// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_probes_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Unsplit Integration remains a typed graph callback after Extinction even though both tasks execute in one Graphics
// packet.  Its packed-output reads, transmittance write, timing ticket, and downstream sampling handoff must all be
// compiler-owned so neither native callback can hide a local state transition or a separate submission.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitTypedAvboitIntegrationTailSharesExtinctionGraphicsPacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitIntegrationLifecycleScope.identity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    const auto createWorkBuffer = [&device](const Name& debugName, const bool isConstantBuffer = false){
        BufferDesc description;
        description
            .setDebugName(debugName)
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
        ;
        if(isConstantBuffer)
            description.setIsConstantBuffer(true);
        return device.createBuffer(description);
    };
    auto stateProbe = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_state_probe"),
        true
    );
    auto depthWarp = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_depth_warp"));
    auto control = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_control"));
    auto extinction = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_extinction"));
    auto extinctionOverflow = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_extinction_overflow")
    );
    auto transmittance = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_transmittance")
    );
    ASSERT_TRUE(stateProbe && depthWarp && control && extinction && extinctionOverflow && transmittance);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const AStringView markerLabel){
        const BufferDesc& description = buffer->getDescription();
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(description.debugName)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId stateProbeResource = importBuffer(
        stateProbe,
        "Unsplit Typed AVBOIT Integration State Probe"
    );
    const GpuGraphResourceId depthWarpResource = importBuffer(depthWarp, "AVBOIT Depth Warp");
    const GpuGraphResourceId controlResource = importBuffer(control, "AVBOIT Control");
    const GpuGraphResourceId extinctionResource = importBuffer(extinction, "AVBOIT Extinction");
    const GpuGraphResourceId extinctionOverflowResource = importBuffer(
        extinctionOverflow,
        "AVBOIT Extinction Overflow"
    );
    const GpuGraphResourceId transmittanceResource = importBuffer(transmittance, "AVBOIT Transmittance");
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(depthWarpResource.valid());
    ASSERT_TRUE(controlResource.valid());
    ASSERT_TRUE(extinctionResource.valid());
    ASSERT_TRUE(extinctionOverflowResource.valid());
    ASSERT_TRUE(transmittanceResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint tailScheduling = preScheduling;
    tailScheduling.cost = GpuTaskCostHint::Medium;
    tailScheduling.mergeWithPrevious = true;
    tailScheduling.allowMergeAcrossConsumerFrontier = true;

    const GpuTaskResourceUse preUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse extinctionUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = depthWarpResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = extinctionOverflowResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse integrationUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = controlResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionOverflowResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = transmittanceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse accumulationUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = transmittanceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    GpuTimingSubmissionTicket preTimingTicket(timing);
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool extinctionRecorded = false;
    bool integrationRecorded = false;
    bool accumulationRecorded = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken extinctionAcceptedToken;
    QueueSubmissionToken integrationAcceptedToken;
    QueueSubmissionToken accumulationAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload prePayload;
    prePayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    prePayload.expectationCount = 1u;
    prePayload.recordOrdinal = &recordOrdinal;
    prePayload.expectedOrdinal = 0u;
    prePayload.timingTicket = &preTimingTicket;
    prePayload.recorded = &preRecorded;
    prePayload.acceptedToken = &preAcceptedToken;
    const GpuTaskId preTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_pre_task"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses)),
        Move(prePayload)
    );
    ASSERT_TRUE(preTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload extinctionPayload;
    extinctionPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    extinctionPayload.expectations[1u] = { depthWarp.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[2u] = { control.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[3u] = { extinction.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectations[4u] = { extinctionOverflow.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectationCount = 5u;
    extinctionPayload.recordOrdinal = &recordOrdinal;
    extinctionPayload.expectedOrdinal = 1u;
    extinctionPayload.timingTicket = &preTimingTicket;
    extinctionPayload.recorded = &extinctionRecorded;
    extinctionPayload.acceptedToken = &extinctionAcceptedToken;
    const GpuTaskId extinctionTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_typed_avboit_extinction_task"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses)),
        Move(extinctionPayload)
    );
    ASSERT_TRUE(extinctionTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload integrationPayload;
    integrationPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    integrationPayload.expectations[1u] = { extinction.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[2u] = { control.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[3u] = { extinctionOverflow.get(), ResourceStates::ShaderResource };
    integrationPayload.expectations[4u] = { transmittance.get(), ResourceStates::UnorderedAccess };
    integrationPayload.expectationCount = 5u;
    integrationPayload.recordOrdinal = &recordOrdinal;
    integrationPayload.expectedOrdinal = 2u;
    integrationPayload.device = &device;
    integrationPayload.timing = &timing;
    integrationPayload.timingTicket = &preTimingTicket;
    integrationPayload.timingScope = &s_UnsplitAvboitIntegrationLifecycleScope;
    integrationPayload.recordTiming = true;
    integrationPayload.recorded = &integrationRecorded;
    integrationPayload.acceptedToken = &integrationAcceptedToken;
    const GpuTaskId integrationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_task"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&extinctionTask, 1u)
            .setResourceUses(integrationUses, LengthOf(integrationUses)),
        Move(integrationPayload)
    );
    ASSERT_TRUE(integrationTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload accumulationPayload;
    accumulationPayload.expectations[0u] = { stateProbe.get(), ResourceStates::ConstantBuffer };
    accumulationPayload.expectations[1u] = { transmittance.get(), ResourceStates::ShaderResource };
    accumulationPayload.expectationCount = 2u;
    accumulationPayload.recordOrdinal = &recordOrdinal;
    accumulationPayload.expectedOrdinal = 3u;
    accumulationPayload.timingTicket = &preTimingTicket;
    accumulationPayload.recorded = &accumulationRecorded;
    accumulationPayload.acceptedToken = &accumulationAcceptedToken;
    const GpuTaskId accumulationTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_accumulation_task"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(tailScheduling)
            .setDependencies(&integrationTask, 1u)
            .setResourceUses(accumulationUses, LengthOf(accumulationUses)),
        Move(accumulationPayload)
    );
    ASSERT_TRUE(accumulationTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_typed_avboit_integration_tail_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, extinctionTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(integrationTask, accumulationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(extinctionTask, integrationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(integrationTask, accumulationTask));
    const GpuTaskId tasks[] = {
        preTask,
        extinctionTask,
        integrationTask,
        accumulationTask,
    };
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);
    const auto expectAssignment = [&](const GpuTaskId task){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    };
    for(const GpuTaskId task : tasks)
        expectAssignment(task);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    for(const GpuTaskId task : tasks)
        EXPECT_EQ(packet, views.compiled.packetForTask(task));
    EXPECT_TRUE(views.compiled.tasksSharePacket(preTask, accumulationTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(preTask, extinctionTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(extinctionTask, integrationTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(integrationTask, accumulationTask));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, LengthOf(tasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], preTask);
    EXPECT_EQ(packetTasks[1u], extinctionTask);
    EXPECT_EQ(packetTasks[2u], integrationTask);
    EXPECT_EQ(packetTasks[3u], accumulationTask);

    const auto hasBufferTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == primaryGraphicsQueue
                && barrier.destinationQueue == primaryGraphicsQueue
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        depthWarpResource,
        ResourceStates::Common,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        controlResource,
        ResourceStates::Common,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionOverflowResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        extinctionOverflowResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        integrationTask,
        transmittanceResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasBufferTransition(
        accumulationTask,
        transmittanceResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::ShaderResource
    ));

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(extinctionRecorded);
    EXPECT_TRUE(integrationRecorded);
    EXPECT_TRUE(accumulationRecorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(stateProbe.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(depthWarp.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(control.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(extinction.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(extinctionOverflow.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(transmittance.get()), ResourceStates::ShaderResource);
    finalStateProbe->close();

    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        accumulationTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_EQ(packetToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(packetToken.physicalQueueIndex, primaryGraphicsQueue.index);
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    for(const GpuTaskId task : tasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(extinctionAcceptedToken);
    expectPacketToken(integrationAcceptedToken);
    expectPacketToken(accumulationAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_UnsplitAvboitIntegrationLifecycleScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

