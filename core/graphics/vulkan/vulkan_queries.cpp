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

    vkResetFences(m_context.device, 1, &query->fence);

    Queue* q = getQueue(queue);
    if(q){
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(q->getVkQueue(), 1, &submitInfo, query->fence);
    }

    query->started = true;
}

bool Device::pollEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = static_cast<EventQuery*>(_query);
    if(!query->started)
        return true;

    res = vkGetFenceStatus(m_context.device, query->fence);
    return res == VK_SUCCESS;
}

void Device::waitEventQuery(IEventQuery* _query){
    auto* query = static_cast<EventQuery*>(_query);
    if(!query->started)
        return;

    vkWaitForFences(m_context.device, 1, &query->fence, VK_TRUE, UINT64_MAX);
}

void Device::resetEventQuery(IEventQuery* _query){
    auto* query = static_cast<EventQuery*>(_query);
    vkResetFences(m_context.device, 1, &query->fence);
    query->started = false;
}

TimerQueryHandle Device::createTimerQuery(){
    auto* query = NewArenaObject<TimerQuery>(*m_context.objectArena, m_context);
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(m_context.objectArena), AdoptRef);
}

bool Device::pollTimerQuery(ITimerQuery* _query){
    auto* query = static_cast<TimerQuery*>(_query);
    return query->resolved;
}

f32 Device::getTimerQueryTime(ITimerQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = static_cast<TimerQuery*>(_query);

    if(!query->resolved)
        return 0.f;

    u64 timestamps[2];
    res = vkGetQueryPoolResults(m_context.device, query->queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT);
    if(res == VK_SUCCESS){
        u64 diff = timestamps[1] - timestamps[0];
        f32 timestampPeriod = m_context.physicalDeviceProperties.limits.timestampPeriod;
        return static_cast<f32>(diff) * timestampPeriod * 1e-9f; // Convert to seconds
    }

    return 0.f;
}

void Device::resetTimerQuery(ITimerQuery* _query){
    auto* query = static_cast<TimerQuery*>(_query);
    vkResetQueryPool(m_context.device, query->queryPool, 0, 2);
    query->started = false;
    query->resolved = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);

    vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->queryPool, 0);
    query->started = true;
}

void CommandList::endTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);

    vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->queryPool, 1);
    query->resolved = true;
}

void CommandList::beginMarker(const Name& name){
    if(m_context.extensions.EXT_debug_utils){
        VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        label.pLabelName = name.c_str();
        vkCmdBeginDebugUtilsLabelEXT(currentCmdBuf->cmdBuf, &label);
    }
    else if(m_context.extensions.EXT_debug_marker){
        VkDebugMarkerMarkerInfoEXT markerInfo = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT };
        markerInfo.pMarkerName = name.c_str();
        vkCmdDebugMarkerBeginEXT(currentCmdBuf->cmdBuf, &markerInfo);
    }

    if(m_device.isAftermathEnabled()){
        const usize aftermathMarker = m_aftermathMarkerTracker.pushEvent(name.c_str());
        vkCmdSetCheckpointNV(currentCmdBuf->cmdBuf, reinterpret_cast<const void*>(aftermathMarker));
    }
}

void CommandList::endMarker(){
    if(m_context.extensions.EXT_debug_utils){
        vkCmdEndDebugUtilsLabelEXT(currentCmdBuf->cmdBuf);
    }
    else if(m_context.extensions.EXT_debug_marker){
        vkCmdDebugMarkerEndEXT(currentCmdBuf->cmdBuf);
    }

    if(m_device.isAftermathEnabled()){
        m_aftermathMarkerTracker.popEvent();
    }
}

void CommandList::setEventQuery(IEventQuery* _query, CommandQueue::Enum){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->fence != VK_NULL_HANDLE)
        vkResetFences(m_context.device, 1, &query->fence);

    if(currentCmdBuf)
        currentCmdBuf->signalFence = query->fence;

    query->started = true;
}

void CommandList::resetEventQuery(IEventQuery* _query){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->fence != VK_NULL_HANDLE)
        vkResetFences(m_context.device, 1, &query->fence);

    query->started = false;
}

void CommandList::waitEventQuery(IEventQuery* _query){
    auto* query = checked_cast<EventQuery*>(_query);

    if(query->fence != VK_NULL_HANDLE)
        vkWaitForFences(m_context.device, 1, &query->fence, VK_TRUE, UINT64_MAX);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

