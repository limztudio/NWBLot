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


TEST_F(DescriptorBufferRoundTripTest, BuiltInUploadTextureTaskRecordsGraphOwnedBlobAndFinalState){
    auto& device = DescriptorBufferRoundTripTest::device();
    u8 sourceBytes[4u * 4u * 4u] = {
        0x1u, 0x2u, 0x3u, 0xffu,  0x4u, 0x5u, 0x6u, 0xffu,
        0x7u, 0x8u, 0x9u, 0xffu,  0xau, 0xbu, 0xcu, 0xffu,
        0xdu, 0xeu, 0xfu, 0xffu,  0x10u, 0x11u, 0x12u, 0xffu,
        0x13u, 0x14u, 0x15u, 0xffu, 0x16u, 0x17u, 0x18u, 0xffu,
        0x19u, 0x1au, 0x1bu, 0xffu, 0x1cu, 0x1du, 0x1eu, 0xffu,
        0x1fu, 0x20u, 0x21u, 0xffu, 0x22u, 0x23u, 0x24u, 0xffu,
        0x25u, 0x26u, 0x27u, 0xffu, 0x28u, 0x29u, 0x2au, 0xffu,
        0x2bu, 0x2cu, 0x2du, 0xffu, 0x2eu, 0x2fu, 0x30u, 0xffu,
    };
    u8 expectedBytes[sizeof(sourceBytes)];
    NWB_MEMCPY(expectedBytes, sizeof(expectedBytes), sourceBytes, sizeof(sourceBytes));
    auto destination = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(destination.get(), nullptr);
    auto keepInitialDestination = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(keepInitialDestination.get(), nullptr);
    Texture* const initialTextures[] = {
        destination.get(),
        keepInitialDestination.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId destinationResource = graph.importTexture(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture_destination"))
            .setMarkerLabel("Built-In Upload Texture Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(destinationResource.valid());
    const GpuGraphResourceId keepInitialDestinationResource = graph.importTexture(
        keepInitialDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture_keep_initial_destination"))
            .setMarkerLabel("Built-In Upload Texture Keep Initial Destination")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(keepInitialDestinationResource.valid());
    const GpuUploadBlobId source = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(source.valid());
    NWB_MEMSET(sourceBytes, 0, sizeof(sourceBytes));

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture"))
        .setMarkerLabel("Built-In Texture Upload")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
    ;
    QueueSubmissionToken acceptedToken;
    const GpuTaskId uploadTask = graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .finalState = ResourceStates::ShaderResource,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        const GpuTaskGraphTaskView uploadTaskView = declarations.taskAt(uploadTask.index);
        ASSERT_EQ(uploadTaskView.resourceUseCount, 2u);
        ASSERT_NE(uploadTaskView.resourceUses, nullptr);
        const TextureSubresourceSet uploadRange(0u, 1u, 0u, 1u);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].resource, destinationResource);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].range.textureSubresources, uploadRange);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].requiredState, ResourceStates::CopyDest);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].resource, destinationResource);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].range.textureSubresources, uploadRange);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].requiredState, ResourceStates::ShaderResource);
    }
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .mipLevel = 1u,
        }
    ).valid());
    // `writeTexture` lowers row/depth pitches into VkBufferImageCopy's 32-bit texel fields. The helper must reject
    // an otherwise small 2D upload whose explicit depth pitch exceeds that native limit, rather than accepting a
    // packet whose recorder later emits no copy.
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .rowPitch = 16u,
            .depthPitch = static_cast<usize>(Limit<u32>::s_Max + 1ull) * 16u,
        }
    ).valid());
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = keepInitialDestinationResource,
            .finalState = ResourceStates::ShaderResource,
        }
    ).valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_upload_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(uploadTask);
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
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, uploadTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(destination.get(), 0u, 0u), ResourceStates::ShaderResource);
    stateProbe->close();

    const GpuTaskScheduler submitter(device);
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
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.value, transaction.packetToken(packet).value);
    EXPECT_TRUE(device.waitForIdle());

    // The copied caller array was overwritten before late recording. Read the image back to prove native recording
    // resolved the graph-owned blob, rather than retaining a caller pointer or merely publishing a state handoff.
    StagingTextureHandle readback = device.createStagingTexture(destination->getDescription(), CpuAccessMode::Read);
    ASSERT_NE(readback.get(), nullptr);
    CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_NE(readbackCommandList.get(), nullptr);
    readbackCommandList->open(finalState);
    ASSERT_TRUE(readbackCommandList->hasCommandBuffer());
    readbackCommandList->copyTexture(*readback, TextureSlice{}, *destination, TextureSlice{});
    readbackCommandList->close();
    CommandList* const readbackLists[] = { readbackCommandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        readbackLists,
        LengthOf(readbackLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    usize readbackRowPitch = 0u;
    const auto* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        *readback,
        TextureSlice{},
        CpuAccessMode::Read,
        &readbackRowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(readbackRowPitch, 4u * sizeof(u32));
    for(u32 row = 0u; row < 4u; ++row){
        for(usize byte = 0u; byte < 4u * sizeof(u32); ++byte){
            EXPECT_EQ(
                readbackBytes[static_cast<usize>(row) * readbackRowPitch + byte],
                expectedBytes[static_cast<usize>(row) * 4u * sizeof(u32) + byte]
            );
        }
    }
    device.unmapStagingTexture(*readback);
}


// The buffer primitive follows the same late-recording/lifecycle contract as texture copies, while its two exact
// regions are retained by the payload. The Graphics producer and consumer make a dedicated Transfer route prove
// an exclusive Graphics -> Transfer -> Graphics ownership handoff; single-queue hosts exercise fallback.
TEST_F(DescriptorBufferRoundTripTest, BuiltInCopyBufferTaskRecordsAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SourceWords[] = {
        0x0347a2d1u,
        0x89abcdefu,
        0x5162f093u,
        0xc0ffee42u,
    };
    static constexpr u32 s_SecondSourceWords[] = {
        0x41f0a7c3u,
        0xdeadc0deu,
        0x0badf00du,
        0x76543210u,
    };
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto secondSource = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    auto secondDestination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(secondSource.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);

    u32* const sourceWords = static_cast<u32*>(device.mapBuffer(*source, CpuAccessMode::Write));
    ASSERT_NE(sourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        sourceWords[wordIndex] = s_SourceWords[wordIndex];
    device.unmapBuffer(*source);
    u32* const secondSourceWords = static_cast<u32*>(device.mapBuffer(*secondSource, CpuAccessMode::Write));
    ASSERT_NE(secondSourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondSourceWords); ++wordIndex)
        secondSourceWords[wordIndex] = s_SecondSourceWords[wordIndex];
    device.unmapBuffer(*secondSource);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_source"))
            .setMarkerLabel("Built-In Buffer Copy Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_destination"))
            .setMarkerLabel("Built-In Buffer Copy Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondSourceResource = graph.importBuffer(
        secondSource,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_second_source"))
            .setMarkerLabel("Built-In Buffer Copy Second Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondDestinationResource = graph.importBuffer(
        secondDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_second_destination"))
            .setMarkerLabel("Built-In Buffer Copy Second Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(secondSourceResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = sourceResource,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = secondSourceResource,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc producerTaskDesc;
    producerTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_producer"))
        .setMarkerLabel("Built-In Buffer Producer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(copyScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    bool producerRecorded = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        producerTaskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = source.get(),
            .expectedState = ResourceStates::CopySource,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskDesc copyTaskDesc;
    copyTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer"))
        .setMarkerLabel("Built-In Buffer Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(copyScheduling)
        .setDependencies(&producerTask, 1u)
    ;
    const GpuCopyBufferTaskRegion copyRegions[] = {
        GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .destination = destinationResource,
            .dataSizeBytes = sizeof(s_SourceWords),
        },
        GpuCopyBufferTaskRegion{
            .source = secondSourceResource,
            .destination = secondDestinationResource,
            .dataSizeBytes = sizeof(s_SecondSourceWords),
        },
    };
    QueueSubmissionToken acceptedToken;
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyTaskDesc,
        GpuCopyBufferTaskDesc{
            .regions = copyRegions,
            .regionCount = LengthOf(copyRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.taskAt(copyTask.index).hasPayload);
        ASSERT_EQ(declarations.taskAt(copyTask.index).resourceUseCount, 4u);
    }

    const GpuTaskResourceUse consumerUses[] = {
        GpuTaskResourceUse{
            .resource = destinationResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = secondDestinationResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc consumerTaskDesc;
    consumerTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_consumer"))
        .setMarkerLabel("Built-In Buffer Consumer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(copyScheduling)
        .setDependencies(&copyTask, 1u)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;
    bool consumerRecorded = false;
    const GpuTaskId consumerTask = graph.addTask<NativePacketPrefixTask>(
        consumerTaskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = destination.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &consumerRecorded,
        }
    );
    ASSERT_TRUE(consumerTask.valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_copy_buffer_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    const GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    const GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumerTask);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(assignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(
        assignment->queueClass,
        dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics
    );
    EXPECT_EQ(consumerAssignment->queueClass, CommandQueue::Graphics);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(copyTask);
    const GpuSubmissionPacketId consumerPacket = views.compiled.packetForTask(consumerTask);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(consumerPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 3u);

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
    ASSERT_EQ(commandIrCapture.recordCount(), LengthOf(copyRegions));
    for(usize regionIndex = 0u; regionIndex < LengthOf(copyRegions); ++regionIndex){
        const GpuCommandIrBuiltinTaskRecord* const copyCapture = commandIrCapture.recordAt(regionIndex);
        ASSERT_NE(copyCapture, nullptr);
        EXPECT_EQ(copyCapture->opcode, GpuCommandIrOpcode::CopyBuffer);
        EXPECT_EQ(copyCapture->task, copyTask);
        EXPECT_EQ(copyCapture->packet, packet);
        EXPECT_EQ(copyCapture->queue, views.compiled.packet(packet).plan->queue);
        EXPECT_EQ(copyCapture->source, copyRegions[regionIndex].source);
        EXPECT_EQ(copyCapture->destination, copyRegions[regionIndex].destination);
        EXPECT_EQ(copyCapture->sourceOffsetBytes, copyRegions[regionIndex].sourceOffsetBytes);
        EXPECT_EQ(copyCapture->destinationOffsetBytes, copyRegions[regionIndex].destinationOffsetBytes);
        EXPECT_EQ(copyCapture->dataSizeBytes, copyRegions[regionIndex].dataSizeBytes);
    }
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, copyTask, finalStateStorage));
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(consumerRecorded);

    const GpuTaskScheduler submitter(device);
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
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(*destination, CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(*destination);
    const u32* const secondCopiedWords = static_cast<const u32*>(
        device.mapBuffer(*secondDestination, CpuAccessMode::Read)
    );
    ASSERT_NE(secondCopiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondSourceWords); ++wordIndex)
        EXPECT_EQ(secondCopiedWords[wordIndex], s_SecondSourceWords[wordIndex]);
    device.unmapBuffer(*secondDestination);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

