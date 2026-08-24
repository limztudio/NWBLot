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


VkImageMemoryBarrier2 BuildTextureStateBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    auto barrier = VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? VulkanDetail::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VulkanDetail::GetVkImageLayout(stateBits);
    // Ordinary transitions use Vulkan's explicit ignored queue-family sentinel.
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange = VulkanDetail::BuildImageSubresourceRange(subresources, aspectMask);

    return barrier;
}

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
    const bool rayTracingStageAvailable
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
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
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
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

bool NeedsTextureStateBarrier(const ResourceStates::Mask oldState, const ResourceStates::Mask stateBits, const bool uavBarrierEnabled){
    return oldState != stateBits || (oldState == stateBits && uavBarrierEnabled);
}

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const ArraySlice arraySlice,
    const MipLevel mipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    barriers.push_back(BuildTextureStateBarrier(
        image,
        aspectMask,
        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
        oldState,
        stateBits,
        rayTracingStageAvailable
    ));
}

void AppendTextureStateBarriersBefore(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const MipLevel mipEnd,
    const ArraySlice currentArraySlice,
    const MipLevel currentMipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice <= currentArraySlice; ++arraySlice){
        const MipLevel previousMipEnd = arraySlice == currentArraySlice ? currentMipLevel : mipEnd;
        for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < previousMipEnd; ++mipLevel)
            AppendTextureStateBarrier(barriers, image, aspectMask, arraySlice, mipLevel, oldState, stateBits, rayTracingStageAvailable);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::executePipelineBarrier(const VkDependencyInfo& depInfo){
    Framebuffer* resumeFramebuffer = nullptr;
    if(m_renderPassActive){
        resumeFramebuffer = m_renderPassFramebuffer;
        endDynamicRendering();
        m_renderPassActive = false;
        m_renderPassFramebuffer = nullptr;
    }

    // Compute may use generic shader visibility, while a dedicated transfer family can synchronize only transfer
    // accesses. Cross-queue semaphore waits make an unsupported producer scope available before this command list;
    // lower that side to NONE instead of naming an illegal Graphics/Compute access on the transfer queue.
    VkDependencyInfo queueCompatibleDepInfo = depInfo;
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);
    Vector<VkMemoryBarrier2, Alloc::ScratchArena> queueCompatibleMemoryBarriers{scratchArena};
    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> queueCompatibleImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> queueCompatibleBufferBarriers{scratchArena};
    if(m_desc.queueType != CommandQueue::Graphics){
        constexpr VkAccessFlags2 s_GraphicsAttachmentAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
            | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
            | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        ;
        const auto queueCompatibleStageMask = [this](const VkPipelineStageFlags2 stageMask){
            return stageMask == VK_PIPELINE_STAGE_2_NONE
                ? VK_PIPELINE_STAGE_2_NONE
                : (
                    m_desc.queueType == CommandQueue::Transfer
                        ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
                        : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
                )
            ;
        };
        const auto makeSourceScopeQueueCompatible = [&](VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask){
            if(m_desc.queueType == CommandQueue::Transfer){
                constexpr VkAccessFlags2 s_TransferAccessMask =
                    VK_ACCESS_2_TRANSFER_READ_BIT
                    | VK_ACCESS_2_TRANSFER_WRITE_BIT
                ;
                if((accessMask & ~s_TransferAccessMask) != 0u){
                    stageMask = VK_PIPELINE_STAGE_2_NONE;
                    accessMask = 0u;
                    return;
                }
                stageMask = queueCompatibleStageMask(stageMask);
                return;
            }

            // Imported Graphics attachments have no legal local Compute/Copy source scope.
            if((accessMask & s_GraphicsAttachmentAccessMask) != 0u){
                stageMask = VK_PIPELINE_STAGE_2_NONE;
                accessMask = 0u;
                return;
            }
            stageMask = queueCompatibleStageMask(stageMask);
        };
        const auto makeDestinationScopeQueueCompatible = [&](VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask){
            if(m_desc.queueType == CommandQueue::Transfer){
                constexpr VkAccessFlags2 s_TransferAccessMask =
                    VK_ACCESS_2_TRANSFER_READ_BIT
                    | VK_ACCESS_2_TRANSFER_WRITE_BIT
                ;
                if((accessMask & ~s_TransferAccessMask) != 0u){
                    stageMask = VK_PIPELINE_STAGE_2_NONE;
                    accessMask = 0u;
                    return;
                }
            }
            stageMask = queueCompatibleStageMask(stageMask);
        };

        queueCompatibleMemoryBarriers.reserve(depInfo.memoryBarrierCount);
        for(u32 index = 0u; index < depInfo.memoryBarrierCount; ++index){
            VkMemoryBarrier2 barrier = depInfo.pMemoryBarriers[index];
            makeSourceScopeQueueCompatible(barrier.srcStageMask, barrier.srcAccessMask);
            makeDestinationScopeQueueCompatible(barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleMemoryBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pMemoryBarriers = queueCompatibleMemoryBarriers.data();

        queueCompatibleImageBarriers.reserve(depInfo.imageMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.imageMemoryBarrierCount; ++index){
            VkImageMemoryBarrier2 barrier = depInfo.pImageMemoryBarriers[index];
            makeSourceScopeQueueCompatible(barrier.srcStageMask, barrier.srcAccessMask);
            makeDestinationScopeQueueCompatible(barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleImageBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pImageMemoryBarriers = queueCompatibleImageBarriers.data();

        queueCompatibleBufferBarriers.reserve(depInfo.bufferMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.bufferMemoryBarrierCount; ++index){
            VkBufferMemoryBarrier2 barrier = depInfo.pBufferMemoryBarriers[index];
            makeSourceScopeQueueCompatible(barrier.srcStageMask, barrier.srcAccessMask);
            makeDestinationScopeQueueCompatible(barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleBufferBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pBufferMemoryBarriers = queueCompatibleBufferBarriers.data();
    }

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &queueCompatibleDepInfo);

    if(resumeFramebuffer){
        RenderPassParameters params = {};
        if(beginDynamicRendering(resumeFramebuffer, params)){
            m_renderPassActive = true;
            m_renderPassFramebuffer = resumeFramebuffer;
        }
    }
}

void CommandList::commitBarriers(){
    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(m_pendingImageBarriers.size());
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(m_pendingBufferBarriers.size());
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data();

    executePipelineBarrier(depInfo);

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(Texture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!textureResource)
        return;

    Texture& texture = *textureResource;
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("set texture state")))
        return;

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(&texture);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot change the state of a permanently tracked texture"));
        return;
    }

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    bool firstSubresource = true;
    bool needsBarrier = false;
    bool usePerSubresourceBarriers = false;
    const bool uavBarrierEnabled = ResourceStates::HasUnorderedAccess(stateBits) && m_stateTracker.isUavBarrierEnabledForTexture(texture);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);
    usize firstBarrierIndex = m_pendingImageBarriers.size();

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            ResourceStates::Mask subresourceOldState = permanentState;
            if(
                permanentState == ResourceStates::Unknown
                && !m_stateTracker.getResolvedTransientTextureState(texture, arraySlice, mipLevel, subresourceOldState)
            )
                return;

            if(firstSubresource){
                oldState = subresourceOldState;
                firstSubresource = false;
            }
            else if(subresourceOldState != oldState && !usePerSubresourceBarriers){
                usePerSubresourceBarriers = true;
                firstBarrierIndex = m_pendingImageBarriers.size();
                ::ContainerDetail::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
                if(VulkanStateTrackingDetail::NeedsTextureStateBarrier(oldState, stateBits, uavBarrierEnabled)){
                    VulkanStateTrackingDetail::AppendTextureStateBarriersBefore(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        resolvedSubresources,
                        mipEnd,
                        arraySlice,
                        mipLevel,
                        oldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }

            const bool subresourceNeedsBarrier = VulkanStateTrackingDetail::NeedsTextureStateBarrier(
                subresourceOldState,
                stateBits,
                uavBarrierEnabled
            );
            if(subresourceNeedsBarrier){
                needsBarrier = true;
                if(usePerSubresourceBarriers){
                    VulkanStateTrackingDetail::AppendTextureStateBarrier(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        arraySlice,
                        mipLevel,
                        subresourceOldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }
        }
    }

    if(!needsBarrier)
        return;

    if(!usePerSubresourceBarriers){
        const VkImageMemoryBarrier2 barrier = VulkanStateTrackingDetail::BuildTextureStateBarrier(
            texture.m_image,
            texture.m_aspectMask,
            resolvedSubresources,
            oldState,
            stateBits,
            m_context.extensions.KHR_ray_tracing_pipeline
        );

        if(permanentState == ResourceStates::Unknown)
            m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

        if(!m_enableAutomaticBarriers){
            m_pendingImageBarriers.push_back(barrier);
            return;
        }

        auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        executePipelineBarrier(depInfo);
        return;
    }

    if(permanentState == ResourceStates::Unknown)
        m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

    if(!m_enableAutomaticBarriers)
        return;

    const usize newBarrierCount = m_pendingImageBarriers.size() - firstBarrierIndex;
    if(newBarrierCount == 0)
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(newBarrierCount);
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data() + firstBarrierIndex;

    executePipelineBarrier(depInfo);
    m_pendingImageBarriers.resize(firstBarrierIndex);
}

void CommandList::setBufferState(Buffer* bufferResource, ResourceStates::Mask stateBits){
    if(!bufferResource)
        return;

    Buffer& buffer = *bufferResource;
    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(&buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot change the state of a permanently tracked buffer"));
        return;
    }

    ResourceStates::Mask oldState = permanentState;
    if(permanentState == ResourceStates::Unknown && !m_stateTracker.getTransientBufferState(buffer, oldState))
        return;

    const bool needsUavBarrier =
        oldState == stateBits
        && ResourceStates::HasUnorderedAccess(stateBits)
        && m_stateTracker.isUavBarrierEnabledForBuffer(buffer)
    ;

    if(oldState == stateBits && !needsUavBarrier)
        return;

    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    if(permanentState == ResourceStates::Unknown)
        m_stateTracker.beginTrackingTransientBuffer(buffer, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;

    executePipelineBarrier(depInfo);
}

void CommandList::setAccelStructState(RayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits){
    if(!accelStructResource)
        return;

    auto* as = accelStructResource;
    if(as->m_buffer)
        setBufferState(as->m_buffer.get(), stateBits);
}

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const RenderLane::Enum destinationLane
){
    releaseTextureOwnership(textureResource, subresources, m_device.resolveRenderLane(destinationLane));
}

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
    if(!textureResource || !m_currentCmdBuf)
        return;

    Texture& texture = *textureResource;
    if(m_stateTracker.isPermanentTexture(texture)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked texture"));
        return;
    }
    if(m_device.usesConcurrentQueueSharing(texture.m_desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared texture"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("release texture ownership")))
        return;

    if(!m_device.getQueue(destinationQueue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership to an unavailable destination queue"));
        return;
    }

    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;

    // Releases require explicit image layouts; never invent one for untouched images.
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            if(state == ResourceStates::Unknown){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership without a known final resource state"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release texture ownership without a known final resource state"));
                return;
            }
            m_stateTracker.beginTrackingTexture(&texture, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), state);
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            const auto existing = m_textureOwnershipReleaseDestinations.find(key);
            if(existing != m_textureOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture ownership was released to conflicting destination queues"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Texture ownership was released to conflicting destination queues"));
                return;
            }
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel)
            m_textureOwnershipReleaseDestinations.insert_or_assign(TextureSubresourceStateKey{ &texture, mipLevel, arraySlice }, destinationQueue);
    }
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const RenderLane::Enum destinationLane){
    releaseBufferOwnership(bufferResource, m_device.resolveRenderLane(destinationLane));
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const CommandQueue::Enum destinationQueue){
    releaseBufferOwnership(bufferResource, m_device.getPrimaryPhysicalQueue(destinationQueue));
}

void CommandList::releaseBufferOwnership(
    Buffer* bufferResource,
    const GpuPhysicalQueueId destinationQueue
){
    if(!bufferResource || !m_currentCmdBuf)
        return;

    Buffer& buffer = *bufferResource;
    if(m_stateTracker.isPermanentBuffer(buffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked buffer"));
        return;
    }
    if(m_device.usesConcurrentQueueSharing(buffer.m_desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared buffer"));
        return;
    }

    if(!m_device.getQueue(destinationQueue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership to an unavailable destination queue"));
        return;
    }

    // Exports need concrete buffer state; tracked state takes precedence over descriptor initial state.
    ResourceStates::Mask state = m_stateTracker.getBufferState(&buffer);
    if(state == ResourceStates::Unknown)
        state = buffer.m_desc.initialState;
    if(state == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership without a known final resource state"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release buffer ownership without a known final resource state"));
        return;
    }
    m_stateTracker.beginTrackingBuffer(&buffer, state);

    const auto existing = m_bufferOwnershipReleaseDestinations.find(&buffer);
    if(existing != m_bufferOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer ownership was released to conflicting destination queues"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Buffer ownership was released to conflicting destination queues"));
        return;
    }

    m_bufferOwnershipReleaseDestinations.insert_or_assign(&buffer, destinationQueue);
}

void CommandList::setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits){
    if(!texture)
        return;
    if(stateBits == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot permanently track a texture in an unknown state"));
        return;
    }
    if(texture->m_desc.keepInitialState && texture->m_desc.initialState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Permanent texture state must match its retained initial state"));
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot replace a texture's permanent state"));
        return;
    }

    setTextureState(texture, s_AllSubresources, stateBits);
    m_stateTracker.setPermanentTextureState(*texture, stateBits);
}

void CommandList::setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    if(!buffer)
        return;
    if(stateBits == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot permanently track a buffer in an unknown state"));
        return;
    }
    if(buffer->m_desc.keepInitialState && buffer->m_desc.initialState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Permanent buffer state must match its retained initial state"));
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot replace a buffer's permanent state"));
        return;
    }

    setBufferState(buffer, stateBits);
    m_stateTracker.setPermanentBufferState(*buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

