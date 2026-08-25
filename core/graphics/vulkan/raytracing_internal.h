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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeRayTracingHandleLayout(
    const VulkanContext& context,
    u32& outHandleSize,
    u32& outHandleSizeAligned,
    u32& outBaseAlignment,
    const tchar* operation
);

bool ComputeShaderTableByteSize(
    u32 recordCount,
    u32 handleSizeAligned,
    u64& outByteSize,
    const tchar* operation
);

[[nodiscard]] bool ComputeShaderTableAllocationByteSize(
    u64 recordByteSize,
    u32 baseAlignment,
    u64& outAllocationByteSize
)noexcept;

[[nodiscard]] bool ComputeShaderTableAlignedOffset(
    u64 deviceAddress,
    u64 allocationByteSize,
    u64 recordByteSize,
    u32 baseAlignment,
    u64& outOffset
)noexcept;

bool FillBlasGeometryForSizeQuery(
    const VulkanContext& context,
    const RayTracingGeometryDesc& geomDesc,
    VkAccelerationStructureGeometryKHR& geometry,
    VkAccelerationStructureGeometrySpheresDataNV& spheresData,
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV& lssData,
    u32& primitiveCount,
    const tchar* operation,
    bool requireBuffers
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

