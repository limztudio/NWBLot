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

void CommandList::clearTextureRectFloat(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    const Color& clearColor
){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(
        textureResource,
        subresources,
        Box(rect, 0, Limit<i32>::s_Max),
        NWB_TEXT("color value"),
        clearValue,
        false,
        false
    );
}

void CommandList::clearTextureBoxFloat(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    const Color& clearColor
){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(textureResource, subresources, box, NWB_TEXT("color value"), clearValue, false, false);
}

void CommandList::clearTextureUInt(Texture* textureResource, TextureSubresourceSet subresources, u32 clearColor){
    clearTextureUInt(textureResource, subresources, UIntColor(clearColor));
}

void CommandList::clearTextureUInt(Texture* textureResource, TextureSubresourceSet subresources, const UIntColor& clearColor){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTexture(textureResource, subresources, NWB_TEXT("unsigned integer value"), clearValue, true, false);
}

void CommandList::clearTextureRectUInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    u32 clearColor
){
    clearTextureRectUInt(textureResource, subresources, rect, UIntColor(clearColor));
}

void CommandList::clearTextureRectUInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    const UIntColor& clearColor
){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(
        textureResource,
        subresources,
        Box(rect, 0, Limit<i32>::s_Max),
        NWB_TEXT("unsigned integer value"),
        clearValue,
        true,
        false
    );
}

void CommandList::clearTextureBoxUInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    u32 clearColor
){
    clearTextureBoxUInt(textureResource, subresources, box, UIntColor(clearColor));
}

void CommandList::clearTextureBoxUInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    const UIntColor& clearColor
){
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

void CommandList::clearTextureRectInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    i32 clearColor
){
    clearTextureRectInt(textureResource, subresources, rect, IntColor(clearColor));
}

void CommandList::clearTextureRectInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Rect& rect,
    const IntColor& clearColor
){
    const VkClearColorValue clearValue = VulkanTextureDetail::BuildTextureClearColorValue(clearColor);
    clearColorTextureBox(
        textureResource,
        subresources,
        Box(rect, 0, Limit<i32>::s_Max),
        NWB_TEXT("signed integer value"),
        clearValue,
        true,
        true
    );
}

void CommandList::clearTextureBoxInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    i32 clearColor
){
    clearTextureBoxInt(textureResource, subresources, box, IntColor(clearColor));
}

void CommandList::clearTextureBoxInt(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const Box& box,
    const IntColor& clearColor
){
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
    static_cast<void>(valueName);
    constexpr const tchar* s_OperationName = NWB_TEXT("clear texture");
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

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("subresource range is empty or invalid"));
        return;
    }
    if(!VulkanTextureDetail::TextureColorClearAspectIsValid(texture.m_aspectMask)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture format does not have a color aspect"));
        return;
    }
    if(!VulkanTextureDetail::TextureColorClearValueTypeMatchesFormat(
        GetFormatInfo(desc.format),
        integerValue,
        signedIntegerValue
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("clear value type does not match the texture format"));
        return;
    }

    const bool blockCompressed = Format::IsBlockCompressedFormat(desc.format);
    if(blockCompressed && desc.sampleCount != 1u){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("block-compressed texture clears require a single-sampled texture")
        );
        return;
    }
    if(blockCompressed && m_renderPassActive){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("block-compressed texture clears cannot execute during active rendering")
        );
        return;
    }

    if(m_renderPassActive){
        const Rect fullRect(0, Limit<i32>::s_Max, 0, Limit<i32>::s_Max);
        if(clearActiveRenderPassColorTextureRect(texture, resolvedSubresources, fullRect, clearValue, valueName))
            retainResource(textureResource);
        return;
    }

    if(blockCompressed){
        constexpr Box s_FullTextureBox(Limit<i32>::s_Max, Limit<i32>::s_Max, Limit<i32>::s_Max);
        clearColorTextureBox(
            textureResource,
            resolvedSubresources,
            s_FullTextureBox,
            valueName,
            clearValue,
            integerValue,
            signedIntegerValue
        );
        return;
    }

    constexpr GpuQueueCapability::Mask s_ColorClearCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute)
    );
    if(!recordAndValidateAnyCommandCapability(s_ColorClearCapabilities, s_OperationName))
        return;
    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;
    if(texture.m_desc.dimension != TextureDimension::Texture3D && resolvedSubresources.numArraySlices > 1u){
        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
        Vector<VkImageSubresourceRange, Alloc::ScratchArena> ranges(resolvedSubresources.numArraySlices, scratchArena);
        VulkanTextureDetail::BuildArrayLayerImageSubresourceRanges(resolvedSubresources, VK_IMAGE_ASPECT_COLOR_BIT, ranges);
        vkCmdClearColorImage(
            m_currentCmdBuf->m_cmdBuf,
            texture.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearValue,
            static_cast<u32>(ranges.size()),
            ranges.data()
        );
    }
    else{
        const VkImageSubresourceRange range = VulkanDetail::BuildImageSubresourceRange(
            resolvedSubresources,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        vkCmdClearColorImage(
            m_currentCmdBuf->m_cmdBuf,
            texture.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearValue,
            1u,
            &range
        );
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
    static_cast<void>(valueName);
    constexpr const tchar* s_OperationName = NWB_TEXT("clear texture box");
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

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("subresource range is empty or invalid"));
        return;
    }
    if(!VulkanTextureDetail::TextureColorClearAspectIsValid(texture.m_aspectMask)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("texture format does not have a color aspect"));
        return;
    }
    if(!VulkanTextureDetail::TextureColorClearValueTypeMatchesFormat(
        GetFormatInfo(desc.format),
        integerValue,
        signedIntegerValue
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("clear value type does not match the texture format"));
        return;
    }

    const bool blockCompressed = Format::IsBlockCompressedFormat(desc.format);
    if(
        (m_renderPassActive || desc.sampleCount != 1u)
        && VulkanTextureDetail::TextureClearBoxCoversSubresources(desc, resolvedSubresources, box)
    ){
        clearColorTexture(textureResource, resolvedSubresources, valueName, clearValue, integerValue, signedIntegerValue);
        return;
    }

    const Box baseResolvedBox = VulkanTextureDetail::ResolveTextureClearBox(
        desc,
        resolvedSubresources.baseMipLevel,
        box
    );
    if(VulkanTextureDetail::TextureClearBoxEmpty(baseResolvedBox))
        return;
    if(blockCompressed && desc.sampleCount != 1u){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("block-compressed texture clears require a single-sampled texture")
        );
        return;
    }
    if(blockCompressed && m_renderPassActive){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("block-compressed texture clears cannot execute during active rendering")
        );
        return;
    }

    if(m_renderPassActive || desc.sampleCount != 1u){
        if(!m_renderPassActive){
            rejectCommandRecording(s_OperationName, NWB_TEXT("bounded multisampled clears require active rendering"));
            return;
        }

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, resolvedSubresources.baseMipLevel);
        if(baseResolvedBox.minZ != 0 || baseResolvedBox.maxZ != static_cast<i32>(mipExtent.depth)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("attachment bounded clears require full attachment depth"));
            return;
        }

        const Rect rect(
            baseResolvedBox.minX,
            baseResolvedBox.maxX,
            baseResolvedBox.minY,
            baseResolvedBox.maxY
        );
        if(clearActiveRenderPassColorTextureRect(texture, resolvedSubresources, rect, clearValue, valueName))
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
        rejectCommandRecording(
            NWB_TEXT("clear texture box"),
            NWB_TEXT("bounded texture box clears do not support the texture format")
        );
        return;
    }

    struct MipClearPlan{
        Box resolvedBox;
        VulkanTextureDetail::TextureClearUploadLayout uploadLayout;
    };

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_TextureClearArena);
    Vector<MipClearPlan, Alloc::ScratchArena> mipClearPlans(resolvedSubresources.numMipLevels, scratchArena);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const u64 blockWidth = static_cast<u64>(texture.m_formatLayout.blockWidth);
    const u64 blockHeight = static_cast<u64>(texture.m_formatLayout.blockHeight);
    const u64 arrayLayerCount = static_cast<u64>(resolvedSubresources.numArraySlices);
    bool hasNonemptyMip = false;
    u32 mipPlanIndex = 0u;
    for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel, ++mipPlanIndex){
        MipClearPlan& mipPlan = mipClearPlans[mipPlanIndex];
        mipPlan.resolvedBox = VulkanTextureDetail::ResolveTextureClearBox(desc, mipLevel, box);
        if(VulkanTextureDetail::TextureClearBoxEmpty(mipPlan.resolvedBox))
            continue;
        hasNonemptyMip = true;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
        if(!VulkanTextureDetail::TextureClearBoxAlignedToBlocks(
            mipPlan.resolvedBox,
            mipExtent,
            texture.m_formatLayout
        )){
            rejectCommandRecording(
                NWB_TEXT("clear texture box"),
                NWB_TEXT("bounded block-compressed clear edges must be block-aligned except at texture edges")
            );
            return;
        }

        const u64 clearWidth = static_cast<u64>(mipPlan.resolvedBox.width());
        const u64 clearHeight = static_cast<u64>(mipPlan.resolvedBox.height());
        const u64 clearDepth = static_cast<u64>(mipPlan.resolvedBox.depth());
        const u64 clearBlockCountX = DivideUp(clearWidth, blockWidth);
        const u64 clearBlockCountY = DivideUp(clearHeight, blockHeight);
        if(clearBlockCountX > Limit<u64>::s_Max / clearBlockCountY){
            rejectCommandRecording(NWB_TEXT("clear texture box"), NWB_TEXT("clear byte size overflows"));
            return;
        }
        const u64 clearSliceBlockCount = clearBlockCountX * clearBlockCountY;
        if(clearDepth > 1ull && clearSliceBlockCount > Limit<u64>::s_Max / clearDepth){
            rejectCommandRecording(NWB_TEXT("clear texture box"), NWB_TEXT("clear byte size overflows"));
            return;
        }
        const u64 clearBlockCount = clearSliceBlockCount * clearDepth;
        if(!VulkanTextureDetail::BuildTextureClearUploadLayout(
            clearBlockCount,
            clearPatternSize,
            arrayLayerCount,
            mipPlan.uploadLayout
        )){
            rejectCommandRecording(
                NWB_TEXT("clear texture box"),
                NWB_TEXT("clear upload layout is not addressable")
            );
            return;
        }
    }
    if(!hasNonemptyMip)
        return;

    const VulkanTextureDetail::TextureClearQueueRequirement::Enum queueRequirement =
        VulkanTextureDetail::TextureClearBoxQueueRequirement(desc, resolvedSubresources, box)
    ;
    if(queueRequirement == VulkanTextureDetail::TextureClearQueueRequirement::Transfer){
        if(!recordAndValidateCommandCapability(
            GpuQueueCapability::Transfer,
            NWB_TEXT("clear color texture box through staging")
        ))
            return;
    }
    else{
        constexpr GpuQueueCapability::Mask s_PartialClearCapabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Compute) | static_cast<u8>(GpuQueueCapability::Graphics)
        );
        if(!recordAndValidateAnyCommandCapability(
            s_PartialClearCapabilities,
            NWB_TEXT("clear partial color texture box through staging")
        ))
            return;
    }

    setTextureState(textureResource, resolvedSubresources, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;

    mipPlanIndex = 0u;
    for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel, ++mipPlanIndex){
        const MipClearPlan& mipPlan = mipClearPlans[mipPlanIndex];
        if(VulkanTextureDetail::TextureClearBoxEmpty(mipPlan.resolvedBox))
            continue;

        const u64 clearWidth = static_cast<u64>(mipPlan.resolvedBox.width());
        const u64 clearHeight = static_cast<u64>(mipPlan.resolvedBox.height());
        const u64 clearDepth = static_cast<u64>(mipPlan.resolvedBox.depth());

        Buffer* stagingBuffer = nullptr;
        u64 stagingOffset = 0;
        void* stagingBytes = nullptr;
        if(!prepareUploadStaging(
            mipPlan.uploadLayout.clearByteCount,
            NWB_TEXT("clearTextureBox"),
            stagingBuffer,
            stagingOffset,
            stagingBytes,
            mipPlan.uploadLayout.stagingAlignment
        )){
            rejectCommandRecording(NWB_TEXT("clear texture box"), NWB_TEXT("staging allocation failed"));
            return;
        }
        if(
            !stagingBuffer
            || !stagingBytes
            || (stagingOffset % mipPlan.uploadLayout.stagingAlignment) != 0u
            || (stagingOffset % mipPlan.uploadLayout.copyOffsetAlignment) != 0u
            || stagingOffset > stagingBuffer->m_creationDesc.byteSize
            || static_cast<u64>(mipPlan.uploadLayout.clearByteCount)
                > stagingBuffer->m_creationDesc.byteSize - stagingOffset
        ){
            rejectCommandRecording(
                NWB_TEXT("clear texture box"),
                NWB_TEXT("staging allocation returned an invalid range")
            );
            return;
        }
        VulkanTextureDetail::FillTextureClearBytes(
            stagingBytes,
            mipPlan.uploadLayout.clearByteCount,
            clearPattern,
            clearPatternSize
        );

        if(desc.dimension == TextureDimension::Texture3D){
            VkBufferImageCopy region{};
            region.bufferOffset = stagingOffset;
            region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(
                VK_IMAGE_ASPECT_COLOR_BIT,
                mipLevel,
                0u,
                1u
            );
            region.imageOffset = { mipPlan.resolvedBox.minX, mipPlan.resolvedBox.minY, mipPlan.resolvedBox.minZ };
            region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), static_cast<u32>(clearDepth) };
            vkCmdCopyBufferToImage(
                m_currentCmdBuf->m_cmdBuf,
                stagingBuffer->m_buffer,
                texture.m_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1u,
                &region
            );
        }
        else if(mipPlan.uploadLayout.mergeArrayLayerCopies){
            Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
            const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
            u32 regionIndex = 0u;
            for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                VkBufferImageCopy region{};
                region.bufferOffset = stagingOffset
                    + static_cast<u64>(regionIndex) * mipPlan.uploadLayout.layerPitch
                ;
                region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevel,
                    arraySlice,
                    1u
                );
                region.imageOffset = { mipPlan.resolvedBox.minX, mipPlan.resolvedBox.minY, 0 };
                region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
                regions[regionIndex++] = region;
            }

            if(!regions.empty())
                vkCmdCopyBufferToImage(
                    m_currentCmdBuf->m_cmdBuf,
                    stagingBuffer->m_buffer,
                    texture.m_image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    static_cast<u32>(regions.size()),
                    regions.data()
                );
        }
        else{
            Vector<VkBufferImageCopy, Alloc::ScratchArena> regions(resolvedSubresources.numArraySlices, scratchArena);
            const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
            u32 regionIndex = 0u;
            for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                VkBufferImageCopy region{};
                region.bufferOffset = stagingOffset;
                region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    mipLevel,
                    arraySlice,
                    1u
                );
                region.imageOffset = { mipPlan.resolvedBox.minX, mipPlan.resolvedBox.minY, 0 };
                region.imageExtent = { static_cast<u32>(clearWidth), static_cast<u32>(clearHeight), 1u };
                regions[regionIndex++] = region;
            }

            if(!regions.empty())
                vkCmdCopyBufferToImage(
                    m_currentCmdBuf->m_cmdBuf,
                    stagingBuffer->m_buffer,
                    texture.m_image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    static_cast<u32>(regions.size()),
                    regions.data()
                );
        }

        retainStagingBuffer(*stagingBuffer);
    }

    retainResource(textureResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

