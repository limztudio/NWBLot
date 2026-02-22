// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQueryHandle Device::createEventQuery(){
    auto* query = NewArenaObject<EventQuery>(*m_context.objectArena, m_context);
    if(!query->m_fence){
        DestroyArenaObject(*m_context.objectArena, query);
        return nullptr;
    }
    return EventQueryHandle(query, EventQueryHandle::deleter_type(m_context.objectArena), AdoptRef);
}

void Device::setEventQuery(IEventQuery* _query, CommandQueue::Enum queue){
    if(!_query)
        return;

    VkResult res = VK_SUCCESS;
    auto* query = static_cast<EventQuery*>(_query);
    if(query->m_fence == VK_NULL_HANDLE)
        return;

    res = vkResetFences(m_context.device, 1, &query->m_fence);
    if(res != VK_SUCCESS)
        return;

    Queue* q = getQueue(queue);
    if(q){
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        Mutex::scoped_lock lock(q->m_mutex);
        res = vkQueueSubmit(q->getVkQueue(), 1, &submitInfo, query->m_fence);
        if(res != VK_SUCCESS)
            return;
    }
    else
        return;

    query->m_started = true;
}

bool Device::pollEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    if(!_query)
        return false;

    auto* query = static_cast<EventQuery*>(_query);
    if(query->m_fence == VK_NULL_HANDLE)
        return false;
    if(!query->m_started)
        return true;

    res = vkGetFenceStatus(m_context.device, query->m_fence);
    return res == VK_SUCCESS;
}

void Device::waitEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    if(!_query)
        return;

    auto* query = static_cast<EventQuery*>(_query);
    if(query->m_fence == VK_NULL_HANDLE)
        return;
    if(!query->m_started)
        return;

    res = vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait event query fence: {}"), ResultToString(res));
}

void Device::resetEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    if(!_query)
        return;

    auto* query = static_cast<EventQuery*>(_query);
    if(query->m_fence == VK_NULL_HANDLE)
        return;
    res = vkResetFences(m_context.device, 1, &query->m_fence);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset event query fence: {}"), ResultToString(res));
}

TimerQueryHandle Device::createTimerQuery(){
    auto* query = NewArenaObject<TimerQuery>(*m_context.objectArena, m_context);
    if(!query->m_queryPool){
        DestroyArenaObject(*m_context.objectArena, query);
        return nullptr;
    }
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(m_context.objectArena), AdoptRef);
}

bool Device::pollTimerQuery(ITimerQuery* _query){
    if(!_query)
        return false;

    auto* query = static_cast<TimerQuery*>(_query);
    return query->m_resolved;
}

f32 Device::getTimerQueryTime(ITimerQuery* _query){
    VkResult res = VK_SUCCESS;

    if(!_query)
        return 0.f;

    auto* query = static_cast<TimerQuery*>(_query);
    if(query->m_queryPool == VK_NULL_HANDLE)
        return 0.f;

    if(!query->m_resolved)
        return 0.f;

    u64 timestamps[s_TimerQueryTimestampCount];
    res = vkGetQueryPoolResults(
        m_context.device,
        query->m_queryPool,
        s_TimerQueryBeginIndex,
        s_TimerQueryTimestampCount,
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT);
    if(res == VK_SUCCESS){
        u64 diff = timestamps[s_TimerQueryEndIndex] - timestamps[s_TimerQueryBeginIndex];
        f32 timestampPeriod = m_context.physicalDeviceProperties.limits.timestampPeriod;
        return static_cast<f32>(diff) * timestampPeriod * 1e-9f; // Convert to seconds
    }

    return 0.f;
}

void Device::resetTimerQuery(ITimerQuery* _query){
    if(!_query)
        return;

    auto* query = static_cast<TimerQuery*>(_query);
    if(query->m_queryPool == VK_NULL_HANDLE)
        return;
    vkResetQueryPool(m_context.device, query->m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);
    query->m_started = false;
    query->m_resolved = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->m_queryPool, s_TimerQueryBeginIndex);
    query->m_started = true;
}

void CommandList::endTimerQuery(ITimerQuery* _query){
    auto* query = checked_cast<TimerQuery*>(_query);
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->m_queryPool, s_TimerQueryEndIndex);
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
    VkResult res = VK_SUCCESS;

    auto* query = checked_cast<EventQuery*>(_query);
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return;

    res = vkResetFences(m_context.device, 1, &query->m_fence);
    if(res != VK_SUCCESS){
        query->m_started = false;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset event query fence for command list signal: {}"), ResultToString(res));
        return;
    }

    if(m_currentCmdBuf){
        m_currentCmdBuf->m_signalFence = query->m_fence;
        query->m_started = true;
    }
    else
        query->m_started = false;
}

void CommandList::resetEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = checked_cast<EventQuery*>(_query);
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return;

    res = vkResetFences(m_context.device, 1, &query->m_fence);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset event query fence from command list: {}"), ResultToString(res));
}

void CommandList::waitEventQuery(IEventQuery* _query){
    VkResult res = VK_SUCCESS;

    auto* query = checked_cast<EventQuery*>(_query);
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return;

    res = vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait event query fence from command list: {}"), ResultToString(res));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

