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
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_context(context)
{}
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
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Acceleration structure extension is required to create ray tracing acceleration structures."));
        return nullptr;
    }

    auto* as = NewArenaObject<AccelStruct>(m_context.objectArena, m_context);
    as->m_desc = desc;

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
        buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(desc.buildFlags);
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
        }

        auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(desc.buildFlags);
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
    if(!desc.isVirtual)
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);

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

    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return info;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("query cluster operation sizes")))
        return info;

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

        auto addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = as->m_accelStruct;
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
        return true;
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
        && backingBuffer
        && backingBuffer->getDeviceGeneration() == m_context.deviceGeneration
        && isBufferReadyForGpuUse(backingBuffer)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

