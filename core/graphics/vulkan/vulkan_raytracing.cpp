// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkDeviceAddress GetBufferDeviceAddress(IBuffer* _buffer, u64 offset){
    if(!_buffer)
        return 0;

    auto* buffer = checked_cast<Buffer*>(_buffer);
    return buffer->m_deviceAddress + offset;
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

bool AlignUpU64Checked(u64 value, u32 alignment, u64& outAligned){
    if(alignment == 0 || (alignment & (alignment - 1u)) != 0u)
        return false;

    const u64 mask = static_cast<u64>(alignment - 1u);
    if(value > Limit<u64>::s_Max - mask)
        return false;

    outAligned = (value + mask) & ~mask;
    return true;
}

bool ComputeRayTracingHandleLayout(const VulkanContext& context, u32& outHandleSize, u32& outHandleSizeAligned, u32& outBaseAlignment, const tchar* operation){
    const u32 handleSize = context.rayTracingPipelineProperties.shaderGroupHandleSize;
    const u32 handleAlignment = context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    const u32 baseAlignment = context.rayTracingPipelineProperties.shaderGroupBaseAlignment;

    if(handleAlignment == 0 || (handleAlignment & (handleAlignment - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle alignment is invalid"), operation);
        return false;
    }
    if(baseAlignment == 0 || (baseAlignment & (baseAlignment - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group base alignment is invalid"), operation);
        return false;
    }
    if(handleSize > Limit<u32>::s_Max - (handleAlignment - 1u)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle size alignment overflows"), operation);
        return false;
    }

    const u32 handleSizeAligned = (handleSize + handleAlignment - 1u) & ~(handleAlignment - 1u);
    if(handleSizeAligned == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle size is invalid"), operation);
        return false;
    }

    outHandleSize = handleSize;
    outHandleSizeAligned = handleSizeAligned;
    outBaseAlignment = baseAlignment;
    return true;
}

bool ComputeShaderTableByteSize(u32 recordCount, u32 handleSizeAligned, u32 baseAlignment, u64& outByteSize, const tchar* operation){
    if(recordCount == 0 || handleSizeAligned == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count or stride is invalid"), operation);
        return false;
    }
    if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table size overflows"), operation);
        return false;
    }

    const u64 rawSize = static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
    if(!AlignUpU64Checked(rawSize, baseAlignment, outByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table alignment overflows"), operation);
        return false;
    }

    return true;
}

using MicromapUsageVector = Vector<VkMicromapUsageEXT, Alloc::ScratchAllocator<VkMicromapUsageEXT>>;

VkOpacityMicromapFormatEXT ConvertOpacityMicromapFormat(const OpacityMicromapFormat::Enum format){
    switch(format){
    case OpacityMicromapFormat::OC1_2_State:
        return VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    case OpacityMicromapFormat::OC1_4_State:
        return VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
    default:
        return VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT;
    }
}

bool BuildOpacityMicromapUsageCounts(
    const Vector<RayTracingOpacityMicromapUsageCount>& counts,
    MicromapUsageVector& outUsageCounts,
    const tchar* operation)
{
    outUsageCounts.clear();
    outUsageCounts.resize(counts.size());

    for(usize i = 0; i < counts.size(); ++i){
        const RayTracingOpacityMicromapUsageCount& count = counts[i];
        const VkOpacityMicromapFormatEXT format = ConvertOpacityMicromapFormat(count.format);
        if(format == VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to {}: opacity micromap usage count {} has invalid format {}"),
                operation,
                i,
                static_cast<u32>(count.format)
            );
            outUsageCounts.clear();
            return false;
        }

        VkMicromapUsageEXT& usageCount = outUsageCounts[i];
        usageCount.count = count.count;
        usageCount.subdivisionLevel = count.subdivisionLevel;
        usageCount.format = static_cast<u32>(format);
    }

    return true;
}

bool ValidateAccelStructBuildInputRange(IBuffer* _buffer, u64 offset, u64 byteSize, const tchar* operation, const tchar* resourceName){
    auto* buffer = checked_cast<Buffer*>(_buffer);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer is invalid"), operation, resourceName);
        return false;
    }

    const BufferDesc& desc = buffer->getDescription();
    if(!desc.isAccelStructBuildInput){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer was not created with acceleration-structure build input usage"), operation, resourceName);
        return false;
    }
    if(!IsBufferRangeInBounds(desc, offset, byteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} buffer range is outside the buffer"), operation, resourceName);
        return false;
    }

    return true;
}

bool FillBlasGeometryForSizeQuery(const RayTracingGeometryDesc& geomDesc, VkAccelerationStructureGeometryKHR& geometry, u32& primitiveCount, const tchar* operation, bool requireBuffers){
    geometry = MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
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
        if(triangles.vertexCount > 0 && triangles.vertexStride < vertexFormatInfo.bytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex stride is smaller than the vertex format size"), operation);
            return false;
        }
        if(requireBuffers){
            u64 vertexByteSize = 0;
            if(!ComputeStridedRangeByteSize(triangles.vertexCount, triangles.vertexStride, vertexFormatInfo.bytesPerBlock, vertexByteSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: triangle vertex buffer range overflows"), operation);
                return false;
            }
            if(!ValidateAccelStructBuildInputRange(triangles.vertexBuffer, triangles.vertexOffset, vertexByteSize, operation, NWB_TEXT("triangle vertex")))
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
                if(!ValidateAccelStructBuildInputRange(triangles.indexBuffer, triangles.indexOffset, indexByteSize, operation, NWB_TEXT("triangle index")))
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
        if(requireBuffers){
            u64 aabbByteSize = 0;
            if(!ComputeStridedRangeByteSize(aabbs.count, aabbs.stride, sizeof(RayTracingGeometryAABB), aabbByteSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: AABB buffer range overflows"), operation);
                return false;
            }
            if(!ValidateAccelStructBuildInputRange(aabbs.buffer, aabbs.offset, aabbByteSize, operation, NWB_TEXT("AABB")))
                return false;
        }

        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.stride = aabbs.stride;
        primitiveCount = aabbs.count;
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

VkBuildAccelerationStructureFlagsKHR ConvertAccelStructBuildFlags(RayTracingAccelStructBuildFlags::Mask buildFlags, bool allowCompaction){
    VkBuildAccelerationStructureFlagsKHR flags = 0;

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if(allowCompaction && (buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction))
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    return flags;
}

bool BuildClusterOperationInputInfo(
    const RayTracingClusterOperationParams& params,
    VkClusterAccelerationStructureInputInfoNV& outInputInfo,
    VkClusterAccelerationStructureMoveObjectsInputNV& outMoveInput,
    VkClusterAccelerationStructureTriangleClusterInputNV& outClusterInput,
    VkClusterAccelerationStructureClustersBottomLevelInputNV& outBlasInput,
    const tchar* operationName)
{
    VkClusterAccelerationStructureOpTypeNV opType;
    switch(params.type){
    case RayTracingClusterOperationType::Move:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
        break;
    case RayTracingClusterOperationType::ClasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::ClasBuildTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
        break;
    case RayTracingClusterOperationType::ClasInstantiateTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::BlasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
        break;
    default:
        return false;
    }

    VkClusterAccelerationStructureOpModeNV opMode;
    switch(params.mode){
    case RayTracingClusterOperationMode::ImplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::ExplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::GetSizes:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
        break;
    default:
        return false;
    }

    VkBuildAccelerationStructureFlagsKHR opFlags = 0;
    if(params.flags & RayTracingClusterOperationFlags::FastTrace)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(!(params.flags & RayTracingClusterOperationFlags::FastTrace) && (params.flags & RayTracingClusterOperationFlags::FastBuild))
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(params.flags & RayTracingClusterOperationFlags::AllowOMM)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;

    outInputInfo = MakeVkStruct<VkClusterAccelerationStructureInputInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV);
    outInputInfo.maxAccelerationStructureCount = params.maxArgCount;
    outInputInfo.flags = opFlags;
    outInputInfo.opType = opType;
    outInputInfo.opMode = opMode;

    outMoveInput = MakeVkStruct<VkClusterAccelerationStructureMoveObjectsInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV);
    outClusterInput = MakeVkStruct<VkClusterAccelerationStructureTriangleClusterInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV);
    outBlasInput = MakeVkStruct<VkClusterAccelerationStructureClustersBottomLevelInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV);

    switch(params.type){
    case RayTracingClusterOperationType::Move:{
        VkClusterAccelerationStructureTypeNV moveType;
        switch(params.move.type){
        case RayTracingClusterOperationMoveType::BottomLevel:  moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        case RayTracingClusterOperationMoveType::ClusterLevel: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV; break;
        case RayTracingClusterOperationMoveType::Template:     moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV; break;
        default: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        }
        outMoveInput.type = moveType;
        outMoveInput.noMoveOverlap = (params.flags & RayTracingClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        outMoveInput.maxMovedBytes = params.move.maxBytes;
        outInputInfo.opInput.pMoveObjects = &outMoveInput;
        break;
    }
    case RayTracingClusterOperationType::ClasBuild:
    case RayTracingClusterOperationType::ClasBuildTemplates:
    case RayTracingClusterOperationType::ClasInstantiateTemplates:{
        const VkFormat vertexFormat = ConvertFormat(params.clas.vertexFormat);
        if(vertexFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: vertex format is unsupported"), operationName);
            return false;
        }
        outClusterInput.vertexFormat = vertexFormat;
        outClusterInput.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        outClusterInput.maxClusterUniqueGeometryCount = params.clas.maxUniqueGeometryCount;
        outClusterInput.maxClusterTriangleCount = params.clas.maxTriangleCount;
        outClusterInput.maxClusterVertexCount = params.clas.maxVertexCount;
        outClusterInput.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
        outClusterInput.maxTotalVertexCount = params.clas.maxTotalVertexCount;
        outClusterInput.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        outInputInfo.opInput.pTriangleClusters = &outClusterInput;
        break;
    }
    case RayTracingClusterOperationType::BlasBuild:{
        outBlasInput.maxClusterCountPerAccelerationStructure = params.blas.maxClasPerBlasCount;
        outBlasInput.maxTotalClusterCount = params.blas.maxTotalClasCount;
        outInputInfo.opInput.pClustersBottomLevel = &outBlasInput;
        break;
    }
    default:
        break;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AccelStruct::AccelStruct(const VulkanContext& context)
    : RefCounter<IRayTracingAccelStruct>(context.threadPool)
    , m_context(context)
{}
AccelStruct::~AccelStruct(){
    if(m_compactionQueryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, m_compactionQueryPool, m_context.allocationCallbacks);
        m_compactionQueryPool = VK_NULL_HANDLE;
    }

    if(m_accelStruct){
        vkDestroyAccelerationStructureKHR(m_context.device, m_accelStruct, m_context.allocationCallbacks);
        m_accelStruct = VK_NULL_HANDLE;
    }

    m_buffer.reset();
}

Object AccelStruct::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_AccelerationStructureKHR)
        return Object(m_accelStruct);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpacityMicromap::OpacityMicromap(const VulkanContext& context)
    : RefCounter<IRayTracingOpacityMicromap>(context.threadPool)
    , m_context(context)
{}
OpacityMicromap::~OpacityMicromap(){
    if(m_micromap != VK_NULL_HANDLE){
        vkDestroyMicromapEXT(m_context.device, m_micromap, m_context.allocationCallbacks);
        m_micromap = VK_NULL_HANDLE;
    }
    m_dataBuffer.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipeline::RayTracingPipeline(const VulkanContext& context, Device& device)
    : RefCounter<IRayTracingPipeline>(context.threadPool)
    , m_shaderGroupHandles(Alloc::CustomAllocator<u8>(context.objectArena))
    , m_context(context)
    , m_device(device)
{}
RayTracingPipeline::~RayTracingPipeline(){
    VulkanDetail::DestroyPipelineAndOwnedLayout(
        m_context.device,
        m_context.allocationCallbacks,
        m_pipeline,
        m_pipelineLayout,
        m_ownsPipelineLayout
    );
}

Object RayTracingPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Acceleration structure extension is required to create ray tracing acceleration structures."));
        return nullptr;
    }

    auto* as = NewArenaObject<AccelStruct>(m_context.objectArena, m_context);
    as->m_desc = desc;

    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ?  VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR :  VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    u64 accelStructSize = s_DefaultTopLevelASBufferSize;
    if(desc.isTopLevel && desc.topLevelMaxInstances > 0){
        if(desc.topLevelMaxInstances > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: TLAS instance capacity exceeds Vulkan limit"));
            DestroyArenaObject(m_context.objectArena, as);
            return nullptr;
        }

        VkAccelerationStructureGeometryKHR geometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(desc.buildFlags, false);
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        u32 primitiveCount = static_cast<u32>(desc.topLevelMaxInstances);
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);
        if(sizeInfo.accelerationStructureSize > 0)
            accelStructSize = sizeInfo.accelerationStructureSize;
    }
    else if(!desc.isTopLevel && !desc.bottomLevelGeometries.empty()){
        if(desc.bottomLevelGeometries.size() > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: geometry count exceeds Vulkan limit"));
            DestroyArenaObject(m_context.objectArena, as);
            return nullptr;
        }

        Alloc::ScratchArena<> scratchArena(s_RayTracingScratchArenaBytes);
        Vector<VkAccelerationStructureGeometryKHR, Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>> geometries{ Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>(scratchArena) };
        Vector<uint32_t, Alloc::ScratchAllocator<uint32_t>> primitiveCounts{ Alloc::ScratchAllocator<uint32_t>(scratchArena) };
        geometries.resize(desc.bottomLevelGeometries.size());
        primitiveCounts.resize(desc.bottomLevelGeometries.size());

        for(usize i = 0; i < desc.bottomLevelGeometries.size(); ++i){
            if(!VulkanDetail::FillBlasGeometryForSizeQuery(desc.bottomLevelGeometries[i], geometries[i], primitiveCounts[i], NWB_TEXT("create BLAS"), false)){
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(desc.buildFlags, true);
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<u32>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, primitiveCounts.data(), &sizeInfo);
        if(sizeInfo.accelerationStructureSize > 0)
            accelStructSize = sizeInfo.accelerationStructureSize;
    }

    BufferDesc bufferDesc;
    bufferDesc.byteSize = accelStructSize;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = "AccelStructBuffer";
    bufferDesc.isVirtual = desc.isVirtual;

    as->m_buffer = createBuffer(bufferDesc);

    if(!as->m_buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate acceleration structure storage buffer"));
        DestroyArenaObject(m_context.objectArena, as);
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
    createInfo.buffer = checked_cast<Buffer*>(as->m_buffer.get())->m_buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;

    res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->m_accelStruct);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, as);
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
    addressInfo.accelerationStructure = as->m_accelStruct;
    if(!desc.isVirtual)
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);

    return RayTracingAccelStructHandle(as, RayTracingAccelStructHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

RayTracingOpacityMicromapHandle Device::createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.EXT_opacity_micromap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Opacity micromap extension is required to create opacity micromaps."));
        return nullptr;
    }
    if(desc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create opacity micromap: usage-count count exceeds Vulkan limit"));
        return nullptr;
    }

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    Alloc::ScratchArena<> scratchArena;
    VulkanDetail::MicromapUsageVector usageCounts{ Alloc::ScratchAllocator<VkMicromapUsageEXT>(scratchArena) };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(desc.counts, usageCounts, NWB_TEXT("create opacity micromap")))
        return nullptr;

    VkMicromapBuildInfoEXT buildInfo = VulkanDetail::MakeVkStruct<VkMicromapBuildInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT);
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.usageCountsCount = static_cast<u32>(usageCounts.size());
    buildInfo.pUsageCounts = usageCounts.empty() ? nullptr : usageCounts.data();

    VkMicromapBuildSizesInfoEXT buildSize = VulkanDetail::MakeVkStruct<VkMicromapBuildSizesInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT);
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    auto* om = NewArenaObject<OpacityMicromap>(m_context.objectArena, m_context);
    om->m_desc = desc;

    BufferDesc bufferDesc;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.byteSize = buildSize.micromapSize;
    bufferDesc.initialState = ResourceStates::OpacityMicromapWrite;
    bufferDesc.keepInitialState = true;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = desc.debugName;
    om->m_dataBuffer = createBuffer(bufferDesc);

    if(!om->m_dataBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate opacity micromap storage buffer"));
        DestroyArenaObject(m_context.objectArena, om);
        return nullptr;
    }

    auto* buffer = checked_cast<Buffer*>(om->m_dataBuffer.get());

    VkMicromapCreateInfoEXT createInfo = VulkanDetail::MakeVkStruct<VkMicromapCreateInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT);
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    createInfo.buffer = buffer->m_buffer;
    createInfo.size = buildSize.micromapSize;
    createInfo.deviceAddress = buffer->m_deviceAddress;

    res = vkCreateMicromapEXT(m_context.device, &createInfo, m_context.allocationCallbacks, &om->m_micromap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create opacity micromap: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, om);
        return nullptr;
    }

    return RayTracingOpacityMicromapHandle(om, RayTracingOpacityMicromapHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

MemoryRequirements Device::getAccelStructMemoryRequirements(IRayTracingAccelStruct* _as){
    auto* as = checked_cast<AccelStruct*>(_as);

    MemoryRequirements requirements = {};

    if(as->m_buffer){
        requirements.size = as->m_buffer->getDescription().byteSize;
        requirements.alignment = s_AccelerationStructureAlignment; // AS alignment requirement
    }

    return requirements;
}

RayTracingClusterOperationSizeInfo Device::getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params){
    if(!m_context.extensions.NV_cluster_acceleration_structure){
        return RayTracingClusterOperationSizeInfo{};
    }

    RayTracingClusterOperationSizeInfo info;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("query cluster operation sizes")))
        return info;

    VkAccelerationStructureBuildSizesInfoKHR vkSizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetClusterAccelerationStructureBuildSizesNV(m_context.device, &inputInfo, &vkSizeInfo);

    info.resultMaxSizeInBytes = vkSizeInfo.accelerationStructureSize;
    info.scratchSizeInBytes = vkSizeInfo.buildScratchSize;

    return info;
}

bool Device::bindAccelStructMemory(IRayTracingAccelStruct* _as, IHeap* heap, u64 offset){
    if(!_as){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: acceleration structure is null"));
        return false;
    }
    if(!heap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: heap is null"));
        return false;
    }

    auto* as = checked_cast<AccelStruct*>(_as);

    if(as->m_buffer){
        if(!bindBufferMemory(as->m_buffer.get(), heap, offset))
            return false;

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = as->m_accelStruct;
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: storage buffer is null"));
    return false;
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_ray_tracing_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Ray tracing pipeline extension is required to create ray tracing pipelines."));
        return nullptr;
    }
    if(desc.maxRecursionDepth > m_context.rayTracingPipelineProperties.maxRayRecursionDepth){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: max recursion depth {} exceeds device limit {}"),
            desc.maxRecursionDepth,
            m_context.rayTracingPipelineProperties.maxRayRecursionDepth
        );
        return nullptr;
    }
    if(desc.hitGroups.size() > (static_cast<usize>(-1) - desc.shaders.size()) / s_RayTracingHitGroupShaderStageCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader stage count overflows"));
        return nullptr;
    }
    const usize maxShaderStages = desc.shaders.size() + desc.hitGroups.size() * s_RayTracingHitGroupShaderStageCount;
    if(maxShaderStages > static_cast<usize>(UINT32_MAX)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader stage count exceeds Vulkan limit"));
        return nullptr;
    }

    auto* pso = NewArenaObject<RayTracingPipeline>(m_context.objectArena, m_context, *this);
    pso->m_desc = desc;

    Alloc::ScratchArena<> scratchArena(s_RayTracingScratchArenaBytes);

    PipelineShaderStageVector stages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    Vector<VkRayTracingShaderGroupCreateInfoKHR, Alloc::ScratchAllocator<VkRayTracingShaderGroupCreateInfoKHR>> groups{ Alloc::ScratchAllocator<VkRayTracingShaderGroupCreateInfoKHR>(scratchArena) };
    PipelineSpecializationInfoVector specInfos{ Alloc::ScratchAllocator<VkSpecializationInfo>(scratchArena) };
    PipelineDescriptorHeapScratch descriptorHeapScratch{ scratchArena };

    stages.reserve(maxShaderStages);
    groups.reserve(desc.shaders.size() + desc.hitGroups.size());
    specInfos.reserve(maxShaderStages);

    auto addShaderSpecialization = [&](Shader* s, VkPipelineShaderStageCreateInfo& stageInfo){
        if(s->m_specializationEntries.empty())
            return;

        VkSpecializationInfo specInfo{};
        specInfo.mapEntryCount = static_cast<u32>(s->m_specializationEntries.size());
        specInfo.pMapEntries = s->m_specializationEntries.data();
        specInfo.dataSize = s->m_specializationData.size();
        specInfo.pData = s->m_specializationData.data();
        specInfos.push_back(specInfo);
        stageInfo.pSpecializationInfo = &specInfos.back();
    };

    for(const auto& shaderDesc : desc.shaders){
        if(!shaderDesc.shader)
            continue;

        auto* s = checked_cast<Shader*>(shaderDesc.shader.get());

        VkPipelineShaderStageCreateInfo stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stageInfo.module = s->m_shaderModule;
        stageInfo.pName = s->m_entryPointName.c_str();

        switch(s->m_desc.shaderType){
        case ShaderType::RayGeneration:
            stageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            break;
        case ShaderType::Miss:
            stageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            break;
        case ShaderType::Callable:
            stageInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            break;
        default:
            continue;
        }
        addShaderSpecialization(s, stageInfo);

        VkRayTracingShaderGroupCreateInfoKHR group = VulkanDetail::MakeVkStruct<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<u32>(stages.size());
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        stages.push_back(stageInfo);
        groups.push_back(group);
    }

    for(const auto& hitGroup : desc.hitGroups){
        VkRayTracingShaderGroupCreateInfoKHR group = VulkanDetail::MakeVkStruct<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
        group.type = hitGroup.isProceduralPrimitive ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        if(hitGroup.closestHitShader){
            auto* s = checked_cast<Shader*>(hitGroup.closestHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.closestHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.anyHitShader){
            auto* s = checked_cast<Shader*>(hitGroup.anyHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.anyHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.intersectionShader){
            auto* s = checked_cast<Shader*>(hitGroup.intersectionShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.intersectionShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        groups.push_back(group);
    }

    if(stages.empty() || groups.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: no shader stages or groups were provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(!configurePipelineBindings(
        desc.globalBindingLayouts,
        NWB_TEXT("ray tracing pipeline"),
        stages,
        descriptorHeapScratch,
        *pso,
        scratchArena))
    {
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    VkRayTracingPipelineCreateInfoKHR createInfo = VulkanDetail::MakeVkStruct<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
    if(pso->m_usesDescriptorHeap)
        createInfo.pNext = descriptorHeapScratch.pNext();
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pso->m_usesDescriptorHeap ? VK_NULL_HANDLE : pso->m_pipelineLayout;

    res = vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 1, &createInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("create ray tracing pipeline"))){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    u32 groupCount = static_cast<u32>(groups.size());
    if(handleSizeAligned == 0 || static_cast<usize>(groupCount) > Limit<usize>::s_Max / static_cast<usize>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader group handle table size overflows"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    const usize shaderGroupHandleBytes = static_cast<usize>(groupCount) * static_cast<usize>(handleSizeAligned);
    pso->m_shaderGroupHandles.resize(shaderGroupHandleBytes);
    res = vkGetRayTracingShaderGroupHandlesKHR(
        m_context.device,
        pso->m_pipeline,
        0,
        groupCount,
        pso->m_shaderGroupHandles.size(),
        pso->m_shaderGroupHandles.data()
    );
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to retrieve ray tracing shader group handles: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return RayTracingPipelineHandle(pso, RayTracingPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderTable::ShaderTable(const VulkanContext& context, Device& device)
    : RefCounter<IRayTracingShaderTable>(context.threadPool)
    , m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable() = default;

RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    auto* sbt = NewArenaObject<ShaderTable>(m_context.objectArena, m_context, m_device);
    sbt->m_pipeline = this;
    return RayTracingShaderTableHandle(sbt, RayTracingShaderTableHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u32 ShaderTable::findGroupIndex(const Name& exportName)const{
    if(!m_pipeline)
        return UINT32_MAX;

    u32 groupIndex = 0;
    for(const auto& shaderDesc : m_pipeline->m_desc.shaders){
        if(!shaderDesc.shader)
            continue;

        const ShaderDesc& desc = shaderDesc.shader->getDescription();
        switch(desc.shaderType){
        case ShaderType::RayGeneration:
        case ShaderType::Miss:
        case ShaderType::Callable:
            break;
        default:
            continue;
        }

        const Name shaderExportName = shaderDesc.exportName != NAME_NONE ? shaderDesc.exportName : desc.entryName;
        if(shaderExportName == exportName)
            return groupIndex;
        ++groupIndex;
    }
    for(const auto& hitGroup : m_pipeline->m_desc.hitGroups){
        if(hitGroup.exportName == exportName)
            return groupIndex;
        ++groupIndex;
    }
    return UINT32_MAX;
}

u32 ShaderTable::appendShaderRecord(
    const Name& exportName,
    BufferHandle& buffer,
    u64& offset,
    u32& count,
    const tchar* operationName,
    const tchar* recordName,
    const tchar* exportKind
){
    if(count == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count exceeds u32 range"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to add shader table record: record count exceeds u32 range"));
        return count;
    }
    if(!m_pipeline)
        return count++;

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, operationName))
        return count;

    const u32 newCount = count + 1;
    u64 sbtSize = 0;
    if(!VulkanDetail::ComputeShaderTableByteSize(newCount, handleSizeAligned, baseAlignment, sbtSize, operationName))
        return count;

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate {} SBT buffer"), recordName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate shader table buffer"));
        return count;
    }

    void* mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map {} SBT buffer"), recordName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map shader table buffer"));
        return count;
    }

    if(buffer && count > 0){
        void* oldMapped = m_device.mapBuffer(buffer.get(), CpuAccessMode::Read);
        if(!oldMapped){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map previous {} SBT buffer"), recordName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map previous shader table buffer"));
            m_device.unmapBuffer(newBuffer.get());
            return count;
        }
        const usize copySize = static_cast<usize>(count) * handleSizeAligned;
        VulkanDetail::CopyHostMemory(taskPool(), mapped, oldMapped, copySize);
        m_device.unmapBuffer(buffer.get());
    }

    const u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        m_device.unmapBuffer(newBuffer.get());
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} export not found in pipeline"), exportKind);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Shader table export not found in pipeline"));
        return count;
    }

    auto* dst = static_cast<u8*>(mapped) + count * handleSizeAligned;
    NWB_MEMCPY(dst, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(newBuffer.get());

    buffer = newBuffer;
    offset = 0;
    return count++;
}

void ShaderTable::allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize){
    BufferDesc bufferDesc;
    bufferDesc.byteSize = sbtSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;

    outBuffer = m_device.createBuffer(bufferDesc);
}

void ShaderTable::setRayGenerationShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set ray generation shader: shader table has no pipeline"));
        return;
    }

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("set ray generation shader")))
        return;
    u64 sbtSize = 0;
    if(!VulkanDetail::ComputeShaderTableByteSize(1, handleSizeAligned, baseAlignment, sbtSize, NWB_TEXT("set ray generation shader")))
        return;

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Ray generation export not found in pipeline"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Ray generation export not found in pipeline"));
        return;
    }

    allocateSBTBuffer(m_raygenBuffer, sbtSize);
    if(!m_raygenBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate ray generation SBT buffer"));
        return;
    }

    m_raygenOffset = 0;

    void* mapped = m_device.mapBuffer(m_raygenBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map ray generation SBT buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map ray generation SBT buffer"));
        return;
    }

    NWB_MEMCPY(mapped, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(m_raygenBuffer.get());
}

u32 ShaderTable::addMissShader(const Name& exportName, IBindingSet* /*bindings*/){
    return appendShaderRecord(exportName, m_missBuffer, m_missOffset, m_missCount, NWB_TEXT("add miss shader"), NWB_TEXT("miss"), NWB_TEXT("Miss shader"));
}

u32 ShaderTable::addHitGroup(const Name& exportName, IBindingSet* /*bindings*/){
    return appendShaderRecord(exportName, m_hitBuffer, m_hitOffset, m_hitCount, NWB_TEXT("add hit group"), NWB_TEXT("hit"), NWB_TEXT("Hit group"));
}

u32 ShaderTable::addCallableShader(const Name& exportName, IBindingSet* /*bindings*/){
    return appendShaderRecord(exportName, m_callableBuffer, m_callableOffset, m_callableCount, NWB_TEXT("add callable shader"), NWB_TEXT("callable"), NWB_TEXT("Callable shader"));
}

void ShaderTable::clearMissShaders(){ m_missCount = 0; m_missBuffer = nullptr; }
void ShaderTable::clearHitShaders(){ m_hitCount = 0; m_hitBuffer = nullptr; }
void ShaderTable::clearCallableShaders(){ m_callableCount = 0; m_callableBuffer = nullptr; }

Object ShaderTable::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Buffer && m_raygenBuffer){
        auto* buf = checked_cast<Buffer*>(m_raygenBuffer.get());
        return Object(buf->m_buffer);
    }
    return Object{nullptr};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setRayTracingState(const RayTracingState& state){
    endActiveRenderPass();
    commitBarriers();
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = state;

    if(!state.shaderTable)
        return;

    auto* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    RayTracingPipeline* pipeline = sbt->m_pipeline;

    if(!pipeline)
        return;

    vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipeline);

    bindPipelineBindingSets(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipelineLayout, pipeline->m_usesDescriptorHeap, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);
}


bool CommandList::attachAccelStructBuildScratchBuffer(
    VkAccelerationStructureBuildGeometryInfoKHR& buildInfo,
    const u64 buildScratchSize,
    const char* debugName,
    const tchar* operationName)
{
    if(buildScratchSize == 0)
        return true;

    BufferDesc scratchDesc;
    scratchDesc.byteSize = buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = debugName;

    BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
    if(!scratchBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate acceleration structure scratch buffer"));
        return false;
    }

    buildInfo.scratchData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(scratchBuffer.get());
    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildBottomLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!_as){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure is null"));
        return;
    }
    if(!pGeometries && numGeometries > 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: geometry data is null"));
        return;
    }
    if(numGeometries == 0)
        return;
    if(numGeometries > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: geometry count exceeds Vulkan limit"));
        return;
    }

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);
    if(!as || as->m_desc.isTopLevel){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure is not bottom-level"));
        return;
    }

    Alloc::ScratchArena<> scratchArena(s_RayTracingScratchArenaBytes);

    Vector<VkAccelerationStructureGeometryKHR, Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>> geometries{ Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>(scratchArena) };
    Vector<VkAccelerationStructureBuildRangeInfoKHR, Alloc::ScratchAllocator<VkAccelerationStructureBuildRangeInfoKHR>> rangeInfos{ Alloc::ScratchAllocator<VkAccelerationStructureBuildRangeInfoKHR>(scratchArena) };
    Vector<uint32_t, Alloc::ScratchAllocator<uint32_t>> primitiveCounts{ Alloc::ScratchAllocator<uint32_t>(scratchArena) };

    geometries.resize(numGeometries);
    rangeInfos.resize(numGeometries);
    primitiveCounts.resize(numGeometries);

    for(usize i = 0; i < numGeometries; ++i){
        VkAccelerationStructureGeometryKHR validationGeometry = {};
        u32 validationPrimitiveCount = 0;
        if(!VulkanDetail::FillBlasGeometryForSizeQuery(pGeometries[i], validationGeometry, validationPrimitiveCount, NWB_TEXT("build BLAS"), true))
            return;
    }

    auto buildGeometry = [&](usize i){
        const auto& geomDesc = pGeometries[i];

        VkAccelerationStructureGeometryKHR geometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        u32 primitiveCount = 0;

        if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
            const auto& triangles = geomDesc.geometryData.triangles;

            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = ConvertFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(triangles.vertexBuffer, triangles.vertexOffset);
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount > 0 ? triangles.vertexCount - 1 : 0;

            if(triangles.indexBuffer){
                VulkanDetail::GetRayTracingIndexType(triangles.indexFormat, geometry.geometry.triangles.indexType);
                geometry.geometry.triangles.indexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(triangles.indexBuffer, triangles.indexOffset);
                primitiveCount = triangles.indexCount / s_TrianglesPerPrimitive;
            }
            else{
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                primitiveCount = triangles.vertexCount / s_TrianglesPerPrimitive;
            }

            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
            const auto& aabbs = geomDesc.geometryData.aabbs;

            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            geometry.geometry.aabbs.data.deviceAddress = VulkanDetail::GetBufferDeviceAddress(aabbs.buffer, aabbs.offset);
            geometry.geometry.aabbs.stride = aabbs.stride;

            primitiveCount = aabbs.count;
            rangeInfo.primitiveCount = primitiveCount;
        }

        geometry.flags = 0;
        if(geomDesc.flags & RayTracingGeometryFlags::Opaque)
            geometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
        if(geomDesc.flags & RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
            geometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

        geometries[i] = geometry;
        rangeInfos[i] = rangeInfo;
        primitiveCounts[i] = static_cast<uint32_t>(primitiveCount);
    };

    if(taskPool().isParallelEnabled() && numGeometries >= s_ParallelGeometryThreshold)
        scheduleParallelFor(static_cast<usize>(0), numGeometries, s_GeometryGrainSize, buildGeometry);
    else{
        for(usize i = 0; i < numGeometries; ++i)
            buildGeometry(i);
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, true);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = static_cast<u32>(geometries.size());
    buildInfo.pGeometries = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, primitiveCounts.data(), &sizeInfo);

    auto* asBuffer = checked_cast<Buffer*>(as->m_buffer.get());
    if(!asBuffer || asBuffer->m_desc.byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        return;
    }

    if(!attachAccelStructBuildScratchBuffer(buildInfo, sizeInfo.buildScratchSize, "AS_BuildScratch", NWB_TEXT("allocate BLAS scratch buffer")))
        return;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfos);

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction)
        m_pendingCompactions.push_back(as);

    m_currentCmdBuf->m_referencedResources.push_back(_as);
}

void CommandList::compactBottomLevelAccelStructs(){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    if(m_pendingCompactions.empty())
        return;

    for(auto& as : m_pendingCompactions){
        if(as->m_compactionQueryPool == VK_NULL_HANDLE || as->m_compacted)
            continue;

        u64 compactedSize = 0;
        res = vkGetQueryPoolResults(
            m_context.device,
            as->m_compactionQueryPool,
            as->m_compactionQueryIndex,
            1,
            sizeof(u64),
            &compactedSize,
            sizeof(u64),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if(res != VK_SUCCESS || compactedSize == 0)
            continue;

        BufferDesc compactBufferDesc;
        compactBufferDesc.byteSize = compactedSize;
        compactBufferDesc.isAccelStructStorage = true;
        compactBufferDesc.debugName = as->m_desc.debugName;

        BufferHandle compactBuffer = m_device.createBuffer(compactBufferDesc);
        if(!compactBuffer)
            continue;

        VkAccelerationStructureCreateInfoKHR createInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
        createInfo.buffer = checked_cast<Buffer*>(compactBuffer.get())->m_buffer;
        createInfo.size = compactedSize;
        createInfo.type = as->m_desc.isTopLevel
            ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
            : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
            ;

        VkAccelerationStructureKHR newAS = VK_NULL_HANDLE;
        res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &newAS);
        if(res != VK_SUCCESS)
            continue;

        VkCopyAccelerationStructureInfoKHR copyInfo = VulkanDetail::MakeVkStruct<VkCopyAccelerationStructureInfoKHR>(VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR);
        copyInfo.src  = as->m_accelStruct; // original (still the copy source)
        copyInfo.dst  = newAS;
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        vkCmdCopyAccelerationStructureKHR(m_currentCmdBuf->m_cmdBuf, &copyInfo);

        m_currentCmdBuf->m_referencedAccelStructHandles.push_back(as->m_accelStruct);
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(as->m_buffer);

        as->m_accelStruct = newAS;
        as->m_buffer = Move(compactBuffer);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addrInfo.accelerationStructure = as->m_accelStruct;
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addrInfo);

        vkDestroyQueryPool(m_context.device, as->m_compactionQueryPool, m_context.allocationCallbacks);
        as->m_compactionQueryPool  = VK_NULL_HANDLE;
        as->m_compactionQueryIndex = 0;
        as->m_compacted = true;
    }

    for(auto& as : m_pendingCompactions){
        if(as->m_compactionQueryPool != VK_NULL_HANDLE || as->m_compacted)
            continue;

        VkQueryPoolCreateInfo queryPoolInfo = VulkanDetail::MakeVkStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        queryPoolInfo.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        queryPoolInfo.queryCount = s_SingleQueryCount;

        VkQueryPool queryPool = VK_NULL_HANDLE;
        if(vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &queryPool) != VK_SUCCESS)
            continue;

        vkCmdResetQueryPool(m_currentCmdBuf->m_cmdBuf, queryPool, s_TimerQueryBeginIndex, s_SingleQueryCount);

        vkCmdWriteAccelerationStructuresPropertiesKHR(
            m_currentCmdBuf->m_cmdBuf,
            s_SingleQueryCount,
            &as->m_accelStruct,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            queryPool,
            s_TimerQueryBeginIndex
        );

        as->m_compactionQueryPool  = queryPool;
        as->m_compactionQueryIndex = 0;
    }

    {
        usize dst = 0;
        for(usize i = 0; i < m_pendingCompactions.size(); ++i)
            if(!m_pendingCompactions[i]->m_compacted)
                m_pendingCompactions[dst++] = Move(m_pendingCompactions[i]);
        m_pendingCompactions.resize(dst);
    }
}

void CommandList::buildTopLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!_as){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure is null"));
        return;
    }
    if(!pInstances && numInstances > 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance data is null"));
        return;
    }
    if(numInstances == 0)
        return;
    if(numInstances > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance count exceeds Vulkan limit"));
        return;
    }

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);
    if(!as || !as->m_desc.isTopLevel){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure is not top-level"));
        return;
    }
    for(usize i = 0; i < numInstances; ++i){
        auto* blas = checked_cast<AccelStruct*>(pInstances[i].bottomLevelAS);
        if(!blas || blas->m_desc.isTopLevel || blas->m_deviceAddress == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} references an invalid bottom-level acceleration structure"), i);
            return;
        }
    }

    u64 instanceBufferSize = numInstances * sizeof(VkAccelerationStructureInstanceKHR);
    BufferDesc instanceBufferDesc;
    instanceBufferDesc.byteSize = instanceBufferSize;
    instanceBufferDesc.cpuAccess = CpuAccessMode::Write;
    instanceBufferDesc.isAccelStructBuildInput = true;
    instanceBufferDesc.debugName = "TLAS_InstanceBuffer";

    BufferHandle instanceBuffer = m_device.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate TLAS instance buffer"));
        return;
    }

    auto* mappedInstances = static_cast<VkAccelerationStructureInstanceKHR*>(m_device.mapBuffer(instanceBuffer.get(), CpuAccessMode::Write));
    if(!mappedInstances){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map TLAS instance buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map TLAS instance buffer"));
        return;
    }

    auto buildVkInstance = [&](usize i){
        const auto& inst = pInstances[i];
        VkAccelerationStructureInstanceKHR& vkInst = mappedInstances[i];

        NWB_MEMCPY(&vkInst.transform, sizeof(VkTransformMatrixKHR), &inst.transform, sizeof(VkTransformMatrixKHR));

        vkInst.instanceCustomIndex = inst.instanceID & s_InstanceFieldMask24Bit;
        vkInst.mask = inst.instanceMask;
        vkInst.instanceShaderBindingTableRecordOffset = inst.instanceContributionToHitGroupIndex & s_InstanceFieldMask24Bit;
        vkInst.flags = 0;

        if(inst.flags & RayTracingInstanceFlags::TriangleCullDisable)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::TriangleFrontCounterclockwise)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::ForceOpaque)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::ForceNonOpaque)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

        auto* blas = checked_cast<AccelStruct*>(inst.bottomLevelAS);
        vkInst.accelerationStructureReference = blas ? blas->m_deviceAddress : 0;
    };

    // TLAS instance conversion is CPU-only and scales with scene instance count.
    if(taskPool().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold)
        scheduleParallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
    else{
        for(usize i = 0; i < numInstances; ++i)
            buildVkInstance(i);
    }

    m_device.unmapBuffer(instanceBuffer.get());

    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceBuffer.get());
    if(!buildTopLevelAccelStructFromInstanceData(_as, as, instanceDataAddress, numInstances, buildFlags, NWB_TEXT("build TLAS")))
        return;

    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(instanceBuffer));
}

void CommandList::buildOpacityMicromap(IRayTracingOpacityMicromap* _omm, const RayTracingOpacityMicromapDesc& ommDesc){
    if(!m_context.extensions.EXT_opacity_micromap)
        return;

    if(!_omm){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is null"));
        return;
    }

    auto* omm = checked_cast<OpacityMicromap*>(_omm);
    if(!omm){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is invalid"));
        return;
    }
    if(ommDesc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: usage-count count exceeds Vulkan limit"));
        return;
    }

    u64 triangleDescBytes = 0;
    for(const RayTracingOpacityMicromapUsageCount& count : ommDesc.counts){
        const u64 countBytes = static_cast<u64>(count.count) * sizeof(VkMicromapTriangleEXT);
        if(triangleDescBytes > UINT64_MAX - countBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor size overflows"));
            return;
        }
        triangleDescBytes += countBytes;
    }

    auto* inputBuffer = checked_cast<Buffer*>(ommDesc.inputBuffer);
    if(!inputBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: input buffer is invalid"));
        return;
    }
    if(!VulkanDetail::IsBufferRangeInBounds(inputBuffer->m_desc, ommDesc.inputBufferOffset, 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: input buffer offset is outside the buffer"));
        return;
    }

    auto* perOmmDescs = checked_cast<Buffer*>(ommDesc.perOmmDescs);
    if(!perOmmDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor buffer is invalid"));
        return;
    }
    if(triangleDescBytes > 0 && !VulkanDetail::IsBufferRangeInBounds(perOmmDescs->m_desc, ommDesc.perOmmDescsOffset, triangleDescBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor range is outside the buffer"));
        return;
    }

    if(m_enableAutomaticBarriers){
        if(ommDesc.inputBuffer)
            setBufferState(ommDesc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
        if(ommDesc.perOmmDescs)
            setBufferState(ommDesc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);
        if(omm->m_dataBuffer)
            setBufferState(omm->m_dataBuffer.get(), ResourceStates::OpacityMicromapWrite);
    }

    if(ommDesc.trackLiveness){
        if(ommDesc.inputBuffer)
            m_currentCmdBuf->m_referencedResources.push_back(ommDesc.inputBuffer);
        if(ommDesc.perOmmDescs)
            m_currentCmdBuf->m_referencedResources.push_back(ommDesc.perOmmDescs);
        if(omm->m_dataBuffer)
            m_currentCmdBuf->m_referencedResources.push_back(omm->m_dataBuffer.get());
    }

    commitBarriers();

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    Alloc::ScratchArena<> scratchArena;
    VulkanDetail::MicromapUsageVector usageCounts{ Alloc::ScratchAllocator<VkMicromapUsageEXT>(scratchArena) };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(ommDesc.counts, usageCounts, NWB_TEXT("build opacity micromap")))
        return;

    VkMicromapBuildInfoEXT buildInfo = VulkanDetail::MakeVkStruct<VkMicromapBuildInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT);
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.dstMicromap = omm->m_micromap;
    buildInfo.usageCountsCount = static_cast<u32>(usageCounts.size());
    buildInfo.pUsageCounts = usageCounts.empty() ? nullptr : usageCounts.data();
    buildInfo.data.deviceAddress = VulkanDetail::GetBufferDeviceAddress(ommDesc.inputBuffer, ommDesc.inputBufferOffset);
    buildInfo.triangleArray.deviceAddress = VulkanDetail::GetBufferDeviceAddress(ommDesc.perOmmDescs, ommDesc.perOmmDescsOffset);
    buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

    VkMicromapBuildSizesInfoEXT buildSize = VulkanDetail::MakeVkStruct<VkMicromapBuildSizesInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT);
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    auto* dataBuffer = checked_cast<Buffer*>(omm->m_dataBuffer.get());
    if(!dataBuffer || dataBuffer->m_desc.byteSize < buildSize.micromapSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap storage is too small"));
        return;
    }

    if(buildSize.buildScratchSize != 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = buildSize.buildScratchSize;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "OMM_BuildScratch";
        scratchDesc.canHaveUAVs = true;

        BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
        if(!scratchBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate opacity micromap scratch buffer"));
            return;
        }

        buildInfo.scratchData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(scratchBuffer.get());

        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);

        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    }
    else{
        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);
    }
}

void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return;

    RayTracingState& state = m_currentRayTracingState;

    if(!state.shaderTable)
        return;

    auto* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    if(!sbt->m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: shader table has no ray tracing pipeline"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: shader table has no ray tracing pipeline"));
        return;
    }
    if(!sbt->m_raygenBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: ray generation shader is not set"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: ray generation shader is not set"));
        return;
    }
    if(args.width == 0 || args.height == 0 || args.depth == 0)
        return;

    const u64 widthHeight = static_cast<u64>(args.width) * args.height;
    const u64 invocationCount = widthHeight * args.depth;
    if((args.height != 0 && widthHeight / args.height != args.width)
        || (args.depth != 0 && invocationCount / args.depth != widthHeight)
        || invocationCount > m_context.rayTracingPipelineProperties.maxRayDispatchInvocationCount)
    {
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to dispatch rays: dispatch dimensions ({}, {}, {}) exceed ray dispatch limit {}"),
            args.width,
            args.height,
            args.depth,
            m_context.rayTracingPipelineProperties.maxRayDispatchInvocationCount
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: dispatch dimensions exceed ray dispatch limit"));
        return;
    }

    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("dispatch rays"))){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: invalid shader group handle layout"));
        return;
    }

    auto computeRegionSize = [&](u32 recordCount, VkDeviceSize& outRegionSize, const tchar* regionName) -> bool{
        if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: {} shader table region size overflows"), regionName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: shader table region size overflows"));
            return false;
        }

        outRegionSize = static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
        return true;
    };

    if(sbt->m_raygenBuffer){
        raygenRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_raygenBuffer.get(), sbt->m_raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }

    if(sbt->m_missBuffer){
        missRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_missBuffer.get(), sbt->m_missOffset);
        missRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_missCount, missRegion.size, NWB_TEXT("miss")))
            return;
    }

    if(sbt->m_hitBuffer){
        hitRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_hitBuffer.get(), sbt->m_hitOffset);
        hitRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_hitCount, hitRegion.size, NWB_TEXT("hit")))
            return;
    }

    if(sbt->m_callableBuffer){
        callableRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_callableBuffer.get(), sbt->m_callableOffset);
        callableRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_callableCount, callableRegion.size, NWB_TEXT("callable")))
            return;
    }

    vkCmdTraceRaysKHR(m_currentCmdBuf->m_cmdBuf, &raygenRegion, &missRegion, &hitRegion, &callableRegion, args.width, args.height, args.depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

