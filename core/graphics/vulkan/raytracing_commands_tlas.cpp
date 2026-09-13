// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildTopLevelAccelStructFromBuffer(
    RayTracingAccelStruct* accelStructResource,
    Buffer* instanceBuffer,
    u64 instanceBufferOffset,
    usize numInstances,
    RayTracingAccelStructBuildFlags::Mask buildFlags
){
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

    if(!m_context.extensions.KHR_acceleration_structure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is not top-level"));
        return;
    }
    VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0u;
    if(!VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, vkBuildFlags, NWB_TEXT("build TLAS from buffer")))
        return;

    auto* instanceBufferImpl = instanceBuffer;
    if(!instanceBufferImpl){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is invalid"));
        return;
    }
    constexpr VkBufferUsageFlags s_BuildInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
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
    if(!buildTopLevelAccelStructFromInstanceData(
        *as,
        instanceDataAddress,
        numInstances,
        buildFlags,
        vkBuildFlags,
        NWB_TEXT("build TLAS from buffer")
    ))
        return;

    retainResource(instanceBuffer);
}

void CommandList::buildTopLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags){
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

    if(!m_context.extensions.KHR_acceleration_structure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure is not top-level"));
        return;
    }
    VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0u;
    if(!VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, vkBuildFlags, NWB_TEXT("build TLAS")))
        return;
    const bool allowEmptyInstances = (buildFlags & RayTracingAccelStructBuildFlags::AllowEmptyInstances) != 0u;
    for(usize i = 0; i < numInstances; ++i){
        auto* blas = pInstances[i].bottomLevelAS;
        if(!blas){
            if(allowEmptyInstances)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} has a null bottom-level acceleration structure"), i);
            return;
        }
        Buffer* const backingBuffer = blas->getBackingBuffer();
        if(
            blas->m_isTopLevelAtCreation
            || !m_device.isAccelStructReadyForGpuUse(blas)
            || !backingBuffer
            || !isBufferAdmittedToCommandQueue(*backingBuffer)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} references an invalid bottom-level acceleration structure"), i);
            return;
        }
        if(pInstances[i].instanceMask == 0u && !allowEmptyInstances){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance {} has a zero mask"), i);
            return;
        }
    }

    constexpr u64 s_InstanceDataPadding = s_TlasInstanceDataAlignment - 1u;
    u64 instanceDataSize = 0u;
    if(
        !TryMultiply<u64>(static_cast<u64>(numInstances), sizeof(VkAccelerationStructureInstanceKHR), instanceDataSize)
        || instanceDataSize > Limit<u64>::s_Max - s_InstanceDataPadding
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance buffer size overflows"));
        return;
    }
    const u64 instanceBufferSize = instanceDataSize + s_InstanceDataPadding;
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
    if(!isBufferReadyForCommandQueue(
        instanceBuffer.get(),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: TLAS instance buffer is not ready for device-address access"));
        return;
    }

    const VkDeviceAddress instanceBufferAddress = VulkanDetail::GetBufferDeviceAddress(instanceBuffer.get());
    VkDeviceAddress instanceDataAddress = 0u;
    if(
        instanceBufferAddress == 0u
        || !AlignUpChecked(instanceBufferAddress, s_TlasInstanceDataAlignment, instanceDataAddress)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: instance buffer device address is null or cannot be aligned"));
        return;
    }
    const u64 instanceBufferOffset = instanceDataAddress - instanceBufferAddress;
    const u64 actualInstanceBufferSize = instanceBuffer->getCreationDescription().byteSize;
    if(
        instanceBufferOffset > actualInstanceBufferSize
        || instanceDataSize > actualInstanceBufferSize - instanceBufferOffset
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: aligned instance data range is outside the buffer"));
        return;
    }

    auto* mappedInstanceData = static_cast<u8*>(m_device.mapBuffer(*instanceBuffer, CpuAccessMode::Write));
    if(!mappedInstanceData){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map TLAS instance buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map TLAS instance buffer"));
        return;
    }

    auto buildVkInstance = [&](usize i){
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

        const u64 destinationOffset = instanceBufferOffset + static_cast<u64>(i) * sizeof(VkAccelerationStructureInstanceKHR);
        NWB_MEMCPY(
            mappedInstanceData + static_cast<usize>(destinationOffset),
            sizeof(VkAccelerationStructureInstanceKHR),
            &vkInst,
            sizeof(VkAccelerationStructureInstanceKHR)
        );
    };

    if(taskScheduler().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold)
        taskScheduler().parallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
    else{
        for(usize i = 0; i < numInstances; ++i)
            buildVkInstance(i);
    }

    m_device.unmapBuffer(*instanceBuffer);

    if(!buildTopLevelAccelStructFromInstanceData(
        *as,
        instanceDataAddress,
        numInstances,
        buildFlags,
        vkBuildFlags,
        NWB_TEXT("build TLAS")
    ))
        return;

    for(usize i = 0; i < numInstances; ++i)
        retainResource(pInstances[i].bottomLevelAS);

    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(instanceBuffer));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

