// limztudio@gmail.com
// Stub implementations for ICommandList/IDevice pure virtual methods not yet fully implemented.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN

using __hidden::checked_cast;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Copy operations (staging texture overloads)


void CommandList::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice){
    // TODO: Implement staging texture copy (texture -> staging)
    NWB_ASSERT(false && "CommandList::copyTexture(IStagingTexture*, ..., ITexture*, ...) not yet implemented");
}

void CommandList::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice){
    // TODO: Implement staging texture copy (staging -> texture)
    NWB_ASSERT(false && "CommandList::copyTexture(ITexture*, ..., IStagingTexture*, ...) not yet implemented");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Sampler Feedback (stubs)


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
// CommandList - Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    // TODO: Implement push constants
    // Requires knowing the current pipeline layout and push constant range
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Draw Indirect


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
// CommandList - Ray Tracing (additional methods)


void CommandList::buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* as, IBuffer* instanceBuffer,
    u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags)
{
    // TODO: Implement buildTopLevelAccelStructFromBuffer
    NWB_ASSERT(false && "CommandList::buildTopLevelAccelStructFromBuffer not yet implemented");
}

void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc){
    // TODO: Implement cluster operations
    NWB_ASSERT(false && "CommandList::executeMultiIndirectClusterOperation not yet implemented");
}

void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    // Cooperative vector matrix conversion not yet supported
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - UAV Barriers and Tracking


void CommandList::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    // TODO: Track UAV barrier state per-texture for automatic barrier placement
}

void CommandList::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    // TODO: Track UAV barrier state per-buffer for automatic barrier placement
}

void CommandList::beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    // Inform state tracker of initial texture state
    // TODO: Store in state tracker for subsequent barrier generation
}

void CommandList::beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    // Inform state tracker of initial buffer state
    // TODO: Store in state tracker for subsequent barrier generation
}

ResourceStates::Mask CommandList::getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel){
    // TODO: Return tracked state from state tracker
    return ResourceStates::Unknown;
}

ResourceStates::Mask CommandList::getBufferState(IBuffer* buffer){
    // TODO: Return tracked state from state tracker
    return ResourceStates::Unknown;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Accessors


IDevice* CommandList::getDevice(){
    return m_device;
}

const CommandListParameters& CommandList::getDesc(){
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Texture tiling and sampler feedback stubs


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
