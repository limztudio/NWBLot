#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_queries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline VkResult GetTimerQueryResults(const VulkanContext& context, const VkQueryPool queryPool, u64 (&timestamps)[s_TimerQueryTimestampCount]){
    return vkGetQueryPoolResults(
        context.device,
        queryPool,
        s_TimerQueryBeginIndex,
        s_TimerQueryTimestampCount,
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


} // namespace __hidden_vulkan_queries


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQuery::EventQuery(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{
    auto fenceInfo = VulkanDetail::MakeVkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

    const VkResult res = vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &m_fence);
    if(res != VK_SUCCESS){
        m_fence = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create fence for EventQuery: {}"), ResultToString(res));
    }
}
EventQuery::~EventQuery(){
    if(m_fence != VK_NULL_HANDLE){
        vkDestroyFence(m_context.device, m_fence, m_context.allocationCallbacks);
        m_fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{
    auto queryPoolInfo = VulkanDetail::MakeVkStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = s_TimerQueryTimestampCount;

    const VkResult res = vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &m_queryPool);
    if(res != VK_SUCCESS){
        m_queryPool = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery: {}"), ResultToString(res));
    }
}
TimerQuery::~TimerQuery(){
    if(m_queryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, m_queryPool, m_context.allocationCallbacks);
        m_queryPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQueryHandle Device::createEventQuery(){
    auto* query = NewArenaObject<EventQuery>(m_context.objectArena, m_context);
    if(!query->m_fence){
        DestroyArenaObject(m_context.objectArena, query);
        return nullptr;
    }
    return EventQueryHandle(query, EventQueryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void Device::setEventQuery(EventQuery* queryResource, CommandQueue::Enum queue){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return;

    query->m_started = false;

    VkResult res = vkResetFences(m_context.device, 1, &query->m_fence);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset event query fence before submit: {}"), ResultToString(res));
        return;
    }

    Queue* q = getQueue(queue);
    if(!q){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to set event query: requested queue is not available"));
        return;
    }

    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    ScopedLock lock(q->m_mutex);
    res = vkQueueSubmit(q->m_queue, 1, &submitInfo, query->m_fence);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to submit event query fence: {}"), ResultToString(res));
        return;
    }

    query->m_started = true;
}

bool Device::pollEventQuery(EventQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return false;
    if(!query->m_started)
        return true;

    const VkResult res = vkGetFenceStatus(m_context.device, query->m_fence);
    if(res == VK_SUCCESS)
        query->m_started = false;
    return res == VK_SUCCESS;
}

void Device::waitEventQuery(EventQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE)
        return;
    if(!query->m_started)
        return;

    const VkResult res = vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait event query fence: {}"), ResultToString(res));
}

TimerQueryHandle Device::createTimerQuery(){
    auto* query = NewArenaObject<TimerQuery>(m_context.objectArena, m_context);
    if(!query->m_queryPool){
        DestroyArenaObject(m_context.objectArena, query);
        return nullptr;
    }
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

bool Device::pollTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return false;

    u64 timestamps[s_TimerQueryTimestampCount] = {};
    const VkResult res = __hidden_vulkan_queries::GetTimerQueryResults(m_context, query->m_queryPool, timestamps);
    return res == VK_SUCCESS;
}

f32 Device::getTimerQueryTime(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return 0.f;

    u64 timestamps[s_TimerQueryTimestampCount] = {};
    const VkResult res = __hidden_vulkan_queries::GetTimerQueryResults(m_context, query->m_queryPool, timestamps);
    if(res == VK_SUCCESS){
        u64 diff = timestamps[s_TimerQueryEndIndex] - timestamps[s_TimerQueryBeginIndex];
        f32 timestampPeriod = m_context.physicalDeviceProperties.limits.timestampPeriod;
        return static_cast<f32>(diff) * timestampPeriod * 1e-9f; // Convert to seconds
    }

    if(res != VK_NOT_READY)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve timer query results: {}"), ResultToString(res));
    return 0.f;
}

void Device::resetTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;
    vkResetQueryPool(m_context.device, query->m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::resetTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;

    // Device-timeline reset: recorded into the command buffer so the validation layer can order it before the
    // timestamp writes issued later this frame. MUST be recorded OUTSIDE any dynamic render pass, because
    // vkCmdResetQueryPool is illegal inside a render pass instance -- the caller guarantees that.
    vkCmdResetQueryPool(m_currentCmdBuf->m_cmdBuf, query->m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);
}

bool CommandList::canResetTimerQueryHere()const{
    return !m_renderPassActive;
}

void CommandList::beginTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;

    // If no dynamic render pass is open, reset the pool on the device timeline right here so it is defined and
    // correctly ordered before this write -- this makes compute / outside-pass scopes (the shadow trace, caustics,
    // etc., which each run on their own command list) fully self-sufficient regardless of any frame-open reset.
    // Inside a render pass vkCmdResetQueryPool is illegal, so those scopes instead rely on the frame-open
    // recordFrameReset() plus the deviceReady gate that defers a freshly created pool's first use by one frame.
    if(!m_renderPassActive)
        vkCmdResetQueryPool(m_currentCmdBuf->m_cmdBuf, query->m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query->m_queryPool, s_TimerQueryBeginIndex);
}

void CommandList::endTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE)
        return;

    vkCmdWriteTimestamp(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query->m_queryPool, s_TimerQueryEndIndex);
}

void CommandList::beginMarker(const AStringView name){
    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useDebugMarker = m_context.extensions.EXT_debug_marker;
    const bool useNvCheckpoint = m_device.isGpuCrashDiagnosticsEnabled();
    const bool useAmdBreadcrumb = m_device.isAmdBreadcrumbEnabled();
    const bool useGpuMarkers = useNvCheckpoint || useAmdBreadcrumb;
    if(!useDebugUtils && !useDebugMarker && !useGpuMarkers)
        return;

    const GraphicsString markerName(name, m_context.objectArena);

    if(useDebugUtils){
        auto label = VulkanDetail::MakeVkStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = markerName.c_str();
        vkCmdBeginDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf, &label);
    }
    else if(useDebugMarker){
        auto markerInfo = VulkanDetail::MakeVkStruct<VkDebugMarkerMarkerInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT);
        markerInfo.pMarkerName = markerName.c_str();
        vkCmdDebugMarkerBeginEXT(m_currentCmdBuf->m_cmdBuf, &markerInfo);
    }

    // Both vendors share one nested-marker hash so resolveMarker works regardless of which (or both) is active.
    if(useGpuMarkers){
        const usize gpuCrashMarker = m_gpuCrashMarkerTracker.pushEvent(markerName.c_str());
        if(useNvCheckpoint)
            vkCmdSetCheckpointNV(m_currentCmdBuf->m_cmdBuf, reinterpret_cast<const void*>(gpuCrashMarker));
        if(useAmdBreadcrumb){
            const Device::AmdBreadcrumbWrite breadcrumb = m_device.reserveAmdBreadcrumb(gpuCrashMarker);
            if(breadcrumb.valid)
                vkCmdWriteBufferMarkerAMD(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, breadcrumb.buffer, breadcrumb.offset, breadcrumb.marker);
        }
    }
}

void CommandList::endMarker(){
    if(m_context.extensions.EXT_debug_utils)
        vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
    else if(m_context.extensions.EXT_debug_marker)
        vkCmdDebugMarkerEndEXT(m_currentCmdBuf->m_cmdBuf);

    if(m_device.isAnyGpuMarkerEnabled())
        m_gpuCrashMarkerTracker.popEvent();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

