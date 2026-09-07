// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Primitive copies belong to the graph as typed task payloads: it derives their resource-state declarations,
// records direct native copies on the resolved physical queue, and publishes the accepted packet token only after
// submission. On this host the same proof automatically exercises either a dedicated Transfer family or Graphics
// fallback without a renderer-specific command thunk.
TEST_F(DescriptorBufferRoundTripTest, BuiltInCopyTextureTaskRecordsAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureDesc copyTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInUAV(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto source = device.createTexture(copyTextureDesc);
    auto destination = device.createTexture(copyTextureDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    Texture* const initialTextures[] = { source.get(), destination.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importTexture(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_source"))
            .setMarkerLabel("Built-In Copy Source")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId destinationResource = graph.importTexture(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_destination"))
            .setMarkerLabel("Built-In Copy Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    GpuTaskDesc copyTaskDesc;
    copyTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_texture"))
        .setMarkerLabel("Built-In Copy Texture")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(copyScheduling)
    ;
    const GpuCopyTextureTaskRegion copyRegions[] = {
        GpuCopyTextureTaskRegion{
            .source = sourceResource,
            .sourceSlice = {},
            .destination = destinationResource,
            .destinationSlice = {},
        },
    };
    QueueSubmissionToken acceptedToken;
    const GpuTaskId copyTask = graph.addCopyTextureTask(
        copyTaskDesc,
        GpuCopyTextureTaskDesc{
            .regions = copyRegions,
            .regionCount = LengthOf(copyRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.taskAt(copyTask.index).hasPayload);
        ASSERT_EQ(declarations.taskAt(copyTask.index).resourceUseCount, 2u);
    }

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_copy_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(
        assignment->queueClass,
        dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics
    );
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const copyCapture = commandIrCapture.recordAt(0u);
    ASSERT_NE(copyCapture, nullptr);
    EXPECT_EQ(copyCapture->opcode, GpuCommandIrOpcode::CopyTexture);
    EXPECT_EQ(copyCapture->task, copyTask);
    EXPECT_EQ(copyCapture->packet, packet);
    EXPECT_EQ(copyCapture->queue, views.compiled.packet(packet).plan->queue);
    EXPECT_EQ(copyCapture->source, sourceResource);
    EXPECT_EQ(copyCapture->destination, destinationResource);
    EXPECT_EQ(copyCapture->sourceSlice.mipLevel, copyRegions[0u].sourceSlice.mipLevel);
    EXPECT_EQ(copyCapture->sourceSlice.arraySlice, copyRegions[0u].sourceSlice.arraySlice);
    EXPECT_EQ(copyCapture->destinationSlice.mipLevel, copyRegions[0u].destinationSlice.mipLevel);
    EXPECT_EQ(copyCapture->destinationSlice.arraySlice, copyRegions[0u].destinationSlice.arraySlice);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, copyTask, finalStateStorage));

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
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


// A late unbound virtual destination must fail packet-wide preflight before a prior valid region emits native work.
TEST_F(DescriptorBufferRoundTripTest, BuiltInCopyTextureTaskRejectsUnboundTailAtomically){
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureDesc commonDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    const TextureHandle validSource = device.createTexture(commonDesc);
    const TextureHandle validDestination = device.createTexture(commonDesc);
    TextureDesc unboundDesc = commonDesc;
    unboundDesc.isVirtual = true;
    const TextureHandle unboundDestination = device.createTexture(unboundDesc);
    ASSERT_TRUE(validSource);
    ASSERT_TRUE(validDestination);
    ASSERT_TRUE(unboundDestination);
    ASSERT_FALSE(device.isTextureReadyForGpuUse(unboundDestination.get()));
    Texture* const primedTextures[] = { validSource.get(), validDestination.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        primedTextures,
        LengthOf(primedTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importTexture = [&](
        const TextureHandle& texture,
        const Name& identity,
        const AStringView label,
        const ResourceStates::Mask state
    ){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(state)
                .setExternalFinalState(state)
        );
    };
    const GpuGraphResourceId validSourceResource = importTexture(
        validSource,
        Name("tests/descriptor_buffer/copy_unbound_valid_source"),
        "Copy Unbound Valid Source",
        ResourceStates::Common
    );
    const GpuGraphResourceId validDestinationResource = importTexture(
        validDestination,
        Name("tests/descriptor_buffer/copy_unbound_valid_destination"),
        "Copy Unbound Valid Destination",
        ResourceStates::Common
    );
    const GpuGraphResourceId unboundDestinationResource = importTexture(
        unboundDestination,
        Name("tests/descriptor_buffer/copy_unbound_destination"),
        "Copy Unbound Destination",
        ResourceStates::Common
    );
    ASSERT_TRUE(validSourceResource.valid());
    ASSERT_TRUE(validDestinationResource.valid());
    ASSERT_TRUE(unboundDestinationResource.valid());

    const GpuCopyTextureTaskRegion regions[] = {
        GpuCopyTextureTaskRegion{
            .source = validSourceResource,
            .sourceSlice = {},
            .destination = validDestinationResource,
            .destinationSlice = {},
        },
        GpuCopyTextureTaskRegion{
            .source = validSourceResource,
            .sourceSlice = {},
            .destination = unboundDestinationResource,
            .destinationSlice = {},
        },
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/descriptor_buffer/copy_unbound_task"))
        .setMarkerLabel("Copy Unbound Task")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
    ;
    const QueueSubmissionToken staleToken{
        .queue = CommandQueue::Graphics,
        .value = 7u,
    };
    QueueSubmissionToken acceptedToken = staleToken;
    const GpuTaskId task = graph.addCopyTextureTask(
        taskDesc,
        GpuCopyTextureTaskDesc{
            .regions = regions,
            .regionCount = LengthOf(regions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(task.valid());
    EXPECT_FALSE(acceptedToken.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/copy_unbound_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    const usize validSourceReferences = validSource->getReferenceCount();
    const usize validDestinationReferences = validDestination->getReferenceCount();
    const usize unboundDestinationReferences = unboundDestination->getReferenceCount();
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture capture(DescriptorBufferRoundTripTest::arena());
    GpuSubmissionPacketId failedPacket;
    const GpuNativePacketRecorder recorder(device);
    acceptedToken = staleToken;
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &capture
    ));
    EXPECT_EQ(failedPacket, packet);
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
    EXPECT_FALSE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, task));
    EXPECT_EQ(validSource->getReferenceCount(), validSourceReferences);
    EXPECT_EQ(validDestination->getReferenceCount(), validDestinationReferences);
    EXPECT_EQ(unboundDestination->getReferenceCount(), unboundDestinationReferences);
    const u64 rejectedRecordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    ASSERT_NE(rejectedRecordingAttemptGeneration, 0u);
    // Packet recording failure already discarded the only task and cleared its output token. The resolved attempt
    // cannot be adopted afterward by a transaction that never owned its exact lifecycle binding.
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_FALSE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        rejectedRecordingAttemptGeneration
    ));
    EXPECT_FALSE(acceptedToken.valid());
}


TEST_F(DescriptorBufferRoundTripTest, BuiltInResolveTextureTaskRejectsSingleSampleSourceAtDeclaration){
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureDesc resolveTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto source = device.createTexture(resolveTextureDesc);
    auto destination = device.createTexture(resolveTextureDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importTexture(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_resolve_source"))
            .setMarkerLabel("Built-In Resolve Source")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId destinationResource = graph.importTexture(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_resolve_destination"))
            .setMarkerLabel("Built-In Resolve Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());

    GpuTaskDesc resolveTaskDesc;
    resolveTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_resolve_texture"))
        .setMarkerLabel("Built-In Resolve Texture")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const GpuResolveTextureTaskRegion resolveRegions[] = {
        GpuResolveTextureTaskRegion{
            .source = sourceResource,
            .destination = destinationResource,
        },
    };
    QueueSubmissionToken acceptedToken{
        .queue = CommandQueue::Graphics,
        .value = 1u,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    EXPECT_FALSE(graph.addResolveTextureTask(
        resolveTaskDesc,
        GpuResolveTextureTaskDesc{
            .regions = resolveRegions,
            .regionCount = LengthOf(resolveRegions),
            .acceptedToken = &acceptedToken,
        }
    ).valid());
    EXPECT_FALSE(acceptedToken.valid());
    const GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(declarations.taskCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

