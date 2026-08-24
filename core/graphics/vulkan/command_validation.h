// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr VkPrimitiveTopology GetPrimitiveTopology(const PrimitiveType::Enum primitiveType)noexcept{
    switch(primitiveType){
    case PrimitiveType::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveType::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveType::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveType::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveType::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case PrimitiveType::TriangleListWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
    case PrimitiveType::TriangleStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    case PrimitiveType::PatchList: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    default: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }
}

[[nodiscard]] inline bool IsViewportValid(
    const Viewport& viewport,
    const VkPhysicalDeviceLimits& limits
)noexcept{
    if(
        !IsFinite(viewport.minX)
        || !IsFinite(viewport.maxX)
        || !IsFinite(viewport.minY)
        || !IsFinite(viewport.maxY)
        || !IsFinite(viewport.minZ)
        || !IsFinite(viewport.maxZ)
    )
        return false;
    if(
        viewport.maxX <= viewport.minX
        || viewport.maxY < viewport.minY
        || viewport.minZ < 0.0f
        || viewport.minZ > 1.0f
        || viewport.maxZ < 0.0f
        || viewport.maxZ > 1.0f
    )
        return false;

    const f32 width = viewport.maxX - viewport.minX;
    const f32 height = viewport.maxY - viewport.minY;
    return
        static_cast<f64>(width) <= static_cast<f64>(limits.maxViewportDimensions[0u])
        && static_cast<f64>(height) <= static_cast<f64>(limits.maxViewportDimensions[1u])
        && viewport.minX >= limits.viewportBoundsRange[0u]
        && viewport.maxX <= limits.viewportBoundsRange[1u]
        && viewport.minY >= limits.viewportBoundsRange[0u]
        && viewport.maxY <= limits.viewportBoundsRange[1u]
    ;
}

[[nodiscard]] constexpr bool IsScissorRectValid(const Rect& scissor)noexcept{
    return
        scissor.minX >= 0
        && scissor.minY >= 0
        && scissor.maxX >= scissor.minX
        && scissor.maxY >= scissor.minY
    ;
}

[[nodiscard]] inline bool IsImplicitScissorValid(const Viewport& viewport)noexcept{
    if(
        !IsFinite(viewport.minX)
        || !IsFinite(viewport.maxX)
        || !IsFinite(viewport.minY)
        || !IsFinite(viewport.maxY)
    )
        return false;

    const f64 minX = static_cast<f64>(Floor(viewport.minX));
    const f64 maxX = static_cast<f64>(Ceil(viewport.maxX));
    const f64 minY = static_cast<f64>(Floor(viewport.minY));
    const f64 maxY = static_cast<f64>(Ceil(viewport.maxY));
    return
        minX >= 0.0
        && minY >= 0.0
        && maxX >= minX
        && maxY >= minY
        && maxX <= static_cast<f64>(Limit<i32>::s_Max)
        && maxY <= static_cast<f64>(Limit<i32>::s_Max)
    ;
}

[[nodiscard]] inline bool BuildImplicitScissor(const Viewport& viewport, VkRect2D& outScissor)noexcept{
    if(!IsImplicitScissorValid(viewport))
        return false;

    const Rect rect(viewport);
    outScissor = {};
    outScissor.offset = { rect.minX, rect.minY };
    outScissor.extent = {
        static_cast<u32>(rect.maxX - rect.minX),
        static_cast<u32>(rect.maxY - rect.minY)
    };
    return true;
}

[[nodiscard]] constexpr bool TextureSubresourceRangesOverlap(
    const TextureSubresourceSet& lhs,
    const TextureSubresourceSet& rhs
)noexcept{
    const u64 lhsMipEnd = static_cast<u64>(lhs.baseMipLevel) + lhs.numMipLevels;
    const u64 rhsMipEnd = static_cast<u64>(rhs.baseMipLevel) + rhs.numMipLevels;
    const u64 lhsArrayEnd = static_cast<u64>(lhs.baseArraySlice) + lhs.numArraySlices;
    const u64 rhsArrayEnd = static_cast<u64>(rhs.baseArraySlice) + rhs.numArraySlices;
    return
        lhs.baseMipLevel < rhsMipEnd
        && rhs.baseMipLevel < lhsMipEnd
        && lhs.baseArraySlice < rhsArrayEnd
        && rhs.baseArraySlice < lhsArrayEnd
    ;
}

[[nodiscard]] constexpr TextureDimension::Enum GetFramebufferAttachmentViewDimension(
    const TextureDesc& desc,
    const TextureSubresourceSet& resolvedSubresources
)noexcept{
    const bool arrayView = resolvedSubresources.numArraySlices > 1u;
    switch(desc.dimension){
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
        return arrayView ? TextureDimension::Texture2DArray : TextureDimension::Texture2D;
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        return arrayView ? TextureDimension::Texture2DMSArray : TextureDimension::Texture2DMS;
    case TextureDimension::Texture3D:
        return arrayView ? TextureDimension::Unknown : TextureDimension::Texture2D;
    default:
        return TextureDimension::Unknown;
    }
}

[[nodiscard]] constexpr bool IsFramebufferAttachmentSubresourceSetValid(
    const TextureDesc& desc,
    const TextureSubresourceSet& subresources
)noexcept{
    if(desc.mipLevels == 0u || subresources.baseMipLevel >= desc.mipLevels)
        return false;
    const u32 remainingMipLevels = desc.mipLevels - subresources.baseMipLevel;
    if(subresources.numMipLevels == TextureSubresourceSet::AllMipLevels){
        if(remainingMipLevels != 1u)
            return false;
    }
    else if(subresources.numMipLevels != 1u)
        return false;

    switch(desc.dimension){
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMSArray:
        if(subresources.baseArraySlice >= desc.arraySize)
            return false;
        if(subresources.numArraySlices == TextureSubresourceSet::AllArraySlices)
            return true;
        return
            subresources.numArraySlices != 0u
            && subresources.numArraySlices <= desc.arraySize - subresources.baseArraySlice
        ;
    default:
        return
            subresources.baseArraySlice == 0u
            && (
                subresources.numArraySlices == 1u
                || subresources.numArraySlices == TextureSubresourceSet::AllArraySlices
            )
        ;
    }
}

[[nodiscard]] inline bool IsPipelineColorAttachmentFormatClassValid(const Format::Enum format)noexcept{
    if(ConvertFormat(format) == VK_FORMAT_UNDEFINED)
        return false;
    const FormatInfo& formatInfo = GetFormatInfo(format);
    return !formatInfo.hasDepth && !formatInfo.hasStencil;
}

[[nodiscard]] constexpr bool IsStridedBufferRangeValid(
    const BufferDesc& desc,
    const u64 bindingOffsetBytes,
    const u32 firstElement,
    const u32 elementCount,
    const u32 elementStrideBytes,
    const u32 requiredElementBytes
)noexcept{
    if(elementCount == 0u)
        return true;
    if(elementStrideBytes == 0u || requiredElementBytes == 0u || requiredElementBytes > elementStrideBytes)
        return false;
    if(static_cast<u64>(firstElement) > Limit<u64>::s_Max / elementStrideBytes)
        return false;
    const u64 firstElementOffsetBytes = static_cast<u64>(firstElement) * elementStrideBytes;
    if(bindingOffsetBytes > Limit<u64>::s_Max - firstElementOffsetBytes)
        return false;
    const u64 firstRequiredByte = bindingOffsetBytes + firstElementOffsetBytes;
    const u64 remainingElementCount = static_cast<u64>(elementCount) - 1u;
    if(remainingElementCount > Limit<u64>::s_Max / elementStrideBytes)
        return false;
    const u64 remainingElementBytes = remainingElementCount * elementStrideBytes;
    if(firstRequiredByte > Limit<u64>::s_Max - remainingElementBytes)
        return false;
    const u64 lastElementOffsetBytes = firstRequiredByte + remainingElementBytes;
    return lastElementOffsetBytes <= desc.byteSize && requiredElementBytes <= desc.byteSize - lastElementOffsetBytes;
}

[[nodiscard]] constexpr u32 GetIndexElementByteSize(const Format::Enum format)noexcept{
    return format == Format::R16_UINT ? sizeof(u16) : format == Format::R32_UINT ? sizeof(u32) : 0u;
}

[[nodiscard]] constexpr bool IsIndexFormatSupported(
    const Format::Enum format,
    const bool fullDrawIndexUint32Enabled
)noexcept{
    return format == Format::R16_UINT || (format == Format::R32_UINT && fullDrawIndexUint32Enabled);
}

[[nodiscard]] constexpr bool IsStencilReadOnlyCompatible(const DepthStencilState& state)noexcept{
    const auto keepsStencil = [](const DepthStencilState::StencilOpDesc& stencilState)constexpr{
        return
            stencilState.failOp == StencilOp::Keep
            && stencilState.depthFailOp == StencilOp::Keep
            && stencilState.passOp == StencilOp::Keep
        ;
    };
    return
        !state.stencilEnable
        || state.stencilWriteMask == 0u
        || (keepsStencil(state.frontFaceStencil) && keepsStencil(state.backFaceStencil))
    ;
}

[[nodiscard]] constexpr bool IsDepthStencilReadOnlyCompatible(
    const DepthStencilState& state,
    const VkImageAspectFlags attachmentAspects
)noexcept{
    const bool depthCompatible =
        (attachmentAspects & VK_IMAGE_ASPECT_DEPTH_BIT) == 0u
        || !state.depthTestEnable
        || !state.depthWriteEnable
    ;
    const bool stencilCompatible =
        (attachmentAspects & VK_IMAGE_ASPECT_STENCIL_BIT) == 0u || IsStencilReadOnlyCompatible(state);
    return depthCompatible && stencilCompatible;
}

[[nodiscard]] constexpr bool IsIndexDrawRangeValid(
    const BufferDesc& desc,
    const u64 bindingOffsetBytes,
    const u32 startIndex,
    const u32 indexCount,
    const u32 indexElementByteSize
)noexcept{
    if(indexElementByteSize == 0u || (bindingOffsetBytes % indexElementByteSize) != 0u)
        return false;
    if(static_cast<u64>(startIndex) > Limit<u64>::s_Max / indexElementByteSize)
        return false;
    if(static_cast<u64>(indexCount) > Limit<u64>::s_Max / indexElementByteSize)
        return false;

    const u64 startOffsetBytes = static_cast<u64>(startIndex) * indexElementByteSize;
    const u64 indexBytes = static_cast<u64>(indexCount) * indexElementByteSize;
    if(bindingOffsetBytes > Limit<u64>::s_Max - startOffsetBytes)
        return false;
    const u64 drawOffsetBytes = bindingOffsetBytes + startOffsetBytes;
    return drawOffsetBytes <= desc.byteSize && indexBytes <= desc.byteSize - drawOffsetBytes;
}

[[nodiscard]] constexpr bool IsIndirectCommandRangeValid(
    const BufferDesc& desc,
    const u64 offsetBytes,
    const u64 commandSizeBytes,
    const u32 commandCount
)noexcept{
    if(commandCount == 0u || commandSizeBytes == 0u || (offsetBytes & s_BufferAlignmentMask) != 0u)
        return false;
    if(commandSizeBytes > Limit<u64>::s_Max / commandCount)
        return false;
    const u64 totalBytes = commandSizeBytes * commandCount;
    return offsetBytes <= desc.byteSize && totalBytes <= desc.byteSize - offsetBytes;
}

[[nodiscard]] constexpr bool IsIndirectDrawCountValid(
    const u32 drawCount,
    const u32 maximumDrawCount,
    const bool multiDrawIndirectEnabled
)noexcept{
    return drawCount != 0u && drawCount <= maximumDrawCount && (drawCount == 1u || multiDrawIndirectEnabled);
}

[[nodiscard]] constexpr bool AreMeshDispatchGroupCountsValid(
    const u32 groupsX,
    const u32 groupsY,
    const u32 groupsZ,
    const u32* const maximumGroupCounts,
    const u32 maximumTotalGroupCount
)noexcept{
    if(
        !maximumGroupCounts
        || groupsX == 0u
        || groupsY == 0u
        || groupsZ == 0u
        || groupsX > maximumGroupCounts[0u]
        || groupsY > maximumGroupCounts[1u]
        || groupsZ > maximumGroupCounts[2u]
    )
        return false;
    if(groupsX > maximumTotalGroupCount || groupsY > maximumTotalGroupCount || groupsZ > maximumTotalGroupCount)
        return false;

    const u64 xy = static_cast<u64>(groupsX) * groupsY;
    return xy <= static_cast<u64>(maximumTotalGroupCount) / groupsZ;
}

struct MeshDispatchLimits{
    const u32* maximumGroupCounts = nullptr;
    u32 maximumTotalGroupCount = 0u;
};

[[nodiscard]] inline MeshDispatchLimits GetMeshDispatchLimits(
    const VkPhysicalDeviceMeshShaderPropertiesEXT& properties,
    const bool taskShaderActive
)noexcept{
    return taskShaderActive
        ? MeshDispatchLimits{ properties.maxTaskWorkGroupCount, properties.maxTaskWorkGroupTotalCount }
        : MeshDispatchLimits{ properties.maxMeshWorkGroupCount, properties.maxMeshWorkGroupTotalCount }
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

