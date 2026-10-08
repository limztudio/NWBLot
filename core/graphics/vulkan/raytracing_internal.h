// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using BlasGeometryVector = Vector<VkAccelerationStructureGeometryKHR, Alloc::ScratchArena>;
using BlasSpheresDataVector = Vector<VkAccelerationStructureGeometrySpheresDataNV, Alloc::ScratchArena>;
using BlasLssDataVector = Vector<VkAccelerationStructureGeometryLinearSweptSpheresDataNV, Alloc::ScratchArena>;
using BlasRangeInfoVector = Vector<VkAccelerationStructureBuildRangeInfoKHR, Alloc::ScratchArena>;
using BlasPrimitiveCountVector = Vector<uint32_t, Alloc::ScratchArena>;
using BlasTransformOffsetVector = Vector<usize, Alloc::ScratchArena>;

struct BlasGeometryScratch{
    BlasGeometryVector geometries;
    BlasSpheresDataVector spheresData;
    BlasLssDataVector lssData;
    BlasRangeInfoVector rangeInfos;
    BlasPrimitiveCountVector primitiveCounts;
    BlasTransformOffsetVector transformOffsets;

    explicit BlasGeometryScratch(Alloc::ScratchArena& scratchArena)
        : geometries(scratchArena)
        , spheresData(scratchArena)
        , lssData(scratchArena)
        , rangeInfos(scratchArena)
        , primitiveCounts(scratchArena)
        , transformOffsets(scratchArena)
    {}

    void resizeForSizeQuery(usize geometryCount){
        geometries.resize(geometryCount);
        spheresData.resize(geometryCount);
        lssData.resize(geometryCount);
        primitiveCounts.resize(geometryCount);
    }

    void resizeForBuild(usize geometryCount){
        geometries.resize(geometryCount);
        spheresData.resize(geometryCount);
        lssData.resize(geometryCount);
        rangeInfos.resize(geometryCount);
        primitiveCounts.resize(geometryCount);
        transformOffsets.resize(geometryCount);
    }
};


struct RayTracingCapabilityInputs{
    bool accelerationStructureExtensionEnabled = false;
    bool accelerationStructureFeatureEnabled = false;
    bool createAccelerationStructureEntryPointAvailable = false;
    bool destroyAccelerationStructureEntryPointAvailable = false;
    bool getAccelerationStructureBuildSizesEntryPointAvailable = false;
    bool getAccelerationStructureDeviceAddressEntryPointAvailable = false;
    bool cmdBuildAccelerationStructuresEntryPointAvailable = false;

    bool rayTracingPipelineExtensionEnabled = false;
    bool rayTracingPipelineFeatureEnabled = false;
    bool createRayTracingPipelinesEntryPointAvailable = false;
    bool getRayTracingShaderGroupHandlesEntryPointAvailable = false;
    bool cmdTraceRaysEntryPointAvailable = false;

    bool opacityMicromapExtensionEnabled = false;
    bool opacityMicromapFeatureEnabled = false;
    bool synchronization2ExtensionEnabled = false;
    bool createMicromapEntryPointAvailable = false;
    bool destroyMicromapEntryPointAvailable = false;
    bool getMicromapBuildSizesEntryPointAvailable = false;
    bool cmdBuildMicromapsEntryPointAvailable = false;
};

[[nodiscard]] inline constexpr bool SupportsRayTracingAccelStruct(const RayTracingCapabilityInputs& inputs)noexcept{
    return
        inputs.accelerationStructureExtensionEnabled
        && inputs.accelerationStructureFeatureEnabled
        && inputs.createAccelerationStructureEntryPointAvailable
        && inputs.destroyAccelerationStructureEntryPointAvailable
        && inputs.getAccelerationStructureBuildSizesEntryPointAvailable
        && inputs.getAccelerationStructureDeviceAddressEntryPointAvailable
        && inputs.cmdBuildAccelerationStructuresEntryPointAvailable
    ;
}

[[nodiscard]] inline constexpr bool SupportsRayTracingPipeline(const RayTracingCapabilityInputs& inputs)noexcept{
    return
        inputs.rayTracingPipelineExtensionEnabled
        && inputs.rayTracingPipelineFeatureEnabled
        && inputs.createRayTracingPipelinesEntryPointAvailable
        && inputs.getRayTracingShaderGroupHandlesEntryPointAvailable
        && inputs.cmdTraceRaysEntryPointAvailable
        && SupportsRayTracingAccelStruct(inputs)
    ;
}

[[nodiscard]] inline constexpr bool SupportsRayTracingOpacityMicromap(const RayTracingCapabilityInputs& inputs)noexcept{
    return
        inputs.opacityMicromapExtensionEnabled
        && inputs.opacityMicromapFeatureEnabled
        && inputs.synchronization2ExtensionEnabled
        && inputs.createMicromapEntryPointAvailable
        && inputs.destroyMicromapEntryPointAvailable
        && inputs.getMicromapBuildSizesEntryPointAvailable
        && inputs.cmdBuildMicromapsEntryPointAvailable
        && SupportsRayTracingAccelStruct(inputs)
    ;
}

[[nodiscard]] inline constexpr bool IsRayTracingShaderTypeAllowed(
    const ShaderType::Mask shaderType,
    const ShaderType::Mask allowedShaderTypes
)noexcept{
    const u16 shaderTypeBits = static_cast<u16>(shaderType);
    return
        shaderTypeBits != 0u
        && (shaderTypeBits & (shaderTypeBits - 1u)) == 0u
        && (shaderTypeBits & static_cast<u16>(allowedShaderTypes)) == shaderTypeBits
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayDispatchLimits{
    u64 maxInvocationCount = 0u;
    Array<u32, 3u> maxAxisCounts = {};
    Array<u32, 3u> maxAxisSizes = {};
};

[[nodiscard]] VkPipelineCreateFlags2 ComputeRayTracingPipelineCreateFlags(const RayTracingPipelineDesc& desc)noexcept;

[[nodiscard]] bool ValidateRayDispatchDimensions(
    const RayTracingDispatchRaysArguments& arguments,
    const RayDispatchLimits& limits
)noexcept;

struct RayTracingHandleLayout{
    u32 handleSize = 0u;
    u32 handleSizeAligned = 0u;
    u32 baseAlignment = 0u;
};

Expected<RayTracingHandleLayout> ComputeRayTracingHandleLayout(
    const VulkanContext& context,
    TStringView operation
);

Expected<u64> ComputeShaderTableByteSize(
    u32 recordCount,
    u32 handleSizeAligned,
    TStringView operation
);

[[nodiscard]] Expected<u64> ComputeShaderTableAllocationByteSize(
    u64 recordByteSize,
    u32 baseAlignment
)noexcept;

[[nodiscard]] Expected<u64> ComputeShaderTableAlignedOffset(
    u64 deviceAddress,
    u64 allocationByteSize,
    u64 recordByteSize,
    u32 baseAlignment
)noexcept;

struct BlasGeometry{
    VkAccelerationStructureGeometryKHR geometry = {};
    VkAccelerationStructureGeometrySpheresDataNV spheresData = {};
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV lssData = {};
    u32 primitiveCount = 0u;
};

Expected<BlasGeometry> FillBlasGeometryForSizeQuery(
    const VulkanContext& context,
    const RayTracingGeometryDesc& geomDesc,
    TStringView operation,
    bool requireBuffers
);

[[nodiscard]] VkMemoryBarrier2 BuildOpacityMicromapWriteAfterWriteBarrier()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

