// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "texture_copy_contract.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline VkBufferImageCopy BuildStagingTextureCopyRegion(
    const TextureSlice& stagingSlice,
    const TextureSlice& imageSlice,
    const VkImageAspectFlags aspectMask,
    const VulkanDetail::StagingTextureMipLayout& stagingMipLayout,
    const VulkanDetail::TextureFormatBlockLayout& stagingFormatLayout,
    const u64 stagingArrayByteSize
){
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
    const u64 bufferOffset = VulkanDetail::ComputeStagingTextureOffset(
        stagingSlice,
        stagingMipLayout,
        stagingFormatLayout,
        stagingArrayByteSize,
        nullptr,
        &bufferRowLength,
        &bufferImageHeight,
        nullptr
    );

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = bufferRowLength;
    region.bufferImageHeight = bufferImageHeight;
    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspectMask, imageSlice.mipLevel, imageSlice.arraySlice);
    region.imageOffset = { static_cast<i32>(imageSlice.x), static_cast<i32>(imageSlice.y), static_cast<i32>(imageSlice.z) };
    region.imageExtent = { imageSlice.width, imageSlice.height, imageSlice.depth };
    return region;
}

[[nodiscard]] inline bool ResolveMipExtentsMatch(
    const TextureDesc& destinationDesc,
    const TextureSubresourceSet& destinationSubresources,
    const TextureDesc& sourceDesc,
    const TextureSubresourceSet& sourceSubresources
){
    for(MipLevel mipOffset = 0u; mipOffset < sourceSubresources.numMipLevels; ++mipOffset){
        const VkExtent3D sourceExtent = VulkanDetail::GetTextureMipExtent(
            sourceDesc,
            sourceSubresources.baseMipLevel + mipOffset
        );
        const VkExtent3D destinationExtent = VulkanDetail::GetTextureMipExtent(
            destinationDesc,
            destinationSubresources.baseMipLevel + mipOffset
        );
        if(
            sourceExtent.width != destinationExtent.width
            || sourceExtent.height != destinationExtent.height
            || sourceExtent.depth != destinationExtent.depth
        )
            return false;
    }
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::copyTexture(Texture* destResource, const TextureSlice& destSlice, Texture* srcResource, const TextureSlice& srcSlice){
    constexpr const tchar* s_OperationName = NWB_TEXT("copy texture");
    if(!destResource || !srcResource){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination textures must be non-null"));
        return;
    }

    Texture& dest = *destResource;
    Texture& src = *srcResource;
    if(&src.m_context != &m_context || &dest.m_context != &m_context){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination textures must belong to this device"));
        return;
    }
    if(src.m_image == VK_NULL_HANDLE || dest.m_image == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination native image handles must be non-null"));
        return;
    }
    if(srcResource == destResource || src.m_image == dest.m_image){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination must be distinct native images"));
        return;
    }

    VulkanTextureDetail::TextureCopyContract contract;
    if(!VulkanTextureDetail::ResolveTextureCopyContract(
        src.m_desc,
        srcSlice,
        dest.m_desc,
        destSlice,
        contract
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination slices violate the image-copy contract"));
        return;
    }
    if(
        src.m_aspectMask != contract.aspectMask
        || dest.m_aspectMask != contract.aspectMask
        || src.m_formatLayout.blockWidth != contract.formatLayout.blockWidth
        || src.m_formatLayout.blockHeight != contract.formatLayout.blockHeight
        || src.m_formatLayout.bytesPerBlock != contract.formatLayout.bytesPerBlock
        || dest.m_formatLayout.blockWidth != contract.formatLayout.blockWidth
        || dest.m_formatLayout.blockHeight != contract.formatLayout.blockHeight
        || dest.m_formatLayout.bytesPerBlock != contract.formatLayout.bytesPerBlock
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture aspect or block metadata disagrees with its format"));
        return;
    }
    if(
        !VulkanTextureDetail::IsTextureImageInfoConsistent(src.m_desc, src.m_imageInfo)
        || !VulkanTextureDetail::IsTextureImageInfoConsistent(dest.m_desc, dest.m_imageInfo)
        || src.m_imageInfo.imageType != contract.imageType
        || dest.m_imageInfo.imageType != contract.imageType
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture descriptions and native image metadata must agree"));
        return;
    }
    if(
        (src.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u
        || (dest.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination images require transfer usage"));
        return;
    }

    const ResourceStates::Mask sourcePermanentState = m_stateTracker.getPermanentTextureState(srcResource);
    const ResourceStates::Mask destinationPermanentState = m_stateTracker.getPermanentTextureState(destResource);
    if(
        (sourcePermanentState != ResourceStates::Unknown && sourcePermanentState != ResourceStates::CopySource)
        || (destinationPermanentState != ResourceStates::Unknown && destinationPermanentState != ResourceStates::CopyDest)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("copy states conflict with a permanent texture state"));
        return;
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, src.m_imageInfo.format, &formatProperties);
    constexpr VkFormatFeatureFlags s_RequiredFormatFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
    ;
    if((formatProperties.optimalTilingFeatures & s_RequiredFormatFeatures) != s_RequiredFormatFeatures){
        rejectCommandRecording(s_OperationName, NWB_TEXT("native format lacks optimal-tiling transfer support"));
        return;
    }

    VkImageFormatProperties sourceFormatProperties{};
    const VkResult sourceFormatResult = vkGetPhysicalDeviceImageFormatProperties(
        m_context.physicalDevice,
        src.m_imageInfo.format,
        src.m_imageInfo.imageType,
        src.m_imageInfo.tiling,
        src.m_imageInfo.usage,
        src.m_imageInfo.flags,
        &sourceFormatProperties
    );
    VkImageFormatProperties destinationFormatProperties{};
    const VkResult destinationFormatResult = vkGetPhysicalDeviceImageFormatProperties(
        m_context.physicalDevice,
        dest.m_imageInfo.format,
        dest.m_imageInfo.imageType,
        dest.m_imageInfo.tiling,
        dest.m_imageInfo.usage,
        dest.m_imageInfo.flags,
        &destinationFormatProperties
    );
    if(
        sourceFormatResult != VK_SUCCESS
        || destinationFormatResult != VK_SUCCESS
        || !VulkanTextureDetail::IsTextureImageWithinFormatLimits(src.m_imageInfo, sourceFormatProperties)
        || !VulkanTextureDetail::IsTextureImageWithinFormatLimits(dest.m_imageInfo, destinationFormatProperties)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("native image formats do not support the requested shapes"));
        return;
    }

    VkImageCopy region{};
    region.srcSubresource = VulkanDetail::BuildImageSubresourceLayers(
        contract.aspectMask,
        contract.sourceSlice.mipLevel,
        contract.sourceSlice.arraySlice
    );
    region.srcOffset = {
        static_cast<i32>(contract.sourceSlice.x),
        static_cast<i32>(contract.sourceSlice.y),
        static_cast<i32>(contract.sourceSlice.z),
    };
    region.dstSubresource = VulkanDetail::BuildImageSubresourceLayers(
        contract.aspectMask,
        contract.destinationSlice.mipLevel,
        contract.destinationSlice.arraySlice
    );
    region.dstOffset = {
        static_cast<i32>(contract.destinationSlice.x),
        static_cast<i32>(contract.destinationSlice.y),
        static_cast<i32>(contract.destinationSlice.z),
    };
    region.extent = {
        contract.destinationSlice.width,
        contract.destinationSlice.height,
        contract.destinationSlice.depth,
    };

    switch(contract.queueRequirement){
    case VulkanTextureDetail::TextureCopyQueueRequirement::Graphics:
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
            return;
        break;
    case VulkanTextureDetail::TextureCopyQueueRequirement::ComputeOrGraphics:
        if(!recordAndValidateAnyCommandCapability(
            GpuQueueCapability::Compute | GpuQueueCapability::Graphics,
            s_OperationName
        ))
            return;
        break;
    default:
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, s_OperationName))
            return;
        break;
    }

    endActiveRenderPass();
    if(m_commandRecordingFailed)
        return;
    setTextureState(
        srcResource,
        TextureSubresourceSet(contract.sourceSlice.mipLevel, 1u, contract.sourceSlice.arraySlice, 1u),
        ResourceStates::CopySource
    );
    if(m_commandRecordingFailed)
        return;
    setTextureState(
        destResource,
        TextureSubresourceSet(contract.destinationSlice.mipLevel, 1u, contract.destinationSlice.arraySlice, 1u),
        ResourceStates::CopyDest
    );
    if(m_commandRecordingFailed)
        return;

    vkCmdCopyImage(m_currentCmdBuf->m_cmdBuf, src.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(srcResource);
    retainResource(destResource);
}

void CommandList::copyTexture(StagingTexture* dest, const TextureSlice& destSlice, Texture* src, const TextureSlice& srcSlice){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("copy texture to staging texture"), NWB_TEXT("resource is invalid"), dest, src))
        return;

    VkBufferImageCopy region{};
    if(!prepareStagingTextureCopy(
        *dest,
        destSlice,
        *src,
        srcSlice,
        NWB_TEXT("copy texture to staging texture"),
        NWB_TEXT("source texture must be single-sampled"),
        region
    ))
        return;
    const bool depthStencilCopy = (src->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u;
    const GpuQueueCapability::Mask requiredCapabilities = depthStencilCopy
        ? GpuQueueCapability::Graphics
        : GpuQueueCapability::Transfer
    ;
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("copy texture to staging texture")))
        return;

    endActiveRenderPass();
    setTextureState(src, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopySource);
    if(m_commandRecordingFailed)
        return;

    vkCmdCopyImageToBuffer(m_currentCmdBuf->m_cmdBuf, src->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_buffer, 1, &region);

    retainResource(src);
    retainResource(dest);
}

void CommandList::copyTexture(Texture* dest, const TextureSlice& destSlice, StagingTexture* src, const TextureSlice& srcSlice){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("copy staging texture to texture"), NWB_TEXT("resource is invalid"), dest, src))
        return;

    VkBufferImageCopy region{};
    if(!prepareStagingTextureCopy(
        *src,
        srcSlice,
        *dest,
        destSlice,
        NWB_TEXT("copy staging texture to texture"),
        NWB_TEXT("destination texture must be single-sampled"),
        region
    ))
        return;
    const bool depthStencilCopy = (dest->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u;
    const GpuQueueCapability::Mask requiredCapabilities = depthStencilCopy
        ? GpuQueueCapability::Graphics
        : GpuQueueCapability::Transfer
    ;
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("copy staging texture to texture")))
        return;

    endActiveRenderPass();
    setTextureState(dest, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, src->m_buffer, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(dest);
    retainResource(src);
}

bool CommandList::tryWriteTexture(
    Texture* destResource,
    u32 arraySlice,
    u32 mipLevel,
    const void* data,
    usize rowPitch,
    usize depthPitch
){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("write texture"), NWB_TEXT("destination texture is null"), destResource))
        return false;

    Texture& dest = *destResource;
    const TextureDesc& texDesc = dest.m_desc;
#if defined(NWB_DEBUG)
    if(texDesc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: destination texture must be single-sampled"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: destination texture must be single-sampled"));
        return false;
    }

    if(!data){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: source data is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: source data is null"));
        return false;
    }

    if(mipLevel >= texDesc.mipLevels || arraySlice >= texDesc.arraySize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: subresource is out of bounds"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: subresource is out of bounds"));
        return false;
    }
#endif

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(texDesc, mipLevel);

    if(!VulkanDetail::ValidateBufferImageCopyAspectMask(dest.m_aspectMask, NWB_TEXT("write texture")))
        return false;

    VulkanDetail::BufferImageCopyLayout copyLayout;
    if(
        !VulkanDetail::BuildBufferImageCopyLayout(
            mipExtent,
            dest.m_formatLayout,
            static_cast<u64>(rowPitch),
            static_cast<u64>(depthPitch),
            VulkanDetail::BufferImageCopyRequiredSize::PaddedSlices,
            VulkanDetail::BufferImageCopyPitchFields::EmitExplicit,
            NWB_TEXT("write texture"),
            copyLayout
        )
    ){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: invalid buffer-image copy layout"));
        return false;
    }
    if(copyLayout.requiredSize > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write texture: upload size exceeds addressable memory"));
        return false;
    }
    const bool depthStencilCopy = (dest.m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u;
    const GpuQueueCapability::Mask requiredCapabilities = depthStencilCopy
        ? GpuQueueCapability::Graphics
        : GpuQueueCapability::Transfer
    ;
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("write texture")))
        return false;

    endActiveRenderPass();
    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    const usize uploadSize = static_cast<usize>(copyLayout.requiredSize);
    if(!prepareUploadStaging(data, uploadSize, NWB_TEXT("writeTexture"), stagingBuffer, stagingOffset))
        return false;

    setTextureState(destResource, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return false;

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = copyLayout.bufferRowLength;
    region.bufferImageHeight = copyLayout.bufferImageHeight;
    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(dest.m_aspectMask, mipLevel, arraySlice);
    region.imageExtent = mipExtent;

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(destResource);
    retainStagingBuffer(*stagingBuffer);
    return true;
}

void CommandList::writeTexture(Texture* destResource, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch){
    if(!tryWriteTexture(destResource, arraySlice, mipLevel, data, rowPitch, depthPitch))
        return;
}

void CommandList::resolveTexture(Texture* destResource, const TextureSubresourceSet& dstSubresources, Texture* srcResource, const TextureSubresourceSet& srcSubresources){
    constexpr const tchar* s_OperationName = NWB_TEXT("resolve texture");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(!destResource || !srcResource){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination textures must be non-null"));
        return;
    }

    Texture& dest = *destResource;
    Texture& src = *srcResource;
    if(&src.m_context != &m_context || &dest.m_context != &m_context){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination textures must belong to this device"));
        return;
    }
    if(src.m_image == VK_NULL_HANDLE || dest.m_image == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination native image handles must be non-null"));
        return;
    }
    if(srcResource == destResource || src.m_image == dest.m_image){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination must be distinct native images"));
        return;
    }

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(src.m_desc, TextureSubresourceMipResolve::Range);
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(dest.m_desc, TextureSubresourceMipResolve::Range);
    if(
        !VulkanDetail::IsTextureSubresourceRangeValid(resolvedSrc)
        || !VulkanDetail::IsTextureSubresourceRangeValid(resolvedDst)
    ){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("source and destination subresource ranges must resolve inside their textures")
        );
        return;
    }
    if(
        src.m_desc.sampleCount <= 1u
        || dest.m_desc.sampleCount != 1u
        || !VulkanDetail::IsSupportedSampleCount(src.m_desc.sampleCount)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source must be multisampled and destination must be single-sampled"));
        return;
    }
    if(
        src.m_desc.format != dest.m_desc.format
        || src.m_imageInfo.format == VK_FORMAT_UNDEFINED
        || src.m_imageInfo.format != dest.m_imageInfo.format
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination native formats must match exactly"));
        return;
    }
    if(src.m_aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || dest.m_aspectMask != VK_IMAGE_ASPECT_COLOR_BIT){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination must have the color aspect only"));
        return;
    }
    if(
        resolvedSrc.numMipLevels != resolvedDst.numMipLevels
        || resolvedSrc.numArraySlices != resolvedDst.numArraySlices
        || !VulkanTextureDetail::ResolveMipExtentsMatch(dest.m_desc, resolvedDst, src.m_desc, resolvedSrc)
    ){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("source and destination subresource counts and mip extents must match")
        );
        return;
    }
    if(
        (dest.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u
        || (src.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination images require transfer usage"));
        return;
    }
    if(
        !VulkanTextureDetail::IsTextureImageInfoConsistent(src.m_desc, src.m_imageInfo)
        || !VulkanTextureDetail::IsTextureImageInfoConsistent(dest.m_desc, dest.m_imageInfo)
        || src.m_imageInfo.imageType != dest.m_imageInfo.imageType
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture descriptions and native image metadata must agree"));
        return;
    }

    const ResourceStates::Mask sourcePermanentState = m_stateTracker.getPermanentTextureState(srcResource);
    const ResourceStates::Mask destinationPermanentState = m_stateTracker.getPermanentTextureState(destResource);
    if(
        (sourcePermanentState != ResourceStates::Unknown && sourcePermanentState != ResourceStates::ResolveSource)
        || (destinationPermanentState != ResourceStates::Unknown && destinationPermanentState != ResourceStates::ResolveDest)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("resolve states conflict with a permanent texture state"));
        return;
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, src.m_imageInfo.format, &formatProperties);
    constexpr VkFormatFeatureFlags s_RequiredFormatFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
        | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
    ;
    if((formatProperties.optimalTilingFeatures & s_RequiredFormatFeatures) != s_RequiredFormatFeatures){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("native format lacks optimal-tiling transfer or color-attachment support")
        );
        return;
    }

    VkImageFormatProperties sourceFormatProperties{};
    const VkResult sourceFormatResult = vkGetPhysicalDeviceImageFormatProperties(
        m_context.physicalDevice,
        src.m_imageInfo.format,
        src.m_imageInfo.imageType,
        src.m_imageInfo.tiling,
        src.m_imageInfo.usage,
        src.m_imageInfo.flags,
        &sourceFormatProperties
    );
    VkImageFormatProperties destinationFormatProperties{};
    const VkResult destinationFormatResult = vkGetPhysicalDeviceImageFormatProperties(
        m_context.physicalDevice,
        dest.m_imageInfo.format,
        dest.m_imageInfo.imageType,
        dest.m_imageInfo.tiling,
        dest.m_imageInfo.usage,
        dest.m_imageInfo.flags,
        &destinationFormatProperties
    );
    if(
        sourceFormatResult != VK_SUCCESS
        || destinationFormatResult != VK_SUCCESS
        || !VulkanTextureDetail::IsTextureImageWithinFormatLimits(
            src.m_imageInfo,
            sourceFormatProperties
        )
        || !VulkanTextureDetail::IsTextureImageWithinFormatLimits(
            dest.m_imageInfo,
            destinationFormatProperties
        )
    ){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("native image formats do not support the requested shapes and sample counts")
        );
        return;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureResolveArena);
    Vector<VkImageResolve, Alloc::ScratchArena> regions(resolvedSrc.numMipLevels, scratchArena);

    for(MipLevel mipOffset = 0; mipOffset < resolvedSrc.numMipLevels; ++mipOffset){
        const MipLevel srcMipLevel = resolvedSrc.baseMipLevel + mipOffset;
        const MipLevel dstMipLevel = resolvedDst.baseMipLevel + mipOffset;
        const VkExtent3D srcExtent = VulkanDetail::GetTextureMipExtent(src.m_desc, srcMipLevel);

        VkImageResolve region{};
        region.srcSubresource = VulkanDetail::BuildImageSubresourceLayers(
            VK_IMAGE_ASPECT_COLOR_BIT,
            srcMipLevel,
            resolvedSrc.baseArraySlice,
            resolvedSrc.numArraySlices
        );
        region.srcOffset = { 0, 0, 0 };
        region.dstSubresource = VulkanDetail::BuildImageSubresourceLayers(
            VK_IMAGE_ASPECT_COLOR_BIT,
            dstMipLevel,
            resolvedDst.baseArraySlice,
            resolvedDst.numArraySlices
        );
        region.dstOffset = { 0, 0, 0 };
        region.extent = srcExtent;
        regions[mipOffset] = region;
    }

    endActiveRenderPass();
    if(m_commandRecordingFailed)
        return;
    setTextureState(srcResource, resolvedSrc, ResourceStates::ResolveSource);
    if(m_commandRecordingFailed)
        return;
    setTextureState(destResource, resolvedDst, ResourceStates::ResolveDest);
    if(m_commandRecordingFailed)
        return;

    vkCmdResolveImage(
        m_currentCmdBuf->m_cmdBuf,
        src.m_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dest.m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<u32>(regions.size()),
        regions.data()
    );

    retainResource(srcResource);
    retainResource(destResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging texture transfer preparation


bool CommandList::prepareStagingTextureCopy(
    StagingTexture& stagingResource,
    const TextureSlice& stagingSlice,
    Texture& textureResource,
    const TextureSlice& textureSlice,
    const tchar* operationName,
    const tchar* singleSampleRequirement,
    VkBufferImageCopy& outRegion
)const{
    const TextureDesc& stagingDesc = stagingResource.m_desc;
    const TextureDesc& textureDesc = textureResource.m_desc;
    if(textureDesc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, singleSampleRequirement);
#if defined(NWB_DEBUG)
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, singleSampleRequirement);
#endif
        return false;
    }
    if(!VulkanDetail::ValidateBufferImageCopyAspectMask(stagingResource.m_aspectMask, operationName))
        return false;
#if defined(NWB_DEBUG)
    if(textureDesc.format != stagingDesc.format){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: source and destination formats do not match"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: source and destination formats do not match"), operationName);
        return false;
    }

    TextureSlice resolvedStaging;
    TextureSlice resolvedTexture;
    if(!VulkanDetail::DebugResolveTextureSlice(stagingDesc, stagingSlice, stagingResource.m_formatLayout, operationName, NWB_TEXT("staging slice is outside the texture"), resolvedStaging))
        return false;
    if(!VulkanDetail::DebugResolveTextureSlice(textureDesc, textureSlice, textureResource.m_formatLayout, operationName, NWB_TEXT("texture slice is outside the texture"), resolvedTexture))
        return false;

    if(!VulkanDetail::DebugValidateTextureSliceExtentsMatch(resolvedStaging, resolvedTexture, operationName, NWB_TEXT("source and destination extents do not match")))
        return false;
#else
    static_cast<void>(operationName);
    static_cast<void>(singleSampleRequirement);
    const TextureSlice resolvedStaging = stagingSlice.resolve(stagingDesc);
    const TextureSlice resolvedTexture = textureSlice.resolve(textureDesc);
#endif

    outRegion = VulkanTextureDetail::BuildStagingTextureCopyRegion(
        resolvedStaging,
        resolvedTexture,
        stagingResource.m_aspectMask,
        stagingResource.m_mipLayouts[resolvedStaging.mipLevel],
        stagingResource.m_formatLayout,
        stagingResource.m_arrayByteSize
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

