// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(AffineTransform) == sizeof(VkTransformMatrixKHR), "AffineTransform must match VkTransformMatrixKHR");

inline constexpr u32 s_LssVerticesPerPrimitive = 2u;
inline constexpr u32 s_LssListIndicesPerPrimitive = 2u;
inline constexpr u32 s_LssSuccessiveIndicesPerPrimitive = 1u;


VkDeviceAddress GetBufferDeviceAddress(Buffer* bufferResource, u64 offset){
    if(!bufferResource)
        return 0;

    const VkDeviceAddress baseAddress = bufferResource->getGpuVirtualAddress();
    if(baseAddress == 0u || baseAddress > Limit<u64>::s_Max - offset)
        return 0u;

    return baseAddress + offset;
}

bool GetRayTracingIndexType(Format::Enum format, VkIndexType& indexType){
    if(format == Format::R16_UINT){
        indexType = VK_INDEX_TYPE_UINT16;
        return true;
    }
    if(format == Format::R32_UINT){
        indexType = VK_INDEX_TYPE_UINT32;
        return true;
    }

    return false;
}

u64 GetRayTracingIndexElementSize(Format::Enum format){
    if(format == Format::R16_UINT)
        return sizeof(u16);
    if(format == Format::R32_UINT)
        return sizeof(u32);
    return 0;
}

VkRayTracingLssIndexingModeNV ConvertRayTracingLssIndexingMode(RayTracingGeometryLssPrimitiveFormat::Enum format){
    switch(format){
    case RayTracingGeometryLssPrimitiveFormat::List:
        return VK_RAY_TRACING_LSS_INDEXING_MODE_LIST_NV;
    case RayTracingGeometryLssPrimitiveFormat::SuccessiveImplicit:
        return VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
    default:
        return VK_RAY_TRACING_LSS_INDEXING_MODE_MAX_ENUM_NV;
    }
}

VkRayTracingLssPrimitiveEndCapsModeNV ConvertRayTracingLssEndcapMode(RayTracingGeometryLssEndcapMode::Enum mode){
    switch(mode){
    case RayTracingGeometryLssEndcapMode::None:
        return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_NONE_NV;
    case RayTracingGeometryLssEndcapMode::Chained:
        return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
    default:
        return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_MAX_ENUM_NV;
    }
}

bool ComputeStridedRangeByteSize(u32 elementCount, u64 stride, u64 elementSize, u64& outByteSize){
    if(elementCount == 0){
        outByteSize = 0;
        return true;
    }

    const u64 spanCount = static_cast<u64>(elementCount - 1);
    if(stride != 0 && spanCount > (UINT64_MAX - elementSize) / stride)
        return false;

    outByteSize = spanCount * stride + elementSize;
    return true;
}
u64 GetRayTracingVertexComponentAlignment(const FormatInfo& formatInfo){
    const u32 componentCount = static_cast<u32>(formatInfo.hasRed)
        + static_cast<u32>(formatInfo.hasGreen)
        + static_cast<u32>(formatInfo.hasBlue)
        + static_cast<u32>(formatInfo.hasAlpha)
    ;
    if(componentCount == 0u || formatInfo.bytesPerBlock == 0u)
        return 0u;

    return Max<u64>(static_cast<u64>(formatInfo.bytesPerBlock) / componentCount, 1u);
}
bool ValidateAccelStructBuildInputRange(
    Buffer* bufferResource,
    u64 offset,
    u64 byteSize,
    u64 requiredAddressAlignment,
    const tchar* operation,
    const tchar* resourceName
){
    auto* buffer = bufferResource;
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer is invalid"), operation, resourceName);
        return false;
    }

    const BufferDesc& desc = buffer->getCreationDescription();
    if(!desc.isAccelStructBuildInput){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer was not created with acceleration-structure build input usage"), operation, resourceName);
        return false;
    }
    const u64 validatedByteSize = byteSize != 0u ? byteSize : 1u;
    if(!IsBufferRangeInBounds(desc, offset, validatedByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer range is outside the buffer"), operation, resourceName);
        return false;
    }

    const VkDeviceAddress address = GetBufferDeviceAddress(buffer, offset);
    if(address == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer device address is null or overflows"), operation, resourceName);
        return false;
    }
    if(requiredAddressAlignment > 1u && (address % requiredAddressAlignment) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to {}: {} buffer device address is not {}-byte aligned")
            , operation
            , resourceName
            , requiredAddressAlignment
        );
        return false;
    }

    return true;
}

bool ValidateStridedBuildInputRange(
    Buffer* buffer,
    u64 offset,
    u32 elementCount,
    u64 stride,
    u64 elementSize,
    const tchar* operation,
    const tchar* resourceName
){
    u64 byteSize = 0;
    if(!ComputeStridedRangeByteSize(elementCount, stride, elementSize, byteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer range overflows"), operation, resourceName);
        return false;
    }

    return ValidateAccelStructBuildInputRange(buffer, offset, byteSize, 1u, operation, resourceName);
}

bool FillBlasGeometryForSizeQuery(
    const VulkanContext& context,
    const RayTracingGeometryDesc& geomDesc,
    VkAccelerationStructureGeometryKHR& geometry,
    VkAccelerationStructureGeometrySpheresDataNV& spheresData,
    VkAccelerationStructureGeometryLinearSweptSpheresDataNV& lssData,
    u32& primitiveCount,
    const tchar* operation,
    bool requireBuffers
){
    geometry = MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
    spheresData = MakeVkStruct<VkAccelerationStructureGeometrySpheresDataNV>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_SPHERES_DATA_NV);
    lssData = MakeVkStruct<VkAccelerationStructureGeometryLinearSweptSpheresDataNV>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_LINEAR_SWEPT_SPHERES_DATA_NV);
    primitiveCount = 0;

    if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
        const auto& triangles = geomDesc.geometryData.triangles;
        const VkFormat vertexFormat = ConvertFormat(triangles.vertexFormat);
        if(vertexFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex format is invalid"), operation);
            return false;
        }
        if(requireBuffers && !triangles.vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex buffer is null"), operation);
            return false;
        }
        if(triangles.vertexCount > 0 && triangles.vertexStride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex stride is zero"), operation);
            return false;
        }
        const FormatInfo& vertexFormatInfo = GetFormatInfo(triangles.vertexFormat);
        if(vertexFormatInfo.bytesPerBlock == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex format size is invalid"), operation);
            return false;
        }
        const u64 vertexComponentAlignment = GetRayTracingVertexComponentAlignment(vertexFormatInfo);
        if(vertexComponentAlignment == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex component alignment is invalid"), operation);
            return false;
        }
        if(triangles.vertexCount > 0 && triangles.vertexStride < vertexFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex stride is smaller than the vertex format size"), operation);
            return false;
        }
        if(triangles.vertexStride > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex stride exceeds the Vulkan limit"), operation);
            return false;
        }
        if(triangles.vertexCount > 0u && (triangles.vertexStride % vertexComponentAlignment) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex stride is not a multiple of the vertex component size"), operation);
            return false;
        }
        if(requireBuffers){
            u64 vertexByteSize = 0;
            if(!ComputeStridedRangeByteSize(triangles.vertexCount, triangles.vertexStride, vertexFormatInfo.bytesPerBlock, vertexByteSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex buffer range overflows"), operation);
                return false;
            }
            if(!ValidateAccelStructBuildInputRange(
                triangles.vertexBuffer,
                triangles.vertexOffset,
                vertexByteSize,
                vertexComponentAlignment,
                operation,
                NWB_TEXT("triangle vertex")
            ))
                return false;
        }

        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = vertexFormat;
        geometry.geometry.triangles.vertexStride = triangles.vertexStride;
        geometry.geometry.triangles.maxVertex = triangles.vertexCount > 0 ? triangles.vertexCount - 1 : 0;

        if(triangles.indexBuffer){
            if(!GetRayTracingIndexType(triangles.indexFormat, geometry.geometry.triangles.indexType)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle index format must be R16_UINT or R32_UINT"), operation);
                return false;
            }
            if(requireBuffers){
                const u64 indexElementSize = triangles.indexFormat == Format::R16_UINT ? sizeof(u16) : sizeof(u32);
                const u64 indexByteSize = static_cast<u64>(triangles.indexCount) * indexElementSize;
                if(!ValidateAccelStructBuildInputRange(
                    triangles.indexBuffer,
                    triangles.indexOffset,
                    indexByteSize,
                    indexElementSize,
                    operation,
                    NWB_TEXT("triangle index")
                ))
                    return false;
            }
            primitiveCount = triangles.indexCount / s_TrianglesPerPrimitive;
        }
        else{
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
            primitiveCount = triangles.vertexCount / s_TrianglesPerPrimitive;
        }
    }
    else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
        const auto& aabbs = geomDesc.geometryData.aabbs;
        if(requireBuffers && !aabbs.buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: AABB buffer is null"), operation);
            return false;
        }
        if(aabbs.count > 0 && aabbs.stride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: AABB stride is zero"), operation);
            return false;
        }
        if(aabbs.count > 0u && (aabbs.stride % 8u) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: AABB stride is not a multiple of 8 bytes"), operation);
            return false;
        }
        if(requireBuffers){
            u64 aabbByteSize = 0;
            if(!ComputeStridedRangeByteSize(aabbs.count, aabbs.stride, sizeof(RayTracingGeometryAABB), aabbByteSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: AABB buffer range overflows"), operation);
                return false;
            }
            if(!ValidateAccelStructBuildInputRange(aabbs.buffer, aabbs.offset, aabbByteSize, 8u, operation, NWB_TEXT("AABB")))
                return false;
        }

        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.stride = aabbs.stride;
        primitiveCount = aabbs.count;
    }
    else if(geomDesc.geometryType == RayTracingGeometryType::Spheres){
        if(
            !context.extensions.NV_ray_tracing_linear_swept_spheres
            || context.rayTracingLinearSweptSpheresFeatures.spheres != VK_TRUE
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere geometry requires VK_NV_ray_tracing_linear_swept_spheres with spheres support"), operation);
            return false;
        }

        const auto& spheres = geomDesc.geometryData.spheres;
        const VkFormat vertexFormat = ConvertFormat(spheres.vertexPositionFormat);
        const VkFormat radiusFormat = ConvertFormat(spheres.vertexRadiusFormat);
        if(vertexFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere position format is invalid"), operation);
            return false;
        }
        if(radiusFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere radius format is invalid"), operation);
            return false;
        }
        if(requireBuffers && !spheres.vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere vertex buffer is null"), operation);
            return false;
        }
        if(spheres.vertexCount > 0 && spheres.vertexPositionStride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere position stride is zero"), operation);
            return false;
        }
        if(spheres.vertexCount > 0 && spheres.vertexRadiusStride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere radius stride is zero"), operation);
            return false;
        }

        const FormatInfo& vertexFormatInfo = GetFormatInfo(spheres.vertexPositionFormat);
        const FormatInfo& radiusFormatInfo = GetFormatInfo(spheres.vertexRadiusFormat);
        if(vertexFormatInfo.bytesPerBlock == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere position format size is invalid"), operation);
            return false;
        }
        if(radiusFormatInfo.bytesPerBlock == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere radius format size is invalid"), operation);
            return false;
        }
        if(spheres.vertexCount > 0 && spheres.vertexPositionStride < vertexFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere position stride is smaller than the position format size"), operation);
            return false;
        }
        if(spheres.vertexCount > 0 && spheres.vertexRadiusStride < radiusFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere radius stride is smaller than the radius format size"), operation);
            return false;
        }
        if(requireBuffers){
            if(!ValidateStridedBuildInputRange(
                spheres.vertexBuffer,
                spheres.vertexPositionOffset,
                spheres.vertexCount,
                spheres.vertexPositionStride,
                vertexFormatInfo.bytesPerBlock,
                operation,
                NWB_TEXT("sphere position")
            ))
                return false;
            if(!ValidateStridedBuildInputRange(
                spheres.vertexBuffer,
                spheres.vertexRadiusOffset,
                spheres.vertexCount,
                spheres.vertexRadiusStride,
                radiusFormatInfo.bytesPerBlock,
                operation,
                NWB_TEXT("sphere radius")
            ))
                return false;
        }

        spheresData.indexType = VK_INDEX_TYPE_NONE_KHR;
        if(spheres.indexBuffer){
            if(!GetRayTracingIndexType(spheres.indexFormat, spheresData.indexType)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere index format must be R16_UINT or R32_UINT"), operation);
                return false;
            }
            const u64 indexElementSize = GetRayTracingIndexElementSize(spheres.indexFormat);
            if(spheres.indexCount > 0 && spheres.indexStride == 0){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere index stride is zero"), operation);
                return false;
            }
            if(spheres.indexCount > 0 && spheres.indexStride < indexElementSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: sphere index stride is smaller than the index format size"), operation);
                return false;
            }
            if(requireBuffers){
                if(!ValidateStridedBuildInputRange(
                    spheres.indexBuffer,
                    spheres.indexOffset,
                    spheres.indexCount,
                    spheres.indexStride,
                    indexElementSize,
                    operation,
                    NWB_TEXT("sphere index")
                ))
                    return false;
            }
            primitiveCount = spheres.indexCount;
        }
        else
            primitiveCount = spheres.vertexCount;

        geometry.geometryType = VK_GEOMETRY_TYPE_SPHERES_NV;
        geometry.pNext = &spheresData;
        spheresData.vertexFormat = vertexFormat;
        spheresData.vertexStride = spheres.vertexPositionStride;
        spheresData.radiusFormat = radiusFormat;
        spheresData.radiusStride = spheres.vertexRadiusStride;
        spheresData.indexStride = spheres.indexBuffer ? spheres.indexStride : 0;
    }
    else if(geomDesc.geometryType == RayTracingGeometryType::Lss){
        if(
            !context.extensions.NV_ray_tracing_linear_swept_spheres
            || context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres != VK_TRUE
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS geometry requires VK_NV_ray_tracing_linear_swept_spheres with linearSweptSpheres support"), operation);
            return false;
        }

        const auto& lss = geomDesc.geometryData.lss;
        const VkFormat vertexFormat = ConvertFormat(lss.vertexPositionFormat);
        const VkFormat radiusFormat = ConvertFormat(lss.vertexRadiusFormat);
        if(vertexFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS position format is invalid"), operation);
            return false;
        }
        if(radiusFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS radius format is invalid"), operation);
            return false;
        }
        if(requireBuffers && !lss.vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS vertex buffer is null"), operation);
            return false;
        }
        if(lss.vertexCount > 0 && lss.vertexPositionStride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS position stride is zero"), operation);
            return false;
        }
        if(lss.vertexCount > 0 && lss.vertexRadiusStride == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS radius stride is zero"), operation);
            return false;
        }

        const VkRayTracingLssIndexingModeNV indexingMode = ConvertRayTracingLssIndexingMode(lss.primitiveFormat);
        const VkRayTracingLssPrimitiveEndCapsModeNV endCapsMode = ConvertRayTracingLssEndcapMode(lss.endcapMode);
        if(indexingMode == VK_RAY_TRACING_LSS_INDEXING_MODE_MAX_ENUM_NV){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS primitive format is invalid"), operation);
            return false;
        }
        if(endCapsMode == VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_MAX_ENUM_NV){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS endcap mode is invalid"), operation);
            return false;
        }

        const FormatInfo& vertexFormatInfo = GetFormatInfo(lss.vertexPositionFormat);
        const FormatInfo& radiusFormatInfo = GetFormatInfo(lss.vertexRadiusFormat);
        if(vertexFormatInfo.bytesPerBlock == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS position format size is invalid"), operation);
            return false;
        }
        if(radiusFormatInfo.bytesPerBlock == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS radius format size is invalid"), operation);
            return false;
        }
        if(lss.vertexCount > 0 && lss.vertexPositionStride < vertexFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS position stride is smaller than the position format size"), operation);
            return false;
        }
        if(lss.vertexCount > 0 && lss.vertexRadiusStride < radiusFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS radius stride is smaller than the radius format size"), operation);
            return false;
        }

        u32 requiredVertexCount = 0;
        if(lss.indexBuffer)
            requiredVertexCount = lss.vertexCount;
        else{
            if(lss.primitiveCount > UINT32_MAX / s_LssVerticesPerPrimitive){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS primitive count overflows vertex consumption"), operation);
                return false;
            }
            requiredVertexCount = lss.primitiveCount * s_LssVerticesPerPrimitive;
            if(lss.vertexCount < requiredVertexCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS vertex count is smaller than the non-indexed primitive consumption"), operation);
                return false;
            }
        }

        if(requireBuffers){
            if(!ValidateStridedBuildInputRange(
                lss.vertexBuffer,
                lss.vertexPositionOffset,
                requiredVertexCount,
                lss.vertexPositionStride,
                vertexFormatInfo.bytesPerBlock,
                operation,
                NWB_TEXT("LSS position")
            ))
                return false;
            if(!ValidateStridedBuildInputRange(
                lss.vertexBuffer,
                lss.vertexRadiusOffset,
                requiredVertexCount,
                lss.vertexRadiusStride,
                radiusFormatInfo.bytesPerBlock,
                operation,
                NWB_TEXT("LSS radius")
            ))
                return false;
        }

        lssData.indexType = VK_INDEX_TYPE_NONE_KHR;
        if(lss.indexBuffer){
            if(!GetRayTracingIndexType(lss.indexFormat, lssData.indexType)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS index format must be R16_UINT or R32_UINT"), operation);
                return false;
            }
            const u64 indexElementSize = GetRayTracingIndexElementSize(lss.indexFormat);
            const u32 indicesPerPrimitive = lss.primitiveFormat == RayTracingGeometryLssPrimitiveFormat::List ? s_LssListIndicesPerPrimitive : s_LssSuccessiveIndicesPerPrimitive;
            if(lss.primitiveCount > UINT32_MAX / indicesPerPrimitive){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS primitive count overflows index consumption"), operation);
                return false;
            }
            const u32 requiredIndexCount = lss.primitiveCount * indicesPerPrimitive;
            if(lss.indexCount < requiredIndexCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS index count is smaller than the selected primitive indexing mode requires"), operation);
                return false;
            }
            if(requiredIndexCount > 0 && lss.indexStride == 0){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS index stride is zero"), operation);
                return false;
            }
            if(requiredIndexCount > 0 && lss.indexStride < indexElementSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: LSS index stride is smaller than the index format size"), operation);
                return false;
            }
            if(requireBuffers){
                if(!ValidateStridedBuildInputRange(
                    lss.indexBuffer,
                    lss.indexOffset,
                    requiredIndexCount,
                    lss.indexStride,
                    indexElementSize,
                    operation,
                    NWB_TEXT("LSS index")
                ))
                    return false;
            }
            lssData.indexStride = lss.indexStride;
        }

        geometry.geometryType = VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV;
        geometry.pNext = &lssData;
        lssData.vertexFormat = vertexFormat;
        lssData.vertexStride = lss.vertexPositionStride;
        lssData.radiusFormat = radiusFormat;
        lssData.radiusStride = lss.vertexRadiusStride;
        lssData.indexingMode = indexingMode;
        lssData.endCapsMode = endCapsMode;
        primitiveCount = lss.primitiveCount;
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: geometry type is not supported by the Vulkan backend"), operation);
        return false;
    }

    geometry.flags = 0;
    if(geomDesc.flags & RayTracingGeometryFlags::Opaque)
        geometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
    if(geomDesc.flags & RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
        geometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

    return true;
}

bool ConvertAccelStructBuildFlags(
    const RayTracingAccelStructBuildFlags::Mask buildFlags,
    VkBuildAccelerationStructureFlagsKHR& outBuildFlags,
    const tchar* const operationName
){
    constexpr u8 s_KnownFlags =
        static_cast<u8>(RayTracingAccelStructBuildFlags::AllowUpdate)
        | static_cast<u8>(RayTracingAccelStructBuildFlags::PreferFastTrace)
        | static_cast<u8>(RayTracingAccelStructBuildFlags::PreferFastBuild)
        | static_cast<u8>(RayTracingAccelStructBuildFlags::MinimizeMemory)
        | static_cast<u8>(RayTracingAccelStructBuildFlags::PerformUpdate)
        | static_cast<u8>(RayTracingAccelStructBuildFlags::AllowEmptyInstances)
    ;
    const u8 flagBits = static_cast<u8>(buildFlags);
    if((flagBits & static_cast<u8>(~s_KnownFlags)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration-structure build flags contain unknown bits"), operationName);
        return false;
    }
    if(
        (buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        && (buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: fast-trace and fast-build flags are mutually exclusive"), operationName);
        return false;
    }

    outBuildFlags = 0u;

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        outBuildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        outBuildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
        outBuildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::MinimizeMemory)
        outBuildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

