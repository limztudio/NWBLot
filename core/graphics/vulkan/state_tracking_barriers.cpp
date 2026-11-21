// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "state_tracking_detail.h"
#include "queue_barrier_scope_policy.h"

#include <core/common/log.h>

#include <global/containers.h>
#include <global/overflow.h>


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
    const b8 rayTracingStageAvailable
)noexcept{
    auto barrier = VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? VulkanDetail::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VulkanDetail::GetVkImageLayout(stateBits);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange = VulkanDetail::BuildImageSubresourceRange(subresources, aspectMask);

    return barrier;
}


b8 NeedsResourceStateBarrier(const ResourceStates::Mask oldState, const ResourceStates::Mask stateBits, const b8 uavBarrierEnabled, const b8 forceMemoryDependency)noexcept{
    return oldState != stateBits || forceMemoryDependency || (oldState == stateBits && uavBarrierEnabled);
}

b8 ImageBarrierOverlapsTextureSubresources(const VkImageMemoryBarrier2& barrier, const VkImage image, const VkImageAspectFlags aspectMask, const TextureSubresourceSet& subresources)noexcept{
    if(barrier.image != image || (barrier.subresourceRange.aspectMask & aspectMask) == 0u)
        return false;
    if(barrier.subresourceRange.levelCount == 0u || barrier.subresourceRange.layerCount == 0u || subresources.numMipLevels == 0u || subresources.numArraySlices == 0u)
        return false;

    return UnsignedRangesOverlap(barrier.subresourceRange.baseMipLevel, barrier.subresourceRange.levelCount, subresources.baseMipLevel, subresources.numMipLevels)
        && UnsignedRangesOverlap(barrier.subresourceRange.baseArrayLayer, barrier.subresourceRange.layerCount, subresources.baseArraySlice, subresources.numArraySlices);
}

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const ArraySlice arraySlice,
    const MipLevel mipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const b8 rayTracingStageAvailable
){
    barriers.push_back(BuildTextureStateBarrier(image, aspectMask, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), oldState, stateBits, rayTracingStageAvailable));
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
    const b8 rayTracingStageAvailable
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


void CommandList::commitBarriers(){
    if(!validateCommandRecordingScope(FOUNDATION_TEXT("commit barriers")))
        return;

    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;

    executePipelineBarrier({}, { m_pendingImageBarriers.data(), m_pendingImageBarriers.size() }, { m_pendingBufferBarriers.data(), m_pendingBufferBarriers.size() });

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(Texture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits, const b8 forceMemoryDependency){
    if(!textureResource)
        return;
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("set texture state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(!validateTextureForGpuState(textureResource, stateBits, s_OperationName))
        return;

    Texture& texture = *textureResource;
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_creationDesc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, VulkanStateTrackingDetail::s_SubresourceRangeOutsideTextureReason);
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(&texture);
    const b8 uavBarrierEnabled = ResourceStates::HasUnorderedAccess(stateBits) && m_stateTracker.isUavBarrierEnabledForTexture(texture);
    if(permanentState == ResourceStates::Unknown && resolvedSubresources.numMipLevels == 1u && resolvedSubresources.numArraySlices == 1u){
        const MipLevel mipLevel = resolvedSubresources.baseMipLevel;
        const ArraySlice arraySlice = resolvedSubresources.baseArraySlice;
        const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
        auto existing = !m_stateTracker.m_textureStates.empty() ? m_stateTracker.m_textureStates.find(key) : m_stateTracker.m_textureStates.end();
        const ResourceStates::Mask subresourceOldState = existing != m_stateTracker.m_textureStates.end()
            ? existing.value()
            : ((texture.m_creationDesc.keepInitialState && texture.isRetainedSubresourceStateKnown(arraySlice, mipLevel)) ? texture.m_creationDesc.initialState : ResourceStates::Unknown);

        retainResource(&texture);
        if(!VulkanStateTrackingDetail::NeedsResourceStateBarrier(subresourceOldState, stateBits, uavBarrierEnabled, forceMemoryDependency))
            return;

        if(existing != m_stateTracker.m_textureStates.end())
            existing.value() = stateBits;
        else{
            m_stateTracker.m_textureStates.emplace(key, stateBits);
            if(texture.m_creationDesc.keepInitialState)
                m_stateTracker.m_hasKeepInitialStateTextures = true;
        }
        m_stateTracker.updateRecentTextureCache(key, stateBits);

        VkImageMemoryBarrier2 barrier = VulkanStateTrackingDetail::BuildTextureStateBarrier(texture.m_image, texture.m_aspectMask, resolvedSubresources, subresourceOldState, stateBits, m_context.extensions.khrRayTracingPipeline);
        if(!m_enableAutomaticBarriers){
            m_pendingImageBarriers.push_back(barrier);
            return;
        }

        executePipelineBarrier({}, { &barrier, 1u }, {});
        return;
    }

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    b8 firstSubresource = true;
    b8 needsBarrier = false;
    b8 usePerSubresourceBarriers = false;
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);
    usize firstBarrierIndex = m_pendingImageBarriers.size();

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask subresourceOldState = permanentState == ResourceStates::Unknown ? m_stateTracker.getResolvedTransientTextureState(texture, arraySlice, mipLevel) : permanentState;

            if(firstSubresource){
                oldState = subresourceOldState;
                firstSubresource = false;
            }
            else if(subresourceOldState != oldState && !usePerSubresourceBarriers){
                usePerSubresourceBarriers = true;
                firstBarrierIndex = m_pendingImageBarriers.size();
                ::ContainerDetail::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
                if(VulkanStateTrackingDetail::NeedsResourceStateBarrier(oldState, stateBits, uavBarrierEnabled, forceMemoryDependency)){
                    VulkanStateTrackingDetail::AppendTextureStateBarriersBefore(m_pendingImageBarriers, texture.m_image, texture.m_aspectMask, resolvedSubresources, mipEnd, arraySlice, mipLevel, oldState, stateBits, m_context.extensions.khrRayTracingPipeline);
                }
            }

            const b8 subresourceNeedsBarrier = VulkanStateTrackingDetail::NeedsResourceStateBarrier(subresourceOldState, stateBits, uavBarrierEnabled, forceMemoryDependency);
            if(subresourceNeedsBarrier){
                needsBarrier = true;
                if(usePerSubresourceBarriers){
                    VulkanStateTrackingDetail::AppendTextureStateBarrier(m_pendingImageBarriers, texture.m_image, texture.m_aspectMask, arraySlice, mipLevel, subresourceOldState, stateBits, m_context.extensions.khrRayTracingPipeline);
                }
            }
        }
    }

    retainResource(&texture);
    if(!needsBarrier)
        return;

    if(!usePerSubresourceBarriers){
        VkImageMemoryBarrier2 barrier = VulkanStateTrackingDetail::BuildTextureStateBarrier(texture.m_image, texture.m_aspectMask, resolvedSubresources, oldState, stateBits, m_context.extensions.khrRayTracingPipeline);

        if(permanentState == ResourceStates::Unknown)
            m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

        if(!m_enableAutomaticBarriers){
            m_pendingImageBarriers.push_back(barrier);
            return;
        }

        executePipelineBarrier({}, { &barrier, 1u }, {});
        return;
    }

    if(permanentState == ResourceStates::Unknown)
        m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

    if(!m_enableAutomaticBarriers)
        return;

    const usize newBarrierCount = m_pendingImageBarriers.size() - firstBarrierIndex;
    if(newBarrierCount == 0)
        return;

    executePipelineBarrier({}, { m_pendingImageBarriers.data() + firstBarrierIndex, newBarrierCount }, {});
    m_pendingImageBarriers.resize(firstBarrierIndex);
}

void CommandList::setAccelStructState(RayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits, const b8 forceMemoryDependency){
    if(!accelStructResource)
        return;

    auto& as = *MakeNotNull(accelStructResource);
    if(as.m_buffer)
        setBufferState(as.m_buffer.get(), stateBits, forceMemoryDependency);
}

void CommandList::executePipelineBarrier(const Span<VkMemoryBarrier2> memoryBarriers, const Span<VkImageMemoryBarrier2> imageBarriers, const Span<VkBufferMemoryBarrier2> bufferBarriers){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("pipeline barrier");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    executePipelineBarrierValidated(memoryBarriers, imageBarriers, bufferBarriers, s_OperationName);
}

void CommandList::executePipelineBarrierValidated(const Span<VkMemoryBarrier2> memoryBarriers, const Span<VkImageMemoryBarrier2> imageBarriers, const Span<VkBufferMemoryBarrier2> bufferBarriers, const TStringView operationName){
    if(m_hasPendingBufferCopies)
        flushPendingBufferCopies();

    m_graphicsBuffersValidatedInPass = false;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.memoryBarrierCount = static_cast<u32>(memoryBarriers.size());
    depInfo.pMemoryBarriers = memoryBarriers.data();
    depInfo.imageMemoryBarrierCount = static_cast<u32>(imageBarriers.size());
    depInfo.pImageMemoryBarriers = imageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(bufferBarriers.size());
    depInfo.pBufferMemoryBarriers = bufferBarriers.data();

    Framebuffer* resumeFramebuffer = nullptr;
    if(m_renderPassActive){
        resumeFramebuffer = m_renderPassFramebuffer;

        const auto attachmentOverlapsBarrier = [&](const FramebufferAttachment& attachment)noexcept{
            if(!attachment.texture)
                return false;

            const TextureSubresourceSet resolvedSubresources = attachment.subresources.resolve(attachment.texture->m_creationDesc, TextureSubresourceMipResolve::Single);
            for(u32 index = 0u; index < depInfo.imageMemoryBarrierCount; ++index){
                if(VulkanStateTrackingDetail::ImageBarrierOverlapsTextureSubresources(depInfo.pImageMemoryBarriers[index], attachment.texture->m_image, attachment.texture->m_aspectMask, resolvedSubresources))
                    return true;
            }
            return false;
        };

        const FramebufferDesc& framebufferDesc = resumeFramebuffer->m_desc;
        b8 attachmentTransitioned = attachmentOverlapsBarrier(framebufferDesc.depthAttachment) || attachmentOverlapsBarrier(framebufferDesc.shadingRateAttachment);
        for(const FramebufferAttachment& attachment : framebufferDesc.colorAttachments)
            attachmentTransitioned = attachmentTransitioned || attachmentOverlapsBarrier(attachment);
        if(attachmentTransitioned)
            resumeFramebuffer = nullptr;

        endDynamicRendering();
        m_renderPassActive = false;
        m_renderPassFramebuffer = nullptr;
        if(!resumeFramebuffer)
            invalidateGraphicsDrawValidationCache();
    }

    const GpuQueueCapability::Mask exactQueueCapabilities = m_hasCachedPhysicalQueueInfo ? m_cachedPhysicalQueueInfo.capabilities : GpuQueueCapability::None;
    if(!m_universalGraphicsCompute){
        const auto normalizeBarrierScopes = [&](const auto barriers)noexcept{
            for(auto& barrier : barriers){
                QueueBarrierScopePolicy::Normalize(exactQueueCapabilities, barrier.srcStageMask, barrier.srcAccessMask);
                QueueBarrierScopePolicy::Normalize(exactQueueCapabilities, barrier.dstStageMask, barrier.dstAccessMask);
            }
        };
        normalizeBarrierScopes(memoryBarriers);
        normalizeBarrierScopes(imageBarriers);
        normalizeBarrierScopes(bufferBarriers);
    }

    m_context.deviceDispatch.vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);

    if(resumeFramebuffer){
        RenderPassParameters params = {};
        if(beginDynamicRendering(*resumeFramebuffer, params)){
            m_renderPassActive = true;
            m_renderPassFramebuffer = resumeFramebuffer;
        }
        else
            rejectCommandRecording(operationName, FOUNDATION_TEXT("dynamic rendering could not resume after the barrier"));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

