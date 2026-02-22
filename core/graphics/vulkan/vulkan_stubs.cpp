// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler Feedback (stubs)


void CommandList::clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture){
    (void)texture;
    // Sampler feedback is a DX12-specific feature with no Vulkan equivalent
}

void CommandList::decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format){
    (void)buffer;
    (void)texture;
    (void)format;
    // Sampler feedback is a DX12-specific feature with no Vulkan equivalent
}

void CommandList::setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits){
    (void)texture;
    (void)stateBits;
    // Sampler feedback is a DX12-specific feature with no Vulkan equivalent
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    VkPipelineLayout layout = VK_NULL_HANDLE;

    if(m_currentGraphicsState.pipeline){
        auto* gp = checked_cast<GraphicsPipeline*>(m_currentGraphicsState.pipeline);
        layout = gp->m_pipelineLayout;
    }
    else if(m_currentComputeState.pipeline){
        auto* cp = checked_cast<ComputePipeline*>(m_currentComputeState.pipeline);
        layout = cp->m_pipelineLayout;
    }
    else if(m_currentMeshletState.pipeline){
        auto* mp = checked_cast<MeshletPipeline*>(m_currentMeshletState.pipeline);
        layout = mp->m_pipelineLayout;
    }
    else if(m_currentRayTracingState.shaderTable){
        auto* rtp = m_currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            auto* rtpImpl = checked_cast<RayTracingPipeline*>(rtp);
            layout = rtpImpl->m_pipelineLayout;
        }
    }

    if(layout == VK_NULL_HANDLE){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: CommandList::setPushConstants: no active pipeline layout"));
        return;
    }

    vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, static_cast<u32>(byteSize), data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw Indirect


void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    if(!m_currentGraphicsState.indirectParams){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: No indirect buffer bound for drawIndexedIndirect"));
        return;
    }
    auto* buffer = checked_cast<Buffer*>(m_currentGraphicsState.indirectParams);
    vkCmdDrawIndexedIndirect(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    m_currentCmdBuf->m_referencedResources.push_back(m_currentGraphicsState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing (additional methods)


void CommandList::buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* _as, IBuffer* instanceBuffer,
    u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags)
{
    if(!_as || !instanceBuffer || numInstances == 0)
        return;

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);

    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(instanceBuffer, instanceBufferOffset);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = 0;

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCount = static_cast<uint32_t>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";

    BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);

        m_currentCmdBuf->m_referencedStagingBuffers.push_back(scratchBuffer);
    }

    m_currentCmdBuf->m_referencedResources.push_back(_as);
    m_currentCmdBuf->m_referencedResources.push_back(instanceBuffer);
}

void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return;

    VkClusterAccelerationStructureOpTypeNV opType;
    switch(opDesc.params.type){
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
        return;
    }

    VkClusterAccelerationStructureOpModeNV opMode;
    switch(opDesc.params.mode){
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
        return;
    }

    VkBuildAccelerationStructureFlagsKHR opFlags = 0;
    if(opDesc.params.flags & RayTracingClusterOperationFlags::FastTrace)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(!(opDesc.params.flags & RayTracingClusterOperationFlags::FastTrace) && (opDesc.params.flags & RayTracingClusterOperationFlags::FastBuild))
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(opDesc.params.flags & RayTracingClusterOperationFlags::AllowOMM)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;

    VkClusterAccelerationStructureInputInfoNV inputInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    inputInfo.maxAccelerationStructureCount = opDesc.params.maxArgCount;
    inputInfo.flags = opFlags;
    inputInfo.opType = opType;
    inputInfo.opMode = opMode;

    VkClusterAccelerationStructureMoveObjectsInputNV moveInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV };
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };

    switch(opDesc.params.type){
    case RayTracingClusterOperationType::Move:{
        VkClusterAccelerationStructureTypeNV moveType;
        switch(opDesc.params.move.type){
        case RayTracingClusterOperationMoveType::BottomLevel:  moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        case RayTracingClusterOperationMoveType::ClusterLevel: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV; break;
        case RayTracingClusterOperationMoveType::Template:     moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV; break;
        default: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        }
        moveInput.type = moveType;
        moveInput.noMoveOverlap = (opDesc.params.flags & RayTracingClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        moveInput.maxMovedBytes = opDesc.params.move.maxBytes;
        inputInfo.opInput.pMoveObjects = &moveInput;
        break;
    }
    case RayTracingClusterOperationType::ClasBuild:
    case RayTracingClusterOperationType::ClasBuildTemplates:
    case RayTracingClusterOperationType::ClasInstantiateTemplates:{
        clusterInput.vertexFormat = __hidden_vulkan::ConvertFormat(opDesc.params.clas.vertexFormat);
        clusterInput.maxGeometryIndexValue = opDesc.params.clas.maxGeometryIndex;
        clusterInput.maxClusterUniqueGeometryCount = opDesc.params.clas.maxUniqueGeometryCount;
        clusterInput.maxClusterTriangleCount = opDesc.params.clas.maxTriangleCount;
        clusterInput.maxClusterVertexCount = opDesc.params.clas.maxVertexCount;
        clusterInput.maxTotalTriangleCount = opDesc.params.clas.maxTotalTriangleCount;
        clusterInput.maxTotalVertexCount = opDesc.params.clas.maxTotalVertexCount;
        clusterInput.minPositionTruncateBitCount = opDesc.params.clas.minPositionTruncateBitCount;
        inputInfo.opInput.pTriangleClusters = &clusterInput;
        break;
    }
    case RayTracingClusterOperationType::BlasBuild:{
        blasInput.maxClusterCountPerAccelerationStructure = opDesc.params.blas.maxClasPerBlasCount;
        blasInput.maxTotalClusterCount = opDesc.params.blas.maxTotalClasCount;
        inputInfo.opInput.pClustersBottomLevel = &blasInput;
        break;
    }
    default:
        break;
    }

    auto* indirectArgCountBuffer = opDesc.inIndirectArgCountBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgCountBuffer) : nullptr;
    auto* indirectArgsBuffer = opDesc.inIndirectArgsBuffer ? checked_cast<Buffer*>(opDesc.inIndirectArgsBuffer) : nullptr;
    auto* inOutAddressesBuffer = opDesc.inOutAddressesBuffer ? checked_cast<Buffer*>(opDesc.inOutAddressesBuffer) : nullptr;
    auto* outSizesBuffer = opDesc.outSizesBuffer ? checked_cast<Buffer*>(opDesc.outSizesBuffer) : nullptr;
    auto* outAccelerationStructuresBuffer = opDesc.outAccelerationStructuresBuffer ? checked_cast<Buffer*>(opDesc.outAccelerationStructuresBuffer) : nullptr;

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
        if(!scratchBufferHandle)
            return;

        scratchBuffer = checked_cast<Buffer*>(scratchBufferHandle.get());
    }

    VkClusterAccelerationStructureCommandsInfoNV commandsInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
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
    if(!m_context.extensions.NV_cooperative_vector)
        return;

    if(numDescs == 0)
        return;

    Alloc::ScratchArena<> scratchArena;

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>> vkConvertDescs{ Alloc::ScratchAllocator<VkConvertCooperativeVectorMatrixInfoNV>(scratchArena) };
    vkConvertDescs.reserve(numDescs);

    Vector<usize, Alloc::ScratchAllocator<usize>> dstSizes{ Alloc::ScratchAllocator<usize>(scratchArena) };
    dstSizes.reserve(numDescs);

    for(usize i = 0; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = convertDescs[i];

        if(!convertDesc.src.buffer || !convertDesc.dst.buffer)
            continue;

        if(m_enableAutomaticBarriers){
            setBufferState(convertDesc.src.buffer, ResourceStates::ShaderResource);
            setBufferState(convertDesc.dst.buffer, ResourceStates::UnorderedAccess);
        }

        VkConvertCooperativeVectorMatrixInfoNV vkDesc = { VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV };
        vkDesc.srcSize = convertDesc.src.size;
        vkDesc.srcData.deviceAddress = checked_cast<Buffer*>(convertDesc.src.buffer)->m_deviceAddress + convertDesc.src.offset;
        vkDesc.pDstSize = &dstSizes.emplace_back(convertDesc.dst.size);
        vkDesc.dstData.deviceAddress = checked_cast<Buffer*>(convertDesc.dst.buffer)->m_deviceAddress + convertDesc.dst.offset;
        vkDesc.srcComponentType = __hidden_vulkan::ConvertCoopVecDataType(convertDesc.src.type);
        vkDesc.dstComponentType = __hidden_vulkan::ConvertCoopVecDataType(convertDesc.dst.type);
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;

        vkDesc.srcLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(convertDesc.src.layout);
        vkDesc.srcStride = convertDesc.src.stride != 0
            ? convertDesc.src.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.src.type, convertDesc.src.layout, convertDesc.numRows, convertDesc.numColumns);

        vkDesc.dstLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(convertDesc.dst.layout);
        vkDesc.dstStride = convertDesc.dst.stride != 0
            ? convertDesc.dst.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.dst.type, convertDesc.dst.layout, convertDesc.numRows, convertDesc.numColumns);

        vkConvertDescs.push_back(vkDesc);
    }

    commitBarriers();

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
    auto* texture = checked_cast<Texture*>(_texture);
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

        if(desc){
            desc->numStandardMips = numStandardMips;
            desc->numPackedMips = texture->m_desc.mipLevels - numStandardMips;
            desc->startTileIndexInOverallResource = texture->m_tileByteSize > 0 ? static_cast<u32>(sparseReqs[0].imageMipTailOffset / texture->m_tileByteSize) : 0;
            desc->numTilesForPackedMips = texture->m_tileByteSize > 0 ? static_cast<u32>(sparseReqs[0].imageMipTailSize / texture->m_tileByteSize) : 0;
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
            &formatPropCount, formatProps.data());
    }

    if(!formatProps.empty()){
        tileWidth = formatProps[0].imageGranularity.width;
        tileHeight = formatProps[0].imageGranularity.height;
        tileDepth = formatProps[0].imageGranularity.depth;
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
                subresourceTilings[i].widthInTiles = (width + tileWidth - 1) / tileWidth;
                subresourceTilings[i].heightInTiles = (height + tileHeight - 1) / tileHeight;
                subresourceTilings[i].depthInTiles = (depth + tileDepth - 1) / tileDepth;
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

            startTileIndex += subresourceTilings[i].widthInTiles * subresourceTilings[i].heightInTiles * subresourceTilings[i].depthInTiles;
        }
    }

    if(numTiles){
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(m_context.device, texture->m_image, &memReqs);
        *numTiles = texture->m_tileByteSize > 0 ? static_cast<u32>(memReqs.size / texture->m_tileByteSize) : 0;
    }
}

void Device::updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue){
    Queue* queue = getQueue(executionQueue);
    if(!queue)
        return;

    queue->updateTextureTileMappings(texture, tileMappings, numTileMappings);
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackTexture(ITexture* /*pairedTexture*/, const SamplerFeedbackTextureDesc& /*desc*/){
    // Sampler feedback is a DX12-specific feature with no Vulkan equivalent
    return nullptr;
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackForNativeTexture(ObjectType /*objectType*/, Object /*texture*/, ITexture* /*pairedTexture*/){
    // Sampler feedback is a DX12-specific feature with no Vulkan equivalent
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

