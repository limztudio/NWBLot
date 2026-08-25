// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

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
    if(cpuAccess != CpuAccessMode::Read && cpuAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: CPU access must be Read or Write"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid CPU access"));
        return nullptr;
    }

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
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(d.queueSharing, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

    const VkResult res = m_allocator.createStagingTexture(*staging, bufferInfo, cpuAccess);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }
#if !defined(NWB_FINAL)
    staging->m_nativeQueueFamilySharingForTesting = sharingInfo;
#endif

    return StagingTextureHandle(staging, StagingTextureHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void* Device::mapStagingTexture(
    StagingTexture* textureResource,
    const TextureSlice& slice,
    const CpuAccessMode::Enum requestedAccess,
    usize* outRowPitch
){
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture is null"));
        return nullptr;
    }
    if(requestedAccess != CpuAccessMode::Read && requestedAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: invalid CPU access mode"));
        return nullptr;
    }

    StagingTexture& staging = *textureResource;
    if(&staging.m_context != &m_context || &staging.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture belongs to another device"));
        return nullptr;
    }
    if(staging.m_buffer == VK_NULL_HANDLE || !staging.m_allocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: native buffer or allocation is null"));
        return nullptr;
    }
    if(staging.m_cpuAccess != CpuAccessMode::Read && staging.m_cpuAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture was created without valid CPU access"));
        return nullptr;
    }
    if(requestedAccess != staging.m_cpuAccess){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to map staging texture: requested access does not match the texture CPU access")
        );
        return nullptr;
    }

    TextureSlice resolvedSlice;
    if(!VulkanDetail::IsTextureSliceInBounds(staging.m_desc, slice, staging.m_formatLayout, &resolvedSlice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return nullptr;
    }

    usize rowPitch = 0;
    u64 rangeSize = 0;
    u64* const outRangeSize = requestedAccess == CpuAccessMode::Read ? &rangeSize : nullptr;
    const u64 offset = VulkanDetail::ComputeStagingTextureOffset(
        resolvedSlice,
        staging.m_mipLayouts[resolvedSlice.mipLevel],
        staging.m_formatLayout,
        staging.m_arrayByteSize,
        &rowPitch,
        nullptr,
        nullptr,
        outRangeSize
    );

    ScopedLock lock(staging.m_mappingMutex);
    if(staging.m_persistentlyMapped && !staging.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: persistent mapping pointer is null"));
        return nullptr;
    }

#if !defined(NWB_FINAL)
    const bool rejectInvalidate = requestedAccess == CpuAccessMode::Read && staging.m_rejectNextInvalidateForTesting;
    if(rejectInvalidate)
        staging.m_rejectNextInvalidateForTesting = false;
#else
    constexpr bool rejectInvalidate = false;
#endif

    void* mappedMemory = staging.m_mappedMemory;
    bool mappedThisCall = false;
    if(!mappedMemory || rejectInvalidate){
        void* newMappedMemory = nullptr;
        const VkResult res = m_allocator.mapStagingTextureMemory(staging, &newMappedMemory);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to map staging texture for CPU access: {}"),
                ResultToString(res)
            );
            return nullptr;
        }
        mappedThisCall = true;
        if(!newMappedMemory){
            m_allocator.unmapStagingTextureMemory(staging);
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mapped pointer is null"));
            return nullptr;
        }
        if(!mappedMemory)
            mappedMemory = newMappedMemory;
    }

    const bool needsInvalidate = requestedAccess == CpuAccessMode::Read && (staging.m_requiresInvalidate || rejectInvalidate);
    if(needsInvalidate){
        const VkResult res = rejectInvalidate
            ? VK_ERROR_MEMORY_MAP_FAILED
            : m_allocator.invalidateStagingTextureMemory(staging, offset, rangeSize)
        ;
        if(res != VK_SUCCESS){
            if(mappedThisCall)
                m_allocator.unmapStagingTextureMemory(staging);
            if(rejectInvalidate)
                return nullptr;
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate staging texture mapping: {}"), ResultToString(res));
            return nullptr;
        }
    }

    if(mappedThisCall)
        staging.m_mappedMemory = mappedMemory;
    if(outRowPitch)
        *outRowPitch = rowPitch;

    return static_cast<u8*>(mappedMemory) + offset;
}

void Device::unmapStagingTexture(StagingTexture* textureResource){
    if(!textureResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap staging texture: texture is null"));
        return;
    }

    StagingTexture& staging = *textureResource;
    if(&staging.m_context != &m_context || &staging.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap staging texture: texture belongs to another device"));
        return;
    }
    if(staging.m_buffer == VK_NULL_HANDLE || !staging.m_allocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap staging texture: native buffer or allocation is null"));
        return;
    }
    if(staging.m_cpuAccess != CpuAccessMode::Read && staging.m_cpuAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap staging texture: texture has invalid CPU access"));
        return;
    }

    ScopedLock lock(staging.m_mappingMutex);
    if(!staging.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unmap staging texture: texture is not mapped"));
        return;
    }
    if(staging.m_persistentlyMapped)
        return;

    m_allocator.unmapStagingTextureMemory(staging);
    staging.m_mappedMemory = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

