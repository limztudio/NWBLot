// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_replay_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuCommandIrReplay, AcceptsOnlyFullUncompressedMultisampleTextureClears){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(Graphics::TextureDimension::Texture2DMS)
            .setFormat(Graphics::Format::RGBA8_UINT)
            .setSampleCount(4u)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/full_multisample_clear"))
            .setMarkerLabel("Replay Full Multisample Clear")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(resource.valid());
    Graphics::GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/command_ir_replay/full_multisample_clear_task"))
        .setMarkerLabel("Replay Full Multisample Clear Task")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    Graphics::GpuClearTextureTaskDesc clearDesc;
    clearDesc.destination = resource;
    clearDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearDesc.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    const Graphics::GpuTaskId task = graph.addClearTextureTask(taskDesc, clearDesc);
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    {
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);
        const Graphics::GpuSubmissionPacketId packet = reads.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        const Graphics::GpuPhysicalQueueId queueId = reads.compiled.packet(packet).plan->queue;

        Graphics::GpuCommandIrCapture fullCapture(testArena.arena);
        ASSERT_TRUE(fullCapture.captureClearTexture(task, packet, queueId, resource, clearDesc));
        const Graphics::GpuCommandIrReplayResult fullResult = Graphics::PreflightGpuCommandIrPacket(
            fullCapture.commandBytes(),
            reads.declarations,
            reads.compiled,
            packet
        );
        EXPECT_EQ(fullResult.error, Graphics::GpuCommandIrReplayError::None);

        Graphics::GpuClearTextureRectUIntTaskDesc rectDesc;
        rectDesc.destination = resource;
        rectDesc.subresources = clearDesc.subresources;
        rectDesc.rect = Graphics::Rect(4, 4);
        Graphics::GpuCommandIrCapture rectCapture(testArena.arena);
        ASSERT_TRUE(rectCapture.captureClearTextureRectUInt(task, packet, queueId, resource, rectDesc));
        const Graphics::GpuCommandIrReplayResult rectResult = Graphics::PreflightGpuCommandIrPacket(
            rectCapture.commandBytes(),
            reads.declarations,
            reads.compiled,
            packet
        );
        EXPECT_EQ(rectResult.error, Graphics::GpuCommandIrReplayError::InvalidTextureClear);
    }

    const Graphics::TextureDesc compressedDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::Texture* const compressedTextureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        compressedDescription
    );
    ASSERT_NE(compressedTextureObject, nullptr);
    Graphics::TextureHandle compressedTexture(
        compressedTextureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::GpuTaskGraph compressedGraph(testArena.arena);
    const Graphics::GpuGraphResourceId compressedResource = compressedGraph.importTexture(
        compressedTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/compressed_multisample_clear"))
            .setMarkerLabel("Replay Compressed Multisample Clear")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(compressedResource.valid());
    Graphics::GpuClearTextureTaskDesc compressedDesc;
    compressedDesc.destination = compressedResource;
    compressedDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    compressedDesc.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    const Graphics::GpuTaskResourceUse compressedUse{
        .resource = compressedResource,
        .range = Graphics::GpuTaskResourceRange{ .textureSubresources = compressedDesc.subresources },
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc compressedTaskDesc = taskDesc;
    compressedTaskDesc
        .setIdentity(Name("tests/command_ir_replay/compressed_multisample_clear_task"))
        .setMarkerLabel("Replay Compressed Multisample Clear Task")
        .setResourceUses(&compressedUse, 1u)
    ;
    const Graphics::GpuTaskId compressedTask = compressedGraph.addTask(compressedTaskDesc);
    ASSERT_TRUE(compressedTask.valid());
    Graphics::GpuTaskGraphAnalysis compressedAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments compressedAssignments(testArena.arena);
    Graphics::GpuCompiledGraph compressedCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        compressedGraph,
        compressedAnalysis,
        topology,
        compressedAssignments,
        compressedCompiledGraph
    ));
    const Tests::GpuTaskGraphReadViews reads(compressedGraph, compressedCompiledGraph);
    const Graphics::GpuSubmissionPacketId compressedPacket = reads.compiled.packetForTask(compressedTask);
    ASSERT_TRUE(compressedPacket.valid());
    const Graphics::GpuPhysicalQueueId compressedQueueId = reads.compiled.packet(compressedPacket).plan->queue;
    Graphics::GpuCommandIrCapture compressedCapture(testArena.arena);
    ASSERT_TRUE(compressedCapture.captureClearTexture(
        compressedTask,
        compressedPacket,
        compressedQueueId,
        compressedResource,
        compressedDesc
    ));
    const Graphics::GpuCommandIrReplayResult compressedResult = Graphics::PreflightGpuCommandIrPacket(
        compressedCapture.commandBytes(),
        reads.declarations,
        reads.compiled,
        compressedPacket
    );
    EXPECT_EQ(compressedResult.error, Graphics::GpuCommandIrReplayError::InvalidTextureClear);
    EXPECT_EQ(compressedResult.recordIndex, 0u);
}

TEST(GpuCommandIrReplay, TextureCopyCorruptionRequiresDeclaredAndActualQueueCapabilities){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createTexture = [&](const Graphics::TextureDesc& sourceDescription){
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!textureObject)
            return Graphics::TextureHandle{};
        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return texture;
    };

    const Graphics::TextureDesc validDescription = Graphics::TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setMipLevels(2u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    Graphics::TextureHandle sourceTexture = createTexture(validDescription);
    Graphics::TextureHandle destinationTexture = createTexture(validDescription);
    ASSERT_TRUE(sourceTexture);
    ASSERT_TRUE(destinationTexture);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = graph.importTexture(
        sourceTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/texture_copy_source"))
            .setMarkerLabel("Replay Texture Copy Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    const Graphics::GpuGraphResourceId destination = graph.importTexture(
        destinationTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/texture_copy_destination"))
            .setMarkerLabel("Replay Texture Copy Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());
    Graphics::TextureSlice mipOneSlice;
    mipOneSlice.setMipLevel(1u);
    const Graphics::GpuCopyTextureTaskRegion regions[]{
        Graphics::GpuCopyTextureTaskRegion{
            .source = source,
            .sourceSlice = {},
            .destination = destination,
            .destinationSlice = {},
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = source,
            .sourceSlice = mipOneSlice,
            .destination = destination,
            .destinationSlice = mipOneSlice,
        },
    };
    Graphics::GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/command_ir_replay/texture_copy"))
        .setMarkerLabel("Replay Texture Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuTaskId task = graph.addCopyTextureTask(
        taskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = regions,
            .regionCount = LengthOf(regions),
        }
    );
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_EQ(declarations.taskAt(task.index).queue.requiredCapabilities, Graphics::GpuQueueCapability::Transfer);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    Graphics::GpuSubmissionPacketId packet;
    Graphics::GpuPhysicalQueueId queueId;
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        packet = compiledPlan.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        queueId = compiledPlan.packet(packet).plan->queue;
    }

    const auto expectPreflight = [&](
        const Graphics::TextureSlice& sourceSlice,
        const Graphics::TextureSlice& destinationSlice,
        const Graphics::GpuCommandIrReplayError::Enum expectedError
    ){
        Graphics::GpuCommandIrCapture capture(testArena.arena);
        EXPECT_TRUE(capture.captureCopyTexture(
            task,
            packet,
            queueId,
            source,
            sourceSlice,
            destination,
            destinationSlice
        ));
        const Tests::GpuTaskGraphReadViews reads(graph, compiledGraph);

        const Graphics::GpuCommandIrReplayResult result = Graphics::PreflightGpuCommandIrPacket(
            capture.commandBytes(),
            reads.declarations,
            reads.compiled,
            packet
        );
        EXPECT_EQ(result.error, expectedError);
    };

    expectPreflight({}, {}, Graphics::GpuCommandIrReplayError::None);
    expectPreflight(mipOneSlice, mipOneSlice, Graphics::GpuCommandIrReplayError::None);
    Graphics::TextureSlice partialSlice;
    partialSlice.setSize(4u, 4u, 1u);
    expectPreflight(partialSlice, partialSlice, Graphics::GpuCommandIrReplayError::InvalidTextureCopy);

    const auto expectImmutableDescriptionRejected = [&](const Graphics::TextureDesc& rejectedDescription){
        Graphics::GpuTaskGraph rejectedGraph(testArena.arena);
        const ImportedTexturePair rejectedTextures = ImportTexturePair(
            testArena,
            context,
            allocator,
            rejectedGraph,
            rejectedDescription,
            rejectedDescription
        );
        ASSERT_TRUE(rejectedTextures.source.valid());
        ASSERT_TRUE(rejectedTextures.destination.valid());
        const Graphics::TextureSubresourceSet replaySubresources(0u, 1u, 0u, 1u);
        const Graphics::GpuTaskResourceUse replayUses[]{
            Graphics::GpuTaskResourceUse{
                .resource = rejectedTextures.source,
                .range = Graphics::GpuTaskResourceRange{ .textureSubresources = replaySubresources },
                .requiredState = Graphics::ResourceStates::CopySource,
                .access = Graphics::GpuTaskResourceAccess::Read,
            },
            Graphics::GpuTaskResourceUse{
                .resource = rejectedTextures.destination,
                .range = Graphics::GpuTaskResourceRange{ .textureSubresources = replaySubresources },
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc rejectedTaskDesc = taskDesc;
        rejectedTaskDesc
            .setIdentity(Name("tests/command_ir_replay/immutable_texture_copy_rejection"))
            .setMarkerLabel("Replay Immutable Texture Copy Rejection")
            .setResourceUses(replayUses, LengthOf(replayUses))
        ;
        const Graphics::GpuTaskId rejectedTask = rejectedGraph.addTask(rejectedTaskDesc);
        ASSERT_TRUE(rejectedTask.valid());
        Graphics::GpuTaskGraphAnalysis rejectedAnalysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments rejectedAssignments(testArena.arena);
        Graphics::GpuCompiledGraph rejectedCompiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(
            rejectedGraph,
            rejectedAnalysis,
            topology,
            rejectedAssignments,
            rejectedCompiledGraph
        ));
        const Tests::GpuTaskGraphReadViews reads(rejectedGraph, rejectedCompiledGraph);
        const Graphics::GpuSubmissionPacketId rejectedPacket = reads.compiled.packetForTask(rejectedTask);
        ASSERT_TRUE(rejectedPacket.valid());
        const Graphics::GpuPhysicalQueueId rejectedQueueId = reads.compiled.packet(rejectedPacket).plan->queue;
        Graphics::GpuCommandIrCapture rejectedCapture(testArena.arena);
        ASSERT_TRUE(rejectedCapture.captureCopyTexture(
            rejectedTask,
            rejectedPacket,
            rejectedQueueId,
            rejectedTextures.source,
            {},
            rejectedTextures.destination,
            {}
        ));
        const Graphics::GpuCommandIrReplayResult rejectedResult = Graphics::PreflightGpuCommandIrPacket(
            rejectedCapture.commandBytes(),
            reads.declarations,
            reads.compiled,
            rejectedPacket
        );
        EXPECT_EQ(rejectedResult.error, Graphics::GpuCommandIrReplayError::InvalidTextureCopy);
        EXPECT_EQ(rejectedResult.recordIndex, 0u);
    };

    Graphics::TextureDesc rejectedDescription = validDescription;
    rejectedDescription.sampleQuality = 1u;
    expectImmutableDescriptionRejected(rejectedDescription);

    rejectedDescription = validDescription;
    rejectedDescription
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setFormat(Graphics::Format::D32)
        .setMipLevels(1u)
        .setSampleCount(2u)
    ;
    expectImmutableDescriptionRejected(rejectedDescription);

    rejectedDescription
        .setFormat(Graphics::Format::BC1_UNORM)
        .setSampleCount(2u)
    ;
    expectImmutableDescriptionRejected(rejectedDescription);

    Graphics::GpuTaskGraph partialGraph(testArena.arena);
    const Graphics::GpuGraphResourceId partialSource = partialGraph.importTexture(
        sourceTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/partial_texture_copy_source"))
            .setMarkerLabel("Replay Partial Texture Copy Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    const Graphics::GpuGraphResourceId partialDestination = partialGraph.importTexture(
        destinationTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/partial_texture_copy_destination"))
            .setMarkerLabel("Replay Partial Texture Copy Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(partialSource.valid());
    ASSERT_TRUE(partialDestination.valid());
    const Graphics::GpuCopyTextureTaskRegion partialRegion{
        .source = partialSource,
        .sourceSlice = partialSlice,
        .destination = partialDestination,
        .destinationSlice = partialSlice,
    };
    const Graphics::GpuTaskId partialTask = partialGraph.addCopyTextureTask(
        taskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &partialRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(partialTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(partialGraph);

        EXPECT_EQ(
            declarations.taskAt(partialTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
        );
    }
    Graphics::GpuTaskGraphAnalysis partialAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments partialAssignments(testArena.arena);
    Graphics::GpuCompiledGraph partialCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(partialGraph, partialAnalysis, topology, partialAssignments, partialCompiledGraph));
    const Tests::GpuTaskGraphReadViews reads(partialGraph, partialCompiledGraph);
    const Graphics::GpuSubmissionPacketId partialPacket = reads.compiled.packetForTask(partialTask);
    ASSERT_TRUE(partialPacket.valid());
    const Graphics::GpuPhysicalQueueId partialQueueId = reads.compiled.packet(partialPacket).plan->queue;
    Graphics::GpuCommandIrCapture partialCapture(testArena.arena);
    ASSERT_TRUE(partialCapture.captureCopyTexture(
        partialTask,
        partialPacket,
        partialQueueId,
        partialSource,
        partialSlice,
        partialDestination,
        partialSlice
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            partialCapture.commandBytes(),
            reads.declarations,
            reads.compiled,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::None
    );
    const Graphics::GpuPhysicalQueueInfo* const partialQueue = reads.compiled.queueInfo(partialQueueId);
    ASSERT_NE(partialQueue, nullptr);
    Graphics::GpuPhysicalQueueInfo* const corruptedQueue = const_cast<Graphics::GpuPhysicalQueueInfo*>(
        partialQueue
    );
    const Graphics::GpuQueueCapability::Mask originalCapabilities = corruptedQueue->capabilities;
    corruptedQueue->capabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueueCapability::Graphics
    );
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            partialCapture.commandBytes(),
            reads.declarations,
            reads.compiled,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::InvalidTextureCopy
    );
    corruptedQueue->capabilities = originalCapabilities;
}

TEST(GpuCommandIrReplay, RequiresExactPhysicalQueueBeyondBroadQueueClass){
    const Graphics::GpuPhysicalQueueInfo packetQueue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueInfo sameClassOtherQueue = GraphicsQueue(1u);

    Graphics::CommandListParameters exactDescription;
    exactDescription.setPhysicalQueue(packetQueue.id);
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            exactDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::None
    );

    Graphics::CommandListParameters sameClassWrongDescription;
    sameClassWrongDescription.setPhysicalQueue(sameClassOtherQueue.id);
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            sameClassWrongDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::CommandListQueueMismatch
    );

    Graphics::CommandListParameters wrongClassDescription;
    wrongClassDescription.setPhysicalQueue(packetQueue.id);
    wrongClassDescription.queueType = Graphics::CommandQueue::Compute;
    EXPECT_EQ(
        Graphics::GpuCommandIrDetail::ValidateReplayCommandListQueue(
            wrongClassDescription,
            packetQueue.id,
            packetQueue.queueClass
        ),
        Graphics::GpuCommandIrReplayError::CommandListQueueMismatch
    );
}

TEST(GpuCommandIrReplay, PreflightsTheWholeStreamAgainstTheCompiledPacketBeforeLowering){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/source"),
        "Replay Source"
    );
    const Graphics::GpuGraphResourceId destination = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/destination"),
        "Replay Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = destination,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/command_ir_replay/copy"))
        .setMarkerLabel("Replay Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setResourceUses(uses, LengthOf(uses))
    ;
    const Graphics::GpuTaskId task = graph.addTask(desc);
    ASSERT_TRUE(task.valid());
    const Graphics::GpuTaskId secondDependencies[] = { task };
    Graphics::GpuTaskDesc secondDesc = desc;
    secondDesc
        .setIdentity(Name("tests/command_ir_replay/copy_second"))
        .setMarkerLabel("Replay Copy Second")
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const Graphics::GpuTaskId secondTask = graph.addTask(secondDesc);
    ASSERT_TRUE(secondTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(task);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledPlan.packetForTask(secondTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    const Graphics::GpuPhysicalQueueId queue = compiledPlan.packet(packet).plan->queue;
    ASSERT_EQ(compiledPlan.packet(secondPacket).plan->queue, queue);

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 0u, destination, 0u, 4u));
    ASSERT_TRUE(capture.captureCopyBuffer(secondTask, secondPacket, queue, source, 0u, destination, 0u, 4u));
    ASSERT_EQ(capture.recordCount(), 2u);
    const Graphics::GpuCommandIrReplayResult validContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        packet
    );
    // Metadata-only imports cannot be lowered, but preflight has already established the stream, graph,
    // packet, queue, task order, resource kinds, and declared CopySource/CopyDest uses before that boundary.
    EXPECT_EQ(validContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(validContext.streamValidation.valid());
    EXPECT_EQ(validContext.recordIndex, 0u);

    // A normal capture concatenates packet bodies. Selecting the second packet must skip the first record rather
    // than rejecting the full frame artifact before its requested packet is reached.
    const Graphics::GpuCommandIrReplayResult secondPacketContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        secondPacket
    );
    EXPECT_EQ(secondPacketContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(secondPacketContext.streamValidation.valid());
    EXPECT_EQ(secondPacketContext.recordIndex, 1u);

    const Graphics::GpuCommandIrReplayResult invalidPacket = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        declarations,
        compiledPlan,
        Graphics::GpuSubmissionPacketId{ Limit<u32>::s_Max - 1u, packet.generation }
    );
    EXPECT_EQ(invalidPacket.error, Graphics::GpuCommandIrReplayError::InvalidPacket);
    EXPECT_TRUE(invalidPacket.streamValidation.valid());

    Graphics::GpuCommandIrCapture wrongQueueCapture(testArena.arena);
    ASSERT_TRUE(wrongQueueCapture.captureCopyBuffer(
        task,
        packet,
        Graphics::GpuPhysicalQueueId{ static_cast<u16>(queue.index + 1u), queue.deviceGeneration },
        source,
        0u,
        destination,
        0u,
        4u
    ));
    const Graphics::GpuCommandIrReplayResult wrongQueue = Graphics::PreflightGpuCommandIrPacket(
        wrongQueueCapture.commandBytes(),
        declarations,
        compiledPlan,
        packet
    );
    EXPECT_EQ(wrongQueue.error, Graphics::GpuCommandIrReplayError::RecordQueueMismatch);
    EXPECT_TRUE(wrongQueue.streamValidation.valid());
    EXPECT_EQ(wrongQueue.recordIndex, 0u);

    const Graphics::GpuCommandIrReplayResult malformed = Graphics::PreflightGpuCommandIrPacket(
        BinaryByteView{},
        declarations,
        compiledPlan,
        packet
    );
    EXPECT_EQ(malformed.error, Graphics::GpuCommandIrReplayError::InvalidStream);
    EXPECT_EQ(malformed.streamValidation.error, Graphics::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

