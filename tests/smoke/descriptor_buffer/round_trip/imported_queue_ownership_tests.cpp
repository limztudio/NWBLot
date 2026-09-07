// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// An imported exclusive resource may name its current physical owner. The first graph packet must use that exact
// queue; the test uses the Device's real topology so a stale synthetic queue ID cannot accidentally pass.
TEST_F(DescriptorBufferRoundTripTest, ImportedInitialOwnerMatchesFirstPacketQueue){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    const GpuPhysicalQueueId initialOwner = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(initialOwner.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/initial_owner_buffer"))
            .setMarkerLabel("Initial Owner Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
            .setInitialOwnerQueue(initialOwner)
    );
    ASSERT_TRUE(resource.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceAt(resource.index).initialOwnerQueue, initialOwner);
    }

    const GpuTaskResourceUse uses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/initial_owner_use"))
        .setMarkerLabel("Initial Owner Use")
        .setQueue(graphicsQueue)
        .setResourceUses(uses, LengthOf(uses))
    ;
    bool taskRecorded = false;
    const GpuTaskId task = graph.addTask<NativePacketPrefixTask>(
        taskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &taskRecorded,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/initial_owner_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
    ASSERT_TRUE(compiledTask.valid());
    EXPECT_EQ(compiledTask.plan->queue, initialOwner);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(taskRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// A completion with an authoritative graph-owned token must reject caller-side rebinding before acceptance, then
// retain the stored dependency while the same physical queue safely elides its redundant native timeline wait.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedExternalCompletionUsesStoredTokenAndElidesSameQueueWait){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    CommandListParameters producerParameters;
    producerParameters.setPhysicalQueue(graphicsQueue);
    const CommandListHandle producer = device.createCommandList(producerParameters);
    ASSERT_NE(producer.get(), nullptr);
    producer->open();
    producer->close();
    CommandList* const producerLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists,
        LengthOf(producerLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_TRUE(producerToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);

    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuExternalCompletionId completion = graph.importExternalCompletion(
        GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/descriptor_buffer/graph_owned_external_completion"))
            .setMarkerLabel("Graph Owned External Completion")
            .setToken(producerToken)
    );
    ASSERT_TRUE(completion.valid());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/graph_owned_external_buffer"))
            .setMarkerLabel("Graph Owned External Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(resource.valid());
    const GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_external_consumer"))
        .setMarkerLabel("Graph Owned External Consumer")
        .setQueue(graphicsRequest)
        .setExternalDependencies(&completion, 1u)
        .setResourceUses(&use, 1u)
    ;
    bool taskRecorded = false;
    QueueSubmissionToken acceptedToken;
    const GpuTaskId task = graph.addTask<NativePacketPrefixTask>(
        taskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &taskRecorded,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/graph_owned_external_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(taskRecorded);

    const GpuTaskGraphExternalCompletionToken forbiddenFallback{
        .completion = completion,
        .token = producerToken,
    };
    const GpuTaskGraphSubmitter submitter(device);
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        &forbiddenFallback,
        1u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.hasAcceptedPackets());
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    transaction.reset(compiledGraph);

    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.hasAcceptedPackets());
    const QueueSubmissionToken consumerToken = transaction.packetToken(packet);
    EXPECT_TRUE(consumerToken.valid());
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 0u);
    VulkanTestQueueSubmit2Capture consumerCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(1u, consumerCapture));
    ASSERT_EQ(consumerCapture.result, VK_SUCCESS);
    ASSERT_FALSE(consumerCapture.overflowed);
    EXPECT_EQ(consumerCapture.queue, nativeGraphicsQueue);
    ASSERT_EQ(consumerCapture.submitCount, 1u);
    ASSERT_FALSE(consumerCapture.submits[0u].overflowed);
    EXPECT_EQ(consumerCapture.submits[0u].waitCount, 0u);
    ASSERT_GT(consumerCapture.submits[0u].signalCount, 0u);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].value, consumerToken.value);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(consumerCapture.submits[0u].signals[0u].deviceIndex, 0u);
    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(submissionStatistics.sameQueueWaitElisionCount, 1u);
    EXPECT_EQ(submissionStatistics.timelineWaitCount, 0u);
    EXPECT_TRUE(device.waitForIdle());
}


// A source outside the graph may release an exclusive buffer to a fixed first graph packet. The graph owns the
// completion wait, imports the producer's actual state, then materializes its different declared initial state;
// the adapter gate is intentional because a real queue-family acquire needs a dedicated Compute family.
TEST_F(DescriptorBufferRoundTripTest, ImportedInitialOwnerHandoffWaitsAndAcquiresCrossQueue){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Initial-owner handoff: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Initial-owner handoff: adapter has no dedicated compute-only queue family.";

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

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);
    const CommandListHandle computePrimer = device.createCommandList(computeParams);
    ASSERT_NE(computePrimer.get(), nullptr);
    computePrimer->open();
    computePrimer->close();
    CommandList* const primerLists[] = { computePrimer.get() };
    const QueueSubmissionToken primerToken = device.executeCommandLists(
        primerLists,
        LengthOf(primerLists),
        computeQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(primerToken.valid());
    ASSERT_TRUE(primerToken.matchesPhysicalQueue(computeQueue.index, computeQueue.deviceGeneration));

    CommandListResourceStateHandoff computeState(asyncScope.arena());
    const CommandListHandle computeProducer = device.createCommandList(computeParams);
    ASSERT_NE(computeProducer.get(), nullptr);
    computeProducer->open();
    computeProducer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    computeProducer->releaseBufferOwnership(buffer.get(), graphicsQueue);
    computeProducer->close(&computeState);
    ASSERT_TRUE(computeState.valid());
    EXPECT_TRUE(computeState.coversBufferWithOwnership(buffer.get(), computeQueue, graphicsQueue));
    EXPECT_FALSE(computeState.coversBufferWithOwnership(buffer.get(), graphicsQueue, graphicsQueue));
    CommandList* const producerLists[] = { computeProducer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists,
        LengthOf(producerLists),
        computeQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    ASSERT_TRUE(producerToken.matchesPhysicalQueue(computeQueue.index, computeQueue.deviceGeneration));
    ASSERT_GT(producerToken.value, primerToken.value);

    CommandListParameters graphicsParams;
    graphicsParams.setQueueType(CommandQueue::Graphics);
    CommandListResourceStateHandoff staleGraphicsState(asyncScope.arena());
    const CommandListHandle staleGraphicsProducer = device.createCommandList(graphicsParams);
    ASSERT_NE(staleGraphicsProducer.get(), nullptr);
    staleGraphicsProducer->open();
    staleGraphicsProducer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    staleGraphicsProducer->close(&staleGraphicsState);
    ASSERT_TRUE(staleGraphicsState.valid());
    EXPECT_FALSE(staleGraphicsState.coversBufferWithOwnership(buffer.get(), computeQueue, graphicsQueue));

    GpuTaskGraph graph(asyncScope.arena());
    const GpuExternalCompletionId completion = graph.importExternalCompletion(
        GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/descriptor_buffer/initial_owner_compute_completion"))
            .setMarkerLabel("Initial Owner Compute Completion")
    );
    ASSERT_TRUE(completion.valid());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/initial_owner_compute_buffer"))
            .setMarkerLabel("Initial Owner Compute Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::ShaderResource)
            .setInitialOwnerQueue(computeQueue)
            .setInitialOwnerReleaseDestinationQueue(graphicsQueue)
            .setInitialOwnerCompletion(completion)
            .setInitialOwnerMinimumCompletionToken(producerToken)
            .setInitialOwnerStateSource(&computeState)
    );
    ASSERT_TRUE(resource.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        const GpuTaskGraphResourceView importedResource = declarations.resourceAt(resource.index);
        ASSERT_NE(importedResource.initialOwnerStateSource, nullptr);
        EXPECT_NE(importedResource.initialOwnerStateSource, &computeState);
        EXPECT_TRUE(importedResource.initialOwnerStateSource->valid());
    }
    // Graph declaration owns a deep state snapshot now. The producer's transient handoff can be released before
    // late native recording without losing the released queue-family layout/owner information.
    computeState.reset();
    EXPECT_FALSE(computeState.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.resourceAt(resource.index).initialOwnerStateSource->valid());
    }

    const GpuTaskResourceUse uses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/initial_owner_compute_use"))
        .setMarkerLabel("Initial Owner Compute Use")
        .setQueue(graphicsRequest)
        .setResourceUses(uses, LengthOf(uses))
    ;
    bool taskRecorded = false;
    const GpuTaskId task = graph.addTask<NativePacketPrefixTask>(
        taskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &taskRecorded,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/initial_owner_handoff_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId packet;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        ASSERT_TRUE(compiledTask.valid());
        EXPECT_EQ(compiledTask.plan->queue, graphicsQueue);
        ASSERT_EQ(compiledTask.plan->prologueBarrierCount, 2u);
        ASSERT_NE(compiledTask.prologueBarriers, nullptr);
        EXPECT_EQ(compiledTask.prologueBarriers[0u].type, GpuCompiledBarrierType::BufferOwnershipAcquire);
        EXPECT_TRUE(compiledTask.prologueBarriers[0u].isInitialOwnerHandoff);
        EXPECT_EQ(compiledTask.prologueBarriers[1u].type, GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(compiledTask.prologueBarriers[1u].before, ResourceStates::ShaderResource);
        EXPECT_EQ(compiledTask.prologueBarriers[1u].after, ResourceStates::ShaderResource);
        EXPECT_TRUE(compiledTask.prologueBarriers[1u].isGraphInitialState);
        packet = compiledTask.plan->packet;
        ASSERT_TRUE(packet.valid());
        const GpuCompiledPacketView compiledPacket = views.compiled.packet(packet);
        ASSERT_TRUE(compiledPacket.valid());
        EXPECT_EQ(compiledPacket.plan->externalDependencyCount, 1u);
        ASSERT_NE(compiledPacket.externalDependencies, nullptr);
        EXPECT_EQ(compiledPacket.externalDependencies[0u], completion);
    }

    const GpuNativePacketRecorder recorder(device);
    {
        GpuTaskGraph staleGraph(asyncScope.arena());
        const GpuExternalCompletionId staleCompletion = staleGraph.importExternalCompletion(
            GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/descriptor_buffer/stale_initial_owner_compute_completion"))
                .setMarkerLabel("Stale Initial Owner Compute Completion")
        );
        ASSERT_TRUE(staleCompletion.valid());
        const GpuGraphResourceId staleResource = staleGraph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/descriptor_buffer/stale_initial_owner_compute_buffer"))
                .setMarkerLabel("Stale Initial Owner Compute Buffer")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::ShaderResource)
                .setInitialOwnerQueue(computeQueue)
                .setInitialOwnerReleaseDestinationQueue(graphicsQueue)
                .setInitialOwnerCompletion(staleCompletion)
                .setInitialOwnerMinimumCompletionToken(producerToken)
                .setInitialOwnerStateSource(&staleGraphicsState)
        );
        ASSERT_TRUE(staleResource.valid());
        const GpuTaskResourceUse staleUses[] = {
            GpuTaskResourceUse{
                .resource = staleResource,
                .range = {},
                .requiredState = ResourceStates::ShaderResource,
                .access = GpuTaskResourceAccess::Read,
            },
        };
        GpuTaskDesc staleTaskDesc;
        staleTaskDesc
            .setIdentity(Name("tests/descriptor_buffer/stale_initial_owner_compute_use"))
            .setMarkerLabel("Stale Initial Owner Compute Use")
            .setQueue(graphicsRequest)
            .setResourceUses(staleUses, LengthOf(staleUses))
        ;
        bool staleTaskRecorded = false;
        const GpuTaskId staleTask = staleGraph.addTask<NativePacketPrefixTask>(
            staleTaskDesc,
            NativePacketPrefixTask::Payload{
                .buffer = buffer.get(),
                .expectedState = ResourceStates::ShaderResource,
                .recorded = &staleTaskRecorded,
            }
        );
        ASSERT_TRUE(staleTask.valid());

        GpuTaskGraphAnalysis staleAnalysis(asyncScope.arena());
        GpuTaskGraphQueueAssignments staleAssignments(asyncScope.arena());
        GpuCompiledGraph staleCompiledGraph(asyncScope.arena());
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(staleGraph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations,
                staleAnalysis,
                topology,
                staleAssignments,
                staleCompiledGraph,
                scratchArena
            ));
        }
        const GpuTaskGraphReadViews staleViews(staleGraph, staleCompiledGraph);
        ASSERT_TRUE(staleViews.valid());
        const GpuCompiledTaskView staleCompiledTask = staleViews.compiled.findTask(staleTask);
        ASSERT_TRUE(staleCompiledTask.valid());
        ASSERT_TRUE(staleCompiledTask.plan->packet.valid());
        GpuRecordedGraph staleRecordedGraph(asyncScope.arena());
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            staleGraph,
            staleCompiledGraph,
            GpuSubmissionPacketRange{ .first = staleCompiledTask.plan->packet, .packetCount = 1u },
            staleRecordedGraph
        ));
        EXPECT_FALSE(staleTaskRecorded);
    }

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(taskRecorded);

    const GpuTaskGraphExternalCompletionToken externalTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = completion,
            .token = producerToken,
        },
    };
    const GpuTaskGraphSubmitter submitter(device);
    QueueSubmissionToken wrongPhysicalToken = producerToken;
    wrongPhysicalToken.physicalQueueIndex = graphicsQueue.index;
    const GpuTaskGraphExternalCompletionToken wrongExternalTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = completion,
            .token = wrongPhysicalToken,
        },
    };
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        wrongExternalTokens,
        LengthOf(wrongExternalTokens),
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    transaction.reset(compiledGraph);
    QueueSubmissionToken staleToken = producerToken;
    --staleToken.value;
    const GpuTaskGraphExternalCompletionToken staleExternalTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = completion,
            .token = staleToken,
        },
    };
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        staleExternalTokens,
        LengthOf(staleExternalTokens),
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    transaction.reset(compiledGraph);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        externalTokens,
        LengthOf(externalTokens),
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken consumerToken = transaction.packetToken(packet);
    EXPECT_TRUE(consumerToken.valid());
    EXPECT_TRUE(consumerToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

