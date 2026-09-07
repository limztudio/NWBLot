// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT Occupancy may prepare multiple transparent compute-emulated materials ahead of raster only when their
// generated-vertex buffers are distinct. Record that alias-free Graphics|Compute handoff on a real packet: both
// producer writes must observe UAV, the Occupancy raster must observe VertexBuffer, and no callback owns a bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAliasFreeAvboitOccupancyGeneratedVertexHandoffMergesWithRaster){
    auto& device = DescriptorBufferRoundTripTest::device();
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
        Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_a")
    );
    auto generatedVertexB = createGeneratedVertex(
        Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_b")
    );
    ASSERT_NE(generatedVertexA.get(), nullptr);
    ASSERT_NE(generatedVertexB.get(), nullptr);
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
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "AVBOIT Occupancy Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "AVBOIT Occupancy Generated Vertex B"
    );
    ASSERT_TRUE(generatedVertexAResource.valid());
    ASSERT_TRUE(generatedVertexBResource.valid());
    EXPECT_NE(generatedVertexAResource, generatedVertexBResource);

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = GpuTaskCostHint::Medium;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const GpuTaskResourceSetUse outputUavSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    bool producerObservedStates = false;
    QueueSubmissionToken producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_producer"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Producer")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceSetUses(&outputUavSetUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertexA.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .additionalBuffer = generatedVertexB.get(),
            .expectedAdditionalBufferState = ResourceStates::UnorderedAccess,
            .recorded = &producerObservedStates,
            .acceptedToken = &producerAcceptedToken,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint rasterScheduling = producerScheduling;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceSetUse outputVertexBufferSetUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };
    bool rasterObservedStates = false;
    QueueSubmissionToken rasterAcceptedToken;
    const GpuTaskId rasterTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_raster"))
            .setMarkerLabel("AVBOIT Occupancy Generated Vertex Raster")
            .setQueue(graphicsComputeQueue)
            .setScheduling(rasterScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceSetUses(&outputVertexBufferSetUse, 1u),
        NativePacketPrefixTask::Payload{
            .buffer = generatedVertexA.get(),
            .expectedState = ResourceStates::VertexBuffer,
            .additionalBuffer = generatedVertexB.get(),
            .expectedAdditionalBufferState = ResourceStates::VertexBuffer,
            .recorded = &rasterObservedStates,
            .acceptedToken = &rasterAcceptedToken,
        }
    );
    ASSERT_TRUE(rasterTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_occupancy_generated_vertex_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, rasterTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, rasterTask));
    ASSERT_EQ(analysis.topologicalOrder().size(), 2u);
    EXPECT_EQ(analysis.topologicalOrder()[0u], producerTask);
    EXPECT_EQ(analysis.topologicalOrder()[1u], rasterTask);

    for(const GpuTaskId task : { producerTask, rasterTask }){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    }
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(producerTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(packet, views.compiled.packetForTask(rasterTask));
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, rasterTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(producerTask, rasterTask));
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(packet).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 2u);
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    EXPECT_EQ(packetTasks[0u], producerTask);
    EXPECT_EQ(packetTasks[1u], rasterTask);

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
    for(const GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producerTask,
            output,
            ResourceStates::Common,
            ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            rasterTask,
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
    EXPECT_TRUE(producerObservedStates);
    EXPECT_TRUE(rasterObservedStates);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, producerTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(stateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
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
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    expectPacketToken(transaction.taskToken(views.compiled, producerTask));
    expectPacketToken(transaction.taskToken(views.compiled, rasterTask));
    expectPacketToken(producerAcceptedToken);
    expectPacketToken(rasterAcceptedToken);
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

