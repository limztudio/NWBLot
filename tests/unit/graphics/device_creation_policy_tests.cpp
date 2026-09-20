// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/alloc/general.h>
#include <core/graphics/rhi/device.h>
#include <core/graphics/vulkan/device_extension_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_creation_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend = Core::GraphicsBackend;

inline constexpr Name s_DeviceCreationPolicyTestArena("tests/graphics/device_creation_policy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(DeviceCreationPolicy, UsesConservativeNativeMeshShaderDefaultOnWindowsArm64){
    Core::Alloc::GlobalArena arena(s_DeviceCreationPolicyTestArena);
    Core::DeviceCreationParameters parameters(arena);

#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    EXPECT_FALSE(parameters.enableNativeMeshShaders);
#else
    EXPECT_TRUE(parameters.enableNativeMeshShaders);
#endif
}

TEST(DeviceCreationPolicy, LowLevelDevicesRequireExplicitAutomaticHardwareRayTracing){
    Core::Alloc::GlobalArena arena(s_DeviceCreationPolicyTestArena);
    Core::DeviceCreationParameters parameters(arena);

    EXPECT_EQ(parameters.hardwareRayTracingPolicy, Core::HardwareRayTracingPolicy::Disabled);
    EXPECT_TRUE(Core::IsValidHardwareRayTracingPolicy(Core::HardwareRayTracingPolicy::Automatic));
    EXPECT_TRUE(Core::IsValidHardwareRayTracingPolicy(Core::HardwareRayTracingPolicy::Disabled));
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

TEST(DeviceCreationPolicy, AutomaticHardwareRayTracingAllowsCanonicalExtensionRequests){
    for(const GraphicsBackend::DeviceExtensionEntry& entry : GraphicsBackend::s_RayTracingDeviceExtensions){
        const auto optional = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Automatic, entry.name, false);
        const auto required = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Automatic, entry.name, true);
        EXPECT_EQ(optional, GraphicsBackend::DeviceExtensionRequestAction::Enable);
        EXPECT_EQ(required, GraphicsBackend::DeviceExtensionRequestAction::Enable);
    }
}

TEST(DeviceCreationPolicy, DisablingHardwareRayTracingPreservesRequiredGraphicsCapabilities){
    constexpr AStringView graphicsExtensions[] = {
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    for(const AStringView extension : graphicsExtensions){
        const auto optional = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Disabled, extension, false);
        const auto required = GraphicsBackend::ResolveDeviceExtensionRequest(Core::HardwareRayTracingPolicy::Disabled, extension, true);
        EXPECT_EQ(optional, GraphicsBackend::DeviceExtensionRequestAction::Enable);
        EXPECT_EQ(required, GraphicsBackend::DeviceExtensionRequestAction::Enable);
    }
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

