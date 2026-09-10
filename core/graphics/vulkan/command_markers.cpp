// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_markers{


class CrashMarkerRollback final : NoCopy{
public:
    explicit CrashMarkerRollback(GpuCrashMarkerTracker& tracker)noexcept
        : m_tracker(tracker)
    {}
    ~CrashMarkerRollback()noexcept{
        if(m_armed)
            m_tracker.popEvent();
    }


public:
    void arm()noexcept{ m_armed = true; }
    void disarm()noexcept{ m_armed = false; }


private:
    GpuCrashMarkerTracker& m_tracker;
    bool m_armed = false;
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginMarker(const AStringView name){
    const CommandMarkerRecordingToken token = beginMarkerLease(name);
    if(!token.valid())
        return;
}

void CommandList::endMarker(){
    if(!publicCommandStateAccessible())
        return;
    if(m_markerStack.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring an unmatched command-list marker end"));
        return;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("end command-list marker")))
        return;

    closeTopMarkerWithoutCallbacks();
}

void CommandList::abandonMarker()noexcept{
    if(!publicCommandStateAccessible() || m_markerStack.empty())
        return;

    closeTopMarkerWithoutCallbacks();
}


CommandMarkerRecordingToken CommandList::beginMarkerLease(const AStringView name){
    if(!validateCommandRecordingScope(NWB_TEXT("begin command-list marker")))
        return {};
    if(m_nextMarkerSerial == Limit<u64>::s_Max){
        rejectCommandRecording(NWB_TEXT("begin command-list marker"), NWB_TEXT("marker identity space is exhausted"));
        return {};
    }

    ++m_nextMarkerSerial;
    const CommandMarkerRecordingToken token{
        .recordingLeaseSerial = m_recordingLeaseSerial,
        .nativeRecordingID = m_nativeRecordingID,
        .markerSerial = m_nextMarkerSerial,
    };
    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useNvCheckpoint = m_device.isGpuCrashDiagnosticsEnabled();
    const bool useAmdBreadcrumb = m_device.isAmdBreadcrumbEnabled();
    const bool useGpuMarkers = useNvCheckpoint || useAmdBreadcrumb;
    if(!useDebugUtils && !useGpuMarkers){
        m_markerStack.push_back(MarkerStackEntry{ .token = token });
        return token;
    }

    const GraphicsString markerName(name, m_context.objectArena);
    usize gpuCrashMarker = 0u;
    Device::AmdBreadcrumbWrite breadcrumb;
    __hidden_command_markers::CrashMarkerRollback crashMarkerRollback(m_gpuCrashMarkerTracker);
    if(useGpuMarkers){
        gpuCrashMarker = m_gpuCrashMarkerTracker.pushEvent(markerName.c_str());
        crashMarkerRollback.arm();
        if(useAmdBreadcrumb){
            breadcrumb = m_device.reserveAmdBreadcrumb(m_creationDesc.physicalQueue, gpuCrashMarker);
            if(breadcrumb.valid)
                m_hostReadbackBarrierTracker.registerDeviceOwnedBuffer(breadcrumb.buffer);
        }
    }
    m_markerStack.push_back(MarkerStackEntry{
        .token = token,
        .usesDebugUtils = useDebugUtils,
        .usesGpuMarkers = useGpuMarkers,
    });
    crashMarkerRollback.disarm();

    if(useDebugUtils){
        auto label = VulkanDetail::MakeVkStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = markerName.c_str();
        m_context.instanceDispatch.vkCmdBeginDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf, &label);
    }

    // Both vendors share one marker hash so resolveMarker works with either active.
    if(useGpuMarkers){
        if(useNvCheckpoint)
            m_context.deviceDispatch.vkCmdSetCheckpointNV(m_currentCmdBuf->m_cmdBuf, reinterpret_cast<const void*>(gpuCrashMarker));
        if(breadcrumb.valid)
            m_context.deviceDispatch.vkCmdWriteBufferMarkerAMD(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, breadcrumb.buffer, breadcrumb.offset, breadcrumb.marker);
    }
    return token;
}

bool CommandList::endMarkerLease(const CommandMarkerRecordingToken& token){
    if(!token.valid())
        return true;
    if(!publicCommandStateAccessible())
        return false;
    if(!markerLeaseMatchesTop(token)){
        invalidateCommandRecording();
        return false;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("end owned command-list marker")))
        return false;

    closeTopMarkerWithoutCallbacks();
    return true;
}

void CommandList::abandonMarkerLease(const CommandMarkerRecordingToken& token)noexcept{
    if(!token.valid() || !publicCommandStateAccessible())
        return;
    if(!markerLeaseMatchesTop(token)){
        invalidateCommandRecording();
        return;
    }

    closeTopMarkerWithoutCallbacks();
}

bool CommandList::markerLeaseMatchesTop(const CommandMarkerRecordingToken& token)const noexcept{
    if(m_markerStack.empty())
        return false;

    const CommandMarkerRecordingToken& current = m_markerStack.back().token;
    return
        token.recordingLeaseSerial == m_recordingLeaseSerial
        && token.nativeRecordingID == m_nativeRecordingID
        && current.recordingLeaseSerial == token.recordingLeaseSerial
        && current.nativeRecordingID == token.nativeRecordingID
        && current.markerSerial == token.markerSerial
    ;
}

void CommandList::closeTopMarkerWithoutCallbacks()noexcept{
    if(!publicCommandStateAccessible() || m_markerStack.empty())
        return;

    const MarkerStackEntry marker = m_markerStack.back();
    if(
        m_isRecording
        && m_currentCmdBuf
        && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE
        && marker.usesDebugUtils
    )
        m_context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
    if(marker.usesGpuMarkers)
        m_gpuCrashMarkerTracker.popEvent();
    m_markerStack.pop_back();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

