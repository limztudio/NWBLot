// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQueryHandle Device::createEventQuery(){
    auto* query = NewArenaObject<EventQuery>(*m_context.objectArena, m_context);
    return EventQueryHandle(query, EventQueryHandle::deleter_type(m_context.objectArena), AdoptRef);
}

void Device::setEventQuery(IEventQuery* _query, CommandQueue::Enum queue){
    auto* query = static_cast<EventQuery*>(_query);

    vkResetFences(m_context.device, 1, &query->m_fence);

    Queue* q = getQueue(queue);
    if(q){
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(q->getVkQueue(), 1, &submitInfo, query->m_fence);
    }

    query->m_started = true;
}

bool Device::pollEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = static_cast<EventQuery*>(_query);
    if(!query->m_started)
        return true;

    res = vkGetFenceStatus(m_context.device, query->m_fence);
    return res == VK_SUCCESS;
}

void Device::waitEventQuery(IEventQuery* _query){
    auto* query = static_cast<EventQuery*>(_query);
    if(!query->m_started)
        return;

    vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
}

void Device::resetEventQuery(IEventQuery* _query){
    auto* query = static_cast<EventQuery*>(_query);
    vkResetFences(m_context.device, 1, &query->m_fence);
    query->m_started = false;
}

TimerQueryHandle Device::createTimerQuery(){
    auto* query = NewArenaObject<TimerQuery>(*m_context.objectArena, m_context);
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(m_context.objectArena), AdoptRef);
}

bool Device::pollTimerQuery(ITimerQuery* _query){
    auto* query = static_cast<TimerQuery*>(_query);
    return query->m_resolved;
}

f32 Device::getTimerQueryTime(ITimerQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = static_cast<TimerQuery*>(_query);

    if(!query->m_resolved)
        return 0.f;

    u64 timestamps[2];
    res = vkGetQueryPoolResults(m_context.device, query->m_queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT);
    if(res == VK_SUCCESS){
        u64 diff = timestamps[1] - timestamps[0];
        f32 timestampPeriod = m_context.physicalDeviceProperties.limits.timestampPeriod;
        return static_cast<f32>(diff) * timestampPeriod * 1e-9f; // Convert to seconds
    }

    return 0.f;
}

void Device::resetTimerQuery(ITimerQuery* _query){
    auto* query = static_cast<TimerQuery*>(_query);
    vkResetQueryPool(m_context.device, query->m_queryPool, 0, 2);
    query->m_started = false;
    query->m_resolved = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->m_queryPool, 0);
    query->m_started = true;
}

void CommandList::endTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->m_queryPool, 1);
    query->m_resolved = true;
}

void CommandList::beginMarker(const Name& name){
    if(m_context.extensions.EXT_debug_utils){
        VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        label.pLabelName = name.c_str();
        vkCmdBeginDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf, &label);
    }
    else if(m_context.extensions.EXT_debug_marker){
        VkDebugMarkerMarkerInfoEXT markerInfo = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT };
        markerInfo.pMarkerName = name.c_str();
        vkCmdDebugMarkerBeginEXT(m_currentCmdBuf->m_cmdBuf, &markerInfo);
    }

    if(m_device.isAftermathEnabled()){
        const usize aftermathMarker = m_aftermathMarkerTracker.pushEvent(name.c_str());
        vkCmdSetCheckpointNV(m_currentCmdBuf->m_cmdBuf, reinterpret_cast<const void*>(aftermathMarker));
    }
}

void CommandList::endMarker(){
    if(m_context.extensions.EXT_debug_utils){
        vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
    }
    else if(m_context.extensions.EXT_debug_marker){
        vkCmdDebugMarkerEndEXT(m_currentCmdBuf->m_cmdBuf);
    }

    if(m_device.isAftermathEnabled()){
        m_aftermathMarkerTracker.popEvent();
    }
}

void CommandList::setEventQuery(IEventQuery* _query, CommandQueue::Enum){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->m_fence != VK_NULL_HANDLE)
        vkResetFences(m_context.device, 1, &query->m_fence);

    if(m_currentCmdBuf)
        m_currentCmdBuf->m_signalFence = query->m_fence;

    query->m_started = true;
}

void CommandList::resetEventQuery(IEventQuery* _query){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->m_fence != VK_NULL_HANDLE)
        vkResetFences(m_context.device, 1, &query->m_fence);

    query->m_started = false;
}

void CommandList::waitEventQuery(IEventQuery* _query){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->m_fence != VK_NULL_HANDLE)
        vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

