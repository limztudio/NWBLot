// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The optional IR lowerer selects one packet from a full primitive capture only after graph-aware preflight. A
// malformed later command in that packet must leave the earlier valid copy unrecorded; the success paths prove both
// the ordinary Core::CommandList lowerer and the explicitly pre-stated direct-Vulkan CopyBuffer prototype.
TEST_F(DescriptorBufferRoundTripTest, CommandIrPacketReplayPreflightsThenLowersCopyBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SourceWords[] = {
        0x7143a9d2u,
        0xcafebabeu,
        0x0badf00du,
        0xdecafbadU,
    };
    static constexpr u32 s_Sentinel = 0xa5a55a5au;
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    auto secondDestination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);
    u32* const sourceWords = static_cast<u32*>(device.mapBuffer(source.get(), CpuAccessMode::Write));
    ASSERT_NE(sourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        sourceWords[wordIndex] = s_SourceWords[wordIndex];
    device.unmapBuffer(source.get());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_source"))
            .setMarkerLabel("Command IR Replay Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_destination"))
            .setMarkerLabel("Command IR Replay Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondDestinationResource = graph.importBuffer(
        secondDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_second_destination"))
            .setMarkerLabel("Command IR Replay Second Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());

    GpuTaskDesc copyDesc;
    copyDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_copy"))
        .setMarkerLabel("Command IR Replay Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const GpuCopyBufferTaskRegion copyRegion{
        .source = sourceResource,
        .destination = destinationResource,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    const GpuTaskId secondDependencies[] = { copyTask };
    GpuTaskDesc secondCopyDesc = copyDesc;
    secondCopyDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_copy_second"))
        .setMarkerLabel("Command IR Replay Copy Second")
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const GpuCopyBufferTaskRegion secondCopyRegion{
        .source = sourceResource,
        .destination = secondDestinationResource,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    const GpuTaskId secondCopyTask = graph.addCopyBufferTask(
        secondCopyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &secondCopyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(secondCopyTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/command_ir_replay_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(copyTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondCopyTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    ASSERT_EQ(views.compiled.packet(packet).plan->queue, queue.id);
    ASSERT_EQ(views.compiled.packet(secondPacket).plan->queue, queue.id);

    auto clearDestination = device.createCommandList();
    ASSERT_NE(clearDestination.get(), nullptr);
    clearDestination->open();
    clearDestination->clearBufferUInt(destination.get(), s_Sentinel);
    clearDestination->clearBufferUInt(secondDestination.get(), s_Sentinel);
    clearDestination->close();
    ASSERT_TRUE(clearDestination->hasCommandBuffer());
    CommandList* const clearCommandLists[] = { clearDestination.get() };
    bool clearSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        clearCommandLists,
        LengthOf(clearCommandLists),
        CommandQueue::Graphics,
        &clearSubmitted
    ), 0u);
    ASSERT_TRUE(clearSubmitted);
    ASSERT_TRUE(device.waitForIdle());

    GpuCommandIrCapture malformedCapture(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(malformedCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        0u,
        sizeof(s_SourceWords)
    ));
    ASSERT_TRUE(malformedCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        sizeof(s_SourceWords) - sizeof(u32),
        sizeof(s_SourceWords)
    ));
    auto rejectedReplay = device.createCommandList();
    ASSERT_NE(rejectedReplay.get(), nullptr);
    rejectedReplay->open();
    ASSERT_TRUE(rejectedReplay->isRecording());
    const GpuCommandIrReplayResult rejectedResult = ReplayGpuCommandIrPacket(
        malformedCapture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *rejectedReplay
    );
    EXPECT_EQ(rejectedResult.error, GpuCommandIrReplayError::InvalidBufferCopy);
    EXPECT_EQ(rejectedResult.recordIndex, 1u);
    EXPECT_TRUE(rejectedResult.streamValidation.valid());
    rejectedReplay->close();
    EXPECT_FALSE(rejectedReplay->isRecording());
    CommandList* const rejectedCommandLists[] = { rejectedReplay.get() };
    bool rejectedSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        rejectedCommandLists,
        LengthOf(rejectedCommandLists),
        CommandQueue::Graphics,
        &rejectedSubmitted
    ), 0u);
    ASSERT_TRUE(rejectedSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const untouchedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(untouchedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(untouchedWords[wordIndex], s_Sentinel);
    device.unmapBuffer(destination.get());

    // The normal recorder produces one capture artifact for its complete packet range. Replay must select this
    // first packet from the two-packet stream, never emit the second packet's body, and retain all ordinary packet
    // state/barrier ownership in the graph recorder.
    GpuRecordedGraph capturedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture capture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        capturedGraph,
        nullptr,
        &capture
    ));
    ASSERT_EQ(capture.recordCount(), 2u);
    const GpuCommandIrBuiltinTaskRecord* const firstCapture = capture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const secondCapture = capture.recordAt(1u);
    ASSERT_NE(firstCapture, nullptr);
    ASSERT_NE(secondCapture, nullptr);
    EXPECT_EQ(firstCapture->task, copyTask);
    EXPECT_EQ(firstCapture->packet, packet);
    EXPECT_EQ(secondCapture->task, secondCopyTask);
    EXPECT_EQ(secondCapture->packet, secondPacket);

    const GpuPhysicalQueueTopology deviceTopology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* sameClassOtherQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < deviceTopology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = deviceTopology.queues[queueIndex];
        if(candidate.queueClass == queue.queueClass && candidate.id != queue.id){
            sameClassOtherQueue = &candidate;
            break;
        }
    }
    if(sameClassOtherQueue){
        CommandListParameters wrongQueueParameters;
        wrongQueueParameters.setPhysicalQueue(sameClassOtherQueue->id);
        CommandListHandle wrongQueueReplay = device.createCommandList(wrongQueueParameters);
        ASSERT_NE(wrongQueueReplay.get(), nullptr);
        wrongQueueReplay->open();
        ASSERT_TRUE(wrongQueueReplay->isRecording());

        const GpuCommandIrReplayResult wrongQueueResult = ReplayGpuCommandIrPacket(
            capture.commandBytes(),
            views.declarations,
            views.compiled,
            packet,
            *wrongQueueReplay
        );
        EXPECT_EQ(wrongQueueResult.error, GpuCommandIrReplayError::CommandListQueueMismatch);
        EXPECT_TRUE(wrongQueueResult.streamValidation.valid());
        const GpuCommandIrReplayResult wrongDirectQueueResult = ReplayGpuCommandIrPacketDirectVulkan(
            capture.commandBytes(),
            views.declarations,
            views.compiled,
            packet,
            *wrongQueueReplay
        );
        EXPECT_EQ(wrongDirectQueueResult.error, GpuCommandIrReplayError::CommandListQueueMismatch);
        EXPECT_TRUE(wrongDirectQueueResult.streamValidation.valid());
        wrongQueueReplay->close();
    }

    auto unopenedReplay = device.createCommandList();
    ASSERT_NE(unopenedReplay.get(), nullptr);
    const GpuCommandIrReplayResult unopenedResult = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *unopenedReplay
    );
    EXPECT_EQ(unopenedResult.error, GpuCommandIrReplayError::CommandListNotRecording);
    EXPECT_TRUE(unopenedResult.streamValidation.valid());

    CommandListParameters exactQueueParameters;
    exactQueueParameters.setPhysicalQueue(queue.id);
    auto stickyDirectReplay = device.createCommandList(exactQueueParameters);
    ASSERT_NE(stickyDirectReplay.get(), nullptr);
    stickyDirectReplay->open();
    constexpr u32 s_InvalidDirectPushConstant = 0x86c13ea5u;
    stickyDirectReplay->setPushConstants(&s_InvalidDirectPushConstant, sizeof(s_InvalidDirectPushConstant));
    ASSERT_TRUE(stickyDirectReplay->commandRecordingFailed());
    const GpuCommandIrReplayResult stickyDirectResult = ReplayGpuCommandIrPacketDirectVulkan(
        capture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *stickyDirectReplay
    );
    EXPECT_EQ(stickyDirectResult.error, GpuCommandIrReplayError::CommandListRecordingFailed);
    EXPECT_TRUE(stickyDirectResult.streamValidation.valid());
    stickyDirectReplay->close();
    EXPECT_FALSE(stickyDirectReplay->hasCommandBuffer());

    auto replay = device.createCommandList(exactQueueParameters);
    ASSERT_NE(replay.get(), nullptr);
    replay->open();
    ASSERT_TRUE(replay->isRecording());
    const GpuCommandIrReplayResult replayResult = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *replay
    );
    EXPECT_TRUE(replayResult.valid());
    EXPECT_TRUE(replayResult.streamValidation.valid());
    replay->close();
    EXPECT_FALSE(replay->isRecording());
    CommandList* const replayCommandLists[] = { replay.get() };
    bool replaySubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        replayCommandLists,
        LengthOf(replayCommandLists),
        queue.id,
        &replaySubmitted
    ), 0u);
    ASSERT_TRUE(replaySubmitted);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const replayedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(replayedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(replayedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());

    auto resetDirectDestination = device.createCommandList();
    ASSERT_NE(resetDirectDestination.get(), nullptr);
    resetDirectDestination->open();
    resetDirectDestination->clearBufferUInt(destination.get(), s_Sentinel);
    resetDirectDestination->close();
    CommandList* const resetDirectCommandLists[] = { resetDirectDestination.get() };
    bool resetDirectSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        resetDirectCommandLists,
        LengthOf(resetDirectCommandLists),
        CommandQueue::Graphics,
        &resetDirectSubmitted
    ), 0u);
    ASSERT_TRUE(resetDirectSubmitted);
    ASSERT_TRUE(device.waitForIdle());

    // The direct Vulkan prototype must reject an unsupported later opcode before it records the first valid copy.
    // This synthetic trace passes current graph/resource preflight (a clear can use the copy task's whole-buffer
    // CopyDest declaration), but it is intentionally outside the current CopyBuffer-only direct-lowering contract.
    GpuCommandIrCapture unsupportedDirectCapture(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(unsupportedDirectCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        0u,
        sizeof(s_SourceWords)
    ));
    ASSERT_TRUE(unsupportedDirectCapture.captureClearBuffer(
        copyTask,
        packet,
        queue.id,
        destinationResource,
        s_Sentinel
    ));
    auto unsupportedDirectReplay = device.createCommandList();
    ASSERT_NE(unsupportedDirectReplay.get(), nullptr);
    unsupportedDirectReplay->open();
    ASSERT_TRUE(unsupportedDirectReplay->isRecording());
    unsupportedDirectReplay->setBufferState(source.get(), ResourceStates::CopySource);
    unsupportedDirectReplay->setBufferState(destination.get(), ResourceStates::CopyDest);
    unsupportedDirectReplay->commitBarriers();
    const GpuCommandIrReplayResult unsupportedDirectResult = ReplayGpuCommandIrPacketDirectVulkan(
        unsupportedDirectCapture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *unsupportedDirectReplay
    );
    EXPECT_EQ(unsupportedDirectResult.error, GpuCommandIrReplayError::UnsupportedDirectVulkanOpcode);
    EXPECT_EQ(unsupportedDirectResult.recordIndex, 1u);
    EXPECT_TRUE(unsupportedDirectResult.streamValidation.valid());
    unsupportedDirectReplay->close();
    CommandList* const unsupportedDirectCommandLists[] = { unsupportedDirectReplay.get() };
    bool unsupportedDirectSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        unsupportedDirectCommandLists,
        LengthOf(unsupportedDirectCommandLists),
        CommandQueue::Graphics,
        &unsupportedDirectSubmitted
    ), 0u);
    ASSERT_TRUE(unsupportedDirectSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const stillSentinelWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(stillSentinelWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(stillSentinelWords[wordIndex], s_Sentinel);
    device.unmapBuffer(destination.get());

    // Model the graph recorder's already-established packet state, then lower only the selected CopyBuffer body
    // directly to Vulkan. The direct lowerer must not create implicit state transitions of its own.
    auto directVulkanReplay = device.createCommandList(exactQueueParameters);
    ASSERT_NE(directVulkanReplay.get(), nullptr);
    directVulkanReplay->open();
    ASSERT_TRUE(directVulkanReplay->isRecording());
    directVulkanReplay->setBufferState(source.get(), ResourceStates::CopySource);
    directVulkanReplay->setBufferState(destination.get(), ResourceStates::CopyDest);
    directVulkanReplay->commitBarriers();
    const GpuCommandIrReplayResult directVulkanResult = ReplayGpuCommandIrPacketDirectVulkan(
        capture.commandBytes(),
        views.declarations,
        views.compiled,
        packet,
        *directVulkanReplay
    );
    EXPECT_TRUE(directVulkanResult.valid());
    EXPECT_TRUE(directVulkanResult.streamValidation.valid());
    directVulkanReplay->close();
    CommandList* const directVulkanCommandLists[] = { directVulkanReplay.get() };
    bool directVulkanSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        directVulkanCommandLists,
        LengthOf(directVulkanCommandLists),
        queue.id,
        &directVulkanSubmitted
    ), 0u);
    ASSERT_TRUE(directVulkanSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const directVulkanWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(directVulkanWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(directVulkanWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());

    const u32* const untouchedSecondWords = static_cast<const u32*>(
        device.mapBuffer(secondDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(untouchedSecondWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(untouchedSecondWords[wordIndex], s_Sentinel);
    device.unmapBuffer(secondDestination.get());

    GpuGraphSubmissionTransaction terminalTransaction(DescriptorBufferRoundTripTest::arena());
    terminalTransaction.reset(compiledGraph);
    ASSERT_TRUE(terminalTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        capturedGraph.recordingAttemptGeneration()
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

