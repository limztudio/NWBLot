// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setRayTracingState(const RayTracingState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("set ray-tracing state")))
        return;

    endActiveRenderPass();
    commitBarriers();
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = state;

    if(!state.shaderTable)
        return;

    auto* sbt = state.shaderTable;
    if(!sbt)
        return;

    retainResource(state.shaderTable);

    RayTracingPipeline* pipeline = sbt->m_pipeline.get();

    if(!pipeline)
        return;

    vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipeline);

    bindDescriptorBufferEmptySet(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipelineLayout);
}


bool CommandList::attachAccelStructBuildScratchBuffer(
    VkAccelerationStructureBuildGeometryInfoKHR& buildInfo,
    const u64 buildScratchSize,
    const char* debugName,
    const tchar* operationName
){
    if(buildScratchSize == 0)
        return true;

    // Align scratch address to Vulkan's acceleration-structure requirement.
    const u64 scratchAlignment = static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);
    const u64 scratchPadding = scratchAlignment > 1u ? scratchAlignment - 1u : 0u;

    BufferDesc scratchDesc;
    scratchDesc.byteSize = buildScratchSize + scratchPadding;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = debugName;

    BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
    if(!scratchBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate acceleration structure scratch buffer"));
        return false;
    }

    buildInfo.scratchData.deviceAddress = AlignUp<u64>(VulkanDetail::GetBufferDeviceAddress(scratchBuffer.get()), scratchAlignment);
    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    return true;
}

bool CommandList::buildTopLevelAccelStructFromInstanceData(
    RayTracingAccelStruct& as,
    const VkDeviceAddress instanceDataAddress,
    const usize numInstances,
    const RayTracingAccelStructBuildFlags::Mask buildFlags,
    const tchar* operationName
){
    auto geometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceDataAddress;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as.m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCount = static_cast<uint32_t>(numInstances);
    auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    auto* asBuffer = as.m_buffer.get();
    if(!asBuffer || asBuffer->m_desc.byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is too small"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure storage is too small"));
        return false;
    }

    if(!attachAccelStructBuildScratchBuffer(buildInfo, sizeInfo.buildScratchSize, "TLAS_BuildScratch", NWB_TEXT("allocate TLAS scratch buffer")))
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

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);

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

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = accelStructResource;
    if(!as || as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure is not bottom-level"));
        return;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);

    VulkanDetail::BlasGeometryScratch blasScratch(scratchArena);
    blasScratch.resizeForBuild(numGeometries);

    usize transformCount = 0;
    for(usize i = 0; i < numGeometries; ++i){
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
        if(pGeometries[i].useTransform){
            if(pGeometries[i].geometryType != RayTracingGeometryType::Triangles){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: only triangle geometry supports per-geometry transforms"));
                return;
            }
            ++transformCount;
        }
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
        if(transformBaseAddress == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: transform buffer device address is null"));
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

    // Refit requires an existing AllowUpdate BLAS.
    const bool performUpdate =
        (buildFlags & RayTracingAccelStructBuildFlags::PerformUpdate)
        && (buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        && as->m_built
    ;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags);
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
    if(!asBuffer || asBuffer->m_desc.byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        return;
    }

    const VkDeviceSize scratchSize = performUpdate ? sizeInfo.updateScratchSize : sizeInfo.buildScratchSize;
    if(!attachAccelStructBuildScratchBuffer(buildInfo, scratchSize, "AS_BuildScratch", NWB_TEXT("allocate BLAS scratch buffer")))
        return;

    // Reused acceleration structures need build-write ordering.
    if(as->m_built){
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

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = blasScratch.rangeInfos.data();
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfos);

    as->m_built = true;

    if(transformBuffer)
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(transformBuffer));

    if(as->m_desc.trackLiveness){
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

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is not top-level"));
        return;
    }

    auto* instanceBufferImpl = instanceBuffer;
    if(!instanceBufferImpl){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is invalid"));
        return;
    }
    if(!instanceBufferImpl->m_desc.isAccelStructBuildInput){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer was not created with acceleration-structure build input usage"));
        return;
    }

    const u64 instanceDataBytes = static_cast<u64>(numInstances) * sizeof(VkAccelerationStructureInstanceKHR);
    if(!VulkanDetail::IsBufferRangeInBounds(instanceBufferImpl->m_desc, instanceBufferOffset, instanceDataBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer range is outside the buffer"));
        return;
    }

    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceBuffer, instanceBufferOffset);
    if(!buildTopLevelAccelStructFromInstanceData(*as, instanceDataAddress, numInstances, buildFlags, NWB_TEXT("build TLAS from buffer")))
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

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = accelStructResource;
    if(!as || !as->m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure is not top-level"));
        return;
    }
    for(usize i = 0; i < numInstances; ++i){
        auto* blas = pInstances[i].bottomLevelAS;
        if(!blas || blas->m_isTopLevelAtCreation || blas->m_deviceAddress == 0){
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

        auto* blas = inst.bottomLevelAS;
        vkInst.accelerationStructureReference = blas ? blas->m_deviceAddress : 0;
    };

    if(taskPool().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold)
        scheduleParallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
    else{
        for(usize i = 0; i < numInstances; ++i)
            buildVkInstance(i);
    }

    m_device.unmapBuffer(instanceBuffer.get());

    const VkDeviceAddress instanceDataAddress = VulkanDetail::GetBufferDeviceAddress(instanceBuffer.get());
    if(!buildTopLevelAccelStructFromInstanceData(*as, instanceDataAddress, numInstances, buildFlags, NWB_TEXT("build TLAS")))
        return;

    if(as->m_desc.trackLiveness){
        for(usize i = 0; i < numInstances; ++i)
            retainResource(pInstances[i].bottomLevelAS);
    }

    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(instanceBuffer));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

