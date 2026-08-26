// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_clear_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::clearDepthStencilTexture(Texture* textureResource, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil){
    if(!clearDepth && !clearStencil)
        return;
    constexpr const tchar* s_OperationName = NWB_TEXT("clear depth/stencil texture");
    if(!textureResource){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture is null"));
        return;
    }
    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || texture.m_image == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture must be a live resource owned by this device"));
        return;
    }
    if(!VulkanTextureDetail::TextureDepthStencilClearAspectsAreValid(texture.m_aspectMask, clearDepth, clearStencil)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("requested aspect is not present in the texture format"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("subresource range is empty or invalid"));
        return;
    }

    const f32 clearDepthValue = clearDepth
        ? VulkanTextureDetail::ClampClearFloat(depth, 0.0f, 1.0f)
        : 0.0f
    ;

    VkClearDepthStencilValue clearValue{};
    clearValue.depth = clearDepthValue;
    clearValue.stencil = stencil;

    if(m_renderPassActive){
        const Rect fullRect(0, Limit<i32>::s_Max, 0, Limit<i32>::s_Max);
        if(clearActiveRenderPassDepthStencilTextureRect(
            texture,
            resolvedSubresources,
            fullRect,
            clearDepth,
            clearDepthValue,
            clearStencil,
            stencil
        ))
            retainResource(textureResource);
        return;
    }
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;

    VkImageAspectFlags aspectMask = 0;
    if(clearDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;
    if(texture.m_desc.dimension != TextureDimension::Texture3D && resolvedSubresources.numArraySlices > 1u){
        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
        Vector<VkImageSubresourceRange, Alloc::ScratchArena> ranges(resolvedSubresources.numArraySlices, scratchArena);
        VulkanTextureDetail::BuildArrayLayerImageSubresourceRanges(resolvedSubresources, aspectMask, ranges);
        vkCmdClearDepthStencilImage(m_currentCmdBuf->m_cmdBuf, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, static_cast<u32>(ranges.size()), ranges.data());
    }
    else{
        const VkImageSubresourceRange range = VulkanDetail::BuildImageSubresourceRange(resolvedSubresources, aspectMask);
        vkCmdClearDepthStencilImage(m_currentCmdBuf->m_cmdBuf, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1u, &range);
    }
    retainResource(textureResource);
}

bool CommandList::clearActiveRenderPassColorTextureRect(
    Texture& texture,
    const TextureSubresourceSet& resolvedSubresources,
    const Rect& rect,
    const VkClearColorValue& clearValue,
    const tchar* valueName
){
    static_cast<void>(valueName);
    constexpr const tchar* s_OperationName = NWB_TEXT("clear color attachment");
    if(!m_renderPassActive || !m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("active rendering with a color attachment is required"));
        return false;
    }

    VulkanTextureDetail::TextureAttachmentClearTarget clearTarget;
    if(!VulkanTextureDetail::FindTextureColorAttachmentClearTarget(
        texture,
        resolvedSubresources,
        m_renderPassFramebuffer->getDescription(),
        clearTarget
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("requested subresources are not active color attachments"));
        return false;
    }

    const Rect resolvedRect = VulkanTextureDetail::ResolveTextureClearRect(
        texture.m_desc,
        resolvedSubresources.baseMipLevel,
        rect
    );
    if(VulkanTextureDetail::TextureClearRectEmpty(resolvedRect))
        return true;
    if(clearTarget.isReadOnly){
        rejectCommandRecording(s_OperationName, NWB_TEXT("active color attachment is read-only"));
        return false;
    }

    VkClearAttachment clearAttachment{};
    clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAttachment.colorAttachment = clearTarget.colorAttachmentIndex;
    clearAttachment.clearValue.color = clearValue;

    const VkClearRect clearRect = VulkanTextureDetail::BuildTextureAttachmentClearRect(
        resolvedSubresources,
        clearTarget.resolvedSubresources,
        resolvedRect
    );
    if(!VulkanTextureDetail::TextureAttachmentClearRectContainedByFramebuffer(
        clearRect,
        m_renderPassFramebuffer->getFramebufferInfo()
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("clear rect is outside the active render area"));
        return false;
    }
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return false;

    vkCmdClearAttachments(m_currentCmdBuf->m_cmdBuf, 1u, &clearAttachment, 1u, &clearRect);
    return true;
}

bool CommandList::clearActiveRenderPassDepthStencilTextureRect(
    Texture& texture,
    const TextureSubresourceSet& resolvedSubresources,
    const Rect& rect,
    const bool clearDepth,
    const f32 depth,
    const bool clearStencil,
    const u8 stencil
){
    constexpr const tchar* s_OperationName = NWB_TEXT("clear depth/stencil attachment");
    if(!m_renderPassActive || !m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("active rendering with a depth/stencil attachment is required"));
        return false;
    }

    const FramebufferDesc& fbDesc = m_renderPassFramebuffer->getDescription();
    const FramebufferAttachment& attachment = fbDesc.depthAttachment;
    if(attachment.texture != &texture){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture is not the active depth/stencil attachment"));
        return false;
    }
    if(attachment.isReadOnly){
        rejectCommandRecording(s_OperationName, NWB_TEXT("active depth/stencil attachment is read-only"));
        return false;
    }

    TextureSubresourceSet resolvedAttachmentSubresources;
    if(!VulkanTextureDetail::ResolveTextureAttachmentClearSubresources(texture, attachment, resolvedSubresources, resolvedAttachmentSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("requested subresources are not active depth/stencil attachments"));
        return false;
    }

    const Rect resolvedRect = VulkanTextureDetail::ResolveTextureClearRect(texture.m_desc, resolvedSubresources.baseMipLevel, rect);
    if(VulkanTextureDetail::TextureClearRectEmpty(resolvedRect))
        return true;

    VkClearAttachment clearAttachment{};
    if(clearDepth)
        clearAttachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        clearAttachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    clearAttachment.clearValue.depthStencil.depth = depth;
    clearAttachment.clearValue.depthStencil.stencil = stencil;

    const VkClearRect clearRect = VulkanTextureDetail::BuildTextureAttachmentClearRect(resolvedSubresources, resolvedAttachmentSubresources, resolvedRect);
    if(!VulkanTextureDetail::TextureAttachmentClearRectContainedByFramebuffer(clearRect, m_renderPassFramebuffer->getFramebufferInfo())){
        rejectCommandRecording(s_OperationName, NWB_TEXT("clear rect is outside the active render area"));
        return false;
    }
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return false;

    vkCmdClearAttachments(m_currentCmdBuf->m_cmdBuf, 1u, &clearAttachment, 1u, &clearRect);
    return true;
}

void CommandList::clearDepthStencilTextureRect(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    const bool clearDepth,
    const f32 depth,
    const bool clearStencil,
    const u8 stencil
){
    clearDepthStencilTextureBox(textureResource, subresources, Box(rect, 0, Limit<i32>::s_Max), clearDepth, depth, clearStencil, stencil);
}

void CommandList::clearDepthStencilTextureBox(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    const bool clearDepth,
    const f32 depth,
    const bool clearStencil,
    const u8 stencil
){
    if(!clearDepth && !clearStencil)
        return;
    if(VulkanTextureDetail::TextureClearBoxEmpty(box))
        return;
    constexpr const tchar* s_OperationName = NWB_TEXT("clear depth/stencil texture box");
    if(!textureResource){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture is null"));
        return;
    }
    Texture& texture = *textureResource;
    if(&texture.m_context != &m_context || texture.m_image == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture must be a live resource owned by this device"));
        return;
    }
    const TextureDesc& desc = texture.m_desc;
    if(!VulkanTextureDetail::TextureDepthStencilClearAspectsAreValid(texture.m_aspectMask, clearDepth, clearStencil)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("requested aspect is not present in the texture format"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("subresource range is empty or invalid"));
        return;
    }

    if(VulkanTextureDetail::TextureClearBoxCoversSubresources(desc, resolvedSubresources, box)){
        clearDepthStencilTexture(textureResource, resolvedSubresources, clearDepth, depth, clearStencil, stencil);
        return;
    }

    const Box baseResolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, resolvedSubresources.baseMipLevel, box);
    if(VulkanTextureDetail::TextureClearBoxEmpty(baseResolvedBox))
        return;
    const f32 clearDepthValue = clearDepth
        ? VulkanTextureDetail::ClampClearFloat(depth, 0.0f, 1.0f)
        : 0.0f
    ;
    if(!m_renderPassActive && desc.sampleCount != 1u){
        rejectCommandRecording(s_OperationName, NWB_TEXT("bounded multisampled clears require active rendering"));
        return;
    }
    if(m_renderPassActive){
        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, resolvedSubresources.baseMipLevel);
        if(baseResolvedBox.minZ != 0 || baseResolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("attachment bounded clears require full attachment depth"));
            return;
        }

        const Rect rect(baseResolvedBox.minX, baseResolvedBox.maxX, baseResolvedBox.minY, baseResolvedBox.maxY);
        if(clearActiveRenderPassDepthStencilTextureRect(
            texture,
            resolvedSubresources,
            rect,
            clearDepth,
            clearDepthValue,
            clearStencil,
            stencil
        ))
            retainResource(textureResource);
        return;
    }
    if(desc.dimension == TextureDimension::Texture3D){
        rejectCommandRecording(s_OperationName, NWB_TEXT("bounded clears do not support 3D depth/stencil textures"));
        return;
    }

    u8 depthPattern[VulkanTextureDetail::s_TextureClearDepthPatternBytes] = {};
    u32 depthPatternSize = 0u;
    if(
        clearDepth
        && !VulkanTextureDetail::BuildTextureDepthClearPattern(
            desc.format,
            clearDepthValue,
            depthPattern,
            depthPatternSize
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("depth clear pattern is unsupported for the texture format"));
        return;
    }

    u8 stencilPattern[VulkanTextureDetail::s_TextureClearStencilPatternBytes] = {};
    u32 stencilPatternSize = 0u;
    if(clearStencil && !VulkanTextureDetail::BuildTextureStencilClearPattern(desc.format, stencil, stencilPattern, stencilPatternSize)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("stencil clear pattern is unsupported for the texture format"));
        return;
    }

    struct MipClearPlan{
        Box resolvedBox;
        VulkanTextureDetail::TextureClearUploadLayout depthLayout;
        VulkanTextureDetail::TextureClearUploadLayout stencilLayout;
    };
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
    Vector<MipClearPlan, Alloc::ScratchArena> mipClearPlans(resolvedSubresources.numMipLevels, scratchArena);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const u64 arrayLayerCount = static_cast<u64>(resolvedSubresources.numArraySlices);
    u32 mipPlanIndex = 0u;
    for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel, ++mipPlanIndex){
        MipClearPlan& mipPlan = mipClearPlans[mipPlanIndex];
        mipPlan.resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, mipLevel, box);
        if(VulkanTextureDetail::TextureClearBoxEmpty(mipPlan.resolvedBox))
            continue;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
        if(mipPlan.resolvedBox.minZ != 0 || mipPlan.resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("bounded clears require full attachment depth"));
            return;
        }

        const u64 texelCount = static_cast<u64>(mipPlan.resolvedBox.width()) * mipPlan.resolvedBox.height();
        if(
            (clearDepth && !VulkanTextureDetail::BuildTextureClearUploadLayout(
                texelCount, depthPatternSize, arrayLayerCount, mipPlan.depthLayout
            ))
            || (clearStencil && !VulkanTextureDetail::BuildTextureClearUploadLayout(
                texelCount, stencilPatternSize, arrayLayerCount, mipPlan.stencilLayout
            ))
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("clear upload layout is not addressable"));
            return;
        }
    }

    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear depth/stencil texture box through staging")))
        return;

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;

    const auto copyAspect = [&](const VkImageAspectFlagBits aspect, const u8* clearPattern, const u32 clearPatternSize) -> bool {
        u32 planIndex = 0u;
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel, ++planIndex){
            const MipClearPlan& mipPlan = mipClearPlans[planIndex];
            if(VulkanTextureDetail::TextureClearBoxEmpty(mipPlan.resolvedBox))
                continue;

            const VulkanTextureDetail::TextureClearUploadLayout& uploadLayout = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                ? mipPlan.depthLayout
                : mipPlan.stencilLayout
            ;
            Buffer* stagingBuffer = nullptr;
            u64 stagingOffset = 0;
            void* stagingBytes = nullptr;
            if(!prepareUploadStaging(
                uploadLayout.clearByteCount,
                NWB_TEXT("clearDepthStencilTextureBox"),
                stagingBuffer,
                stagingOffset,
                stagingBytes,
                uploadLayout.stagingAlignment
            )){
                rejectCommandRecording(s_OperationName, NWB_TEXT("staging allocation failed"));
                return false;
            }
            if(
                !stagingBuffer
                || !stagingBytes
                || (stagingOffset % uploadLayout.stagingAlignment) != 0u
                || (stagingOffset % uploadLayout.copyOffsetAlignment) != 0u
                || stagingOffset > stagingBuffer->m_creationDesc.byteSize
                || static_cast<u64>(uploadLayout.clearByteCount)
                    > stagingBuffer->m_creationDesc.byteSize - stagingOffset
            ){
                rejectCommandRecording(s_OperationName, NWB_TEXT("staging allocation returned an invalid range"));
                return false;
            }
            VulkanTextureDetail::FillTextureClearBytes(
                stagingBytes,
                uploadLayout.clearByteCount,
                clearPattern,
                clearPatternSize
            );

            if(uploadLayout.mergeArrayLayerCopies){
                Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
                const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
                u32 regionIndex = 0u;
                for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                    VkBufferImageCopy region{};
                    region.bufferOffset = stagingOffset + static_cast<u64>(regionIndex) * uploadLayout.layerPitch;
                    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspect, mipLevel, arraySlice, 1u);
                    region.imageOffset = { mipPlan.resolvedBox.minX, mipPlan.resolvedBox.minY, 0 };
                    region.imageExtent = {
                        static_cast<u32>(mipPlan.resolvedBox.width()),
                        static_cast<u32>(mipPlan.resolvedBox.height()),
                        1u
                    };
                    regions[regionIndex++] = region;
                }

                if(!regions.empty())
                    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());
            }
            else{
                Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
                const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
                u32 regionIndex = 0u;
                for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                    VkBufferImageCopy region{};
                    region.bufferOffset = stagingOffset;
                    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspect, mipLevel, arraySlice, 1u);
                    region.imageOffset = { mipPlan.resolvedBox.minX, mipPlan.resolvedBox.minY, 0 };
                    region.imageExtent = {
                        static_cast<u32>(mipPlan.resolvedBox.width()),
                        static_cast<u32>(mipPlan.resolvedBox.height()),
                        1u
                    };
                    regions[regionIndex++] = region;
                }

                if(!regions.empty())
                    vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());
            }

            retainStagingBuffer(*stagingBuffer);
        }

        return true;
    };

    if(clearDepth && !copyAspect(VK_IMAGE_ASPECT_DEPTH_BIT, depthPattern, depthPatternSize))
        return;
    if(clearStencil && !copyAspect(VK_IMAGE_ASPECT_STENCIL_BIT, stencilPattern, stencilPatternSize))
        return;

    retainResource(textureResource);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

