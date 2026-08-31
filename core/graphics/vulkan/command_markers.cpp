// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginMarker(const AStringView name){
    if(!validateCommandRecordingScope(NWB_TEXT("begin command-list marker")))
        return;

    ++m_markerDepth;

    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useNvCheckpoint = m_device.isGpuCrashDiagnosticsEnabled();
    const bool useAmdBreadcrumb = m_device.isAmdBreadcrumbEnabled();
    const bool useGpuMarkers = useNvCheckpoint || useAmdBreadcrumb;
    if(!useDebugUtils && !useGpuMarkers)
        return;

    const GraphicsString markerName(name, m_context.objectArena);

    if(useDebugUtils){
        auto label = VulkanDetail::MakeVkStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = markerName.c_str();
        m_context.instanceDispatch.vkCmdBeginDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf, &label);
    }

    // Both vendors share one nested-marker hash so resolveMarker works regardless of which (or both) is active.
    if(useGpuMarkers){
        const usize gpuCrashMarker = m_gpuCrashMarkerTracker.pushEvent(markerName.c_str());
        if(useNvCheckpoint)
            m_context.deviceDispatch.vkCmdSetCheckpointNV(m_currentCmdBuf->m_cmdBuf, reinterpret_cast<const void*>(gpuCrashMarker));
        if(useAmdBreadcrumb){
            const Device::AmdBreadcrumbWrite breadcrumb = m_device.reserveAmdBreadcrumb(
                m_creationDesc.physicalQueue,
                gpuCrashMarker
            );
            if(breadcrumb.valid){
                m_hostReadbackBarrierTracker.registerDeviceOwnedBuffer(breadcrumb.buffer);
                m_context.deviceDispatch.vkCmdWriteBufferMarkerAMD(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, breadcrumb.buffer, breadcrumb.offset, breadcrumb.marker);
            }
        }
    }
}

void CommandList::endMarker(){
    if(m_markerDepth == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring an unmatched command-list marker end"));
        return;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("end command-list marker")))
        return;

    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useGpuMarkers = m_device.isAnyGpuMarkerEnabled();

    if(useDebugUtils)
        m_context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);

    if(useGpuMarkers)
        m_gpuCrashMarkerTracker.popEvent();

    --m_markerDepth;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

