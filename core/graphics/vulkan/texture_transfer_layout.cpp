// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_transfer_layout.h"

#include <global/algorithm.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace __hidden_texture_transfer_layout{


struct StagingTextureMip{
    VulkanDetail::StagingTextureMipLayout layout;
    u64 byteSize = 0u;
};

inline Expected<StagingTextureMip> BuildStagingTextureMipLayout(const TextureDesc& desc, const VulkanDetail::TextureFormatBlockLayout& formatLayout, const u32 mip)noexcept{
    StagingTextureMip result{};

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mip);

    const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipExtent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipExtent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);

    const u64 bufferRowLength = blocksX * formatLayout.blockWidth;
    const u64 bufferImageHeight = blocksY * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX)
        return MakeUnexpected(Failure{});

    const u64 blockCount = blocksX * blocksY;
    if(blockCount > UINT64_MAX / formatLayout.bytesPerBlock)
        return MakeUnexpected(Failure{});

    result.layout.rowPitch = blocksX * formatLayout.bytesPerBlock;
    result.layout.slicePitch = blockCount * formatLayout.bytesPerBlock;
    if(mipExtent.depth > UINT64_MAX / result.layout.slicePitch)
        return MakeUnexpected(Failure{});

    result.byteSize = result.layout.slicePitch * mipExtent.depth;
    result.layout.bufferRowLength = static_cast<u32>(bufferRowLength);
    result.layout.bufferImageHeight = static_cast<u32>(bufferImageHeight);
    return result;
}

inline Expected<u64> AddAlignedStagingMipSize(const u64 size, const u64 mipSize, const u32 alignment)noexcept{
    const auto total = AddNoOverflow(size, mipSize);
    if(!total)
        return MakeUnexpected(total.error());
    return AlignUpChecked<u64>(*total, static_cast<u64>(alignment));
}

};


Expected<VulkanDetail::TextureTransferLayout::StagingLayout> VulkanDetail::TextureTransferLayout::buildStagingLayout(Alloc::GlobalArena& arena, const TextureDesc& desc, const VulkanDetail::TextureFormatBlockLayout& formatLayout, const u32 bufferOffsetAlignment){
    NWB_ASSERT(desc.mipLevels != 0u);
    StagingLayout result{ 0u, VulkanDetail::StagingTextureMipLayoutVector(arena) };
    result.mipLayouts.reserve(desc.mipLevels);
    for(u32 mip = 0u; mip < desc.mipLevels; ++mip){
        auto layout = __hidden_texture_transfer_layout::BuildStagingTextureMipLayout(desc, formatLayout, mip);
        if(!layout)
            return MakeUnexpected(layout.error());
        layout->layout.byteOffset = result.arrayByteSize;
        result.mipLayouts.push_back(layout->layout);
        const auto size = __hidden_texture_transfer_layout::AddAlignedStagingMipSize(result.arrayByteSize, layout->byteSize, bufferOffsetAlignment);
        if(!size)
            return MakeUnexpected(size.error());
        result.arrayByteSize = *size;
    }
    return result;
}

namespace VulkanDetail{


Expected<BufferImageCopyLayout> TextureTransferLayout::buildBufferImageCopyLayout(
    const VkExtent3D& extent,
    const TextureFormatBlockLayout& formatLayout,
    const u64 rowPitch,
    const u64 depthPitch,
    const BufferImageCopyRequiredSize::Enum requiredSizeMode,
    const BufferImageCopyPitchFields::Enum pitchFields,
    TStringView operationName
){
    BufferImageCopyLayout outLayout{};
    NWB_ASSERT(formatLayout.blockWidth != 0u && formatLayout.blockHeight != 0u && formatLayout.bytesPerBlock != 0u);

    const u64 blockCountX = Max<u64>(DivideUp(static_cast<u64>(extent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blockCountY = Max<u64>(DivideUp(static_cast<u64>(extent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);

    const u64 naturalRowPitch = blockCountX * formatLayout.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0 ? rowPitch : naturalRowPitch;
    if(blockCountY > UINT64_MAX / effectiveRowPitch){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture pitch size overflows"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatLayout.bytesPerBlock) != 0){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid row pitch"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0 ? depthPitch : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid depth pitch"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 bufferRowBlocks = effectiveRowPitch / formatLayout.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(bufferRowBlocks > UINT64_MAX / formatLayout.blockWidth || bufferImageBlocks > UINT64_MAX / formatLayout.blockHeight){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return MakeUnexpected(Failure{});
    }

    const u64 bufferRowLength = bufferRowBlocks * formatLayout.blockWidth;
    const u64 bufferImageHeight = bufferImageBlocks * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX){
        if(!operationName.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: row pitch or depth pitch exceeds Vulkan buffer image copy limits"), operationName);
        return MakeUnexpected(Failure{});
    }

    if(requiredSizeMode == BufferImageCopyRequiredSize::PaddedSlices){
        const auto requiredSize = ComputeStridedRangeByteSize(Max<u32>(extent.depth, 1u), effectiveDepthPitch, packedSlicePitch);
        if(!requiredSize){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        outLayout.requiredSize = *requiredSize;
    }
    else{
        const u64 depthOffset = static_cast<u64>(extent.depth - 1);
        if(depthOffset > UINT64_MAX / effectiveDepthPitch){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        const u64 depthBytes = depthOffset * effectiveDepthPitch;
        const u64 rowBytes = static_cast<u64>(blockCountY - 1) * effectiveRowPitch;
        if(depthBytes > UINT64_MAX - rowBytes || depthBytes + rowBytes > UINT64_MAX - naturalRowPitch){
            if(!operationName.empty())
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: transfer size overflows"), operationName);
            return MakeUnexpected(Failure{});
        }
        outLayout.requiredSize = depthBytes + rowBytes + naturalRowPitch;
    }

    outLayout.bufferRowLength = pitchFields == BufferImageCopyPitchFields::EmitExplicit || rowPitch != 0 ? static_cast<u32>(bufferRowLength) : 0u;
    outLayout.bufferImageHeight = pitchFields == BufferImageCopyPitchFields::EmitExplicit || depthPitch != 0 ? static_cast<u32>(bufferImageHeight) : 0u;
    return outLayout;
}

Expected<TextureSlice> TextureTransferLayout::resolveSlice(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout)noexcept{
    if(desc.mipLevels == 0 || slice.mipLevel >= desc.mipLevels)
        return MakeUnexpected(Failure{});
    if(desc.arraySize == 0 || slice.arraySlice >= desc.arraySize)
        return MakeUnexpected(Failure{});
    if(formatLayout.blockWidth == 0 || formatLayout.blockHeight == 0 || formatLayout.bytesPerBlock == 0)
        return MakeUnexpected(Failure{});

    const VkExtent3D mipExtent = GetTextureMipExtent(desc, slice.mipLevel);
    const TextureSlice resolved = slice.resolve(mipExtent.width, mipExtent.height, mipExtent.depth);
    if(resolved.width == 0 || resolved.height == 0 || resolved.depth == 0)
        return MakeUnexpected(Failure{});
    if(resolved.x > mipExtent.width || resolved.width > mipExtent.width - resolved.x)
        return MakeUnexpected(Failure{});
    if(resolved.y > mipExtent.height || resolved.height > mipExtent.height - resolved.y)
        return MakeUnexpected(Failure{});
    if(resolved.z > mipExtent.depth || resolved.depth > mipExtent.depth - resolved.z)
        return MakeUnexpected(Failure{});

    if((resolved.x % formatLayout.blockWidth) != 0 || (resolved.y % formatLayout.blockHeight) != 0)
        return MakeUnexpected(Failure{});
    if((resolved.width % formatLayout.blockWidth) != 0 && resolved.x + resolved.width != mipExtent.width)
        return MakeUnexpected(Failure{});
    if((resolved.height % formatLayout.blockHeight) != 0 && resolved.y + resolved.height != mipExtent.height)
        return MakeUnexpected(Failure{});

    return resolved;
}

Expected<StagingTextureRange> TextureTransferLayout::buildStagingRange(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    const u64 arrayByteSize,
    const u64 totalByteSize,
    const u32 requiredOffsetAlignment,
    const b8 requireHostPointerRange
)noexcept{
    if(
        resolvedSlice.width == 0u
        || resolvedSlice.height == 0u
        || resolvedSlice.depth == 0u
        || formatLayout.blockWidth == 0u
        || formatLayout.blockHeight == 0u
        || formatLayout.bytesPerBlock == 0u
        || mipLayout.rowPitch == 0u
        || mipLayout.slicePitch == 0u
        || mipLayout.bufferRowLength == 0u
        || mipLayout.bufferImageHeight == 0u
        || arrayByteSize == 0u
        || totalByteSize == 0u
        || arrayByteSize > totalByteSize
        || mipLayout.byteOffset >= arrayByteSize
        || (resolvedSlice.x % formatLayout.blockWidth) != 0u
        || (resolvedSlice.y % formatLayout.blockHeight) != 0u
    )
        return MakeUnexpected(Failure{});

    const auto arrayOffset = TryMultiply<u64>(arrayByteSize, static_cast<u64>(resolvedSlice.arraySlice));
    const auto depthOffset = TryMultiply<u64>(mipLayout.slicePitch, static_cast<u64>(resolvedSlice.z));
    const auto rowOffset = TryMultiply<u64>(mipLayout.rowPitch, static_cast<u64>(resolvedSlice.y / formatLayout.blockHeight));
    const auto columnOffset = TryMultiply<u64>(formatLayout.bytesPerBlock, static_cast<u64>(resolvedSlice.x / formatLayout.blockWidth));
    if(!arrayOffset || !depthOffset || !rowOffset || !columnOffset)
        return MakeUnexpected(Failure{});
    auto byteOffset = AddNoOverflow(mipLayout.byteOffset, *arrayOffset);
    if(!byteOffset)
        return MakeUnexpected(byteOffset.error());
    byteOffset = AddNoOverflow(*byteOffset, *depthOffset);
    if(!byteOffset)
        return MakeUnexpected(byteOffset.error());
    byteOffset = AddNoOverflow(*byteOffset, *rowOffset);
    if(!byteOffset)
        return MakeUnexpected(byteOffset.error());
    byteOffset = AddNoOverflow(*byteOffset, *columnOffset);
    if(!byteOffset)
        return MakeUnexpected(byteOffset.error());

    const u64 mappedBlocksX = Max<u64>(DivideUp(static_cast<u64>(resolvedSlice.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 mappedBlocksY = Max<u64>(DivideUp(static_cast<u64>(resolvedSlice.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);
    const auto depthSize = TryMultiply<u64>(static_cast<u64>(resolvedSlice.depth - 1u), mipLayout.slicePitch);
    const auto rowSize = TryMultiply<u64>(mappedBlocksY - 1u, mipLayout.rowPitch);
    const auto columnSize = TryMultiply<u64>(mappedBlocksX, static_cast<u64>(formatLayout.bytesPerBlock));
    if(!depthSize || !rowSize || !columnSize)
        return MakeUnexpected(Failure{});
    auto byteSize = AddNoOverflow(*depthSize, *rowSize);
    if(!byteSize)
        return MakeUnexpected(byteSize.error());
    byteSize = AddNoOverflow(*byteSize, *columnSize);
    if(!byteSize || *byteSize == 0u || *byteOffset > totalByteSize || *byteSize > totalByteSize - *byteOffset || (requiredOffsetAlignment != 0u && (*byteOffset % requiredOffsetAlignment) != 0u))
        return MakeUnexpected(Failure{});

    const u64 maximumHostRange = static_cast<u64>(Limit<usize>::s_Max);
    if(requireHostPointerRange && (*byteOffset > maximumHostRange || *byteSize > maximumHostRange - *byteOffset || mipLayout.rowPitch > maximumHostRange))
        return MakeUnexpected(Failure{});

    return StagingTextureRange{ *byteOffset, *byteSize, mipLayout.rowPitch, mipLayout.bufferRowLength, mipLayout.bufferImageHeight };
}

};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
