// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"
#include "queue_barrier_scope_policy.h"
#include "buffer_ownership_transfer.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_state_tracking_buffers{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_ByteRangeOutsideBufferReason = FOUNDATION_TEXT("byte range is empty or outside the buffer");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsBufferStateRangeValid(const BufferRange range, const BufferDesc& description)noexcept{
    return range.hasExtent() && range.byteOffset < description.byteSize && (range.byteSize == BufferRange::s_AllBytes || range.byteSize <= description.byteSize - range.byteOffset);
}

VkBufferMemoryBarrier2 BuildBufferStateBarrier(const VkBuffer buffer, const BufferRange range, const ResourceStates::Mask oldState, const ResourceStates::Mask stateBits, const b8 rayTracingStageAvailable)noexcept{
    const ResourceStates::Mask sourceState = oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common;
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(sourceState, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(sourceState);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = range.byteOffset;
    barrier.size = range.byteSize;
    return barrier;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const StateTracker::BufferRangeStates* StateTracker::findBufferRangesCached(Buffer& buffer)const noexcept{
    if(m_lastBufferKey == &buffer)
        return m_lastBufferRanges;
    if(m_bufferStates.empty())
        return nullptr;
    const auto found = m_bufferStates.find(MakeNotNull(&buffer));
    if(found == m_bufferStates.end())
        return nullptr;
    m_lastBufferKey = &buffer;
    m_lastBufferRanges = const_cast<BufferRangeStates*>(&found.value());
    return m_lastBufferRanges;
}

StateTracker::BufferRangeStates* StateTracker::findBufferRangesCachedMutable(Buffer& buffer)noexcept{
    return const_cast<BufferRangeStates*>(findBufferRangesCached(buffer));
}

ResourceStates::Mask StateTracker::getBufferStateFromRanges(Buffer& buffer, const BufferRangeStates* const ranges, const BufferRange& resolvedRange)const noexcept{
    const ResourceStates::Mask fallbackState = buffer.isRetainedStateKnown() ? buffer.m_creationDesc.initialState : ResourceStates::Unknown;
    if(!ranges || ranges->empty())
        return fallbackState;

    ResourceStates::Mask resolvedState = ResourceStates::Unknown;
    b8 first = true;
    const auto accumulate = [&](const ResourceStates::Mask state)noexcept{
        if(first){
            resolvedState = state;
            first = false;
            return true;
        }
        return state == resolvedState;
    };
    u64 cursor = resolvedRange.byteOffset;
    for(const BufferRangeState& entry : *ranges){
        const BufferRange overlap = entry.range.intersect(resolvedRange);
        if(!overlap.hasExtent())
            continue;
        if(overlap.byteOffset > cursor && !accumulate(fallbackState))
            return ResourceStates::Unknown;
        if(!accumulate(entry.state))
            return ResourceStates::Unknown;
        cursor = overlap.end();
    }
    if(cursor < resolvedRange.end() && !accumulate(fallbackState))
        return ResourceStates::Unknown;
    return resolvedState;
}

b8 StateTracker::hasExplicitBufferStateFromRanges(const BufferRangeStates* const ranges, const BufferRange& resolvedRange, const b8 requireKnown)const noexcept{
    if(!ranges || ranges->empty())
        return false;

    u64 cursor = resolvedRange.byteOffset;
    for(const BufferRangeState& entry : *ranges){
        if(entry.range.end() <= cursor)
            continue;
        if(entry.range.byteOffset > cursor || (requireKnown && entry.state == ResourceStates::Unknown))
            return false;
        cursor = entry.range.end();
        if(cursor >= resolvedRange.end())
            return true;
    }
    return false;
}

ResourceStates::Mask StateTracker::getBufferState(Buffer* buffer, const BufferRange range)const noexcept{
    if(!buffer || !VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc))
        return ResourceStates::Unknown;

    if(!m_permanentBufferStates.empty()){
        const auto permanent = m_permanentBufferStates.find(MakeNotNull(buffer));
        if(permanent != m_permanentBufferStates.end())
            return permanent.value().state;
    }

    const BufferRange resolvedRange = range.resolve(buffer->m_creationDesc);
    return getBufferStateFromRanges(*buffer, findBufferRangesCached(*buffer), resolvedRange);
}

b8 StateTracker::hasExplicitBufferState(Buffer* buffer, const BufferRange range, const b8 requireKnown)const noexcept{
    if(!buffer || !VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc))
        return false;
    const BufferRange resolvedRange = range.resolve(buffer->m_creationDesc);
    if(!resolvedRange.hasExtent())
        return false;
    if(!m_permanentBufferStates.empty() && m_permanentBufferStates.find(MakeNotNull(buffer)) != m_permanentBufferStates.end())
        return true;
    return hasExplicitBufferStateFromRanges(findBufferRangesCached(*buffer), resolvedRange, requireKnown);
}

void StateTracker::beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state, const BufferRange range){
    if(!buffer || isPermanentBuffer(*buffer))
        return;
    beginTrackingTransientBuffer(*buffer, state, range);
}

void StateTracker::beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state, const BufferRange range, const b8 seedOnly){
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc))
        return;
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!resolvedRange.hasExtent())
        return;
    beginTrackingTransientBufferWithRanges(buffer, state, resolvedRange, seedOnly, findBufferRangesCachedMutable(buffer));
}

void StateTracker::beginTrackingTransientBufferWithRanges(Buffer& buffer, ResourceStates::Mask state, const BufferRange& resolvedRange, const b8 seedOnly, BufferRangeStates* const existingRanges){
    if(!existingRanges){
        invalidateBufferCache();
        auto [it, inserted] = m_bufferStates.emplace(MakeNotNull(&buffer), BufferRangeStates(m_context.objectArena));
        (void)inserted;
        it.value().push_back(BufferRangeState{ resolvedRange, state });
        m_lastBufferKey = &buffer;
        m_lastBufferRanges = &it.value();
        return;
    }

    if(!existingRanges->empty()){
        if(!seedOnly && resolvedRange.isEntireBuffer(buffer.m_creationDesc)){
            existingRanges->resize(1u);
            (*existingRanges)[0u] = BufferRangeState{ resolvedRange, state };
            return;
        }
        if(hasExplicitBufferStateFromRanges(existingRanges, resolvedRange)){
            if(seedOnly || (state != ResourceStates::Unknown && getBufferStateFromRanges(buffer, existingRanges, resolvedRange) == state))
                return;
        }
        if(resolvedRange.byteOffset >= existingRanges->back().range.end()){
            if(resolvedRange.byteOffset == existingRanges->back().range.end() && existingRanges->back().state == state){
                existingRanges->back().range.byteSize += resolvedRange.byteSize;
                return;
            }
            existingRanges->push_back(BufferRangeState{ resolvedRange, state });
            return;
        }
    }

    constexpr usize s_InlineMergedCapacity = 8u;
    Array<BufferRangeState, s_InlineMergedCapacity> inlineMerged{};
    usize inlineMergedCount = 0u;
    BufferRangeStates overflowMerged{ m_context.objectArena };
    b8 usingOverflow = false;
    if(existingRanges->size() * 2u + 1u > s_InlineMergedCapacity){
        usingOverflow = true;
        overflowMerged.reserve(existingRanges->size() * 2u + 1u);
    }
    const auto append = [&](const BufferRange piece, const ResourceStates::Mask pieceState){
        if(!piece.hasExtent())
            return;
        if(usingOverflow){
            if(!overflowMerged.empty() && overflowMerged.back().range.end() == piece.byteOffset && overflowMerged.back().state == pieceState)
                overflowMerged.back().range.byteSize += piece.byteSize;
            else
                overflowMerged.push_back(BufferRangeState{ piece, pieceState });
            return;
        }
        if(inlineMergedCount != 0u && inlineMerged[inlineMergedCount - 1u].range.end() == piece.byteOffset && inlineMerged[inlineMergedCount - 1u].state == pieceState){
            inlineMerged[inlineMergedCount - 1u].range.byteSize += piece.byteSize;
            return;
        }
        if(inlineMergedCount < s_InlineMergedCapacity){
            inlineMerged[inlineMergedCount++] = BufferRangeState{ piece, pieceState };
            return;
        }
        usingOverflow = true;
        overflowMerged.reserve(existingRanges->size() * 2u + 1u);
        overflowMerged.assign(inlineMerged.data(), inlineMergedCount);
        overflowMerged.push_back(BufferRangeState{ piece, pieceState });
    };

    u64 cursor = resolvedRange.byteOffset;
    for(const BufferRangeState& entry : *existingRanges){
        if(entry.range.end() <= resolvedRange.byteOffset){
            append(entry.range, entry.state);
            continue;
        }
        if(entry.range.byteOffset >= resolvedRange.end()){
            if(cursor < resolvedRange.end()){
                append(BufferRange(cursor, resolvedRange.end() - cursor), state);
                cursor = resolvedRange.end();
            }
            append(entry.range, entry.state);
            continue;
        }

        if(entry.range.byteOffset < resolvedRange.byteOffset)
            append(BufferRange(entry.range.byteOffset, resolvedRange.byteOffset - entry.range.byteOffset), entry.state);
        const BufferRange overlap = entry.range.intersect(resolvedRange);
        if(cursor < overlap.byteOffset)
            append(BufferRange(cursor, overlap.byteOffset - cursor), state);
        append(overlap, seedOnly ? entry.state : state);
        cursor = overlap.end();
        if(entry.range.end() > resolvedRange.end())
            append(BufferRange(resolvedRange.end(), entry.range.end() - resolvedRange.end()), entry.state);
    }
    if(cursor < resolvedRange.end())
        append(BufferRange(cursor, resolvedRange.end() - cursor), state);

    if(usingOverflow)
        existingRanges->assign(overflowMerged.data(), overflowMerged.size());
    else
        existingRanges->assign(inlineMerged.data(), inlineMergedCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setBufferState(Buffer* bufferResource, ResourceStates::Mask stateBits, const b8 forceMemoryDependency, const BufferRange range){
    if(!bufferResource)
        return;
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("set buffer state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(!validateBufferForGpuState(bufferResource, stateBits, s_OperationName))
        return;

    Buffer& buffer = *bufferResource;
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc)){
        rejectCommandRecording(s_OperationName, __hidden_state_tracking_buffers::s_ByteRangeOutsideBufferReason);
        return;
    }
    setBufferStateValidated(buffer, stateBits, forceMemoryDependency, resolvedRange);
}

void CommandList::setBufferStateValidated(Buffer& buffer, const ResourceStates::Mask stateBits, const b8 forceMemoryDependency, const BufferRange& resolvedRange){
    if(m_hasPendingBufferCopies)
        flushPendingBufferCopies();
    m_graphicsBuffersValidatedInPass = false;

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(&buffer);
    const ResourceStates::Mask fallbackState = buffer.isRetainedStateKnown() ? buffer.m_creationDesc.initialState : ResourceStates::Unknown;
    const b8 uavBarrierEnabled = ResourceStates::HasUnorderedAccess(stateBits) && m_stateTracker.isUavBarrierEnabledForBuffer(buffer);
    const usize firstBarrierIndex = m_pendingBufferBarriers.size();
    const auto append = [&](const BufferRange piece, const ResourceStates::Mask oldState){
        if(!piece.hasExtent())
            return;
        if(VulkanDetail::HasBufferDeviceWriteState(oldState) || VulkanDetail::HasBufferDeviceWriteState(stateBits))
            registerHostReadbackBuffer(buffer);
        if(VulkanStateTrackingDetail::NeedsResourceStateBarrier(oldState, stateBits, uavBarrierEnabled, forceMemoryDependency)){
            m_pendingBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferStateBarrier(buffer.m_buffer, piece, oldState, stateBits, m_context.extensions.khrRayTracingPipeline));
        }
    };

    if(permanentState != ResourceStates::Unknown)
        append(resolvedRange, permanentState);
    else{
        StateTracker::BufferRangeStates* const existingRanges = m_stateTracker.findBufferRangesCachedMutable(buffer);
        u64 cursor = resolvedRange.byteOffset;
        if(existingRanges){
            for(const StateTracker::BufferRangeState& entry : *existingRanges){
                const BufferRange overlap = entry.range.intersect(resolvedRange);
                if(!overlap.hasExtent())
                    continue;
                if(cursor < overlap.byteOffset)
                    append(BufferRange(cursor, overlap.byteOffset - cursor), fallbackState);
                append(overlap, entry.state);
                cursor = overlap.end();
            }
        }
        if(cursor < resolvedRange.end())
            append(BufferRange(cursor, resolvedRange.end() - cursor), fallbackState);
        m_stateTracker.beginTrackingTransientBufferWithRanges(buffer, stateBits, resolvedRange, false, existingRanges);
    }
    retainResource(&buffer);

    const usize newBarrierCount = m_pendingBufferBarriers.size() - firstBarrierIndex;
    if(!m_enableAutomaticBarriers || newBarrierCount == 0u)
        return;
    executePipelineBarrierValidated({}, {}, { m_pendingBufferBarriers.data() + firstBarrierIndex, newBarrierCount }, FOUNDATION_TEXT("set buffer state"));
    m_pendingBufferBarriers.resize(firstBarrierIndex);
}

void CommandList::queueWriteBufferCopyAndBarrier(Buffer& buffer, const VkBuffer srcVkBuf, const u64 stagingOffset, const u64 destOffsetBytes, const u64 dataSize){
    const VkBuffer dstVkBuf = buffer.m_buffer;
    const BufferRange resolvedRange(destOffsetBytes, dataSize);
    if(m_hasPendingBufferCopies){
        b8 needFlush = !m_enableAutomaticBarriers || m_pendingCopySrc != srcVkBuf || m_pendingCopyDst != dstVkBuf || m_pendingBufferCopyCount >= s_MaxPendingBufferCopies;
        if(!needFlush){
            for(u32 copyIndex = 0u; copyIndex < m_pendingBufferCopyCount; ++copyIndex){
                if(UnsignedRangesOverlap(destOffsetBytes, dataSize, m_pendingBufferCopies[copyIndex].dstOffset, m_pendingBufferCopies[copyIndex].size)){
                    needFlush = true;
                    break;
                }
            }
        }
        if(needFlush)
            flushPendingBufferCopies();
    }

    m_graphicsBuffersValidatedInPass = false;
    registerHostReadbackBuffer(buffer);

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(&buffer);
    const ResourceStates::Mask fallbackState = buffer.isRetainedStateKnown() ? buffer.m_creationDesc.initialState : ResourceStates::Unknown;
    const GpuQueueCapability::Mask exactQueueCapabilities = m_hasCachedPhysicalQueueInfo ? m_cachedPhysicalQueueInfo.capabilities : GpuQueueCapability::None;
    const usize firstManualBarrierIndex = m_pendingBufferBarriers.size();
    b8 overflowedCopyBarriers = false;

    const auto appendBarrier = [&](const BufferRange piece, const ResourceStates::Mask oldState){
        if(!piece.hasExtent())
            return;
        if(!VulkanStateTrackingDetail::NeedsResourceStateBarrier(oldState, ResourceStates::CopyDest, false, false))
            return;

        VkBufferMemoryBarrier2 barrier = VulkanStateTrackingDetail::BuildBufferStateBarrier(dstVkBuf, piece, oldState, ResourceStates::CopyDest, m_context.extensions.khrRayTracingPipeline);
        if(!m_enableAutomaticBarriers){
            m_pendingBufferBarriers.push_back(barrier);
            return;
        }

        if(!m_universalGraphicsCompute){
            QueueBarrierScopePolicy::Normalize(exactQueueCapabilities, barrier.srcStageMask, barrier.srcAccessMask);
            QueueBarrierScopePolicy::Normalize(exactQueueCapabilities, barrier.dstStageMask, barrier.dstAccessMask);
        }
        if(m_pendingCopyBarrierCount != 0u){
            VkBufferMemoryBarrier2& last = m_pendingCopyBarriers[m_pendingCopyBarrierCount - 1u];
            if(
                last.buffer == barrier.buffer
                && last.offset + last.size == barrier.offset
                && last.srcStageMask == barrier.srcStageMask
                && last.srcAccessMask == barrier.srcAccessMask
                && last.dstStageMask == barrier.dstStageMask
                && last.dstAccessMask == barrier.dstAccessMask
            ){
                last.size += barrier.size;
                return;
            }
        }
        if(m_pendingCopyBarrierCount < s_MaxPendingBufferCopies && !overflowedCopyBarriers){
            m_pendingCopyBarriers[m_pendingCopyBarrierCount++] = barrier;
            return;
        }
        overflowedCopyBarriers = true;
        m_pendingBufferBarriers.push_back(barrier);
    };

    if(permanentState != ResourceStates::Unknown)
        appendBarrier(resolvedRange, permanentState);
    else{
        StateTracker::BufferRangeStates* const existingRanges = m_stateTracker.findBufferRangesCachedMutable(buffer);
        u64 cursor = resolvedRange.byteOffset;
        if(existingRanges){
            for(const StateTracker::BufferRangeState& entry : *existingRanges){
                const BufferRange overlap = entry.range.intersect(resolvedRange);
                if(!overlap.hasExtent())
                    continue;
                if(cursor < overlap.byteOffset)
                    appendBarrier(BufferRange(cursor, overlap.byteOffset - cursor), fallbackState);
                appendBarrier(overlap, entry.state);
                cursor = overlap.end();
            }
        }
        if(cursor < resolvedRange.end())
            appendBarrier(BufferRange(cursor, resolvedRange.end() - cursor), fallbackState);
        m_stateTracker.beginTrackingTransientBufferWithRanges(buffer, ResourceStates::CopyDest, resolvedRange, false, existingRanges);
    }

    retainResource(&buffer);

    if(!m_enableAutomaticBarriers || overflowedCopyBarriers){
        if(m_hasPendingBufferCopies || m_pendingCopyBarrierCount != 0u)
            flushPendingBufferCopies();
        if(m_enableAutomaticBarriers && m_pendingBufferBarriers.size() > firstManualBarrierIndex){
            executePipelineBarrierValidated({}, {}, { m_pendingBufferBarriers.data() + firstManualBarrierIndex, m_pendingBufferBarriers.size() - firstManualBarrierIndex }, FOUNDATION_TEXT("write buffer"));
            m_pendingBufferBarriers.resize(firstManualBarrierIndex);
        }
        const VkBufferCopy region{ stagingOffset, destOffsetBytes, dataSize };
        m_context.deviceDispatch.vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, srcVkBuf, dstVkBuf, 1u, &region);
        return;
    }

    if(m_hasPendingBufferCopies){
        VkBufferCopy& last = m_pendingBufferCopies[m_pendingBufferCopyCount - 1u];
        if(last.srcOffset + last.size == stagingOffset && last.dstOffset + last.size == destOffsetBytes){
            last.size += dataSize;
            return;
        }
        m_pendingBufferCopies[m_pendingBufferCopyCount++] = VkBufferCopy{ stagingOffset, destOffsetBytes, dataSize };
        return;
    }

    m_pendingCopySrc = srcVkBuf;
    m_pendingCopyDst = dstVkBuf;
    m_pendingBufferCopies[0u] = VkBufferCopy{ stagingOffset, destOffsetBytes, dataSize };
    m_pendingBufferCopyCount = 1u;
    m_hasPendingBufferCopies = true;
}

void CommandList::flushPendingBufferCopies()noexcept{
    if(!m_hasPendingBufferCopies && m_pendingCopyBarrierCount == 0u)
        return;

    const u32 copyCount = m_pendingBufferCopyCount;
    const u32 barrierCount = m_pendingCopyBarrierCount;
    const VkBuffer srcBuffer = m_pendingCopySrc;
    const VkBuffer dstBuffer = m_pendingCopyDst;
    resetPendingBufferCopies();

    if(m_commandRecordingFailed || !m_currentCmdBuf || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE)
        return;
    if(barrierCount != 0u){
        auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        depInfo.bufferMemoryBarrierCount = barrierCount;
        depInfo.pBufferMemoryBarriers = m_pendingCopyBarriers.data();
        m_context.deviceDispatch.vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);
    }
    if(copyCount != 0u)
        m_context.deviceDispatch.vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, srcBuffer, dstBuffer, copyCount, m_pendingBufferCopies.data());
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const CommandQueue::Enum destinationQueue, const BufferRange range){
    if(bufferResource)
        releaseBufferOwnership(bufferResource, m_device.getPrimaryPhysicalQueue(destinationQueue), range);
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const GpuPhysicalQueueId destinationQueue, const BufferRange range){
    BufferOwnershipTransfer(*this).release(bufferResource, destinationQueue, range);
}

void CommandList::beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits, const BufferRange range){
    if(!buffer)
        return;
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("begin tracking buffer state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(s_OperationName, VulkanStateTrackingDetail::s_InitialStateUnknownReason);
        return;
    }
    if(!validateBufferForGpuState(buffer, stateBits, s_OperationName))
        return;
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc)){
        rejectCommandRecording(s_OperationName, __hidden_state_tracking_buffers::s_ByteRangeOutsideBufferReason);
        return;
    }
    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("initial state conflicts with the permanent buffer state"));
        return;
    }
    m_stateTracker.beginTrackingBuffer(buffer, stateBits, range);
    retainResource(buffer);
}

void CommandList::seedBufferState(Buffer* buffer, ResourceStates::Mask stateBits, const BufferRange range){
    if(!buffer)
        return;
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("seed buffer state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(s_OperationName, VulkanStateTrackingDetail::s_InitialStateUnknownReason);
        return;
    }
    if(!validateBufferForGpuState(buffer, stateBits, s_OperationName))
        return;
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc)){
        rejectCommandRecording(s_OperationName, __hidden_state_tracking_buffers::s_ByteRangeOutsideBufferReason);
        return;
    }
    if(!m_stateTracker.isPermanentBuffer(*buffer))
        m_stateTracker.beginTrackingTransientBuffer(*buffer, stateBits, range, true);
    retainResource(buffer);
}

ResourceStates::Mask CommandList::getBufferState(Buffer* buffer, const BufferRange range)noexcept{
    const GraphPublicationReadOwnership ownership(*this);
    if(!ownership.m_readable)
        return ResourceStates::Unknown;
    return m_stateTracker.getBufferState(buffer, range);
}

b8 CommandList::hasExplicitBufferState(Buffer* buffer, const BufferRange range, const b8 requireKnown)const noexcept{
    const GraphPublicationReadOwnership ownership(*this);
    if(!ownership.m_readable)
        return false;
    return m_stateTracker.hasExplicitBufferState(buffer, range, requireKnown);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

