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
namespace QueuePresentWaitDisposition = Core::GraphicsBackend::VulkanDetail::QueuePresentWaitDisposition;
namespace ResourceStates = Core::ResourceStates;
namespace SwapChainOutputMode = Core::SwapChainOutputMode;
using Core::AcquiredBackBuffer;
using Core::AcquiredPresentationFrame;
using Core::FramebufferHandle;
using Core::GpuPhysicalQueueId;
using Core::GpuPhysicalQueueInfo;
using Core::TextureHandle;
using Core::GraphicsBackend::VulkanDetail::ClassifyQueuePresentWaitDisposition;
using Core::GraphicsBackend::VulkanDetail::IsPrimaryGraphicsPresentationQueue;
using Core::GraphicsBackend::VulkanDetail::SelectSurfaceFormat;
using Core::GraphicsBackend::VulkanDetail::SwapChainImagePresentationState;
using Core::GraphicsBackend::VulkanDetail::SwapChainSurfaceFormatSelection;

static_assert(SameAs<decltype(AcquiredBackBuffer::texture), TextureHandle>);
static_assert(SameAs<decltype(AcquiredPresentationFrame::framebuffer), FramebufferHandle>);

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


TEST(SwapChainPresentation, ClassifiesWhetherQueuePresentConsumedItsBinaryWait){
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_SUCCESS), QueuePresentWaitDisposition::Consumed);
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_SUBOPTIMAL_KHR), QueuePresentWaitDisposition::Consumed);
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_ERROR_OUT_OF_DATE_KHR), QueuePresentWaitDisposition::Consumed);
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_ERROR_SURFACE_LOST_KHR), QueuePresentWaitDisposition::Consumed);
    EXPECT_EQ(
        ClassifyQueuePresentWaitDisposition(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT),
        QueuePresentWaitDisposition::Consumed
    );
    EXPECT_EQ(
        ClassifyQueuePresentWaitDisposition(VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT),
        QueuePresentWaitDisposition::Consumed
    );
    EXPECT_EQ(
        ClassifyQueuePresentWaitDisposition(VK_ERROR_OUT_OF_HOST_MEMORY),
        QueuePresentWaitDisposition::NotConsumed
    );
    EXPECT_EQ(
        ClassifyQueuePresentWaitDisposition(VK_ERROR_OUT_OF_DEVICE_MEMORY),
        QueuePresentWaitDisposition::NotConsumed
    );
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_ERROR_DEVICE_LOST), QueuePresentWaitDisposition::DeviceLost);
    EXPECT_EQ(ClassifyQueuePresentWaitDisposition(VK_ERROR_UNKNOWN), QueuePresentWaitDisposition::Unknown);
}

TEST(SwapChainPresentation, AcquiredPresentationTypesRejectIncompleteOrInvalidSnapshots){
    AcquiredBackBuffer backBuffer;
    EXPECT_FALSE(backBuffer.valid());
    EXPECT_EQ(backBuffer.index, Limit<u32>::s_Max);
    EXPECT_EQ(backBuffer.nativeInitialState, ResourceStates::Unknown);

    backBuffer.index = 0u;
    backBuffer.nativeInitialState = ResourceStates::RenderTarget;
    EXPECT_FALSE(backBuffer.valid());

    AcquiredPresentationFrame frame;
    EXPECT_FALSE(frame.valid());
}

TEST(SwapChainPresentation, ReacquiredImagesExposeUnknownUntilTheirFirstConsumedPresent){
    SwapChainImagePresentationState images[2];

    EXPECT_EQ(images[0].nativeInitialState(), ResourceStates::Unknown);
    images[0].observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Consumed);

    EXPECT_EQ(images[1].nativeInitialState(), ResourceStates::Unknown);
    images[1].observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Consumed);

    EXPECT_EQ(images[0].nativeInitialState(), ResourceStates::Present);
}

TEST(SwapChainPresentation, PresentationStateAdvancesOnlyForConsumedWaitsAndResetsOnRebuild){
    SwapChainImagePresentationState state;
    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::NotConsumed);
    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::DeviceLost);
    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Unknown);
    EXPECT_FALSE(state.hasPresented);
    EXPECT_EQ(state.nativeInitialState(), ResourceStates::Unknown);

    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Consumed);
    EXPECT_TRUE(state.hasPresented);
    EXPECT_EQ(state.nativeInitialState(), ResourceStates::Present);

    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::NotConsumed);
    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::DeviceLost);
    state.observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Unknown);
    EXPECT_TRUE(state.hasPresented);
    EXPECT_EQ(state.nativeInitialState(), ResourceStates::Present);

    state = SwapChainImagePresentationState{};
    EXPECT_FALSE(state.hasPresented);
    EXPECT_EQ(state.nativeInitialState(), ResourceStates::Unknown);
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

