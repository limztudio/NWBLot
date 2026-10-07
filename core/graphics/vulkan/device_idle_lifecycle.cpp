// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::waitForIdle(){
    const VkResult res = waitForNativeIdle();
    if(res == VK_ERROR_DEVICE_LOST){
        markDeviceLost();
        captureDeviceLoss("wait idle");
        return false;
    }
    else if(res != VK_SUCCESS){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to wait for device idle. {}"), ResultToString(res));
        return false;
    }

    for(Queue* queue : m_physicalQueues){
        if(queue){
            ScopedLock lock(queue->m_mutex);
            queue->collectCompletedCommandBuffers();
        }
    }
    m_scratchManager.collectCompletedChunks();
    m_gpuDescriptorHeap.collectRetired();

    return true;
}

VkResult Device::waitForNativeIdle()noexcept{
    for(Queue* queue : m_physicalQueues){
        if(queue)
            queue->m_mutex.lock();
    }
    for(NativeQueueState* nativeQueueState : m_nativeQueueStates){
        if(nativeQueueState)
            nativeQueueState->hostMutex.lock();
    }

    const VkResult result = isDeviceLost() ? VK_ERROR_DEVICE_LOST : m_context.deviceDispatch.vkDeviceWaitIdle(m_context.device);
    if(result == VK_SUCCESS){
        for(Queue* queue : m_physicalQueues){
            if(queue)
                queue->m_lastFinishedID = queue->m_lastSubmittedID;
        }
    }

    for(usize nativeQueueIndex = m_nativeQueueStates.size(); nativeQueueIndex > 0u; --nativeQueueIndex){
        NativeQueueState* const nativeQueueState = m_nativeQueueStates[nativeQueueIndex - 1u];
        if(nativeQueueState)
            nativeQueueState->hostMutex.unlock();
    }
    for(usize queueIndex = m_physicalQueues.size(); queueIndex > 0u; --queueIndex){
        Queue* const queue = m_physicalQueues[queueIndex - 1u];
        if(queue)
            queue->m_mutex.unlock();
    }
    return result;
}

void Device::prepareForDestructionAfterIdleOrLoss(){
    usize activePendingRecordingLeaseCount = 0u;
    usize heapUseCount = 0u;
    {
        ScopedLock heapLock(m_gpuDescriptorHeap.m_mutex);
        activePendingRecordingLeaseCount = m_gpuDescriptorHeap.m_activePendingRecordingLeaseCount;
        heapUseCount = m_gpuDescriptorHeap.m_heapUses.size();
    }
    if(activePendingRecordingLeaseCount != 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device teardown is discarding {} active GpuDescriptorHeap pending-recording leases.")
            , activePendingRecordingLeaseCount
        );
    }
    if(heapUseCount != 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device teardown is discarding {} command buffers that still reference GpuDescriptorHeap.")
            , heapUseCount
        );
    }

    bool descriptorLifecycleTransitioning = false;
    {
        ScopedLock descriptorLifecycleLock(m_descriptorBufferManager.m_lifecycleMutex);
        descriptorLifecycleTransitioning = m_descriptorBufferManager.m_lifecycleTransitioning;
    }
    if(descriptorLifecycleTransitioning){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Device teardown is completing an interrupted descriptor-buffer lifecycle transition.")
        );
    }

    savePipelineCacheData();
    m_gpuDescriptorHeap.shutdownForDeviceTeardown();
    m_descriptorBufferManager.shutdownForDeviceTeardown();
}

void Device::runGarbageCollection(){
    // Avoid extra queue queries after device loss.
    if(isDeviceLost())
        return;

    for(Queue* queue : m_physicalQueues){
        if(queue){
            VkResult completionResult = VK_SUCCESS;
            {
                ScopedLock lock(queue->m_mutex);
                completionResult = queue->updateLastFinishedID();
                if(completionResult == VK_SUCCESS)
                    queue->collectCompletedCommandBuffers();
            }
            if(completionResult == VK_ERROR_DEVICE_LOST){
                captureDeviceLoss("queue timeline query");
                return;
            }
            if(completionResult != VK_SUCCESS){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(completionResult));
                return;
            }
        }
    }
    m_scratchManager.collectCompletedChunks();
    m_gpuDescriptorHeap.collectRetired();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

