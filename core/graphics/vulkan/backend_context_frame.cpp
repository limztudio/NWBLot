// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"
#include "arena_names.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<QueueSubmissionNativeSignal> BackendContext::InvokeFramePresentationSignalPreparation(void* const context, const u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept{
    if(!context)
        return MakeUnexpected(Failure{});
    return MakeNotNull(static_cast<BackendContext*>(context))->prepareFramePresentationSignal(identity, executionQueue);
}

b8 BackendContext::InvokeFramePresentationSignalResolution(void* const context, const u64 identity, const QueueSubmissionToken& token)noexcept{
    if(!context)
        return false;
    return MakeNotNull(static_cast<BackendContext*>(context))->resolveFramePresentationSignal(identity, token);
}

Texture* BackendContext::getBackBuffer(u32 index)const noexcept{
    if(index < m_swapChainImages.size())
        return m_swapChainImages[index].rhiHandle.get();
    return nullptr;
}

QueueSubmissionPreSubmitHook BackendContext::claimFramePresentationSignal()noexcept{
    return m_framePresentation.claimFramePresentationSignal();
}

b8 BackendContext::confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept{
    return m_framePresentation.confirmFramePresentationSignal(claim, token);
}

b8 BackendContext::cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim){
    return m_framePresentation.cancelFramePresentationSignal(claim);
}

void BackendContext::clearSemaphores(SemaphoreVector& semaphores)noexcept{
    if(m_vulkanDevice){
        for(auto& semaphore : semaphores){
            if(semaphore)
                m_deviceDispatch.vkDestroySemaphore(m_vulkanDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    semaphores.clear();
}

b8 BackendContext::recreateSemaphores(SemaphoreVector& semaphores, const usize count, const AStringView operationName){
    VkResult res = VK_SUCCESS;

    clearSemaphores(semaphores);
    semaphores.reserve(count);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for(usize i = 0u; i < count; ++i){
        VkSemaphore sem = VK_NULL_HANDLE;
        res = m_deviceDispatch.vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}. {}"), ConvertString(operationName), ResultToString(res));
            clearSemaphores(semaphores);
            return false;
        }
        semaphores.push_back(sem);
    }

    return true;
}

void BackendContext::clearAcquireSyncSlots()noexcept{
    if(m_vulkanDevice){
        for(AcquireSyncSlot& slot : m_acquireSyncSlots){
            if(slot.fence)
                m_deviceDispatch.vkDestroyFence(m_vulkanDevice, slot.fence, nullptr);
            if(slot.semaphore)
                m_deviceDispatch.vkDestroySemaphore(m_vulkanDevice, slot.semaphore, nullptr);
            slot = {};
        }
    }
    m_acquireSyncSlots.clear();
    m_acquireSyncSlotIndex = 0u;
}

b8 BackendContext::recreateAcquireSyncSlots(const usize count){
    clearAcquireSyncSlots();
    m_acquireSyncSlots.reserve(count);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for(usize index = 0u; index < count; ++index){
        AcquireSyncSlot slot;
        ScopeExit destroyUnpublishedSlot([&]()noexcept{
            if(slot.fence)
                m_deviceDispatch.vkDestroyFence(m_vulkanDevice, slot.fence, nullptr);
            if(slot.semaphore)
                m_deviceDispatch.vkDestroySemaphore(m_vulkanDevice, slot.semaphore, nullptr);
        });

        VkResult result = m_deviceDispatch.vkCreateSemaphore(m_vulkanDevice, &semaphoreInfo, nullptr, &slot.semaphore);
        if(result == VK_SUCCESS)
            result = m_deviceDispatch.vkCreateFence(m_vulkanDevice, &fenceInfo, nullptr, &slot.fence);
        if(result != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acquire synchronization slot {}. {}"), index, ResultToString(result));
            clearAcquireSyncSlots();
            return false;
        }
        m_acquireSyncSlots.push_back(slot);
        destroyUnpublishedSlot.release();
    }
    return true;
}

b8 BackendContext::prepareAcquireSyncSlot(AcquireSyncSlot& slot){
    if(slot.state == AcquireSyncSlotState::Idle){
        return slot.semaphore != VK_NULL_HANDLE && slot.fence != VK_NULL_HANDLE && !slot.consumerToken.valid();
    }
    if(slot.state != AcquireSyncSlotState::ConsumerAccepted || !m_rhiDevice || !m_rhiDevice->waitForSubmissionTokenInternal(slot.consumerToken, Device::DeviceLossDiagnosticPolicy::Defer))
        return false;

    VkResult result = m_deviceDispatch.vkWaitForFences(m_vulkanDevice, 1u, &slot.fence, VK_TRUE, UINT64_MAX);
    if(result == VK_SUCCESS)
        result = m_deviceDispatch.vkResetFences(m_vulkanDevice, 1u, &slot.fence);
    if(result != VK_SUCCESS){
        if(result == VK_ERROR_DEVICE_LOST)
            m_rhiDevice->markDeviceLost();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to prepare an acquire synchronization slot for reuse. {}"), ResultToString(result));
        return false;
    }

    slot.consumerToken = {};
    slot.state = AcquireSyncSlotState::Idle;
    return true;
}

b8 BackendContext::waitAcquireSyncSlotsForLifecycle(){
    if(!m_vulkanDevice)
        return m_acquireSyncSlots.empty();

    for(AcquireSyncSlot& slot : m_acquireSyncSlots){
        if(slot.state == AcquireSyncSlotState::Idle)
            continue;
        const VkResult result = m_deviceDispatch.vkWaitForFences(m_vulkanDevice, 1u, &slot.fence, VK_TRUE, UINT64_MAX);
        if(result == VK_SUCCESS)
            continue;
        if(result == VK_ERROR_DEVICE_LOST && m_rhiDevice)
            m_rhiDevice->markDeviceLost();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to join a WSI acquire fence during lifecycle transition. {}"), ResultToString(result));
        return false;
    }
    return true;
}

b8 BackendContext::createFrameSyncQueries(){
    if(!m_rhiDevice)
        return false;

    // One fence per in-flight frame, created with device/swap-chain resources so present() only recycles.
    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();
    m_queryPool.reserve(m_maxFramesInFlight);

    for(u32 index = 0u; index < m_maxFramesInFlight; ++index){
        auto query = m_rhiDevice->createEventQuery();
        if(!query){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create frame synchronization query {} of {}"), index + 1u, m_maxFramesInFlight);
            m_queryPool.clear();
            return false;
        }
        m_queryPool.push_back(Move(*query));
    }

    return true;
}

Expected<QueueSubmissionNativeSignal> BackendContext::prepareFramePresentationSignal(const u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept{
    return m_framePresentation.prepareFramePresentationSignal(identity, executionQueue);
}

b8 BackendContext::resolveFramePresentationSignal(const u64 identity, const QueueSubmissionToken& token)noexcept{
    return m_framePresentation.resolveFramePresentationSignal(identity, token);
}

b8 BackendContext::replaceFramePresentationSemaphoreAfterIdle(){
    return m_framePresentation.replaceFramePresentationSemaphoreAfterIdle();
}

void BackendContext::resetFramePresentationSignal()noexcept{
    m_framePresentation.resetFramePresentationSignal();
}

b8 BackendContext::cancelFramePresentationSignalDeferred(const QueueSubmissionPreSubmitHook* const claim, UniqueLock<Futex>& lifecycleLock){
    return m_framePresentation.cancelFramePresentationSignalDeferred(claim, lifecycleLock);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

