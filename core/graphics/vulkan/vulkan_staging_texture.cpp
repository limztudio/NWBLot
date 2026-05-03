// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_staging_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool BuildStagingTextureMipLayout(
    const TextureDesc& desc,
    const VulkanDetail::TextureFormatBlockLayout& formatLayout,
    const u32 mip,
    VulkanDetail::StagingTextureMipLayout& outLayout,
    u64& outMipSize
){
    outLayout = {};
    outMipSize = 0;

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mip);

    const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipExtent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipExtent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);
    if(blocksX > UINT64_MAX / blocksY)
        return false;

    const u64 bufferRowLength = blocksX * formatLayout.blockWidth;
    const u64 bufferImageHeight = blocksY * formatLayout.blockHeight;
    if(bufferRowLength > UINT32_MAX || bufferImageHeight > UINT32_MAX)
        return false;

    const u64 blockCount = blocksX * blocksY;
    if(blockCount > UINT64_MAX / formatLayout.bytesPerBlock)
        return false;

    outLayout.rowPitch = blocksX * formatLayout.bytesPerBlock;
    outLayout.slicePitch = blockCount * formatLayout.bytesPerBlock;
    if(mipExtent.depth > UINT64_MAX / outLayout.slicePitch)
        return false;

    outMipSize = outLayout.slicePitch * mipExtent.depth;
    outLayout.bufferRowLength = static_cast<u32>(bufferRowLength);
    outLayout.bufferImageHeight = static_cast<u32>(bufferImageHeight);
    return true;
}

inline bool AddAlignedStagingMipSize(u64& size, const u64 mipSize){
    if(size > UINT64_MAX - mipSize)
        return false;
    return AlignUpU64Checked(size + mipSize, s_BufferAlignmentBytes, size);
}

inline bool BuildStagingTextureLayout(
    const TextureDesc& desc,
    const VulkanDetail::TextureFormatBlockLayout& formatLayout,
    u64& outArrayByteSize,
    VulkanDetail::StagingTextureMipLayoutVector& outMipLayouts
){
    outArrayByteSize = 0;
    outMipLayouts.clear();
    if(desc.mipLevels == 0)
        return false;

    outMipLayouts.reserve(desc.mipLevels);

    u64 arrayByteSize = 0;
    for(u32 mip = 0; mip < desc.mipLevels; ++mip){
        VulkanDetail::StagingTextureMipLayout layout;
        u64 mipSize = 0;
        if(!BuildStagingTextureMipLayout(desc, formatLayout, mip, layout, mipSize)){
            outMipLayouts.clear();
            return false;
        }

        layout.byteOffset = arrayByteSize;
        outMipLayouts.push_back(layout);

        if(!AddAlignedStagingMipSize(arrayByteSize, mipSize)){
            outMipLayouts.clear();
            return false;
        }
    }

    outArrayByteSize = arrayByteSize;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsTextureSliceInBounds(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout, TextureSlice* outResolved){
    if(desc.mipLevels == 0 || slice.mipLevel >= desc.mipLevels)
        return false;
    if(desc.arraySize == 0 || slice.arraySlice >= desc.arraySize)
        return false;
    if(formatLayout.blockWidth == 0 || formatLayout.blockHeight == 0 || formatLayout.bytesPerBlock == 0)
        return false;

    const VkExtent3D mipExtent = GetTextureMipExtent(desc, slice.mipLevel);
    const TextureSlice resolved = slice.resolve(mipExtent.width, mipExtent.height, mipExtent.depth);
    if(resolved.width == 0 || resolved.height == 0 || resolved.depth == 0)
        return false;
    if(resolved.x > mipExtent.width || resolved.width > mipExtent.width - resolved.x)
        return false;
    if(resolved.y > mipExtent.height || resolved.height > mipExtent.height - resolved.y)
        return false;
    if(resolved.z > mipExtent.depth || resolved.depth > mipExtent.depth - resolved.z)
        return false;

    if((resolved.x % formatLayout.blockWidth) != 0 || (resolved.y % formatLayout.blockHeight) != 0)
        return false;
    if((resolved.width % formatLayout.blockWidth) != 0 && resolved.x + resolved.width != mipExtent.width)
        return false;
    if((resolved.height % formatLayout.blockHeight) != 0 && resolved.y + resolved.height != mipExtent.height)
        return false;

    if(outResolved)
        *outResolved = resolved;
    return true;
}

u64 ComputeStagingTextureOffset(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    const u64 arrayByteSize,
    usize* outRowPitch,
    u32* outBufferRowLength,
    u32* outBufferImageHeight,
    u64* outRangeSize
){
    u64 offset = mipLayout.byteOffset;
    offset += arrayByteSize * resolvedSlice.arraySlice;

    if(outRowPitch)
        *outRowPitch = static_cast<usize>(mipLayout.rowPitch);
    if(outBufferRowLength)
        *outBufferRowLength = mipLayout.bufferRowLength;
    if(outBufferImageHeight)
        *outBufferImageHeight = mipLayout.bufferImageHeight;
    if(outRangeSize){
        const u64 mappedBlocksX = Max<u64>(
            DivideUp(static_cast<u64>(resolvedSlice.width), static_cast<u64>(formatLayout.blockWidth)),
            1ull
        );
        const u64 mappedBlocksY = Max<u64>(
            DivideUp(static_cast<u64>(resolvedSlice.height), static_cast<u64>(formatLayout.blockHeight)),
            1ull
        );
        *outRangeSize =
            static_cast<u64>(resolvedSlice.depth - 1u) * mipLayout.slicePitch
            + (mappedBlocksY - 1u) * mipLayout.rowPitch
            + mappedBlocksX * formatLayout.bytesPerBlock
        ;
    }

    offset +=
        static_cast<u64>(resolvedSlice.z) * mipLayout.slicePitch
        + static_cast<u64>(resolvedSlice.y / formatLayout.blockHeight) * mipLayout.rowPitch
        + static_cast<u64>(resolvedSlice.x / formatLayout.blockWidth) * formatLayout.bytesPerBlock
    ;

    return offset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    if(!VulkanDetail::ValidateTextureShape(d, NWB_TEXT("create staging texture"))){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture shape"));
        return nullptr;
    }
    if(d.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        return nullptr;
    }

    const FormatInfo& formatInfo = GetFormatInfo(d.format);
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    if(!VulkanDetail::GetTextureFormatBlockLayout(formatInfo, formatLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        return nullptr;
    }

    auto* staging = NewArenaObject<StagingTexture>(m_context.objectArena, m_context, m_allocator);
    staging->m_desc = d;
    staging->m_formatLayout = formatLayout;
    staging->m_aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    staging->m_cpuAccess = cpuAccess;

    u64 arrayByteSize = 0;
    const bool layoutBuilt = __hidden_vulkan_staging_texture::BuildStagingTextureLayout(
        d,
        staging->m_formatLayout,
        arrayByteSize,
        staging->m_mipLayouts
    );
    if(!layoutBuilt || (arrayByteSize != 0 && d.arraySize > UINT64_MAX / arrayByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }
    const u64 totalSize = arrayByteSize * d.arraySize;

    staging->m_arrayByteSize = arrayByteSize;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VkResult res = m_allocator.createStagingTexture(*staging, bufferInfo, cpuAccess);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    return StagingTextureHandle(staging, StagingTextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void* Device::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch){
    if(!tex){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture is null"));
        return nullptr;
    }

    auto* staging = checked_cast<StagingTexture*>(tex);
    if(staging->m_cpuAccess == CpuAccessMode::None){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture was created without CPU access"));
        return nullptr;
    }
    TextureSlice resolvedSlice;
    if(!VulkanDetail::IsTextureSliceInBounds(staging->m_desc, slice, staging->m_formatLayout, &resolvedSlice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return nullptr;
    }

    if(!staging->m_mappedMemory){
        const VkResult res = m_allocator.mapStagingTextureMemory(*staging, &staging->m_mappedMemory);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture for CPU access: {}"), ResultToString(res));
            return nullptr;
        }
    }

    usize rowPitch = 0;
    u64 rangeSize = 0;
    const bool needsInvalidate = staging->m_cpuAccess == CpuAccessMode::Read && staging->m_requiresInvalidate;
    u64* outRangeSize = needsInvalidate ? &rangeSize : nullptr;
    const u64 offset = VulkanDetail::ComputeStagingTextureOffset(
        resolvedSlice,
        staging->m_mipLayouts[resolvedSlice.mipLevel],
        staging->m_formatLayout,
        staging->m_arrayByteSize,
        &rowPitch,
        nullptr,
        nullptr,
        outRangeSize
    );

    if(needsInvalidate){
        const VkResult res = m_allocator.invalidateStagingTextureMemory(*staging, offset, rangeSize);
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

