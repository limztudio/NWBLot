// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 AlignBufferOffset(u64 off){
    return ((off + (s_BufferAlignmentBytes - 1)) / s_BufferAlignmentBytes) * s_BufferAlignmentBytes;
}

bool AlignBufferOffsetChecked(u64 offset, u64& alignedOffset){
    if(offset > UINT64_MAX - (s_BufferAlignmentBytes - 1))
        return false;

    alignedOffset = AlignBufferOffset(offset);
    return true;
}

bool IsTextureSliceInBounds(const TextureDesc& desc, const TextureSlice& slice){
    if(desc.mipLevels == 0 || slice.mipLevel >= desc.mipLevels)
        return false;
    if(desc.arraySize == 0 || slice.arraySlice >= desc.arraySize)
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    if(formatInfo.blockSize == 0 || formatInfo.bytesPerBlock == 0)
        return false;

    const u32 mipWidth = Max<u32>(desc.width >> slice.mipLevel, 1u);
    const u32 mipHeight = Max<u32>(desc.height >> slice.mipLevel, 1u);
    const u32 mipDepth = desc.dimension == TextureDimension::Texture3D
        ? Max<u32>(desc.depth >> slice.mipLevel, 1u)
        : 1u;

    const TextureSlice resolved = slice.resolve(desc);
    if(resolved.width == 0 || resolved.height == 0 || resolved.depth == 0)
        return false;
    if(resolved.x > mipWidth || resolved.width > mipWidth - resolved.x)
        return false;
    if(resolved.y > mipHeight || resolved.height > mipHeight - resolved.y)
        return false;
    if(resolved.z > mipDepth || resolved.depth > mipDepth - resolved.z)
        return false;

    const u32 blockSize = formatInfo.blockSize;
    if((resolved.x % blockSize) != 0 || (resolved.y % blockSize) != 0)
        return false;
    if((resolved.width % blockSize) != 0 && resolved.x + resolved.width != mipWidth)
        return false;
    if((resolved.height % blockSize) != 0 && resolved.y + resolved.height != mipHeight)
        return false;

    return true;
}

u64 ComputeStagingTextureOffset(const TextureDesc& desc, const TextureSlice& slice, usize* outRowPitch, u32* outBufferRowLength, u32* outBufferImageHeight){
    if(!IsTextureSliceInBounds(desc, slice)){
        if(outRowPitch)
            *outRowPitch = 0;
        if(outBufferRowLength)
            *outBufferRowLength = 0;
        if(outBufferImageHeight)
            *outBufferImageHeight = 0;
        return 0;
    }

    const TextureSlice resolved = slice.resolve(desc);
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);

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

            const u64 blocksX = Max<u64>((static_cast<u64>(mipWidth) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);
            const u64 blocksY = Max<u64>((static_cast<u64>(mipHeight) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);

            const u64 sliceSize = blocksX * blocksY * formatInfo.bytesPerBlock;
            offset = AlignBufferOffset(offset + sliceSize * mipDepth);
        }
    }

    const u32 mipWidth = Max<u32>(desc.width >> resolved.mipLevel, 1u);
    const u32 mipHeight = Max<u32>(desc.height >> resolved.mipLevel, 1u);

    const u64 blocksX = Max<u64>((static_cast<u64>(mipWidth) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);
    const u64 blocksY = Max<u64>((static_cast<u64>(mipHeight) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);

    const u64 rowPitch = static_cast<u64>(blocksX) * formatInfo.bytesPerBlock;
    const u64 slicePitch = rowPitch * blocksY;

    if(outRowPitch)
        *outRowPitch = static_cast<usize>(rowPitch);
    if(outBufferRowLength)
        *outBufferRowLength = static_cast<u32>(blocksX * formatInfo.blockSize);
    if(outBufferImageHeight)
        *outBufferImageHeight = static_cast<u32>(blocksY * formatInfo.blockSize);

    offset += static_cast<u64>(resolved.z) * slicePitch
            + static_cast<u64>(resolved.y / formatInfo.blockSize) * rowPitch
            + static_cast<u64>(resolved.x / formatInfo.blockSize) * formatInfo.bytesPerBlock
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
    if(formatInfo.blockSize == 0 || formatInfo.bytesPerBlock == 0){
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

            const u64 blocksX = Max<u64>((static_cast<u64>(mipWidth) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);
            const u64 blocksY = Max<u64>((static_cast<u64>(mipHeight) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1ull);
            if(blocksX > UINT64_MAX / blocksY || blocksX * formatInfo.blockSize > UINT32_MAX || blocksY * formatInfo.blockSize > UINT32_MAX){
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
            if(!__hidden_vulkan::AlignBufferOffsetChecked(totalSize + mipSize, totalSize)){
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

    res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &staging->m_buffer);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, staging->m_buffer, &memRequirements);

    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if(cpuAccess == CpuAccessMode::Read)
        memProps |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    bool foundMemoryType = false;
    u32 memoryTypeIndex = 0;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((memRequirements.memoryTypeBits & (1u << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & memProps) == memProps){
            memoryTypeIndex = i;
            foundMemoryType = true;
            break;
        }
    }

    // Fallback: try without HOST_CACHED if Read access was requested
    if(!foundMemoryType && cpuAccess == CpuAccessMode::Read){
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
            if((memRequirements.memoryTypeBits & (1u << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & memProps) == memProps){
                memoryTypeIndex = i;
                foundMemoryType = true;
                break;
            }
        }
    }

    if(!foundMemoryType){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to find host-visible memory for staging texture"));
        vkDestroyBuffer(m_context.device, staging->m_buffer, m_context.allocationCallbacks);
        staging->m_buffer = VK_NULL_HANDLE;
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &staging->m_memory);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate staging texture memory: {}"), ResultToString(res));
        vkDestroyBuffer(m_context.device, staging->m_buffer, m_context.allocationCallbacks);
        staging->m_buffer = VK_NULL_HANDLE;
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    res = vkBindBufferMemory(m_context.device, staging->m_buffer, staging->m_memory, 0);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind staging texture memory: {}"), ResultToString(res));
        vkFreeMemory(m_context.device, staging->m_memory, m_context.allocationCallbacks);
        vkDestroyBuffer(m_context.device, staging->m_buffer, m_context.allocationCallbacks);
        staging->m_memory = VK_NULL_HANDLE;
        staging->m_buffer = VK_NULL_HANDLE;
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    if(cpuAccess != CpuAccessMode::None){
        res = vkMapMemory(m_context.device, staging->m_memory, 0, totalSize, 0, &staging->m_mappedMemory);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture memory: {}"), ResultToString(res));
            vkFreeMemory(m_context.device, staging->m_memory, m_context.allocationCallbacks);
            vkDestroyBuffer(m_context.device, staging->m_buffer, m_context.allocationCallbacks);
            staging->m_memory = VK_NULL_HANDLE;
            staging->m_buffer = VK_NULL_HANDLE;
            DestroyArenaObject(m_context.objectArena, staging);
            return nullptr;
        }
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
    if(!__hidden_vulkan::IsTextureSliceInBounds(staging->m_desc, slice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return nullptr;
    }

    if(!staging->m_mappedMemory){
        res = vkMapMemory(m_context.device, staging->m_memory, 0, VK_WHOLE_SIZE, 0, &staging->m_mappedMemory);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture for CPU access: {}"), ResultToString(res));
            return nullptr;
        }
    }

    usize rowPitch = 0;
    const u64 offset = __hidden_vulkan::ComputeStagingTextureOffset(staging->m_desc, slice, &rowPitch);
    if(outRowPitch)
        *outRowPitch = rowPitch;

    return static_cast<u8*>(staging->m_mappedMemory) + offset;
}

void Device::unmapStagingTexture(IStagingTexture* tex){
    // Memory is persistently mapped; no-op.
    // Unmapping is handled in StagingTexture destructor.
    (void)tex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
