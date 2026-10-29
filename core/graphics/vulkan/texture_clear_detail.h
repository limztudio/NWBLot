// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_clear_contract.h"
#include "arena_names.h"

#include <global/algorithm.h>
#include <global/simd/convert.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TextureClearColorComponentCount = 3u;
inline constexpr u32 s_TextureClearRGComponentCount = 2u;
inline constexpr u32 s_TextureClearRGBAComponentCount = 4u;
inline constexpr u32 s_TextureClearDepthPatternBytes = 4u;
inline constexpr u32 s_TextureClearStencilPatternBytes = 1u;
inline constexpr u32 s_TextureClearMaxPatternBytes = 16u;
inline constexpr u32 s_TextureClearUploadAlignment = 4u;

inline constexpr TStringView s_TextureNotLiveResourceReason = NWB_TEXT("texture must be a live resource owned by this device");
inline constexpr TStringView s_TextureNotReadyForNativeClearReason = NWB_TEXT("texture is not ready for the requested native clear on this exact command queue");
inline constexpr TStringView s_ClearSubresourceRangeInvalidReason = NWB_TEXT("subresource range is empty or invalid");
inline constexpr TStringView s_BoundedMultisampledClearInactiveReason = NWB_TEXT("bounded multisampled clears require active rendering");
inline constexpr TStringView s_BoundedClearDepthReason = NWB_TEXT("attachment bounded clears require full attachment depth");
inline constexpr TStringView s_ClearUploadLayoutUnaddressableReason = NWB_TEXT("clear upload layout is not addressable");
inline constexpr TStringView s_ClearStagingRangeInvalidReason = NWB_TEXT("staging allocation returned an invalid range");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline b8 TextureClearRectEmpty(const Rect& rect)noexcept{
    return rect.minX >= rect.maxX || rect.minY >= rect.maxY;
}

inline Rect ResolveTextureClearRect(const TextureDesc& desc, const MipLevel mipLevel, const Rect& rect)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
    const i32 width = static_cast<i32>(mipExtent.width);
    const i32 height = static_cast<i32>(mipExtent.height);
    return Rect(Max<i32>(0, Min<i32>(rect.minX, width)), Max<i32>(0, Min<i32>(rect.maxX, width)), Max<i32>(0, Min<i32>(rect.minY, height)), Max<i32>(0, Min<i32>(rect.maxY, height)));
}

inline b8 TextureClearSubresourcesContainedBy(const TextureSubresourceSet& requested, const TextureSubresourceSet& container)noexcept{
    const MipLevel requestedMipEnd = requested.baseMipLevel + requested.numMipLevels;
    const MipLevel containerMipEnd = container.baseMipLevel + container.numMipLevels;
    const ArraySlice requestedArrayEnd = requested.baseArraySlice + requested.numArraySlices;
    const ArraySlice containerArrayEnd = container.baseArraySlice + container.numArraySlices;

    return
        requested.numMipLevels != 0u
        && requested.numArraySlices != 0u
        && requested.baseMipLevel >= container.baseMipLevel
        && requestedMipEnd <= containerMipEnd
        && requested.baseArraySlice >= container.baseArraySlice
        && requestedArrayEnd <= containerArrayEnd
    ;
}

inline void BuildArrayLayerImageSubresourceRanges(const TextureSubresourceSet& subresources, const VkImageAspectFlags aspectMask, const Span<VkImageSubresourceRange> ranges)noexcept{
    const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
    u32 rangeIndex = 0u;
    for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice)
        ranges[rangeIndex++] = VulkanDetail::BuildImageSubresourceRange(TextureSubresourceSet(subresources.baseMipLevel, subresources.numMipLevels, arraySlice, 1u), aspectMask);
}

inline VkClearRect BuildTextureAttachmentClearRect(const TextureSubresourceSet& requested, const TextureSubresourceSet& attachment, const Rect& rect)noexcept{
    VkClearRect clearRect{};
    clearRect.rect.offset = { rect.minX, rect.minY };
    clearRect.rect.extent = { static_cast<u32>(rect.width()), static_cast<u32>(rect.height()) };
    clearRect.baseArrayLayer = requested.baseArraySlice - attachment.baseArraySlice;
    clearRect.layerCount = requested.numArraySlices;
    return clearRect;
}

struct TextureAttachmentClearTarget{
    TextureSubresourceSet resolvedSubresources;
    u32 colorAttachmentIndex = 0u;
    b8 isReadOnly = false;
};

inline Expected<TextureSubresourceSet> ResolveTextureAttachmentClearSubresources(Texture& texture, const FramebufferAttachment& attachment, const TextureSubresourceSet& requestedSubresources)noexcept{
    if(attachment.texture != &texture)
        return MakeUnexpected(Failure{});
    const TextureSubresourceSet resolved = attachment.subresources.resolve(texture.getCreationDescription(), TextureSubresourceMipResolve::Single);
    if(!TextureClearSubresourcesContainedBy(requestedSubresources, resolved))
        return MakeUnexpected(Failure{});
    return resolved;
}

inline Expected<TextureAttachmentClearTarget> FindTextureColorAttachmentClearTarget(Texture& texture, const TextureSubresourceSet& requestedSubresources, const FramebufferDesc& fbDesc)noexcept{
    u32 colorAttachmentIndex = 0u;
    for(const FramebufferAttachment& attachment : fbDesc.colorAttachments){
        if(!attachment.texture)
            continue;
        const u32 activeColorAttachmentIndex = colorAttachmentIndex++;
        const auto resolved = ResolveTextureAttachmentClearSubresources(texture, attachment, requestedSubresources);
        if(!resolved)
            continue;
        return TextureAttachmentClearTarget{ *resolved, activeColorAttachmentIndex, attachment.isReadOnly };
    }
    return MakeUnexpected(Failure{});
}

inline b8 TextureAttachmentClearRectContainedByFramebuffer(const VkClearRect& clearRect, const FramebufferInfoEx& framebufferInfo)noexcept{
    if(clearRect.rect.offset.x < 0 || clearRect.rect.offset.y < 0)
        return false;

    const u64 endX = static_cast<u64>(clearRect.rect.offset.x) + clearRect.rect.extent.width;
    const u64 endY = static_cast<u64>(clearRect.rect.offset.y) + clearRect.rect.extent.height;
    const u64 endLayer = static_cast<u64>(clearRect.baseArrayLayer) + clearRect.layerCount;
    return endX <= framebufferInfo.width && endY <= framebufferInfo.height && endLayer <= framebufferInfo.arraySize;
}

inline b8 TextureClearBoxAlignedToBlocks(const Box& box, const VkExtent3D& mipExtent, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept{
    if(formatLayout.blockWidth <= 1u && formatLayout.blockHeight <= 1u)
        return true;

    const i32 blockWidth = static_cast<i32>(formatLayout.blockWidth);
    const i32 blockHeight = static_cast<i32>(formatLayout.blockHeight);
    return (box.minX % blockWidth) == 0 && (box.minY % blockHeight) == 0 && (box.maxX == static_cast<i32>(mipExtent.width) || (box.maxX % blockWidth) == 0) && (box.maxY == static_cast<i32>(mipExtent.height) || (box.maxY % blockHeight) == 0);
}

inline constexpr u64 s_TextureClearMergedLayerUploadThreshold = 64ull * 1024ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextureClearUploadLayout{
    u64 uploadSize = 0ull;
    u64 layerPitch = 0ull;
    usize clearByteCount = 0u;
    u32 copyOffsetAlignment = 0u;
    u32 stagingAlignment = 0u;
    b8 mergeArrayLayerCopies = false;
};

inline Expected<TextureClearUploadLayout> BuildTextureClearUploadLayout(const u64 elementCount, const u32 elementSize, const u64 arrayLayerCount)noexcept{
    if(elementCount == 0ull || elementSize == 0u || arrayLayerCount == 0ull || elementCount > Limit<u64>::s_Max / elementSize)
        return MakeUnexpected(Failure{});
    const u64 uploadSize = elementCount * elementSize;
    const auto copyOffsetAlignment = CommonAlignmentChecked<u32>(s_TextureClearUploadAlignment, elementSize);
    if(!copyOffsetAlignment)
        return MakeUnexpected(copyOffsetAlignment.error());
    const auto stagingAlignment = VulkanDetail::TryComputeUploadSuballocationAlignment(*copyOffsetAlignment);
    const auto layerPitch = AlignUpU64Checked(uploadSize, *copyOffsetAlignment);
    if(!stagingAlignment || !layerPitch)
        return MakeUnexpected(Failure{});
    const b8 mergeArrayLayerCopies = arrayLayerCount > 1ull && *layerPitch <= s_TextureClearMergedLayerUploadThreshold / arrayLayerCount;
    if(mergeArrayLayerCopies && arrayLayerCount - 1ull > (Limit<u64>::s_Max - uploadSize) / *layerPitch)
        return MakeUnexpected(Failure{});
    const u64 clearByteCount = mergeArrayLayerCopies ? *layerPitch * (arrayLayerCount - 1ull) + uploadSize : uploadSize;
    if(clearByteCount > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});
    return TextureClearUploadLayout{ uploadSize, *layerPitch, static_cast<usize>(clearByteCount), *copyOffsetAlignment, *stagingAlignment, mergeArrayLayerCopies };
}

[[nodiscard]] inline b8 TextureClearStagingRangeValid(const void* const stagingBytes, const u64 stagingOffset, const u64 stagingBufferByteSize, const TextureClearUploadLayout& uploadLayout)noexcept{
    return
        stagingBytes != nullptr
        && (stagingOffset % uploadLayout.stagingAlignment) == 0u
        && (stagingOffset % uploadLayout.copyOffsetAlignment) == 0u
        && stagingOffset <= stagingBufferByteSize
        && static_cast<u64>(uploadLayout.clearByteCount) <= stagingBufferByteSize - stagingOffset
    ;
}

inline void BuildArrayLayerBufferImageCopies(
    const TextureSubresourceSet& resolvedSubresources,
    const VkImageAspectFlags aspectMask,
    const MipLevel mipLevel,
    const Box& resolvedBox,
    const u64 stagingOffset,
    const TextureClearUploadLayout& uploadLayout,
    const Span<VkBufferImageCopy> regions
)noexcept{
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const u64 layerPitch = uploadLayout.mergeArrayLayerCopies ? uploadLayout.layerPitch : 0ull;
    const VkExtent3D imageExtent{ static_cast<u32>(resolvedBox.width()), static_cast<u32>(resolvedBox.height()), 1u };
    u32 regionIndex = 0u;
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        VkBufferImageCopy region{};
        region.bufferOffset = stagingOffset + static_cast<u64>(regionIndex) * layerPitch;
        region.imageSubresource = VulkanDetail::BuildImageSubresourceLayers(aspectMask, mipLevel, arraySlice, 1u);
        region.imageOffset = { resolvedBox.minX, resolvedBox.minY, 0 };
        region.imageExtent = imageExtent;
        regions[regionIndex++] = region;
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

