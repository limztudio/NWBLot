// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_staging_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StagingTextureMipLayout{
    u64 rowPitch = 0;
    u64 slicePitch = 0;
    u64 mipSize = 0;
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
};

inline void ClearStagingTextureLayoutOutputs(
    usize* outRowPitch,
    u32* outBufferRowLength,
    u32* outBufferImageHeight,
    u64* outRangeSize
){
    if(outRowPitch)
        *outRowPitch = 0;
    if(outBufferRowLength)
        *outBufferRowLength = 0;
    if(outBufferImageHeight)
        *outBufferImageHeight = 0;
    if(outRangeSize)
        *outRangeSize = 0;
}

inline bool BuildStagingTextureMipLayout(
    const TextureDesc& desc,
    const FormatInfo& formatInfo,
    const u32 formatBlockWidth,
    const u32 formatBlockHeight,
    const u32 mip,
    StagingTextureMipLayout& outLayout
){
    outLayout = {};

    const u32 mipWidth = Max<u32>(desc.width >> mip, 1u);
    const u32 mipHeight = Max<u32>(desc.height >> mip, 1u);
    const u32 mipDepth = desc.dimension == TextureDimension::Texture3D ? Max<u32>(desc.depth >> mip, 1u) : 1u;

    const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipWidth), static_cast<u64>(formatBlockWidth)), 1ull);
    const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipHeight), static_cast<u64>(formatBlockHeight)), 1ull);
    if(blocksX > UINT64_MAX / blocksY)
        return false;

    const u64 bufferRowLength = blocksX * formatBlockWidth;
    const u64 bufferImageHeight = blocksY * formatBlockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX)
        return false;

    const u64 blockCount = blocksX * blocksY;
    if(blockCount > UINT64_MAX / formatInfo.bytesPerBlock)
        return false;

    outLayout.rowPitch = blocksX * formatInfo.bytesPerBlock;
    outLayout.slicePitch = blockCount * formatInfo.bytesPerBlock;
    if(mipDepth > UINT64_MAX / outLayout.slicePitch)
        return false;

    outLayout.mipSize = outLayout.slicePitch * mipDepth;
    outLayout.bufferRowLength = static_cast<u32>(bufferRowLength);
    outLayout.bufferImageHeight = static_cast<u32>(bufferImageHeight);
    return true;
}

inline bool AddAlignedStagingMipSize(u64& size, const u64 mipSize){
    if(size > UINT64_MAX - mipSize)
        return false;
    return AlignUpU64Checked(size + mipSize, s_BufferAlignmentBytes, size);
}

inline bool BuildStagingTextureArrayLayout(
    const TextureDesc& desc,
    const FormatInfo& formatInfo,
    const u32 formatBlockWidth,
    const u32 formatBlockHeight,
    const u32 targetMip,
    u64& outArrayByteSize,
    u64* outTargetMipOffset,
    StagingTextureMipLayout* outTargetLayout
){
    outArrayByteSize = 0;
    if(outTargetMipOffset)
        *outTargetMipOffset = 0;
    if(outTargetLayout)
        *outTargetLayout = {};

    const bool needsTargetMip = outTargetMipOffset || outTargetLayout;
    bool foundTargetMip = !needsTargetMip;
    for(u32 mip = 0; mip < desc.mipLevels; ++mip){
        StagingTextureMipLayout layout;
        if(!BuildStagingTextureMipLayout(desc, formatInfo, formatBlockWidth, formatBlockHeight, mip, layout))
            return false;

        if(mip == targetMip){
            if(outTargetMipOffset)
                *outTargetMipOffset = outArrayByteSize;
            if(outTargetLayout)
                *outTargetLayout = layout;
            foundTargetMip = true;
        }

        if(!AddAlignedStagingMipSize(outArrayByteSize, layout.mipSize))
            return false;
    }

    return foundTargetMip;
}

inline bool BuildStagingTextureMipOffsetLayout(
    const TextureDesc& desc,
    const FormatInfo& formatInfo,
    const u32 formatBlockWidth,
    const u32 formatBlockHeight,
    const u32 targetMip,
    u64& outTargetMipOffset,
    StagingTextureMipLayout& outTargetLayout
){
    outTargetMipOffset = 0;
    outTargetLayout = {};

    for(u32 mip = 0; mip < desc.mipLevels; ++mip){
        StagingTextureMipLayout layout;
        if(!BuildStagingTextureMipLayout(desc, formatInfo, formatBlockWidth, formatBlockHeight, mip, layout))
            return false;

        if(mip == targetMip){
            outTargetLayout = layout;
            return true;
        }

        if(!AddAlignedStagingMipSize(outTargetMipOffset, layout.mipSize))
            return false;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsTextureSliceInBounds(const TextureDesc& desc, const TextureSlice& slice){
    if(desc.mipLevels == 0 || slice.mipLevel >= desc.mipLevels)
        return false;
    if(desc.arraySize == 0 || slice.arraySlice >= desc.arraySize)
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0)
        return false;

    const u32 mipWidth = Max<u32>(desc.width >> slice.mipLevel, 1u);
    const u32 mipHeight = Max<u32>(desc.height >> slice.mipLevel, 1u);
    const u32 mipDepth = desc.dimension == TextureDimension::Texture3D ? Max<u32>(desc.depth >> slice.mipLevel, 1u) : 1u;

    const TextureSlice resolved = slice.resolve(desc);
    if(resolved.width == 0 || resolved.height == 0 || resolved.depth == 0)
        return false;
    if(resolved.x > mipWidth || resolved.width > mipWidth - resolved.x)
        return false;
    if(resolved.y > mipHeight || resolved.height > mipHeight - resolved.y)
        return false;
    if(resolved.z > mipDepth || resolved.depth > mipDepth - resolved.z)
        return false;

    if((resolved.x % formatBlockWidth) != 0 || (resolved.y % formatBlockHeight) != 0)
        return false;
    if((resolved.width % formatBlockWidth) != 0 && resolved.x + resolved.width != mipWidth)
        return false;
    if((resolved.height % formatBlockHeight) != 0 && resolved.y + resolved.height != mipHeight)
        return false;

    return true;
}

u64 ComputeStagingTextureOffset(
    const TextureDesc& desc,
    const TextureSlice& slice,
    usize* outRowPitch,
    u32* outBufferRowLength,
    u32* outBufferImageHeight,
    u64* outRangeSize,
    u64 arrayByteSize
){
    if(!IsTextureSliceInBounds(desc, slice)){
        __hidden_vulkan_staging_texture::ClearStagingTextureLayoutOutputs(
            outRowPitch,
            outBufferRowLength,
            outBufferImageHeight,
            outRangeSize
        );
        return 0;
    }

    const TextureSlice resolved = slice.resolve(desc);
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);

    u64 offset = 0;
    __hidden_vulkan_staging_texture::StagingTextureMipLayout layout;
    bool layoutBuilt = false;
    if(arrayByteSize != 0){
        layoutBuilt = __hidden_vulkan_staging_texture::BuildStagingTextureMipOffsetLayout(
            desc,
            formatInfo,
            formatBlockWidth,
            formatBlockHeight,
            resolved.mipLevel,
            offset,
            layout
        );
    }
    else{
        layoutBuilt = __hidden_vulkan_staging_texture::BuildStagingTextureArrayLayout(
            desc,
            formatInfo,
            formatBlockWidth,
            formatBlockHeight,
            resolved.mipLevel,
            arrayByteSize,
            &offset,
            &layout
        );
    }

    if(!layoutBuilt){
        __hidden_vulkan_staging_texture::ClearStagingTextureLayoutOutputs(
            outRowPitch,
            outBufferRowLength,
            outBufferImageHeight,
            outRangeSize
        );
        return 0;
    }
    if(arrayByteSize != 0 && desc.arraySize > UINT64_MAX / arrayByteSize){
        __hidden_vulkan_staging_texture::ClearStagingTextureLayoutOutputs(
            outRowPitch,
            outBufferRowLength,
            outBufferImageHeight,
            outRangeSize
        );
        return 0;
    }

    offset += arrayByteSize * resolved.arraySlice;

    if(outRowPitch)
        *outRowPitch = static_cast<usize>(layout.rowPitch);
    if(outBufferRowLength)
        *outBufferRowLength = layout.bufferRowLength;
    if(outBufferImageHeight)
        *outBufferImageHeight = layout.bufferImageHeight;
    if(outRangeSize){
        const u64 mappedBlocksX = Max<u64>(
            DivideUp(static_cast<u64>(resolved.width), static_cast<u64>(formatBlockWidth)),
            1ull
        );
        const u64 mappedBlocksY = Max<u64>(
            DivideUp(static_cast<u64>(resolved.height), static_cast<u64>(formatBlockHeight)),
            1ull
        );
        *outRangeSize =
            static_cast<u64>(resolved.depth - 1u) * layout.slicePitch
            + (mappedBlocksY - 1u) * layout.rowPitch
            + mappedBlocksX * formatInfo.bytesPerBlock
        ;
    }

    offset +=
        static_cast<u64>(resolved.z) * layout.slicePitch
        + static_cast<u64>(resolved.y / formatBlockHeight) * layout.rowPitch
        + static_cast<u64>(resolved.x / formatBlockWidth) * formatInfo.bytesPerBlock
    ;

    return offset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    VkResult res = VK_SUCCESS;

    if(d.width == 0 || d.height == 0 || d.depth == 0 || d.mipLevels == 0 || d.arraySize == 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create staging texture: dimensions, mip count, and array size must be nonzero")
        );
        NWB_ASSERT_MSG(
            false,
            NWB_TEXT("Vulkan: Failed to create staging texture: dimensions, mip count, and array size must be nonzero")
        );
        return nullptr;
    }
    if(d.dimension == TextureDimension::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: texture dimension is unknown"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: texture dimension is unknown"));
        return nullptr;
    }
    if(d.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        return nullptr;
    }
    if(
        (d.dimension == TextureDimension::Texture1D || d.dimension == TextureDimension::Texture1DArray)
        && (d.height != 1 || d.depth != 1)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging 1D texture: height and depth must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging 1D texture: height and depth must be 1"));
        return nullptr;
    }
    if(d.dimension != TextureDimension::Texture3D && d.depth != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create non-3D staging texture: depth must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create non-3D staging texture: depth must be 1"));
        return nullptr;
    }
    if(d.dimension == TextureDimension::Texture3D && d.arraySize != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging 3D texture: array size must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging 3D texture: array size must be 1"));
        return nullptr;
    }

    const FormatInfo& formatInfo = GetFormatInfo(d.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        return nullptr;
    }

    u64 arrayByteSize = 0;
    const bool layoutBuilt = __hidden_vulkan_staging_texture::BuildStagingTextureArrayLayout(
        d,
        formatInfo,
        formatBlockWidth,
        formatBlockHeight,
        0,
        arrayByteSize,
        nullptr,
        nullptr
    );
    if(!layoutBuilt || (arrayByteSize != 0 && d.arraySize > UINT64_MAX / arrayByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return nullptr;
    }
    const u64 totalSize = arrayByteSize * d.arraySize;

    auto* staging = NewArenaObject<StagingTexture>(m_context.objectArena, m_context, m_allocator);
    staging->m_desc = d;
    staging->m_arrayByteSize = arrayByteSize;
    staging->m_cpuAccess = cpuAccess;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = m_allocator.createStagingTexture(*staging, bufferInfo, cpuAccess);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    return StagingTextureHandle(staging, StagingTextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void* Device::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch){
    VkResult res = VK_SUCCESS;

    if(!tex){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture is null"));
        return nullptr;
    }

    auto* staging = checked_cast<StagingTexture*>(tex);
    if(staging->m_cpuAccess == CpuAccessMode::None){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture was created without CPU access"));
        return nullptr;
    }
    if(!VulkanDetail::IsTextureSliceInBounds(staging->m_desc, slice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return nullptr;
    }

    if(!staging->m_mappedMemory){
        res = m_allocator.mapStagingTextureMemory(*staging, &staging->m_mappedMemory);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture for CPU access: {}"), ResultToString(res));
            return nullptr;
        }
    }

    usize rowPitch = 0;
    u64 rangeSize = 0;
    u64* outRangeSize = staging->m_cpuAccess == CpuAccessMode::Read ? &rangeSize : nullptr;
    const u64 offset = VulkanDetail::ComputeStagingTextureOffset(
        staging->m_desc,
        slice,
        &rowPitch,
        nullptr,
        nullptr,
        outRangeSize,
        staging->m_arrayByteSize
    );

    if(staging->m_cpuAccess == CpuAccessMode::Read){
        res = m_allocator.invalidateStagingTextureMemory(*staging, offset, rangeSize);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate staging texture mapping: {}"), ResultToString(res));
            return nullptr;
        }
    }

    if(outRowPitch)
        *outRowPitch = rowPitch;

    return static_cast<u8*>(staging->m_mappedMemory) + offset;
}

void Device::unmapStagingTexture(IStagingTexture* tex){
    if(!tex)
        return;

    auto* staging = checked_cast<StagingTexture*>(tex);
    if(staging->m_mappedMemory && !staging->m_persistentlyMapped){
        staging->m_allocator.unmapStagingTextureMemory(*staging);
        staging->m_mappedMemory = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

