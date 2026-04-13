// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler Feedback (stubs)


void CommandList::clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture){
    (void)texture;
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}

void CommandList::decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format){
    (void)buffer;
    (void)texture;
    (void)format;
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to decode sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}

void CommandList::setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits){
    (void)texture;
    (void)stateBits;
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set sampler feedback texture state: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    if(byteSize == 0)
        return;
    if(!data){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: data is null"));
        return;
    }
    if(byteSize > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds uint32 range"));
        return;
    }

    const u32 pushConstantByteSize = static_cast<u32>(byteSize);
    if(!__hidden_vulkan::ValidatePushConstantByteSize(m_context, pushConstantByteSize, NWB_TEXT("set push constants")))
        return;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    u32 pipelinePushConstantByteSize = 0;

    if(m_currentGraphicsState.pipeline){
        auto* gp = checked_cast<GraphicsPipeline*>(m_currentGraphicsState.pipeline);
        layout = gp->m_pipelineLayout;
        pipelinePushConstantByteSize = gp->m_pushConstantByteSize;
    }
    else if(m_currentComputeState.pipeline){
        auto* cp = checked_cast<ComputePipeline*>(m_currentComputeState.pipeline);
        layout = cp->m_pipelineLayout;
        pipelinePushConstantByteSize = cp->m_pushConstantByteSize;
    }
    else if(m_currentMeshletState.pipeline){
        auto* mp = checked_cast<MeshletPipeline*>(m_currentMeshletState.pipeline);
        layout = mp->m_pipelineLayout;
        pipelinePushConstantByteSize = mp->m_pushConstantByteSize;
    }
    else if(m_currentRayTracingState.shaderTable){
        auto* rtp = m_currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            auto* rtpImpl = checked_cast<RayTracingPipeline*>(rtp);
            layout = rtpImpl->m_pipelineLayout;
            pipelinePushConstantByteSize = rtpImpl->m_pushConstantByteSize;
        }
    }

    if(layout == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        return;
    }
    if(pipelinePushConstantByteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size {} exceeds active pipeline push constant range {}"),
            pushConstantByteSize,
            pipelinePushConstantByteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: byte size exceeds active pipeline push constant range"));
        return;
    }

    vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw Indirect


void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    Buffer* indirectBuffer = nullptr;
    if(!prepareDrawIndirect(offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments), NWB_TEXT("draw indexed indirect"), NWB_TEXT("drawIndexedIndirect"), true, indirectBuffer))
        return;

    vkCmdDrawIndexedIndirect(m_currentCmdBuf->m_cmdBuf, indirectBuffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    m_currentCmdBuf->m_referencedResources.push_back(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing (additional methods)


bool CommandList::buildTopLevelAccelStructFromInstanceData(
    IRayTracingAccelStruct* asInterface,
    AccelStruct* as,
    const VkDeviceAddress instanceDataAddress,
    const usize numInstances,
    const RayTracingAccelStructBuildFlags::Mask buildFlags,
    const tchar* operationName)
{
    VkAccelerationStructureGeometryKHR geometry = __hidden_vulkan::MakeVkStruct<VkAccelerationStructureGeometryKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceDataAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = __hidden_vulkan::MakeVkStruct<VkAccelerationStructureBuildGeometryInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = __hidden_vulkan::ConvertAccelStructBuildFlags(buildFlags, false);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCount = static_cast<uint32_t>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = __hidden_vulkan::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    auto* asBuffer = checked_cast<Buffer*>(as->m_buffer.get());
    if(!asBuffer || asBuffer->m_desc.byteSize < sizeInfo.accelerationStructureSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: acceleration structure storage is too small"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to build TLAS: acceleration structure storage is too small"));
        return false;
    }

    if(!attachAccelStructBuildScratchBuffer(buildInfo, sizeInfo.buildScratchSize, "TLAS_BuildScratch", NWB_TEXT("allocate TLAS scratch buffer")))
        return false;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);

    m_currentCmdBuf->m_referencedResources.push_back(asInterface);
    return true;
}

void CommandList::buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* _as, IBuffer* instanceBuffer,
    u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags)
{
    if(!_as){
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

    auto* as = checked_cast<AccelStruct*>(_as);
    if(!as || !as->m_desc.isTopLevel){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: acceleration structure is not top-level"));
        return;
    }

    auto* instanceBufferImpl = checked_cast<Buffer*>(instanceBuffer);
    if(!instanceBufferImpl){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer is invalid"));
        return;
    }
    if(!instanceBufferImpl->m_desc.isAccelStructBuildInput){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer was not created with acceleration-structure build input usage"));
        return;
    }

    const u64 instanceDataBytes = static_cast<u64>(numInstances) * sizeof(VkAccelerationStructureInstanceKHR);
    if(!__hidden_vulkan::IsBufferRangeInBounds(instanceBufferImpl->m_desc, instanceBufferOffset, instanceDataBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build TLAS from buffer: instance buffer range is outside the buffer"));
        return;
    }

    const VkDeviceAddress instanceDataAddress = __hidden_vulkan::GetBufferDeviceAddress(instanceBuffer, instanceBufferOffset);
    if(!buildTopLevelAccelStructFromInstanceData(_as, as, instanceDataAddress, numInstances, buildFlags, NWB_TEXT("build TLAS from buffer")))
        return;

    m_currentCmdBuf->m_referencedResources.push_back(instanceBuffer);
}

void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!__hidden_vulkan::BuildClusterOperationInputInfo(opDesc.params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("execute cluster operation")))
        return;

    auto* indirectArgCountBuffer = opDesc.inIndirectArgCountBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgCountBuffer) : nullptr;
    auto* indirectArgsBuffer = opDesc.inIndirectArgsBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgsBuffer) : nullptr;
    auto* inOutAddressesBuffer = opDesc.inOutAddressesBuffer ? checked_cast<Buffer*>(opDesc.inOutAddressesBuffer) : nullptr;
    auto* outSizesBuffer = opDesc.outSizesBuffer ? checked_cast<Buffer*>(opDesc.outSizesBuffer) : nullptr;
    auto* outAccelerationStructuresBuffer = opDesc.outAccelerationStructuresBuffer ? checked_cast<Buffer*>(opDesc.outAccelerationStructuresBuffer) : nullptr;

    if(indirectArgCountBuffer && opDesc.inIndirectArgCountOffsetInBytes >= indirectArgCountBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-arg-count offset is out of range."));
        return;
    }
    if(indirectArgsBuffer && opDesc.inIndirectArgsOffsetInBytes >= indirectArgsBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-args offset is out of range."));
        return;
    }
    if(inOutAddressesBuffer && opDesc.inOutAddressesOffsetInBytes >= inOutAddressesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation in/out-addresses offset is out of range."));
        return;
    }
    if(outSizesBuffer && opDesc.outSizesOffsetInBytes >= outSizesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation out-sizes offset is out of range."));
        return;
    }
    if(outAccelerationStructuresBuffer && opDesc.outAccelerationStructuresOffsetInBytes >= outAccelerationStructuresBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation output AS offset is out of range."));
        return;
    }

    if(m_enableAutomaticBarriers){
        if(indirectArgsBuffer)
            setBufferState(opDesc.inIndirectArgsBuffer, ResourceStates::ShaderResource);
        if(indirectArgCountBuffer)
            setBufferState(opDesc.inIndirectArgCountBuffer, ResourceStates::ShaderResource);
        if(inOutAddressesBuffer)
            setBufferState(opDesc.inOutAddressesBuffer, ResourceStates::UnorderedAccess);
        if(outSizesBuffer)
            setBufferState(opDesc.outSizesBuffer, ResourceStates::UnorderedAccess);
        if(outAccelerationStructuresBuffer)
            setBufferState(opDesc.outAccelerationStructuresBuffer, ResourceStates::AccelStructWrite);
    }

    if(indirectArgCountBuffer)
        m_currentCmdBuf->m_referencedResources.push_back(opDesc.inIndirectArgCountBuffer);
    if(indirectArgsBuffer)
        m_currentCmdBuf->m_referencedResources.push_back(opDesc.inIndirectArgsBuffer);
    if(inOutAddressesBuffer)
        m_currentCmdBuf->m_referencedResources.push_back(opDesc.inOutAddressesBuffer);
    if(outSizesBuffer)
        m_currentCmdBuf->m_referencedResources.push_back(opDesc.outSizesBuffer);
    if(outAccelerationStructuresBuffer)
        m_currentCmdBuf->m_referencedResources.push_back(opDesc.outAccelerationStructuresBuffer);

    commitBarriers();

    BufferHandle scratchBufferHandle;
    Buffer* scratchBuffer = nullptr;
    if(opDesc.scratchSizeInBytes > 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = opDesc.scratchSizeInBytes;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "ClusterOp_Scratch";
        scratchDesc.canHaveUAVs = true;

        scratchBufferHandle = m_device.createBuffer(scratchDesc);
        if(!scratchBufferHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate cluster operation scratch buffer"));
            return;
        }

        scratchBuffer = checked_cast<Buffer*>(scratchBufferHandle.get());
    }

    VkClusterAccelerationStructureCommandsInfoNV commandsInfo = __hidden_vulkan::MakeVkStruct<VkClusterAccelerationStructureCommandsInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV);
    commandsInfo.input = inputInfo;
    commandsInfo.scratchData = scratchBuffer ? scratchBuffer->m_deviceAddress : 0;
    commandsInfo.dstImplicitData = outAccelerationStructuresBuffer ? outAccelerationStructuresBuffer->m_deviceAddress + opDesc.outAccelerationStructuresOffsetInBytes : 0;

    if(inOutAddressesBuffer){
        commandsInfo.dstAddressesArray.deviceAddress = inOutAddressesBuffer->m_deviceAddress + opDesc.inOutAddressesOffsetInBytes;
        commandsInfo.dstAddressesArray.stride = inOutAddressesBuffer->getDescription().structStride;
        commandsInfo.dstAddressesArray.size = inOutAddressesBuffer->getDescription().byteSize - opDesc.inOutAddressesOffsetInBytes;
    }

    if(outSizesBuffer){
        commandsInfo.dstSizesArray.deviceAddress = outSizesBuffer->m_deviceAddress + opDesc.outSizesOffsetInBytes;
        commandsInfo.dstSizesArray.stride = outSizesBuffer->getDescription().structStride;
        commandsInfo.dstSizesArray.size = outSizesBuffer->getDescription().byteSize - opDesc.outSizesOffsetInBytes;
    }

    if(indirectArgsBuffer){
        commandsInfo.srcInfosArray.deviceAddress = indirectArgsBuffer->m_deviceAddress + opDesc.inIndirectArgsOffsetInBytes;
        commandsInfo.srcInfosArray.stride = indirectArgsBuffer->getDescription().structStride;
        commandsInfo.srcInfosArray.size = indirectArgsBuffer->getDescription().byteSize - opDesc.inIndirectArgsOffsetInBytes;
    }

    commandsInfo.srcInfosCount = indirectArgCountBuffer ? indirectArgCountBuffer->m_deviceAddress + opDesc.inIndirectArgCountOffsetInBytes : 0;
    commandsInfo.addressResolutionFlags = static_cast<VkClusterAccelerationStructureAddressResolutionFlagsNV>(0);

    vkCmdBuildClusterAccelerationStructureIndirectNV(m_currentCmdBuf->m_cmdBuf, &commandsInfo);

    if(scratchBufferHandle)
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBufferHandle));
}

void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return;

    if(numDescs == 0)
        return;
    if(!convertDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptors are null"));
        return;
    }
    if(numDescs > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    Alloc::ScratchArena<> scratchArena;

    Vector<CooperativeVectorConvertMatrixLayoutDesc const*, Alloc::ScratchAllocator<CooperativeVectorConvertMatrixLayoutDesc const*>> validDescs{ Alloc::ScratchAllocator<CooperativeVectorConvertMatrixLayoutDesc const*>(scratchArena) };
    validDescs.reserve(numDescs);

    for(usize i = 0; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = convertDescs[i];
        if(!convertDesc.src.buffer || !convertDesc.dst.buffer)
            continue;

        auto* srcBuffer = checked_cast<Buffer*>(convertDesc.src.buffer);
        auto* dstBuffer = checked_cast<Buffer*>(convertDesc.dst.buffer);
        if(!srcBuffer || !dstBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: buffer is invalid"));
            continue;
        }
        if(!__hidden_vulkan::IsBufferRangeInBounds(srcBuffer->m_desc, convertDesc.src.offset, convertDesc.src.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: source range is outside the buffer"));
            continue;
        }
        if(!__hidden_vulkan::IsBufferRangeInBounds(dstBuffer->m_desc, convertDesc.dst.offset, convertDesc.dst.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: destination range is outside the buffer"));
            continue;
        }

        if(m_enableAutomaticBarriers){
            setBufferState(convertDesc.src.buffer, ResourceStates::ConvertCoopVecMatrixInput);
            setBufferState(convertDesc.dst.buffer, ResourceStates::ConvertCoopVecMatrixOutput);
        }

        validDescs.push_back(&convertDesc);
    }

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>> vkConvertDescs(validDescs.size(), Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>(scratchArena));
    Vector<usize, Alloc::ScratchAllocator<usize>> dstSizes(validDescs.size(), Alloc::ScratchAllocator<usize>(scratchArena));

    auto buildConvertDesc = [&](usize i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = *validDescs[i];
        dstSizes[i] = convertDesc.dst.size;

        VkConvertCooperativeVectorMatrixInfoNV vkDesc = __hidden_vulkan::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
        vkDesc.srcSize = convertDesc.src.size;
        vkDesc.srcData.deviceAddress = checked_cast<Buffer*>(convertDesc.src.buffer)->m_deviceAddress + convertDesc.src.offset;
        vkDesc.pDstSize = &dstSizes[i];
        vkDesc.dstData.deviceAddress = checked_cast<Buffer*>(convertDesc.dst.buffer)->m_deviceAddress + convertDesc.dst.offset;
        vkDesc.srcComponentType = __hidden_vulkan::ConvertCoopVecDataType(convertDesc.src.type);
        vkDesc.dstComponentType = __hidden_vulkan::ConvertCoopVecDataType(convertDesc.dst.type);
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;

        vkDesc.srcLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(convertDesc.src.layout);
        vkDesc.srcStride = convertDesc.src.stride != 0
            ? convertDesc.src.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.src.type, convertDesc.src.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkDesc.dstLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(convertDesc.dst.layout);
        vkDesc.dstStride = convertDesc.dst.stride != 0
            ? convertDesc.dst.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.dst.type, convertDesc.dst.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkConvertDescs[i] = vkDesc;
    };

    if(taskPool().isParallelEnabled() && validDescs.size() >= s_ParallelConvertThreshold)
        scheduleParallelFor(static_cast<usize>(0), validDescs.size(), s_ConvertGrainSize, buildConvertDesc);
    else{
        for(usize i = 0; i < validDescs.size(); ++i)
            buildConvertDesc(i);
    }

    commitBarriers();

    if(vkConvertDescs.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    if(!vkConvertDescs.empty())
        vkCmdConvertCooperativeVectorMatrixNV(m_currentCmdBuf->m_cmdBuf, static_cast<u32>(vkConvertDescs.size()), vkConvertDescs.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UAV Barriers and Tracking


void CommandList::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    if(!texture)
        return;
    m_stateTracker->setEnableUavBarriersForTexture(texture, enableBarriers);
}

void CommandList::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    if(!buffer)
        return;
    m_stateTracker->setEnableUavBarriersForBuffer(buffer, enableBarriers);
}

void CommandList::beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    m_stateTracker->beginTrackingTexture(texture, subresources, stateBits);
}

void CommandList::beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    m_stateTracker->beginTrackingBuffer(buffer, stateBits);
}

ResourceStates::Mask CommandList::getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel){
    return m_stateTracker->getTextureState(texture, arraySlice, mipLevel);
}

ResourceStates::Mask CommandList::getBufferState(IBuffer* buffer){
    return m_stateTracker->getBufferState(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors


IDevice* CommandList::getDevice(){
    return &m_device;
}

const CommandListParameters& CommandList::getDescription(){
    return m_desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture tiling and sampler feedback stubs


void Device::getTextureTiling(ITexture* _texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings){
    auto clearOutputs = [&](){
        if(numTiles)
            *numTiles = 0;
        if(desc)
            *desc = {};
        if(tileShape)
            *tileShape = {};
        if(subresourceTilingsNum)
            *subresourceTilingsNum = 0;
    };
    auto ceilDivU32 = [](const u32 value, const u32 divisor, u32& outValue){
        if(divisor == 0)
            return false;

        outValue = value == 0 ? 0 : 1u + ((value - 1u) / divisor);
        return true;
    };
    auto addTileCount = [](const u32 startTileIndex, const u32 widthInTiles, const u32 heightInTiles, const u32 depthInTiles, u32& outNextTileIndex){
        u64 tileCount = widthInTiles;
        if(heightInTiles != 0 && tileCount > Limit<u64>::s_Max / heightInTiles)
            return false;
        tileCount *= heightInTiles;
        if(depthInTiles != 0 && tileCount > Limit<u64>::s_Max / depthInTiles)
            return false;
        tileCount *= depthInTiles;
        if(tileCount > static_cast<u64>(Limit<u32>::s_Max - startTileIndex))
            return false;

        outNextTileIndex = startTileIndex + static_cast<u32>(tileCount);
        return true;
    };
    auto checkedTileCount = [](const VkDeviceSize byteSize, const u64 tileByteSize, u32& outTileCount){
        if(tileByteSize == 0){
            outTileCount = 0;
            return true;
        }

        const u64 tileCount = byteSize / tileByteSize;
        if(tileCount > Limit<u32>::s_Max)
            return false;

        outTileCount = static_cast<u32>(tileCount);
        return true;
    };

    if(!_texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture is null"));
        clearOutputs();
        return;
    }
    if(!queryFeatureSupport(Feature::VirtualResources)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: virtual/tiled resources are not supported by this backend"));
        clearOutputs();
        return;
    }

    auto* texture = checked_cast<Texture*>(_texture);
    if(!texture->m_desc.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture is not tiled"));
        clearOutputs();
        return;
    }

    u32 numStandardMips = 0;
    u32 tileWidth = 1;
    u32 tileHeight = 1;
    u32 tileDepth = 1;

    Alloc::ScratchArena<> scratchArena;

    uint32_t sparseReqCount = 0;
    vkGetImageSparseMemoryRequirements(m_context.device, texture->m_image, &sparseReqCount, nullptr);

    Vector<VkSparseImageMemoryRequirements, Alloc::ScratchAllocator<VkSparseImageMemoryRequirements>> sparseReqs(sparseReqCount, Alloc::ScratchAllocator<VkSparseImageMemoryRequirements>(scratchArena));
    if(sparseReqCount > 0)
        vkGetImageSparseMemoryRequirements(m_context.device, texture->m_image, &sparseReqCount, sparseReqs.data());

    if(!sparseReqs.empty()){
        numStandardMips = sparseReqs[0].imageMipTailFirstLod;
        if(numStandardMips > texture->m_desc.mipLevels){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image mip tail exceeds texture mip levels"));
            clearOutputs();
            return;
        }

        if(desc){
            desc->numStandardMips = numStandardMips;
            desc->numPackedMips = texture->m_desc.mipLevels - numStandardMips;
            if(!checkedTileCount(sparseReqs[0].imageMipTailOffset, texture->m_tileByteSize, desc->startTileIndexInOverallResource)
                || !checkedTileCount(sparseReqs[0].imageMipTailSize, texture->m_tileByteSize, desc->numTilesForPackedMips)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: packed mip tile range exceeds u32 limits"));
                clearOutputs();
                return;
            }
        }
    }

    uint32_t formatPropCount = 0;
    vkGetPhysicalDeviceSparseImageFormatProperties(
        m_context.physicalDevice,
        texture->m_imageInfo.format,
        texture->m_imageInfo.imageType,
        texture->m_imageInfo.samples,
        texture->m_imageInfo.usage,
        texture->m_imageInfo.tiling,
        &formatPropCount, nullptr
    );

    Vector<VkSparseImageFormatProperties, Alloc::ScratchAllocator<VkSparseImageFormatProperties>> formatProps(formatPropCount, Alloc::ScratchAllocator<VkSparseImageFormatProperties>(scratchArena));
    if(formatPropCount > 0){
        vkGetPhysicalDeviceSparseImageFormatProperties(
            m_context.physicalDevice,
            texture->m_imageInfo.format,
            texture->m_imageInfo.imageType,
            texture->m_imageInfo.samples,
            texture->m_imageInfo.usage,
            texture->m_imageInfo.tiling,
            &formatPropCount, formatProps.data()
        );
    }

    if(!formatProps.empty()){
        tileWidth = formatProps[0].imageGranularity.width;
        tileHeight = formatProps[0].imageGranularity.height;
        tileDepth = formatProps[0].imageGranularity.depth;
    }
    if(tileWidth == 0 || tileHeight == 0 || tileDepth == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile shape is invalid"));
        clearOutputs();
        return;
    }

    if(tileShape){
        tileShape->widthInTexels = tileWidth;
        tileShape->heightInTexels = tileHeight;
        tileShape->depthInTexels = tileDepth;
    }

    if(subresourceTilingsNum && subresourceTilings){
        *subresourceTilingsNum = Min(*subresourceTilingsNum, texture->m_desc.mipLevels);
        u32 startTileIndex = 0;

        u32 width = texture->m_desc.width;
        u32 height = texture->m_desc.height;
        u32 depth = texture->m_desc.depth;

        for(u32 i = 0; i < *subresourceTilingsNum; ++i){
            if(i < numStandardMips){
                if(!ceilDivU32(width, tileWidth, subresourceTilings[i].widthInTiles)
                    || !ceilDivU32(height, tileHeight, subresourceTilings[i].heightInTiles)
                    || !ceilDivU32(depth, tileDepth, subresourceTilings[i].depthInTiles)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile shape is invalid"));
                    clearOutputs();
                    return;
                }
                subresourceTilings[i].startTileIndexInOverallResource = startTileIndex;
            }
            else{
                subresourceTilings[i].widthInTiles = 0;
                subresourceTilings[i].heightInTiles = 0;
                subresourceTilings[i].depthInTiles = 0;
                subresourceTilings[i].startTileIndexInOverallResource = UINT32_MAX;
            }

            width = Max(width / 2, tileWidth);
            height = Max(height / 2, tileHeight);
            depth = Max(depth / 2, tileDepth);

            if(!addTileCount(
                startTileIndex,
                subresourceTilings[i].widthInTiles,
                subresourceTilings[i].heightInTiles,
                subresourceTilings[i].depthInTiles,
                startTileIndex
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: sparse image tile count exceeds u32 limits"));
                clearOutputs();
                return;
            }
        }
    }

    if(numTiles){
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memReqs);
        if(!checkedTileCount(memReqs.size, texture->m_tileByteSize, *numTiles)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get texture tiling: texture tile count exceeds u32 limits"));
            clearOutputs();
            return;
        }
    }
}

void Device::updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue){
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: texture is null"));
        return;
    }
    if(!queryFeatureSupport(Feature::VirtualResources)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: virtual/tiled resources are not supported by this backend"));
        return;
    }
    if(!checked_cast<Texture*>(texture)->m_desc.isTiled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: texture is not tiled"));
        return;
    }
    if(!tileMappings && numTileMappings > 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: mappings are null"));
        return;
    }
    if(numTileMappings == 0)
        return;

    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: requested queue is not available"));
        return;
    }

    queue->updateTextureTileMappings(texture, tileMappings, numTileMappings);
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc){
    (void)pairedTexture;
    (void)desc;
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler feedback texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
    return nullptr;
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture){
    (void)objectType;
    (void)texture;
    (void)pairedTexture;
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create sampler feedback texture for native texture: sampler feedback is not supported by this backend"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Sampler feedback is not supported by this backend"));
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
