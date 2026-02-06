// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Event/Timer Query Implementation


EventQueryHandle Device::createEventQuery(){
    EventQuery* query = new EventQuery(m_Context);
    return RefCountPtr<IEventQuery, BlankDeleter<IEventQuery>>(query, AdoptRef);
}

void Device::setEventQuery(IEventQuery* _query, CommandQueue::Enum queue){
    EventQuery* query = static_cast<EventQuery*>(_query);
    query->started = true;
    
    // TODO: Submit fence to queue
}

bool Device::pollEventQuery(IEventQuery* _query){
    EventQuery* query = static_cast<EventQuery*>(_query);
    
    if(!query->started)
        return true;
    
    VkResult res = vkGetFenceStatus(m_Context.device, query->fence);
    return res == VK_SUCCESS;
}

void Device::waitEventQuery(IEventQuery* _query){
    EventQuery* query = static_cast<EventQuery*>(_query);
    
    if(!query->started)
        return;
    
    vkWaitForFences(m_Context.device, 1, &query->fence, VK_TRUE, UINT64_MAX);
}

void Device::resetEventQuery(IEventQuery* _query){
    EventQuery* query = static_cast<EventQuery*>(_query);
    vkResetFences(m_Context.device, 1, &query->fence);
    query->started = false;
}

TimerQueryHandle Device::createTimerQuery(){
    TimerQuery* query = new TimerQuery(m_Context);
    return RefCountPtr<ITimerQuery, BlankDeleter<ITimerQuery>>(query, AdoptRef);
}

bool Device::pollTimerQuery(ITimerQuery* _query){
    TimerQuery* query = static_cast<TimerQuery*>(_query);
    return query->resolved;
}

f32 Device::getTimerQueryTime(ITimerQuery* _query){
    TimerQuery* query = static_cast<TimerQuery*>(_query);
    
    if(!query->resolved)
        return 0.f;
    
    u64 timestamps[2];
    VkResult res = vkGetQueryPoolResults(m_Context.device, query->queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT);
    
    if(res == VK_SUCCESS){
        u64 diff = timestamps[1] - timestamps[0];
        f32 timestampPeriod = m_Context.physicalDeviceProperties.limits.timestampPeriod;
        return static_cast<f32>(diff) * timestampPeriod * 1e-9f; // Convert to seconds
    }
    
    return 0.f;
}

void Device::resetTimerQuery(ITimerQuery* _query){
    TimerQuery* query = static_cast<TimerQuery*>(_query);
    vkResetQueryPool(m_Context.device, query->queryPool, 0, 2);
    query->started = false;
    query->resolved = false;
}

//-----------------------------------------------------------------------------
// CommandList - Queries and markers
//-----------------------------------------------------------------------------

void CommandList::beginTimerQuery(ITimerQuery* _query){
    TimerQuery* query = checked_cast<TimerQuery*>(_query);
    
    vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->queryPool, 0);
    query->started = true;
}

void CommandList::endTimerQuery(ITimerQuery* _query){
    TimerQuery* query = checked_cast<TimerQuery*>(_query);
    
    vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->queryPool, 1);
    query->resolved = true;
}

void CommandList::beginMarker(const Name& name){
    if(m_Context->extensions.EXT_debug_utils){
        VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        label.pLabelName = "marker"; // TODO: extract string from Name when Name class has accessors
        vkCmdBeginDebugUtilsLabelEXT(currentCmdBuf->cmdBuf, &label);
    }
}

void CommandList::endMarker(){
    if(m_Context->extensions.EXT_debug_utils)
        vkCmdEndDebugUtilsLabelEXT(currentCmdBuf->cmdBuf);
}

void CommandList::setEventQuery(IEventQuery* _query, CommandQueue::Enum waitQueue){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    
    // Set fence at the end of command buffer
    // Note: Actual fence signaling happens at queue submit time
    query->started = true;
}

void CommandList::resetEventQuery(IEventQuery* _query){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    
    if(query->fence != VK_NULL_HANDLE)
        vkResetFences(m_Context->device, 1, &query->fence);
    
    query->started = false;
}

void CommandList::waitEventQuery(IEventQuery* _query){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    
    // Wait for fence
    if(query->fence != VK_NULL_HANDLE)
        vkWaitForFences(m_Context->device, 1, &query->fence, VK_TRUE, UINT64_MAX);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
