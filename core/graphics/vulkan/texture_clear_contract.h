// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_shape_policy.h"
#include <core/graphics/backend_selection/texture_contract_types.h>

#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 TextureClearStagedEncodingSupportsFormat(const Format::Enum format)noexcept{
    return format >= Format::BC1_UNORM && format <= Format::BC5_SNORM;
}

[[nodiscard]] inline b8 TextureClearValueMatchesFormat(const TextureDesc& description, const TextureClearValueKind::Enum valueKind, const b8 clearDepth, const b8 clearStencil)noexcept{
    if(description.format == Format::UNKNOWN || VulkanDetail::ConvertFormat(description.format) == VK_FORMAT_UNDEFINED)
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(description.format);
    const b8 depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
    switch(valueKind){
    case TextureClearValueKind::Float:
        return !depthStencilFormat && (formatInfo.kind == FormatKind::Normalized || formatInfo.kind == FormatKind::Float);
    case TextureClearValueKind::UInt:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
    case TextureClearValueKind::Int:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && formatInfo.isSigned;
    case TextureClearValueKind::DepthStencil:
        return depthStencilFormat && (clearDepth || clearStencil) && (!clearDepth || formatInfo.hasDepth) && (!clearStencil || formatInfo.hasStencil);
    default:
        return false;
    }
}

[[nodiscard]] inline Expected<TextureClearContract> ResolveTextureClearContract(const TextureDesc& description, const TextureSubresourceSet& subresources, const TextureClearValueKind::Enum valueKind, const b8 clearDepth, const b8 clearStencil)noexcept{
    TextureClearContract contract{};
    if(!TextureShapePolicy::isValid(description) || description.sampleQuality != 0u || !VulkanDetail::IsSupportedSampleCount(description.sampleCount) || !TextureClearValueMatchesFormat(description, valueKind, clearDepth, clearStencil))
        return MakeUnexpected(Failure{});

    const b8 blockCompressed = Format::IsBlockCompressedFormat(description.format);
    if(blockCompressed && (description.sampleCount != 1u || !TextureClearStagedEncodingSupportsFormat(description.format)))
        return MakeUnexpected(Failure{});

    contract.subresources = subresources.resolve(description, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(contract.subresources)){
        return MakeUnexpected(Failure{});
    }

    if(valueKind == TextureClearValueKind::DepthStencil)
        contract.queueRequirement = TextureClearQueueRequirement::Graphics;
    else if(!blockCompressed)
        contract.queueRequirement = TextureClearQueueRequirement::ComputeOrGraphics;
    return contract;
}

[[nodiscard]] inline b8 TextureClearBoxEmpty(const Box& box)noexcept{
    return box.minX >= box.maxX || box.minY >= box.maxY || box.minZ >= box.maxZ;
}

[[nodiscard]] inline Box ResolveTextureClearBox(const TextureDesc& description, const MipLevel mipLevel, const Box& box)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, mipLevel);
    const i32 width = static_cast<i32>(mipExtent.width);
    const i32 height = static_cast<i32>(mipExtent.height);
    const i32 depth = static_cast<i32>(mipExtent.depth);
    return Box(Max<i32>(0, Min<i32>(box.minX, width)), Max<i32>(0, Min<i32>(box.maxX, width)), Max<i32>(0, Min<i32>(box.minY, height)), Max<i32>(0, Min<i32>(box.maxY, height)), Max<i32>(0, Min<i32>(box.minZ, depth)), Max<i32>(0, Min<i32>(box.maxZ, depth)));
}

[[nodiscard]] inline b8 BoxCoversWholeMipExtent(const TextureDesc& description, const MipLevel mipLevel, const Box& resolvedBox)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, mipLevel);
    return resolvedBox.minX == 0
        && resolvedBox.minY == 0
        && resolvedBox.minZ == 0
        && resolvedBox.maxX == static_cast<i32>(mipExtent.width)
        && resolvedBox.maxY == static_cast<i32>(mipExtent.height)
        && resolvedBox.maxZ == static_cast<i32>(mipExtent.depth)
    ;
}

[[nodiscard]] inline TextureClearQueueRequirement::Enum TextureClearBoxQueueRequirement(const TextureDesc& description, const TextureSubresourceSet& subresources, const Box& box)noexcept{
    const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
    for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
        const Box resolvedBox = ResolveTextureClearBox(description, mipLevel, box);
        if(TextureClearBoxEmpty(resolvedBox))
            continue;

        if(!BoxCoversWholeMipExtent(description, mipLevel, resolvedBox))
            return TextureClearQueueRequirement::ComputeOrGraphics;
    }
    return TextureClearQueueRequirement::Transfer;
}

[[nodiscard]] inline b8 TextureClearBoxFullyCoversSubresources(const TextureDesc& description, const TextureSubresourceSet& subresources, const Box& box)noexcept{
    const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
    for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
        const Box resolvedBox = ResolveTextureClearBox(description, mipLevel, box);
        if(TextureClearBoxEmpty(resolvedBox) || !BoxCoversWholeMipExtent(description, mipLevel, resolvedBox))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

using VulkanTextureDetail::ResolveTextureClearContract;
using VulkanTextureDetail::TextureClearBoxQueueRequirement;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

