// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_commands_build{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AccelStructGeometryBuildSignature MakeAccelStructGeometryBuildSignature(
    const VkAccelerationStructureGeometryKHR& geometry,
    const VkAccelerationStructureGeometrySpheresDataNV* const spheresData,
    const VkAccelerationStructureGeometryLinearSweptSpheresDataNV* const lssData,
    const u32 primitiveCount,
    const bool transformDataPresent
){
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
        NWB_ASSERT(spheresData);
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
        NWB_ASSERT(lssData);
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

bool AccelStructGeometryBuildSignaturesEqual(
    const AccelStructGeometryBuildSignature& previous,
    const AccelStructGeometryBuildSignature& current
){
    if(
        previous.geometryType != current.geometryType
        || previous.geometryFlags != current.geometryFlags
        || previous.primitiveCount != current.primitiveCount
    )
        return false;

    if(previous.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR){
        return
            previous.vertexFormat == current.vertexFormat
            && previous.indexType == current.indexType
            && previous.maxVertex == current.maxVertex
            && previous.transformDataPresent == current.transformDataPresent
        ;
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


void CommandList::setRayTracingState(const RayTracingState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("set ray-tracing state")))
        return;

    ShaderTable* const shaderTable = state.shaderTable;
    RayTracingPipeline* const pipeline = shaderTable ? shaderTable->m_pipeline.get() : nullptr;
    if(
        shaderTable
        && (
            &shaderTable->m_context != &m_context
            || &shaderTable->m_device != &m_device
            || !pipeline
            || &pipeline->m_context != &m_context
            || &pipeline->m_device != &m_device
            || pipeline->m_pipeline == VK_NULL_HANDLE
            || pipeline->m_pipelineLayout == VK_NULL_HANDLE
            || !m_context.extensions.KHR_ray_tracing_pipeline
            || !m_context.rayTracingPipelineFeatureEnabled
            || !vkCmdTraceRaysKHR
        )
    ){
        rejectCommandRecording(
            NWB_TEXT("set ray-tracing state"),
            NWB_TEXT("shader table or retained pipeline is foreign, unavailable, or not ready")
        );
        return;
    }

    endActiveRenderPass();
    commitBarriers();
    if(m_commandRecordingFailed)
        return;
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = state;

    if(!shaderTable)
        return;

    retainResource(shaderTable);

    vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipeline);

    bindDescriptorBufferEmptySet(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipelineLayout);
}


bool CommandList::suballocateBuildScratchAddress(
    const u64 buildScratchSize,
    const u64 scratchAlignment,
    VkDeviceAddress& outScratchAddress,
    const tchar* operationName
){
    outScratchAddress = 0u;
    if(buildScratchSize == 0u)
        return true;
    if(!m_currentCmdBuf || m_nativeRecordingID == 0u || scratchAlignment == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch recording identity or alignment is invalid"), operationName);
        return false;
    }
    const u64 scratchPadding = scratchAlignment - 1u;
    if(buildScratchSize > Limit<u64>::s_Max - scratchPadding){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch allocation size overflows"), operationName);
        return false;
    }
    const u64 scratchAllocationSize = buildScratchSize + scratchPadding;

    Buffer* scratchBuffer = nullptr;
    u64 scratchAllocationOffset = 0u;
    const GpuPhysicalQueueId physicalQueue = m_currentCmdBuf->m_queue.m_physicalQueue;
    const u64 completedVersion = m_device.queueGetCompletedInstance(physicalQueue);
    if(!m_device.m_scratchManager.suballocateBuffer(
        scratchAllocationSize,
        &scratchBuffer,
        &scratchAllocationOffset,
        nullptr,
        m_currentCmdBuf.get(),
        m_nativeRecordingID,
        physicalQueue,
        completedVersion,
        1u
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch-buffer suballocation failed"), operationName);
        return false;
    }
    if(!isBufferReadyForCommandQueue(
        scratchBuffer,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch buffer is not ready for device-address access"), operationName);
        return false;
    }

    const VkDeviceAddress scratchBaseAddress = VulkanDetail::GetBufferDeviceAddress(scratchBuffer, scratchAllocationOffset);
    VkDeviceAddress alignedScratchAddress = 0u;
    if(scratchBaseAddress == 0u || !AlignUpChecked(scratchBaseAddress, scratchAlignment, alignedScratchAddress)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: scratch device address is null or cannot be aligned"), operationName);
        return false;
    }
    const u64 scratchOffset = alignedScratchAddress - scratchBaseAddress;
    if(scratchOffset > scratchAllocationSize || buildScratchSize > scratchAllocationSize - scratchOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: aligned scratch range is outside the buffer"), operationName);
        return false;
    }

    outScratchAddress = alignedScratchAddress;
    return true;
}

bool CommandList::validateAccelStructBuildSignature(
    AccelStruct& accelStruct,
    const VkAccelerationStructureTypeKHR accelStructType,
    const VkBuildAccelerationStructureFlagsKHR buildFlags,
    const AccelStructGeometryBuildSignature* const geometrySignatures,
    const usize geometrySignatureCount,
    const bool performUpdate,
    const tchar* const operationName,
    bool& outHasPriorBuild
){
    outHasPriorBuild = false;

    const auto validatePriorSignature = [
        accelStructType,
        buildFlags,
        geometrySignatures,
        geometrySignatureCount,
        operationName
    ](
        const VkAccelerationStructureTypeKHR priorAccelStructType,
        const VkBuildAccelerationStructureFlagsKHR priorBuildFlags,
        const AccelStructGeometryBuildSignature* const priorGeometrySignatures,
        const usize priorGeometrySignatureCount
    ) -> bool{
        if((priorBuildFlags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: the prior accepted build did not allow updates"), operationName);
            return false;
        }
        if(
            priorAccelStructType != accelStructType
            || priorBuildFlags != buildFlags
            || priorGeometrySignatureCount != geometrySignatureCount
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: update build signature does not match the prior build"), operationName);
            return false;
        }
        for(usize geometryIndex = 0u; geometryIndex < geometrySignatureCount; ++geometryIndex){
            if(!__hidden_raytracing_commands_build::AccelStructGeometryBuildSignaturesEqual(
                priorGeometrySignatures[geometryIndex],
                geometrySignatures[geometryIndex]
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: update build signature does not match the prior build"), operationName);
                return false;
            }
        }

        return true;
    };

    VkAccelerationStructureTypeKHR priorAccelStructType = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
    VkBuildAccelerationStructureFlagsKHR priorBuildFlags = 0u;
    const AccelStructGeometryBuildSignature* priorGeometrySignatures = nullptr;
    usize priorGeometrySignatureCount = 0u;
    if(m_currentCmdBuf->getPendingAccelStructBuildSignature(
        accelStruct,
        priorAccelStructType,
        priorBuildFlags,
        priorGeometrySignatures,
        priorGeometrySignatureCount
    )){
        outHasPriorBuild = true;
        return !performUpdate || validatePriorSignature(
            priorAccelStructType,
            priorBuildFlags,
            priorGeometrySignatures,
            priorGeometrySignatureCount
        );
    }

    ScopedLock lock(accelStruct.m_acceptedBuildSignatureMutex);
    outHasPriorBuild = accelStruct.m_hasAcceptedBuild;
    if(!performUpdate)
        return true;
    if(!outHasPriorBuild){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to {}: PerformUpdate requires a previously accepted build or an earlier same-command-buffer build")
            , operationName
        );
        return false;
    }

    return validatePriorSignature(
        accelStruct.m_acceptedBuildType,
        accelStruct.m_acceptedBuildFlags,
        accelStruct.m_acceptedBuildGeometrySignatures.data(),
        accelStruct.m_acceptedBuildGeometrySignatures.size()
    );
}

bool CommandList::buildTopLevelAccelStructFromInstanceData(
    RayTracingAccelStruct& as,
    const VkDeviceAddress instanceDataAddress,
    const usize numInstances,
    const RayTracingAccelStructBuildFlags::Mask buildFlags,
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags,
    const tchar* operationName
){
    Buffer* const backingBuffer = as.getBackingBuffer();
    if(
        !m_device.isAccelStructReadyForGpuUse(&as)
        || !backingBuffer
        || !isBufferAdmittedToCommandQueue(*backingBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is not ready"), operationName);
        return false;
    }
    constexpr VkDeviceAddress s_InstanceDataAlignment = 16u;
    if(instanceDataAddress == 0u || instanceDataAddress % s_InstanceDataAlignment != 0u){
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
    const AccelStructGeometryBuildSignature geometrySignature =
        __hidden_raytracing_commands_build::MakeAccelStructGeometryBuildSignature(
            geometry,
            nullptr,
            nullptr,
            primitiveCount,
            false
        )
    ;
    bool hasPriorBuild = false;
    if(!validateAccelStructBuildSignature(
        as,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        vkBuildFlags,
        &geometrySignature,
        1u,
        performUpdate,
        operationName,
        hasPriorBuild
    ))
        return false;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = vkBuildFlags;
    buildInfo.mode = performUpdate
        ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
        : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR
    ;
    buildInfo.srcAccelerationStructure = performUpdate ? as.m_accelStruct : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = as.m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    auto* asBuffer = as.m_buffer.get();
    if(!asBuffer || asBuffer->getCreationDescription().byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is too small"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure storage is too small"));
        return false;
    }

    const VkDeviceSize scratchSize = performUpdate ? sizeInfo.updateScratchSize : sizeInfo.buildScratchSize;
    const u64 scratchAlignment = Max<u64>(
        static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment),
        1u
    );
    if(!suballocateBuildScratchAddress(
        scratchSize,
        scratchAlignment,
        buildInfo.scratchData.deviceAddress,
        NWB_TEXT("allocate TLAS scratch buffer")
    ))
        return false;

    // Order BLAS writes and prior TLAS writes before this TLAS build.
    {
        auto reuseBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        reuseBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        reuseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        auto reuseDepInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        reuseDepInfo.memoryBarrierCount = 1;
        reuseDepInfo.pMemoryBarriers = &reuseBarrier;
        executePipelineBarrier(reuseDepInfo);
    }
    if(m_commandRecordingFailed)
        return false;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);

    m_currentCmdBuf->appendPendingAccelStructBuildCommit(
        as,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        vkBuildFlags,
        &geometrySignature,
        1u
    );
    retainResource(&as);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildBottomLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("build bottom-level acceleration structure")))
        return;
    if(!accelStructResource){
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

    if(!m_context.extensions.KHR_acceleration_structure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto* as = accelStructResource;
    if(!as || as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure is not bottom-level"));
        return;
    }
    Buffer* const backingBuffer = as->getBackingBuffer();
    if(
        !m_device.isAccelStructReadyForGpuUse(as)
        || !backingBuffer
        || !isBufferAdmittedToCommandQueue(*backingBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is not ready"));
        return;
    }
    VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0u;
    if(!VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, vkBuildFlags, NWB_TEXT("build BLAS")))
        return;

    const bool performUpdate = (buildFlags & RayTracingAccelStructBuildFlags::PerformUpdate) != 0u;
    if(performUpdate && !(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update BLAS: PerformUpdate requires AllowUpdate"));
        return;
    }
    if(performUpdate && !(as->m_desc.buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update BLAS: acceleration structure creation did not allow updates"));
        return;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);

    VulkanDetail::BlasGeometryScratch blasScratch(scratchArena);
    blasScratch.resizeForBuild(numGeometries);

    using OpacityMicromapGeometryVector = Vector<VkAccelerationStructureTrianglesOpacityMicromapEXT, Alloc::ScratchArena>;
    using OpacityMicromapUsageVector = Vector<VkMicromapUsageEXT, Alloc::ScratchArena>;
    using OpacityMicromapUsageOffsetVector = Vector<usize, Alloc::ScratchArena>;
    using AccelStructGeometryBuildSignatureVector = Vector<AccelStructGeometryBuildSignature, Alloc::ScratchArena>;

    OpacityMicromapGeometryVector opacityMicromapGeometries(numGeometries, scratchArena);
    OpacityMicromapUsageOffsetVector opacityMicromapUsageOffsets(numGeometries, scratchArena);
    OpacityMicromapUsageVector opacityMicromapUsageCounts(scratchArena);
    AccelStructGeometryBuildSignatureVector geometrySignatures(numGeometries, scratchArena);

    usize totalOpacityMicromapUsageCount = 0u;
    bool hasOpacityMicromap = false;
    for(usize i = 0; i < numGeometries; ++i){
        opacityMicromapUsageOffsets[i] = Limit<usize>::s_Max;
        if(pGeometries[i].geometryType != RayTracingGeometryType::Triangles)
            continue;

        const RayTracingGeometryTriangles& triangles = pGeometries[i].geometryData.triangles;
        if(!triangles.opacityMicromap){
            if(
                triangles.ommIndexBuffer
                || triangles.ommIndexBufferOffset != 0u
                || triangles.pOmmUsageCounts
                || triangles.numOmmUsageCounts != 0u
                || triangles.ommIndexFormat != Format::UNKNOWN
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM metadata requires an opacity micromap"));
                return;
            }
            continue;
        }
        if(!m_context.extensions.EXT_opacity_micromap || !m_context.opacityMicromapFeatureEnabled){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM geometry requires VK_EXT_opacity_micromap"));
            return;
        }
        if(triangles.numOmmUsageCounts != 0u && !triangles.pOmmUsageCounts){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM usage counts are null"));
            return;
        }
        if(totalOpacityMicromapUsageCount > Limit<usize>::s_Max - triangles.numOmmUsageCounts){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM usage-count storage overflows"));
            return;
        }

        hasOpacityMicromap = true;
        opacityMicromapUsageOffsets[i] = totalOpacityMicromapUsageCount;
        totalOpacityMicromapUsageCount += triangles.numOmmUsageCounts;
    }
    opacityMicromapUsageCounts.resize(totalOpacityMicromapUsageCount);

    constexpr VkBufferUsageFlags s_BuildInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    const auto validateBuildInput = [&](Buffer* const buffer, const tchar* const resourceName) -> bool{
        if(!buffer)
            return true;
        if(isBufferReadyForCommandQueue(buffer, s_BuildInputUsage))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: {} buffer is foreign or not ready"), resourceName);
        return false;
    };

    usize transformCount = 0;
    for(usize i = 0; i < numGeometries; ++i){
        const RayTracingGeometryDesc& geometryDesc = pGeometries[i];
        if(geometryDesc.geometryType == RayTracingGeometryType::Triangles){
            const RayTracingGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
            if(
                !validateBuildInput(triangles.vertexBuffer, NWB_TEXT("triangle vertex"))
                || !validateBuildInput(triangles.indexBuffer, NWB_TEXT("triangle index"))
            )
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::AABBs){
            if(!validateBuildInput(geometryDesc.geometryData.aabbs.buffer, NWB_TEXT("AABB")))
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::Spheres){
            const RayTracingGeometrySpheres& spheres = geometryDesc.geometryData.spheres;
            if(
                !validateBuildInput(spheres.vertexBuffer, NWB_TEXT("sphere vertex"))
                || !validateBuildInput(spheres.indexBuffer, NWB_TEXT("sphere index"))
            )
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::Lss){
            const RayTracingGeometryLss& lss = geometryDesc.geometryData.lss;
            if(
                !validateBuildInput(lss.vertexBuffer, NWB_TEXT("LSS vertex"))
                || !validateBuildInput(lss.indexBuffer, NWB_TEXT("LSS index"))
            )
                return;
        }

        blasScratch.transformOffsets[i] = Limit<usize>::s_Max;
        if(!VulkanDetail::FillBlasGeometryForSizeQuery(
            m_context,
            pGeometries[i],
            blasScratch.geometries[i],
            blasScratch.spheresData[i],
            blasScratch.lssData[i],
            blasScratch.primitiveCounts[i],
            NWB_TEXT("build BLAS"),
            true
        ))
            return;
        if(
            pGeometries[i].geometryType == RayTracingGeometryType::Triangles
            && pGeometries[i].geometryData.triangles.opacityMicromap
        ){
            const RayTracingGeometryTriangles& triangles = pGeometries[i].geometryData.triangles;
            OpacityMicromap* const opacityMicromap = triangles.opacityMicromap;
            if(&opacityMicromap->m_context != &m_context){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap belongs to another device"));
                return;
            }
            if(
                !m_currentCmdBuf->hasPendingOpacityMicromapBuild(*opacityMicromap)
                && !opacityMicromap->m_acceptedConstructed.load(MemoryOrder::acquire)
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap has not been constructed by an accepted or earlier same-command-buffer build")
                );
                return;
            }

            constexpr VkBufferUsageFlags s_MicromapStorageUsage =
                VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            ;
            Buffer* const micromapStorage = opacityMicromap->m_dataBuffer.get();
            if(!isBufferReadyForCommandQueue(micromapStorage, s_MicromapStorageUsage)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap storage is not ready"));
                return;
            }
            const BufferDesc& micromapStorageDesc = micromapStorage->getCreationDescription();
            if(
                !micromapStorageDesc.isAccelStructStorage
                || opacityMicromap->m_micromap == VK_NULL_HANDLE
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap is invalid"));
                return;
            }

            VkIndexType ommIndexType = VK_INDEX_TYPE_NONE_KHR;
            u64 ommIndexStride = 0u;
            if(triangles.ommIndexFormat == Format::UNKNOWN){
                if(triangles.ommIndexBuffer || triangles.ommIndexBufferOffset != 0u){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Vulkan: Failed to build BLAS: implicit triangle OMM indices cannot specify a buffer")
                    );
                    return;
                }
            }
            else if(triangles.ommIndexFormat == Format::R16_UINT){
                ommIndexType = VK_INDEX_TYPE_UINT16;
                ommIndexStride = sizeof(u16);
            }
            else if(triangles.ommIndexFormat == Format::R32_UINT){
                ommIndexType = VK_INDEX_TYPE_UINT32;
                ommIndexStride = sizeof(u32);
            }
            else{
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index format must be UNKNOWN, R16_UINT, or R32_UINT")
                );
                return;
            }

            VkDeviceAddress ommIndexAddress = 0u;
            if(ommIndexType != VK_INDEX_TYPE_NONE_KHR){
                if(!triangles.ommIndexBuffer){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer is null"));
                    return;
                }
                if(!validateBuildInput(triangles.ommIndexBuffer, NWB_TEXT("triangle OMM index")))
                    return;

                const BufferDesc& ommIndexDesc = triangles.ommIndexBuffer->getCreationDescription();
                const u64 ommIndexByteSize = static_cast<u64>(blasScratch.primitiveCounts[i]) * ommIndexStride;
                const u64 validatedOmmIndexByteSize = ommIndexByteSize != 0u ? ommIndexByteSize : 1u;
                if(!ommIndexDesc.isAccelStructBuildInput){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer lacks build-input provenance")
                    );
                    return;
                }
                if(!VulkanDetail::IsBufferRangeInBounds(ommIndexDesc, triangles.ommIndexBufferOffset, validatedOmmIndexByteSize)){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer range is outside the buffer")
                    );
                    return;
                }

                const VkDeviceAddress ommIndexBaseAddress = triangles.ommIndexBuffer->getGpuVirtualAddress();
                if(ommIndexBaseAddress > Limit<u64>::s_Max - triangles.ommIndexBufferOffset){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer address overflows"));
                    return;
                }
                ommIndexAddress = ommIndexBaseAddress + triangles.ommIndexBufferOffset;
                if(ommIndexAddress == 0u || ommIndexAddress % ommIndexStride != 0u){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index device address is misaligned"));
                    return;
                }
            }

            const usize usageOffset = opacityMicromapUsageOffsets[i];
            for(u32 usageIndex = 0u; usageIndex < triangles.numOmmUsageCounts; ++usageIndex){
                const RayTracingOpacityMicromapUsageCount& usage = triangles.pOmmUsageCounts[usageIndex];
                VkOpacityMicromapFormatEXT format = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT;
                if(usage.format == OpacityMicromapFormat::OC1_2_State)
                    format = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
                else if(usage.format == OpacityMicromapFormat::OC1_4_State)
                    format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
                else{
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM usage count has an invalid format"));
                    return;
                }
                const u32 maxSubdivisionLevel = format == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
                    ? opacityMicromap->m_maxOpacity2StateSubdivisionLevel
                    : opacityMicromap->m_maxOpacity4StateSubdivisionLevel
                ;
                if(usage.subdivisionLevel > maxSubdivisionLevel){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM subdivision level {} exceeds device limit {}")
                        , usage.subdivisionLevel
                        , maxSubdivisionLevel
                    );
                    return;
                }

                VkMicromapUsageEXT& vkUsage = opacityMicromapUsageCounts[usageOffset + usageIndex];
                vkUsage.count = usage.count;
                vkUsage.subdivisionLevel = usage.subdivisionLevel;
                vkUsage.format = format;
            }

            VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
            ommGeometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureTrianglesOpacityMicromapEXT>(
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT
            );
            ommGeometry.indexType = ommIndexType;
            ommGeometry.indexBuffer.deviceAddress = ommIndexAddress;
            ommGeometry.indexStride = ommIndexStride;
            ommGeometry.baseTriangle = 0u;
            ommGeometry.usageCountsCount = triangles.numOmmUsageCounts;
            ommGeometry.micromap = opacityMicromap->m_micromap;
        }
        if(pGeometries[i].useTransform){
            if(pGeometries[i].geometryType != RayTracingGeometryType::Triangles){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: only triangle geometry supports per-geometry transforms"));
                return;
            }
            ++transformCount;
        }

        geometrySignatures[i] = __hidden_raytracing_commands_build::MakeAccelStructGeometryBuildSignature(
            blasScratch.geometries[i],
            &blasScratch.spheresData[i],
            &blasScratch.lssData[i],
            blasScratch.primitiveCounts[i],
            pGeometries[i].useTransform
        );
    }

    // OMM update invariants also cover the micromap handle and its index/usage metadata. Those details are not
    // persisted in the AS signature, so this path remains fail-closed.
    if(performUpdate && hasOpacityMicromap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update BLAS: opacity-micromap geometry updates are unsupported"));
        return;
    }

    bool hasPriorBuild = false;
    if(!validateAccelStructBuildSignature(
        *as,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        vkBuildFlags,
        geometrySignatures.data(),
        geometrySignatures.size(),
        performUpdate,
        performUpdate ? NWB_TEXT("update BLAS") : NWB_TEXT("build BLAS"),
        hasPriorBuild
    ))
        return;

    for(usize i = 0; i < numGeometries; ++i){
        if(opacityMicromapUsageOffsets[i] == Limit<usize>::s_Max)
            continue;

        VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
        const u32 usageCount = pGeometries[i].geometryData.triangles.numOmmUsageCounts;
        ommGeometry.pUsageCounts = usageCount != 0u
            ? opacityMicromapUsageCounts.data() + opacityMicromapUsageOffsets[i]
            : nullptr
        ;
        blasScratch.geometries[i].geometry.triangles.pNext = &ommGeometry;
    }

    BufferHandle transformBuffer;
    VkDeviceAddress transformBaseAddress = 0;
    if(transformCount > 0){
        if(transformCount > Limit<usize>::s_Max / sizeof(VkTransformMatrixKHR)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: transform buffer size overflows"));
            return;
        }

        const usize transformBufferSize = transformCount * sizeof(VkTransformMatrixKHR);

        BufferDesc transformBufferDesc;
        transformBufferDesc.byteSize = static_cast<u64>(transformBufferSize);
        transformBufferDesc.cpuAccess = CpuAccessMode::Write;
        transformBufferDesc.isAccelStructBuildInput = true;
        transformBufferDesc.debugName = "BLAS_TransformBuffer";

        transformBuffer = m_device.createBuffer(transformBufferDesc);
        if(!transformBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate BLAS transform buffer"));
            return;
        }
        if(!isBufferReadyForCommandQueue(transformBuffer.get(), s_BuildInputUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: BLAS transform buffer is not ready for device-address access"));
            return;
        }

        auto* mappedTransforms = static_cast<u8*>(m_device.mapBuffer(transformBuffer.get(), CpuAccessMode::Write));
        if(!mappedTransforms){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map BLAS transform buffer"));
            return;
        }

        usize transformIndex = 0;
        for(usize i = 0; i < numGeometries; ++i){
            if(!pGeometries[i].useTransform)
                continue;

            const usize transformOffset = transformIndex * sizeof(VkTransformMatrixKHR);
            NWB_MEMCPY(mappedTransforms + transformOffset, sizeof(VkTransformMatrixKHR), &pGeometries[i].transform, sizeof(VkTransformMatrixKHR));
            blasScratch.transformOffsets[i] = transformOffset;
            ++transformIndex;
        }

        m_device.unmapBuffer(transformBuffer.get());

        transformBaseAddress = VulkanDetail::GetBufferDeviceAddress(transformBuffer.get());
        if(transformBaseAddress == 0u || transformBaseAddress % 16u != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: transform buffer device address must be 16-byte aligned"));
            return;
        }
    }

    auto buildGeometry = [&](usize i){
        const auto& geomDesc = pGeometries[i];

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        const u32 primitiveCount = blasScratch.primitiveCounts[i];

        if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
            const auto& triangles = geomDesc.geometryData.triangles;

            blasScratch.geometries[i].geometry.triangles.vertexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(triangles.vertexBuffer, triangles.vertexOffset);
            if(blasScratch.transformOffsets[i] != Limit<usize>::s_Max)
                blasScratch.geometries[i].geometry.triangles.transformData.deviceAddress = transformBaseAddress + static_cast<u64>(blasScratch.transformOffsets[i]);

            if(triangles.indexBuffer)
                blasScratch.geometries[i].geometry.triangles.indexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(triangles.indexBuffer, triangles.indexOffset);

            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
            const auto& aabbs = geomDesc.geometryData.aabbs;

            blasScratch.geometries[i].geometry.aabbs.data.deviceAddress = VulkanDetail::GetBufferDeviceAddress(aabbs.buffer, aabbs.offset);
            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::Spheres){
            const auto& spheres = geomDesc.geometryData.spheres;

            blasScratch.spheresData[i].vertexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(spheres.vertexBuffer, spheres.vertexPositionOffset);
            blasScratch.spheresData[i].radiusData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(spheres.vertexBuffer, spheres.vertexRadiusOffset);
            if(spheres.indexBuffer)
                blasScratch.spheresData[i].indexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(spheres.indexBuffer, spheres.indexOffset);
            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::Lss){
            const auto& lss = geomDesc.geometryData.lss;

            blasScratch.lssData[i].vertexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(lss.vertexBuffer, lss.vertexPositionOffset);
            blasScratch.lssData[i].radiusData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(lss.vertexBuffer, lss.vertexRadiusOffset);
            if(lss.indexBuffer)
                blasScratch.lssData[i].indexData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(lss.indexBuffer, lss.indexOffset);
            rangeInfo.primitiveCount = primitiveCount;
        }

        blasScratch.rangeInfos[i] = rangeInfo;
    };

    if(taskPool().isParallelEnabled() && numGeometries >= s_ParallelGeometryThreshold)
        scheduleParallelFor(static_cast<usize>(0), numGeometries, s_GeometryGrainSize, buildGeometry);
    else{
        for(usize i = 0; i < numGeometries; ++i)
            buildGeometry(i);
    }

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = vkBuildFlags;
    buildInfo.mode = performUpdate
        ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
        : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR
    ;
    buildInfo.srcAccelerationStructure = performUpdate ? as->m_accelStruct : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = static_cast<u32>(blasScratch.geometries.size());
    buildInfo.pGeometries = blasScratch.geometries.data();

    auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, blasScratch.primitiveCounts.data(), &sizeInfo);

    auto* asBuffer = as->m_buffer.get();
    if(!asBuffer || asBuffer->getCreationDescription().byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        return;
    }

    const VkDeviceSize scratchSize = performUpdate ? sizeInfo.updateScratchSize : sizeInfo.buildScratchSize;
    const u64 scratchAlignment = Max<u64>(
        static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment),
        1u
    );
    if(!suballocateBuildScratchAddress(
        scratchSize,
        scratchAlignment,
        buildInfo.scratchData.deviceAddress,
        NWB_TEXT("allocate BLAS scratch buffer")
    ))
        return;

    // Reused acceleration structures need build-write ordering.
    if(hasPriorBuild){
        auto reuseBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        reuseBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        reuseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        auto reuseDepInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        reuseDepInfo.memoryBarrierCount = 1;
        reuseDepInfo.pMemoryBarriers = &reuseBarrier;
        executePipelineBarrier(reuseDepInfo);
    }

    if(hasOpacityMicromap){
        auto micromapBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        micromapBarrier.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
        micromapBarrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
        micromapBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        micromapBarrier.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT;

        auto micromapDepInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        micromapDepInfo.memoryBarrierCount = 1u;
        micromapDepInfo.pMemoryBarriers = &micromapBarrier;
        executePipelineBarrier(micromapDepInfo);
    }
    if(m_commandRecordingFailed)
        return;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = blasScratch.rangeInfos.data();
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfos);

    m_currentCmdBuf->appendPendingAccelStructBuildCommit(
        *as,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        vkBuildFlags,
        geometrySignatures.data(),
        geometrySignatures.size()
    );

    if(transformBuffer)
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(transformBuffer));

    for(usize i = 0; i < numGeometries; ++i){
        const RayTracingGeometryDesc& geomDesc = pGeometries[i];
        if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
            const RayTracingGeometryTriangles& triangles = geomDesc.geometryData.triangles;
            if(triangles.vertexBuffer)
                retainResource(triangles.vertexBuffer);
            if(triangles.indexBuffer)
                retainResource(triangles.indexBuffer);
            if(triangles.opacityMicromap)
                retainResource(triangles.opacityMicromap);
            if(triangles.ommIndexBuffer)
                retainResource(triangles.ommIndexBuffer);
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
            const RayTracingGeometryAABBs& aabbs = geomDesc.geometryData.aabbs;
            if(aabbs.buffer)
                retainResource(aabbs.buffer);
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::Spheres){
            const RayTracingGeometrySpheres& spheres = geomDesc.geometryData.spheres;
            if(spheres.vertexBuffer)
                retainResource(spheres.vertexBuffer);
            if(spheres.indexBuffer)
                retainResource(spheres.indexBuffer);
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::Lss){
            const RayTracingGeometryLss& lss = geomDesc.geometryData.lss;
            if(lss.vertexBuffer)
                retainResource(lss.vertexBuffer);
            if(lss.indexBuffer)
                retainResource(lss.indexBuffer);
        }
    }

    retainResource(accelStructResource);
}

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

    constexpr u64 s_InstanceDataAlignment = 16u;
    constexpr u64 s_InstanceDataPadding = s_InstanceDataAlignment - 1u;
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
        || !AlignUpChecked(instanceBufferAddress, s_InstanceDataAlignment, instanceDataAddress)
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

    auto* mappedInstanceData = static_cast<u8*>(m_device.mapBuffer(instanceBuffer.get(), CpuAccessMode::Write));
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

    if(taskPool().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold)
        scheduleParallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
    else{
        for(usize i = 0; i < numInstances; ++i)
            buildVkInstance(i);
    }

    m_device.unmapBuffer(instanceBuffer.get());

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

