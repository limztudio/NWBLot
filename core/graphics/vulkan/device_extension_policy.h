// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/graphics/rhi/hardware_ray_tracing_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeviceExtensionFeature{
    enum Enum : u8{
        None = 0,
        AccelerationStructure,
        RayTracingPipeline,
        RayQuery,
        OpacityMicromap,
        ClusterAccelerationStructure,
        RayTracingInvocationReorder,
        RayTracingInvocationReorderExt,
        RayTracingLinearSweptSpheres,
        MeshShader,
        FragmentShadingRate,
        DescriptorBuffer,
        DeviceFault,
        TextureCompressionAstcHdr,
        Count,
    };
};

struct DeviceExtensionEntry{
    const char* name;
    DeviceExtensionFeature::Enum feature = DeviceExtensionFeature::None;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr DeviceExtensionEntry s_RayTracingDeviceExtensions[] = {
    { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::AccelerationStructure },
    { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, DeviceExtensionFeature::None },
    { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME, DeviceExtensionFeature::None },
    { VK_KHR_RAY_QUERY_EXTENSION_NAME, DeviceExtensionFeature::RayQuery },
    { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, DeviceExtensionFeature::RayTracingPipeline },
    { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, DeviceExtensionFeature::OpacityMicromap },
    { VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, DeviceExtensionFeature::RayTracingInvocationReorderExt },
    { VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::ClusterAccelerationStructure },
    { VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, DeviceExtensionFeature::RayTracingInvocationReorder },
    { VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME, DeviceExtensionFeature::RayTracingLinearSweptSpheres },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeviceExtensionRequestAction{
    enum Enum : u8{
        Enable = 0,
        Omit,
        Reject,
    };
};

// A disabled RT policy rejects required RT extensions instead of silently weakening either request.
[[nodiscard]] DeviceExtensionRequestAction::Enum ResolveDeviceExtensionRequest(
    HardwareRayTracingPolicy::Enum policy, AStringView extensionName, bool required)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

