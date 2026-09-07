// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "external_state_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The multi-producer export is not limited to direct native consumers: a later graph imports its exact terminal
// mip sources, freezes the merged state snapshot, and waits on the source graph's compact physical-queue frontier
// before recording either first consumer. This runs on the ordinary primary-Graphics topology.
TEST_F(DescriptorBufferRoundTripTest, ExternalFinalHandoffImportsIntoLaterGraph){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Unknown)
    );
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph sourceGraph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceTexture = sourceGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/multi_import_source_texture"))
            .setMarkerLabel("Multi Import Source Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Unknown)
            .setExternalFinalState(ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(graphicsQueue)
    );
    ASSERT_TRUE(sourceTexture.valid());
    const GpuTaskResourceUse sourceMip0Use{
        .resource = sourceTexture,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceUse sourceMip1Use{
        .resource = sourceTexture,
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
    GpuTaskDesc sourceMip0Desc;
    sourceMip0Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_source_mip0"))
        .setMarkerLabel("Multi Import Source Mip 0")
        .setQueue(graphicsRequest)
        .setResourceUses(&sourceMip0Use, 1u)
    ;
    GpuTaskDesc sourceMip1Desc;
    sourceMip1Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_source_mip1"))
        .setMarkerLabel("Multi Import Source Mip 1")
        .setQueue(graphicsRequest)
        .setResourceUses(&sourceMip1Use, 1u)
    ;
    bool sourceMip0Recorded = false;
    bool sourceMip1Recorded = false;
    const GpuTaskId sourceMip0Task = sourceGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        sourceMip0Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &sourceMip0Recorded,
        }
    );
    const GpuTaskId sourceMip1Task = sourceGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        sourceMip1Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &sourceMip1Recorded,
        }
    );
    ASSERT_TRUE(sourceMip0Task.valid());
    ASSERT_TRUE(sourceMip1Task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis sourceAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments sourceAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph sourceCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena sourceScratch(Name("tests/descriptor_buffer/multi_import_source_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(sourceGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            sourceAnalysis,
            topology,
            sourceAssignments,
            sourceCompiledGraph,
            sourceScratch
        ));
    }
    GpuSubmissionPacketId sourceMip0Packet;
    GpuSubmissionPacketId sourceMip1Packet;
    {
        const GpuTaskGraphReadViews sourceViews(sourceGraph, sourceCompiledGraph);
        ASSERT_TRUE(sourceViews.valid());
        sourceMip0Packet = sourceViews.compiled.packetForTask(sourceMip0Task);
        sourceMip1Packet = sourceViews.compiled.packetForTask(sourceMip1Task);
    }
    ASSERT_TRUE(sourceMip0Packet.valid());
    ASSERT_TRUE(sourceMip1Packet.valid());
    ASSERT_NE(sourceMip0Packet, sourceMip1Packet);
    GpuRecordedGraph sourceRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction sourceTransaction(DescriptorBufferRoundTripTest::arena());
    sourceTransaction.reset(sourceCompiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        sourceGraph,
        sourceCompiledGraph,
        GpuSubmissionPacketRange{ .first = sourceMip0Packet, .packetCount = 1u },
        sourceRecordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        sourceGraph,
        sourceCompiledGraph,
        GpuSubmissionPacketRange{ .first = sourceMip1Packet, .packetCount = 1u },
        sourceRecordedGraph
    ));
    EXPECT_TRUE(sourceMip0Recorded);
    EXPECT_TRUE(sourceMip1Recorded);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        sourceGraph,
        sourceCompiledGraph,
        sourceRecordedGraph,
        GpuSubmissionPacketRange{ .first = sourceMip0Packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        sourceTransaction,
        sourceScratch
    ));
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        sourceGraph,
        sourceCompiledGraph,
        sourceRecordedGraph,
        GpuSubmissionPacketRange{ .first = sourceMip1Packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        sourceTransaction,
        sourceScratch
    ));
    GpuTaskGraphExternalResourceHandoffSnapshot handoffSnapshot(DescriptorBufferRoundTripTest::arena());
    {
        const GpuTaskGraphReadViews sourceViews(sourceGraph, sourceCompiledGraph);
        ASSERT_TRUE(sourceTransaction.externalResourceHandoff(
            sourceGraph,
            sourceViews.declarations,
            sourceCompiledGraph,
            sourceViews.compiled,
            sourceRecordedGraph,
            sourceTexture,
            handoffSnapshot
        ));
        ASSERT_TRUE(handoffSnapshot.validFor(sourceCompiledGraph, sourceViews.compiled));
    }
    const GpuTaskGraphExternalResourceHandoff* const handoff = handoffSnapshot.value();
    ASSERT_NE(handoff, nullptr);
    ASSERT_EQ(handoff->terminalRangeCount, 2u);
    ASSERT_NE(handoff->terminalRanges, nullptr);
    ASSERT_EQ(handoff->waitTokenCount, 1u);

    // A descriptor cannot widen one terminal source to cover mip 1 when its immutable packet snapshot contains
    // only mip 0. The acquire must reject during recording rather than treating the nonempty subset as proof of
    // the whole claimed range.
    CommandListResourceStateHandoff mip0StateSource(DescriptorBufferRoundTripTest::arena());
    {
        const GpuTaskGraphReadViews sourceViews(sourceGraph, sourceCompiledGraph);
        ASSERT_TRUE(sourceRecordedGraph.copyTaskFinalStateSeed(
            sourceCompiledGraph,
            sourceViews.compiled,
            sourceMip0Task,
            mip0StateSource
        ));
    }
    GpuTaskGraph incompleteSourceGraph(DescriptorBufferRoundTripTest::arena());
    const GpuExternalCompletionId incompleteSourceCompletion = incompleteSourceGraph.importExternalCompletion(
        GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/descriptor_buffer/multi_import_incomplete_source_completion"))
            .setMarkerLabel("Multi Import Incomplete Source Completion")
    );
    ASSERT_TRUE(incompleteSourceCompletion.valid());
    const GpuGraphInitialOwnerHandoffSourceDesc incompleteSource{
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(0u, 2u, 0u, 1u),
        },
        .sourceQueue = handoff->terminalRanges[0u].sourceQueue,
        .destinationQueue = handoff->destinationQueue,
        .completion = incompleteSourceCompletion,
        .minimumCompletionToken = handoff->terminalRanges[0u].token,
        .stateSource = &mip0StateSource,
    };
    const GpuGraphResourceId incompleteSourceTexture = incompleteSourceGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/multi_import_incomplete_source_texture"))
            .setMarkerLabel("Multi Import Incomplete Source Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::ShaderResource)
            .setInitialOwnerHandoffSources(&incompleteSource, 1u)
    );
    ASSERT_TRUE(incompleteSourceTexture.valid());
    const GpuTaskResourceUse incompleteSourceUse{
        .resource = incompleteSourceTexture,
        .range = incompleteSource.range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc incompleteSourceTaskDesc;
    incompleteSourceTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_incomplete_source_use"))
        .setMarkerLabel("Multi Import Incomplete Source Use")
        .setQueue(graphicsRequest)
        .setResourceUses(&incompleteSourceUse, 1u)
    ;
    bool incompleteSourceRecorded = false;
    const GpuTaskId incompleteSourceTask = incompleteSourceGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        incompleteSourceTaskDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &incompleteSourceRecorded,
        }
    );
    ASSERT_TRUE(incompleteSourceTask.valid());
    GpuTaskGraphAnalysis incompleteSourceAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments incompleteSourceAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph incompleteSourceCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena incompleteSourceScratch(Name("tests/descriptor_buffer/multi_import_incomplete_source_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(incompleteSourceGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            incompleteSourceAnalysis,
            topology,
            incompleteSourceAssignments,
            incompleteSourceCompiledGraph,
            incompleteSourceScratch
        ));
    }
    {
        const GpuTaskGraphReadViews incompleteSourceViews(incompleteSourceGraph, incompleteSourceCompiledGraph);
        ASSERT_TRUE(incompleteSourceViews.valid());
        const GpuSubmissionPacketId incompleteSourcePacket = incompleteSourceViews.compiled.packetForTask(
            incompleteSourceTask
        );
        ASSERT_TRUE(incompleteSourcePacket.valid());
        GpuRecordedGraph incompleteSourceRecordedGraph(DescriptorBufferRoundTripTest::arena());
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            incompleteSourceGraph,
            incompleteSourceCompiledGraph,
            GpuSubmissionPacketRange{ .first = incompleteSourcePacket, .packetCount = 1u },
            incompleteSourceRecordedGraph
        ));
        EXPECT_FALSE(incompleteSourceRecordedGraph.packetSnapshot(incompleteSourcePacket).has_value());
        EXPECT_FALSE(incompleteSourceRecorded);
    }

    GpuTaskGraph destinationGraph(DescriptorBufferRoundTripTest::arena());
    const GpuExternalCompletionId destinationCompletions[] = {
        destinationGraph.importExternalCompletion(
            GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/descriptor_buffer/multi_import_destination_completion_mip0"))
                .setMarkerLabel("Multi Import Destination Completion Mip 0")
        ),
        destinationGraph.importExternalCompletion(
            GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/descriptor_buffer/multi_import_destination_completion_mip1"))
                .setMarkerLabel("Multi Import Destination Completion Mip 1")
        ),
    };
    ASSERT_TRUE(destinationCompletions[0u].valid());
    ASSERT_TRUE(destinationCompletions[1u].valid());
    const GpuGraphInitialOwnerHandoffSourceDesc destinationSources[] = {
        GpuGraphInitialOwnerHandoffSourceDesc{
            .range = handoff->terminalRanges[0u].range,
            .sourceQueue = handoff->terminalRanges[0u].sourceQueue,
            .destinationQueue = handoff->destinationQueue,
            .completion = destinationCompletions[0u],
            .minimumCompletionToken = handoff->terminalRanges[0u].token,
            .stateSource = handoff->stateSource,
        },
        GpuGraphInitialOwnerHandoffSourceDesc{
            .range = handoff->terminalRanges[1u].range,
            .sourceQueue = handoff->terminalRanges[1u].sourceQueue,
            .destinationQueue = handoff->destinationQueue,
            .completion = destinationCompletions[1u],
            .minimumCompletionToken = handoff->terminalRanges[1u].token,
            .stateSource = handoff->stateSource,
        },
    };
    const GpuGraphResourceId destinationTexture = destinationGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/multi_import_destination_texture"))
            .setMarkerLabel("Multi Import Destination Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(handoff->finalState)
            .setInitialOwnerHandoffSources(destinationSources, LengthOf(destinationSources))
    );
    ASSERT_TRUE(destinationTexture.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(destinationGraph);
        const GpuTaskGraphResourceView importedTexture = declarations.resourceAt(destinationTexture.index);
        ASSERT_EQ(importedTexture.initialOwnerHandoffSourceCount, LengthOf(destinationSources));
        ASSERT_NE(importedTexture.initialOwnerHandoffSources, nullptr);
        EXPECT_NE(importedTexture.initialOwnerHandoffSources[0u].stateSource, handoff->stateSource);
        EXPECT_NE(importedTexture.initialOwnerHandoffSources[1u].stateSource, handoff->stateSource);
    }

    const GpuTaskResourceUse destinationMip0Use{
        .resource = destinationTexture,
        .range = destinationSources[0u].range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskResourceUse destinationMip1Use{
        .resource = destinationTexture,
        .range = destinationSources[1u].range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc destinationMip0Desc;
    destinationMip0Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_destination_mip0"))
        .setMarkerLabel("Multi Import Destination Mip 0")
        .setQueue(graphicsRequest)
        .setResourceUses(&destinationMip0Use, 1u)
    ;
    GpuTaskDesc destinationMip1Desc;
    destinationMip1Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_destination_mip1"))
        .setMarkerLabel("Multi Import Destination Mip 1")
        .setQueue(graphicsRequest)
        .setResourceUses(&destinationMip1Use, 1u)
    ;
    ResourceStates::Mask destinationMip0State = ResourceStates::Unknown;
    ResourceStates::Mask destinationMip1State = ResourceStates::Unknown;
    bool destinationMip0Recorded = false;
    bool destinationMip1Recorded = false;
    const GpuTaskId destinationMip0Task = destinationGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        destinationMip0Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &destinationMip0State,
            .recorded = &destinationMip0Recorded,
        }
    );
    const GpuTaskId destinationMip1Task = destinationGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        destinationMip1Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &destinationMip1State,
            .observedMipLevel = 1u,
            .recorded = &destinationMip1Recorded,
        }
    );
    ASSERT_TRUE(destinationMip0Task.valid());
    ASSERT_TRUE(destinationMip1Task.valid());

    GpuTaskGraphAnalysis destinationAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments destinationAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph destinationCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena destinationScratch(Name("tests/descriptor_buffer/multi_import_destination_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(destinationGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            destinationAnalysis,
            topology,
            destinationAssignments,
            destinationCompiledGraph,
            destinationScratch
        ));
    }
    const GpuTaskGraphReadViews destinationViews(destinationGraph, destinationCompiledGraph);
    ASSERT_TRUE(destinationViews.valid());
    const GpuSubmissionPacketId destinationMip0Packet = destinationViews.compiled.packetForTask(destinationMip0Task);
    const GpuSubmissionPacketId destinationMip1Packet = destinationViews.compiled.packetForTask(destinationMip1Task);
    ASSERT_TRUE(destinationMip0Packet.valid());
    ASSERT_TRUE(destinationMip1Packet.valid());
    const GpuCompiledTaskView destinationMip0 = destinationViews.compiled.findTask(destinationMip0Task);
    const GpuCompiledTaskView destinationMip1 = destinationViews.compiled.findTask(destinationMip1Task);
    ASSERT_TRUE(destinationMip0.valid());
    ASSERT_TRUE(destinationMip1.valid());
    ASSERT_NE(destinationMip0.prologueBarriers, nullptr);
    ASSERT_NE(destinationMip1.prologueBarriers, nullptr);
    EXPECT_EQ(destinationMip0.prologueBarriers[0u].type, GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_EQ(destinationMip1.prologueBarriers[0u].type, GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_TRUE(destinationMip0.prologueBarriers[0u].isInitialOwnerHandoff);
    EXPECT_TRUE(destinationMip1.prologueBarriers[0u].isInitialOwnerHandoff);

    GpuRecordedGraph destinationRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction destinationTransaction(DescriptorBufferRoundTripTest::arena());
    destinationTransaction.reset(destinationCompiledGraph);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        GpuSubmissionPacketRange{ .first = destinationMip0Packet, .packetCount = 1u },
        destinationRecordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        GpuSubmissionPacketRange{ .first = destinationMip1Packet, .packetCount = 1u },
        destinationRecordedGraph
    ));
    EXPECT_TRUE(destinationMip0Recorded);
    EXPECT_TRUE(destinationMip1Recorded);
    EXPECT_EQ(destinationMip0State, ResourceStates::ShaderResource);
    EXPECT_EQ(destinationMip1State, ResourceStates::ShaderResource);

    const GpuTaskGraphExternalCompletionToken completionTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = destinationCompletions[0u],
            .token = handoff->waitTokens[0u],
        },
        GpuTaskGraphExternalCompletionToken{
            .completion = destinationCompletions[1u],
            .token = handoff->waitTokens[0u],
        },
    };
    // A token from the right physical queue is still insufficient when it predates the source range's accepted
    // terminal submission. The destination graph must reject it before a Vulkan acquire can race that producer.
    ASSERT_GT(handoff->terminalRanges[1u].token.value, 1u);
    QueueSubmissionToken staleMip1Token = handoff->terminalRanges[1u].token;
    --staleMip1Token.value;
    const GpuTaskGraphExternalCompletionToken staleCompletionTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = destinationCompletions[0u],
            .token = handoff->waitTokens[0u],
        },
        GpuTaskGraphExternalCompletionToken{
            .completion = destinationCompletions[1u],
            .token = staleMip1Token,
        },
    };
    GpuGraphSubmissionTransaction staleTransaction(DescriptorBufferRoundTripTest::arena());
    staleTransaction.reset(destinationCompiledGraph);
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = destinationMip1Packet, .packetCount = 1u },
        staleCompletionTokens,
        LengthOf(staleCompletionTokens),
        nullptr,
        0u,
        staleTransaction,
        destinationScratch
    ));
    EXPECT_FALSE(staleTransaction.taskToken(destinationViews.compiled, destinationMip1Task).valid());
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = destinationMip0Packet, .packetCount = 1u },
        completionTokens,
        LengthOf(completionTokens),
        nullptr,
        0u,
        destinationTransaction,
        destinationScratch
    ));
    // Once mip 0 accepts, a bad mip 1 completion must still be retryable.  The validation happens before native
    // submission, so it cannot discard this later graph task merely because the transaction is already bound.
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = destinationMip1Packet, .packetCount = 1u },
        staleCompletionTokens,
        LengthOf(staleCompletionTokens),
        nullptr,
        0u,
        destinationTransaction,
        destinationScratch
    ));
    EXPECT_FALSE(destinationTransaction.taskToken(destinationViews.compiled, destinationMip1Task).valid());
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = destinationMip1Packet, .packetCount = 1u },
        completionTokens,
        LengthOf(completionTokens),
        nullptr,
        0u,
        destinationTransaction,
        destinationScratch
    ));
    EXPECT_TRUE(destinationTransaction.taskToken(destinationViews.compiled, destinationMip0Task).valid());
    EXPECT_TRUE(destinationTransaction.taskToken(destinationViews.compiled, destinationMip1Task).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Target-topology companion to the same-Graphics graph-import proof above. Disjoint mips finish on Graphics and
// dedicated Compute, then a later Graphics graph waits on both accepted producer frontiers and imports the paired
// Compute-to-Graphics ownership acquire from one merged state source.
TEST_F(DescriptorBufferRoundTripTest, ExternalFinalHandoffImportsCrossQueueTextureRangesIntoLaterGraph){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Multi-producer external-final handoff: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Multi-producer external-final handoff: adapter has no dedicated compute-only queue family.";

    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    const GpuPhysicalQueueId computeQueue = BackendQueueId(device, CommandQueue::Compute);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(computeQueue.valid());
    ASSERT_NE(graphicsQueue, computeQueue);
    const TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Unknown)
    );
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph graph(asyncScope.arena());
    const GpuGraphResourceId resource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/external_final_cross_queue_texture"))
            .setMarkerLabel("External Final Cross-Queue Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Unknown)
            .setExternalFinalState(ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(graphicsQueue)
    );
    ASSERT_TRUE(resource.valid());

    const GpuTaskResourceUse graphicsUse{
        .resource = resource,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceUse computeUse{
        .resource = resource,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(1u, 1u, 0u, 1u),
        },
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc graphicsDesc;
    graphicsDesc
        .setIdentity(Name("tests/descriptor_buffer/external_final_cross_queue_graphics"))
        .setMarkerLabel("External Final Cross-Queue Graphics")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&graphicsUse, 1u)
    ;
    GpuTaskDesc computeDesc;
    computeDesc
        .setIdentity(Name("tests/descriptor_buffer/external_final_cross_queue_compute"))
        .setMarkerLabel("External Final Cross-Queue Compute")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            false,
            false,
        })
        .setResourceUses(&computeUse, 1u)
    ;
    bool graphicsRecorded = false;
    bool computeRecorded = false;
    const GpuTaskId graphicsTask = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        graphicsDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &graphicsRecorded,
        }
    );
    const GpuTaskId computeTask = graph.addTask<NativePacketExternalFinalTextureProbeTask>(
        computeDesc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .recorded = &computeRecorded,
        }
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/external_final_cross_queue_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId graphicsPacket;
    GpuSubmissionPacketId computePacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        graphicsPacket = views.compiled.packetForTask(graphicsTask);
        computePacket = views.compiled.packetForTask(computeTask);
        ASSERT_TRUE(graphicsPacket.valid());
        ASSERT_TRUE(computePacket.valid());
        ASSERT_NE(graphicsPacket, computePacket);
        const GpuCompiledTaskView compiledGraphics = views.compiled.findTask(graphicsTask);
        const GpuCompiledTaskView compiledCompute = views.compiled.findTask(computeTask);
        ASSERT_TRUE(compiledGraphics.valid());
        ASSERT_TRUE(compiledCompute.valid());
        EXPECT_EQ(compiledGraphics.plan->queue, graphicsQueue);
        EXPECT_EQ(compiledCompute.plan->queue, computeQueue);
        const GpuCompiledExternalResourceExportView exportInfo = views.compiled.externalResourceExport(resource);
        ASSERT_TRUE(exportInfo.valid());
        ASSERT_EQ(exportInfo.plan->sourceCount, 2u);
        EXPECT_FALSE(exportInfo.plan->producerTask.valid());
    }

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
    GpuTaskGraphExternalResourceHandoffSnapshot handoffSnapshot(asyncScope.arena());
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
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
            GpuSubmissionPacketRange{ .first = computePacket, .packetCount = 1u },
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
    }
    const GpuTaskGraphExternalResourceHandoff* const handoff = handoffSnapshot.value();
    ASSERT_NE(handoff, nullptr);
    ASSERT_EQ(handoff->producerCount, 2u);
    ASSERT_EQ(handoff->waitTokenCount, 2u);
    EXPECT_EQ(handoff->producers[0u].sourceQueue, graphicsQueue);
    EXPECT_EQ(handoff->producers[1u].sourceQueue, computeQueue);
    EXPECT_TRUE(handoff->waitTokens[0u].valid());
    EXPECT_TRUE(handoff->waitTokens[1u].valid());

    const auto waitTokenForRange = [&](const GpuTaskGraphExternalResourceHandoffRange& range){
        for(usize waitIndex = 0u; waitIndex < handoff->waitTokenCount; ++waitIndex){
            const QueueSubmissionToken& wait = handoff->waitTokens[waitIndex];
            if(
                wait.queue == range.token.queue
                && wait.physicalQueueIndex == range.token.physicalQueueIndex
                && wait.deviceGeneration == range.token.deviceGeneration
                && wait.value >= range.token.value
            )
                return &wait;
        }
        return static_cast<const QueueSubmissionToken*>(nullptr);
    };
    ASSERT_EQ(handoff->terminalRangeCount, 2u);
    ASSERT_NE(handoff->terminalRanges, nullptr);
    const QueueSubmissionToken* const mip0Wait = waitTokenForRange(handoff->terminalRanges[0u]);
    const QueueSubmissionToken* const mip1Wait = waitTokenForRange(handoff->terminalRanges[1u]);
    ASSERT_NE(mip0Wait, nullptr);
    ASSERT_NE(mip1Wait, nullptr);
    const GpuTaskGraphExternalResourceHandoffRange* computeRange = nullptr;
    for(usize rangeIndex = 0u; rangeIndex < handoff->terminalRangeCount; ++rangeIndex){
        if(handoff->terminalRanges[rangeIndex].sourceQueue == computeQueue){
            computeRange = handoff->terminalRanges + rangeIndex;
            break;
        }
    }
    ASSERT_NE(computeRange, nullptr);
    ASSERT_NE(handoff->stateSource, nullptr);
    EXPECT_TRUE(handoff->stateSource->coversTextureRangeWithOwnership(
        texture.get(),
        computeRange->range.textureSubresources,
        computeQueue,
        graphicsQueue
    ));
    EXPECT_FALSE(handoff->stateSource->coversTextureRangeWithOwnership(
        texture.get(),
        computeRange->range.textureSubresources,
        graphicsQueue,
        graphicsQueue
    ));
    EXPECT_FALSE(handoff->stateSource->coversTextureRangeWithOwnership(
        texture.get(),
        computeRange->range.textureSubresources,
        computeQueue,
        computeQueue
    ));

    GpuTaskGraph destinationGraph(asyncScope.arena());
    const GpuExternalCompletionId completions[] = {
        destinationGraph.importExternalCompletion(
            GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/descriptor_buffer/multi_import_cross_queue_completion_mip0"))
                .setMarkerLabel("Multi Import Cross-Queue Completion Mip 0")
        ),
        destinationGraph.importExternalCompletion(
            GpuExternalCompletionDesc{}
                .setIdentity(Name("tests/descriptor_buffer/multi_import_cross_queue_completion_mip1"))
                .setMarkerLabel("Multi Import Cross-Queue Completion Mip 1")
        ),
    };
    ASSERT_TRUE(completions[0u].valid());
    ASSERT_TRUE(completions[1u].valid());
    const GpuGraphInitialOwnerHandoffSourceDesc sources[] = {
        GpuGraphInitialOwnerHandoffSourceDesc{
            .range = handoff->terminalRanges[0u].range,
            .sourceQueue = handoff->terminalRanges[0u].sourceQueue,
            .destinationQueue = handoff->destinationQueue,
            .completion = completions[0u],
            .minimumCompletionToken = handoff->terminalRanges[0u].token,
            .stateSource = handoff->stateSource,
        },
        GpuGraphInitialOwnerHandoffSourceDesc{
            .range = handoff->terminalRanges[1u].range,
            .sourceQueue = handoff->terminalRanges[1u].sourceQueue,
            .destinationQueue = handoff->destinationQueue,
            .completion = completions[1u],
            .minimumCompletionToken = handoff->terminalRanges[1u].token,
            .stateSource = handoff->stateSource,
        },
    };
    const GpuGraphResourceId destinationTexture = destinationGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/multi_import_cross_queue_texture"))
            .setMarkerLabel("Multi Import Cross-Queue Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(handoff->finalState)
            .setInitialOwnerHandoffSources(sources, LengthOf(sources))
    );
    ASSERT_TRUE(destinationTexture.valid());
    const GpuTaskResourceUse mip0Use{
        .resource = destinationTexture,
        .range = sources[0u].range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskResourceUse mip1Use{
        .resource = destinationTexture,
        .range = sources[1u].range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuQueueRequest destinationQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc mip0Desc;
    mip0Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_cross_queue_mip0"))
        .setMarkerLabel("Multi Import Cross-Queue Mip 0")
        .setQueue(destinationQueue)
        .setResourceUses(&mip0Use, 1u)
    ;
    GpuTaskDesc mip1Desc;
    mip1Desc
        .setIdentity(Name("tests/descriptor_buffer/multi_import_cross_queue_mip1"))
        .setMarkerLabel("Multi Import Cross-Queue Mip 1")
        .setQueue(destinationQueue)
        .setResourceUses(&mip1Use, 1u)
    ;
    ResourceStates::Mask mip0State = ResourceStates::Unknown;
    ResourceStates::Mask mip1State = ResourceStates::Unknown;
    const GpuTaskId mip0Task = destinationGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        mip0Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &mip0State,
        }
    );
    const GpuTaskId mip1Task = destinationGraph.addTask<NativePacketExternalFinalTextureProbeTask>(
        mip1Desc,
        NativePacketExternalFinalTextureProbeTask::Payload{
            .texture = texture.get(),
            .observedState = &mip1State,
            .observedMipLevel = 1u,
        }
    );
    ASSERT_TRUE(mip0Task.valid());
    ASSERT_TRUE(mip1Task.valid());
    GpuTaskGraphAnalysis destinationAnalysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments destinationAssignments(asyncScope.arena());
    GpuCompiledGraph destinationCompiledGraph(asyncScope.arena());
    Alloc::ScratchArena destinationScratch(Name("tests/descriptor_buffer/multi_import_cross_queue_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(destinationGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            destinationAnalysis,
            topology,
            destinationAssignments,
            destinationCompiledGraph,
            destinationScratch
        ));
    }
    const GpuTaskGraphReadViews destinationViews(destinationGraph, destinationCompiledGraph);
    ASSERT_TRUE(destinationViews.valid());
    const GpuSubmissionPacketId mip0Packet = destinationViews.compiled.packetForTask(mip0Task);
    const GpuSubmissionPacketId mip1Packet = destinationViews.compiled.packetForTask(mip1Task);
    ASSERT_TRUE(mip0Packet.valid());
    ASSERT_TRUE(mip1Packet.valid());
    const GpuCompiledTaskView compiledMip0 = destinationViews.compiled.findTask(mip0Task);
    const GpuCompiledTaskView compiledMip1 = destinationViews.compiled.findTask(mip1Task);
    ASSERT_TRUE(compiledMip0.valid());
    ASSERT_TRUE(compiledMip1.valid());
    ASSERT_NE(compiledMip0.prologueBarriers, nullptr);
    ASSERT_NE(compiledMip1.prologueBarriers, nullptr);
    EXPECT_EQ(compiledMip0.prologueBarriers[0u].type, GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_EQ(compiledMip1.prologueBarriers[0u].type, GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_EQ(compiledMip0.prologueBarriers[0u].sourceQueue, handoff->terminalRanges[0u].sourceQueue);
    EXPECT_EQ(compiledMip1.prologueBarriers[0u].sourceQueue, handoff->terminalRanges[1u].sourceQueue);

    GpuRecordedGraph destinationRecordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction destinationTransaction(asyncScope.arena());
    destinationTransaction.reset(destinationCompiledGraph);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        GpuSubmissionPacketRange{ .first = mip0Packet, .packetCount = 1u },
        destinationRecordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        GpuSubmissionPacketRange{ .first = mip1Packet, .packetCount = 1u },
        destinationRecordedGraph
    ));
    EXPECT_EQ(mip0State, ResourceStates::ShaderResource);
    EXPECT_EQ(mip1State, ResourceStates::ShaderResource);
    const GpuTaskGraphExternalCompletionToken completionTokens[] = {
        GpuTaskGraphExternalCompletionToken{
            .completion = completions[0u],
            .token = *mip0Wait,
        },
        GpuTaskGraphExternalCompletionToken{
            .completion = completions[1u],
            .token = *mip1Wait,
        },
    };
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = mip0Packet, .packetCount = 1u },
        completionTokens,
        LengthOf(completionTokens),
        nullptr,
        0u,
        destinationTransaction,
        destinationScratch
    ));
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        destinationGraph,
        destinationCompiledGraph,
        destinationRecordedGraph,
        GpuSubmissionPacketRange{ .first = mip1Packet, .packetCount = 1u },
        completionTokens,
        LengthOf(completionTokens),
        nullptr,
        0u,
        destinationTransaction,
        destinationScratch
    ));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

