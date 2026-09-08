// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQuery::EventQuery(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{
    auto fenceInfo = VulkanDetail::MakeVkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

    const VkResult res = m_context.deviceDispatch.vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &m_fence);
    if(res != VK_SUCCESS){
        m_fence = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create fence for EventQuery: {}"), ResultToString(res));
    }
}

EventQuery::~EventQuery(){
    if(m_fence != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyFence(m_context.device, m_fence, m_context.allocationCallbacks);
        m_fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQueryHandle Device::createEventQuery(){
    auto* query = NewArenaObject<EventQuery>(m_context.objectArena, m_context);
    if(!query || !query->m_fence){
        DestroyArenaObject(m_context.objectArena, query);
        return nullptr;
    }
    return EventQueryHandle(query, EventQueryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

bool Device::setEventQuery(EventQuery* queryResource, CommandQueue::Enum queue){
    return setEventQueryInternal(queryResource, queue, DeviceLossDiagnosticPolicy::Capture);
}

bool Device::setEventQueryInternal(
    EventQuery* queryResource,
    const CommandQueue::Enum queue,
    const DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return false;

    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    Queue* const q = getQueue(queue);
    if(!q){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to set event query: requested queue is not available"));
        return false;
    }

    VkResult res = VK_SUCCESS;
    bool submissionSuppressed = false;
    AStringView deviceLossContext;
    {
        ScopedLock queryLock(query->m_mutex);
        if(query->m_started){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Refusing to reset an event query fence while its submission is pending."));
            return false;
        }
        ScopedLock lock(q->m_mutex);
        if(submissionsBlocked())
            return false;

        res = m_context.deviceDispatch.vkResetFences(m_context.device, 1, &query->m_fence);
        if(res == VK_ERROR_DEVICE_LOST){
            markDeviceLost();
            deviceLossContext = "event query reset";
        }
        if(res == VK_SUCCESS){
            auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
            {
                ScopedLock hostLock(q->m_nativeQueue.hostMutex);
                submissionSuppressed = submissionsBlocked();
                if(!submissionSuppressed)
                    res = m_context.deviceDispatch.vkQueueSubmit(q->m_nativeQueue.queue, 1, &submitInfo, query->m_fence);
            }
            if(res == VK_ERROR_DEVICE_LOST){
                markDeviceLost();
                deviceLossContext = "event query submit";
            }
            if(!submissionSuppressed && res == VK_SUCCESS)
                query->m_started = true;
        }
    }

    if(!deviceLossContext.empty() && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss(deviceLossContext);
    if(submissionSuppressed || res != VK_SUCCESS){
        if(submissionSuppressed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Event query submission was suppressed because the device requires recreation."));
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to submit event query fence: {}"), ResultToString(res));
        return false;
    }

    return true;
}

bool Device::pollEventQuery(EventQuery* queryResource){
    return pollEventQueryInternal(queryResource, DeviceLossDiagnosticPolicy::Capture);
}

bool Device::pollEventQueryInternal(
    EventQuery* queryResource,
    const DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query->m_mutex);
        if(!query->m_started)
            return true;

        res = m_context.deviceDispatch.vkGetFenceStatus(m_context.device, query->m_fence);
        if(res == VK_SUCCESS)
            query->m_started = false;
        else if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }
    if(res == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss("event query poll");
    return res == VK_SUCCESS;
}

bool Device::waitEventQuery(EventQuery* queryResource){
    return waitEventQueryInternal(queryResource, DeviceLossDiagnosticPolicy::Capture);
}

bool Device::waitEventQueryInternal(
    EventQuery* queryResource,
    const DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query->m_mutex);
        if(!query->m_started)
            return true;

        res = m_context.deviceDispatch.vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
        if(res == VK_SUCCESS)
            query->m_started = false;
        else if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
            captureDeviceLoss("event query wait");
        if(deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait event query fence: {}"), ResultToString(res));
    }
    return res == VK_SUCCESS;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

