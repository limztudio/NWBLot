// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CSG receiver-surface compute generation consumes graph-owned clip data, then G-buffer owns the receiver-event
// raster output and the generated vertex-buffer handoff. This records all three callbacks in one real primary
// Graphics packet without either callback performing a native state transition.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedOpaqueCsgReceiverComputeHandoffMergesWithGbuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto receiverEvent = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/csg_receiver_event"))
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    auto csgClipContext = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/csg_clip_context"))
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    auto generatedVertex = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/csg_receiver_generated_vertex"))
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(receiverEvent.get(), nullptr);
    ASSERT_NE(csgClipContext.get(), nullptr);
    ASSERT_NE(generatedVertex.get(), nullptr);

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
    const GpuGraphResourceId receiverEventResource = importBuffer(receiverEvent, "CSG Receiver Event");
    const GpuGraphResourceId csgClipContextResource = importBuffer(csgClipContext, "CSG Clip Context");
    const GpuGraphResourceId generatedVertexResource = importBuffer(generatedVertex, "CSG Receiver Generated Vertex");
    ASSERT_TRUE(receiverEventResource.valid());
    ASSERT_TRUE(csgClipContextResource.valid());
    ASSERT_TRUE(generatedVertexResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.overlapPreferred = false;
    clearScheduling.avoidQueueCrossing = true;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = false;
    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = receiverEventResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool clearObservedCopyDest = false;
    QueueSubmissionToken clearAcceptedToken;
    const GpuTaskId clearTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_receiver_clear"))
            .setMarkerLabel("CSG Receiver Clear")
            .setQueue(graphicsComputeQueue)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses)),
        NativePacketPrefixTask::Payload{
            .buffer = receiverEvent.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &clearObservedCopyDest,
            .acceptedToken = &clearAcceptedToken,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    GpuTaskSchedulingHint producerScheduling = clearScheduling;
    producerScheduling.cost = GpuTaskCostHint::Medium;
    producerScheduling.mergeWithPrevious = true;
    producerScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = csgClipContextResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool producerObservedUnorderedAccess = false;
    QueueSubmissionToken producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_receiver_compute_emulation"))
            .setMarkerLabel("CSG Receiver Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setDependencies(&clearTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &producerObservedUnorderedAccess,
            .acceptedToken = &producerAcceptedToken,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint gbufferScheduling = producerScheduling;
    const GpuTaskResourceUse gbufferUses[] = {
        GpuTaskResourceUse{
            .resource = receiverEventResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = generatedVertexResource,
            .range = {},
            .requiredState = ResourceStates::VertexBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool gbufferObservedVertexBuffer = false;
    QueueSubmissionToken gbufferAcceptedToken;
    const GpuTaskId gbufferTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_receiver_gbuffer"))
            .setMarkerLabel("CSG Receiver G-buffer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(gbufferScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(gbufferUses, LengthOf(gbufferUses)),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertex.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .recorded = &gbufferObservedVertexBuffer,
            .acceptedToken = &gbufferAcceptedToken,
        }
    );
    ASSERT_TRUE(gbufferTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/csg_receiver_compute_emulation_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(clearTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, gbufferTask));
    EXPECT_TRUE(analysis.hasInferredEdge(clearTask, gbufferTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, gbufferTask));

    const GpuTaskQueueAssignment* const clearAssignment = assignments.find(clearTask);
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    const GpuTaskQueueAssignment* const gbufferAssignment = assignments.find(gbufferTask);
    ASSERT_NE(clearAssignment, nullptr);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(gbufferAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(producerAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(gbufferAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(clearAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(gbufferAssignment->queueClass, CommandQueue::Graphics);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(clearTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(packet, views.compiled.packetForTask(producerTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(gbufferTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(clearTask, gbufferTask));
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 3u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], clearTask);
    EXPECT_EQ(packetTasks[1u], producerTask);
    EXPECT_EQ(packetTasks[2u], gbufferTask);

    const auto hasTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        if(!compiledTask.valid())
            return false;
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(usize barrierIndex = 0u; barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(clearTask, receiverEventResource, ResourceStates::Common, ResourceStates::CopyDest));
    EXPECT_TRUE(hasTransition(producerTask, csgClipContextResource, ResourceStates::Common, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasTransition(producerTask, generatedVertexResource, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(gbufferTask, receiverEventResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(gbufferTask, generatedVertexResource, ResourceStates::UnorderedAccess, ResourceStates::VertexBuffer));

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
    EXPECT_TRUE(clearObservedCopyDest);
    EXPECT_TRUE(producerObservedUnorderedAccess);
    EXPECT_TRUE(gbufferObservedVertexBuffer);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, clearTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(receiverEvent.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertex.get()), ResourceStates::VertexBuffer);
    stateProbe->close();

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
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(clearAcceptedToken.valid());
    ASSERT_TRUE(producerAcceptedToken.valid());
    ASSERT_TRUE(gbufferAcceptedToken.valid());
    EXPECT_EQ(clearAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(clearAcceptedToken.value, packetToken.value);
    EXPECT_EQ(producerAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(producerAcceptedToken.value, packetToken.value);
    EXPECT_EQ(gbufferAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(gbufferAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());
}


// The opaque CSG interval-sample path has its own placement: Combine publishes the removed-interval aliases,
// compute emulation consumes them and writes a pairwise-distinct generated-vertex set, then Sample rasterizes that
// exact set. Record the full handoff on a real Graphics packet without callback-local state transitions.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAliasFreeOpaqueCsgIntervalSampleComputeHandoffMergesWithSample){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto csgClipContext = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/csg_interval_sample_clip_context"))
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    const auto createGeneratedVertex = [&device](const Name& debugName){
        return device.createBuffer(
            BufferDesc()
                .setDebugName(debugName)
                .setByteSize(3u * 4u * sizeof(f32))
                .setStructStride(4u * sizeof(f32))
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setIsVertexBuffer(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
        );
    };
    auto generatedVertexA = createGeneratedVertex(
        Name("tests/descriptor_buffer/csg_interval_sample_generated_vertex_a")
    );
    auto generatedVertexB = createGeneratedVertex(
        Name("tests/descriptor_buffer/csg_interval_sample_generated_vertex_b")
    );
    auto removedInterval = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(1u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(csgClipContext.get(), nullptr);
    ASSERT_NE(generatedVertexA.get(), nullptr);
    ASSERT_NE(generatedVertexB.get(), nullptr);
    ASSERT_NE(removedInterval.get(), nullptr);
    EXPECT_NE(generatedVertexA.get(), generatedVertexB.get());

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
    const GpuGraphResourceId csgClipContextResource = importBuffer(csgClipContext, "CSG Interval-Sample Clip Context");
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "CSG Interval-Sample Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "CSG Interval-Sample Generated Vertex B"
    );
    const TextureDesc& removedIntervalDescription = removedInterval->getDescription();
    const GpuGraphResourceId removedIntervalResource = graph.importTexture(
        removedInterval,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_interval_sample_removed_interval"))
            .setMarkerLabel("Opaque CSG Removed Interval")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(removedIntervalDescription.initialState)
            .setQueueSharing(removedIntervalDescription.queueSharing)
    );
    ASSERT_TRUE(csgClipContextResource.valid());
    ASSERT_TRUE(generatedVertexAResource.valid());
    ASSERT_TRUE(generatedVertexBResource.valid());
    ASSERT_TRUE(removedIntervalResource.valid());
    EXPECT_NE(generatedVertexAResource, generatedVertexBResource);

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_interval_sample_generated_vertex_outputs"))
            .setMarkerLabel("Opaque CSG Interval-Sample Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse combineUses[] = {
        GpuTaskResourceUse{
            .resource = csgClipContextResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse computeEmulationUses[] = {
        GpuTaskResourceUse{
            .resource = removedIntervalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse sampleUses[] = {
        GpuTaskResourceUse{
            .resource = removedIntervalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint combineScheduling;
    combineScheduling.cost = GpuTaskCostHint::Medium;
    combineScheduling.overlapPreferred = false;
    combineScheduling.avoidQueueCrossing = true;
    combineScheduling.forceSubmissionBoundary = false;
    combineScheduling.allowPacketMerge = true;
    combineScheduling.mergeWithPrevious = false;
    bool combineObservedStates = false;
    QueueSubmissionToken combineAcceptedToken;
    const GpuTaskId combineTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_interval_sample_combine"))
            .setMarkerLabel("Opaque CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(combineScheduling)
            .setResourceUses(combineUses, LengthOf(combineUses)),
        NativePacketPrefixTask::Payload{
            .buffer = csgClipContext.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = removedInterval.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &combineObservedStates,
            .acceptedToken = &combineAcceptedToken,
        }
    );
    ASSERT_TRUE(combineTask.valid());

    GpuTaskSchedulingHint handoffScheduling = combineScheduling;
    handoffScheduling.mergeWithPrevious = true;
    handoffScheduling.allowMergeAcrossConsumerFrontier = true;
    bool computeEmulationObservedStates = false;
    QueueSubmissionToken computeEmulationAcceptedToken;
    const GpuTaskId computeEmulationTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_interval_sample_compute_emulation"))
            .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(handoffScheduling)
            .setDependencies(&combineTask, 1u)
            .setResourceUses(computeEmulationUses, LengthOf(computeEmulationUses))
            .setResourceSetUses(&outputUavSetUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertexA.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .additionalBuffer = generatedVertexB.get(),
            .expectedAdditionalBufferState = ResourceStates::UnorderedAccess,
            .texture = removedInterval.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &computeEmulationObservedStates,
            .acceptedToken = &computeEmulationAcceptedToken,
        }
    );
    ASSERT_TRUE(computeEmulationTask.valid());

    bool sampleObservedStates = false;
    QueueSubmissionToken sampleAcceptedToken;
    const GpuTaskId sampleTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/csg_interval_sample_raster"))
            .setMarkerLabel("Opaque CSG Interval Sample")
            .setQueue(graphicsComputeQueue)
            .setScheduling(handoffScheduling)
            .setDependencies(&computeEmulationTask, 1u)
            .setResourceUses(sampleUses, LengthOf(sampleUses))
            .setResourceSetUses(&outputVertexBufferSetUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertexA.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .additionalBuffer = generatedVertexB.get(),
            .expectedAdditionalBufferState = ResourceStates::VertexBuffer,
            .texture = removedInterval.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &sampleObservedStates,
            .acceptedToken = &sampleAcceptedToken,
        }
    );
    ASSERT_TRUE(sampleTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/csg_interval_sample_compute_emulation_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(combineTask, computeEmulationTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(computeEmulationTask, sampleTask));
    EXPECT_TRUE(analysis.hasInferredEdge(combineTask, computeEmulationTask));
    EXPECT_TRUE(analysis.hasInferredEdge(combineTask, sampleTask));
    EXPECT_TRUE(analysis.hasInferredEdge(computeEmulationTask, sampleTask));

    for(const GpuTaskId task : { combineTask, computeEmulationTask, sampleTask }){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(combineTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(packet, views.compiled.packetForTask(computeEmulationTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(sampleTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(combineTask, computeEmulationTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(computeEmulationTask, sampleTask));
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(packet).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 3u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], combineTask);
    EXPECT_EQ(packetTasks[1u], computeEmulationTask);
    EXPECT_EQ(packetTasks[2u], sampleTask);

    const auto hasTextureBarrier = [&](const GpuTaskId task, const GpuCompiledBarrierType::Enum type, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        if(!compiledTask.valid())
            return false;
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == removedIntervalResource
                && barrier.range.textureSubresources == removedIntervalRange
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    const auto hasBufferTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        if(!compiledTask.valid())
            return false;
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
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
    EXPECT_TRUE(hasTextureBarrier(
        combineTask,
        GpuCompiledBarrierType::TextureTransition,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTextureBarrier(
        computeEmulationTask,
        GpuCompiledBarrierType::TextureUav,
        ResourceStates::UnorderedAccess,
        ResourceStates::UnorderedAccess
    ));
    for(const GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            computeEmulationTask,
            output,
            ResourceStates::Common,
            ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            sampleTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer
        ));
    }

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
    EXPECT_TRUE(combineObservedStates);
    EXPECT_TRUE(computeEmulationObservedStates);
    EXPECT_TRUE(sampleObservedStates);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, combineTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(
        stateProbe->getTextureSubresourceState(removedInterval.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    stateProbe->close();

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
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    for(const QueueSubmissionToken& acceptedToken : {
        combineAcceptedToken,
        computeEmulationAcceptedToken,
        sampleAcceptedToken,
    }){
        ASSERT_TRUE(acceptedToken.valid());
        EXPECT_EQ(acceptedToken.queue, packetToken.queue);
        EXPECT_EQ(acceptedToken.value, packetToken.value);
        EXPECT_EQ(acceptedToken.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(acceptedToken.deviceGeneration, packetToken.deviceGeneration);
    }
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

