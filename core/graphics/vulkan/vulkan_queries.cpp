// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Event/Timer Query Implementation


EventQueryHandle Device::createEventQuery(){
    EventQuery* query = new EventQuery(m_Context);
    return MakeRefCountPtr<IEventQuery, BlankDeleter<IEventQuery>>(query);
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
    return MakeRefCountPtr<ITimerQuery, BlankDeleter<ITimerQuery>>(query);
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
    const VulkanContext& vk = *m_Context;
    
    vk.vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->queryPool, 0);
    query->started = true;
}

void CommandList::endTimerQuery(ITimerQuery* _query){
    TimerQuery* query = checked_cast<TimerQuery*>(_query);
    const VulkanContext& vk = *m_Context;
    
    vk.vkCmdWriteTimestamp(currentCmdBuf->cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->queryPool, 1);
    query->resolved = true;
}

void CommandList::beginMarker(const char* name){
    const VulkanContext& vk = *m_Context;
    
    if(vk.extensions.EXT_debug_utils){
        VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
        label.pLabelName = name;
        vk.vkCmdBeginDebugUtilsLabelEXT(currentCmdBuf->cmdBuf, &label);
    }
}

void CommandList::endMarker(){
    const VulkanContext& vk = *m_Context;
    
    if(vk.extensions.EXT_debug_utils)
        vk.vkCmdEndDebugUtilsLabelEXT(currentCmdBuf->cmdBuf);
}

void CommandList::setEventQuery(IEventQuery* _query, CommandQueue::Enum waitQueue){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    const VulkanContext& vk = *m_Context;
    
    // Set fence at the end of command buffer
    VkFence fence = query->fence;
    // Note: Actual fence signaling happens at queue submit time
    query->started = true;
}

void CommandList::resetEventQuery(IEventQuery* _query){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    const VulkanContext& vk = *m_Context;
    
    if(query->fence != VK_NULL_HANDLE)
        vk.vkResetFences(vk.device, 1, &query->fence);
    
    query->started = false;
}

void CommandList::waitEventQuery(IEventQuery* _query){
    EventQuery* query = checked_cast<EventQuery*>(_query);
    const VulkanContext& vk = *m_Context;
    
    // Wait for fence
    if(query->fence != VK_NULL_HANDLE)
        vk.vkWaitForFences(vk.device, 1, &query->fence, VK_TRUE, UINT64_MAX);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
