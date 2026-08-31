// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "texture_copy_contract.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_transfer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

// Buffer-image copies address exactly one image aspect. Keep image provenance validation against the texture's
// complete native metadata, but lower the selected public upload plane into its own Vulkan aspect and byte layout.
[[nodiscard]] inline bool ResolveTextureUploadCopyAspect(
    const TextureDesc& textureDesc,
    const VkImageAspectFlags textureAspectMask,
    const TextureUploadAspect::Enum requestedAspect,
    VkImageAspectFlags& outAspectMask,
    VulkanDetail::TextureFormatBlockLayout& outFormatLayout
){
    outAspectMask = 0u;
    outFormatLayout = {};

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    TextureUploadAspect::Enum resolvedAspect;
    TextureUploadAspectLayout uploadLayout;
    if(
        !ResolveTextureUploadAspect(formatInfo, requestedAspect, resolvedAspect)
        || !GetTextureUploadAspectLayout(formatInfo, requestedAspect, uploadLayout)
    )
        return false;

    switch(resolvedAspect){
    case TextureUploadAspect::Color:
        outAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        break;
    case TextureUploadAspect::Depth:
        outAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    case TextureUploadAspect::Stencil:
        outAspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        break;
    default:
        return false;
    }
    if((textureAspectMask & outAspectMask) != outAspectMask)
        return false;

    outFormatLayout.blockWidth = uploadLayout.blockWidth;
    outFormatLayout.blockHeight = uploadLayout.blockHeight;
    outFormatLayout.bytesPerBlock = uploadLayout.bytesPerBlock;
    return outFormatLayout.blockWidth != 0u
        && outFormatLayout.blockHeight != 0u
        && outFormatLayout.bytesPerBlock != 0u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(!validateTextureForGpuState(
        srcResource,
        ResourceStates::CopySource,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    ))
        return;
    if(!validateTextureForGpuState(
        destResource,
        ResourceStates::CopyDest,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ))
        return;

    VulkanTextureDetail::TextureCopyContract contract;
    if(!VulkanTextureDetail::ResolveTextureCopyContract(
        src.m_creationDesc,
        srcSlice,
        dest.m_creationDesc,
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
        !VulkanTextureDetail::IsTextureImageInfoConsistent(src.m_creationDesc, src.m_imageInfo)
        || !VulkanTextureDetail::IsTextureImageInfoConsistent(dest.m_creationDesc, dest.m_imageInfo)
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

bool CommandList::tryWriteTexture(
    Texture* destResource,
    u32 arraySlice,
    u32 mipLevel,
    const void* data,
    usize rowPitch,
    usize depthPitch,
    TextureUploadAspect::Enum aspect
){
    constexpr const tchar* s_OperationName = NWB_TEXT("write texture");
    if(!destResource){
        rejectCommandRecording(s_OperationName, NWB_TEXT("destination texture is null"));
        return false;
    }
    if(!data){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source data is null"));
        return false;
    }

    Texture& dest = *destResource;
    const TextureDesc& texDesc = dest.m_creationDesc;
    if(texDesc.sampleCount != 1){
        rejectCommandRecording(s_OperationName, NWB_TEXT("destination texture must be single-sampled"));
        return false;
    }

    if(mipLevel >= texDesc.mipLevels || arraySlice >= texDesc.arraySize){
        rejectCommandRecording(s_OperationName, NWB_TEXT("destination subresource is out of bounds"));
        return false;
    }
    if(!validateTextureForGpuState(
        destResource,
        ResourceStates::CopyDest,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ))
        return false;

    if(!VulkanTextureDetail::IsTextureImageInfoConsistent(texDesc, dest.m_imageInfo)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture description and native image metadata must agree"));
        return false;
    }

    const FormatInfo& formatInfo = GetFormatInfo(texDesc.format);
    VulkanDetail::TextureFormatBlockLayout expectedFormatLayout;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(
        !VulkanDetail::GetTextureFormatBlockLayout(formatInfo, expectedFormatLayout)
        || expectedAspectMask == 0u
        || dest.m_aspectMask != expectedAspectMask
        || dest.m_formatLayout.blockWidth != expectedFormatLayout.blockWidth
        || dest.m_formatLayout.blockHeight != expectedFormatLayout.blockHeight
        || dest.m_formatLayout.bytesPerBlock != expectedFormatLayout.bytesPerBlock
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture description and native format metadata must agree"));
        return false;
    }
    if((dest.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u){
        rejectCommandRecording(s_OperationName, NWB_TEXT("destination image requires transfer-destination usage"));
        return false;
    }

    VkImageAspectFlags copyAspectMask = 0u;
    VulkanDetail::TextureFormatBlockLayout copyFormatLayout;
    if(!__hidden_texture_transfer::ResolveTextureUploadCopyAspect(
        texDesc,
        dest.m_aspectMask,
        aspect,
        copyAspectMask,
        copyFormatLayout
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("upload aspect is not present in the destination format"));
        return false;
    }

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(texDesc, mipLevel);

    if(!VulkanDetail::IsBufferImageCopyAspectMaskSupported(copyAspectMask)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("destination aspect mask is invalid for a buffer-image copy"));
        return false;
    }

    VulkanDetail::BufferImageCopyLayout copyLayout;
    if(
        !VulkanDetail::BuildBufferImageCopyLayout(
            mipExtent,
            copyFormatLayout,
            static_cast<u64>(rowPitch),
            static_cast<u64>(depthPitch),
            VulkanDetail::BufferImageCopyRequiredSize::PaddedSlices,
            VulkanDetail::BufferImageCopyPitchFields::EmitExplicit,
            copyLayout
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source pitches do not define a valid buffer-image copy layout"));
        return false;
    }
    if(copyLayout.requiredSize > static_cast<u64>(Limit<usize>::s_Max)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("upload size exceeds addressable memory"));
        return false;
    }
    u32 uploadAlignment = 0u;
    const u32 uploadAlignmentRequirement = copyAspectMask == VK_IMAGE_ASPECT_COLOR_BIT
        ? copyFormatLayout.bytesPerBlock
        : static_cast<u32>(sizeof(u32))
    ;
    if(!VulkanDetail::TryComputeUploadSuballocationAlignment(uploadAlignmentRequirement, uploadAlignment)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("upload buffer offset alignment overflows"));
        return false;
    }
    const bool depthStencilCopy = copyAspectMask != VK_IMAGE_ASPECT_COLOR_BIT;
    const GpuQueueCapability::Mask requiredCapabilities = depthStencilCopy
        ? GpuQueueCapability::Graphics
        : GpuQueueCapability::Transfer
    ;
    if(!recordAndValidateCommandCapability(requiredCapabilities, s_OperationName))
        return false;

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    const usize uploadSize = static_cast<usize>(copyLayout.requiredSize);
    if(!prepareUploadStaging(
        data,
        uploadSize,
        NWB_TEXT("writeTexture"),
        stagingBuffer,
        stagingOffset,
        uploadAlignment
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("staging allocation failed"));
        return false;
    }
    if(
        !stagingBuffer
        || (stagingOffset % uploadAlignment) != 0u
        || (stagingOffset % uploadAlignmentRequirement) != 0u
        || stagingOffset > stagingBuffer->m_creationDesc.byteSize
        || static_cast<u64>(uploadSize) > stagingBuffer->m_creationDesc.byteSize - stagingOffset
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("staging allocation returned an invalid offset or range"));
        return false;
    }

    endActiveRenderPass();
    setTextureState(destResource, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return false;

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.bufferRowLength = copyLayout.bufferRowLength;
    region.bufferImageHeight = copyLayout.bufferImageHeight;
    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(copyAspectMask, mipLevel, arraySlice);
    region.imageExtent = mipExtent;

    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, dest.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    retainResource(destResource);
    retainStagingBuffer(*stagingBuffer);
    return true;
}

void CommandList::writeTexture(
    Texture* destResource,
    u32 arraySlice,
    u32 mipLevel,
    const void* data,
    usize rowPitch,
    usize depthPitch,
    TextureUploadAspect::Enum aspect
){
    if(!tryWriteTexture(destResource, arraySlice, mipLevel, data, rowPitch, depthPitch, aspect))
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
    if(!validateTextureForGpuState(
        srcResource,
        ResourceStates::ResolveSource,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    ))
        return;
    if(!validateTextureForGpuState(
        destResource,
        ResourceStates::ResolveDest,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ))
        return;
    if(
        !VulkanTextureDetail::IsTextureDescShapeValid(src.m_creationDesc)
        || !VulkanTextureDetail::IsTextureDescShapeValid(dest.m_creationDesc)
        || !VulkanTextureDetail::IsTextureImageInfoConsistent(src.m_creationDesc, src.m_imageInfo)
        || !VulkanTextureDetail::IsTextureImageInfoConsistent(dest.m_creationDesc, dest.m_imageInfo)
        || src.m_imageInfo.imageType != dest.m_imageInfo.imageType
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture descriptions and native image metadata must agree"));
        return;
    }
    if(
        src.m_creationDesc.sampleCount <= 1u
        || dest.m_creationDesc.sampleCount != 1u
        || !VulkanDetail::IsSupportedSampleCount(src.m_creationDesc.sampleCount)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source must be multisampled and destination must be single-sampled"));
        return;
    }
    if(
        src.m_creationDesc.format != dest.m_creationDesc.format
        || src.m_imageInfo.format == VK_FORMAT_UNDEFINED
        || src.m_imageInfo.format != dest.m_imageInfo.format
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination native formats must match exactly"));
        return;
    }

    const FormatInfo& formatInfo = GetFormatInfo(src.m_creationDesc.format);
    VulkanDetail::TextureFormatBlockLayout expectedFormatLayout;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(
        !VulkanDetail::GetTextureFormatBlockLayout(formatInfo, expectedFormatLayout)
        || expectedAspectMask != VK_IMAGE_ASPECT_COLOR_BIT
        || src.m_aspectMask != expectedAspectMask
        || dest.m_aspectMask != expectedAspectMask
        || src.m_formatLayout.blockWidth != expectedFormatLayout.blockWidth
        || src.m_formatLayout.blockHeight != expectedFormatLayout.blockHeight
        || src.m_formatLayout.bytesPerBlock != expectedFormatLayout.bytesPerBlock
        || dest.m_formatLayout.blockWidth != expectedFormatLayout.blockWidth
        || dest.m_formatLayout.blockHeight != expectedFormatLayout.blockHeight
        || dest.m_formatLayout.bytesPerBlock != expectedFormatLayout.bytesPerBlock
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture aspect or block metadata disagrees with its format"));
        return;
    }
    if(
        (dest.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u
        || (src.m_imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination images require transfer usage"));
        return;
    }

    const TextureSubresourceSet resolvedSrc = srcSubresources.resolve(
        src.m_creationDesc,
        TextureSubresourceMipResolve::Range
    );
    const TextureSubresourceSet resolvedDst = dstSubresources.resolve(
        dest.m_creationDesc,
        TextureSubresourceMipResolve::Range
    );
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
        resolvedSrc.numMipLevels != resolvedDst.numMipLevels
        || resolvedSrc.numArraySlices != resolvedDst.numArraySlices
        || !__hidden_texture_transfer::ResolveMipExtentsMatch(
            dest.m_creationDesc,
            resolvedDst,
            src.m_creationDesc,
            resolvedSrc
        )
    ){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("source and destination subresource counts and mip extents must match")
        );
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
        const VkExtent3D srcExtent = VulkanDetail::GetTextureMipExtent(src.m_creationDesc, srcMipLevel);

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


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

