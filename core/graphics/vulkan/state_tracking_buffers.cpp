// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


bool IsBufferStateRangeValid(const BufferRange range, const BufferDesc& description)noexcept{
    return
        range.hasExtent()
        && range.byteOffset < description.byteSize
        && (range.byteSize == BufferRange::AllBytes || range.byteSize <= description.byteSize - range.byteOffset)
    ;
}

VkBufferMemoryBarrier2 BuildBufferStateBarrier(
    const VkBuffer buffer,
    const BufferRange range,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ResourceStates::Mask StateTracker::getBufferState(Buffer* buffer, const BufferRange range)const{
    if(!buffer || !VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc))
        return ResourceStates::Unknown;

    const auto permanent = m_permanentBufferStates.find(buffer);
    if(permanent != m_permanentBufferStates.end())
        return permanent.value().state;

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientBufferState(*buffer, state, range) ? state : ResourceStates::Unknown;
}

bool StateTracker::hasExplicitBufferState(Buffer* buffer, const BufferRange range, const bool requireKnown)const{
    if(!buffer || !VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc))
        return false;
    const BufferRange resolvedRange = range.resolve(buffer->m_creationDesc);
    if(!resolvedRange.hasExtent())
        return false;
    if(m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end())
        return true;
    const auto found = m_bufferStates.find(buffer);
    if(found == m_bufferStates.end())
        return false;

    u64 cursor = resolvedRange.byteOffset;
    for(const BufferRangeState& entry : found.value()){
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

void StateTracker::beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state, const BufferRange range){
    if(!buffer || m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end())
        return;
    beginTrackingTransientBuffer(*buffer, state, range);
}

bool StateTracker::getTransientBufferState(Buffer& buffer, ResourceStates::Mask& outState, const BufferRange range)const{
    outState = ResourceStates::Unknown;
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc))
        return false;
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!resolvedRange.hasExtent())
        return false;

    const ResourceStates::Mask fallbackState = buffer.isRetainedStateKnown() ? buffer.m_creationDesc.initialState : ResourceStates::Unknown;
    const auto found = m_bufferStates.find(&buffer);
    if(found == m_bufferStates.end()){
        outState = fallbackState;
        return true;
    }

    bool first = true;
    const auto accumulate = [&](const ResourceStates::Mask state){
        if(first){
            outState = state;
            first = false;
            return true;
        }
        if(state != outState){
            outState = ResourceStates::Unknown;
            return false;
        }
        return true;
    };
    u64 cursor = resolvedRange.byteOffset;
    for(const BufferRangeState& entry : found.value()){
        const BufferRange overlap = entry.range.intersect(resolvedRange);
        if(!overlap.hasExtent())
            continue;
        if(overlap.byteOffset > cursor && !accumulate(fallbackState))
            return true;
        if(!accumulate(entry.state))
            return true;
        cursor = overlap.end();
    }
    if(cursor < resolvedRange.end() && !accumulate(fallbackState))
        return true;
    return true;
}

void StateTracker::beginTrackingTransientBuffer(
    Buffer& buffer,
    ResourceStates::Mask state,
    const BufferRange range,
    const bool seedOnly
){
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc))
        return;
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!resolvedRange.hasExtent())
        return;

    auto found = m_bufferStates.find(&buffer);
    if(found != m_bufferStates.end()){
        if(hasExplicitBufferState(&buffer, resolvedRange)){
            if(seedOnly || (state != ResourceStates::Unknown && getBufferState(&buffer, resolvedRange) == state))
                return;
        }
        if(!seedOnly && resolvedRange.isEntireBuffer(buffer.m_creationDesc)){
            found.value().resize(1u);
            found.value().front() = BufferRangeState{resolvedRange, state};
            return;
        }
    }
    BufferRangeStates updated{m_context.objectArena};
    updated.reserve(found == m_bufferStates.end() ? 1u : found.value().size() * 2u + 1u);
    const auto append = [&](const BufferRange piece, const ResourceStates::Mask pieceState){
        if(!piece.hasExtent())
            return;
        if(!updated.empty() && updated.back().range.end() == piece.byteOffset && updated.back().state == pieceState)
            updated.back().range.byteSize += piece.byteSize;
        else
            updated.push_back(BufferRangeState{piece, pieceState});
    };

    u64 cursor = resolvedRange.byteOffset;
    if(found != m_bufferStates.end()){
        for(const BufferRangeState& entry : found.value()){
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
    }
    if(cursor < resolvedRange.end())
        append(BufferRange(cursor, resolvedRange.end() - cursor), state);
    m_bufferStates.insert_or_assign(&buffer, Move(updated));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setBufferState(
    Buffer* bufferResource,
    ResourceStates::Mask stateBits,
    const bool forceMemoryDependency,
    const BufferRange range
){
    if(!bufferResource)
        return;
    constexpr TStringView s_OperationName = NWB_TEXT("set buffer state");
    if(!validateCommandRecordingScope(s_OperationName.data()))
        return;
    if(!validateBufferForGpuState(bufferResource, stateBits, s_OperationName.data()))
        return;

    Buffer& buffer = *bufferResource;
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("byte range is empty or outside the buffer"));
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(&buffer);
    const ResourceStates::Mask fallbackState = buffer.isRetainedStateKnown() ? buffer.m_creationDesc.initialState : ResourceStates::Unknown;
    const bool uavBarrierEnabled = ResourceStates::HasUnorderedAccess(stateBits) && m_stateTracker.isUavBarrierEnabledForBuffer(buffer);
    const usize firstBarrierIndex = m_pendingBufferBarriers.size();
    const auto append = [&](const BufferRange piece, const ResourceStates::Mask oldState){
        if(!piece.hasExtent())
            return;
        if(VulkanDetail::HasBufferDeviceWriteState(oldState) || VulkanDetail::HasBufferDeviceWriteState(stateBits))
            registerHostReadbackBuffer(buffer);
        if(VulkanStateTrackingDetail::NeedsResourceStateBarrier(oldState, stateBits, uavBarrierEnabled, forceMemoryDependency)){
            m_pendingBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferStateBarrier(
                buffer.m_buffer, piece, oldState, stateBits, m_context.extensions.KHR_ray_tracing_pipeline
            ));
        }
    };

    const auto found = m_stateTracker.m_bufferStates.find(&buffer);
    if(permanentState != ResourceStates::Unknown)
        append(resolvedRange, permanentState);
    else{
        u64 cursor = resolvedRange.byteOffset;
        if(found != m_stateTracker.m_bufferStates.end()){
            for(const StateTracker::BufferRangeState& entry : found.value()){
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
        m_stateTracker.beginTrackingTransientBuffer(buffer, stateBits, resolvedRange);
    }
    retainResource(&buffer);

    const usize newBarrierCount = m_pendingBufferBarriers.size() - firstBarrierIndex;
    if(!m_enableAutomaticBarriers || newBarrierCount == 0u)
        return;
    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(newBarrierCount);
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data() + firstBarrierIndex;
    executePipelineBarrier(depInfo);
    m_pendingBufferBarriers.resize(firstBarrierIndex);
}

void CommandList::releaseBufferOwnership(
    Buffer* buffer,
    const CommandQueue::Enum destinationQueue,
    const BufferRange range
){
    if(buffer)
        releaseBufferOwnership(buffer, m_device.getPrimaryPhysicalQueue(destinationQueue), range);
}

void CommandList::releaseBufferOwnership(
    Buffer* bufferResource,
    const GpuPhysicalQueueId destinationQueue,
    const BufferRange range
){
    if(!bufferResource)
        return;
    constexpr TStringView s_OperationName = NWB_TEXT("release buffer ownership");
    if(!validateCommandRecordingScope(s_OperationName.data()))
        return;
    Buffer& buffer = *bufferResource;
    if(!isBufferReadyForCommandQueue(&buffer)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("buffer is not ready for this exact command queue"));
        return;
    }
    if(m_stateTracker.isPermanentBuffer(buffer)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("permanently tracked buffers cannot transfer ownership"));
        return;
    }
    if(buffer.m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("concurrently shared buffers do not have exclusive ownership"));
        return;
    }
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("byte range is empty or outside the buffer"));
        return;
    }
    if(!m_device.getQueue(destinationQueue)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("destination queue is unavailable"));
        return;
    }

    const auto existing = m_bufferOwnershipReleaseDestinations.find(&buffer);
    if(existing != m_bufferOwnershipReleaseDestinations.end()){
        for(const BufferOwnershipRelease& release : existing.value()){
            if(release.range.overlaps(resolvedRange) && release.destinationQueue != destinationQueue){
                rejectCommandRecording(s_OperationName.data(), NWB_TEXT("byte range already targets a conflicting destination queue"));
                return;
            }
        }
    }

    const ResourceStates::Mask initialState = buffer.resolveTaskGraphImportInitialState();
    if(initialState == ResourceStates::Unknown && !m_stateTracker.hasExplicitBufferState(&buffer, resolvedRange)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("final byte range state is unknown"));
        return;
    }
    if(initialState != ResourceStates::Unknown && !validateBufferForGpuState(&buffer, initialState, s_OperationName.data()))
        return;
    const auto tracked = m_stateTracker.m_bufferStates.find(&buffer);
    if(tracked != m_stateTracker.m_bufferStates.end()){
        for(const StateTracker::BufferRangeState& entry : tracked.value()){
            if(!entry.range.overlaps(resolvedRange))
                continue;
            if(entry.state == ResourceStates::Unknown){
                rejectCommandRecording(s_OperationName.data(), NWB_TEXT("final byte range state is unknown"));
                return;
            }
            if(!validateBufferForGpuState(&buffer, entry.state, s_OperationName.data()))
                return;
        }
    }
    if(initialState != ResourceStates::Unknown)
        m_stateTracker.beginTrackingTransientBuffer(buffer, initialState, resolvedRange, true);

    Vector<BufferOwnershipRelease, Alloc::GlobalArena> releases{m_context.objectArena};
    releases.reserve(existing == m_bufferOwnershipReleaseDestinations.end() ? 1u : existing.value().size() + 1u);
    BufferRange mergedRange = resolvedRange;
    if(existing != m_bufferOwnershipReleaseDestinations.end()){
        for(const BufferOwnershipRelease& release : existing.value()){
            if(release.destinationQueue == destinationQueue && release.range.overlaps(mergedRange)){
                const u64 begin = Min(release.range.byteOffset, mergedRange.byteOffset);
                mergedRange = BufferRange(begin, Max(release.range.end(), mergedRange.end()) - begin);
            }
            else
                releases.push_back(release);
        }
    }
    releases.push_back(BufferOwnershipRelease{mergedRange, destinationQueue});
    Sort(releases.begin(), releases.end(), [](const BufferOwnershipRelease& lhs, const BufferOwnershipRelease& rhs){
        return lhs.range.byteOffset < rhs.range.byteOffset;
    });
    m_bufferOwnershipReleaseDestinations.insert_or_assign(&buffer, Move(releases));
    retainResource(&buffer);
}

void CommandList::beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits, const BufferRange range){
    if(!buffer)
        return;
    constexpr TStringView s_OperationName = NWB_TEXT("begin tracking buffer state");
    if(!validateCommandRecordingScope(s_OperationName.data()))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("initial state cannot be unknown"));
        return;
    }
    if(!validateBufferForGpuState(buffer, stateBits, s_OperationName.data()))
        return;
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("byte range is empty or outside the buffer"));
        return;
    }
    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("initial state conflicts with the permanent buffer state"));
        return;
    }
    m_stateTracker.beginTrackingBuffer(buffer, stateBits, range);
    retainResource(buffer);
}

void CommandList::seedBufferState(Buffer* buffer, ResourceStates::Mask stateBits, const BufferRange range){
    if(!buffer)
        return;
    constexpr TStringView s_OperationName = NWB_TEXT("seed buffer state");
    if(!validateCommandRecordingScope(s_OperationName.data()))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("initial state cannot be unknown"));
        return;
    }
    if(!validateBufferForGpuState(buffer, stateBits, s_OperationName.data()))
        return;
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer->m_creationDesc)){
        rejectCommandRecording(s_OperationName.data(), NWB_TEXT("byte range is empty or outside the buffer"));
        return;
    }
    if(!m_stateTracker.isPermanentBuffer(*buffer))
        m_stateTracker.beginTrackingTransientBuffer(*buffer, stateBits, range, true);
    retainResource(buffer);
}

ResourceStates::Mask CommandList::getBufferState(Buffer* buffer, const BufferRange range){
    const GraphPublicationReadOwnership ownership(*this);
    if(!ownership.m_readable)
        return ResourceStates::Unknown;
    return m_stateTracker.getBufferState(buffer, range);
}

bool CommandList::hasExplicitBufferState(Buffer* buffer, const BufferRange range, const bool requireKnown)const{
    const GraphPublicationReadOwnership ownership(*this);
    if(!ownership.m_readable)
        return false;
    return m_stateTracker.hasExplicitBufferState(buffer, range, requireKnown);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

