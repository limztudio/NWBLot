// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN

using __hidden_vulkan::checked_cast;
using __hidden_vulkan::ConvertFormat;
using __hidden_vulkan::getBufferDeviceAddress;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copy operations (staging texture overloads)


void CommandList::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice){
    StagingTexture* staging = checked_cast<StagingTexture*>(dest);
    Texture* texture = checked_cast<Texture*>(src);
    
    TextureSlice resolvedSrc = srcSlice.resolve(texture->desc);
    TextureSlice resolvedDst = destSlice.resolve(staging->desc);
    
    const FormatInfo& formatInfo = GetFormatInfo(texture->desc.format);
    
    // Compute buffer offset from destSlice position
    u64 bufferOffset = 0;
    if(resolvedDst.mipLevel > 0 || resolvedDst.arraySlice > 0){
        // Simple linear offset calculation for the staging buffer
        u32 rowPitch = (resolvedDst.width / formatInfo.blockSize) * formatInfo.bytesPerBlock;
        u32 slicePitch = rowPitch * (resolvedDst.height / formatInfo.blockSize);
        bufferOffset = static_cast<u64>(resolvedDst.z) * slicePitch + static_cast<u64>(resolvedDst.y / formatInfo.blockSize) * rowPitch + static_cast<u64>(resolvedDst.x / formatInfo.blockSize) * formatInfo.bytesPerBlock;
    }
    
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0; // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = resolvedSrc.mipLevel;
    region.imageSubresource.baseArrayLayer = resolvedSrc.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<i32>(resolvedSrc.x), static_cast<i32>(resolvedSrc.y), static_cast<i32>(resolvedSrc.z) };
    region.imageExtent = { resolvedSrc.width, resolvedSrc.height, resolvedSrc.depth };
    
    vkCmdCopyImageToBuffer(currentCmdBuf->cmdBuf, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(src);
    currentCmdBuf->referencedResources.push_back(dest);
}

void CommandList::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice){
    Texture* texture = checked_cast<Texture*>(dest);
    StagingTexture* staging = checked_cast<StagingTexture*>(src);
    
    TextureSlice resolvedDst = destSlice.resolve(texture->desc);
    TextureSlice resolvedSrc = srcSlice.resolve(staging->desc);
    
    const FormatInfo& formatInfo = GetFormatInfo(staging->desc.format);
    
    // Compute buffer offset from srcSlice position
    u64 bufferOffset = 0;
    if(resolvedSrc.mipLevel > 0 || resolvedSrc.arraySlice > 0){
        u32 rowPitch = (resolvedSrc.width / formatInfo.blockSize) * formatInfo.bytesPerBlock;
        u32 slicePitch = rowPitch * (resolvedSrc.height / formatInfo.blockSize);
        bufferOffset = static_cast<u64>(resolvedSrc.z) * slicePitch + static_cast<u64>(resolvedSrc.y / formatInfo.blockSize) * rowPitch + static_cast<u64>(resolvedSrc.x / formatInfo.blockSize) * formatInfo.bytesPerBlock;
    }
    
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0; // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = resolvedDst.mipLevel;
    region.imageSubresource.baseArrayLayer = resolvedDst.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<i32>(resolvedDst.x), static_cast<i32>(resolvedDst.y), static_cast<i32>(resolvedDst.z) };
    region.imageExtent = { resolvedDst.width, resolvedDst.height, resolvedDst.depth };
    
    vkCmdCopyBufferToImage(currentCmdBuf->cmdBuf, staging->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(dest);
    currentCmdBuf->referencedResources.push_back(src);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler Feedback (stubs)


void CommandList::clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture){
    // Sampler feedback not supported in Vulkan backend
}

void CommandList::decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format){
    // Sampler feedback not supported in Vulkan backend
}

void CommandList::setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits){
    // Sampler feedback not supported in Vulkan backend
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    VkPipelineLayout layout = VK_NULL_HANDLE;
    
    // Determine the current pipeline layout from active state
    if(currentGraphicsState.pipeline){
        GraphicsPipeline* gp = checked_cast<GraphicsPipeline*>(currentGraphicsState.pipeline);
        layout = gp->pipelineLayout;
    }
    else if(currentComputeState.pipeline){
        ComputePipeline* cp = checked_cast<ComputePipeline*>(currentComputeState.pipeline);
        layout = cp->pipelineLayout;
    }
    else if(currentMeshletState.pipeline){
        MeshletPipeline* mp = checked_cast<MeshletPipeline*>(currentMeshletState.pipeline);
        layout = mp->pipelineLayout;
    }
    else if(currentRayTracingState.shaderTable){
        IRayTracingPipeline* rtp = currentRayTracingState.shaderTable->getPipeline();
        if(rtp){
            RayTracingPipeline* rtpImpl = checked_cast<RayTracingPipeline*>(rtp);
            layout = rtpImpl->pipelineLayout;
        }
    }
    
    if(layout == VK_NULL_HANDLE){
        NWB_ASSERT(false && "setPushConstants: no active pipeline layout");
        return;
    }
    
    vkCmdPushConstants(currentCmdBuf->cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, static_cast<u32>(byteSize), data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw Indirect


void CommandList::drawIndexedIndirect(u32 offsetBytes, u32 drawCount){
    if(!currentGraphicsState.indirectParams){
        NWB_ASSERT(false && "No indirect buffer bound for drawIndexedIndirect");
        return;
    }
    Buffer* buffer = checked_cast<Buffer*>(currentGraphicsState.indirectParams);
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
    
    if(!m_context->extensions.KHR_acceleration_structure)
        return;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    // Set up geometry for instances using the provided buffer directly
    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = getBufferDeviceAddress(instanceBuffer, instanceBufferOffset);
    
    // Set up build info
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
    
    // Query scratch buffer size
    u32 primitiveCount = static_cast<u32>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                             &buildInfo, &primitiveCount, &sizeInfo);
    
    // Allocate scratch buffer
    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";
    
    BufferHandle scratchBuffer = m_device->createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer.get());
        
        // Build acceleration structure
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfo);
        
        // Track buffers
        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
    }
    
    currentCmdBuf->referencedResources.push_back(_as);
    currentCmdBuf->referencedResources.push_back(instanceBuffer);
}

void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc){
    // TODO: Implement cluster operations
    NWB_ASSERT(false && "CommandList::executeMultiIndirectClusterOperation not yet implemented");
}

void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    // Cooperative vector matrix conversion not yet supported
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UAV Barriers and Tracking


void CommandList::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    // No-op: UAV barrier tracking per-texture is an advanced optimization not yet needed
    (void)texture;
    (void)enableBarriers;
}

void CommandList::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    // No-op: UAV barrier tracking per-buffer is an advanced optimization not yet needed
    (void)buffer;
    (void)enableBarriers;
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
    return m_device;
}

const CommandListParameters& CommandList::getDescription(){
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture tiling and sampler feedback stubs


void Device::getTextureTiling(ITexture* /*texture*/, u32* numTiles, PackedMipDesc* /*desc*/, TileShape* /*tileShape*/, u32* subresourceTilingsNum, SubresourceTiling* /*subresourceTilings*/){
    // Sparse/tiled resources not yet implemented in this backend
    if(numTiles) *numTiles = 0;
    if(subresourceTilingsNum) *subresourceTilingsNum = 0;
}

void Device::updateTextureTileMappings(ITexture* /*texture*/, const TextureTilesMapping* /*tileMappings*/, u32 /*numTileMappings*/, CommandQueue::Enum /*executionQueue*/){
    // Sparse/tiled resources not yet implemented in this backend
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackTexture(ITexture* /*pairedTexture*/, const SamplerFeedbackTextureDesc& /*desc*/){
    // Sampler feedback not supported in Vulkan backend
    return nullptr;
}

SamplerFeedbackTextureHandle Device::createSamplerFeedbackForNativeTexture(ObjectType /*objectType*/, Object /*texture*/, ITexture* /*pairedTexture*/){
    // Sampler feedback not supported in Vulkan backend
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
