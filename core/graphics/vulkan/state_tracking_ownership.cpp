// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ResourceStates::Mask NormalizeOwnershipState(const ResourceStates::Mask state){
    return state != ResourceStates::Unknown ? state : ResourceStates::Common;
}

VkImageMemoryBarrier2 BuildTextureOwnershipReleaseBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    return barrier;
}

VkImageMemoryBarrier2 BuildTextureOwnershipAcquireBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipReleaseBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable,
    const BufferRange range
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = range.byteOffset;
    barrier.size = range.byteSize;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable,
    const BufferRange range
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = range.byteOffset;
    barrier.size = range.byteSize;
    return barrier;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const CommandQueue::Enum destinationQueue
){
    releaseTextureOwnership(textureResource, subresources, m_device.getPrimaryPhysicalQueue(destinationQueue));
}

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const GpuPhysicalQueueId destinationQueue
){
    if(!textureResource)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("release texture ownership")))
        return;

    Texture& texture = *textureResource;
    if(!isTextureReadyForCommandQueue(&texture)){
        rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("texture is not ready for this exact command queue"));
        return;
    }
    if(m_stateTracker.isPermanentTexture(texture)){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("permanently tracked textures cannot transfer ownership")
        );
        return;
    }
    if(texture.m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("concurrently shared textures do not have exclusive ownership")
        );
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(
        texture.m_creationDesc,
        TextureSubresourceMipResolve::Range
    );
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("subresource range is empty or outside the texture")
        );
        return;
    }

    if(!m_device.getQueue(destinationQueue)){
        rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("destination queue is unavailable"));
        return;
    }

    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;

    // Releases require explicit image layouts; never invent one for untouched images. Validate every affected
    // subresource before publishing any tracked state or destination.
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            if(state == ResourceStates::Unknown){
                rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("final resource state is unknown"));
                return;
            }
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            const auto existing = m_textureOwnershipReleaseDestinations.find(key);
            if(existing != m_textureOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
                rejectCommandRecording(
                    NWB_TEXT("release texture ownership"),
                    NWB_TEXT("subresource already targets a conflicting destination queue")
                );
                return;
            }
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            m_stateTracker.beginTrackingTexture(
                &texture,
                TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                state
            );
            m_textureOwnershipReleaseDestinations.insert_or_assign(TextureSubresourceStateKey{ &texture, mipLevel, arraySlice }, destinationQueue);
        }
    }
    retainResource(&texture);
}

void CommandList::setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits){
    if(!texture)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("set permanent texture state")))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(NWB_TEXT("set permanent texture state"), NWB_TEXT("permanent state cannot be unknown"));
        return;
    }
    if(!isTextureReadyForCommandQueue(texture)){
        rejectCommandRecording(NWB_TEXT("set permanent texture state"), NWB_TEXT("texture is not ready for this exact command queue"));
        return;
    }
    if(texture->m_creationDesc.keepInitialState && texture->m_creationDesc.initialState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent texture state"),
            NWB_TEXT("permanent state conflicts with the retained initial state")
        );
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent texture state"),
            NWB_TEXT("a different permanent state is already tracked")
        );
        return;
    }
    for(auto it = m_textureOwnershipReleaseDestinations.begin(); it != m_textureOwnershipReleaseDestinations.end(); ++it){
        if(it->first.texture == texture){
            rejectCommandRecording(
                NWB_TEXT("set permanent texture state"),
                NWB_TEXT("texture already has a pending ownership release")
            );
            return;
        }
    }

    setTextureState(texture, s_AllSubresources, stateBits);
    if(m_commandRecordingFailed)
        return;
    retainResource(texture);
    m_stateTracker.setPermanentTextureState(*texture, stateBits);
}

void CommandList::setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    if(!buffer)
        return;
    constexpr const tchar* s_OperationName = NWB_TEXT("set permanent buffer state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(s_OperationName, NWB_TEXT("permanent state cannot be unknown"));
        return;
    }
    if(!validateBufferForGpuState(buffer, stateBits, s_OperationName))
        return;
    if(buffer->m_creationDesc.keepInitialState && buffer->m_creationDesc.initialState != stateBits){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("permanent state conflicts with the retained initial state")
        );
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("a different permanent state is already tracked")
        );
        return;
    }
    if(m_bufferOwnershipReleaseDestinations.find(buffer) != m_bufferOwnershipReleaseDestinations.end()){
        rejectCommandRecording(
            s_OperationName,
            NWB_TEXT("buffer already has a pending ownership release")
        );
        return;
    }

    setBufferState(buffer, stateBits);
    if(m_commandRecordingFailed)
        return;
    retainResource(buffer);
    m_stateTracker.setPermanentBufferState(*buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

