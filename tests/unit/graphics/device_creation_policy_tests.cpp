// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/rhi/device.h>
#include <core/graphics/backend_selection/test/device_extension_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_creation_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend = Core::GraphicsBackend;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(DeviceCreationPolicy, RejectsUnknownHardwareRayTracingPolicy){
    EXPECT_FALSE(Core::IsValidHardwareRayTracingPolicy(static_cast<Core::HardwareRayTracingPolicy::Enum>(255u)));
}

TEST(DeviceCreationPolicy, DisabledHardwareRayTracingCannotBeOverriddenByExplicitExtensionRequests){
    constexpr AStringView rayTracingExtensions[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME,
        VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME,
        VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME,
        VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME,
    };
    for(const AStringView extension : rayTracingExtensions){
        const auto optional = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Disabled, extension, false);
        const auto required = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Disabled, extension, true);
        EXPECT_EQ(optional, GraphicsBackend::DeviceExtensionRequestAction::Omit);
        EXPECT_EQ(required, GraphicsBackend::DeviceExtensionRequestAction::Reject);
    }
}

TEST(DeviceCreationPolicy, UnknownPolicyRejectsOptionalAndRequiredExtensions){
    constexpr auto invalidPolicy = static_cast<Core::HardwareRayTracingPolicy::Enum>(255u);
    const auto optional = GraphicsBackend::ResolveDeviceExtensionRequest(invalidPolicy, VK_KHR_RAY_QUERY_EXTENSION_NAME, false);
    const auto required = GraphicsBackend::ResolveDeviceExtensionRequest(invalidPolicy, VK_KHR_SWAPCHAIN_EXTENSION_NAME, true);
    EXPECT_EQ(optional, GraphicsBackend::DeviceExtensionRequestAction::Reject);
    EXPECT_EQ(required, GraphicsBackend::DeviceExtensionRequestAction::Reject);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

