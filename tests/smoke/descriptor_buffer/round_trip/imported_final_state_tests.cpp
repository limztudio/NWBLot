// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "external_state_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Exercise one imported terminal-state scenario after the task performs an incompatible local transition. Both
// callers below retain explicit coverage while sharing the native record, submit, snapshot, and handoff assertions.
static void ExpectImportedFinalStateExportAfterTaskLocalTransition(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const bool keepInitialState,
    const ResourceStates::Mask externalFinalState
){
    const GpuPhysicalQueueId graphicsPhysicalQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsPhysicalQueue.valid());
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(keepInitialState)
    );
    ASSERT_NE(texture.get(), nullptr);
    Texture* const initialTextures[] = { texture.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(arena);
    const GpuGraphResourceId resource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/external_final_texture"))
            .setMarkerLabel("External Final Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Common)
            .setExternalFinalState(externalFinalState)
            .setExternalFinalReleaseDestinationQueue(graphicsPhysicalQueue)
    );
    ASSERT_TRUE(resource.valid());

    const GpuTaskResourceUse uses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
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
        .setIdentity(Name("tests/descriptor_buffer/external_final_texture_probe"))
        .setMarkerLabel("External Final Texture Probe")
        .setQueue(graphicsQueue)
        .setResourceUses(uses, LengthOf(uses))
    ;
    ResourceStates::Mask observedEntryState = ResourceStates::Unknown;
    bool taskRecorded = false;
    const GpuTaskId task = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        taskDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &observedEntryState,
            .localFinalState = ResourceStates::CopyDest,
            .recorded = &taskRecorded,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(arena);
    GpuTaskGraphQueueAssignments assignments(arena);
    GpuCompiledGraph compiledGraph(arena);
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/external_final_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
    ASSERT_TRUE(compiledTask.valid());
    const GpuCompiledExternalResourceExportView exportInfo = views.compiled.externalResourceExport(resource);
    ASSERT_TRUE(exportInfo.valid());
    EXPECT_EQ(exportInfo.plan->producerTask, task);
    EXPECT_EQ(exportInfo.plan->sourceQueue, graphicsPhysicalQueue);
    EXPECT_EQ(exportInfo.plan->destinationQueue, graphicsPhysicalQueue);
    ASSERT_EQ(compiledTask.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const prologue = compiledTask.prologueBarriers;
    ASSERT_NE(prologue, nullptr);
    EXPECT_EQ(prologue[0u].type, GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(prologue[0u].before, ResourceStates::Common);
    EXPECT_EQ(prologue[0u].after, ResourceStates::ShaderResource);
    ASSERT_EQ(compiledTask.plan->epilogueBarrierCount, 1u);
    const GpuCompiledBarrier* const epilogue = compiledTask.epilogueBarriers;
    ASSERT_NE(epilogue, nullptr);
    EXPECT_EQ(epilogue[0u].type, GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(epilogue[0u].resource, resource);
    EXPECT_EQ(epilogue[0u].before, ResourceStates::ShaderResource);
    EXPECT_EQ(epilogue[0u].after, externalFinalState);

    GpuRecordedGraph recordedGraph(arena);
    GpuGraphSubmissionTransaction transaction(arena);
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(taskRecorded);
    EXPECT_EQ(observedEntryState, ResourceStates::ShaderResource);
    CommandListResourceStateHandoff finalState(arena);
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, task, finalState));
    // The task-local CopyDest transition is retained by the native tracker, so this proves the graph export was
    // reasserted before the packet snapshot was captured.
    EXPECT_FALSE(finalState.empty());
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(&finalState);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), externalFinalState);
    stateProbe->close();

    Atomic<bool> handoffQueryStarted{ false };
    Atomic<bool> stopHandoffQueries{ false };
    Atomic<u32> handoffQueryCount{ 0u };
    Atomic<bool> handoffContractViolationObserved{ false };
    Atomic<bool> postAcceptanceHandoffObserved{ false };
    GpuTaskGraphExternalResourceHandoffSnapshot firstHandoffSnapshot(arena);
    GpuTaskGraphExternalResourceHandoffSnapshot secondHandoffSnapshot(arena);
    Thread handoffQueryThread([&](){
        const GpuTaskGraphReadViews queryViews(graph, compiledGraph);
        handoffQueryStarted.store(true, MemoryOrder::release);
        handoffQueryStarted.notify_all();
        while(!stopHandoffQueries.load(MemoryOrder::acquire)){
            const QueueSubmissionToken acceptedToken = transaction.packetToken(packet);
            const bool firstHandoffSucceeded = transaction.externalResourceHandoff(
                graph,
                queryViews.declarations,
                compiledGraph,
                queryViews.compiled,
                recordedGraph,
                resource,
                firstHandoffSnapshot
            );
            const bool secondHandoffSucceeded = transaction.externalResourceHandoff(
                graph,
                queryViews.declarations,
                compiledGraph,
                queryViews.compiled,
                recordedGraph,
                resource,
                secondHandoffSnapshot
            );
            if(acceptedToken.valid()){
                if(
                    !firstHandoffSucceeded
                    || !secondHandoffSucceeded
                    || !firstHandoffSnapshot.validFor(compiledGraph, queryViews.compiled)
                    || !secondHandoffSnapshot.validFor(compiledGraph, queryViews.compiled)
                )
                    handoffContractViolationObserved.store(true, MemoryOrder::release);
                else
                    postAcceptanceHandoffObserved.store(true, MemoryOrder::release);
            }
            const u32 completedQueryCount = handoffQueryCount.load(MemoryOrder::relaxed);
            handoffQueryCount.store(completedQueryCount + 1u, MemoryOrder::release);
            YieldThread();
        }
    });
    while(!handoffQueryStarted.load(MemoryOrder::acquire))
        handoffQueryStarted.wait(false, MemoryOrder::acquire);
    while(handoffQueryCount.load(MemoryOrder::acquire) < 64u)
        YieldThread();

    const GpuTaskGraphSubmitter submitter(device);
    const bool submitted = submitter.submitPacketRangeInCompileOrder(
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
    );
    const u32 postSubmissionQueryTarget = handoffQueryCount.load(MemoryOrder::acquire) + 64u;
    while(handoffQueryCount.load(MemoryOrder::acquire) < postSubmissionQueryTarget)
        YieldThread();
    stopHandoffQueries.store(true, MemoryOrder::release);
    handoffQueryThread.join();

    ASSERT_TRUE(submitted);
    EXPECT_FALSE(handoffContractViolationObserved.load(MemoryOrder::acquire));
    EXPECT_TRUE(postAcceptanceHandoffObserved.load(MemoryOrder::acquire));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    GpuTaskGraphExternalResourceHandoffSnapshot handoffSnapshot(arena);
    ASSERT_TRUE(transaction.externalResourceHandoff(
        graph,
        views.declarations,
        compiledGraph,
        views.compiled,
        recordedGraph,
        resource,
        handoffSnapshot
    ));
    ASSERT_TRUE(handoffSnapshot.validFor(compiledGraph, views.compiled));
    const GpuTaskGraphExternalResourceHandoff* const handoff = handoffSnapshot.value();
    ASSERT_NE(handoff, nullptr);
    EXPECT_EQ(handoff->producerTask, task);
    EXPECT_EQ(handoff->sourceQueue, graphicsPhysicalQueue);
    EXPECT_EQ(handoff->destinationQueue, graphicsPhysicalQueue);
    EXPECT_EQ(handoff->finalState, externalFinalState);
    EXPECT_TRUE(handoff->token.matchesPhysicalQueue(
        graphicsPhysicalQueue.index,
        graphicsPhysicalQueue.deviceGeneration
    ));
    EXPECT_NE(handoff->stateSource, &finalState);
    stateProbe->open(handoff->stateSource);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), externalFinalState);
    stateProbe->close();
    EXPECT_TRUE(device.waitForIdle());
}


// The compiler establishes ShaderResource for the task, while its local compatibility transition changes that state
// to CopyDest. A non-retained import may export the different ShaderResource state in its accepted packet snapshot.
TEST_F(DescriptorBufferRoundTripTest, ImportedFinalStateExportReassertsStateAfterTaskLocalTransition){
    ExpectImportedFinalStateExportAfterTaskLocalTransition(
        device(),
        arena(),
        false,
        ResourceStates::ShaderResource
    );
}


// A retained texture is restored at native close. Its graph-owned terminal export must agree on Common, and the
// accepted packet snapshot and transaction handoff must both publish that actual restored state.
TEST_F(DescriptorBufferRoundTripTest, RetainedImportedFinalStateMatchesCloseTimeRestoration){
    ExpectImportedFinalStateExportAfterTaskLocalTransition(
        device(),
        arena(),
        true,
        ResourceStates::Common
    );
}


// A graph-owned terminal export may release an exclusive resource to a direct native consumer. The graph publishes
// the accepting source token and exact state snapshot together; the Compute list opens from that snapshot and waits
// on that token, so neither caller reconstructs a packet ID or manually emits a queue-family acquire.
TEST_F(DescriptorBufferRoundTripTest, ExternalFinalHandoffReleasesToDedicatedCompute){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "External-final handoff: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "External-final handoff: adapter has no dedicated compute-only queue family.";

    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    const GpuPhysicalQueueId computeQueue = BackendQueueId(device, CommandQueue::Compute);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(computeQueue.valid());
    ASSERT_NE(graphicsQueue, computeQueue);

    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(asyncScope.arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/external_final_compute_buffer"))
            .setMarkerLabel("External Final Compute Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
            .setExternalFinalState(ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(computeQueue)
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
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/external_final_graphics_writer"))
        .setMarkerLabel("External Final Graphics Writer")
        .setQueue(graphicsRequest)
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
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/external_final_handoff_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
    ASSERT_TRUE(compiledTask.valid());
    EXPECT_EQ(compiledTask.plan->queue, graphicsQueue);
    const GpuCompiledExternalResourceExportView exportInfo = views.compiled.externalResourceExport(resource);
    ASSERT_TRUE(exportInfo.valid());
    EXPECT_EQ(exportInfo.plan->producerTask, task);
    EXPECT_EQ(exportInfo.plan->sourceQueue, graphicsQueue);
    EXPECT_EQ(exportInfo.plan->destinationQueue, computeQueue);

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(taskRecorded);
    GpuTaskGraphExternalResourceHandoffSnapshot handoffSnapshot(asyncScope.arena());
    EXPECT_FALSE(transaction.externalResourceHandoff(
        graph,
        views.declarations,
        compiledGraph,
        views.compiled,
        recordedGraph,
        resource,
        handoffSnapshot
    ));
    EXPECT_FALSE(handoffSnapshot.valid());

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
    ASSERT_TRUE(transaction.externalResourceHandoff(
        graph,
        views.declarations,
        compiledGraph,
        views.compiled,
        recordedGraph,
        resource,
        handoffSnapshot
    ));
    ASSERT_TRUE(handoffSnapshot.validFor(compiledGraph, views.compiled));
    const GpuTaskGraphExternalResourceHandoff* const handoff = handoffSnapshot.value();
    ASSERT_NE(handoff, nullptr);
    EXPECT_EQ(handoff->resource, resource);
    EXPECT_EQ(handoff->producerTask, task);
    EXPECT_EQ(handoff->sourceQueue, graphicsQueue);
    EXPECT_EQ(handoff->destinationQueue, computeQueue);
    EXPECT_EQ(handoff->finalState, ResourceStates::ShaderResource);
    EXPECT_TRUE(handoff->token.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));

    CommandListParameters computeParameters;
    computeParameters.setQueueType(CommandQueue::Compute);
    const CommandListHandle computeConsumer = device.createCommandList(computeParameters);
    ASSERT_NE(computeConsumer.get(), nullptr);
    computeConsumer->open(handoff->stateSource);
    EXPECT_EQ(computeConsumer->getBufferState(buffer.get()), ResourceStates::ShaderResource);
    computeConsumer->close();
    CommandList* const consumerLists[] = { computeConsumer.get() };
    QueueSubmissionDesc consumerSubmission;
    consumerSubmission.setWaitTokens(&handoff->token, 1u);
    const QueueSubmissionToken consumerToken = device.executeCommandLists(
        consumerLists,
        LengthOf(consumerLists),
        computeQueue,
        consumerSubmission
    );
    ASSERT_TRUE(consumerToken.valid());
    EXPECT_TRUE(consumerToken.matchesPhysicalQueue(computeQueue.index, computeQueue.deviceGeneration));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

