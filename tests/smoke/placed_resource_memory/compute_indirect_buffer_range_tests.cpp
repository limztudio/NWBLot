// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/headless_gtest_fixture.h>
#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_indirect_buffer_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;

// Minimal Vulkan 1.3 compute entry point with no resources and a 1x1x1 local size.
constexpr u32 s_ComputeSpirv[] = {
    0x07230203u, 0x00010600u, 0x00070000u, 0x00000005u, 0x00000000u,
    0x00020011u, 0x00000001u,
    0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000005u, 0x00000001u, 0x6e69616du, 0x00000000u,
    0x00060010u, 0x00000001u, 0x00000011u, 0x00000001u, 0x00000001u, 0x00000001u,
    0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u,
    0x00050036u, 0x00000002u, 0x00000001u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000004u,
    0x000100fdu,
    0x00010038u,
};

struct BarrierCapture{
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> barriers;
    PFN_vkCmdPipelineBarrier2 original = nullptr;

    explicit BarrierCapture(Alloc::ScratchArena& arena)
        : barriers(arena)
    {
        barriers.reserve(16u);
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
            NWB_ASSERT_MSG(false, "compute indirect barrier capture dispatch is unavailable");
        g_capture = &m_capture;
    }
    ~CaptureScope(){ g_capture = nullptr; }


private:
    BarrierCapture& m_capture;
    ScopedVulkanDeviceDispatchOverride<PFN_vkCmdPipelineBarrier2> m_override;
};

struct ComputeIndirectBufferRangeTestConfig : HeadlessGraphicsTestConfig{
};

class ComputeIndirectBufferRangeTest : public HeadlessGraphicsTest<ComputeIndirectBufferRangeTestConfig>{
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ComputeIndirectBufferRangeTest, DispatchConsumesOnlySelectedArgumentsAndPreservesAdjacentStates){
    auto& device = ComputeIndirectBufferRangeTest::device();
    ShaderDesc shaderDesc(ComputeIndirectBufferRangeTest::arena());
    shaderDesc.setShaderType(ShaderType::Compute).setDebugName(Name("tests/compute_indirect/ranges"));
    const ShaderHandle shader = device.createShader(shaderDesc, s_ComputeSpirv, sizeof(s_ComputeSpirv));
    ASSERT_TRUE(shader);
    const ComputePipelineHandle pipeline = device.createComputePipeline(ComputePipelineDesc().setComputeShader(shader));
    ASSERT_TRUE(pipeline);
    const BufferHandle buffer = device.createBuffer(BufferDesc().setByteSize(128u).setCanHaveUAVs(true).setIsDrawIndirectArgs(true));
    ASSERT_TRUE(buffer);
    const CommandListHandle commands = device.createCommandList();
    ASSERT_TRUE(commands);
    const DispatchIndirectArguments arguments;
    constexpr u32 s_FirstOffset = 16u;
    constexpr u32 s_SecondOffset = 64u;
    const BufferRange firstRange(s_FirstOffset, sizeof(arguments));
    const BufferRange secondRange(s_SecondOffset, sizeof(arguments));
    const BufferRange prefixRange(0u, s_FirstOffset);
    const BufferRange middleRange(firstRange.end(), s_SecondOffset - firstRange.end());
    const BufferRange suffixRange(secondRange.end(), 128u - secondRange.end());
    Alloc::ScratchArena scratchArena(Name("tests/compute_indirect/range_barriers"));
    BarrierCapture capture(scratchArena);
    CaptureScope captureScope(device, capture);

    commands->open();
    commands->beginTrackingBufferState(buffer.get(), ResourceStates::Common);
    ASSERT_TRUE(commands->tryWriteBuffer(*buffer, &arguments, sizeof(arguments), s_FirstOffset));
    ASSERT_TRUE(commands->tryWriteBuffer(*buffer, &arguments, sizeof(arguments), s_SecondOffset));
    commands->setBufferState(buffer.get(), ResourceStates::ShaderResource, false, prefixRange);
    commands->setBufferState(buffer.get(), ResourceStates::UnorderedAccess, false, suffixRange);
    commands->commitBarriers();
    ASSERT_FALSE(commands->commandRecordingFailed());
    capture.barriers.clear();

    const ComputeState state = ComputeState().setPipeline(pipeline.get()).setIndirectParams(buffer.get());
    commands->setComputeState(state);
    commands->dispatch(1u, 1u, 1u);
    ASSERT_FALSE(commands->commandRecordingFailed());
    EXPECT_TRUE(capture.barriers.empty());
    EXPECT_EQ(commands->getBufferState(buffer.get(), firstRange), ResourceStates::CopyDest);
    EXPECT_EQ(commands->getBufferState(buffer.get(), secondRange), ResourceStates::CopyDest);

    commands->dispatchIndirect(s_FirstOffset);
    ASSERT_FALSE(commands->commandRecordingFailed());
    ASSERT_EQ(capture.barriers.size(), 1u);
    EXPECT_EQ(capture.barriers[0u].offset, firstRange.byteOffset);
    EXPECT_EQ(capture.barriers[0u].size, firstRange.byteSize);
    EXPECT_EQ(capture.barriers[0u].dstAccessMask, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    EXPECT_EQ(commands->getBufferState(buffer.get(), firstRange), ResourceStates::IndirectArgument);
    EXPECT_EQ(commands->getBufferState(buffer.get(), secondRange), ResourceStates::CopyDest);

    capture.barriers.clear();
    commands->setComputeState(state);
    commands->dispatchIndirect(s_FirstOffset);
    ASSERT_FALSE(commands->commandRecordingFailed());
    EXPECT_TRUE(capture.barriers.empty());
    commands->dispatchIndirect(s_SecondOffset);
    ASSERT_FALSE(commands->commandRecordingFailed());
    ASSERT_EQ(capture.barriers.size(), 1u);
    EXPECT_EQ(capture.barriers[0u].offset, secondRange.byteOffset);
    EXPECT_EQ(capture.barriers[0u].size, secondRange.byteSize);

    ASSERT_TRUE(commands->tryWriteBuffer(*buffer, &arguments, sizeof(arguments), s_FirstOffset));
    commands->commitBarriers();
    capture.barriers.clear();
    commands->dispatchIndirect(s_FirstOffset);
    ASSERT_FALSE(commands->commandRecordingFailed());
    ASSERT_EQ(capture.barriers.size(), 1u);
    EXPECT_EQ(capture.barriers[0u].offset, firstRange.byteOffset);
    EXPECT_EQ(capture.barriers[0u].size, firstRange.byteSize);
    EXPECT_EQ(commands->getBufferState(buffer.get(), prefixRange), ResourceStates::ShaderResource);
    EXPECT_EQ(commands->getBufferState(buffer.get(), middleRange), ResourceStates::Common);
    EXPECT_EQ(commands->getBufferState(buffer.get(), suffixRange), ResourceStates::UnorderedAccess);
    EXPECT_EQ(commands->getBufferState(buffer.get(), secondRange), ResourceStates::IndirectArgument);
    commands->close();
    ASSERT_FALSE(commands->commandRecordingFailed());

    CommandList* const lists[] = { commands.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        lists, LengthOf(lists), commands->getDescription().physicalQueue, QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

