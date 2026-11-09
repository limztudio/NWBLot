// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "blas_opacity_micromap_policy.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanDetail{


b8 BlasOpacityMicromapPolicy::PrepareBlasOpacityMicromapUsageOffsets(
    const VulkanContext& context,
    const RayTracingGeometryDesc* const geometries,
    const usize geometryCount,
    const TStringView operation,
    OpacityMicromapUsageOffsetVector& outUsageOffsets,
    OpacityMicromapUsageVector& outUsageCounts,
    b8& outHasOpacityMicromap
){
    usize totalOpacityMicromapUsageCount = 0u;
    outHasOpacityMicromap = false;
    for(usize i = 0u; i < geometryCount; ++i){
        outUsageOffsets[i] = Limit<usize>::s_Max;
        if(geometries[i].geometryType != RayTracingGeometryType::Triangles)
            continue;

        const RayTracingGeometryTriangles& triangles = geometries[i].geometryData.triangles;
        if(!triangles.opacityMicromap){
            if(triangles.ommIndexBuffer || triangles.ommIndexBufferOffset != 0u || triangles.ommUsageCounts || triangles.numOmmUsageCounts != 0u || triangles.ommIndexFormat != Format::UNKNOWN){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM metadata requires an opacity micromap"), operation);
                return false;
            }
            continue;
        }
        if(!context.extensions.extOpacityMicromap || !context.opacityMicromapFeatureEnabled){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM geometry requires VK_EXT_opacity_micromap"), operation);
            return false;
        }
        if(triangles.numOmmUsageCounts != 0u && !triangles.ommUsageCounts){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM usage counts are null"), operation);
            return false;
        }
        if(totalOpacityMicromapUsageCount > Limit<usize>::s_Max - triangles.numOmmUsageCounts){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM usage-count storage overflows"), operation);
            return false;
        }

        outHasOpacityMicromap = true;
        outUsageOffsets[i] = totalOpacityMicromapUsageCount;
        totalOpacityMicromapUsageCount += triangles.numOmmUsageCounts;
    }
    outUsageCounts.resize(totalOpacityMicromapUsageCount);
    return true;
}

Expected<OpacityMicromapIndexFormatInfo> BlasOpacityMicromapPolicy::ResolveOpacityMicromapIndexFormat(const Format::Enum format, const TStringView operation){
    if(format == Format::UNKNOWN)
        return OpacityMicromapIndexFormatInfo{};
    if(format == Format::R16_UINT)
        return OpacityMicromapIndexFormatInfo{ VK_INDEX_TYPE_UINT16, sizeof(u16) };
    if(format == Format::R32_UINT)
        return OpacityMicromapIndexFormatInfo{ VK_INDEX_TYPE_UINT32, sizeof(u32) };

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM index format must be UNKNOWN, R16_UINT, or R32_UINT"), operation);
    return MakeUnexpected(Failure{});
}

b8 BlasOpacityMicromapPolicy::ValidateOpacityMicromapStorageAndFillUsages(
    const RayTracingGeometryTriangles& triangles,
    const VkIndexType ommIndexType,
    const u64 ommIndexStride,
    const VkDeviceAddress ommIndexAddress,
    const usize usageOffset,
    const TStringView operation,
    OpacityMicromapUsageVector& inOutUsageCounts,
    VkAccelerationStructureTrianglesOpacityMicromapEXT& outOmmGeometry
){
    OpacityMicromap* const opacityMicromap = triangles.opacityMicromap;
    Buffer* const micromapStorage = opacityMicromap->getDataBuffer();
    const BufferDesc& micromapStorageDesc = micromapStorage->getCreationDescription();
    if(!micromapStorageDesc.isAccelStructStorage || opacityMicromap->getMicromap() == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle opacity micromap is invalid"), operation);
        return false;
    }

    for(u32 usageIndex = 0u; usageIndex < triangles.numOmmUsageCounts; ++usageIndex){
        const RayTracingOpacityMicromapUsageCount& usage = triangles.ommUsageCounts[usageIndex];
        VkOpacityMicromapFormatEXT format = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_KHR;
        if(usage.format == OpacityMicromapFormat::OC1_2_State)
            format = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
        else if(usage.format == OpacityMicromapFormat::OC1_4_State)
            format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
        else{
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM usage count has an invalid format"), operation);
            return false;
        }
        const u32 maxSubdivisionLevel = format == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? opacityMicromap->getMaxOpacity2StateSubdivisionLevel() : opacityMicromap->getMaxOpacity4StateSubdivisionLevel();
        if(usage.subdivisionLevel > maxSubdivisionLevel){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: triangle OMM subdivision level {} exceeds device limit {}"), operation, usage.subdivisionLevel, maxSubdivisionLevel);
            return false;
        }

        VkMicromapUsageEXT& vkUsage = inOutUsageCounts[usageOffset + usageIndex];
        vkUsage.count = usage.count;
        vkUsage.subdivisionLevel = usage.subdivisionLevel;
        vkUsage.format = format;
    }

    outOmmGeometry = MakeVkStruct<VkAccelerationStructureTrianglesOpacityMicromapEXT>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT);
    outOmmGeometry.indexType = ommIndexType;
    outOmmGeometry.indexBuffer.deviceAddress = ommIndexAddress;
    outOmmGeometry.indexStride = ommIndexStride;
    outOmmGeometry.baseTriangle = 0u;
    outOmmGeometry.usageCountsCount = triangles.numOmmUsageCounts;
    outOmmGeometry.micromap = opacityMicromap->getMicromap();
    return true;
}

void BlasOpacityMicromapPolicy::AttachBlasOpacityMicromapGeometries(
    const RayTracingGeometryDesc* const geometries,
    const usize geometryCount,
    const OpacityMicromapUsageOffsetVector& opacityMicromapUsageOffsets,
    const OpacityMicromapUsageVector& opacityMicromapUsageCounts,
    OpacityMicromapGeometryVector& inOutOpacityMicromapGeometries,
    BlasGeometryScratch& inOutBlasScratch
)noexcept{
    for(usize i = 0u; i < geometryCount; ++i){
        if(opacityMicromapUsageOffsets[i] == Limit<usize>::s_Max)
            continue;

        VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = inOutOpacityMicromapGeometries[i];
        const u32 usageCount = geometries[i].geometryData.triangles.numOmmUsageCounts;
        ommGeometry.pUsageCounts = usageCount != 0u ? opacityMicromapUsageCounts.data() + opacityMicromapUsageOffsets[i] : nullptr;
        inOutBlasScratch.geometries[i].geometry.triangles.pNext = &ommGeometry;
    }
}


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
