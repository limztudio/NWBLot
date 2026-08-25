// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/host_readback_sync.h>
#include <core/graphics/vulkan/state_tracking_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_host_readback_sync_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace HostSync = Core::GraphicsBackend::VulkanDetail;
namespace StateTracking = Core::GraphicsBackend::VulkanStateTrackingDetail;

inline constexpr Name s_HostReadbackTestArena("tests/graphics/host_readback_sync");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(HostReadbackSync, ClassifiesEveryResourceStateBit){
    struct StateCase{
        ResourceStates::Mask state;
        bool writesBuffer;
    };
    constexpr StateCase s_Cases[] = {
        { ResourceStates::Common, false },
        { ResourceStates::ConstantBuffer, false },
        { ResourceStates::VertexBuffer, false },
        { ResourceStates::IndexBuffer, false },
        { ResourceStates::IndirectArgument, false },
        { ResourceStates::ShaderResource, false },
        { ResourceStates::UnorderedAccess, true },
        { ResourceStates::RenderTarget, false },
        { ResourceStates::DepthWrite, false },
        { ResourceStates::DepthRead, false },
        { ResourceStates::StreamOut, true },
        { ResourceStates::CopyDest, true },
        { ResourceStates::CopySource, false },
        { ResourceStates::ResolveDest, true },
        { ResourceStates::ResolveSource, false },
        { ResourceStates::Present, false },
        { ResourceStates::AccelStructRead, false },
        { ResourceStates::AccelStructWrite, true },
        { ResourceStates::AccelStructBuildInput, false },
        { ResourceStates::AccelStructBuildBlas, true },
        { ResourceStates::ShadingRateSurface, false },
        { ResourceStates::OpacityMicromapWrite, true },
        { ResourceStates::OpacityMicromapBuildInput, false },
        { ResourceStates::ConvertCoopVecMatrixInput, false },
        { ResourceStates::ConvertCoopVecMatrixOutput, true },
    };

    EXPECT_FALSE(HostSync::HasBufferDeviceWriteState(ResourceStates::Unknown));
    for(const StateCase& stateCase : s_Cases)
        EXPECT_EQ(HostSync::HasBufferDeviceWriteState(stateCase.state), stateCase.writesBuffer);
    EXPECT_TRUE(HostSync::HasBufferDeviceWriteState(
        ResourceStates::ShaderResource | ResourceStates::CopyDest
    ));
}

TEST(HostReadbackSync, BuildsExactWholeBufferHostDependency){
    const VkBuffer buffer = reinterpret_cast<VkBuffer>(static_cast<usize>(0x1234u));
    const VkBufferMemoryBarrier2 barrier = HostSync::BuildHostReadBufferBarrier(buffer);

    EXPECT_EQ(barrier.sType, VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    EXPECT_EQ(barrier.pNext, nullptr);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_MEMORY_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_HOST_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_HOST_READ_BIT);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(barrier.buffer, buffer);
    EXPECT_EQ(barrier.offset, 0u);
    EXPECT_EQ(barrier.size, VK_WHOLE_SIZE);
}

TEST(HostReadbackSync, DeduplicatesNativeBuffersAndAppendsOneBarrierEach){
    Alloc::GlobalArena arena(s_HostReadbackTestArena);
    HostSync::HostReadbackBarrierTracker tracker(arena);
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena> barriers(arena);
    const VkBuffer first = reinterpret_cast<VkBuffer>(static_cast<usize>(0x1000u));
    const VkBuffer second = reinterpret_cast<VkBuffer>(static_cast<usize>(0x2000u));

    EXPECT_FALSE(tracker.registerBuffer(VK_NULL_HANDLE));
    EXPECT_TRUE(tracker.registerBuffer(first));
    EXPECT_FALSE(tracker.registerBuffer(first));
    EXPECT_TRUE(tracker.registerBuffer(second));
    EXPECT_EQ(tracker.size(), 2u);
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    tracker.appendBarriers(barriers);
    ASSERT_EQ(barriers.size(), 2u);
    EXPECT_EQ(barriers[0u].buffer, first);
    EXPECT_EQ(barriers[1u].buffer, second);
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 2u);
#endif

    tracker.clear();
    EXPECT_EQ(tracker.size(), 0u);
    EXPECT_TRUE(tracker.registerBuffer(first));
}

TEST(HostReadbackSync, UniversalHostScopesSurviveEveryExactQueueClass){
    constexpr GpuQueueCapability::Mask s_Capabilities[] = {
        GpuQueueCapability::Graphics,
        GpuQueueCapability::Compute,
        GpuQueueCapability::Transfer,
    };

    for(const GpuQueueCapability::Mask capabilities : s_Capabilities){
        VkPipelineStageFlags2 sourceStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkAccessFlags2 sourceAccess = VK_ACCESS_2_MEMORY_WRITE_BIT;
        StateTracking::NormalizeBarrierScopeForQueueCapabilities(capabilities, sourceStage, sourceAccess);
        EXPECT_EQ(sourceStage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        EXPECT_EQ(sourceAccess, VK_ACCESS_2_MEMORY_WRITE_BIT);

        VkPipelineStageFlags2 destinationStage = VK_PIPELINE_STAGE_2_HOST_BIT;
        VkAccessFlags2 destinationAccess = VK_ACCESS_2_HOST_READ_BIT;
        StateTracking::NormalizeBarrierScopeForQueueCapabilities(
            capabilities,
            destinationStage,
            destinationAccess
        );
        EXPECT_EQ(destinationStage, VK_PIPELINE_STAGE_2_HOST_BIT);
        EXPECT_EQ(destinationAccess, VK_ACCESS_2_HOST_READ_BIT);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

