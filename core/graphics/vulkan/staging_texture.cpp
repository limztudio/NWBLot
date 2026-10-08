// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_staging_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StagingTextureMip{
    VulkanDetail::StagingTextureMipLayout layout;
    u64 byteSize = 0u;
};

struct StagingTextureLayout{
    u64 arrayByteSize = 0u;
    VulkanDetail::StagingTextureMipLayoutVector mipLayouts;
};

struct StagingTextureQueueFamilies{
    VulkanDetail::StagingTextureQueueFamilyVector families;
    VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
};

inline Expected<StagingTextureMip> BuildStagingTextureMipLayout(
    const TextureDesc& desc,
    const VulkanDetail::TextureFormatBlockLayout& formatLayout,
    const u32 mip
){
    StagingTextureMip result{};

    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mip);

    const u64 blocksX = Max<u64>(DivideUp(static_cast<u64>(mipExtent.width), static_cast<u64>(formatLayout.blockWidth)), 1ull);
    const u64 blocksY = Max<u64>(DivideUp(static_cast<u64>(mipExtent.height), static_cast<u64>(formatLayout.blockHeight)), 1ull);
    if(blocksX > UINT64_MAX / blocksY)
        return MakeUnexpected(Failure{});

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
    return AlignUpU64Checked(*total, static_cast<u64>(alignment));
}

inline Expected<StagingTextureLayout> BuildStagingTextureLayout(
    Alloc::GlobalArena& arena,
    const TextureDesc& desc,
    const VulkanDetail::TextureFormatBlockLayout& formatLayout,
    const u32 bufferOffsetAlignment
){
    if(desc.mipLevels == 0u)
        return MakeUnexpected(Failure{});
    StagingTextureLayout result{ 0u, VulkanDetail::StagingTextureMipLayoutVector(arena) };
    result.mipLayouts.reserve(desc.mipLevels);
    for(u32 mip = 0u; mip < desc.mipLevels; ++mip){
        auto layout = BuildStagingTextureMipLayout(desc, formatLayout, mip);
        if(!layout)
            return MakeUnexpected(layout.error());
        layout->layout.byteOffset = result.arrayByteSize;
        result.mipLayouts.push_back(layout->layout);
        const auto size = AddAlignedStagingMipSize(result.arrayByteSize, layout->byteSize, bufferOffsetAlignment);
        if(!size)
            return MakeUnexpected(size.error());
        result.arrayByteSize = *size;
    }
    return result;
}

inline Expected<StagingTextureQueueFamilies> BuildStagingTextureQueueFamilies(
    Alloc::GlobalArena& arena, Device& device, const ResourceQueueSharing::Mask sharing
){
    StagingTextureQueueFamilies result{ VulkanDetail::StagingTextureQueueFamilyVector(arena) };

    NWB_ASSERT(ResourceQueueSharing::IsValid(sharing));

    if(sharing == ResourceQueueSharing::Exclusive){
        const GpuPhysicalQueueId primaryGraphics = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(primaryGraphics);
        if(!queueInfo || queueInfo->familyIndex == VK_QUEUE_FAMILY_IGNORED)
            return MakeUnexpected(Failure{});
        result.families.push_back(queueInfo->familyIndex);
        return result;
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u || topology.queueCount > Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});
    result.families.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(
            queue.familyIndex == VK_QUEUE_FAMILY_IGNORED
            || !ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass)
        )
            continue;

        bool alreadyAdmitted = false;
        for(const u32 familyIndex : result.families){
            if(familyIndex == queue.familyIndex){
                alreadyAdmitted = true;
                break;
            }
        }
        if(!alreadyAdmitted)
            result.families.push_back(queue.familyIndex);
    }

    if(result.families.empty() || result.families.size() > Limit<u32>::s_Max){
        result.families.clear();
        return MakeUnexpected(Failure{});
    }
    if(result.families.size() >= 2u)
        result.mode = VK_SHARING_MODE_CONCURRENT;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureSlice> ResolveTextureSlice(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout)noexcept{
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

Expected<StagingTextureRange> BuildStagingTextureRange(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    const u64 arrayByteSize,
    const u64 totalByteSize,
    const u32 requiredOffsetAlignment,
    const bool requireHostPointerRange
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

    const u64 mappedBlocksX = Max<u64>(
        DivideUp(static_cast<u64>(resolvedSlice.width), static_cast<u64>(formatLayout.blockWidth)),
        1ull
    );
    const u64 mappedBlocksY = Max<u64>(
        DivideUp(static_cast<u64>(resolvedSlice.height), static_cast<u64>(formatLayout.blockHeight)),
        1ull
    );
    const auto depthSize = TryMultiply<u64>(static_cast<u64>(resolvedSlice.depth - 1u), mipLayout.slicePitch);
    const auto rowSize = TryMultiply<u64>(mappedBlocksY - 1u, mipLayout.rowPitch);
    const auto columnSize = TryMultiply<u64>(mappedBlocksX, static_cast<u64>(formatLayout.bytesPerBlock));
    if(!depthSize || !rowSize || !columnSize)
        return MakeUnexpected(Failure{});
    auto byteSize = AddNoOverflow(*depthSize, *rowSize);
    if(!byteSize)
        return MakeUnexpected(byteSize.error());
    byteSize = AddNoOverflow(*byteSize, *columnSize);
    if(
        !byteSize || *byteSize == 0u
        || *byteOffset > totalByteSize || *byteSize > totalByteSize - *byteOffset
        || (requiredOffsetAlignment != 0u && (*byteOffset % requiredOffsetAlignment) != 0u)
    )
        return MakeUnexpected(Failure{});

    const u64 maximumHostRange = static_cast<u64>(Limit<usize>::s_Max);
    if(
        requireHostPointerRange
        && (
            *byteOffset > maximumHostRange
            || *byteSize > maximumHostRange - *byteOffset
            || mipLayout.rowPitch > maximumHostRange
        )
    )
        return MakeUnexpected(Failure{});

    return StagingTextureRange{
        *byteOffset, *byteSize, mipLayout.rowPitch, mipLayout.bufferRowLength, mipLayout.bufferImageHeight
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    if(!ResourceQueueSharing::IsValid(d.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: queue sharing contains unknown bits"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: queue sharing contains unknown bits"));
        return nullptr;
    }
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
    if(static_cast<u32>(d.format) >= static_cast<u32>(Format::kCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: texture format is out of range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: texture format is out of range"));
        return nullptr;
    }

    const FormatInfo& formatInfo = GetFormatInfo(d.format);
    const auto formatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    if(!formatLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        return nullptr;
    }
    const auto bufferOffsetAlignment = VulkanDetail::TryComputeCommonAlignment(s_BufferAlignmentBytes, formatLayout->bytesPerBlock);
    if(!bufferOffsetAlignment){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        return nullptr;
    }
    auto layout = __hidden_vulkan_staging_texture::BuildStagingTextureLayout(
        m_context.objectArena, d, *formatLayout, *bufferOffsetAlignment
    );
    if(!layout || layout->arrayByteSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return nullptr;
    }
    const auto totalSize = TryMultiply<u64>(layout->arrayByteSize, static_cast<u64>(d.arraySize));
    if(!totalSize || *totalSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return nullptr;
    }
    auto queueFamilies = __hidden_vulkan_staging_texture::BuildStagingTextureQueueFamilies(m_context.objectArena, *this, d.queueSharing);
    if(!queueFamilies){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: requested queue sharing is unavailable"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: unavailable queue sharing"));
        return nullptr;
    }
    auto* staging = NewArenaObject<StagingTexture>(m_context.objectArena, m_context, m_allocator);
    if(!staging){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate staging texture wrapper"));
        return nullptr;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = *totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = queueFamilies->mode;
    if(queueFamilies->mode == VK_SHARING_MODE_CONCURRENT){
        bufferInfo.queueFamilyIndexCount = static_cast<u32>(queueFamilies->families.size());
        bufferInfo.pQueueFamilyIndices = queueFamilies->families.data();
    }

    const VkResult res = m_allocator.createStagingTexture(*staging, bufferInfo, cpuAccess);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    staging->m_desc = d;
    staging->m_creationDesc = d;
    staging->m_formatLayout = *formatLayout;
    staging->m_aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    staging->m_arrayByteSize = layout->arrayByteSize;
    staging->m_totalByteSize = *totalSize;
    staging->m_bufferOffsetAlignment = *bufferOffsetAlignment;
    staging->m_creationQueueSharing = d.queueSharing;
    staging->m_creationSharingMode = queueFamilies->mode;
    staging->m_mipLayouts = Move(layout->mipLayouts);
    staging->m_admittedQueueFamilies = Move(queueFamilies->families);
    staging->m_cpuAccess = cpuAccess;

    return StagingTextureHandle(staging, StagingTextureHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

Expected<StagingTextureMapping> Device::mapStagingTexture(
    StagingTexture& staging,
    const TextureSlice& slice,
    const CpuAccessMode::Enum requestedAccess
){
    if(requestedAccess != CpuAccessMode::Read && requestedAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: invalid CPU access mode"));
        return MakeUnexpected(Failure{});
    }

    if(&staging.m_context != &m_context || &staging.m_allocator != &m_allocator){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture belongs to another device"));
        return MakeUnexpected(Failure{});
    }
    if(staging.m_buffer == VK_NULL_HANDLE || !staging.m_allocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: native buffer or allocation is null"));
        return MakeUnexpected(Failure{});
    }
    if(staging.m_cpuAccess != CpuAccessMode::Read && staging.m_cpuAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: texture was created without valid CPU access"));
        return MakeUnexpected(Failure{});
    }
    if(requestedAccess != staging.m_cpuAccess){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to map staging texture: requested access does not match the texture CPU access")
        );
        return MakeUnexpected(Failure{});
    }

    const auto expectedTotalByteSize = TryMultiply<u64>(staging.m_arrayByteSize, static_cast<u64>(staging.m_creationDesc.arraySize));
    if(
        staging.m_creationDesc.arraySize == 0u || staging.m_creationDesc.mipLevels == 0u
        || staging.m_mipLayouts.size() != staging.m_creationDesc.mipLevels
        || !expectedTotalByteSize || *expectedTotalByteSize != staging.m_totalByteSize
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: immutable layout provenance is invalid"));
        return MakeUnexpected(Failure{});
    }

    if(
        slice.mipLevel >= staging.m_creationDesc.mipLevels
        || slice.mipLevel >= staging.m_mipLayouts.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mip is outside the creation layout"));
        return MakeUnexpected(Failure{});
    }

    const auto resolvedSlice = VulkanDetail::ResolveTextureSlice(staging.m_creationDesc, slice, staging.m_formatLayout);
    if(!resolvedSlice){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return MakeUnexpected(Failure{});
    }

    const auto range = VulkanDetail::BuildStagingTextureRange(
        *resolvedSlice,
        staging.m_mipLayouts[resolvedSlice->mipLevel],
        staging.m_formatLayout,
        staging.m_arrayByteSize,
        staging.m_totalByteSize,
        0u,
        true
    );
    if(!range){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mapped range is invalid"));
        return MakeUnexpected(Failure{});
    }

    ScopedLock lock(staging.m_mappingMutex);
    if(!staging.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: persistent mapping pointer is null"));
        return MakeUnexpected(Failure{});
    }

    const bool needsInvalidate = requestedAccess == CpuAccessMode::Read && staging.m_requiresInvalidate;
    if(needsInvalidate){
        const VkResult res = m_allocator.invalidateStagingTextureMemory(staging, range->byteOffset, range->byteSize);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate staging texture mapping: {}"), ResultToString(res));
            return MakeUnexpected(Failure{});
        }
    }

    return StagingTextureMapping{
        static_cast<u8*>(staging.m_mappedMemory) + static_cast<usize>(range->byteOffset),
        static_cast<usize>(range->rowPitch)
    };
}

void Device::unmapStagingTexture(StagingTexture& staging){
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

    // CPU-access staging allocations remain mapped until destruction.
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

