// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


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
    u64* outRangeSize
){
    if(!IsTextureSliceInBounds(desc, slice)){
        if(outRowPitch)
            *outRowPitch = 0;
        if(outBufferRowLength)
            *outBufferRowLength = 0;
        if(outBufferImageHeight)
            *outBufferImageHeight = 0;
        if(outRangeSize)
            *outRangeSize = 0;
        return 0;
    }

    const TextureSlice resolved = slice.resolve(desc);
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);

    u64 offset = 0;
    bool found = false;
    for(u32 arr = 0; arr < desc.arraySize && !found; ++arr){
        for(u32 mip = 0; mip < desc.mipLevels && !found; ++mip){
            if(arr == resolved.arraySlice && mip == resolved.mipLevel){
                found = true;
                break;
            }

            const u32 mipWidth = Max<u32>(desc.width >> mip, 1u);
            const u32 mipHeight = Max<u32>(desc.height >> mip, 1u);
            const u32 mipDepth = Max<u32>(desc.depth >> mip, 1u);

            const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipWidth), static_cast<u64>(formatBlockWidth)), 1ull);
            const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipHeight), static_cast<u64>(formatBlockHeight)), 1ull);

            const u64 sliceSize = blocksX * blocksY * formatInfo.bytesPerBlock;
            offset = AlignUp(offset + sliceSize * mipDepth, s_BufferAlignmentBytes);
        }
    }

    const u32 mipWidth = Max<u32>(desc.width >> resolved.mipLevel, 1u);
    const u32 mipHeight = Max<u32>(desc.height >> resolved.mipLevel, 1u);

    const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipWidth), static_cast<u64>(formatBlockWidth)), 1ull);
    const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipHeight), static_cast<u64>(formatBlockHeight)), 1ull);

    const u64 rowPitch = static_cast<u64>(blocksX) * formatInfo.bytesPerBlock;
    const u64 slicePitch = rowPitch * blocksY;

    if(outRowPitch)
        *outRowPitch = static_cast<usize>(rowPitch);
    if(outBufferRowLength)
        *outBufferRowLength = static_cast<u32>(blocksX * formatBlockWidth);
    if(outBufferImageHeight)
        *outBufferImageHeight = static_cast<u32>(blocksY * formatBlockHeight);
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
            static_cast<u64>(resolved.depth - 1u) * slicePitch
            + (mappedBlocksY - 1u) * rowPitch
            + mappedBlocksX * formatInfo.bytesPerBlock
        ;
    }

    offset +=
        static_cast<u64>(resolved.z) * slicePitch
        + static_cast<u64>(resolved.y / formatBlockHeight) * rowPitch
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: dimensions, mip count, and array size must be nonzero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: dimensions, mip count, and array size must be nonzero"));
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
    if((d.dimension == TextureDimension::Texture1D || d.dimension == TextureDimension::Texture1DArray) && (d.height != 1 || d.depth != 1)){
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

    auto* staging = NewArenaObject<StagingTexture>(m_context.objectArena, m_context, m_allocator);
    staging->m_desc = d;
    staging->m_cpuAccess = cpuAccess;

    u64 totalSize = 0;
    for(u32 arraySlice = 0; arraySlice < d.arraySize; ++arraySlice){
        for(u32 mip = 0; mip < d.mipLevels; ++mip){
            auto mipWidth = Max<u32>(d.width >> mip, 1u);
            auto mipHeight = Max<u32>(d.height >> mip, 1u);
            auto mipDepth = Max<u32>(d.depth >> mip, 1u);

            const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipWidth), static_cast<u64>(formatBlockWidth)), 1ull);
            const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipHeight), static_cast<u64>(formatBlockHeight)), 1ull);
            if(blocksX > UINT64_MAX / blocksY || blocksX * formatBlockWidth > UINT32_MAX || blocksY * formatBlockHeight > UINT32_MAX){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: row layout exceeds Vulkan buffer image copy limits"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: row layout exceeds Vulkan buffer image copy limits"));
                DestroyArenaObject(m_context.objectArena, staging);
                return nullptr;
            }

            const u64 blockCount = blocksX * blocksY;
            if(blockCount > UINT64_MAX / formatInfo.bytesPerBlock){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: mip size overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: mip size overflows"));
                DestroyArenaObject(m_context.objectArena, staging);
                return nullptr;
            }
            const u64 sliceSize = blockCount * formatInfo.bytesPerBlock;
            if(mipDepth > UINT64_MAX / sliceSize || totalSize > UINT64_MAX - sliceSize * mipDepth){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: total size overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: total size overflows"));
                DestroyArenaObject(m_context.objectArena, staging);
                return nullptr;
            }
            const u64 mipSize = sliceSize * mipDepth;
            if(!AlignUpU64Checked(totalSize + mipSize, s_BufferAlignmentBytes, totalSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: total size alignment overflows"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: total size alignment overflows"));
                DestroyArenaObject(m_context.objectArena, staging);
                return nullptr;
            }
        }
    }

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

    auto* staging = static_cast<StagingTexture*>(tex);
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
    const u64 offset = VulkanDetail::ComputeStagingTextureOffset(
        staging->m_desc,
        slice,
        &rowPitch,
        nullptr,
        nullptr,
        &rangeSize
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

    auto* staging = static_cast<StagingTexture*>(tex);
    if(staging->m_mappedMemory && !staging->m_persistentlyMapped){
        staging->m_allocator.unmapStagingTextureMemory(*staging);
        staging->m_mappedMemory = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

