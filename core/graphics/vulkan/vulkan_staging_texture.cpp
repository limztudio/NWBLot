// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 AlignBufferOffset(u64 off){
    return ((off + (s_BufferAlignmentBytes - 1)) / s_BufferAlignmentBytes) * s_BufferAlignmentBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    VkResult res = VK_SUCCESS;

    auto* staging = new StagingTexture(m_context, m_allocator);
    staging->desc = d;
    staging->cpuAccess = cpuAccess;

    const FormatInfo& formatInfo = GetFormatInfo(d.format);

    u64 totalSize = 0;
    for(u32 arraySlice = 0; arraySlice < d.arraySize; ++arraySlice){
        for(u32 mip = 0; mip < d.mipLevels; ++mip){
            auto mipWidth = Max<u32>(d.width >> mip, 1u);
            auto mipHeight = Max<u32>(d.height >> mip, 1u);
            auto mipDepth = Max<u32>(d.depth >> mip, 1u);

            auto blocksX = Max<u32>((mipWidth + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
            auto blocksY = Max<u32>((mipHeight + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);

            auto sliceSize = static_cast<u64>(blocksX) * blocksY * formatInfo.bytesPerBlock;
            totalSize = __hidden_vulkan::AlignBufferOffset(totalSize + sliceSize * mipDepth);
        }
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &staging->buffer);
    if(res != VK_SUCCESS){
        delete staging;
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, staging->buffer, &memRequirements);

    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if(cpuAccess == CpuAccessMode::Read)
        memProps |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    bool foundMemoryType = false;
    u32 memoryTypeIndex = 0;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((memRequirements.memoryTypeBits & (1 << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & memProps) == memProps){
            memoryTypeIndex = i;
            foundMemoryType = true;
            break;
        }
    }

    // Fallback: try without HOST_CACHED if Read access was requested
    if(!foundMemoryType && cpuAccess == CpuAccessMode::Read){
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
            if((memRequirements.memoryTypeBits & (1 << i)) && (m_context.memoryProperties.memoryTypes[i].propertyFlags & memProps) == memProps){
                memoryTypeIndex = i;
                foundMemoryType = true;
                break;
            }
        }
    }

    if(!foundMemoryType){
        vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
        staging->buffer = VK_NULL_HANDLE;
        delete staging;
        return nullptr;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &staging->memory);
    if(res != VK_SUCCESS){
        vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
        staging->buffer = VK_NULL_HANDLE;
        delete staging;
        return nullptr;
    }

    res = vkBindBufferMemory(m_context.device, staging->buffer, staging->memory, 0);
    if(res != VK_SUCCESS){
        vkFreeMemory(m_context.device, staging->memory, m_context.allocationCallbacks);
        vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
        staging->memory = VK_NULL_HANDLE;
        staging->buffer = VK_NULL_HANDLE;
        delete staging;
        return nullptr;
    }

    if(cpuAccess != CpuAccessMode::None){
        res = vkMapMemory(m_context.device, staging->memory, 0, totalSize, 0, &staging->mappedMemory);
        if(res != VK_SUCCESS){
            vkFreeMemory(m_context.device, staging->memory, m_context.allocationCallbacks);
            vkDestroyBuffer(m_context.device, staging->buffer, m_context.allocationCallbacks);
            staging->memory = VK_NULL_HANDLE;
            staging->buffer = VK_NULL_HANDLE;
            delete staging;
            return nullptr;
        }
    }

    return StagingTextureHandle(staging, AdoptRef);
}

void* Device::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch){
    VkResult res = VK_SUCCESS;

    if(!tex)
        return nullptr;

    auto* staging = static_cast<StagingTexture*>(tex);

    if(!staging->mappedMemory){
        res = vkMapMemory(m_context.device, staging->memory, 0, VK_WHOLE_SIZE, 0, &staging->mappedMemory);
        if(res != VK_SUCCESS)
            return nullptr;
    }

    const TextureDesc& desc = staging->desc;
    const TextureSlice resolved = slice.resolve(desc);
    const FormatInfo& formatInfo = GetFormatInfo(desc.format);

    // Compute byte offset to the requested slice/mip by summing all preceding regions
    u64 offset = 0;
    bool found = false;
    for(u32 arr = 0; arr < desc.arraySize && !found; ++arr){
        for(u32 mip = 0; mip < desc.mipLevels && !found; ++mip){
            if(arr == resolved.arraySlice && mip == resolved.mipLevel){
                found = true;
                break;
            }

            const auto mipWidth = Max<u32>(desc.width >> mip, 1u);
            const auto mipHeight = Max<u32>(desc.height >> mip, 1u);
            const auto mipDepth = Max<u32>(desc.depth >> mip, 1u);

            const auto blocksX = Max<u32>((mipWidth + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
            const auto blocksY = Max<u32>((mipHeight + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);

            const auto sliceSize = static_cast<u64>(blocksX) * blocksY * formatInfo.bytesPerBlock;
            offset = __hidden_vulkan::AlignBufferOffset(offset + sliceSize * mipDepth);
        }
    }

    const auto mipWidth = Max<u32>(desc.width >> resolved.mipLevel, 1u);
    const auto blocksX = Max<u32>((mipWidth + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
    const u32 rowPitch = blocksX * formatInfo.bytesPerBlock;

    if(outRowPitch)
        *outRowPitch = rowPitch;

    const auto mipHeight = Max<u32>(desc.height >> resolved.mipLevel, 1u);
    const auto blocksY = Max<u32>((mipHeight + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
    const auto slicePitch = rowPitch * blocksY;

    offset += static_cast<u64>(resolved.z) * slicePitch
            + static_cast<u64>(resolved.y / formatInfo.blockSize) * rowPitch
            + static_cast<u64>(resolved.x / formatInfo.blockSize) * formatInfo.bytesPerBlock
    ;

    return static_cast<u8*>(staging->mappedMemory) + offset;
}

void Device::unmapStagingTexture(IStagingTexture* tex){
    // Memory is persistently mapped; no-op.
    // Unmapping is handled in StagingTexture destructor.
    (void)tex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

