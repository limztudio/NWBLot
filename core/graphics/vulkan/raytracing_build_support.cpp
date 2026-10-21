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


AccelStructGeometryBuildSignature MakeAccelStructGeometryBuildSignature(
    const VkAccelerationStructureGeometryKHR& geometry,
    const VkAccelerationStructureGeometrySpheresDataNV* const spheresData,
    const VkAccelerationStructureGeometryLinearSweptSpheresDataNV* const lssData,
    const u32 primitiveCount,
    const bool transformDataPresent
)noexcept{
    AccelStructGeometryBuildSignature signature;
    signature.geometryType = geometry.geometryType;
    signature.geometryFlags = geometry.flags;
    signature.primitiveCount = primitiveCount;

    if(geometry.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR){
        signature.vertexFormat = geometry.geometry.triangles.vertexFormat;
        signature.indexType = geometry.geometry.triangles.indexType;
        signature.maxVertex = geometry.geometry.triangles.maxVertex;
        signature.transformDataPresent = transformDataPresent;
    }
    else if(geometry.geometryType == VK_GEOMETRY_TYPE_SPHERES_NV){
        if(spheresData){
            signature.vertexFormat = spheresData->vertexFormat;
            signature.radiusFormat = spheresData->radiusFormat;
            signature.indexType = spheresData->indexType;
            signature.vertexStride = spheresData->vertexStride;
            signature.radiusStride = spheresData->radiusStride;
            signature.indexStride = spheresData->indexStride;
        }
    }
    else if(geometry.geometryType == VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV){
        if(lssData){
            signature.vertexFormat = lssData->vertexFormat;
            signature.radiusFormat = lssData->radiusFormat;
            signature.indexType = lssData->indexType;
            signature.vertexStride = lssData->vertexStride;
            signature.radiusStride = lssData->radiusStride;
            signature.indexStride = lssData->indexStride;
            signature.lssIndexingMode = lssData->indexingMode;
            signature.lssEndCapsMode = lssData->endCapsMode;
        }
    }

    return signature;
}

bool AccelStructGeometryBuildSignaturesEqual(const AccelStructGeometryBuildSignature& previous, const AccelStructGeometryBuildSignature& current)noexcept{
    if(previous.geometryType != current.geometryType || previous.geometryFlags != current.geometryFlags || previous.primitiveCount != current.primitiveCount)
        return false;

    if(previous.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR){
        return previous.vertexFormat == current.vertexFormat && previous.indexType == current.indexType && previous.maxVertex == current.maxVertex && previous.transformDataPresent == current.transformDataPresent;
    }
    if(previous.geometryType == VK_GEOMETRY_TYPE_SPHERES_NV){
        return
            previous.vertexFormat == current.vertexFormat
            && previous.radiusFormat == current.radiusFormat
            && previous.indexType == current.indexType
            && previous.vertexStride == current.vertexStride
            && previous.radiusStride == current.radiusStride
            && previous.indexStride == current.indexStride
        ;
    }
    if(previous.geometryType == VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV){
        return
            previous.vertexFormat == current.vertexFormat
            && previous.radiusFormat == current.radiusFormat
            && previous.indexType == current.indexType
            && previous.vertexStride == current.vertexStride
            && previous.radiusStride == current.radiusStride
            && previous.indexStride == current.indexStride
            && previous.lssIndexingMode == current.lssIndexingMode
            && previous.lssEndCapsMode == current.lssEndCapsMode
        ;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<VkDeviceAddress> CommandList::suballocateBuildScratchAddress(const u64 buildScratchSize, const u64 scratchAlignment, TStringView operationName){
    if(buildScratchSize == 0u)
        return VkDeviceAddress{0u};
    if(!m_currentCmdBuf || m_nativeRecordingID == 0u || scratchAlignment == 0u){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch recording identity or alignment is invalid"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 scratchPadding = scratchAlignment - 1u;
    if(buildScratchSize > Limit<u64>::s_Max - scratchPadding){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch allocation size overflows"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 scratchAllocationSize = buildScratchSize + scratchPadding;

    const GpuPhysicalQueueId physicalQueue = m_currentCmdBuf->m_queue.m_physicalQueue;
    // Without a completed value, no retired chunk is reused.
    const Expected<u64> completedVersion = m_device.queueGetCompletedInstance(physicalQueue);
    const auto allocation = m_device.m_scratchManager.suballocateBuffer(scratchAllocationSize, m_currentCmdBuf.get(), m_nativeRecordingID, physicalQueue, completedVersion ? *completedVersion : 0u, 1u);
    if(!allocation){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch-buffer suballocation failed"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(!isBufferReadyForCommandQueue(allocation->buffer.get(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch buffer is not ready for device-address access"), operationName);
        return MakeUnexpected(Failure{});
    }

    const VkDeviceAddress scratchBaseAddress = VulkanDetail::GetBufferDeviceAddress(allocation->buffer.get(), allocation->offset);
    const auto alignedScratchAddress = AlignUpChecked(scratchBaseAddress, scratchAlignment);
    if(scratchBaseAddress == 0u || !alignedScratchAddress){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch device address is null or cannot be aligned"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 scratchOffset = *alignedScratchAddress - scratchBaseAddress;
    if(scratchOffset > scratchAllocationSize || buildScratchSize > scratchAllocationSize - scratchOffset){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: aligned scratch range is outside the buffer"), operationName);
        return MakeUnexpected(Failure{});
    }

    return *alignedScratchAddress;
}

Expected<BufferSuballocation> CommandList::suballocateBuildInput(const u64 byteCount, const u64 deviceAddressAlignment, TStringView operationName){
    if(!m_currentCmdBuf || m_nativeRecordingID == 0u || byteCount == 0u || deviceAddressAlignment == 0u){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: build-input recording identity, size, or alignment is invalid"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 inputPadding = deviceAddressAlignment - 1u;
    if(byteCount > Limit<u64>::s_Max - inputPadding){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: build-input allocation size overflows"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 inputAllocationSize = byteCount + inputPadding;

    const GpuPhysicalQueueId physicalQueue = m_currentCmdBuf->m_queue.m_physicalQueue;
    // Without a completed value, no retired chunk is reused.
    const Expected<u64> completedVersion = m_device.queueGetCompletedInstance(physicalQueue);
    const auto allocation = m_device.m_accelStructInputManager.suballocateBuffer(inputAllocationSize, m_currentCmdBuf.get(), m_nativeRecordingID, physicalQueue, completedVersion ? *completedVersion : 0u, 1u);
    if(!allocation){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: build-input suballocation failed"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(!allocation->cpuAddress || !isBufferReadyForCommandQueue(allocation->buffer.get(), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: build-input buffer is not mapped or not ready for device-address access"), operationName);
        return MakeUnexpected(Failure{});
    }

    const VkDeviceAddress inputBaseAddress = VulkanDetail::GetBufferDeviceAddress(allocation->buffer.get(), allocation->offset);
    const auto alignedInputAddress = AlignUpChecked(inputBaseAddress, deviceAddressAlignment);
    if(inputBaseAddress == 0u || !alignedInputAddress){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: build-input device address is null or cannot be aligned"), operationName);
        return MakeUnexpected(Failure{});
    }
    const u64 inputOffset = *alignedInputAddress - inputBaseAddress;
    if(inputOffset > inputAllocationSize || byteCount > inputAllocationSize - inputOffset){
        invalidateCommandRecording();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: aligned build-input range is outside the buffer"), operationName);
        return MakeUnexpected(Failure{});
    }

    retainStagingBuffer(*allocation->buffer);
    return BufferSuballocation{ allocation->buffer, allocation->offset + inputOffset, static_cast<u8*>(allocation->cpuAddress) + inputOffset };
}

Expected<bool> CommandList::validateAccelStructBuildSignature(
    AccelStruct& accelStruct,
    const VkAccelerationStructureTypeKHR accelStructType,
    const VkBuildAccelerationStructureFlagsKHR buildFlags,
    const AccelStructGeometryBuildSignature* const geometrySignatures,
    const usize geometrySignatureCount,
    const bool performUpdate,
    const TStringView operationName
){
    accelStruct.collectRetiredBuildSignatureRoles();

    const auto validatePriorSignature = [
        accelStructType,
        buildFlags,
        geometrySignatures,
        geometrySignatureCount,
        operationName
    ](const VkAccelerationStructureTypeKHR priorAccelStructType, const VkBuildAccelerationStructureFlagsKHR priorBuildFlags, const AccelStructGeometryBuildSignature* const priorGeometrySignatures, const usize priorGeometrySignatureCount) -> bool{
        if((priorBuildFlags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: the prior accepted build did not allow updates"), operationName);
            return false;
        }
        if(priorAccelStructType != accelStructType || priorBuildFlags != buildFlags || priorGeometrySignatureCount != geometrySignatureCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: update build signature does not match the prior build"), operationName);
            return false;
        }
        for(usize geometryIndex = 0u; geometryIndex < geometrySignatureCount; ++geometryIndex){
            if(!VulkanDetail::AccelStructGeometryBuildSignaturesEqual(priorGeometrySignatures[geometryIndex], geometrySignatures[geometryIndex])){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: update build signature does not match the prior build"), operationName);
                return false;
            }
        }

        return true;
    };

    const auto pendingRole = m_currentCmdBuf->getPendingAccelStructBuildSignature(accelStruct);
    if(pendingRole){
        const AccelStructBuildSignatureRole& prior = **pendingRole;
        if(performUpdate && !validatePriorSignature(prior.accelStructType, prior.buildFlags, prior.geometrySignatures.data(), prior.geometrySignatures.size()))
            return MakeUnexpected(Failure{});
        return true;
    }

    ScopedLock lock(accelStruct.m_acceptedBuildSignatureMutex);
    const AccelStructBuildSignatureRole* const acceptedRole = accelStruct.m_acceptedBuildSignatureRole;
    if(!performUpdate)
        return acceptedRole != nullptr;
    if(!acceptedRole){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: PerformUpdate requires a previously accepted build or an earlier same-command-buffer build"), operationName);
        return MakeUnexpected(Failure{});
    }

    if(!validatePriorSignature(acceptedRole->accelStructType, acceptedRole->buildFlags, acceptedRole->geometrySignatures.data(), acceptedRole->geometrySignatures.size()))
        return MakeUnexpected(Failure{});
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

