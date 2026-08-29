// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_resource_detail.h"

#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_transfer_staging{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BuildStagingTextureCopyRegion(
    const TextureSlice& stagingSlice,
    const TextureSlice& imageSlice,
    const VkImageAspectFlags aspectMask,
    const VulkanDetail::StagingTextureMipLayout& stagingMipLayout,
    const VulkanDetail::TextureFormatBlockLayout& stagingFormatLayout,
    const u64 stagingArrayByteSize,
    const u64 stagingTotalByteSize,
    const u32 requiredOffsetAlignment,
    VkBufferImageCopy& outRegion
){
    VulkanDetail::StagingTextureRange range;
    if(!VulkanDetail::BuildStagingTextureRange(
        stagingSlice,
        stagingMipLayout,
        stagingFormatLayout,
        stagingArrayByteSize,
        stagingTotalByteSize,
        requiredOffsetAlignment,
        false,
        range
    ))
        return false;
    if(
        (range.byteOffset % s_BufferAlignmentBytes) != 0u
        || (range.byteOffset % stagingFormatLayout.bytesPerBlock) != 0u
    )
        return false;

    outRegion = {};
    outRegion.bufferOffset = range.byteOffset;
    outRegion.bufferRowLength = range.bufferRowLength;
    outRegion.bufferImageHeight = range.bufferImageHeight;
    outRegion.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(
        aspectMask,
        imageSlice.mipLevel,
        imageSlice.arraySlice
    );
    outRegion.imageOffset = {
        static_cast<i32>(imageSlice.x),
        static_cast<i32>(imageSlice.y),
        static_cast<i32>(imageSlice.z),
    };
    outRegion.imageExtent = { imageSlice.width, imageSlice.height, imageSlice.depth };
    return true;
}

[[nodiscard]] inline bool IsWholeImageSubresourceCopy(
    const TextureDesc& imageDesc,
    const VkBufferImageCopy& region
)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(imageDesc, region.imageSubresource.mipLevel);
    return region.imageOffset.x == 0
        && region.imageOffset.y == 0
        && region.imageOffset.z == 0
        && region.imageExtent.width == mipExtent.width
        && region.imageExtent.height == mipExtent.height
        && region.imageExtent.depth == mipExtent.depth
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::copyTexture(
    StagingTexture* dest,
    const TextureSlice& destSlice,
    Texture* src,
    const TextureSlice& srcSlice
){
    constexpr const tchar* s_OperationName = NWB_TEXT("copy texture to staging texture");
    if(!dest || !src){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination resources must be non-null"));
        return;
    }
    if(!validateStagingTextureCopyResources(
        *dest,
        *src,
        CpuAccessMode::Read,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        s_OperationName
    ))
        return;
    if(!validateTextureForGpuState(
        src,
        ResourceStates::CopySource,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    ))
        return;

    VkBufferImageCopy region{};
    if(!prepareStagingTextureCopy(
        *dest,
        destSlice,
        *src,
        srcSlice,
        region
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination slices violate the copy contract"));
        return;
    }
    const bool depthStencilCopy =
        (src->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u
    ;
    if(depthStencilCopy){
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
            return;
    }
    else if(!__hidden_texture_transfer_staging::IsWholeImageSubresourceCopy(src->m_creationDesc, region)){
        if(!recordAndValidateAnyCommandCapability(
            GpuQueueCapability::Compute | GpuQueueCapability::Graphics,
            s_OperationName
        ))
            return;
    }
    else if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, s_OperationName))
        return;

    endActiveRenderPass();
    setTextureState(
        src,
        TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u),
        ResourceStates::CopySource
    );
    if(m_commandRecordingFailed)
        return;

    registerHostReadbackStagingTexture(*dest);
    vkCmdCopyImageToBuffer(
        m_currentCmdBuf->m_cmdBuf,
        src->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dest->m_buffer,
        1,
        &region
    );

    retainResource(src);
    retainResource(dest);
}

void CommandList::copyTexture(
    Texture* dest,
    const TextureSlice& destSlice,
    StagingTexture* src,
    const TextureSlice& srcSlice
){
    constexpr const tchar* s_OperationName = NWB_TEXT("copy staging texture to texture");
    if(!dest || !src){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination resources must be non-null"));
        return;
    }
    if(!validateStagingTextureCopyResources(
        *src,
        *dest,
        CpuAccessMode::Write,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        s_OperationName
    ))
        return;
    if(!validateTextureForGpuState(
        dest,
        ResourceStates::CopyDest,
        s_OperationName,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ))
        return;

    VkBufferImageCopy region{};
    if(!prepareStagingTextureCopy(
        *src,
        srcSlice,
        *dest,
        destSlice,
        region
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("source and destination slices violate the copy contract"));
        return;
    }
    const bool depthStencilCopy =
        (dest->m_aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u
    ;
    if(depthStencilCopy){
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
            return;
    }
    else if(!__hidden_texture_transfer_staging::IsWholeImageSubresourceCopy(dest->m_creationDesc, region)){
        if(!recordAndValidateAnyCommandCapability(
            GpuQueueCapability::Compute | GpuQueueCapability::Graphics,
            s_OperationName
        ))
            return;
    }
    else if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, s_OperationName))
        return;

    endActiveRenderPass();
    setTextureState(
        dest,
        TextureSubresourceSet(region.imageSubresource.mipLevel, 1u, region.imageSubresource.baseArrayLayer, 1u),
        ResourceStates::CopyDest
    );
    if(m_commandRecordingFailed)
        return;

    vkCmdCopyBufferToImage(
        m_currentCmdBuf->m_cmdBuf,
        src->m_buffer,
        dest->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    retainResource(dest);
    retainResource(src);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging texture transfer preparation


bool CommandList::validateStagingTextureCopyResources(
    StagingTexture& stagingTexture,
    Texture& texture,
    const CpuAccessMode::Enum requiredCpuAccess,
    const VkImageUsageFlags requiredImageUsage,
    const tchar* const operationName
)noexcept{
    if(!validateCommandRecordingScope(operationName))
        return false;

    Queue* const expectedQueue = m_device.getQueue(m_creationDesc.physicalQueue);
    TrackedCommandBuffer* const activeCommandBuffer = m_currentCmdBuf.get();
    if(
        !m_isRecording
        || m_commandRecordingFailed
        || !activeCommandBuffer
        || activeCommandBuffer->m_cmdBuf == VK_NULL_HANDLE
        || activeCommandBuffer->m_arenaState != TrackedCommandBufferArenaState::Leased
        || &activeCommandBuffer->m_context != &m_context
        || !expectedQueue
        || &activeCommandBuffer->m_queue != expectedQueue
        || m_recordingLeaseSerial == 0u
    ){
        rejectCommandRecording(operationName, NWB_TEXT("command list has no exact active command-buffer lease"));
        return false;
    }
    if(&stagingTexture.m_context != &m_context || &texture.m_context != &m_context){
        rejectCommandRecording(operationName, NWB_TEXT("staging texture and texture must belong to this device"));
        return false;
    }
    if(
        stagingTexture.m_buffer == VK_NULL_HANDLE
        || !stagingTexture.m_allocation
        || texture.m_image == VK_NULL_HANDLE
    ){
        rejectCommandRecording(operationName, NWB_TEXT("staging buffer and image handles must be non-null"));
        return false;
    }
    if(stagingTexture.m_cpuAccess != requiredCpuAccess){
        rejectCommandRecording(operationName, NWB_TEXT("staging texture CPU-access direction is incompatible"));
        return false;
    }
    if((texture.m_imageInfo.usage & requiredImageUsage) != requiredImageUsage){
        rejectCommandRecording(operationName, NWB_TEXT("image lacks the required transfer usage"));
        return false;
    }

    u64 expectedTotalByteSize = 0u;
    if(
        stagingTexture.m_creationDesc.arraySize == 0u
        || stagingTexture.m_creationDesc.mipLevels == 0u
        || stagingTexture.m_mipLayouts.size() != stagingTexture.m_creationDesc.mipLevels
        || stagingTexture.m_creationQueueSharing != stagingTexture.m_creationDesc.queueSharing
        || !TryMultiply<u64>(
            stagingTexture.m_arrayByteSize,
            static_cast<u64>(stagingTexture.m_creationDesc.arraySize),
            expectedTotalByteSize
        )
        || expectedTotalByteSize != stagingTexture.m_totalByteSize
    ){
        rejectCommandRecording(operationName, NWB_TEXT("staging texture has invalid immutable layout provenance"));
        return false;
    }

    const GpuPhysicalQueueInfo* const exactQueue = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    if(!exactQueue || exactQueue->id != m_creationDesc.physicalQueue){
        rejectCommandRecording(operationName, NWB_TEXT("command list has no valid exact physical queue"));
        return false;
    }
    if(
        stagingTexture.m_admittedQueueFamilies.empty()
        || stagingTexture.m_admittedQueueFamilies.size() > Limit<u32>::s_Max
        || (
            stagingTexture.m_creationSharingMode != VK_SHARING_MODE_EXCLUSIVE
            && stagingTexture.m_creationSharingMode != VK_SHARING_MODE_CONCURRENT
        )
        || (
            stagingTexture.m_creationSharingMode == VK_SHARING_MODE_EXCLUSIVE
            && stagingTexture.m_admittedQueueFamilies.size() != 1u
        )
        || (
            stagingTexture.m_creationSharingMode == VK_SHARING_MODE_CONCURRENT
            && stagingTexture.m_admittedQueueFamilies.size() < 2u
        )
    ){
        rejectCommandRecording(operationName, NWB_TEXT("staging texture has invalid immutable sharing provenance"));
        return false;
    }

    bool familyAdmitted = false;
    for(const u32 familyIndex : stagingTexture.m_admittedQueueFamilies){
        if(familyIndex == exactQueue->familyIndex){
            familyAdmitted = true;
            break;
        }
    }
    if(!familyAdmitted){
        rejectCommandRecording(operationName, NWB_TEXT("exact command queue family was not admitted at creation"));
        return false;
    }
    if(
        stagingTexture.m_creationSharingMode == VK_SHARING_MODE_CONCURRENT
        && !ResourceQueueSharing::IncludesQueueClass(
            stagingTexture.m_creationQueueSharing,
            exactQueue->queueClass
        )
    ){
        rejectCommandRecording(operationName, NWB_TEXT("exact command queue class was not admitted at creation"));
        return false;
    }
    return true;
}

bool CommandList::prepareStagingTextureCopy(
    StagingTexture& stagingResource,
    const TextureSlice& stagingSlice,
    Texture& textureResource,
    const TextureSlice& textureSlice,
    VkBufferImageCopy& outRegion
)const{
    const TextureDesc& stagingDesc = stagingResource.m_creationDesc;
    const TextureDesc& textureDesc = textureResource.m_creationDesc;
    if(textureDesc.sampleCount != 1)
        return false;
    if(textureDesc.format != stagingDesc.format)
        return false;
    if(!VulkanTextureDetail::IsTextureImageInfoConsistent(textureDesc, textureResource.m_imageInfo))
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    VulkanDetail::TextureFormatBlockLayout expectedFormatLayout;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(
        !VulkanDetail::GetTextureFormatBlockLayout(formatInfo, expectedFormatLayout)
        || expectedAspectMask == 0u
        || stagingResource.m_aspectMask != expectedAspectMask
        || textureResource.m_aspectMask != expectedAspectMask
        || stagingResource.m_formatLayout.blockWidth != expectedFormatLayout.blockWidth
        || stagingResource.m_formatLayout.blockHeight != expectedFormatLayout.blockHeight
        || stagingResource.m_formatLayout.bytesPerBlock != expectedFormatLayout.bytesPerBlock
        || stagingResource.m_creationQueueSharing != stagingDesc.queueSharing
        || stagingResource.m_totalByteSize == 0u
        || stagingResource.m_arrayByteSize == 0u
        || stagingResource.m_bufferOffsetAlignment == 0u
        || textureResource.m_formatLayout.blockWidth != expectedFormatLayout.blockWidth
        || textureResource.m_formatLayout.blockHeight != expectedFormatLayout.blockHeight
        || textureResource.m_formatLayout.bytesPerBlock != expectedFormatLayout.bytesPerBlock
    )
        return false;
    if(!VulkanDetail::IsBufferImageCopyAspectMaskSupported(stagingResource.m_aspectMask))
        return false;

    TextureSlice resolvedStaging;
    TextureSlice resolvedTexture;
    if(!VulkanDetail::IsTextureSliceInBounds(
        stagingDesc,
        stagingSlice,
        stagingResource.m_formatLayout,
        &resolvedStaging
    ))
        return false;
    if(!VulkanDetail::IsTextureSliceInBounds(
        textureDesc,
        textureSlice,
        textureResource.m_formatLayout,
        &resolvedTexture
    ))
        return false;

    if(
        resolvedStaging.width != resolvedTexture.width
        || resolvedStaging.height != resolvedTexture.height
        || resolvedStaging.depth != resolvedTexture.depth
    )
        return false;

    if(resolvedStaging.mipLevel >= stagingResource.m_mipLayouts.size())
        return false;
    if(!__hidden_texture_transfer_staging::BuildStagingTextureCopyRegion(
        resolvedStaging,
        resolvedTexture,
        stagingResource.m_aspectMask,
        stagingResource.m_mipLayouts[resolvedStaging.mipLevel],
        stagingResource.m_formatLayout,
        stagingResource.m_arrayByteSize,
        stagingResource.m_totalByteSize,
        stagingResource.m_bufferOffsetAlignment,
        outRegion
    ))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

