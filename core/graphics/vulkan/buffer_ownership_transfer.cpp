// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "buffer_ownership_transfer.h"
#include "state_tracking_detail.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer_ownership_transfer{

static constexpr TStringView s_ByteRangeOutsideBufferReason = NWB_TEXT("byte range is empty or outside the buffer");

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::BufferOwnershipTransfer::release(Buffer* bufferResource, const GpuPhysicalQueueId destinationQueue, const BufferRange range){
    if(!bufferResource)
        return;
    constexpr TStringView s_OperationName = NWB_TEXT("release buffer ownership");
    constexpr TStringView s_FinalRangeStateUnknownReason = NWB_TEXT("final byte range state is unknown");
    if(!m_command.validateCommandRecordingScope(s_OperationName))
        return;
    Buffer& buffer = *bufferResource;
    if(!m_command.isBufferReadyForCommandQueue(&buffer)){
        m_command.rejectCommandRecording(s_OperationName, s_BufferNotReadyForQueueReason);
        return;
    }
    if(m_command.m_stateTracker.isPermanentBuffer(buffer)){
        m_command.rejectCommandRecording(s_OperationName, NWB_TEXT("permanently tracked buffers cannot transfer ownership"));
        return;
    }
    if(buffer.m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
        m_command.rejectCommandRecording(s_OperationName, NWB_TEXT("concurrently shared buffers do not have exclusive ownership"));
        return;
    }
    const BufferRange resolvedRange = range.resolve(buffer.m_creationDesc);
    if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(range, buffer.m_creationDesc)){
        m_command.rejectCommandRecording(s_OperationName, __hidden_buffer_ownership_transfer::s_ByteRangeOutsideBufferReason);
        return;
    }
    if(!m_command.m_device.getQueue(destinationQueue)){
        m_command.rejectCommandRecording(s_OperationName, VulkanStateTrackingDetail::s_DestinationQueueUnavailableReason);
        return;
    }

    const auto existing = m_command.m_bufferOwnershipReleaseDestinations.find(MakeNotNull(&buffer));
    if(existing != m_command.m_bufferOwnershipReleaseDestinations.end()){
        for(const BufferOwnershipRelease& release : existing.value()){
            if(release.range.overlaps(resolvedRange) && release.destinationQueue != destinationQueue){
                m_command.rejectCommandRecording(s_OperationName, NWB_TEXT("byte range already targets a conflicting destination queue"));
                return;
            }
        }
    }

    const ResourceStates::Mask initialState = buffer.resolveTaskGraphImportInitialState();
    if(initialState == ResourceStates::Unknown && !m_command.m_stateTracker.hasExplicitBufferState(&buffer, resolvedRange)){
        m_command.rejectCommandRecording(s_OperationName, s_FinalRangeStateUnknownReason);
        return;
    }
    if(initialState != ResourceStates::Unknown && !m_command.validateBufferForGpuState(&buffer, initialState, s_OperationName))
        return;
    const auto tracked = m_command.m_stateTracker.m_bufferStates.find(MakeNotNull(&buffer));
    if(tracked != m_command.m_stateTracker.m_bufferStates.end()){
        for(const StateTracker::BufferRangeState& entry : tracked.value()){
            if(!entry.range.overlaps(resolvedRange))
                continue;
            if(entry.state == ResourceStates::Unknown){
                m_command.rejectCommandRecording(s_OperationName, s_FinalRangeStateUnknownReason);
                return;
            }
            if(!m_command.validateBufferForGpuState(&buffer, entry.state, s_OperationName))
                return;
        }
    }
    if(initialState != ResourceStates::Unknown)
        m_command.m_stateTracker.beginTrackingTransientBuffer(buffer, initialState, resolvedRange, true);

    Vector<BufferOwnershipRelease, Alloc::GlobalArena> releases{m_command.m_context.objectArena};
    releases.reserve(existing == m_command.m_bufferOwnershipReleaseDestinations.end() ? 1u : existing.value().size() + 1u);
    BufferRange mergedRange = resolvedRange;
    if(existing != m_command.m_bufferOwnershipReleaseDestinations.end()){
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
    Sort(releases.begin(), releases.end(), [](const BufferOwnershipRelease& lhs, const BufferOwnershipRelease& rhs)noexcept{
        return lhs.range.byteOffset < rhs.range.byteOffset;
    });
    m_command.m_bufferOwnershipReleaseDestinations.insert_or_assign(MakeNotNull(&buffer), Move(releases));
    m_command.retainResource(&buffer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

