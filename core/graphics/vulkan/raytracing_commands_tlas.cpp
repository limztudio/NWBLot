// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildTopLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags){
    constexpr TStringView s_BuildTlasOperation = NWB_TEXT("build TLAS");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("build top-level acceleration structure")))
        return;
    if(!accelStructResource){
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

    if(!m_context.extensions.khrAccelerationStructure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto& as = *MakeNotNull(accelStructResource);
    if(!as.m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure is not top-level"));
        return;
    }
    const auto vkBuildFlagsResult = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, s_BuildTlasOperation);
    if(!vkBuildFlagsResult)
        return;
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags = *vkBuildFlagsResult;
    const bool allowEmptyInstances = (buildFlags & RayTracingAccelStructBuildFlags::AllowEmptyInstances) != 0u;
    for(usize i = 0u; i < numInstances; ++i){
        auto* blas = pInstances[i].bottomLevelAS;
        if(!blas){
            if(allowEmptyInstances)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} has a null bottom-level acceleration structure"), i);
            return;
        }
        Buffer* const backingBuffer = blas->getBackingBuffer();
        if(blas->m_isTopLevelAtCreation || !m_device.isAccelStructReadyForGpuUse(blas) || !backingBuffer || !isBufferAdmittedToCommandQueue(*backingBuffer)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} references an invalid bottom-level acceleration structure"), i);
            return;
        }
        if(pInstances[i].instanceMask == 0u && !allowEmptyInstances){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} has a zero mask"), i);
            return;
        }
    }

    const auto instanceDataSizeResult = TryMultiply<u64>(static_cast<u64>(numInstances), sizeof(VkAccelerationStructureInstanceKHR));
    if(!instanceDataSizeResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance buffer size overflows"));
        return;
    }
    const u64 instanceDataSize = *instanceDataSizeResult;
    const auto instanceInput = suballocateBuildInput(instanceDataSize, s_TlasInstanceDataAlignment, NWB_TEXT("allocate TLAS instance input"));
    if(!instanceInput)
        return;
    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceInput->buffer.get(), instanceInput->offset);
    const auto mappedInstanceData = MakeNotNull(static_cast<u8*>(instanceInput->cpuAddress));

    auto buildVkInstance = [&](usize i)noexcept{
        const auto& inst = pInstances[i];
        VkAccelerationStructureInstanceKHR vkInst = {};

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

        auto* blas = inst.bottomLevelAS;
        vkInst.accelerationStructureReference = blas ? blas->m_deviceAddress : 0;

        const u64 destinationOffset = static_cast<u64>(i) * sizeof(VkAccelerationStructureInstanceKHR);
        NWB_MEMCPY(mappedInstanceData.get() + static_cast<usize>(destinationOffset), sizeof(VkAccelerationStructureInstanceKHR), &vkInst, sizeof(VkAccelerationStructureInstanceKHR));
    };

    if(taskScheduler().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold){
        const auto parallelWork = taskScheduler().parallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
        if(!parallelWork){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: acceleration-structure preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
            m_commandRecordingFailed = true;
            return;
        }
    }
    else{
        for(usize i = 0u; i < numInstances; ++i)
            buildVkInstance(i);
    }

    if(!buildTopLevelAccelStructFromInstanceData(as, instanceDataAddress, numInstances, buildFlags, vkBuildFlags, s_BuildTlasOperation, pInstances))
        return;
}

void CommandList::buildTopLevelAccelStructFromBuffer(RayTracingAccelStruct* accelStructResource, Buffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags){
    constexpr TStringView s_BuildTlasFromBufferOperation = NWB_TEXT("build TLAS from buffer");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("build top-level acceleration structure from buffer")))
        return;
    if(!accelStructResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is null"));
        return;
    }
    if(!instanceBuffer && numInstances > 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is null"));
        return;
    }
    if(numInstances == 0)
        return;
    if(numInstances > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance count exceeds Vulkan limit"));
        return;
    }

    if(!m_context.extensions.khrAccelerationStructure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto& as = *MakeNotNull(accelStructResource);
    if(!as.m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is not top-level"));
        return;
    }
    const auto vkBuildFlagsResult = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, s_BuildTlasFromBufferOperation);
    if(!vkBuildFlagsResult)
        return;
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags = *vkBuildFlagsResult;

    auto& instanceBufferImpl = *MakeNotNull(instanceBuffer);
    constexpr VkBufferUsageFlags s_BuildInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(!isBufferReadyForCommandQueue(&instanceBufferImpl, s_BuildInputUsage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is foreign or not ready"));
        return;
    }
    const BufferDesc& instanceBufferCreationDesc = instanceBufferImpl.getCreationDescription();
    if(!instanceBufferCreationDesc.isAccelStructBuildInput){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer was not created with acceleration-structure build input usage"));
        return;
    }

    const u64 instanceDataBytes = static_cast<u64>(numInstances) * sizeof(VkAccelerationStructureInstanceKHR);
    if(!VulkanDetail::IsBufferRangeInBounds(instanceBufferCreationDesc, instanceBufferOffset, instanceDataBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer range is outside the buffer"));
        return;
    }

    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceBuffer, instanceBufferOffset);
    if(!buildTopLevelAccelStructFromInstanceData(as, instanceDataAddress, numInstances, buildFlags, vkBuildFlags, s_BuildTlasFromBufferOperation))
        return;

    retainResource(instanceBuffer);
}

bool CommandList::buildTopLevelAccelStructFromInstanceData(
    RayTracingAccelStruct& as,
    const VkDeviceAddress instanceDataAddress,
    const usize numInstances,
    const RayTracingAccelStructBuildFlags::Mask buildFlags,
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags,
    TStringView operationName,
    const RayTracingInstanceDesc* /*instances*/
){
    Buffer* const backingBuffer = as.getBackingBuffer();
    if(!m_device.isAccelStructReadyForGpuUse(&as) || !backingBuffer || !isBufferAdmittedToCommandQueue(*backingBuffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is not ready"), operationName);
        return false;
    }
    if(instanceDataAddress == 0u || instanceDataAddress % s_TlasInstanceDataAlignment != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: instance data device address must be 16-byte aligned"), operationName);
        return false;
    }

    const bool performUpdate = (buildFlags & RayTracingAccelStructBuildFlags::PerformUpdate) != 0u;
    if(performUpdate && !(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: PerformUpdate requires AllowUpdate"), operationName);
        return false;
    }
    if(performUpdate && !(as.m_desc.buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure creation did not allow updates"), operationName);
        return false;
    }

    auto geometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceDataAddress;

    const auto primitiveCount = static_cast<u32>(numInstances);
    const AccelStructGeometryBuildSignature geometrySignature = VulkanDetail::MakeAccelStructGeometryBuildSignature(geometry, nullptr, nullptr, primitiveCount, false);
    const auto hasPriorBuildResult = validateAccelStructBuildSignature(as, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, vkBuildFlags, &geometrySignature, 1u, performUpdate, operationName);
    if(!hasPriorBuildResult)
        return false;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = vkBuildFlags;
    buildInfo.mode = performUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.srcAccelerationStructure = performUpdate ? as.m_accelStruct : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = as.m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    m_context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    auto* asBuffer = as.m_buffer.get();
    if(!asBuffer || asBuffer->getCreationDescription().byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is too small"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure storage is too small"));
        return false;
    }

    const VkDeviceSize scratchSize = performUpdate ? sizeInfo.updateScratchSize : sizeInfo.buildScratchSize;
    const u64 scratchAlignment = Max<u64>(static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment), 1u);
    const auto scratchAddressResult = suballocateBuildScratchAddress(scratchSize, scratchAlignment, NWB_TEXT("allocate TLAS scratch buffer"));
    if(!scratchAddressResult)
        return false;
    buildInfo.scratchData.deviceAddress = *scratchAddressResult;

    // Order BLAS writes and prior TLAS writes before this TLAS build.
    {
        auto reuseBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        reuseBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        reuseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        executePipelineBarrier({ &reuseBarrier, 1u }, {}, {});
    }
    if(m_commandRecordingFailed)
        return false;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    if(!m_currentCmdBuf->appendPendingAccelStructBuildCommit(as, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, vkBuildFlags, &geometrySignature, 1u)){
        rejectCommandRecording(operationName, VulkanDetail::s_RetainBuildSignatureFailedReason);
        return false;
    }
    m_context.deviceDispatch.vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

