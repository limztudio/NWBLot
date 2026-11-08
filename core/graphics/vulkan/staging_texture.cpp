// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_shape_policy.h"
#include "backend.h"
#include "texture_transfer_layout.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_staging_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StagingTextureQueueFamilies{
    VulkanDetail::StagingTextureQueueFamilyVector families;
    VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
};

inline Expected<StagingTextureQueueFamilies> BuildStagingTextureQueueFamilies(Alloc::GlobalArena& arena, Device& device, const ResourceQueueSharing::Mask sharing){
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
        if(queue.familyIndex == VK_QUEUE_FAMILY_IGNORED || !ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass))
            continue;

        b8 alreadyAdmitted = false;
        for(const u32 familyIndex : result.families){
            if(familyIndex == queue.familyIndex){
                alreadyAdmitted = true;
                break;
            }
        }
        if(!alreadyAdmitted)
            result.families.push_back(queue.familyIndex);
    }

    if(result.families.empty())
        return MakeUnexpected(Failure{});
    if(result.families.size() >= s_MinConcurrentSharingQueueFamilyCount)
        result.mode = VK_SHARING_MODE_CONCURRENT;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<StagingTextureHandle> Device::createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess){
    if(!ResourceQueueSharing::IsValid(d.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: queue sharing contains unknown bits"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: queue sharing contains unknown bits"));
        return MakeUnexpected(Failure{});
    }
    if(cpuAccess != CpuAccessMode::Read && cpuAccess != CpuAccessMode::Write){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: CPU access must be Read or Write"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid CPU access"));
        return MakeUnexpected(Failure{});
    }

    if(!VulkanTextureDetail::TextureShapePolicy::validate(d, NWB_TEXT("create staging texture"))){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture shape"));
        return MakeUnexpected(Failure{});
    }
    if(d.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: sample count must be 1"));
        return MakeUnexpected(Failure{});
    }
    if(static_cast<u32>(d.format) >= static_cast<u32>(Format::Count)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: texture format is out of range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: texture format is out of range"));
        return MakeUnexpected(Failure{});
    }

    const FormatInfo& formatInfo = GetFormatInfo(d.format);
    const auto formatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    if(!formatLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid texture format"));
        return MakeUnexpected(Failure{});
    }
    const auto bufferOffsetAlignment = CommonAlignmentChecked<u32>(s_BufferAlignmentBytes, formatLayout->bytesPerBlock);
    if(!bufferOffsetAlignment){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: invalid buffer offset alignment"));
        return MakeUnexpected(Failure{});
    }
    auto layout = VulkanDetail::TextureTransferLayout::buildStagingLayout(m_context.objectArena, d, *formatLayout, *bufferOffsetAlignment);
    if(!layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return MakeUnexpected(Failure{});
    }
    // Every admitted mip has at least one byte, so the layout and the array total are nonzero.
    NWB_ASSERT(layout->arrayByteSize != 0u);
    const auto totalSize = TryMultiply<u64>(layout->arrayByteSize, static_cast<u64>(d.arraySize));
    if(!totalSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: computed layout overflows"));
        return MakeUnexpected(Failure{});
    }
    auto queueFamilies = __hidden_vulkan_staging_texture::BuildStagingTextureQueueFamilies(m_context.objectArena, *this, d.queueSharing);
    if(!queueFamilies){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create staging texture: requested queue sharing is unavailable"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create staging texture: unavailable queue sharing"));
        return MakeUnexpected(Failure{});
    }
    StagingTextureHandle staging(NewArenaObject<StagingTexture>(m_context.objectArena, m_context, m_allocator), StagingTextureHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!staging){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate staging texture wrapper"));
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
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

    return staging;
}

Expected<StagingTextureMapping> Device::mapStagingTexture(StagingTexture& staging, const TextureSlice& slice, const CpuAccessMode::Enum requestedAccess){
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
    NWB_ASSERT(staging.m_cpuAccess == CpuAccessMode::Read || staging.m_cpuAccess == CpuAccessMode::Write);
    if(requestedAccess != staging.m_cpuAccess){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: requested access does not match the texture CPU access"));
        return MakeUnexpected(Failure{});
    }

    // Creation sets the layout once: one mip layout per mip and an overflow-checked total size.
    NWB_ASSERT(staging.m_mipLayouts.size() == staging.m_creationDesc.mipLevels && staging.m_totalByteSize == staging.m_arrayByteSize * static_cast<u64>(staging.m_creationDesc.arraySize));

    if(slice.mipLevel >= staging.m_creationDesc.mipLevels){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mip is outside the creation layout"));
        return MakeUnexpected(Failure{});
    }

    const auto resolvedSlice = VulkanDetail::TextureTransferLayout::resolveSlice(staging.m_creationDesc, slice, staging.m_formatLayout);
    if(!resolvedSlice){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: slice is outside the texture"));
        return MakeUnexpected(Failure{});
    }

    const auto range = VulkanDetail::TextureTransferLayout::buildStagingRange(*resolvedSlice, staging.m_mipLayouts[resolvedSlice->mipLevel], staging.m_formatLayout, staging.m_arrayByteSize, staging.m_totalByteSize, 0u, true);
    if(!range){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: mapped range is invalid"));
        return MakeUnexpected(Failure{});
    }

    ScopedLock lock(staging.m_mappingMutex);
    if(!staging.m_mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map staging texture: persistent mapping pointer is null"));
        return MakeUnexpected(Failure{});
    }

    const b8 needsInvalidate = requestedAccess == CpuAccessMode::Read && staging.m_requiresInvalidate;
    if(needsInvalidate){
        const VkResult res = m_allocator.invalidateStagingTextureMemory(staging, range->byteOffset, range->byteSize);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate staging texture mapping: {}"), ResultToString(res));
            return MakeUnexpected(Failure{});
        }
    }

    return StagingTextureMapping{ static_cast<u8*>(staging.m_mappedMemory) + static_cast<usize>(range->byteOffset), static_cast<usize>(range->rowPitch) };
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
    NWB_ASSERT(staging.m_cpuAccess == CpuAccessMode::Read || staging.m_cpuAccess == CpuAccessMode::Write);

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

