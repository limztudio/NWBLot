// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "blas_opacity_micromap_policy.h"
#include "cluster_operation_input.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AccelStruct::AccelStruct(const VulkanContext& context, const ResourceQueueSharing::Mask creationQueueSharing)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_desc(context.objectArena)
    , m_context(context)
    , m_creationQueueSharing(creationQueueSharing)
{
    m_desc.queueSharing = creationQueueSharing;
}

AccelStruct::~AccelStruct()noexcept{
    collectRetiredBuildSignatureRoles();
    if(m_acceptedBuildSignatureRole){
        DestroyArenaObjectNoexcept(m_context.objectArena, m_acceptedBuildSignatureRole);
        m_acceptedBuildSignatureRole = nullptr;
    }

    if(m_accelStruct){
        m_context.deviceDispatch.vkDestroyAccelerationStructureKHR(m_context.device, m_accelStruct, m_context.allocationCallbacks);
        m_accelStruct = VK_NULL_HANDLE;
    }

    m_buffer.reset();
}

Object AccelStruct::getNativeHandle(ObjectType objectType)noexcept{
    if(objectType == ObjectTypes::s_AccelerationStructureKHR)
        return Object(m_accelStruct);
    return Object(nullptr);
}

GlobalUniquePtr<AccelStructBuildSignatureRole> AccelStruct::acquireBuildSignatureRole(){
    {
        NothrowScopedLock lock(m_acceptedBuildSignatureMutex);
        if(AccelStructBuildSignatureRole* const role = m_retiredBuildSignatureRoles){
            m_retiredBuildSignatureRoles = role->nextRetiredRole;
            role->nextRetiredRole = nullptr;
            return GlobalUniquePtr<AccelStructBuildSignatureRole>(role, GlobalUniquePtr<AccelStructBuildSignatureRole>::deleter_type(m_context.objectArena));
        }
    }
    return MakeGlobalUnique<AccelStructBuildSignatureRole>(m_context.objectArena, m_context.objectArena);
}

void AccelStruct::collectRetiredBuildSignatureRoles()noexcept{
    AccelStructBuildSignatureRole* retiredRoles = nullptr;
    {
        NothrowScopedLock lock(m_acceptedBuildSignatureMutex);
        retiredRoles = m_retiredBuildSignatureRoles;
        m_retiredBuildSignatureRoles = nullptr;
    }

    while(retiredRoles){
        AccelStructBuildSignatureRole* const nextRole = retiredRoles->nextRetiredRole;
        retiredRoles->nextRetiredRole = nullptr;
        DestroyArenaObjectNoexcept(m_context.objectArena, retiredRoles);
        retiredRoles = nextRole;
    }
}

void AccelStruct::retireBuildSignatureRole(AccelStructBuildSignatureRole& role)noexcept{
    role.accelStructType = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
    role.buildFlags = 0u;
    role.geometrySignatures.clear();
    if(role.bottomLevelReferences){
        if(role.bottomLevelReferences->getReferenceCount() == 1u)
            role.bottomLevelReferences->resources.clear();
        else
            role.bottomLevelReferences = nullptr;
    }

    NothrowScopedLock lock(m_acceptedBuildSignatureMutex);
    role.nextRetiredRole = m_retiredBuildSignatureRoles;
    m_retiredBuildSignatureRoles = &role;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RayTracingAccelStructHandle> Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    if(!ResourceQueueSharing::IsValid(desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: queue sharing contains unknown bits"));
        return MakeUnexpected(Failure{});
    }

    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.khrAccelerationStructure || !m_context.accelerationStructureFeatureEnabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Enabled acceleration structure feature support is required to create ray tracing acceleration structures."));
        return MakeUnexpected(Failure{});
    }
    const auto vkBuildFlagsResult = VulkanDetail::ConvertAccelStructBuildFlags(desc.buildFlags, NWB_TEXT("create acceleration structure"));
    if(!vkBuildFlagsResult)
        return MakeUnexpected(Failure{});
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags = *vkBuildFlagsResult;

    RayTracingAccelStructHandle as(NewArenaObject<AccelStruct>(m_context.objectArena, m_context, desc.queueSharing), RayTracingAccelStructHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!as)
        return MakeUnexpected(Failure{});
    as->m_desc = desc;
    as->m_isTopLevelAtCreation = desc.isTopLevel;

    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    u64 accelStructSize = s_DefaultTopLevelASBufferSize;
    if(desc.isTopLevel && desc.topLevelMaxInstances > 0){
        if(desc.topLevelMaxInstances > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: TLAS instance capacity exceeds Vulkan limit"));
            return MakeUnexpected(Failure{});
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
        m_context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);
        if(sizeInfo.accelerationStructureSize > 0)
            accelStructSize = sizeInfo.accelerationStructureSize;
    }
    else if(!desc.isTopLevel && !desc.bottomLevelGeometries.empty()){
        if(desc.bottomLevelGeometries.size() > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: geometry count exceeds Vulkan limit"));
            return MakeUnexpected(Failure{});
        }

        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);
        VulkanDetail::BlasGeometryScratch blasScratch(scratchArena);
        const usize geometryCount = desc.bottomLevelGeometries.size();
        blasScratch.resizeForSizeQuery(geometryCount);

        constexpr TStringView s_CreateBlasOperation = NWB_TEXT("create BLAS");
        VulkanDetail::OpacityMicromapGeometryVector opacityMicromapGeometries(geometryCount, scratchArena);
        VulkanDetail::OpacityMicromapUsageOffsetVector opacityMicromapUsageOffsets(geometryCount, scratchArena);
        VulkanDetail::OpacityMicromapUsageVector opacityMicromapUsageCounts(scratchArena);
        b8 hasOpacityMicromap = false;
        if(!VulkanDetail::BlasOpacityMicromapPolicy::prepareBlasOpacityMicromapUsageOffsets(m_context, desc.bottomLevelGeometries.data(), geometryCount, s_CreateBlasOperation, opacityMicromapUsageOffsets, opacityMicromapUsageCounts, hasOpacityMicromap))
            return MakeUnexpected(Failure{});

        for(usize i = 0u; i < geometryCount; ++i){
            const auto geometry = VulkanDetail::FillBlasGeometryForSizeQuery(m_context, desc.bottomLevelGeometries[i], s_CreateBlasOperation, false);
            if(!geometry)
                return MakeUnexpected(Failure{});
            blasScratch.assignGeometry(i, *geometry);

            if(opacityMicromapUsageOffsets[i] != Limit<usize>::s_Max){
                const RayTracingGeometryTriangles& triangles = desc.bottomLevelGeometries[i].geometryData.triangles;
                OpacityMicromap* const opacityMicromap = triangles.opacityMicromap;
                if(&opacityMicromap->m_context != &m_context){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle opacity micromap belongs to another device"));
                    return MakeUnexpected(Failure{});
                }
                if(!isBufferReadyForGpuUse(opacityMicromap->m_dataBuffer.get(), VulkanDetail::s_MicromapStorageUsage)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create BLAS: triangle opacity micromap storage is not ready"));
                    return MakeUnexpected(Failure{});
                }
                const auto ommIndexFormat = VulkanDetail::BlasOpacityMicromapPolicy::resolveOpacityMicromapIndexFormat(triangles.ommIndexFormat, s_CreateBlasOperation);
                if(!ommIndexFormat)
                    return MakeUnexpected(Failure{});
                if(!VulkanDetail::BlasOpacityMicromapPolicy::validateOpacityMicromapStorageAndFillUsages(
                    triangles,
                    ommIndexFormat->indexType,
                    ommIndexFormat->indexStride,
                    0u,
                    opacityMicromapUsageOffsets[i],
                    s_CreateBlasOperation,
                    opacityMicromapUsageCounts,
                    opacityMicromapGeometries[i]
                )){
                    return MakeUnexpected(Failure{});
                }
            }
        }

        VulkanDetail::BlasOpacityMicromapPolicy::attachBlasOpacityMicromapGeometries(desc.bottomLevelGeometries.data(), geometryCount, opacityMicromapUsageOffsets, opacityMicromapUsageCounts, opacityMicromapGeometries, blasScratch);

        auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
        buildInfo.type = asType;
        buildInfo.flags = vkBuildFlags;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<u32>(blasScratch.geometries.size());
        buildInfo.pGeometries = blasScratch.geometries.data();

        auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        m_context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, blasScratch.primitiveCounts.data(), &sizeInfo);
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

    auto bufferResult = createBuffer(bufferDesc);
    if(!bufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate acceleration structure storage buffer"));
        return MakeUnexpected(Failure{});
    }
    as->m_buffer = Move(*bufferResult);

    auto createInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureCreateInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
    createInfo.buffer = as->m_buffer->m_buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;

    res = m_context.deviceDispatch.vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->m_accelStruct);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    auto addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
    addressInfo.accelerationStructure = as->m_accelStruct;
    if(!desc.isVirtual){
        constexpr VkBufferUsageFlags s_RequiredStorageUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if(!isBufferReadyForGpuUse(as->m_buffer.get(), s_RequiredStorageUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acceleration structure: storage buffer is not ready for device-address access"));
            return MakeUnexpected(Failure{});
        }
        as->m_deviceAddress = m_context.deviceDispatch.vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
    }

    constexpr usize s_InitialBuildSignatureRoleCount = VulkanDefaults::s_VulkanDefaultInitialBuildSignatureRoleCount;
    for(usize roleIndex = 0u; roleIndex < s_InitialBuildSignatureRoleCount; ++roleIndex){
        GlobalUniquePtr<AccelStructBuildSignatureRole> role = MakeGlobalUnique<AccelStructBuildSignatureRole>(m_context.objectArena, m_context.objectArena);
        if(!role)
            return MakeUnexpected(Failure{});
        if(desc.isTopLevel){
            role->geometrySignatures.reserve(1u);
            if(desc.topLevelMaxInstances > 0u){
                auto* const references = NewArenaObject<AccelStructBottomLevelReferenceControl>(m_context.objectArena, m_context.objectArena);
                if(!references)
                    return MakeUnexpected(Failure{});
                role->bottomLevelReferences = AccelStructBottomLevelReferenceHandle(references, AccelStructBottomLevelReferenceHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
                references->resources.reserve(desc.topLevelMaxInstances);
            }
        }
        else if(!desc.bottomLevelGeometries.empty())
            role->geometrySignatures.reserve(desc.bottomLevelGeometries.size());
        as->retireBuildSignatureRole(*role.release());
    }

    return as;
}

Expected<MemoryRequirements> Device::getAccelStructMemoryRequirements(RayTracingAccelStruct& accelerationStructure){
    if(&accelerationStructure.m_context != &m_context){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get acceleration structure memory requirements: resource belongs to another device"));
        return MakeUnexpected(Failure{});
    }
    if(!accelerationStructure.m_desc.isVirtual || !accelerationStructure.m_buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get acceleration structure memory requirements: resource is not virtual"));
        return MakeUnexpected(Failure{});
    }

    NWB_ASSERT(accelerationStructure.getDeviceGeneration() == accelerationStructure.m_buffer->getDeviceGeneration());
    const auto requirements = getBufferMemoryRequirements(*accelerationStructure.m_buffer);
    if(!requirements)
        return MakeUnexpected(requirements.error());
    if(requirements->size == 0u || requirements->alignment == 0u)
        return MakeUnexpected(Failure{});

    MemoryRequirements alignedRequirements = *requirements;
    alignedRequirements.alignment = Max<u64>(alignedRequirements.alignment, s_AccelerationStructureAlignment);
    return alignedRequirements;
}

Expected<RayTracingClusterOperationSizeInfo> Device::getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params){
    return VulkanDetail::ClusterOperationQuery::query(m_context, params);
}

b8 Device::bindAccelStructMemory(RayTracingAccelStruct& accelerationStructure, Heap& heap, u64 offset){
    ScopedLock resourceLock(accelerationStructure.m_memoryBindingMutex);
    if(accelerationStructure.m_buffer){
        if(!bindBufferMemory(*accelerationStructure.m_buffer, heap, offset))
            return false;

        constexpr VkBufferUsageFlags s_RequiredStorageUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if(!isBufferReadyForGpuUse(accelerationStructure.m_buffer.get(), s_RequiredStorageUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: storage buffer is not ready for device-address access"));
            return false;
        }

        auto addressInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureDeviceAddressInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
        addressInfo.accelerationStructure = accelerationStructure.m_accelStruct;
        accelerationStructure.m_deviceAddress = m_context.deviceDispatch.vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
        if(accelerationStructure.m_deviceAddress != 0u)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: device address is null"));
        return false;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind acceleration structure memory: storage buffer is null"));
    return false;
}

b8 Device::isAccelStructReadyForGpuUse(RayTracingAccelStruct* accelStructResource)const noexcept{
    if(!accelStructResource)
        return false;

    const AccelStruct& accelerationStructure = *accelStructResource;
    NothrowScopedLock resourceLock(accelerationStructure.m_memoryBindingMutex);
    Buffer* const backingBuffer = accelerationStructure.m_buffer.get();
    return
        &accelerationStructure.m_context == &m_context
        && accelerationStructure.m_accelStruct != VK_NULL_HANDLE
        && accelerationStructure.m_deviceAddress != 0u
        && accelerationStructure.queueSharingMatchesCreation()
        && backingBuffer
        && backingBuffer->getDeviceGeneration() == m_context.deviceGeneration
        && backingBuffer->getCreationDescription().queueSharing == accelerationStructure.m_creationQueueSharing
        && isBufferReadyForGpuUse(backingBuffer, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

