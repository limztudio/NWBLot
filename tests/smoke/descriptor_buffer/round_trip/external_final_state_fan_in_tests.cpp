// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "external_state_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


struct DeclarationMutationExternalHandoffState{
    const GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuRecordedGraph* recordedGraph = nullptr;
    const GpuGraphSubmissionTransaction* transaction = nullptr;
    GpuTaskGraphExternalResourceHandoffSnapshot* handoffSnapshot = nullptr;
    GpuGraphResourceId resource;
    bool attempted = false;
    bool succeeded = true;
};


struct DeclarationMutationExternalHandoffTask{
    struct Payload{
        DeclarationMutationExternalHandoffState* state = nullptr;

        explicit Payload(DeclarationMutationExternalHandoffState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(
                !state
                || !state->graph
                || !state->compiledGraph
                || !state->recordedGraph
                || !state->transaction
                || !state->handoffSnapshot
            )
                return;

            state->attempted = true;
            GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(
                *state->graph
            );
            if(!declarationAccess.valid()){
                state->succeeded = false;
                return;
            }
            const GpuCompiledGraph::ReadView planAccess(*state->compiledGraph);
            state->succeeded = state->transaction->externalResourceHandoff(
                *state->graph,
                declarationAccess,
                *state->compiledGraph,
                planAccess,
                *state->recordedGraph,
                state->resource,
                *state->handoffSnapshot
            );
            state->succeeded = state->succeeded && state->handoffSnapshot->validFor(
                *state->compiledGraph,
                planAccess
            );
        }
    };
};


struct DeclarationMutationTransactionRejectionState{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    GpuTaskId task;
    u64 recordingAttemptGeneration = 0u;
    bool rejectAttempted = false;
    bool discardAttempted = false;
    bool discardSucceeded = true;
};


struct DeclarationMutationTransactionRejectionTask{
    struct Payload{
        DeclarationMutationTransactionRejectionState* state = nullptr;

        explicit Payload(DeclarationMutationTransactionRejectionState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(!state || !state->graph || !state->compiledGraph || !state->transaction)
                return;

            state->rejectAttempted = true;
            state->transaction->rejectTask(
                *state->graph,
                *state->compiledGraph,
                state->task,
                state->recordingAttemptGeneration
            );
            state->discardAttempted = true;
            state->discardSucceeded = state->transaction->discardUnaccepted(
                *state->graph,
                *state->compiledGraph,
                state->recordingAttemptGeneration
            );
        }
    };
};


// Matching concurrent reads may deliberately omit their ordinary read/read dependency when the later packet has
// an independent state source. The terminal state export is different: its native transition must wait for every
// overlapping reader, so the compiler restores that one execution dependency without manufacturing a state seed.
TEST_F(DescriptorBufferRoundTripTest, ExternalFinalStateOrdersOverlappingIndependentCrossQueueReads){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "External-final read ordering: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "External-final read ordering: adapter has no dedicated compute-only queue family.";

    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    const GpuPhysicalQueueId computeQueue = BackendQueueId(device, CommandQueue::Compute);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(computeQueue.valid());
    ASSERT_NE(graphicsQueue, computeQueue);

    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(texture.get(), nullptr);
    CommandListResourceStateHandoff independentState(asyncScope.arena());
    const CommandListHandle stateProducer = device.createCommandList();
    ASSERT_NE(stateProducer.get(), nullptr);
    stateProducer->open();
    stateProducer->setTextureState(texture.get(), s_AllSubresources, ResourceStates::ShaderResource);
    stateProducer->close(&independentState);
    ASSERT_TRUE(independentState.valid());
    CommandList* const stateProducerLists[] = { stateProducer.get() };
    const QueueSubmissionToken stateProducerToken = device.executeCommandLists(
        stateProducerLists,
        LengthOf(stateProducerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(stateProducerToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    GpuTaskGraph graph(asyncScope.arena());
    const GpuGraphResourceId resource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/external_final_independent_cross_queue_texture"))
            .setMarkerLabel("External Final Independent Cross-Queue Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::ShaderResource)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setExternalFinalState(ResourceStates::CopySource)
    );
    ASSERT_TRUE(resource.valid());

    const TextureSubresourceSet subresources(0u, 1u, 0u, 1u);
    const GpuTaskResourceRange range{ .textureSubresources = subresources };
    const GpuTaskResourceUse graphicsUse{
        .resource = resource,
        .range = range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskResourceUse computeUse{
        .resource = resource,
        .range = range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = true,
    };
    const GpuTaskExternalStateSource computeStateSources[] = {
        GpuTaskExternalStateSource{ .states = &independentState },
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc graphicsDesc;
    graphicsDesc
        .setIdentity(Name("tests/descriptor_buffer/external_final_independent_graphics_read"))
        .setMarkerLabel("External Final Independent Graphics Read")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(&graphicsUse, 1u)
    ;
    GpuTaskDesc computeDesc;
    computeDesc
        .setIdentity(Name("tests/descriptor_buffer/external_final_independent_compute_read"))
        .setMarkerLabel("External Final Independent Compute Read")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setExternalStateSources(computeStateSources, LengthOf(computeStateSources))
        .setResourceUses(&computeUse, 1u)
    ;
    ResourceStates::Mask graphicsObservedState = ResourceStates::Unknown;
    ResourceStates::Mask computeObservedState = ResourceStates::Unknown;
    bool graphicsRecorded = false;
    bool computeRecorded = false;
    const GpuTaskId graphicsTask = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        graphicsDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &graphicsObservedState,
            .recorded = &graphicsRecorded,
        }
    );
    const GpuTaskId computeTask = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        computeDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &computeObservedState,
            .recorded = &computeRecorded,
        }
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/external_final_independent_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId graphicsPacket = views.compiled.packetForTask(graphicsTask);
    const GpuSubmissionPacketId computePacket = views.compiled.packetForTask(computeTask);
    ASSERT_TRUE(graphicsPacket.valid());
    ASSERT_TRUE(computePacket.valid());
    ASSERT_NE(graphicsPacket, computePacket);
    const GpuCompiledTaskView compiledGraphics = views.compiled.findTask(graphicsTask);
    const GpuCompiledTaskView compiledCompute = views.compiled.findTask(computeTask);
    ASSERT_TRUE(compiledGraphics.valid());
    ASSERT_TRUE(compiledCompute.valid());
    EXPECT_EQ(compiledGraphics.plan->queue, graphicsQueue);
    EXPECT_EQ(compiledCompute.plan->queue, computeQueue);
    EXPECT_EQ(compiledCompute.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledCompute.plan->prologueBarrierCount, 0u);
    EXPECT_EQ(views.compiled.packet(graphicsPacket).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(computePacket).plan->dependencyCount, 1u);
    const GpuPacketDependency* const computeDependencies = views.compiled.packet(computePacket).dependencies;
    ASSERT_NE(computeDependencies, nullptr);
    EXPECT_EQ(computeDependencies[0u].producer, graphicsPacket);
    EXPECT_EQ(computeDependencies[0u].consumer, computePacket);
    EXPECT_EQ(compiledGraphics.plan->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledCompute.plan->epilogueBarrierCount, 1u);
    const GpuCompiledBarrier* const computeEpilogue = views.compiled.findTask(computeTask).epilogueBarriers;
    ASSERT_NE(computeEpilogue, nullptr);
    EXPECT_EQ(computeEpilogue[0u].type, GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(computeEpilogue[0u].resource, resource);
    EXPECT_EQ(computeEpilogue[0u].range.textureSubresources, subresources);
    EXPECT_EQ(computeEpilogue[0u].before, ResourceStates::ShaderResource);
    EXPECT_EQ(computeEpilogue[0u].after, ResourceStates::CopySource);
    EXPECT_EQ(computeEpilogue[0u].sourceQueue, computeQueue);
    EXPECT_EQ(computeEpilogue[0u].destinationQueue, computeQueue);
    const GpuTaskGraphCompileStatistics& compileStatistics = views.compiled.compileStatistics();
    ASSERT_TRUE(compileStatistics.valid());
    EXPECT_EQ(compileStatistics.crossQueuePacketDependencyCount, 1u);
    EXPECT_EQ(compileStatistics.crossFamilyPacketDependencyCount, 1u);

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = graphicsPacket, .packetCount = 1u },
        recordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = computePacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(graphicsRecorded);
    EXPECT_TRUE(computeRecorded);
    EXPECT_EQ(graphicsObservedState, ResourceStates::ShaderResource);
    EXPECT_EQ(computeObservedState, ResourceStates::ShaderResource);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        computeTask,
        finalStateStorage
    ));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    EXPECT_FALSE(finalState->empty());
    CommandListParameters finalStateProbeParameters;
    finalStateProbeParameters.setPhysicalQueue(computeQueue);
    const CommandListHandle finalStateProbe = device.createCommandList(finalStateProbeParameters);
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(
        finalStateProbe->getTextureSubresourceState(texture.get(), 0u, 0u),
        ResourceStates::CopySource
    );
    finalStateProbe->close();

    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    const VkQueue nativeComputeQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, computeQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    ASSERT_NE(nativeComputeQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = graphicsPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken graphicsToken = transaction.packetToken(graphicsPacket);
    ASSERT_TRUE(graphicsToken.valid());
    EXPECT_TRUE(graphicsToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = computePacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken computeToken = transaction.packetToken(computePacket);
    ASSERT_TRUE(computeToken.valid());
    EXPECT_TRUE(computeToken.matchesPhysicalQueue(computeQueue.index, computeQueue.deviceGeneration));
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulSubmissionCount(), 2u);
    EXPECT_EQ(submissionObserver.successfulWaitCount(), 1u);
    VulkanTestQueueSubmit2Capture graphicsCapture;
    VulkanTestQueueSubmit2Capture computeCapture;
    ASSERT_TRUE(submissionObserver.capturedSubmission(0u, graphicsCapture));
    ASSERT_TRUE(submissionObserver.capturedSubmission(1u, computeCapture));
    __hidden_descriptor_buffer_round_trip_tests::ExpectNativeTimelineDependency(
        graphicsCapture,
        nativeGraphicsQueue,
        graphicsToken,
        computeCapture,
        nativeComputeQueue,
        computeToken
    );

    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.acceptedPacketCount, 2u);
    EXPECT_EQ(submissionStatistics.acceptedTaskCount, 2u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(submissionStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(submissionStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(submissionStatistics.timelineWaitCount, 1u);
    EXPECT_EQ(submissionStatistics.mergedTimelineWaitCount, 0u);
    const GpuTaskGraphPhysicalQueueSubmissionStatistics graphicsStatistics =
        transaction.physicalQueueSubmissionStatistics(views.compiled, graphicsQueue)
    ;
    const GpuTaskGraphPhysicalQueueSubmissionStatistics computeStatistics =
        transaction.physicalQueueSubmissionStatistics(views.compiled, computeQueue)
    ;
    ASSERT_TRUE(graphicsStatistics.valid());
    ASSERT_TRUE(computeStatistics.valid());
    EXPECT_EQ(graphicsStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(graphicsStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(graphicsStatistics.plannedWaitTokenCount, 0u);
    EXPECT_EQ(graphicsStatistics.timelineWaitCount, 0u);
    EXPECT_EQ(computeStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(computeStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(computeStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(computeStatistics.timelineWaitCount, 1u);
    EXPECT_TRUE(device.waitForIdle());
}


// Two independent packets write disjoint texture mips, then both publish their terminal range to one external
// Graphics consumer. The transaction must remain invalid until both packets accept, compact their same-queue waits
// to the latest timeline token, and hand the native consumer one merged state source containing both mips.
TEST_F(DescriptorBufferRoundTripTest, ExternalFinalHandoffMergesMultipleTerminalPackets){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            // Vulkan images are created in Undefined layout. The first graph tasks discard both mip contents, so
            // retain that physical fact instead of declaring an unproven Common state.
            .setInitialState(ResourceStates::Unknown)
    );
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/external_final_multi_packet_texture"))
            .setMarkerLabel("External Final Multi-Packet Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Unknown)
            .setExternalFinalState(ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(graphicsQueue)
    );
    ASSERT_TRUE(resource.valid());

    const GpuTaskResourceUse mip0Use{
        .resource = resource,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceUse mip1Use{
        .resource = resource,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(1u, 1u, 0u, 1u),
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc mip0Desc;
    mip0Desc
        .setIdentity(Name("tests/descriptor_buffer/external_final_multi_packet_mip0"))
        .setMarkerLabel("External Final Multi-Packet Mip 0")
        .setQueue(graphicsRequest)
        .setResourceUses(&mip0Use, 1u)
    ;
    GpuTaskDesc mip1Desc;
    mip1Desc
        .setIdentity(Name("tests/descriptor_buffer/external_final_multi_packet_mip1"))
        .setMarkerLabel("External Final Multi-Packet Mip 1")
        .setQueue(graphicsRequest)
        .setResourceUses(&mip1Use, 1u)
    ;
    bool mip0Recorded = false;
    bool mip1Recorded = false;
    const GpuTaskId mip0Task = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        mip0Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &mip0Recorded,
        }
    );
    const GpuTaskId mip1Task = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        mip1Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &mip1Recorded,
        }
    );
    ASSERT_TRUE(mip0Task.valid());
    ASSERT_TRUE(mip1Task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/external_final_multi_packet_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId mip0Packet;
    GpuSubmissionPacketId mip1Packet;
    u64 initialRevision = 0u;
    usize initialTaskCount = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        mip0Packet = views.compiled.packetForTask(mip0Task);
        mip1Packet = views.compiled.packetForTask(mip1Task);
        ASSERT_TRUE(mip0Packet.valid());
        ASSERT_TRUE(mip1Packet.valid());
        ASSERT_NE(mip0Packet, mip1Packet);
        const GpuCompiledExternalResourceExportView exportInfo = views.compiled.externalResourceExport(resource);
        ASSERT_TRUE(exportInfo.valid());
        ASSERT_EQ(exportInfo.plan->sourceCount, 2u);
        EXPECT_FALSE(exportInfo.plan->producerTask.valid());
        EXPECT_FALSE(exportInfo.plan->sourceQueue.valid());
        initialRevision = views.declarations.declarationRevision();
        initialTaskCount = views.declarations.taskCount();
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuTaskGraphExternalResourceHandoffSnapshot reentrantHandoffSnapshot(
        DescriptorBufferRoundTripTest::arena()
    );
    DeclarationMutationExternalHandoffState reentrantHandoffState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .recordedGraph = &recordedGraph,
        .transaction = &transaction,
        .handoffSnapshot = &reentrantHandoffSnapshot,
        .resource = resource,
        .attempted = false,
        .succeeded = true,
    };
    const GpuTaskId handoffProbe = graph.addTask<DeclarationMutationExternalHandoffTask>(
        GpuTaskDesc{},
        DeclarationMutationExternalHandoffTask::Payload(reentrantHandoffState)
    );
    EXPECT_FALSE(handoffProbe.valid());
    EXPECT_TRUE(reentrantHandoffState.attempted);
    EXPECT_FALSE(reentrantHandoffState.succeeded);
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        EXPECT_FALSE(reentrantHandoffSnapshot.validFor(compiledGraph, views.compiled));
        EXPECT_TRUE(transaction.validFor(views.compiled));
        EXPECT_EQ(views.declarations.declarationRevision(), initialRevision);
        EXPECT_EQ(views.declarations.taskCount(), initialTaskCount);
    }

    GpuTaskGraphExternalResourceHandoffSnapshot handoffSnapshot(DescriptorBufferRoundTripTest::arena());
    const GpuTaskGraphExternalResourceHandoff* reusedHandoff = nullptr;
    usize staleProducerCount = 0u;
    usize staleWaitTokenCount = 0u;
    usize staleTerminalRangeCount = 0u;
    const CommandListResourceStateHandoff* staleStateSource = nullptr;
    QueueSubmissionToken staleWaitToken;
    GpuTaskGraphSubmissionStatistics beforeStaleCalls;
    u64 staleRecordingAttemptGeneration = 0u;
    {
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = mip0Packet, .packetCount = 1u },
        recordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = mip1Packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(mip0Recorded);
    EXPECT_TRUE(mip1Recorded);
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
        GpuSubmissionPacketRange{ .first = mip0Packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
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
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = mip1Packet, .packetCount = 1u },
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
    EXPECT_EQ(handoff->destinationQueue, graphicsQueue);
    EXPECT_EQ(handoff->finalState, ResourceStates::ShaderResource);
    EXPECT_EQ(handoff->terminalRangeCount, 2u);
    ASSERT_EQ(handoff->producerCount, 2u);
    EXPECT_FALSE(handoff->producerTask.valid());
    EXPECT_FALSE(handoff->sourceQueue.valid());
    EXPECT_FALSE(handoff->token.valid());
    ASSERT_EQ(handoff->waitTokenCount, 1u);
    EXPECT_TRUE(handoff->waitTokens[0u].matchesPhysicalQueue(
        graphicsQueue.index,
        graphicsQueue.deviceGeneration
    ));
    EXPECT_GE(handoff->waitTokens[0u].value, handoff->producers[0u].token.value);
    EXPECT_GE(handoff->waitTokens[0u].value, handoff->producers[1u].token.value);
    CommandListResourceStateHandoff mip0FinalState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff mip1FinalState(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, mip0Task, mip0FinalState));
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, mip1Task, mip1FinalState));
    EXPECT_NE(handoff->stateSource, &mip0FinalState);
    EXPECT_NE(handoff->stateSource, &mip1FinalState);

    const ArenaMemoryStats warmedHandoffArenaStats = DescriptorBufferRoundTripTest::arena().memoryStats();
    for(u32 repeat = 0u; repeat < 32u; ++repeat){
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
    }
    const ArenaMemoryStats reusedHandoffArenaStats = DescriptorBufferRoundTripTest::arena().memoryStats();
    EXPECT_EQ(reusedHandoffArenaStats.reservedBytes, warmedHandoffArenaStats.reservedBytes);
    EXPECT_EQ(reusedHandoffArenaStats.usedBytes, warmedHandoffArenaStats.usedBytes);
    EXPECT_EQ(reusedHandoffArenaStats.peakUsedBytes, warmedHandoffArenaStats.peakUsedBytes);
    EXPECT_EQ(reusedHandoffArenaStats.allocationCount, warmedHandoffArenaStats.allocationCount);
    EXPECT_EQ(reusedHandoffArenaStats.reallocationCount, warmedHandoffArenaStats.reallocationCount);
    EXPECT_EQ(reusedHandoffArenaStats.deallocationCount, warmedHandoffArenaStats.deallocationCount);
    reusedHandoff = handoffSnapshot.value();
    ASSERT_NE(reusedHandoff, nullptr);

    const CommandListHandle consumer = device.createCommandList();
    ASSERT_NE(consumer.get(), nullptr);
    consumer->open(reusedHandoff->stateSource);
    EXPECT_EQ(consumer->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(consumer->getTextureSubresourceState(texture.get(), 0u, 1u), ResourceStates::ShaderResource);
    consumer->close();
    CommandList* const consumerLists[] = { consumer.get() };
    QueueSubmissionDesc consumerSubmission;
    consumerSubmission.setWaitTokens(reusedHandoff->waitTokens, reusedHandoff->waitTokenCount);
    const QueueSubmissionToken consumerToken = device.executeCommandLists(
        consumerLists,
        LengthOf(consumerLists),
        graphicsQueue,
        consumerSubmission
    );
    ASSERT_TRUE(consumerToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_TRUE(device.waitForIdle());

    staleRecordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(staleRecordingAttemptGeneration, 0u);
    ASSERT_TRUE(recordedGraph.validFor(compiledGraph, views.compiled));
    ASSERT_TRUE(handoffSnapshot.validFor(compiledGraph, views.compiled));
    staleProducerCount = reusedHandoff->producerCount;
    staleWaitTokenCount = reusedHandoff->waitTokenCount;
    staleTerminalRangeCount = reusedHandoff->terminalRangeCount;
    staleStateSource = reusedHandoff->stateSource;
    staleWaitToken = reusedHandoff->waitTokens[0u];
    beforeStaleCalls = transaction.submissionStatistics();
    }

    ASSERT_TRUE(graph.tryReset());
    u64 resetRevision = 0u;
    usize resetTaskCount = 0u;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        resetRevision = declarations.declarationRevision();
        resetTaskCount = declarations.taskCount();
    }

    GpuTaskGraphExternalResourceHandoffSnapshot staleHandoffSnapshot(DescriptorBufferRoundTripTest::arena());
    DeclarationMutationExternalHandoffState staleHandoffState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .recordedGraph = &recordedGraph,
        .transaction = &transaction,
        .handoffSnapshot = &staleHandoffSnapshot,
        .resource = resource,
        .attempted = false,
        .succeeded = true,
    };
    const GpuTaskId staleHandoffProbe = graph.addTask<DeclarationMutationExternalHandoffTask>(
        GpuTaskDesc{},
        DeclarationMutationExternalHandoffTask::Payload(staleHandoffState)
    );
    EXPECT_FALSE(staleHandoffProbe.valid());
    EXPECT_TRUE(staleHandoffState.attempted);
    EXPECT_FALSE(staleHandoffState.succeeded);
    {
        const GpuTaskGraphReadViews staleViews(graph, compiledGraph);
        EXPECT_FALSE(staleViews.valid());
        EXPECT_FALSE(staleHandoffSnapshot.validFor(compiledGraph, staleViews.compiled));
        EXPECT_EQ(staleViews.declarations.declarationRevision(), resetRevision);
        EXPECT_EQ(staleViews.declarations.taskCount(), resetTaskCount);
        ASSERT_TRUE(handoffSnapshot.validFor(compiledGraph, staleViews.compiled));
        EXPECT_EQ(reusedHandoff->producerCount, staleProducerCount);
        EXPECT_EQ(reusedHandoff->waitTokenCount, staleWaitTokenCount);
        EXPECT_EQ(reusedHandoff->terminalRangeCount, staleTerminalRangeCount);
        EXPECT_EQ(reusedHandoff->stateSource, staleStateSource);
        EXPECT_EQ(reusedHandoff->waitTokens[0u].queue, staleWaitToken.queue);
        EXPECT_EQ(reusedHandoff->waitTokens[0u].value, staleWaitToken.value);
        EXPECT_EQ(reusedHandoff->waitTokens[0u].physicalQueueIndex, staleWaitToken.physicalQueueIndex);
        EXPECT_EQ(reusedHandoff->waitTokens[0u].deviceGeneration, staleWaitToken.deviceGeneration);
    }

    DeclarationMutationTransactionRejectionState staleRejectionState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .transaction = &transaction,
        .task = mip0Task,
        .recordingAttemptGeneration = staleRecordingAttemptGeneration,
        .rejectAttempted = false,
        .discardAttempted = false,
        .discardSucceeded = true,
    };
    const GpuTaskId staleRejectionProbe = graph.addTask<DeclarationMutationTransactionRejectionTask>(
        GpuTaskDesc{},
        DeclarationMutationTransactionRejectionTask::Payload(staleRejectionState)
    );
    EXPECT_FALSE(staleRejectionProbe.valid());
    EXPECT_TRUE(staleRejectionState.rejectAttempted);
    EXPECT_TRUE(staleRejectionState.discardAttempted);
    EXPECT_FALSE(staleRejectionState.discardSucceeded);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.declarationRevision(), resetRevision);
        EXPECT_EQ(declarations.taskCount(), resetTaskCount);
    }

    const GpuTaskGraphSubmissionStatistics afterStaleCalls = transaction.submissionStatistics();
    EXPECT_EQ(afterStaleCalls.graphGeneration, beforeStaleCalls.graphGeneration);
    EXPECT_EQ(afterStaleCalls.planGeneration, beforeStaleCalls.planGeneration);
    EXPECT_EQ(afterStaleCalls.recordingAttemptGeneration, beforeStaleCalls.recordingAttemptGeneration);
    EXPECT_EQ(afterStaleCalls.deviceGeneration, beforeStaleCalls.deviceGeneration);
    EXPECT_EQ(afterStaleCalls.acceptedPacketCount, beforeStaleCalls.acceptedPacketCount);
    EXPECT_EQ(afterStaleCalls.acceptedTaskCount, beforeStaleCalls.acceptedTaskCount);
    EXPECT_EQ(afterStaleCalls.rejectedPacketCount, beforeStaleCalls.rejectedPacketCount);
    EXPECT_EQ(afterStaleCalls.rejectedTaskCount, beforeStaleCalls.rejectedTaskCount);
    EXPECT_EQ(afterStaleCalls.nativeSubmissionCount, beforeStaleCalls.nativeSubmissionCount);
    EXPECT_EQ(afterStaleCalls.rejectedSubmissionCount, beforeStaleCalls.rejectedSubmissionCount);
    EXPECT_EQ(afterStaleCalls.nativeCommandListCount, beforeStaleCalls.nativeCommandListCount);
    EXPECT_EQ(afterStaleCalls.plannedWaitTokenCount, beforeStaleCalls.plannedWaitTokenCount);
    EXPECT_EQ(afterStaleCalls.sameQueueWaitElisionCount, beforeStaleCalls.sameQueueWaitElisionCount);
    EXPECT_EQ(afterStaleCalls.timelineWaitCount, beforeStaleCalls.timelineWaitCount);
    EXPECT_EQ(afterStaleCalls.mergedTimelineWaitCount, beforeStaleCalls.mergedTimelineWaitCount);
    EXPECT_EQ(afterStaleCalls.acceptedFrontierSubmissionCount, beforeStaleCalls.acceptedFrontierSubmissionCount);
    EXPECT_EQ(afterStaleCalls.recoverySubmissionCount, beforeStaleCalls.recoverySubmissionCount);
    EXPECT_EQ(afterStaleCalls.submissionSeconds, beforeStaleCalls.submissionSeconds);
    for(usize queueIndex = 0u; queueIndex < GpuTaskGraphSubmissionStatistics::s_QueueClassCount; ++queueIndex){
        EXPECT_EQ(
            afterStaleCalls.nativeSubmissionCountByQueueClass[queueIndex],
            beforeStaleCalls.nativeSubmissionCountByQueueClass[queueIndex]
        );
        EXPECT_EQ(
            afterStaleCalls.nativeCommandListCountByQueueClass[queueIndex],
            beforeStaleCalls.nativeCommandListCountByQueueClass[queueIndex]
        );
        EXPECT_EQ(
            afterStaleCalls.timelineWaitCountByQueueClass[queueIndex],
            beforeStaleCalls.timelineWaitCountByQueueClass[queueIndex]
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

