// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "raytracing_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanDetail{


struct OpacityMicromapIndexFormatInfo{
    VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    u64 indexStride = 0u;
};

class BlasOpacityMicromapPolicy{
public:
    [[nodiscard]] static b8 PrepareBlasOpacityMicromapUsageOffsets(
        const VulkanContext& context,
        const RayTracingGeometryDesc* geometries,
        usize geometryCount,
        TStringView operation,
        OpacityMicromapUsageOffsetVector& outUsageOffsets,
        OpacityMicromapUsageVector& outUsageCounts,
        b8& outHasOpacityMicromap
    );
    [[nodiscard]] static Expected<OpacityMicromapIndexFormatInfo> ResolveOpacityMicromapIndexFormat(Format::Enum format, TStringView operation);
    [[nodiscard]] static b8 ValidateOpacityMicromapStorageAndFillUsages(
        const RayTracingGeometryTriangles& triangles,
        VkIndexType ommIndexType,
        u64 ommIndexStride,
        VkDeviceAddress ommIndexAddress,
        usize usageOffset,
        TStringView operation,
        OpacityMicromapUsageVector& inOutUsageCounts,
        VkAccelerationStructureTrianglesOpacityMicromapEXT& outOmmGeometry
    );
    static void AttachBlasOpacityMicromapGeometries(
        const RayTracingGeometryDesc* geometries,
        usize geometryCount,
        const OpacityMicromapUsageOffsetVector& opacityMicromapUsageOffsets,
        const OpacityMicromapUsageVector& opacityMicromapUsageCounts,
        OpacityMicromapGeometryVector& inOutOpacityMicromapGeometries,
        BlasGeometryScratch& inOutBlasScratch
    )noexcept;

};


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

