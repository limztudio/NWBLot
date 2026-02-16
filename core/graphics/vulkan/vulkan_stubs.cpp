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
    
    if(currentGraphicsState.pipeline){
        auto* gp = checked_cast<GraphicsPipeline*>(currentGraphicsState.pipeline);
        layout = gp->pipelineLayout;
    }
    else if(currentComputeState.pipeline){
        auto* cp = checked_cast<ComputePipeline*>(currentComputeState.pipeline);
        layout = cp->pipelineLayout;
    }
    else if(currentMeshletState.pipeline){
        auto* mp = checked_cast<MeshletPipeline*>(currentMeshletState.pipeline);
        layout = mp->pipelineLayout;
    }
    else if(currentRayTracingState.shaderTable){
        auto* rtp = currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            auto* rtpImpl = checked_cast<RayTracingPipeline*>(rtp);
            layout = rtpImpl->pipelineLayout;
        }
    }
    
    if(layout == VK_NULL_HANDLE){
        NWB_ASSERT_MSG(false, NWB_TEXT("setPushConstants: no active pipeline layout"));
        return;
    }
    
    vkCmdPushConstants(currentCmdBuf->cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, static_cast<u32>(byteSize), data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw Indirect


void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    if(!currentGraphicsState.indirectParams){
        NWB_ASSERT_MSG(false, NWB_TEXT("No indirect buffer bound for drawIndexedIndirect"));
        return;
    }
    auto* buffer = checked_cast<Buffer*>(currentGraphicsState.indirectParams);
    vkCmdDrawIndexedIndirect(currentCmdBuf->cmdBuf, buffer->buffer, offsetBytes, drawCount, sizeof(DrawIndexedIndirectArguments));
    currentCmdBuf->referencedResources.push_back(currentGraphicsState.indirectParams);
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
    buildInfo.dstAccelerationStructure = as->accelStruct;
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
        
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfo);
        
        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
    }
    
    currentCmdBuf->referencedResources.push_back(_as);
    currentCmdBuf->referencedResources.push_back(instanceBuffer);
}

void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc){
    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return;
    
    VkClusterAccelerationStructureOpTypeNV opType;
    switch(desc.params.type){
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
    switch(desc.params.mode){
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
    if(desc.params.flags & RayTracingClusterOperationFlags::FastTrace)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(!(desc.params.flags & RayTracingClusterOperationFlags::FastTrace) && (desc.params.flags & RayTracingClusterOperationFlags::FastBuild))
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(desc.params.flags & RayTracingClusterOperationFlags::AllowOMM)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;
    
    VkClusterAccelerationStructureInputInfoNV inputInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    inputInfo.maxAccelerationStructureCount = desc.params.maxArgCount;
    inputInfo.flags = opFlags;
    inputInfo.opType = opType;
    inputInfo.opMode = opMode;
    
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV };
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
    
    switch(desc.params.type){
    case RayTracingClusterOperationType::Move:{
        VkClusterAccelerationStructureTypeNV moveType;
        switch(desc.params.move.type){
        case RayTracingClusterOperationMoveType::BottomLevel:  moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        case RayTracingClusterOperationMoveType::ClusterLevel: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV; break;
        case RayTracingClusterOperationMoveType::Template:     moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV; break;
        default: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        }
        moveInput.type = moveType;
        moveInput.noMoveOverlap = (desc.params.flags & RayTracingClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        moveInput.maxMovedBytes = desc.params.move.maxBytes;
        inputInfo.opInput.pMoveObjects = &moveInput;
        break;
    }
    case RayTracingClusterOperationType::ClasBuild:
    case RayTracingClusterOperationType::ClasBuildTemplates:
    case RayTracingClusterOperationType::ClasInstantiateTemplates:{
        clusterInput.vertexFormat = __hidden_vulkan::ConvertFormat(desc.params.clas.vertexFormat);
        clusterInput.maxGeometryIndexValue = desc.params.clas.maxGeometryIndex;
        clusterInput.maxClusterUniqueGeometryCount = desc.params.clas.maxUniqueGeometryCount;
        clusterInput.maxClusterTriangleCount = desc.params.clas.maxTriangleCount;
        clusterInput.maxClusterVertexCount = desc.params.clas.maxVertexCount;
        clusterInput.maxTotalTriangleCount = desc.params.clas.maxTotalTriangleCount;
        clusterInput.maxTotalVertexCount = desc.params.clas.maxTotalVertexCount;
        clusterInput.minPositionTruncateBitCount = desc.params.clas.minPositionTruncateBitCount;
        inputInfo.opInput.pTriangleClusters = &clusterInput;
        break;
    }
    case RayTracingClusterOperationType::BlasBuild:{
        blasInput.maxClusterCountPerAccelerationStructure = desc.params.blas.maxClasPerBlasCount;
        blasInput.maxTotalClusterCount = desc.params.blas.maxTotalClasCount;
        inputInfo.opInput.pClustersBottomLevel = &blasInput;
        break;
    }
    default:
        break;
    }
    
    auto* indirectArgCountBuffer = desc.inIndirectArgCountBuffer ? checked_cast<Buffer*>(desc.inIndirectArgCountBuffer) : nullptr;
    auto* indirectArgsBuffer = desc.inIndirectArgsBuffer ? checked_cast<Buffer*>(desc.inIndirectArgsBuffer) : nullptr;
    auto* inOutAddressesBuffer = desc.inOutAddressesBuffer ? checked_cast<Buffer*>(desc.inOutAddressesBuffer) : nullptr;
    auto* outSizesBuffer = desc.outSizesBuffer ? checked_cast<Buffer*>(desc.outSizesBuffer) : nullptr;
    auto* outAccelerationStructuresBuffer = desc.outAccelerationStructuresBuffer ? checked_cast<Buffer*>(desc.outAccelerationStructuresBuffer) : nullptr;
    
    if(enableAutomaticBarriers){
        if(indirectArgsBuffer)
            setBufferState(desc.inIndirectArgsBuffer, ResourceStates::ShaderResource);
        if(indirectArgCountBuffer)
            setBufferState(desc.inIndirectArgCountBuffer, ResourceStates::ShaderResource);
        if(inOutAddressesBuffer)
            setBufferState(desc.inOutAddressesBuffer, ResourceStates::UnorderedAccess);
        if(outSizesBuffer)
            setBufferState(desc.outSizesBuffer, ResourceStates::UnorderedAccess);
        if(outAccelerationStructuresBuffer)
            setBufferState(desc.outAccelerationStructuresBuffer, ResourceStates::AccelStructWrite);
    }
    
    if(indirectArgCountBuffer)
        currentCmdBuf->referencedResources.push_back(desc.inIndirectArgCountBuffer);
    if(indirectArgsBuffer)
        currentCmdBuf->referencedResources.push_back(desc.inIndirectArgsBuffer);
    if(inOutAddressesBuffer)
        currentCmdBuf->referencedResources.push_back(desc.inOutAddressesBuffer);
    if(outSizesBuffer)
        currentCmdBuf->referencedResources.push_back(desc.outSizesBuffer);
    if(outAccelerationStructuresBuffer)
        currentCmdBuf->referencedResources.push_back(desc.outAccelerationStructuresBuffer);
    
    commitBarriers();
    
    BufferHandle scratchBufferHandle;
    Buffer* scratchBuffer = nullptr;
    if(desc.scratchSizeInBytes > 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = desc.scratchSizeInBytes;
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
    commandsInfo.scratchData = scratchBuffer ? scratchBuffer->deviceAddress : 0;
    commandsInfo.dstImplicitData = outAccelerationStructuresBuffer ? outAccelerationStructuresBuffer->deviceAddress + desc.outAccelerationStructuresOffsetInBytes : 0;
    
    if(inOutAddressesBuffer){
        commandsInfo.dstAddressesArray.deviceAddress = inOutAddressesBuffer->deviceAddress + desc.inOutAddressesOffsetInBytes;
        commandsInfo.dstAddressesArray.stride = inOutAddressesBuffer->getDescription().structStride;
        commandsInfo.dstAddressesArray.size = inOutAddressesBuffer->getDescription().byteSize - desc.inOutAddressesOffsetInBytes;
    }
    
    if(outSizesBuffer){
        commandsInfo.dstSizesArray.deviceAddress = outSizesBuffer->deviceAddress + desc.outSizesOffsetInBytes;
        commandsInfo.dstSizesArray.stride = outSizesBuffer->getDescription().structStride;
        commandsInfo.dstSizesArray.size = outSizesBuffer->getDescription().byteSize - desc.outSizesOffsetInBytes;
    }
    
    if(indirectArgsBuffer){
        commandsInfo.srcInfosArray.deviceAddress = indirectArgsBuffer->deviceAddress + desc.inIndirectArgsOffsetInBytes;
        commandsInfo.srcInfosArray.stride = indirectArgsBuffer->getDescription().structStride;
        commandsInfo.srcInfosArray.size = indirectArgsBuffer->getDescription().byteSize - desc.inIndirectArgsOffsetInBytes;
    }
    
    commandsInfo.srcInfosCount = indirectArgCountBuffer ? indirectArgCountBuffer->deviceAddress + desc.inIndirectArgCountOffsetInBytes : 0;
    commandsInfo.addressResolutionFlags = static_cast<VkClusterAccelerationStructureAddressResolutionFlagsNV>(0);
    
    vkCmdBuildClusterAccelerationStructureIndirectNV(currentCmdBuf->cmdBuf, &commandsInfo);
    
    if(scratchBufferHandle)
        currentCmdBuf->referencedStagingBuffers.push_back(Move(scratchBufferHandle));
}

void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    if(!m_context.extensions.NV_cooperative_vector)
        return;
    
    if(numDescs == 0)
        return;
    
    Vector<VkConvertCooperativeVectorMatrixInfoNV> vkConvertDescs;
    vkConvertDescs.reserve(numDescs);
    
    Vector<usize> dstSizes;
    dstSizes.reserve(numDescs);
    
    for(usize i = 0; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& desc = convertDescs[i];
        
        if(!desc.src.buffer || !desc.dst.buffer)
            continue;
        
        if(enableAutomaticBarriers){
            setBufferState(desc.src.buffer, ResourceStates::ShaderResource);
            setBufferState(desc.dst.buffer, ResourceStates::UnorderedAccess);
        }
        
        VkConvertCooperativeVectorMatrixInfoNV vkDesc = { VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV };
        vkDesc.srcSize = desc.src.size;
        vkDesc.srcData.deviceAddress = checked_cast<Buffer*>(desc.src.buffer)->deviceAddress + desc.src.offset;
        vkDesc.pDstSize = &dstSizes.emplace_back(desc.dst.size);
        vkDesc.dstData.deviceAddress = checked_cast<Buffer*>(desc.dst.buffer)->deviceAddress + desc.dst.offset;
        vkDesc.srcComponentType = __hidden_vulkan::ConvertCoopVecDataType(desc.src.type);
        vkDesc.dstComponentType = __hidden_vulkan::ConvertCoopVecDataType(desc.dst.type);
        vkDesc.numRows = desc.numRows;
        vkDesc.numColumns = desc.numColumns;
        
        vkDesc.srcLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(desc.src.layout);
        vkDesc.srcStride = desc.src.stride != 0
            ? desc.src.stride
            : GetCooperativeVectorOptimalMatrixStride(desc.src.type, desc.src.layout, desc.numRows, desc.numColumns);
        
        vkDesc.dstLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(desc.dst.layout);
        vkDesc.dstStride = desc.dst.stride != 0
            ? desc.dst.stride
            : GetCooperativeVectorOptimalMatrixStride(desc.dst.type, desc.dst.layout, desc.numRows, desc.numColumns);
        
        vkConvertDescs.push_back(vkDesc);
    }
    
    commitBarriers();
    
    if(!vkConvertDescs.empty())
        vkCmdConvertCooperativeVectorMatrixNV(currentCmdBuf->cmdBuf, static_cast<u32>(vkConvertDescs.size()), vkConvertDescs.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UAV Barriers and Tracking


void CommandList::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    if(!texture)
        return;
    stateTracker->setEnableUavBarriersForTexture(texture, enableBarriers);
}

void CommandList::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    if(!buffer)
        return;
    stateTracker->setEnableUavBarriersForBuffer(buffer, enableBarriers);
}

void CommandList::beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    stateTracker->beginTrackingTexture(texture, subresources, stateBits);
}

void CommandList::beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    stateTracker->beginTrackingBuffer(buffer, stateBits);
}

ResourceStates::Mask CommandList::getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel){
    return stateTracker->getTextureState(texture, arraySlice, mipLevel);
}

ResourceStates::Mask CommandList::getBufferState(IBuffer* buffer){
    return stateTracker->getBufferState(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors


IDevice* CommandList::getDevice(){
    return &m_device;
}

const CommandListParameters& CommandList::getDescription(){
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture tiling and sampler feedback stubs


void Device::getTextureTiling(ITexture* _texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings){
    auto* texture = checked_cast<Texture*>(_texture);
    u32 numStandardMips = 0;
    u32 tileWidth = 1;
    u32 tileHeight = 1;
    u32 tileDepth = 1;
    
    uint32_t sparseReqCount = 0;
    vkGetImageSparseMemoryRequirements(m_context.device, texture->image, &sparseReqCount, nullptr);
    
    Vector<VkSparseImageMemoryRequirements> sparseReqs(sparseReqCount);
    if(sparseReqCount > 0)
        vkGetImageSparseMemoryRequirements(m_context.device, texture->image, &sparseReqCount, sparseReqs.data());
    
    if(!sparseReqs.empty()){
        numStandardMips = sparseReqs[0].imageMipTailFirstLod;
        
        if(desc){
            desc->numStandardMips = numStandardMips;
            desc->numPackedMips = texture->desc.mipLevels - numStandardMips;
            desc->startTileIndexInOverallResource = texture->tileByteSize > 0 ? static_cast<u32>(sparseReqs[0].imageMipTailOffset / texture->tileByteSize) : 0;
            desc->numTilesForPackedMips = texture->tileByteSize > 0 ? static_cast<u32>(sparseReqs[0].imageMipTailSize / texture->tileByteSize) : 0;
        }
    }
    
    uint32_t formatPropCount = 0;
    vkGetPhysicalDeviceSparseImageFormatProperties(
        m_context.physicalDevice,
        texture->imageInfo.format,
        texture->imageInfo.imageType,
        texture->imageInfo.samples,
        texture->imageInfo.usage,
        texture->imageInfo.tiling,
        &formatPropCount, nullptr
        );
    
    Vector<VkSparseImageFormatProperties> formatProps(formatPropCount);
    if(formatPropCount > 0){
        vkGetPhysicalDeviceSparseImageFormatProperties(
            m_context.physicalDevice,
            texture->imageInfo.format,
            texture->imageInfo.imageType,
            texture->imageInfo.samples,
            texture->imageInfo.usage,
            texture->imageInfo.tiling,
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
        *subresourceTilingsNum = Min(*subresourceTilingsNum, texture->desc.mipLevels);
        u32 startTileIndex = 0;
        
        u32 width = texture->desc.width;
        u32 height = texture->desc.height;
        u32 depth = texture->desc.depth;
        
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
        vkGetImageMemoryRequirements(m_context.device, texture->image, &memReqs);
        *numTiles = texture->tileByteSize > 0 ? static_cast<u32>(memReqs.size / texture->tileByteSize) : 0;
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

