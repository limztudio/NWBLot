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

inline bool AddAlignedStagingMipSize(u64& size, const u64 mipSize, const u32 alignment){
    if(size > UINT64_MAX - mipSize)
        return false;
    return AlignUpU64Checked(size + mipSize, static_cast<u64>(alignment), size);
}

inline bool BuildStagingTextureLayout(
    const TextureDesc& desc,
    const VulkanDetail::TextureFormatBlockLayout& formatLayout,
    const u32 bufferOffsetAlignment,
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

        if(!AddAlignedStagingMipSize(arrayByteSize, mipSize, bufferOffsetAlignment)){
            outMipLayouts.clear();
            return false;
        }
    }

    outArrayByteSize = arrayByteSize;
    return true;
}

inline bool BuildStagingTextureQueueFamilies(
    Device& device,
    const ResourceQueueSharing::Mask sharing,
    VulkanDetail::StagingTextureQueueFamilyVector& outFamilies,
    VkSharingMode& outMode
){
    outFamilies.clear();
    outMode = VK_SHARING_MODE_EXCLUSIVE;

    NWB_ASSERT(ResourceQueueSharing::IsValid(sharing));

    if(sharing == ResourceQueueSharing::Exclusive){
        const GpuPhysicalQueueId primaryGraphics = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(primaryGraphics);
        if(!queueInfo || queueInfo->familyIndex == VK_QUEUE_FAMILY_IGNORED)
            return false;
        outFamilies.push_back(queueInfo->familyIndex);
        return true;
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u || topology.queueCount > Limit<u32>::s_Max)
        return false;
    outFamilies.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(
            queue.familyIndex == VK_QUEUE_FAMILY_IGNORED
            || !VulkanDetail::StagingTextureSharingIncludesQueueClass(sharing, queue.queueClass)
        )
            continue;

        bool alreadyAdmitted = false;
        for(const u32 familyIndex : outFamilies){
            if(familyIndex == queue.familyIndex){
                alreadyAdmitted = true;
                break;
            }
        }
        if(!alreadyAdmitted)
            outFamilies.push_back(queue.familyIndex);
    }

    if(outFamilies.empty() || outFamilies.size() > Limit<u32>::s_Max){
        outFamilies.clear();
        return false;
    }
    if(outFamilies.size() >= 2u)
        outMode = VK_SHARING_MODE_CONCURRENT;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool StagingTextureSharingIncludesQueueClass(
    const ResourceQueueSharing::Mask sharing,
    const CommandQueue::Enum queueClass
)noexcept{
    switch(queueClass){
    case CommandQueue::Graphics:
        return (sharing & ResourceQueueSharing::Graphics) != ResourceQueueSharing::Exclusive;
    case CommandQueue::Compute:
        return (sharing & ResourceQueueSharing::AsyncCompute) != ResourceQueueSharing::Exclusive;
    case CommandQueue::Transfer:
        return (sharing & ResourceQueueSharing::Transfer) != ResourceQueueSharing::Exclusive;
    default:
        return false;
    }
}

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

bool BuildStagingTextureRange(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    const u64 arrayByteSize,
    const u64 totalByteSize,
    const u32 requiredOffsetAlignment,
    const bool requireHostPointerRange,
    StagingTextureRange& outRange
)noexcept{
    outRange = {};
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
        return false;

    u64 byteOffset = mipLayout.byteOffset;
    u64 product = 0u;
    if(
        !TryMultiply<u64>(arrayByteSize, static_cast<u64>(resolvedSlice.arraySlice), product)
        || !AddNoOverflow(byteOffset, product, byteOffset)
        || !TryMultiply<u64>(mipLayout.slicePitch, static_cast<u64>(resolvedSlice.z), product)
        || !AddNoOverflow(byteOffset, product, byteOffset)
        || !TryMultiply<u64>(
            mipLayout.rowPitch,
            static_cast<u64>(resolvedSlice.y / formatLayout.blockHeight),
            product
        )
        || !AddNoOverflow(byteOffset, product, byteOffset)
        || !TryMultiply<u64>(
            static_cast<u64>(formatLayout.bytesPerBlock),
            static_cast<u64>(resolvedSlice.x / formatLayout.blockWidth),
            product
        )
        || !AddNoOverflow(byteOffset, product, byteOffset)
    )
        return false;

    const u64 mappedBlocksX = Max<u64>(
        DivideUp(static_cast<u64>(resolvedSlice.width), static_cast<u64>(formatLayout.blockWidth)),
        1ull
    );
    const u64 mappedBlocksY = Max<u64>(
        DivideUp(static_cast<u64>(resolvedSlice.height), static_cast<u64>(formatLayout.blockHeight)),
        1ull
    );
    u64 byteSize = 0u;
    if(
        !TryMultiply<u64>(static_cast<u64>(resolvedSlice.depth - 1u), mipLayout.slicePitch, byteSize)
        || !TryMultiply<u64>(mappedBlocksY - 1u, mipLayout.rowPitch, product)
        || !AddNoOverflow(byteSize, product, byteSize)
        || !TryMultiply<u64>(mappedBlocksX, static_cast<u64>(formatLayout.bytesPerBlock), product)
        || !AddNoOverflow(byteSize, product, byteSize)
        || byteSize == 0u
        || byteOffset > totalByteSize
        || byteSize > totalByteSize - byteOffset
        || (requiredOffsetAlignment != 0u && (byteOffset % requiredOffsetAlignment) != 0u)
    )
        return false;

    const u64 maximumHostRange = static_cast<u64>(Limit<usize>::s_Max);
    if(
        requireHostPointerRange
        && (
            byteOffset > maximumHostRange
            || byteSize > maximumHostRange - byteOffset
            || mipLayout.rowPitch > maximumHostRange
        )
    )
        return false;

    outRange.byteOffset = byteOffset;
    outRange.byteSize = byteSize;
    outRange.rowPitch = mipLayout.rowPitch;
    outRange.bufferRowLength = mipLayout.bufferRowLength;
    outRange.bufferImageHeight = mipLayout.bufferImageHeight;
    return true;
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
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    if(!VulkanDetail::GetTextureFormatBlockLayout(formatInfo, formatLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        return nullptr;
    }

    u32 bufferOffsetAlignment = 0u;
    if(!VulkanDetail::TryComputeCommonAlignment(
        s_BufferAlignmentBytes,
        formatLayout.bytesPerBlock,
        bufferOffsetAlignment
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        return nullptr;
    }

    VulkanDetail::StagingTextureMipLayoutVector mipLayouts(m_context.objectArena);
    u64 arrayByteSize = 0;
    const bool layoutBuilt = __hidden_vulkan_staging_texture::BuildStagingTextureLayout(
        d,
        formatLayout,
        bufferOffsetAlignment,
        arrayByteSize,
        mipLayouts
    );
    u64 totalSize = 0u;
    if(
        !layoutBuilt
        || arrayByteSize == 0u
        || !TryMultiply<u64>(arrayByteSize, static_cast<u64>(d.arraySize), totalSize)
        || totalSize == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return nullptr;
    }

    VulkanDetail::StagingTextureQueueFamilyVector admittedFamilies(m_context.objectArena);
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(!__hidden_vulkan_staging_texture::BuildStagingTextureQueueFamilies(
        *this,
        d.queueSharing,
        admittedFamilies,
        sharingMode
    )){
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
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = sharingMode;
    if(sharingMode == VK_SHARING_MODE_CONCURRENT){
        bufferInfo.queueFamilyIndexCount = static_cast<u32>(admittedFamilies.size());
        bufferInfo.pQueueFamilyIndices = admittedFamilies.data();
    }

    const VkResult res = m_allocator.createStagingTexture(*staging, bufferInfo, cpuAccess);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, staging);
        return nullptr;
    }

    staging->m_desc = d;
    staging->m_creationDesc = d;
    staging->m_formatLayout = formatLayout;
    staging->m_aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    staging->m_arrayByteSize = arrayByteSize;
    staging->m_totalByteSize = totalSize;
    staging->m_bufferOffsetAlignment = bufferOffsetAlignment;
    staging->m_creationQueueSharing = d.queueSharing;
    staging->m_creationSharingMode = sharingMode;
    staging->m_mipLayouts = Move(mipLayouts);
    staging->m_admittedQueueFamilies = Move(admittedFamilies);
    staging->m_cpuAccess = cpuAccess;

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

    u64 expectedTotalByteSize = 0u;
    if(
        staging.m_creationDesc.arraySize == 0u
        || staging.m_creationDesc.mipLevels == 0u
        || staging.m_mipLayouts.size() != staging.m_creationDesc.mipLevels
        || !TryMultiply<u64>(
            staging.m_arrayByteSize,
            static_cast<u64>(staging.m_creationDesc.arraySize),
            expectedTotalByteSize
        )
        || expectedTotalByteSize != staging.m_totalByteSize
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: immutable layout provenance is invalid"));
        return nullptr;
    }

    if(
        slice.mipLevel >= staging.m_creationDesc.mipLevels
        || slice.mipLevel >= staging.m_mipLayouts.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mip is outside the creation layout"));
        return nullptr;
    }

    TextureSlice resolvedSlice;
    if(!VulkanDetail::IsTextureSliceInBounds(
        staging.m_creationDesc,
        slice,
        staging.m_formatLayout,
        &resolvedSlice
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return nullptr;
    }

    VulkanDetail::StagingTextureRange range;
    if(!VulkanDetail::BuildStagingTextureRange(
        resolvedSlice,
        staging.m_mipLayouts[resolvedSlice.mipLevel],
        staging.m_formatLayout,
        staging.m_arrayByteSize,
        staging.m_totalByteSize,
        0u,
        true,
        range
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mapped range is invalid"));
        return nullptr;
    }

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
            : m_allocator.invalidateStagingTextureMemory(staging, range.byteOffset, range.byteSize)
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
        *outRowPitch = static_cast<usize>(range.rowPitch);

    return static_cast<u8*>(mappedMemory) + static_cast<usize>(range.byteOffset);
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

