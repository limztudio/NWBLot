// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/headless_gtest_fixture.h>
#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer_range_synchronization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;

struct BarrierCapture{
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> barriers;
    PFN_vkCmdPipelineBarrier2 original = nullptr;

    explicit BarrierCapture(Alloc::ScratchArena& arena)
        : barriers(arena)
    {
        barriers.reserve(32u);
    }
};

thread_local BarrierCapture* g_capture = nullptr;

VKAPI_ATTR void VKAPI_CALL CaptureBarrier(VkCommandBuffer commandBuffer, const VkDependencyInfo* dependency){
    NWB_ASSERT(g_capture);
    for(u32 index = 0u; index < dependency->bufferMemoryBarrierCount; ++index)
        g_capture->barriers.push_back(dependency->pBufferMemoryBarriers[index]);
    g_capture->original(commandBuffer, dependency);
}

class CaptureScope final : NoCopy{
public:
    CaptureScope(GraphicsBackend::Device& device, BarrierCapture& capture)
        : m_capture(capture)
        , m_override(device, &VolkDeviceTable::vkCmdPipelineBarrier2)
    {
        m_capture.original = m_override.original();
        if(!m_override.replace(&CaptureBarrier))
            NWB_ASSERT_MSG(false, "buffer barrier capture dispatch is unavailable");
        g_capture = &m_capture;
    }
    ~CaptureScope(){ g_capture = nullptr; }


private:
    BarrierCapture& m_capture;
    ScopedVulkanDeviceDispatchOverride<PFN_vkCmdPipelineBarrier2> m_override;
};

struct BufferRangeGpuTestConfig : HeadlessGraphicsTestConfig{
    static constexpr bool s_TransferQueueEnabled = true;
};

class BufferRangeGpuTest : public HeadlessGraphicsTest<BufferRangeGpuTestConfig>{
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(BufferRangeGpuTest, NativeTransitionsPreserveAdjacentByteStates){
    auto& device = BufferRangeGpuTest::device();
    auto buffer = device.createBuffer(BufferDesc().setByteSize(256u));
    ASSERT_TRUE(buffer);
    auto commands = device.createCommandList();
    ASSERT_TRUE(commands);
    Alloc::ScratchArena scratchArena(Name("tests/buffer_range/native_barriers"));
    BarrierCapture capture(scratchArena);
    CaptureScope captureScope(device, capture);
    commands->open();
    commands->beginTrackingBufferState(buffer.get(), ResourceStates::Common);
    commands->setBufferState(buffer.get(), ResourceStates::CopyDest, false, BufferRange(64u, 64u));
    commands->setBufferState(buffer.get(), ResourceStates::CopyDest, false, BufferRange(192u, 64u));
    commands->commitBarriers();
    ASSERT_FALSE(commands->commandRecordingFailed());
    ASSERT_EQ(capture.barriers.size(), 2u);
    EXPECT_EQ(capture.barriers[0u].offset, 64u);
    EXPECT_EQ(capture.barriers[0u].size, 64u);
    EXPECT_EQ(capture.barriers[1u].offset, 192u);
    EXPECT_EQ(capture.barriers[1u].size, 64u);
    EXPECT_EQ(commands->getBufferState(buffer.get(), BufferRange(0u, 64u)), ResourceStates::Common);
    EXPECT_EQ(commands->getBufferState(buffer.get(), BufferRange(64u, 64u)), ResourceStates::CopyDest);
    EXPECT_EQ(commands->getBufferState(buffer.get(), BufferRange(128u, 64u)), ResourceStates::Common);
    EXPECT_EQ(commands->getBufferState(buffer.get()), ResourceStates::Unknown);

    capture.barriers.clear();
    commands->setBufferState(buffer.get(), ResourceStates::CopySource, false, BufferRange(32u, 192u));
    commands->commitBarriers();
    ASSERT_EQ(capture.barriers.size(), 4u);
    const BufferRange expectedRanges[] = {
        BufferRange(32u, 32u), BufferRange(64u, 64u), BufferRange(128u, 64u), BufferRange(192u, 32u),
    };
    for(usize index = 0u; index < LengthOf(expectedRanges); ++index){
        EXPECT_EQ(capture.barriers[index].offset, expectedRanges[index].byteOffset);
        EXPECT_EQ(capture.barriers[index].size, expectedRanges[index].byteSize);
    }
    EXPECT_EQ(commands->getBufferState(buffer.get(), BufferRange(0u, 32u)), ResourceStates::Common);
    EXPECT_EQ(commands->getBufferState(buffer.get(), BufferRange(224u, 32u)), ResourceStates::CopyDest);
    commands->close();
    EXPECT_FALSE(commands->commandRecordingFailed());
}

TEST_F(BufferRangeGpuTest, RepeatedUavBarriersCoverOnlyActiveScratchPrefix){
    auto& device = BufferRangeGpuTest::device();
    constexpr u64 s_ScratchBytes = 1024u * 1024u;
    auto buffer = device.createBuffer(BufferDesc().setByteSize(s_ScratchBytes).setCanHaveUAVs(true));
    ASSERT_TRUE(buffer);
    auto commands = device.createCommandList();
    ASSERT_TRUE(commands);
    Alloc::ScratchArena scratchArena(Name("tests/buffer_range/repeated_uav"));
    BarrierCapture capture(scratchArena);
    CaptureScope captureScope(device, capture);
    commands->open();
    commands->beginTrackingBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    commands->setEnableUavBarriersForBuffer(buffer.get(), true);

    // Reused scratch can grow and shrink between meshes. Every same-state fence must retain the selected prefix.
    constexpr u64 s_ActiveBytes[] = { 1024u, 4096u, 1024u, s_ScratchBytes };
    for(const u64 activeBytes : s_ActiveBytes){
        for(u32 step = 0u; step < 3u; ++step){
            capture.barriers.clear();
            commands->setBufferState(buffer.get(), ResourceStates::UnorderedAccess, false, BufferRange(0u, activeBytes));
            commands->commitBarriers();
            ASSERT_FALSE(commands->commandRecordingFailed());
            ASSERT_EQ(capture.barriers.size(), 1u);
            EXPECT_EQ(capture.barriers.front().offset, 0u);
            EXPECT_EQ(capture.barriers.front().size, activeBytes);
            EXPECT_NE(capture.barriers.front().srcAccessMask & VK_ACCESS_2_SHADER_WRITE_BIT, 0u);
            EXPECT_NE(capture.barriers.front().dstAccessMask & VK_ACCESS_2_SHADER_WRITE_BIT, 0u);
        }
        EXPECT_EQ(commands->getBufferState(buffer.get()), ResourceStates::UnorderedAccess);
    }
    commands->close();
    EXPECT_FALSE(commands->commandRecordingFailed());
}

TEST_F(BufferRangeGpuTest, GraphDisjointUploadsFanInToFullBufferReadback){
    auto& device = BufferRangeGpuTest::device();
    const u32 firstWords[] = { 11u, 22u, 33u, 44u };
    const u32 secondWords[] = { 55u, 66u, 77u, 88u };
    constexpr u64 s_HalfSize = sizeof(firstWords);
    auto buffer = device.createBuffer(BufferDesc().setByteSize(s_HalfSize * 2u));
    auto readback = device.createBuffer(BufferDesc().setByteSize(s_HalfSize * 2u).setCpuAccess(CpuAccessMode::Read));
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(readback);
    GpuTaskGraph graph(BufferRangeGpuTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/buffer_range/upload"))
            .setMarkerLabel("Buffer Range Upload")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId readbackResource = graph.importBuffer(
        readback,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/buffer_range/readback"))
            .setMarkerLabel("Buffer Range Readback")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(bufferResource.valid());
    ASSERT_TRUE(readbackResource.valid());
    const GpuUploadBlobId firstBlob = graph.copyUploadData(firstWords, sizeof(firstWords), alignof(u32));
    const GpuUploadBlobId secondBlob = graph.copyUploadData(secondWords, sizeof(secondWords), alignof(u32));
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc taskDesc;
    taskDesc
        .setQueue(GpuQueueRequest{ GpuQueueCapability::Transfer, GpuQueuePreference::Graphics, false, false })
        .setScheduling(scheduling)
        .setMarkerLabel("Buffer Range Transfer")
    ;
    const GpuTaskId first = graph.addUploadBufferTask(
        taskDesc.setIdentity(Name("tests/buffer_range/first")),
        GpuUploadBufferTaskDesc{ .source = firstBlob, .destination = bufferResource }
    );
    const GpuTaskId second = graph.addUploadBufferTask(
        taskDesc.setIdentity(Name("tests/buffer_range/second")),
        GpuUploadBufferTaskDesc{ .source = secondBlob, .destination = bufferResource, .destinationOffsetBytes = s_HalfSize }
    );
    const GpuCopyBufferTaskRegion copyRegion{
        .source = bufferResource,
        .destination = readbackResource,
        .dataSizeBytes = s_HalfSize * 2u,
    };
    const GpuTaskId copy = graph.addCopyBufferTask(
        taskDesc.setIdentity(Name("tests/buffer_range/copy")),
        GpuCopyBufferTaskDesc{ .regions = &copyRegion, .regionCount = 1u }
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(copy.valid());
    GpuTaskGraphAnalysis analysis(BufferRangeGpuTest::arena());
    GpuTaskGraphQueueAssignments assignments(BufferRangeGpuTest::arena());
    GpuCompiledGraph compiled(BufferRangeGpuTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/buffer_range/graph_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(compiler.compile(declarations, analysis, device.getPhysicalQueueTopology(), assignments, compiled, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiled);
    ASSERT_TRUE(views.valid());
    EXPECT_EQ(views.compiled.findTask(copy).plan->prologueStateSeedCount, 2u);
    for(const GpuTaskDependencyEdge& edge : analysis.edges())
        EXPECT_FALSE(edge.producer == first && edge.consumer == second);

    GpuRecordedGraph recorded(BufferRangeGpuTest::arena());
    GpuGraphSubmissionTransaction transaction(BufferRangeGpuTest::arena());
    transaction.reset(compiled);
    const GpuSubmissionPacketRange range{ .first = views.compiled.packetForTask(first), .packetCount = 3u };
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(graph, compiled, range, recorded));
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph, compiled, recorded, range, nullptr, 0u, nullptr, 0u, transaction, scratchArena
    ));
    ASSERT_TRUE(device.waitForIdle());
    const auto* const words = static_cast<const u32*>(device.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(words, nullptr);
    for(usize index = 0u; index < LengthOf(firstWords); ++index){
        EXPECT_EQ(words[index], firstWords[index]);
        EXPECT_EQ(words[index + LengthOf(firstWords)], secondWords[index]);
    }
    device.unmapBuffer(readback.get());
}


TEST_F(BufferRangeGpuTest, DisjointOwnershipReleasesMatchAcquiresAndExecuteOnAnotherFamily){
    auto& device = BufferRangeGpuTest::device();
    const GpuPhysicalQueueId sourceQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    const GpuPhysicalQueueId destinationQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!sourceQueue.valid() || !destinationQueue.valid())
        GTEST_SKIP() << "No separate transfer and graphics queues are available.";
    const u32 sourceFamily = device.getQueueFamilyIndex(sourceQueue);
    const u32 destinationFamily = device.getQueueFamilyIndex(destinationQueue);
    if(sourceFamily == destinationFamily)
        GTEST_SKIP() << "Transfer and graphics queues share one Vulkan family.";
    ASSERT_NE(sourceFamily, VK_QUEUE_FAMILY_IGNORED);
    ASSERT_NE(destinationFamily, VK_QUEUE_FAMILY_IGNORED);

    const BufferRange releasedRanges[] = { BufferRange(0u, 64u), BufferRange(128u, 64u) };
    u32 firstWords[16u];
    u32 secondWords[16u];
    for(u32 index = 0u; index < LengthOf(firstWords); ++index){
        firstWords[index] = 100u + index;
        secondWords[index] = 200u + index;
    }
    auto buffer = device.createBuffer(BufferDesc().setByteSize(256u));
    auto readback = device.createBuffer(BufferDesc().setByteSize(128u).setCpuAccess(CpuAccessMode::Read));
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(readback);
    CommandListParameters sourceParameters;
    sourceParameters.setQueueType(CommandQueue::Transfer).setPhysicalQueue(sourceQueue);
    auto producer = device.createCommandList(sourceParameters);
    auto consumer = device.createCommandList();
    ASSERT_TRUE(producer);
    ASSERT_TRUE(consumer);
    CommandListResourceStateHandoff producerStates(BufferRangeGpuTest::arena());
    CommandListResourceStateHandoff firstSubset(BufferRangeGpuTest::arena());
    CommandListResourceStateHandoff secondSubset(BufferRangeGpuTest::arena());
    CommandListResourceStateHandoff consumerStates(BufferRangeGpuTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/buffer_range/ownership_barriers"));
    BarrierCapture capture(scratchArena);
    CaptureScope captureScope(device, capture);

    producer->open();
    producer->beginTrackingBufferState(buffer.get(), ResourceStates::Common);
    ASSERT_TRUE(producer->tryWriteBuffer(buffer.get(), firstWords, sizeof(firstWords), releasedRanges[0u].byteOffset));
    ASSERT_TRUE(producer->tryWriteBuffer(buffer.get(), secondWords, sizeof(secondWords), releasedRanges[1u].byteOffset));
    for(const BufferRange range : releasedRanges)
        producer->setBufferState(buffer.get(), ResourceStates::CopySource, false, range);
    producer->commitBarriers();
    ASSERT_FALSE(producer->commandRecordingFailed());
    capture.barriers.clear();
    for(const BufferRange range : releasedRanges)
        producer->releaseBufferOwnership(buffer.get(), destinationQueue, range);
    producer->close(&producerStates);
    ASSERT_FALSE(producer->commandRecordingFailed());
    ASSERT_TRUE(producerStates.valid());

    VkBufferMemoryBarrier2 releaseBarriers[LengthOf(releasedRanges)]{};
    usize releaseCount = 0u;
    for(const VkBufferMemoryBarrier2& barrier : capture.barriers){
        if(barrier.srcQueueFamilyIndex != sourceFamily || barrier.dstQueueFamilyIndex != destinationFamily)
            continue;
        ASSERT_LT(releaseCount, LengthOf(releaseBarriers));
        releaseBarriers[releaseCount] = barrier;
        EXPECT_EQ(barrier.offset, releasedRanges[releaseCount].byteOffset);
        EXPECT_EQ(barrier.size, releasedRanges[releaseCount].byteSize);
        EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_NONE);
        EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_NONE);
        ++releaseCount;
    }
    ASSERT_EQ(releaseCount, LengthOf(releasedRanges));
    ASSERT_TRUE(firstSubset.buildBufferRangeSubset(producerStates, buffer.get(), releasedRanges[0u]));
    ASSERT_TRUE(secondSubset.buildBufferRangeSubset(producerStates, buffer.get(), releasedRanges[1u]));
    const CommandListResourceStateHandoff* const branches[] = { &secondSubset };
    ASSERT_TRUE(consumerStates.buildFanIn(firstSubset, branches, LengthOf(branches), scratchArena));

    capture.barriers.clear();
    consumer->open(&consumerStates);
    ASSERT_FALSE(consumer->commandRecordingFailed());
    ASSERT_EQ(capture.barriers.size(), LengthOf(releasedRanges));
    for(usize index = 0u; index < LengthOf(releasedRanges); ++index){
        const VkBufferMemoryBarrier2& acquire = capture.barriers[index];
        EXPECT_EQ(acquire.buffer, releaseBarriers[index].buffer);
        EXPECT_EQ(acquire.offset, releaseBarriers[index].offset);
        EXPECT_EQ(acquire.size, releaseBarriers[index].size);
        EXPECT_EQ(acquire.srcQueueFamilyIndex, releaseBarriers[index].srcQueueFamilyIndex);
        EXPECT_EQ(acquire.dstQueueFamilyIndex, releaseBarriers[index].dstQueueFamilyIndex);
        EXPECT_EQ(acquire.srcStageMask, VK_PIPELINE_STAGE_2_NONE);
        EXPECT_EQ(acquire.srcAccessMask, VK_ACCESS_2_NONE);
        EXPECT_EQ(consumer->getBufferState(buffer.get(), releasedRanges[index]), ResourceStates::CopySource);
    }
    EXPECT_FALSE(consumer->hasExplicitBufferState(buffer.get(), BufferRange(64u, 64u)));
    EXPECT_FALSE(consumer->hasExplicitBufferState(buffer.get(), BufferRange(192u, 64u)));
    EXPECT_EQ(consumer->getBufferState(buffer.get(), BufferRange(64u, 64u)), ResourceStates::Unknown);
    EXPECT_EQ(consumer->getBufferState(buffer.get(), BufferRange(192u, 64u)), ResourceStates::Unknown);
    consumer->beginTrackingBufferState(readback.get(), ResourceStates::Common);
    consumer->copyBuffer(readback.get(), 0u, buffer.get(), releasedRanges[0u].byteOffset, releasedRanges[0u].byteSize);
    consumer->copyBuffer(readback.get(), 64u, buffer.get(), releasedRanges[1u].byteOffset, releasedRanges[1u].byteSize);
    consumer->close();
    ASSERT_FALSE(consumer->commandRecordingFailed());

    CommandList* const producerLists[] = { producer.get() };
    const QueueSubmissionToken producerToken = device.executeCommandLists(
        producerLists, LengthOf(producerLists), sourceQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(producerToken.valid());
    CommandList* const consumerLists[] = { consumer.get() };
    const QueueSubmissionToken consumerToken = device.executeCommandLists(
        consumerLists,
        LengthOf(consumerLists),
        destinationQueue,
        QueueSubmissionDesc().setWaitTokens(&producerToken, 1u)
    );
    ASSERT_TRUE(consumerToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    const auto* const words = static_cast<const u32*>(device.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(words, nullptr);
    for(usize index = 0u; index < LengthOf(firstWords); ++index){
        EXPECT_EQ(words[index], firstWords[index]);
        EXPECT_EQ(words[index + LengthOf(firstWords)], secondWords[index]);
    }
    device.unmapBuffer(readback.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

