// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "texture_clear_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::clearTextureFloat(Texture* textureResource, TextureSubresourceSet subresources, const Color& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTexture(textureResource, subresources, NWB_TEXT("color value"), clearValue, false, false);
}

void CommandList::clearTextureRectFloat(Texture* textureResource, TextureSubresourceSet subresources, const Rect& rect, const Color& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, Box(rect, 0, Limit<i32>::s_Max), NWB_TEXT("color value"), clearValue, false, false);
}

void CommandList::clearTextureBoxFloat(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, const Color& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, box, NWB_TEXT("color value"), clearValue, false, false);
}

void CommandList::clearDepthStencilTexture(Texture* textureResource, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil){
    if(!clearDepth && !clearStencil)
        return;
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("clear depth/stencil texture"), NWB_TEXT("texture is null"), textureResource))
        return;

    Texture& texture = *textureResource;
    if(!VulkanTextureDetail::ValidateTextureDepthStencilClearAspects(
        texture.m_aspectMask,
        clearDepth,
        clearStencil,
        NWB_TEXT("clear depth/stencil texture")
    ))
        return;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("clear depth/stencil texture")))
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear depth/stencil texture")))
        return;

    VkClearDepthStencilValue clearValue{};
    clearValue.depth = depth;
    clearValue.stencil = stencil;

    if(m_renderPassActive){
        const Rect fullRect(0, Limit<i32>::s_Max, 0, Limit<i32>::s_Max);
        if(clearActiveRenderPassDepthStencilTextureRect(texture, resolvedSubresources, fullRect, clearDepth, depth, clearStencil, stencil))
            retainResource(textureResource);
        return;
    }

    VkImageAspectFlags aspectMask = 0;
    if(clearDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(clearStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
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
#if !defined(NWB_DEBUG)
    static_cast<void>(valueName);
#endif
    if(!m_renderPassActive || !m_renderPassFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture rect with {}: active-render-pass bounded rect clears require the texture to be an active color attachment"), valueName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture rect with {}: active-render-pass bounded rect clears require the texture to be an active color attachment"), valueName);
        return false;
    }

    VulkanTextureDetail::TextureAttachmentClearTarget clearTarget;
    if(VulkanTextureDetail::FindTextureColorAttachmentClearTarget(texture, resolvedSubresources, m_renderPassFramebuffer->getDescription(), clearTarget)){
        const Rect resolvedRect = VulkanTextureDetail::ResolveTextureClearRect(texture.m_desc, resolvedSubresources.baseMipLevel, rect);
        if(VulkanTextureDetail::TextureClearRectEmpty(resolvedRect))
            return true;

        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAttachment.colorAttachment = clearTarget.colorAttachmentIndex;
        clearAttachment.clearValue.color = clearValue;

        const VkClearRect clearRect = VulkanTextureDetail::BuildTextureAttachmentClearRect(resolvedSubresources, clearTarget.resolvedSubresources, resolvedRect);
        if(!VulkanTextureDetail::TextureAttachmentClearRectContainedByFramebuffer(clearRect, m_renderPassFramebuffer->getFramebufferInfo())){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture rect with {}: clear rect is outside the active render area"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture rect with {}: clear rect is outside the active render area"), valueName);
            return false;
        }

        vkCmdClearAttachments(m_currentCmdBuf->m_cmdBuf, 1u, &clearAttachment, 1u, &clearRect);
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture rect with {}: active-render-pass bounded rect clears require the requested subresources to be active color attachments"), valueName);
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture rect with {}: active-render-pass bounded rect clears require the requested subresources to be active color attachments"), valueName);
    return false;
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
    if(!m_renderPassActive || !m_renderPassFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the texture to be an active depth/stencil attachment"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the texture to be an active depth/stencil attachment"));
        return false;
    }

    const FramebufferDesc& fbDesc = m_renderPassFramebuffer->getDescription();
    const FramebufferAttachment& attachment = fbDesc.depthAttachment;
    if(attachment.texture != &texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the requested subresources to be active depth/stencil attachments"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the requested subresources to be active depth/stencil attachments"));
        return false;
    }
    if(attachment.isReadOnly){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active depth/stencil attachment is read-only"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active depth/stencil attachment is read-only"));
        return false;
    }

    TextureSubresourceSet resolvedAttachmentSubresources;
    if(!VulkanTextureDetail::ResolveTextureAttachmentClearSubresources(texture, attachment, resolvedSubresources, resolvedAttachmentSubresources)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the requested subresources to be active depth/stencil attachments"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: active-render-pass bounded rect clears require the requested subresources to be active depth/stencil attachments"));
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: clear rect is outside the active render area"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture rect: clear rect is outside the active render area"));
        return false;
    }

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
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("clear depth/stencil texture box"), NWB_TEXT("texture is null"), textureResource))
        return;
    Texture& texture = *textureResource;
    const TextureDesc& desc = texture.m_desc;
    if(!VulkanTextureDetail::ValidateTextureDepthStencilClearAspects(
        texture.m_aspectMask,
        clearDepth,
        clearStencil,
        NWB_TEXT("clear depth/stencil texture box")
    ))
        return;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("clear depth/stencil texture box")))
        return;

    if(VulkanTextureDetail::TextureClearBoxCoversSubresources(desc, resolvedSubresources, box)){
        clearDepthStencilTexture(textureResource, resolvedSubresources, clearDepth, depth, clearStencil, stencil);
        return;
    }

    if(m_renderPassActive || desc.sampleCount != 1u){
        const Box resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, resolvedSubresources.baseMipLevel, box);
        if(VulkanTextureDetail::TextureClearBoxEmpty(resolvedBox))
            return;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, resolvedSubresources.baseMipLevel);
        if(resolvedBox.minZ != 0 || resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: attachment bounded clears require the box to cover the full attachment depth"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: attachment bounded clears require the box to cover the full attachment depth"));
            return;
        }
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear depth/stencil texture box as attachment")))
            return;

        const Rect rect(resolvedBox.minX, resolvedBox.maxX, resolvedBox.minY, resolvedBox.maxY);
        if(clearActiveRenderPassDepthStencilTextureRect(texture, resolvedSubresources, rect, clearDepth, depth, clearStencil, stencil))
            retainResource(textureResource);
        return;
    }
#if defined(NWB_DEBUG)
    if(desc.dimension == TextureDimension::Texture3D){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded texture box clears do not support 3D depth/stencil textures"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded texture box clears do not support 3D depth/stencil textures"));
        return;
    }
#endif
    if(desc.dimension == TextureDimension::Texture3D)
        return;

    u8 depthPattern[VulkanTextureDetail::s_TextureClearDepthPatternBytes] = {};
    u32 depthPatternSize = 0u;
    if(clearDepth && !VulkanTextureDetail::BuildTextureDepthClearPattern(desc.format, depth, depthPattern, depthPatternSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded depth box clears do not support texture format {}"), StringConvert(GetFormatInfo(desc.format).name));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded depth box clears do not support texture format"));
        return;
    }

    u8 stencilPattern[VulkanTextureDetail::s_TextureClearStencilPatternBytes] = {};
    u32 stencilPatternSize = 0u;
    if(clearStencil && !VulkanTextureDetail::BuildTextureStencilClearPattern(desc.format, stencil, stencilPattern, stencilPatternSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded stencil box clears do not support texture format {}"), StringConvert(GetFormatInfo(desc.format).name));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded stencil box clears do not support texture format"));
        return;
    }
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear depth/stencil texture box through staging")))
        return;

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const auto copyAspect = [&](const VkImageAspectFlagBits aspect, const u8* clearPattern, const u32 clearPatternSize) -> bool {
        const u64 arrayLayerCount = static_cast<u64>(resolvedSubresources.numArraySlices);
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const Box resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, mipLevel, box);
            if(VulkanTextureDetail::TextureClearBoxEmpty(resolvedBox))
                continue;

            const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
            if(resolvedBox.minZ != 0 || resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded depth/stencil clears require the box to cover the full attachment depth"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: bounded depth/stencil clears require the box to cover the full attachment depth"));
                return false;
            }

            const u64 clearWidth = static_cast<u64>(resolvedBox.width());
            const u64 clearHeight = static_cast<u64>(resolvedBox.height());
            const u64 texelCount = clearWidth * clearHeight;
            if(texelCount > Limit<u64>::s_Max / clearPatternSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                return false;
            }

            const u64 uploadSize64 = texelCount * clearPatternSize;
            u64 layerPitch64 = uploadSize64;
            if(!AlignUpU64Checked(layerPitch64, VulkanTextureDetail::s_TextureClearUploadAlignment, layerPitch64)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                return false;
            }

            const bool mergeArrayLayerCopies =
                arrayLayerCount > 1ull
                && layerPitch64 <= (VulkanTextureDetail::s_TextureClearMergedLayerUploadThreshold / arrayLayerCount)
            ;
            if(mergeArrayLayerCopies && arrayLayerCount - 1ull > (Limit<u64>::s_Max - uploadSize64) / layerPitch64){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size overflows"));
                return false;
            }

            const u64 clearByteSize64 = mergeArrayLayerCopies ? layerPitch64 * (arrayLayerCount - 1ull) + uploadSize64 : uploadSize64;
            if(clearByteSize64 > static_cast<u64>(Limit<usize>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size exceeds addressable memory"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear depth/stencil texture box: clear byte size exceeds addressable memory"));
                return false;
            }

            const usize clearByteCount = static_cast<usize>(clearByteSize64);
            Buffer* stagingBuffer = nullptr;
            u64 stagingOffset = 0;
            void* stagingBytes = nullptr;
            if(!prepareUploadStaging(clearByteCount, NWB_TEXT("clearDepthStencilTextureBox"), stagingBuffer, stagingOffset, stagingBytes))
                return false;
            VulkanTextureDetail::FillTextureClearBytes(stagingBytes, clearByteCount, clearPattern, clearPatternSize);

            if(mergeArrayLayerCopies){
                Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
                const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
                u32 regionIndex = 0u;
                for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                    VkBufferImageCopy region{};
                    region.bufferOffset = stagingOffset + static_cast<u64>(regionIndex) * layerPitch64;
                    region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspect, mipLevel, arraySlice, 1u);
                    region.imageOffset = { resolvedBox.minX, resolvedBox.minY, 0 };
                    region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
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
                    region.imageOffset = { resolvedBox.minX, resolvedBox.minY, 0 };
                    region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
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

void CommandList::clearTextureUInt(Texture* textureResource, TextureSubresourceSet subresources, u32 clearColor){
    clearTextureUInt(textureResource, subresources, UIntColor(clearColor));
}

void CommandList::clearTextureUInt(Texture* textureResource, TextureSubresourceSet subresources, const UIntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTexture(textureResource, subresources, NWB_TEXT("unsigned integer value"), clearValue, true, false);
}

void CommandList::clearTextureRectUInt(Texture* textureResource, TextureSubresourceSet subresources, const Rect& rect, u32 clearColor){
    clearTextureRectUInt(textureResource, subresources, rect, UIntColor(clearColor));
}

void CommandList::clearTextureRectUInt(Texture* textureResource, TextureSubresourceSet subresources, const Rect& rect, const UIntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, Box(rect, 0, Limit<i32>::s_Max), NWB_TEXT("unsigned integer value"), clearValue, true, false);
}

void CommandList::clearTextureBoxUInt(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, u32 clearColor){
    clearTextureBoxUInt(textureResource, subresources, box, UIntColor(clearColor));
}

void CommandList::clearTextureBoxUInt(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, const UIntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, box, NWB_TEXT("unsigned integer value"), clearValue, true, false);
}

void CommandList::clearTextureInt(Texture* textureResource, TextureSubresourceSet subresources, i32 clearColor){
    clearTextureInt(textureResource, subresources, IntColor(clearColor));
}

void CommandList::clearTextureInt(Texture* textureResource, TextureSubresourceSet subresources, const IntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTexture(textureResource, subresources, NWB_TEXT("signed integer value"), clearValue, true, true);
}

void CommandList::clearTextureRectInt(Texture* textureResource, TextureSubresourceSet subresources, const Rect& rect, i32 clearColor){
    clearTextureRectInt(textureResource, subresources, rect, IntColor(clearColor));
}

void CommandList::clearTextureRectInt(Texture* textureResource, TextureSubresourceSet subresources, const Rect& rect, const IntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, Box(rect, 0, Limit<i32>::s_Max), NWB_TEXT("signed integer value"), clearValue, true, true);
}

void CommandList::clearTextureBoxInt(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, i32 clearColor){
    clearTextureBoxInt(textureResource, subresources, box, IntColor(clearColor));
}

void CommandList::clearTextureBoxInt(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, const IntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, box, NWB_TEXT("signed integer value"), clearValue, true, true);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Color clear implementation


void CommandList::clearColorTexture(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const tchar* valueName,
    const VkClearColorValue& clearValue,
    const bool integerValue,
    const bool signedIntegerValue
){
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("clear texture"), NWB_TEXT("texture is null"), textureResource))
        return;
#if !defined(NWB_DEBUG)
    static_cast<void>(valueName);
#endif
    Texture& texture = *textureResource;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("clear texture")))
        return;

    if(!VulkanTextureDetail::ValidateTextureColorClear(
        texture.m_desc,
        texture.m_aspectMask,
        NWB_TEXT("clear texture"),
        valueName,
        integerValue,
        signedIntegerValue
    ))
        return;

    if(m_renderPassActive){
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear color attachment")))
            return;
        const Rect fullRect(0, Limit<i32>::s_Max, 0, Limit<i32>::s_Max);
        if(clearActiveRenderPassColorTextureRect(texture, resolvedSubresources, fullRect, clearValue, valueName))
            retainResource(textureResource);
        return;
    }

    constexpr GpuQueueCapability::Mask s_ColorClearCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute)
    );
    if(!recordAndValidateAnyCommandCapability(s_ColorClearCapabilities, NWB_TEXT("clear color texture")))
        return;
    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    if(texture.m_desc.dimension != TextureDimension::Texture3D && resolvedSubresources.numArraySlices > 1u){
        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
        Vector<VkImageSubresourceRange, Alloc::ScratchArena> ranges(resolvedSubresources.numArraySlices, scratchArena);
        VulkanTextureDetail::BuildArrayLayerImageSubresourceRanges(resolvedSubresources, VK_IMAGE_ASPECT_COLOR_BIT, ranges);
        vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, static_cast<u32>(ranges.size()), ranges.data());
    }
    else{
        const VkImageSubresourceRange range = VulkanDetail::BuildImageSubresourceRange(resolvedSubresources, VK_IMAGE_ASPECT_COLOR_BIT);
        vkCmdClearColorImage(m_currentCmdBuf->m_cmdBuf, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1u, &range);
    }
    retainResource(textureResource);
}

void CommandList::clearColorTextureBox(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    const tchar* valueName,
    const VkClearColorValue& clearValue,
    const bool integerValue,
    const bool signedIntegerValue
){
    if(VulkanTextureDetail::TextureClearBoxEmpty(box))
        return;
    if(!VulkanDetail::DebugValidateNotNull(NWB_TEXT("clear texture box"), NWB_TEXT("texture is null"), textureResource))
        return;
#if !defined(NWB_DEBUG)
    static_cast<void>(valueName);
#endif

    Texture& texture = *textureResource;
    const TextureDesc& desc = texture.m_desc;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("clear texture box")))
        return;

    if(!VulkanTextureDetail::ValidateTextureColorClear(
        desc,
        texture.m_aspectMask,
        NWB_TEXT("clear texture box"),
        valueName,
        integerValue,
        signedIntegerValue
    ))
        return;

    if(
        (m_renderPassActive || desc.sampleCount != 1u)
        && VulkanTextureDetail::TextureClearBoxCoversSubresources(desc, resolvedSubresources, box)
    ){
        clearColorTexture(textureResource, resolvedSubresources, valueName, clearValue, integerValue, signedIntegerValue);
        return;
    }

    if(m_renderPassActive || desc.sampleCount != 1u){
        const Box resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, resolvedSubresources.baseMipLevel, box);
        if(VulkanTextureDetail::TextureClearBoxEmpty(resolvedBox))
            return;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, resolvedSubresources.baseMipLevel);
        if(resolvedBox.minZ != 0 || resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: attachment bounded box clears must cover the full attachment depth"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: attachment bounded box clears must cover the full attachment depth"), valueName);
            return;
        }
        if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("clear color texture box as attachment")))
            return;

        if(clearActiveRenderPassColorTextureRect(texture, resolvedSubresources, Rect(resolvedBox.minX, resolvedBox.maxX, resolvedBox.minY, resolvedBox.maxY), clearValue, valueName))
            retainResource(textureResource);
        return;
    }

    u8 clearPattern[VulkanTextureDetail::s_TextureClearMaxPatternBytes] = {};
    u32 clearPatternSize = 0u;
    const bool patternReady = !integerValue
        ? VulkanTextureDetail::BuildTextureFloatClearPattern(desc.format, clearValue, clearPattern, clearPatternSize)
        : (
            signedIntegerValue
            ? VulkanTextureDetail::BuildTextureIntClearPattern(desc.format, clearValue, clearPattern, clearPatternSize)
            : VulkanTextureDetail::BuildTextureUIntClearPattern(desc.format, clearValue, clearPattern, clearPatternSize)
        )
    ;
    if(!patternReady || clearPatternSize != texture.m_formatLayout.bytesPerBlock){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to clear texture box with {}: bounded texture box clears do not support texture format {}"),
            valueName,
            StringConvert(GetFormatInfo(desc.format).name)
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: bounded texture box clears do not support texture format"), valueName);
        return;
    }
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, NWB_TEXT("clear color texture box through staging")))
        return;

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
        const Box resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, mipLevel, box);
        if(VulkanTextureDetail::TextureClearBoxEmpty(resolvedBox))
            continue;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
        const u64 clearWidth = static_cast<u64>(resolvedBox.width());
        const u64 clearHeight = static_cast<u64>(resolvedBox.height());
        const u64 clearDepth = static_cast<u64>(resolvedBox.depth());
        if(!VulkanTextureDetail::TextureClearBoxAlignedToBlocks(resolvedBox, mipExtent, texture.m_formatLayout)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: bounded block-compressed box clears require block-aligned box edges except at texture edges"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: bounded block-compressed box clears require block-aligned box edges except at texture edges"), valueName);
            return;
        }

        const u64 blockWidth = static_cast<u64>(texture.m_formatLayout.blockWidth);
        const u64 blockHeight = static_cast<u64>(texture.m_formatLayout.blockHeight);
        const u64 clearBlockCountX = DivideUp(clearWidth, blockWidth);
        const u64 clearBlockCountY = DivideUp(clearHeight, blockHeight);
        if(clearBlockCountX > Limit<u64>::s_Max / clearBlockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            return;
        }
        const u64 clearSliceBlockCount = clearBlockCountX * clearBlockCountY;
        if(clearDepth > 1ull && clearSliceBlockCount > Limit<u64>::s_Max / clearDepth){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            return;
        }
        const u64 clearBlockCount = clearSliceBlockCount * clearDepth;
        if(clearBlockCount > Limit<u64>::s_Max / clearPatternSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size overflows"), valueName);
            return;
        }
        const u64 uploadSize64 = clearBlockCount * clearPatternSize;
        if(uploadSize64 > static_cast<u64>(Limit<usize>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size exceeds addressable memory"), valueName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear texture box with {}: clear byte size exceeds addressable memory"), valueName);
            return;
        }

        const u64 arrayLayerCount = static_cast<u64>(resolvedSubresources.numArraySlices);
        const bool mergeArrayLayerCopies =
            desc.dimension != TextureDimension::Texture3D
            && arrayLayerCount > 1ull
            && uploadSize64 <= (VulkanTextureDetail::s_TextureClearMergedLayerUploadThreshold / arrayLayerCount)
        ;
        const u64 clearByteSize64 = mergeArrayLayerCopies ? uploadSize64 * arrayLayerCount : uploadSize64;

        const usize clearByteCount = static_cast<usize>(clearByteSize64);
        Buffer* stagingBuffer = nullptr;
        u64 stagingOffset = 0;
        void* stagingBytes = nullptr;
        if(!prepareUploadStaging(clearByteCount, NWB_TEXT("clearTextureBox"), stagingBuffer, stagingOffset, stagingBytes))
            return;
        VulkanTextureDetail::FillTextureClearBytes(stagingBytes, clearByteCount, clearPattern, clearPatternSize);

        if(desc.dimension == TextureDimension::Texture3D){
            VkBufferImageCopy region{};
            region.bufferOffset = stagingOffset;
            region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 0u, 1u);
            region.imageOffset = { resolvedBox.minX, resolvedBox.minY, resolvedBox.minZ };
            region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), static_cast<u32>(clearDepth) };
            vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
        }
        else if(mergeArrayLayerCopies){
            Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
            const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
            u32 regionIndex = 0u;
            for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                VkBufferImageCopy region{};
                region.bufferOffset = stagingOffset + static_cast<u64>(regionIndex) * uploadSize64;
                region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arraySlice, 1u);
                region.imageOffset = { resolvedBox.minX, resolvedBox.minY, 0 };
                region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
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
                region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arraySlice, 1u);
                region.imageOffset = { resolvedBox.minX, resolvedBox.minY, 0 };
                region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
                regions[regionIndex++] = region;
            }

            if(!regions.empty())
                vkCmdCopyBufferToImage(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, texture.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(regions.size()), regions.data());
        }

        retainStagingBuffer(*stagingBuffer);
    }

    retainResource(textureResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

