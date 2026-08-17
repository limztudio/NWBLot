// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/swapchain_presentation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_swapchain_presentation_tests{

namespace Format = Core::Format;
namespace SwapChainOutputMode = Core::SwapChainOutputMode;
using Core::GpuPhysicalQueueId;
using Core::GpuPhysicalQueueInfo;
using Core::GraphicsBackend::VulkanDetail::IsPrimaryGraphicsPresentationQueue;
using Core::GraphicsBackend::VulkanDetail::SelectSurfaceFormat;
using Core::GraphicsBackend::VulkanDetail::SwapChainSurfaceFormatSelection;

constexpr VkSurfaceFormatKHR MakeSurfaceFormat(const VkFormat format, const VkColorSpaceKHR colorSpace){
    return { format, colorSpace };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SwapChainPresentation, PrefersHdr10PqWhenTheSurfaceAdvertisesIt){
    const VkSurfaceFormatKHR formats[] = {
        MakeSurfaceFormat(VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
        MakeSurfaceFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT),
    };

    SwapChainSurfaceFormatSelection selected;
    ASSERT_TRUE(SelectSurfaceFormat(
        formats,
        static_cast<u32>(LengthOf(formats)),
        Format::BGRA8_UNORM_SRGB,
        true,
        selected
    ));

    EXPECT_EQ(selected.outputMode, SwapChainOutputMode::HDR10);
    EXPECT_EQ(selected.backBufferFormat, Format::R10G10B10A2_UNORM);
    EXPECT_EQ(selected.surfaceFormat.format, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    EXPECT_EQ(selected.surfaceFormat.colorSpace, VK_COLOR_SPACE_HDR10_ST2084_EXT);
}

TEST(SwapChainPresentation, FallsBackToSdrWhenHdr10IsUnavailable){
    const VkSurfaceFormatKHR formats[] = {
        MakeSurfaceFormat(VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
    };

    SwapChainSurfaceFormatSelection selected;
    ASSERT_TRUE(SelectSurfaceFormat(
        formats,
        static_cast<u32>(LengthOf(formats)),
        Format::BGRA8_UNORM_SRGB,
        true,
        selected
    ));

    EXPECT_EQ(selected.outputMode, SwapChainOutputMode::SDR);
    EXPECT_EQ(selected.backBufferFormat, Format::BGRA8_UNORM_SRGB);
    EXPECT_EQ(selected.surfaceFormat.format, VK_FORMAT_B8G8R8A8_SRGB);
    EXPECT_EQ(selected.surfaceFormat.colorSpace, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

TEST(SwapChainPresentation, DoesNotSelectHdr10UnlessColorSpaceSupportWasEnabled){
    const VkSurfaceFormatKHR formats[] = {
        MakeSurfaceFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT),
        MakeSurfaceFormat(VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
    };

    SwapChainSurfaceFormatSelection selected;
    ASSERT_TRUE(SelectSurfaceFormat(
        formats,
        static_cast<u32>(LengthOf(formats)),
        Format::RGBA8_UNORM_SRGB,
        false,
        selected
    ));

    EXPECT_EQ(selected.outputMode, SwapChainOutputMode::SDR);
    EXPECT_EQ(selected.backBufferFormat, Format::RGBA8_UNORM_SRGB);
    EXPECT_EQ(selected.surfaceFormat.format, VK_FORMAT_R8G8B8A8_SRGB);
}

TEST(SwapChainPresentation, RejectsSurfacesWithoutACompatibleHdrOrSdrFormat){
    const VkSurfaceFormatKHR formats[] = {
        MakeSurfaceFormat(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT),
    };

    SwapChainSurfaceFormatSelection selected;
    EXPECT_FALSE(SelectSurfaceFormat(
        formats,
        static_cast<u32>(LengthOf(formats)),
        Format::BGRA8_UNORM_SRGB,
        true,
        selected
    ));
    EXPECT_EQ(selected.backBufferFormat, Format::UNKNOWN);
    EXPECT_EQ(selected.outputMode, SwapChainOutputMode::SDR);
}

TEST(SwapChainPresentation, RestrictsGraphPresentationSignalsToPrimaryGraphicsTransport){
    const GpuPhysicalQueueId primaryGraphicsQueue{
        .index = 1u,
        .deviceGeneration = 7u,
    };
    const GpuPhysicalQueueInfo primaryGraphicsInfo{
        .id = primaryGraphicsQueue,
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = Core::GpuQueueCapability::Graphics,
        .familyIndex = 3u,
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuPhysicalQueueInfo sameFamilyAuxiliaryGraphicsInfo{
        .id = GpuPhysicalQueueId{
            .index = 2u,
            .deviceGeneration = primaryGraphicsQueue.deviceGeneration,
        },
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = Core::GpuQueueCapability::Graphics,
        .familyIndex = primaryGraphicsInfo.familyIndex,
        .queueIndex = 1u,
        .dedicated = false,
    };
    const GpuPhysicalQueueInfo crossFamilyAuxiliaryGraphicsInfo{
        .id = GpuPhysicalQueueId{
            .index = 3u,
            .deviceGeneration = primaryGraphicsQueue.deviceGeneration,
        },
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = Core::GpuQueueCapability::Graphics,
        .familyIndex = 4u,
        .queueIndex = 0u,
        .dedicated = false,
    };

    EXPECT_TRUE(IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, &primaryGraphicsInfo));
    EXPECT_FALSE(IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, &sameFamilyAuxiliaryGraphicsInfo));
    EXPECT_FALSE(IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, &crossFamilyAuxiliaryGraphicsInfo));
    EXPECT_FALSE(IsPrimaryGraphicsPresentationQueue({}, &primaryGraphicsInfo));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
