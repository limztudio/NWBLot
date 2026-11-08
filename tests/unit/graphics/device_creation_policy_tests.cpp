// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/rhi/hardware_ray_tracing_policy.h>
#include <core/graphics/vulkan/vulkan_queue_request_plan.h>
#include <core/graphics/vulkan/physical_queue_registration_policy.h>
#include <core/graphics/vulkan/physical_queue_sharing_policy.h>
#include <core/graphics/vulkan/swap_chain_surface_policy.h>
#include <tests/common/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_creation_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(DeviceCreationPolicy, SurfacePolicyKeepsFallbackOrderAndRejectsMissingAlpha){
    using Policy = Core::GraphicsBackend::SwapChainSurfacePolicy;
    const Array<VkPresentModeKHR, 2> modes = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR };
    const auto mode = Policy::selectPresentMode(modes, false);
    ASSERT_TRUE(mode);
    EXPECT_EQ(*mode, VK_PRESENT_MODE_FIFO_KHR);
    EXPECT_FALSE(Policy::selectPresentMode({}, false));

    const Array<VkSurfaceFormatKHR, 1> formats = {{ { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } }};
    const auto format = Policy::selectFormat(formats, Core::Format::RGBA8_UNORM_SRGB, true);
    ASSERT_TRUE(format);
    EXPECT_EQ(format->surfaceFormat.format, VK_FORMAT_R8G8B8A8_SRGB);
    EXPECT_EQ(format->outputMode, Core::SwapChainOutputMode::SDR);

    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = { UINT32_MAX, UINT32_MAX };
    caps.minImageExtent = { 100u, 200u };
    caps.maxImageExtent = { 500u, 600u };
    caps.minImageCount = 2u;
    caps.maxImageCount = 3u;
    const VkExtent2D extent = Policy::resolveExtent(caps, 1u, 900u);
    EXPECT_EQ(extent.width, 100u);
    EXPECT_EQ(extent.height, 600u);
    EXPECT_EQ(Policy::imageCount(caps, 5u), 3u);
    EXPECT_FALSE(Policy::selectCompositeAlpha(caps));
    caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    const auto alpha = Policy::selectCompositeAlpha(caps);
    ASSERT_TRUE(alpha);
    EXPECT_EQ(*alpha, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);
}

TEST(DeviceCreationPolicy, QueueRegistrationRejectsBoundaryAndDuplicateIdentity){
    using Policy = Core::GraphicsBackend::PhysicalQueueRegistrationPolicy;
    const Core::GraphicsBackend::NativeQueueState native(reinterpret_cast<VkQueue>(1u), 4u, 2u);
    Core::GraphicsBackend::VulkanPhysicalQueueDesc desc{};
    desc.queueClass = Core::CommandQueue::Graphics;
    desc.capabilities = Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Transfer;
    desc.timestampValidBits = 35u;
    Array<b8, static_cast<u32>(Core::CommandQueue::kCount)> primaries{};
    EXPECT_EQ(Policy::validate(desc, native, {}, primaries), Policy::Result::Invalid);
    desc.timestampValidBits = 36u;
    EXPECT_EQ(Policy::validate(desc, native, {}, primaries), Policy::Result::Valid);
    Array<Core::GpuPhysicalQueueInfo, 1> existing{};
    existing[0].familyIndex = 4u;
    existing[0].queueIndex = 2u;
    EXPECT_EQ(Policy::validate(desc, native, existing, primaries), Policy::Result::DuplicateQueue);
    desc.primaryForClass = true;
    primaries[static_cast<u32>(Core::CommandQueue::Graphics)] = true;
    EXPECT_EQ(Policy::validate(desc, native, {}, primaries), Policy::Result::DuplicatePrimary);
}

TEST(DeviceCreationPolicy, QueueSharingUsesFirstDistinctFamilyAndHandlesMissingPrimary){
    using Policy = Core::GraphicsBackend::PhysicalQueueSharingPolicy;
    constexpr u32 missing = Limit<u32>::s_Max;
    Array<Core::GpuPhysicalQueueInfo, 5> queues{};
    queues[0].queueClass = Core::CommandQueue::Graphics;
    queues[0].familyIndex = 4u;
    queues[1].queueClass = Core::CommandQueue::Graphics;
    queues[1].familyIndex = 7u;
    queues[2].queueClass = Core::CommandQueue::Graphics;
    queues[2].familyIndex = 8u;
    queues[3].queueClass = Core::CommandQueue::Compute;
    queues[3].familyIndex = 4u;
    queues[4].queueClass = Core::CommandQueue::Transfer;
    queues[4].familyIndex = 9u;
    const auto shared = Policy::resolve(queues, 4u, 4u, 9u);
    EXPECT_EQ(shared.graphics, 4);
    EXPECT_EQ(shared.auxiliaryGraphics, 7);
    EXPECT_EQ(shared.auxiliaryCompute, -1);
    EXPECT_FALSE(shared.asyncComputeLaneEnabled);
    EXPECT_TRUE(shared.transferQueueEnabled);
    const auto missingGraphics = Policy::resolve(queues, missing, 4u, 4u);
    EXPECT_EQ(missingGraphics.graphics, -1);
    EXPECT_EQ(missingGraphics.auxiliaryGraphics, -1);
    EXPECT_FALSE(missingGraphics.asyncComputeLaneEnabled);
    EXPECT_FALSE(missingGraphics.transferQueueEnabled);
}

TEST(DeviceCreationPolicy, CanonicalQueuesRejectAliasedHandlesAndOutOfRangeIndices){
    using Policy = Core::GraphicsBackend::PhysicalQueueRegistrationPolicy;
    Array<VkQueueFamilyProperties, 1> families{};
    families[0].queueCount = 2u;
    Array<Core::GraphicsBackend::VulkanNativeQueueDesc, 2> queues{};
    queues[0].queue = reinterpret_cast<VkQueue>(1u);
    queues[0].familyIndex = 0u;
    queues[0].queueIndex = 0u;
    queues[1].queue = reinterpret_cast<VkQueue>(2u);
    queues[1].familyIndex = 0u;
    queues[1].queueIndex = 1u;
    EXPECT_EQ(Policy::validateCanonical(queues, families), Policy::Result::Valid);
    queues[1].queue = queues[0].queue;
    EXPECT_EQ(Policy::validateCanonical(queues, families), Policy::Result::DuplicateQueue);
    queues[1].queue = reinterpret_cast<VkQueue>(2u);
    queues[1].queueIndex = 2u;
    EXPECT_EQ(Policy::validateCanonical(queues, families), Policy::Result::Invalid);
}

TEST(DeviceCreationPolicy, QueuePlanKeepsCrossFamilyOrderAndStablePriorities){
    using Plan = Core::GraphicsBackend::VulkanQueueRequestPlan;
    Core::Alloc::ScratchArena scratch(NWB::Tests::s_TestArena);
    Vector<VkQueueFamilyProperties, Core::Alloc::ScratchArena> families(scratch);
    families.resize(4u);
    families[0].queueCount = 3u;
    families[1].queueCount = 1u;
    families[2].queueCount = 2u;
    families[3].queueCount = 1u;
    NWB::Tests::TestArena<> arena;
    Core::DeviceCreationParameters params(arena.arena);
    params.enableSameClassMultiQueue = true;
    params.headlessDevice = false;
    const Plan::Input input{
        .deviceParams = params,
        .physicalQueueFamilies = families,
        .graphicsQueueFamily = 0,
        .secondaryGraphicsQueueFamily = 1,
        .presentQueueFamily = 1,
        .schedulerComputeQueueFamily = 2,
        .secondaryComputeQueueFamily = -1,
        .transferQueueFamily = 3,
        .secondaryTransferQueueFamily = -1,
        .createCrossFamilySecondaryGraphicsQueue = true,
        .createComputeQueue = true,
        .createCrossFamilySecondaryComputeQueue = false,
        .createDedicatedTransferQueue = true,
        .createCrossFamilySecondaryTransferQueue = false,
    };
    const Plan plan(scratch, input);
    const auto& requests = plan.sameClassQueueRequests();
    ASSERT_EQ(requests.size(), 4u);
    EXPECT_EQ(requests[0].family, 1);
    EXPECT_EQ(requests[0].queueIndex, 0u);
    EXPECT_EQ(requests[1].family, 0);
    EXPECT_EQ(requests[1].queueIndex, 1u);
    EXPECT_EQ(requests[2].family, 0);
    EXPECT_EQ(requests[2].queueIndex, 2u);
    EXPECT_EQ(requests[3].family, 2);
    EXPECT_EQ(requests[3].queueIndex, 1u);
    const auto& queues = plan.queueDescriptions();
    ASSERT_EQ(queues.size(), 4u);
    const u32 expectedCounts[] = { 3u, 1u, 2u, 1u };
    for(u32 i = 0u; i < 4u; ++i){
        EXPECT_EQ(queues[i].queueFamilyIndex, i);
        EXPECT_EQ(queues[i].queueCount, expectedCounts[i]);
        EXPECT_EQ(queues[i].pQueuePriorities, queues[0].pQueuePriorities);
        for(u32 j = 0u; j < queues[i].queueCount; ++j)
            EXPECT_EQ(queues[i].pQueuePriorities[j], 1.0f);
    }
}

TEST(DeviceCreationPolicy, RejectsUnknownHardwareRayTracingPolicy){
    EXPECT_FALSE(Core::IsValidHardwareRayTracingPolicy(static_cast<Core::HardwareRayTracingPolicy::Enum>(255u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

