// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureClearValueKind{
    enum Enum : u8{
        Float = 0u,
        UInt,
        Int,
        DepthStencil,
    };
};

namespace TextureClearQueueRequirement{
    enum Enum : u8{
        Transfer = 0u,
        ComputeOrGraphics,
        Graphics,
    };
};

struct TextureClearContract{
    TextureSubresourceSet subresources;
    TextureClearQueueRequirement::Enum queueRequirement = TextureClearQueueRequirement::Transfer;
};

[[nodiscard]] constexpr bool TextureClearStagedEncodingSupportsFormat(const Format::Enum format)noexcept{
    return format >= Format::BC1_UNORM && format <= Format::BC5_SNORM;
}

[[nodiscard]] inline bool TextureClearValueMatchesFormat(
    const TextureDesc& description,
    const TextureClearValueKind::Enum valueKind,
    const bool clearDepth,
    const bool clearStencil
)noexcept{
    if(description.format == Format::UNKNOWN || VulkanDetail::ConvertFormat(description.format) == VK_FORMAT_UNDEFINED)
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(description.format);
    const bool depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
    switch(valueKind){
    case TextureClearValueKind::Float:
        return !depthStencilFormat
            && (formatInfo.kind == FormatKind::Normalized || formatInfo.kind == FormatKind::Float)
        ;
    case TextureClearValueKind::UInt:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
    case TextureClearValueKind::Int:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && formatInfo.isSigned;
    case TextureClearValueKind::DepthStencil:
        return depthStencilFormat
            && (clearDepth || clearStencil)
            && (!clearDepth || formatInfo.hasDepth)
            && (!clearStencil || formatInfo.hasStencil)
        ;
    default:
        return false;
    }
}

[[nodiscard]] inline bool ResolveTextureClearContract(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    const TextureClearValueKind::Enum valueKind,
    const bool clearDepth,
    const bool clearStencil,
    TextureClearContract& outContract
)noexcept{
    outContract = {};
    if(
        !IsTextureDescShapeValid(description)
        || description.sampleQuality != 0u
        || !VulkanDetail::IsSupportedSampleCount(description.sampleCount)
        || !TextureClearValueMatchesFormat(description, valueKind, clearDepth, clearStencil)
    )
        return false;

    const bool blockCompressed = Format::IsBlockCompressedFormat(description.format);
    if(
        blockCompressed
        && (
            description.sampleCount != 1u
            || !TextureClearStagedEncodingSupportsFormat(description.format)
        )
    )
        return false;

    outContract.subresources = subresources.resolve(description, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(outContract.subresources)){
        outContract = {};
        return false;
    }

    if(valueKind == TextureClearValueKind::DepthStencil)
        outContract.queueRequirement = TextureClearQueueRequirement::Graphics;
    else if(!blockCompressed)
        outContract.queueRequirement = TextureClearQueueRequirement::ComputeOrGraphics;
    return true;
}

[[nodiscard]] inline bool TextureClearBoxEmpty(const Box& box)noexcept{
    return box.minX >= box.maxX || box.minY >= box.maxY || box.minZ >= box.maxZ;
}

[[nodiscard]] inline Box ResolveTextureClearBox(
    const TextureDesc& description,
    const MipLevel mipLevel,
    const Box& box
)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, mipLevel);
    const i32 width = static_cast<i32>(mipExtent.width);
    const i32 height = static_cast<i32>(mipExtent.height);
    const i32 depth = static_cast<i32>(mipExtent.depth);
    return Box(
        Max<i32>(0, Min<i32>(box.minX, width)),
        Max<i32>(0, Min<i32>(box.maxX, width)),
        Max<i32>(0, Min<i32>(box.minY, height)),
        Max<i32>(0, Min<i32>(box.maxY, height)),
        Max<i32>(0, Min<i32>(box.minZ, depth)),
        Max<i32>(0, Min<i32>(box.maxZ, depth))
    );
}

[[nodiscard]] inline TextureClearQueueRequirement::Enum TextureClearBoxQueueRequirement(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    const Box& box
)noexcept{
    const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
    for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
        const Box resolvedBox = ResolveTextureClearBox(description, mipLevel, box);
        if(TextureClearBoxEmpty(resolvedBox))
            continue;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, mipLevel);
        if(
            resolvedBox.minX != 0
            || resolvedBox.minY != 0
            || resolvedBox.minZ != 0
            || resolvedBox.maxX != static_cast<i32>(mipExtent.width)
            || resolvedBox.maxY != static_cast<i32>(mipExtent.height)
            || resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)
        )
            return TextureClearQueueRequirement::ComputeOrGraphics;
    }
    return TextureClearQueueRequirement::Transfer;
}

[[nodiscard]] inline bool TextureClearBoxFullyCoversSubresources(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    const Box& box
)noexcept{
    const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
    for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
        const Box resolvedBox = ResolveTextureClearBox(description, mipLevel, box);
        if(TextureClearBoxEmpty(resolvedBox))
            return false;

        const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, mipLevel);
        if(
            resolvedBox.minX != 0
            || resolvedBox.minY != 0
            || resolvedBox.minZ != 0
            || resolvedBox.maxX != static_cast<i32>(mipExtent.width)
            || resolvedBox.maxY != static_cast<i32>(mipExtent.height)
            || resolvedBox.maxZ != static_cast<i32>(mipExtent.depth)
        )
            return false;
    }
    return true;
}

[[nodiscard]] inline bool TextureClearQueueRequirementSatisfied(
    const TextureClearQueueRequirement::Enum requirement,
    const GpuQueueCapability::Mask declaredCapabilities,
    const GpuQueueCapability::Mask physicalCapabilities
)noexcept{
    const u8 declaredBits = static_cast<u8>(declaredCapabilities);
    const u8 physicalBits = static_cast<u8>(physicalCapabilities);
    switch(requirement){
    case TextureClearQueueRequirement::Transfer:
        return (
            declaredBits
            & physicalBits
            & static_cast<u8>(GpuQueueCapability::Transfer)
        ) != 0u;
    case TextureClearQueueRequirement::ComputeOrGraphics:
        return (
            declaredBits
            & physicalBits
            & static_cast<u8>(GpuQueueCapability::Compute | GpuQueueCapability::Graphics)
        ) != 0u;
    case TextureClearQueueRequirement::Graphics:
        return (
            declaredBits
            & physicalBits
            & static_cast<u8>(GpuQueueCapability::Graphics)
        ) != 0u;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

