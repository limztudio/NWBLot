// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_TriangleVertexInputName = FOUNDATION_TEXT("triangle vertex");
inline constexpr TStringView s_TriangleIndexInputName = FOUNDATION_TEXT("triangle index");
inline constexpr TStringView s_AabbInputName = FOUNDATION_TEXT("AABB");
inline constexpr TStringView s_SphereVertexInputName = FOUNDATION_TEXT("sphere vertex");
inline constexpr TStringView s_SphereIndexInputName = FOUNDATION_TEXT("sphere index");
inline constexpr TStringView s_LssVertexInputName = FOUNDATION_TEXT("LSS vertex");
inline constexpr TStringView s_LssIndexInputName = FOUNDATION_TEXT("LSS index");
inline constexpr VkBufferUsageFlags s_AccelStructBuildInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
inline constexpr VkBufferUsageFlags s_MicromapStorageUsage = VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
inline constexpr u32 s_MaxPositionTruncateBitCount = 32u;

using BlasGeometryVector = Vector<VkAccelerationStructureGeometryKHR, Alloc::ScratchArena>;
using BlasSpheresDataVector = Vector<VkAccelerationStructureGeometrySpheresDataNV, Alloc::ScratchArena>;
using BlasLssDataVector = Vector<VkAccelerationStructureGeometryLinearSweptSpheresDataNV, Alloc::ScratchArena>;
using BlasRangeInfoVector = Vector<VkAccelerationStructureBuildRangeInfoKHR, Alloc::ScratchArena>;
using BlasPrimitiveCountVector = Vector<uint32_t, Alloc::ScratchArena>;
using BlasTransformOffsetVector = Vector<usize, Alloc::ScratchArena>;
using OpacityMicromapGeometryVector = Vector<VkAccelerationStructureTrianglesOpacityMicromapEXT, Alloc::ScratchArena>;
using OpacityMicromapUsageVector = Vector<VkMicromapUsageEXT, Alloc::ScratchArena>;
using OpacityMicromapUsageOffsetVector = Vector<usize, Alloc::ScratchArena>;

struct BlasGeometry{
    VkAccelerationStructureGeometryKHR geometry = {};
    VkAccelerationStructureGeometrySpheresDataNV spheresData = {};
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV lssData = {};
    u32 primitiveCount = 0u;
};

struct BlasGeometryScratch{
    BlasGeometryVector geometries;
    BlasSpheresDataVector spheresData;
    BlasLssDataVector lssData;
    BlasRangeInfoVector rangeInfos;
    BlasPrimitiveCountVector primitiveCounts;
    BlasTransformOffsetVector transformOffsets;


public:
    explicit BlasGeometryScratch(Alloc::ScratchArena& scratchArena)noexcept
        : geometries(scratchArena)
        , spheresData(scratchArena)
        , lssData(scratchArena)
        , rangeInfos(scratchArena)
        , primitiveCounts(scratchArena)
        , transformOffsets(scratchArena)
    {}


public:
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

    void assignGeometry(const usize index, const BlasGeometry& source)noexcept{
        geometries[index] = source.geometry;
        spheresData[index] = source.spheresData;
        lssData[index] = source.lssData;
        primitiveCounts[index] = source.primitiveCount;
        if(source.geometry.geometryType == VK_GEOMETRY_TYPE_SPHERES_NV)
            geometries[index].pNext = &spheresData[index];
        else if(source.geometry.geometryType == VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV)
            geometries[index].pNext = &lssData[index];
    }
};


struct RayTracingCapabilityInputs{
    b8 accelerationStructureExtensionEnabled = false;
    b8 accelerationStructureFeatureEnabled = false;
    b8 createAccelerationStructureEntryPointAvailable = false;
    b8 destroyAccelerationStructureEntryPointAvailable = false;
    b8 getAccelerationStructureBuildSizesEntryPointAvailable = false;
    b8 getAccelerationStructureDeviceAddressEntryPointAvailable = false;
    b8 cmdBuildAccelerationStructuresEntryPointAvailable = false;

    b8 rayTracingPipelineExtensionEnabled = false;
    b8 rayTracingPipelineFeatureEnabled = false;
    b8 createRayTracingPipelinesEntryPointAvailable = false;
    b8 getRayTracingShaderGroupHandlesEntryPointAvailable = false;
    b8 cmdTraceRaysEntryPointAvailable = false;

    b8 opacityMicromapExtensionEnabled = false;
    b8 opacityMicromapFeatureEnabled = false;
    b8 synchronization2ExtensionEnabled = false;
    b8 createMicromapEntryPointAvailable = false;
    b8 destroyMicromapEntryPointAvailable = false;
    b8 getMicromapBuildSizesEntryPointAvailable = false;
    b8 cmdBuildMicromapsEntryPointAvailable = false;
};

[[nodiscard]] inline constexpr b8 SupportsRayTracingAccelStruct(const RayTracingCapabilityInputs& inputs)noexcept{
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

[[nodiscard]] inline constexpr b8 SupportsRayTracingPipeline(const RayTracingCapabilityInputs& inputs)noexcept{
    return
        inputs.rayTracingPipelineExtensionEnabled
        && inputs.rayTracingPipelineFeatureEnabled
        && inputs.createRayTracingPipelinesEntryPointAvailable
        && inputs.getRayTracingShaderGroupHandlesEntryPointAvailable
        && inputs.cmdTraceRaysEntryPointAvailable
        && SupportsRayTracingAccelStruct(inputs)
    ;
}

[[nodiscard]] inline constexpr b8 SupportsRayTracingOpacityMicromap(const RayTracingCapabilityInputs& inputs)noexcept{
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

[[nodiscard]] inline constexpr b8 IsRayTracingShaderTypeAllowed(const ShaderType::Mask shaderType, const ShaderType::Mask allowedShaderTypes)noexcept{
    const u16 shaderTypeBits = static_cast<u16>(shaderType);
    return HasSingleSetBit(shaderTypeBits) && (shaderTypeBits & static_cast<u16>(allowedShaderTypes)) == shaderTypeBits;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayDispatchLimits{
    u64 maxInvocationCount = 0u;
    Array<u32, 3u> maxAxisCounts = {};
    Array<u32, 3u> maxAxisSizes = {};
};

[[nodiscard]] VkPipelineCreateFlags2 ComputeRayTracingPipelineCreateFlags(const RayTracingPipelineDesc& desc)noexcept;

[[nodiscard]] b8 ValidateRayDispatchDimensions(const RayTracingDispatchRaysArguments& arguments, const RayDispatchLimits& limits)noexcept;

struct RayTracingHandleLayout{
    u32 handleSize = 0u;
    u32 handleSizeAligned = 0u;
    u32 baseAlignment = 0u;
};

Expected<RayTracingHandleLayout> ComputeRayTracingHandleLayout(const VulkanContext& context, TStringView operation);

Expected<u64> ComputeShaderTableByteSize(u32 recordCount, u32 handleSizeAligned, TStringView operation);

[[nodiscard]] Expected<u64> ComputeShaderTableAllocationByteSize(u64 recordByteSize, u32 baseAlignment)noexcept;

[[nodiscard]] Expected<u64> ComputeShaderTableAlignedOffset(u64 deviceAddress, u64 allocationByteSize, u64 recordByteSize, u32 baseAlignment)noexcept;

Expected<BlasGeometry> FillBlasGeometryForSizeQuery(const VulkanContext& context, const RayTracingGeometryDesc& geomDesc, TStringView operation, b8 requireBuffers);
[[nodiscard]] VkMemoryBarrier2 BuildOpacityMicromapWriteAfterWriteBarrier()noexcept;

inline constexpr TStringView s_RetainBuildSignatureFailedReason = FOUNDATION_TEXT("failed to retain acceleration-structure build signature");

[[nodiscard]] AccelStructGeometryBuildSignature MakeAccelStructGeometryBuildSignature(
    const VkAccelerationStructureGeometryKHR& geometry,
    const VkAccelerationStructureGeometrySpheresDataNV* spheresData,
    const VkAccelerationStructureGeometryLinearSweptSpheresDataNV* lssData,
    u32 primitiveCount,
    b8 transformDataPresent
)noexcept;
[[nodiscard]] b8 AccelStructGeometryBuildSignaturesEqual(const AccelStructGeometryBuildSignature& previous, const AccelStructGeometryBuildSignature& current)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

