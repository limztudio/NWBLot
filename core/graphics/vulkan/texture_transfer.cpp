// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::copyTexture(Texture* destResource, const TextureSlice& destSlice, Texture* srcResource, const TextureSlice& srcSlice){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("copy texture"), NWB_TEXT("resource is invalid"), destResource, srcResource))
        return;

    Texture& dest = *destResource;
    Texture& src = *srcResource;
#if defined(NWB_DEBUG)
    if(dest.m_desc.sampleCount != src.m_desc.sampleCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture: source and destination sample counts do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture: source and destination sample counts do not match"));
        return;
    }
#endif

    TextureSlice resolvedDst;
    TextureSlice resolvedSrc;
    if(!VulkanDetail::DebugResolveTextureSlice(dest.m_desc, destSlice, dest.m_formatLayout, NWB_TEXT("copy texture"), NWB_TEXT("destination slice is outside the texture"), resolvedDst))
        return;
    if(!VulkanDetail::DebugResolveTextureSlice(src.m_desc, srcSlice, src.m_formatLayout, NWB_TEXT("copy texture"), NWB_TEXT("source slice is outside the texture"), resolvedSrc))
        return;

    if(!VulkanDetail::DebugValidateTextureSliceExtentsMatch(resolvedDst, resolvedSrc, NWB_TEXT("copy texture"), NWB_TEXT("source and destination extents do not match")))
        return;
    constexpr VkImageAspectFlags s_DepthStencilAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    const bool sourceDepthStencil = (src.m_aspectMask & s_DepthStencilAspectMask) != 0u;
    const bool destinationDepthStencil = (dest.m_aspectMask & s_DepthStencilAspectMask) != 0u;
    const bool crossesColorDepthStencilAspects = sourceDepthStencil != destinationDepthStencil;
    const bool multisampledDepthStencilCopy =
        (sourceDepthStencil || destinationDepthStencil)
        && (src.m_desc.sampleCount != 1u || dest.m_desc.sampleCount != 1u)
    ;
    const GpuQueueCapability::Mask requiredCapabilities = crossesColorDepthStencilAspects || multisampledDepthStencilCopy
        ? GpuQueueCapability::Graphics
        : GpuQueueCapability::Transfer
    ;
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("copy texture")))
        return;

    VkImageCopy region{};
    region.srcSubresource = VulkanDetail::BuildImageSubresourceLayers(src.m_aspectMask, resolvedSrc.mipLevel, resolvedSrc.arraySlice);
    region.srcOffset = { static_cast<int32_t>(resolvedSrc.x), static_cast<int32_t>(resolvedSrc.y), static_cast<int32_t>(resolvedSrc.z) };
    region.dstSubresource = VulkanDetail::BuildImageSubresourceLayers(dest.m_aspectMask, resolvedDst.mipLevel, resolvedDst.arraySlice);
    region.dstOffset = { static_cast<int32_t>(resolvedDst.x), static_cast<int32_t>(resolvedDst.y), static_cast<int32_t>(resolvedDst.z) };
    region.extent = { resolvedDst.width, resolvedDst.height, resolvedDst.depth };

    setTextureState(srcResource, TextureSubresourceSet(resolvedSrc.mipLevel, 1u, resolvedSrc.arraySlice, 1u), ResourceStates::CopySource);
    setTextureState(destResource, TextureSubresourceSet(resolvedDst.mipLevel, 1u, resolvedDst.arraySlice, 1u), ResourceStates::CopyDest);

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

    setTextureState(src, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopySource);

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

    setTextureState(dest, TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u), ResourceStates::CopyDest);

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

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    const usize uploadSize = static_cast<usize>(copyLayout.requiredSize);
    if(!prepareUploadStaging(data, uploadSize, NWB_TEXT("writeTexture"), stagingBuffer, stagingOffset))
        return false;

    setTextureState(destResource, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), ResourceStates::CopyDest);

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
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("resolve texture"), NWB_TEXT("resource is invalid"), destResource, srcResource))
        return;

    Texture& dest = *destResource;
    Texture& src = *srcResource;
#if defined(NWB_DEBUG)
    if(src.m_desc.sampleCount <= 1 || dest.m_desc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source must be multisampled and destination must be single-sampled"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: invalid sample counts"));
        return;
    }
    if(src.m_imageInfo.format != dest.m_imageInfo.format){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination formats do not match"));
        return;
    }
    if((src.m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: depth/stencil resolves are not supported by this path"));
        return;
    }
#endif

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(src.m_desc, TextureSubresourceMipResolve::Range);
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(dest.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSrc, NWB_TEXT("resolve texture")))
        return;
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedDst, NWB_TEXT("resolve texture")))
        return;
#if defined(NWB_DEBUG)
    if(resolvedSrc.numMipLevels != resolvedDst.numMipLevels || resolvedSrc.numArraySlices != resolvedDst.numArraySlices){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination subresources do not match"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination subresources do not match"));
        return;
    }
#endif

    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("resolve texture")))
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureResolveArena);
    Vector<VkImageResolve, Alloc::ScratchArena> regions(resolvedSrc.numMipLevels, scratchArena);

    for(MipLevel mipOffset = 0; mipOffset < resolvedSrc.numMipLevels; ++mipOffset){
        const MipLevel srcMipLevel = resolvedSrc.baseMipLevel + mipOffset;
        const MipLevel dstMipLevel = resolvedDst.baseMipLevel + mipOffset;
        const VkExtent3D srcExtent = VulkanDetail::GetTextureMipExtent(src.m_desc, srcMipLevel);
#if defined(NWB_DEBUG)
        const VkExtent3D dstExtent = VulkanDetail::GetTextureMipExtent(dest.m_desc, dstMipLevel);
        if(srcExtent.width != dstExtent.width || srcExtent.height != dstExtent.height || srcExtent.depth != dstExtent.depth){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to resolve texture: source and destination mip extents do not match"));
            return;
        }
#endif

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

    if(!regions.empty()){
        vkCmdResolveImage(
            m_currentCmdBuf->m_cmdBuf,
            src.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dest.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<u32>(regions.size()),
            regions.data()
        );
    }

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

