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

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
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
    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceInput->buffer, instanceInput->offset);
    auto* mappedInstanceData = static_cast<u8*>(instanceInput->cpuAddress);

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
        NWB_MEMCPY(mappedInstanceData + static_cast<usize>(destinationOffset), sizeof(VkAccelerationStructureInstanceKHR), &vkInst, sizeof(VkAccelerationStructureInstanceKHR));
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

    if(!buildTopLevelAccelStructFromInstanceData(*as, instanceDataAddress, numInstances, buildFlags, vkBuildFlags, s_BuildTlasOperation))
        return;

    for(usize i = 0u; i < numInstances; ++i)
        retainResource(pInstances[i].bottomLevelAS);
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

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is not top-level"));
        return;
    }
    const auto vkBuildFlagsResult = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, s_BuildTlasFromBufferOperation);
    if(!vkBuildFlagsResult)
        return;
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags = *vkBuildFlagsResult;

    auto* instanceBufferImpl = instanceBuffer;
    if(!instanceBufferImpl){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is invalid"));
        return;
    }
    constexpr VkBufferUsageFlags s_BuildInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(!isBufferReadyForCommandQueue(instanceBufferImpl, s_BuildInputUsage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is foreign or not ready"));
        return;
    }
    const BufferDesc& instanceBufferCreationDesc = instanceBufferImpl->getCreationDescription();
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
    if(!buildTopLevelAccelStructFromInstanceData(*as, instanceDataAddress, numInstances, buildFlags, vkBuildFlags, s_BuildTlasFromBufferOperation))
        return;

    retainResource(instanceBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

