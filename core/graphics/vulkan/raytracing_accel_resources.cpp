// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildClusterOperationInputInfo(
    const RayTracingClusterOperationParams& params,
    VkClusterAccelerationStructureInputInfoNV& outInputInfo,
    VkClusterAccelerationStructureMoveObjectsInputNV& outMoveInput,
    VkClusterAccelerationStructureTriangleClusterInputNV& outClusterInput,
    VkClusterAccelerationStructureClustersBottomLevelInputNV& outBlasInput,
    const tchar* operationName
){
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
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_CLUSTER_OPACITY_MICROMAPS_BIT_NV;

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
        default: return false;
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


AccelStruct::AccelStruct(
    const VulkanContext& context,
    const ResourceQueueSharing::Mask creationQueueSharing
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_creationQueueSharing(creationQueueSharing)
    , m_acceptedBuildGeometrySignatures(context.objectArena)
    , m_context(context)
{
    m_desc.queueSharing = creationQueueSharing;
}
AccelStruct::~AccelStruct(){
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

RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    if(!ResourceQueueSharing::IsValid(desc.queueSharing)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create acceleration structure: queue sharing contains unknown bits")
        );
        return nullptr;
    }

    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure || !m_context.accelerationStructureFeatureEnabled){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Enabled acceleration structure feature support is required to create ray tracing acceleration structures.")
        );
        return nullptr;
    }
    VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0u;
    if(!VulkanDetail::ConvertAccelStructBuildFlags(
        desc.buildFlags,
        vkBuildFlags,
        NWB_TEXT("create acceleration structure")
    ))
        return nullptr;

    auto* as = NewArenaObject<AccelStruct>(m_context.objectArena, m_context, desc.queueSharing);
    as->m_desc = desc;
    as->m_isTopLevelAtCreation = desc.isTopLevel;

    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    u64 accelStructSize = s_DefaultTopLevelASBufferSize;
    if(desc.isTopLevel && desc.topLevelMaxInstances > 0){
        if(desc.topLevelMaxInstances > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: TLAS instance capacity exceeds Vulkan limit"));
            DestroyArenaObject(m_context.objectArena, as);
            return nullptr;
        }

        auto geometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;

        auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = vkBuildFlags;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        u32 primitiveCount = static_cast<u32>(desc.topLevelMaxInstances);
        auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
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

        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);
        VulkanDetail::BlasGeometryScratch blasScratch(scratchArena);
        const usize geometryCount = desc.bottomLevelGeometries.size();
        blasScratch.resizeForSizeQuery(geometryCount);

        using OpacityMicromapGeometryVector = Vector<VkAccelerationStructureTrianglesOpacityMicromapEXT, Alloc::ScratchArena>;
        using OpacityMicromapUsageVector = Vector<VkMicromapUsageEXT, Alloc::ScratchArena>;
        using OpacityMicromapUsageOffsetVector = Vector<usize, Alloc::ScratchArena>;

        OpacityMicromapGeometryVector opacityMicromapGeometries(geometryCount, scratchArena);
        OpacityMicromapUsageOffsetVector opacityMicromapUsageOffsets(geometryCount, scratchArena);
        OpacityMicromapUsageVector opacityMicromapUsageCounts(scratchArena);

        usize totalOpacityMicromapUsageCount = 0u;
        for(usize i = 0; i < geometryCount; ++i){
            opacityMicromapUsageOffsets[i] = Limit<usize>::s_Max;
            if(desc.bottomLevelGeometries[i].geometryType != RayTracingGeometryType::Triangles)
                continue;

            const RayTracingGeometryTriangles& triangles = desc.bottomLevelGeometries[i].geometryData.triangles;
            if(!triangles.opacityMicromap){
                if(
                    triangles.ommIndexBuffer
                    || triangles.ommIndexBufferOffset != 0u
                    || triangles.pOmmUsageCounts
                    || triangles.numOmmUsageCounts != 0u
                    || triangles.ommIndexFormat != Format::UNKNOWN
                ){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM metadata requires an opacity micromap")
                    );
                    DestroyArenaObject(m_context.objectArena, as);
                    return nullptr;
                }
                continue;
            }
            if(!m_context.extensions.EXT_opacity_micromap || !m_context.opacityMicromapFeatureEnabled){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM geometry requires VK_EXT_opacity_micromap")
                );
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }
            if(triangles.numOmmUsageCounts != 0u && !triangles.pOmmUsageCounts){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM usage counts are null"));
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }
            if(totalOpacityMicromapUsageCount > Limit<usize>::s_Max - triangles.numOmmUsageCounts){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM usage-count storage overflows"));
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }

            OpacityMicromap* const opacityMicromap = triangles.opacityMicromap;
            if(&opacityMicromap->m_context != &m_context){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to create BLAS: triangle opacity micromap belongs to another device")
                );
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }
            constexpr VkBufferUsageFlags s_MicromapStorageUsage =
                VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            ;
            Buffer* const micromapStorage = opacityMicromap->m_dataBuffer.get();
            if(!isBufferReadyForGpuUse(micromapStorage, s_MicromapStorageUsage)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle opacity micromap storage is not ready"));
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }
            const BufferDesc& micromapStorageDesc = micromapStorage->getCreationDescription();
            if(
                !micromapStorageDesc.isAccelStructStorage
                || opacityMicromap->m_micromap == VK_NULL_HANDLE
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle opacity micromap is invalid"));
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }

            VkIndexType ommIndexType = VK_INDEX_TYPE_NONE_KHR;
            u64 ommIndexStride = 0u;
            if(triangles.ommIndexFormat == Format::R16_UINT){
                ommIndexType = VK_INDEX_TYPE_UINT16;
                ommIndexStride = sizeof(u16);
            }
            else if(triangles.ommIndexFormat == Format::R32_UINT){
                ommIndexType = VK_INDEX_TYPE_UINT32;
                ommIndexStride = sizeof(u32);
            }
            else if(triangles.ommIndexFormat != Format::UNKNOWN){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM index format must be UNKNOWN, R16_UINT, or R32_UINT")
                );
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }

            VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
            ommGeometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureTrianglesOpacityMicromapEXT>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT
            );
            ommGeometry.indexType = ommIndexType;
            ommGeometry.indexStride = ommIndexStride;
            ommGeometry.baseTriangle = 0u;
            ommGeometry.usageCountsCount = triangles.numOmmUsageCounts;
            ommGeometry.micromap = opacityMicromap->m_micromap;

            opacityMicromapUsageOffsets[i] = totalOpacityMicromapUsageCount;
            totalOpacityMicromapUsageCount += triangles.numOmmUsageCounts;
        }
        opacityMicromapUsageCounts.resize(totalOpacityMicromapUsageCount);

        for(usize i = 0; i < geometryCount; ++i){
            if(
                !VulkanDetail::FillBlasGeometryForSizeQuery(
                    m_context,
                    desc.bottomLevelGeometries[i],
                    blasScratch.geometries[i],
                    blasScratch.spheresData[i],
                    blasScratch.lssData[i],
                    blasScratch.primitiveCounts[i],
                    NWB_TEXT("create BLAS"),
                    false
                )
            ){
                DestroyArenaObject(m_context.objectArena, as);
                return nullptr;
            }

            if(opacityMicromapUsageOffsets[i] != Limit<usize>::s_Max){
                const RayTracingGeometryTriangles& triangles = desc.bottomLevelGeometries[i].geometryData.triangles;
                const usize usageOffset = opacityMicromapUsageOffsets[i];
                for(u32 usageIndex = 0u; usageIndex < triangles.numOmmUsageCounts; ++usageIndex){
                    const RayTracingOpacityMicromapUsageCount& usage = triangles.pOmmUsageCounts[usageIndex];
                    VkOpacityMicromapFormatEXT format = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_KHR;
                    if(usage.format == OpacityMicromapFormat::OC1_2_State)
                        format = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
                    else if(usage.format == OpacityMicromapFormat::OC1_4_State)
                        format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
                    else{
                        NWB_LOGGER_ERROR(
                            NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM usage count has an invalid format")
                        );
                        DestroyArenaObject(m_context.objectArena, as);
                        return nullptr;
                    }
                    const u32 maxSubdivisionLevel = format == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
                        ? triangles.opacityMicromap->m_maxOpacity2StateSubdivisionLevel
                        : triangles.opacityMicromap->m_maxOpacity4StateSubdivisionLevel
                    ;
                    if(usage.subdivisionLevel > maxSubdivisionLevel){
                        NWB_LOGGER_ERROR(
                            NWB_TEXT("Vulkan: Failed to create BLAS: triangle OMM subdivision level {} exceeds device limit {}")
                            , usage.subdivisionLevel
                            , maxSubdivisionLevel
                        );
                        DestroyArenaObject(m_context.objectArena, as);
                        return nullptr;
                    }

                    VkMicromapUsageEXT& vkUsage = opacityMicromapUsageCounts[usageOffset + usageIndex];
                    vkUsage.count = usage.count;
                    vkUsage.subdivisionLevel = usage.subdivisionLevel;
                    vkUsage.format = format;
                }
            }
        }

        for(usize i = 0; i < geometryCount; ++i){
            if(opacityMicromapUsageOffsets[i] == Limit<usize>::s_Max)
                continue;

            VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
            const u32 usageCount = desc.bottomLevelGeometries[i].geometryData.triangles.numOmmUsageCounts;
            ommGeometry.pUsageCounts = usageCount != 0u
                ? opacityMicromapUsageCounts.data() + opacityMicromapUsageOffsets[i]
                : nullptr
            ;
            blasScratch.geometries[i].geometry.triangles.pNext = &ommGeometry;
        }

        auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = vkBuildFlags;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<u32>(blasScratch.geometries.size());
        buildInfo.pGeometries = blasScratch.geometries.data();

        auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, blasScratch.primitiveCounts.data(), &sizeInfo);
        if(sizeInfo.accelerationStructureSize > 0)
            accelStructSize = sizeInfo.accelerationStructureSize;
    }

    BufferDesc bufferDesc;
    bufferDesc.byteSize = accelStructSize;
    bufferDesc.isAccelStructStorage = true;
    // Identify the backing allocation with its acceleration structure.
    bufferDesc.debugName = desc.debugName;
    bufferDesc.isVirtual = desc.isVirtual;
    bufferDesc.queueSharing = desc.queueSharing;

    as->m_buffer = createBuffer(bufferDesc);
    if(!as->m_buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate acceleration structure storage buffer"));
        DestroyArenaObject(m_context.objectArena, as);
        return nullptr;
    }

    auto createInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
    createInfo.buffer = as->m_buffer->m_buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;

    res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->m_accelStruct);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, as);
        return nullptr;
    }

    auto addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
    addressInfo.accelerationStructure = as->m_accelStruct;
    if(!desc.isVirtual){
        constexpr VkBufferUsageFlags s_RequiredStorageUsage =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        ;
        if(!isBufferReadyForGpuUse(as->m_buffer.get(), s_RequiredStorageUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: storage buffer is not ready for device-address access"));
            DestroyArenaObject(m_context.objectArena, as);
            return nullptr;
        }
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
    }

    return RayTracingAccelStructHandle(as, RayTracingAccelStructHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

MemoryRequirements Device::getAccelStructMemoryRequirements(RayTracingAccelStruct* accelStructResource){
    if(!accelStructResource){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to get acceleration structure memory requirements: acceleration structure is null")
        );
        return {};
    }

    AccelStruct& accelerationStructure = *accelStructResource;
    if(&accelerationStructure.m_context != &m_context){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to get acceleration structure memory requirements: resource belongs to another device")
        );
        return {};
    }
    if(!accelerationStructure.m_desc.isVirtual || !accelerationStructure.m_buffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to get acceleration structure memory requirements: resource is not virtual")
        );
        return {};
    }

    NWB_ASSERT(accelerationStructure.getDeviceGeneration()
        == accelerationStructure.m_buffer->getDeviceGeneration());
    MemoryRequirements requirements = getBufferMemoryRequirements(accelerationStructure.m_buffer.get());
    if(requirements.size == 0u || requirements.alignment == 0u)
        return {};

    requirements.alignment = Max<u64>(requirements.alignment, s_AccelerationStructureAlignment);
    return requirements;
}

RayTracingClusterOperationSizeInfo Device::getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params){
    RayTracingClusterOperationSizeInfo info{};

    if(
        !m_context.extensions.NV_cluster_acceleration_structure
        || !m_context.clusterAccelerationStructureFeatureEnabled
        || !vkGetClusterAccelerationStructureBuildSizesNV
    )
        return info;
    constexpr u32 s_SupportedOperationFlags =
        static_cast<u32>(RayTracingClusterOperationFlags::FastTrace)
        | static_cast<u32>(RayTracingClusterOperationFlags::FastBuild)
        | static_cast<u32>(RayTracingClusterOperationFlags::NoOverlap)
        | static_cast<u32>(RayTracingClusterOperationFlags::AllowOMM)
    ;
    if(
        params.maxArgCount == 0u
        || (static_cast<u32>(params.flags) & ~s_SupportedOperationFlags) != 0u
        || (
            (params.flags & RayTracingClusterOperationFlags::FastTrace)
            && (params.flags & RayTracingClusterOperationFlags::FastBuild)
        )
        || (
            (params.flags & RayTracingClusterOperationFlags::NoOverlap)
            && params.type != RayTracingClusterOperationType::Move
        )
    )
        return info;
    if(
        (params.flags & RayTracingClusterOperationFlags::AllowOMM)
        && (
            !m_context.extensions.EXT_opacity_micromap
            || !m_context.opacityMicromapFeatureEnabled
        )
    )
        return info;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("query cluster operation sizes")))
        return info;

    if(
        params.type == RayTracingClusterOperationType::ClasBuild
        || params.type == RayTracingClusterOperationType::ClasBuildTemplates
        || params.type == RayTracingClusterOperationType::ClasInstantiateTemplates
    ){
        const auto& properties = m_context.nvClusterAccelerationStructureProperties;
        if(
            params.clas.maxTriangleCount > properties.maxTrianglesPerCluster
            || params.clas.maxVertexCount > properties.maxVerticesPerCluster
            || params.clas.maxGeometryIndex > properties.maxClusterGeometryIndex
            || params.clas.minPositionTruncateBitCount > 32u
        )
            return info;

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, clusterInput.vertexFormat, &formatProperties);
        if((formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) == 0u)
            return info;
    }

    auto vkSizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetClusterAccelerationStructureBuildSizesNV(m_context.device, &inputInfo, &vkSizeInfo);

    info.resultMaxSizeInBytes = vkSizeInfo.accelerationStructureSize;
    info.scratchSizeInBytes = vkSizeInfo.buildScratchSize;

    return info;
}

bool Device::bindAccelStructMemory(RayTracingAccelStruct* accelStructResource, Heap* heap, u64 offset){
    if(!accelStructResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: acceleration structure is null"));
        return false;
    }
    if(!heap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: heap is null"));
        return false;
    }

    auto* as = accelStructResource;
    ScopedLock resourceLock(as->m_memoryBindingMutex);
    if(as->m_buffer){
        if(!bindBufferMemory(as->m_buffer.get(), heap, offset))
            return false;

        constexpr VkBufferUsageFlags s_RequiredStorageUsage =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        ;
        if(!isBufferReadyForGpuUse(as->m_buffer.get(), s_RequiredStorageUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: storage buffer is not ready for device-address access"));
            return false;
        }

        auto addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = as->m_accelStruct;
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
        if(as->m_deviceAddress != 0u)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: device address is null"));
        return false;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: storage buffer is null"));
    return false;
}

bool Device::isAccelStructReadyForGpuUse(RayTracingAccelStruct* accelStructResource)const noexcept{
    if(!accelStructResource)
        return false;

    const AccelStruct& accelerationStructure = *accelStructResource;
    ScopedLock resourceLock(accelerationStructure.m_memoryBindingMutex);
    Buffer* const backingBuffer = accelerationStructure.m_buffer.get();
    return &accelerationStructure.m_context == &m_context
        && accelerationStructure.m_accelStruct != VK_NULL_HANDLE
        && accelerationStructure.m_deviceAddress != 0u
        && accelerationStructure.queueSharingMatchesCreation()
        && backingBuffer
        && backingBuffer->getDeviceGeneration() == m_context.deviceGeneration
        && backingBuffer->getCreationDescription().queueSharing == accelerationStructure.m_creationQueueSharing
        && isBufferReadyForGpuUse(
            backingBuffer,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

