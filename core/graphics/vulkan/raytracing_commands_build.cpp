// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildBottomLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags){
    constexpr TStringView s_BuildBottomLevelAccelStructOperation = NWB_TEXT("build bottom-level acceleration structure");
    constexpr TStringView s_BuildBlasOperation = NWB_TEXT("build BLAS");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_BuildBottomLevelAccelStructOperation))
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

    if(!m_context.extensions.khrAccelerationStructure || !m_context.accelerationStructureFeatureEnabled)
        return;

    auto& as = *MakeNotNull(accelStructResource);
    if(as.m_isTopLevelAtCreation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure is not bottom-level"));
        return;
    }
    Buffer* const backingBuffer = as.getBackingBuffer();
    if(!m_device.isAccelStructReadyForGpuUse(&as) || !backingBuffer || !isBufferAdmittedToCommandQueue(*backingBuffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is not ready"));
        return;
    }
    const auto vkBuildFlagsResult = VulkanDetail::ConvertAccelStructBuildFlags(buildFlags, s_BuildBlasOperation);
    if(!vkBuildFlagsResult)
        return;
    const VkBuildAccelerationStructureFlagsKHR vkBuildFlags = *vkBuildFlagsResult;

    const bool performUpdate = (buildFlags & RayTracingAccelStructBuildFlags::PerformUpdate) != 0u;
    if(performUpdate && !(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update BLAS: PerformUpdate requires AllowUpdate"));
        return;
    }
    if(performUpdate && !(as.m_desc.buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)){
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
    for(usize i = 0u; i < numGeometries; ++i){
        opacityMicromapUsageOffsets[i] = Limit<usize>::s_Max;
        if(pGeometries[i].geometryType != RayTracingGeometryType::Triangles)
            continue;

        const RayTracingGeometryTriangles& triangles = pGeometries[i].geometryData.triangles;
        if(!triangles.opacityMicromap){
            if(triangles.ommIndexBuffer || triangles.ommIndexBufferOffset != 0u || triangles.pOmmUsageCounts || triangles.numOmmUsageCounts != 0u || triangles.ommIndexFormat != Format::UNKNOWN){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM metadata requires an opacity micromap"));
                return;
            }
            continue;
        }
        if(!m_context.extensions.extOpacityMicromap || !m_context.opacityMicromapFeatureEnabled){
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

    constexpr VkBufferUsageFlags s_BuildInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const auto validateBuildInput = [&](Buffer* const buffer, const TStringView resourceName) -> bool{
        if(!buffer)
            return true;
        if(isBufferReadyForCommandQueue(buffer, s_BuildInputUsage))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: {} buffer is foreign or not ready"), resourceName);
        return false;
    };

    usize transformCount = 0;
    for(usize i = 0u; i < numGeometries; ++i){
        const RayTracingGeometryDesc& geometryDesc = pGeometries[i];
        if(geometryDesc.geometryType == RayTracingGeometryType::Triangles){
            const RayTracingGeometryTriangles& triangles = geometryDesc.geometryData.triangles;
            if(!validateBuildInput(triangles.vertexBuffer, VulkanDetail::s_TriangleVertexInputName) || !validateBuildInput(triangles.indexBuffer, VulkanDetail::s_TriangleIndexInputName))
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::AABBs){
            if(!validateBuildInput(geometryDesc.geometryData.aabbs.buffer, VulkanDetail::s_AabbInputName))
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::Spheres){
            const RayTracingGeometrySpheres& spheres = geometryDesc.geometryData.spheres;
            if(!validateBuildInput(spheres.vertexBuffer, VulkanDetail::s_SphereVertexInputName) || !validateBuildInput(spheres.indexBuffer, VulkanDetail::s_SphereIndexInputName))
                return;
        }
        else if(geometryDesc.geometryType == RayTracingGeometryType::Lss){
            const RayTracingGeometryLss& lss = geometryDesc.geometryData.lss;
            if(!validateBuildInput(lss.vertexBuffer, VulkanDetail::s_LssVertexInputName) || !validateBuildInput(lss.indexBuffer, VulkanDetail::s_LssIndexInputName))
                return;
        }

        blasScratch.transformOffsets[i] = Limit<usize>::s_Max;
        const auto geometry = VulkanDetail::FillBlasGeometryForSizeQuery(m_context, pGeometries[i], s_BuildBlasOperation, true);
        if(!geometry)
            return;
        blasScratch.geometries[i] = geometry->geometry;
        blasScratch.spheresData[i] = geometry->spheresData;
        blasScratch.lssData[i] = geometry->lssData;
        blasScratch.primitiveCounts[i] = geometry->primitiveCount;
        if(geometry->geometry.geometryType == VK_GEOMETRY_TYPE_SPHERES_NV)
            blasScratch.geometries[i].pNext = &blasScratch.spheresData[i];
        else if(geometry->geometry.geometryType == VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV)
            blasScratch.geometries[i].pNext = &blasScratch.lssData[i];
        if(pGeometries[i].geometryType == RayTracingGeometryType::Triangles && pGeometries[i].geometryData.triangles.opacityMicromap){
            const RayTracingGeometryTriangles& triangles = pGeometries[i].geometryData.triangles;
            OpacityMicromap* const opacityMicromap = triangles.opacityMicromap;
            if(&opacityMicromap->m_context != &m_context){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap belongs to another device"));
                return;
            }
            if(!m_currentCmdBuf->hasPendingOpacityMicromapBuild(*opacityMicromap) && !opacityMicromap->m_acceptedConstructed.load(MemoryOrder::acquire)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap has not been constructed by an accepted or earlier same-command-buffer build"));
                return;
            }

            constexpr VkBufferUsageFlags s_MicromapStorageUsage = VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            Buffer* const micromapStorage = opacityMicromap->m_dataBuffer.get();
            if(!isBufferReadyForCommandQueue(micromapStorage, s_MicromapStorageUsage)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap storage is not ready"));
                return;
            }
            const BufferDesc& micromapStorageDesc = micromapStorage->getCreationDescription();
            if(!micromapStorageDesc.isAccelStructStorage || opacityMicromap->m_micromap == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle opacity micromap is invalid"));
                return;
            }

            VkIndexType ommIndexType = VK_INDEX_TYPE_NONE_KHR;
            u64 ommIndexStride = 0u;
            if(triangles.ommIndexFormat == Format::UNKNOWN){
                if(triangles.ommIndexBuffer || triangles.ommIndexBufferOffset != 0u){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: implicit triangle OMM indices cannot specify a buffer"));
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
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index format must be UNKNOWN, R16_UINT, or R32_UINT"));
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
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer lacks build-input provenance"));
                    return;
                }
                if(!VulkanDetail::IsBufferRangeInBounds(ommIndexDesc, triangles.ommIndexBufferOffset, validatedOmmIndexByteSize)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM index buffer range is outside the buffer"));
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
                VkOpacityMicromapFormatEXT format = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_KHR;
                if(usage.format == OpacityMicromapFormat::OC1_2_State)
                    format = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
                else if(usage.format == OpacityMicromapFormat::OC1_4_State)
                    format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
                else{
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM usage count has an invalid format"));
                    return;
                }
                const u32 maxSubdivisionLevel = format == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? opacityMicromap->m_maxOpacity2StateSubdivisionLevel : opacityMicromap->m_maxOpacity4StateSubdivisionLevel;
                if(usage.subdivisionLevel > maxSubdivisionLevel){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: triangle OMM subdivision level {} exceeds device limit {}"), usage.subdivisionLevel, maxSubdivisionLevel);
                    return;
                }

                VkMicromapUsageEXT& vkUsage = opacityMicromapUsageCounts[usageOffset + usageIndex];
                vkUsage.count = usage.count;
                vkUsage.subdivisionLevel = usage.subdivisionLevel;
                vkUsage.format = format;
            }

            VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
            ommGeometry = VulkanDetail::MakeVkStruct<VkAccelerationStructureTrianglesOpacityMicromapEXT>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT);
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

        geometrySignatures[i] = VulkanDetail::MakeAccelStructGeometryBuildSignature(blasScratch.geometries[i], &blasScratch.spheresData[i], &blasScratch.lssData[i], blasScratch.primitiveCounts[i], pGeometries[i].useTransform);
    }

    // OMM update invariants also cover the micromap handle and its index/usage metadata. Those details are not persisted in the AS signature, so this path remains fail-closed.
    if(performUpdate && hasOpacityMicromap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update BLAS: opacity-micromap geometry updates are unsupported"));
        return;
    }

    const auto hasPriorBuildResult = validateAccelStructBuildSignature(
        as,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        vkBuildFlags,
        geometrySignatures.data(),
        geometrySignatures.size(),
        performUpdate,
        performUpdate ? NWB_TEXT("update BLAS") : s_BuildBlasOperation
    );
    if(!hasPriorBuildResult)
        return;
    const bool hasPriorBuild = *hasPriorBuildResult;

    for(usize i = 0u; i < numGeometries; ++i){
        if(opacityMicromapUsageOffsets[i] == Limit<usize>::s_Max)
            continue;

        VkAccelerationStructureTrianglesOpacityMicromapEXT& ommGeometry = opacityMicromapGeometries[i];
        const u32 usageCount = pGeometries[i].geometryData.triangles.numOmmUsageCounts;
        ommGeometry.pUsageCounts = usageCount != 0u ? opacityMicromapUsageCounts.data() + opacityMicromapUsageOffsets[i] : nullptr;
        blasScratch.geometries[i].geometry.triangles.pNext = &ommGeometry;
    }

    VkDeviceAddress transformBaseAddress = 0;
    if(transformCount > 0){
        if(transformCount > Limit<usize>::s_Max / sizeof(VkTransformMatrixKHR)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: transform buffer size overflows"));
            return;
        }

        const usize transformBufferSize = transformCount * sizeof(VkTransformMatrixKHR);
        constexpr u32 s_TransformDeviceAddressAlignment = 16u;
        const auto transformInput = suballocateBuildInput(static_cast<u64>(transformBufferSize), s_TransformDeviceAddressAlignment, NWB_TEXT("allocate BLAS transform input"));
        if(!transformInput)
            return;
        const auto mappedTransforms = MakeNotNull(static_cast<u8*>(transformInput->cpuAddress));

        usize transformIndex = 0;
        for(usize i = 0u; i < numGeometries; ++i){
            if(!pGeometries[i].useTransform)
                continue;

            const usize transformOffset = transformIndex * sizeof(VkTransformMatrixKHR);
            NWB_MEMCPY(mappedTransforms.get() + transformOffset, sizeof(VkTransformMatrixKHR), &pGeometries[i].transform, sizeof(VkTransformMatrixKHR));
            blasScratch.transformOffsets[i] = transformOffset;
            ++transformIndex;
        }

        transformBaseAddress = VulkanDetail::GetBufferDeviceAddress(transformInput->buffer.get(), transformInput->offset);
    }

    auto buildGeometry = [&](usize i)noexcept{
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

    if(taskScheduler().isParallelEnabled() && numGeometries >= s_ParallelGeometryThreshold){
        const auto parallelWork = taskScheduler().parallelFor(static_cast<usize>(0), numGeometries, s_GeometryGrainSize, buildGeometry);
        if(!parallelWork){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: acceleration-structure preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
            m_commandRecordingFailed = true;
            return;
        }
    }
    else{
        for(usize i = 0u; i < numGeometries; ++i)
            buildGeometry(i);
    }

    auto buildInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = vkBuildFlags;
    buildInfo.mode = performUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.srcAccelerationStructure = performUpdate ? as.m_accelStruct : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = as.m_accelStruct;
    buildInfo.geometryCount = static_cast<u32>(blasScratch.geometries.size());
    buildInfo.pGeometries = blasScratch.geometries.data();

    auto sizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    m_context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, blasScratch.primitiveCounts.data(), &sizeInfo);

    auto* asBuffer = as.m_buffer.get();
    if(!asBuffer || asBuffer->getCreationDescription().byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build BLAS: acceleration structure storage is too small"));
        return;
    }

    const VkDeviceSize scratchSize = performUpdate ? sizeInfo.updateScratchSize : sizeInfo.buildScratchSize;
    const u64 scratchAlignment = Max<u64>(static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment), 1u);
    const auto scratchAddressResult = suballocateBuildScratchAddress(scratchSize, scratchAlignment, NWB_TEXT("allocate BLAS scratch buffer"));
    if(!scratchAddressResult)
        return;
    buildInfo.scratchData.deviceAddress = *scratchAddressResult;

    // Reused acceleration structures need build-write ordering.
    if(hasPriorBuild){
        auto reuseBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        reuseBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        reuseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        reuseBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        executePipelineBarrier({ &reuseBarrier, 1u }, {}, {});
    }

    if(hasOpacityMicromap){
        auto micromapBarrier = VulkanDetail::MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
        micromapBarrier.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
        micromapBarrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
        micromapBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        micromapBarrier.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT;

        executePipelineBarrier({ &micromapBarrier, 1u }, {}, {});
    }
    if(m_commandRecordingFailed)
        return;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = blasScratch.rangeInfos.data();
    if(!m_currentCmdBuf->appendPendingAccelStructBuildCommit(as, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, vkBuildFlags, geometrySignatures.data(), geometrySignatures.size())){
        rejectCommandRecording(s_BuildBottomLevelAccelStructOperation, VulkanDetail::s_RetainBuildSignatureFailedReason);
        return;
    }
    m_context.deviceDispatch.vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfos);

    for(usize i = 0u; i < numGeometries; ++i){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

