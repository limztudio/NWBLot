// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ordered primary command buffers must carry the producer's final state into the consumer. The consumer's
// ShaderResource transition therefore has CopyDest as its source, rather than treating the buffer as unknown.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffTransfersFinalBufferState){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto restoredBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(restoredBuffer.get(), nullptr);
    auto permanentBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(permanentBuffer.get(), nullptr);

    CommandListResourceStateHandoff handoff(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    producer->open();
    producer->setBufferState(buffer.get(), ResourceStates::CopyDest);
    producer->setBufferState(restoredBuffer.get(), ResourceStates::CopyDest);
    producer->setPermanentBufferState(permanentBuffer.get(), ResourceStates::ShaderResource);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());

    consumer->open(&handoff);
    EXPECT_EQ(consumer->getBufferState(buffer.get()), ResourceStates::CopyDest);
    EXPECT_EQ(consumer->getBufferState(restoredBuffer.get()), ResourceStates::Common);
    EXPECT_EQ(consumer->getBufferState(permanentBuffer.get()), ResourceStates::ShaderResource);
    consumer->setBufferState(buffer.get(), ResourceStates::ShaderResource);
    EXPECT_EQ(consumer->getBufferState(buffer.get()), ResourceStates::ShaderResource);
    consumer->close();

    CommandList* commandLists[] = { producer.get(), consumer.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 2u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Subset publication happens only after the complete candidate exists, so the source may be the destination itself
// without erasing state before it is selected.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffSubsetBuildersSupportSelfAlias){
    Alloc::ScratchArena subsetScratchArena(Name("tests/descriptor_buffer/subset_self_alias"));
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle selectedBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle omittedBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);
    ASSERT_NE(selectedBuffer.get(), nullptr);
    ASSERT_NE(omittedBuffer.get(), nullptr);

    CommandListResourceStateHandoff handoff(DescriptorBufferRoundTripTest::arena());
    const CommandListHandle producer = device.createCommandList();
    const CommandListHandle resourceSubsetProbe = device.createCommandList();
    const CommandListHandle textureRangeProbe = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(resourceSubsetProbe.get(), nullptr);
    ASSERT_NE(textureRangeProbe.get(), nullptr);

    const TextureSubresourceSet firstMip(0u, 1u, 0u, 1u);
    const TextureSubresourceSet secondMip(1u, 1u, 0u, 1u);
    producer->open();
    producer->setTextureState(texture.get(), firstMip, ResourceStates::UnorderedAccess);
    producer->setTextureState(texture.get(), secondMip, ResourceStates::ShaderResource);
    producer->setBufferState(selectedBuffer.get(), ResourceStates::UnorderedAccess);
    producer->setBufferState(omittedBuffer.get(), ResourceStates::UnorderedAccess);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());

    Texture* const selectedTextures[] = { texture.get() };
    Buffer* const selectedBuffers[] = { selectedBuffer.get() };
    ASSERT_TRUE(handoff.buildResourceSubset(
        handoff,
        selectedTextures,
        LengthOf(selectedTextures),
        selectedBuffers,
        LengthOf(selectedBuffers),
        subsetScratchArena
    ));
    resourceSubsetProbe->open(&handoff);
    EXPECT_EQ(
        resourceSubsetProbe->getTextureSubresourceState(texture.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    EXPECT_EQ(
        resourceSubsetProbe->getTextureSubresourceState(texture.get(), 0u, 1u),
        ResourceStates::ShaderResource
    );
    EXPECT_EQ(resourceSubsetProbe->getBufferState(selectedBuffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(resourceSubsetProbe->getBufferState(omittedBuffer.get()), ResourceStates::Unknown);
    resourceSubsetProbe->close();

    ASSERT_TRUE(handoff.buildTextureRangeSubset(handoff, texture.get(), secondMip));
    textureRangeProbe->open(&handoff);
    EXPECT_EQ(textureRangeProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::Unknown);
    EXPECT_EQ(
        textureRangeProbe->getTextureSubresourceState(texture.get(), 0u, 1u),
        ResourceStates::ShaderResource
    );
    EXPECT_EQ(textureRangeProbe->getBufferState(selectedBuffer.get()), ResourceStates::Unknown);
    textureRangeProbe->close();

    CommandList* const commandLists[] = { producer.get(), resourceSubsetProbe.get(), textureRangeProbe.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Validation failures return before candidate construction and publication. The same boundary also leaves the
// destination unchanged if candidate allocation throws and unwinds to the application exception boundary.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffSubsetValidationPreservesDestination){
    Alloc::ScratchArena subsetScratchArena(Name("tests/descriptor_buffer/subset_validation"));
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);
    ASSERT_NE(buffer.get(), nullptr);

    CommandListResourceStateHandoff destination(DescriptorBufferRoundTripTest::arena());
    const CommandListHandle producer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    producer->open();
    producer->setTextureState(texture.get(), TextureSubresourceSet(0u, 1u, 0u, 1u), ResourceStates::UnorderedAccess);
    producer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    producer->close(&destination);
    ASSERT_TRUE(destination.valid());

    CommandListResourceStateHandoff preserved(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(preserved.copyFrom(destination));
    ASSERT_TRUE(destination.equivalentTo(preserved));
    CommandListResourceStateHandoff invalidSource(DescriptorBufferRoundTripTest::arena());
    ASSERT_FALSE(invalidSource.valid());

    EXPECT_FALSE(destination.buildResourceSubset(invalidSource, nullptr, 0u, nullptr, 0u, subsetScratchArena));
    EXPECT_TRUE(destination.equivalentTo(preserved));
    EXPECT_FALSE(destination.buildResourceSubset(preserved, nullptr, 1u, nullptr, 0u, subsetScratchArena));
    EXPECT_TRUE(destination.equivalentTo(preserved));
    EXPECT_FALSE(destination.buildResourceSubset(preserved, nullptr, 0u, nullptr, 1u, subsetScratchArena));
    EXPECT_TRUE(destination.equivalentTo(preserved));
    EXPECT_FALSE(destination.buildTextureRangeSubset(preserved, nullptr, TextureSubresourceSet{}));
    EXPECT_TRUE(destination.equivalentTo(preserved));
    EXPECT_FALSE(destination.buildTextureRangeSubset(
        preserved,
        texture.get(),
        TextureSubresourceSet(2u, 1u, 0u, 1u)
    ));
    EXPECT_TRUE(destination.equivalentTo(preserved));

    CommandList* const commandLists[] = { producer.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Compiled barrier metadata is treated as an immutable packet contract. Corrupted force markers must fail
// preflight before task recording, including canonical UAV classification and graph-boundary exclusions.
TEST_F(DescriptorBufferRoundTripTest, PacketPreflightRejectsCorruptedForcedMemoryDependencyMetadata){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::CopyDest)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/corrupted_forced_dependency"))
            .setMarkerLabel("Corrupted Forced Dependency")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::CopyDest)
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
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.allowPacketMerge = true;
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/corrupted_forced_dependency_producer"))
        .setMarkerLabel("Corrupted Forced Dependency Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(&use, 1u)
    ;
    const bool shouldRecord = true;
    bool producerAttempted = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketCaptureRetryTask>(
        producerDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &producerAttempted,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.allowPacketMerge = true;
    consumerScheduling.mergeWithPrevious = true;
    GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/descriptor_buffer/corrupted_forced_dependency_consumer"))
        .setMarkerLabel("Corrupted Forced Dependency Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(&producerTask, 1u)
        .setResourceUses(&use, 1u)
    ;
    bool consumerAttempted = false;
    const GpuTaskId consumerTask = graph.addTask<NativePacketCaptureRetryTask>(
        consumerDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &consumerAttempted,
        }
    );
    ASSERT_TRUE(consumerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/corrupted_forced_dependency_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    {
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId consumerPacket = views.compiled.packetForTask(consumerTask);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledConsumer = views.compiled.findTask(consumerTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(consumerPacket, packet);
    ASSERT_TRUE(views.compiled.taskPrecedesInSamePacket(producerTask, consumerTask));
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledConsumer.valid());
    ASSERT_EQ(compiledProducer.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledConsumer.plan->prologueBarrierCount, 1u);
    GpuCompiledBarrier* const initialBarrier = const_cast<GpuCompiledBarrier*>(compiledProducer.prologueBarriers);
    GpuCompiledBarrier* const dependencyBarrier = const_cast<GpuCompiledBarrier*>(compiledConsumer.prologueBarriers);
    ASSERT_NE(initialBarrier, nullptr);
    ASSERT_NE(dependencyBarrier, nullptr);
    ASSERT_EQ(initialBarrier->type, GpuCompiledBarrierType::BufferTransition);
    ASSERT_EQ(initialBarrier->before, ResourceStates::CopyDest);
    ASSERT_EQ(initialBarrier->after, ResourceStates::CopyDest);
    ASSERT_TRUE(initialBarrier->isGraphInitialState);
    ASSERT_FALSE(initialBarrier->isInitialOwnerHandoff);
    ASSERT_FALSE(initialBarrier->forceMemoryDependency);
    ASSERT_EQ(dependencyBarrier->type, GpuCompiledBarrierType::BufferTransition);
    ASSERT_EQ(dependencyBarrier->before, ResourceStates::CopyDest);
    ASSERT_EQ(dependencyBarrier->after, ResourceStates::CopyDest);
    ASSERT_FALSE(dependencyBarrier->isGraphInitialState);
    ASSERT_FALSE(dependencyBarrier->isInitialOwnerHandoff);
    ASSERT_TRUE(dependencyBarrier->forceMemoryDependency);
    const GpuCompiledBarrier validInitialBarrier = *initialBarrier;
    const GpuCompiledBarrier validDependencyBarrier = *dependencyBarrier;

    const auto expectPreflightRejection = [&](
        GpuCompiledBarrier& targetBarrier,
        const GpuCompiledBarrier& corruptedBarrier,
        const GpuCompiledBarrier& validTargetBarrier
    ){
        targetBarrier = corruptedBarrier;
        producerAttempted = false;
        consumerAttempted = false;
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
            recordedGraph
        ));
        EXPECT_FALSE(producerAttempted);
        EXPECT_FALSE(consumerAttempted);
        EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
        targetBarrier = validTargetBarrier;
    };

    {
        SCOPED_TRACE("graph-initial force marker");
        GpuCompiledBarrier corruptedBarrier = validInitialBarrier;
        corruptedBarrier.forceMemoryDependency = true;
        expectPreflightRejection(*initialBarrier, corruptedBarrier, validInitialBarrier);
    }
    {
        SCOPED_TRACE("forced transition carrying unordered access");
        GpuCompiledBarrier corruptedBarrier = validInitialBarrier;
        corruptedBarrier.before = ResourceStates::UnorderedAccess;
        corruptedBarrier.after = ResourceStates::UnorderedAccess;
        corruptedBarrier.isGraphInitialState = false;
        corruptedBarrier.forceMemoryDependency = true;
        expectPreflightRejection(*initialBarrier, corruptedBarrier, validInitialBarrier);
    }
    {
        SCOPED_TRACE("unordered-access dependency without force marker");
        GpuCompiledBarrier corruptedBarrier = validInitialBarrier;
        corruptedBarrier.type = GpuCompiledBarrierType::BufferUav;
        corruptedBarrier.before = ResourceStates::UnorderedAccess;
        corruptedBarrier.after = ResourceStates::UnorderedAccess;
        corruptedBarrier.isGraphInitialState = false;
        expectPreflightRejection(*initialBarrier, corruptedBarrier, validInitialBarrier);
    }
    {
        SCOPED_TRACE("forced external state export");
        GpuCompiledBarrier corruptedBarrier = validInitialBarrier;
        corruptedBarrier.type = GpuCompiledBarrierType::BufferStateExport;
        corruptedBarrier.isGraphInitialState = false;
        corruptedBarrier.forceMemoryDependency = true;
        expectPreflightRejection(*initialBarrier, corruptedBarrier, validInitialBarrier);
    }
    {
        SCOPED_TRACE("internal same-state transition without force marker");
        GpuCompiledBarrier corruptedBarrier = validDependencyBarrier;
        corruptedBarrier.forceMemoryDependency = false;
        expectPreflightRejection(*dependencyBarrier, corruptedBarrier, validDependencyBarrier);
    }

    producerAttempted = false;
    consumerAttempted = false;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(producerAttempted);
    EXPECT_TRUE(consumerAttempted);
    }
    EXPECT_FALSE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    recordedGraph.reset(compiledGraph);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

