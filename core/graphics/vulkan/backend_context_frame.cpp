// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture* BackendContext::getBackBuffer(u32 index)const{
    if(index < m_swapChainImages.size())
        return m_swapChainImages[index].rhiHandle.get();
    return nullptr;
}

QueueSubmissionPreSubmitHook BackendContext::claimFramePresentationSignal()noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    if(
        !m_framePresentationClaimsEnabled
        || !m_rhiDevice
        || !m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Idle
        || !m_swapChain
        || m_swapChainIndex >= m_presentSemaphores.size()
    )
        return {};

    const VkSemaphore semaphore = m_presentSemaphores[m_swapChainIndex];
    if(semaphore == VK_NULL_HANDLE)
        return {};
    if(
        m_framePresentationClaimIdentityExhausted
        || m_nextFramePresentationClaimIdentity == Limit<u64>::s_Max
    )
        return {};

    m_framePresentationSemaphore = semaphore;
    m_framePresentationSwapChainIndex = m_swapChainIndex;
    m_framePresentationQueue = {};
    m_framePresentationSubmission = {};
    ++m_nextFramePresentationClaimIdentity;
    if(m_nextFramePresentationClaimIdentity == Limit<u64>::s_Max)
        m_framePresentationClaimIdentityExhausted = true;
    m_framePresentationClaimIdentity = m_nextFramePresentationClaimIdentity;
    m_framePresentationSignalState = FramePresentationSignalState::Claimed;
    return QueueSubmissionPreSubmitHook{
        .context = this,
        .identity = m_framePresentationClaimIdentity,
        .invoke = &BackendContext::PrepareFramePresentationSignal,
        .resolved = &BackendContext::ResolveFramePresentationSignal,
    };
}

bool BackendContext::PrepareFramePresentationSignal(
    void* const context,
    const u64 identity,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    if(!context)
        return false;
    return static_cast<BackendContext*>(context)->prepareFramePresentationSignal(identity, executionQueue, outSignal);
}

bool BackendContext::prepareFramePresentationSignal(
    const u64 identity,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
)noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    outSignal = {};
    if(
        !m_framePresentationClaimsEnabled
        || !m_rhiDevice
        || !m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Claimed
        || identity == 0u
        || identity != m_framePresentationClaimIdentity
        || !executionQueue.valid()
        || !m_swapChain
        || m_framePresentationSemaphore == VK_NULL_HANDLE
        || m_framePresentationSwapChainIndex != m_swapChainIndex
        || m_swapChainIndex >= m_presentSemaphores.size()
        || m_presentSemaphores[m_swapChainIndex] != m_framePresentationSemaphore
    )
        return false;

    const GpuPhysicalQueueInfo* const queueInfo = m_rhiDevice->getPhysicalQueueInfo(executionQueue);
    const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!VulkanDetail::IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, queueInfo))
        return false;

#if VK_USE_64_BIT_PTR_DEFINES
    outSignal.semaphore = Object(static_cast<void*>(m_framePresentationSemaphore));
#else
    outSignal.semaphore = Object(static_cast<u64>(m_framePresentationSemaphore));
#endif
    outSignal.value = 0u;
    if(!outSignal.valid())
        return false;

    m_framePresentationQueue = executionQueue;
    m_framePresentationSignalState = FramePresentationSignalState::Queued;
    return true;
}

bool BackendContext::ResolveFramePresentationSignal(
    void* const context,
    const u64 identity,
    const QueueSubmissionToken& token
)noexcept{
    if(!context)
        return false;
    return static_cast<BackendContext*>(context)->resolveFramePresentationSignal(identity, token);
}

bool BackendContext::resolveFramePresentationSignal(
    const u64 identity,
    const QueueSubmissionToken& token
)noexcept{
    bool resolved = false;
    {
        NothrowScopedLock presentationLock(m_framePresentationMutex);
        if(
            !m_framePresentationClaimsEnabled
            || !m_rhiDevice
            || !m_frameAcquired
            || m_framePresentationSignalState != FramePresentationSignalState::Queued
            || identity == 0u
            || identity != m_framePresentationClaimIdentity
            || m_framePresentationSwapChainIndex != m_swapChainIndex
            || m_swapChainIndex >= m_presentSemaphores.size()
            || m_presentSemaphores[m_swapChainIndex] != m_framePresentationSemaphore
        )
            return false;

        if(!token.valid()){
            m_framePresentationQueue = {};
            m_framePresentationSignalState = FramePresentationSignalState::Rejected;
            resolved = true;
        }
        else{
            const GpuPhysicalQueueInfo* const queueInfo = m_rhiDevice->getPhysicalQueueInfo(m_framePresentationQueue);
            const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
            if(
                token.queue != CommandQueue::Graphics
                || !token.matchesPhysicalQueue(
                    m_framePresentationQueue.index,
                    m_framePresentationQueue.deviceGeneration
                )
                || !VulkanDetail::IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, queueInfo)
            ){
                m_framePresentationSignalState = FramePresentationSignalState::Failed;
                m_rhiDevice->quarantineDevice();
                resolved = false;
            }
            else{
                m_framePresentationSubmission = token;
                m_framePresentationSignalState = FramePresentationSignalState::Accepted;
                resolved = true;
            }
        }
    }
    m_framePresentationCondition.notify_all();
    return resolved;
}

bool BackendContext::confirmFramePresentationSignal(
    const QueueSubmissionPreSubmitHook& claim,
    const QueueSubmissionToken& token
)noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    return m_framePresentationClaimsEnabled
        && claim.context == this
        && claim.identity != 0u
        && claim.identity == m_framePresentationClaimIdentity
        && claim.invoke == &BackendContext::PrepareFramePresentationSignal
        && claim.resolved == &BackendContext::ResolveFramePresentationSignal
        && m_framePresentationSignalState == FramePresentationSignalState::Accepted
        && token.valid()
        && token.queue == m_framePresentationSubmission.queue
        && token.value == m_framePresentationSubmission.value
        && token.physicalQueueIndex == m_framePresentationSubmission.physicalQueueIndex
        && token.deviceGeneration == m_framePresentationSubmission.deviceGeneration
    ;
}

bool BackendContext::replaceFramePresentationSemaphoreAfterIdle(){
    if(
        !m_vulkanDevice
        || m_framePresentationSemaphore == VK_NULL_HANDLE
        || m_framePresentationSwapChainIndex >= m_presentSemaphores.size()
        || m_presentSemaphores[m_framePresentationSwapChainIndex] != m_framePresentationSemaphore
    )
        return false;

    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore replacement = VK_NULL_HANDLE;
    const VkResult result = m_deviceDispatch.vkCreateSemaphore(m_vulkanDevice, &createInfo, nullptr, &replacement);
    if(result != VK_SUCCESS){
        if(result == VK_ERROR_DEVICE_LOST && m_rhiDevice)
            m_rhiDevice->markDeviceLost();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to replace an abandoned presentation semaphore. {}"), ResultToString(result));
        return false;
    }

    m_deviceDispatch.vkDestroySemaphore(m_vulkanDevice, m_framePresentationSemaphore, nullptr);
    m_presentSemaphores[m_framePresentationSwapChainIndex] = replacement;
    return true;
}

void BackendContext::resetFramePresentationSignal()noexcept{
    m_framePresentationSemaphore = VK_NULL_HANDLE;
    m_framePresentationQueue = {};
    m_framePresentationSubmission = {};
    m_framePresentationClaimIdentity = 0u;
    m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    m_framePresentationSignalState = FramePresentationSignalState::Idle;
}

bool BackendContext::cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim){
    UniqueLock<Futex> lifecycleLock(m_swapChainLifecycleMutex);
    const bool result = cancelFramePresentationSignalDeferred(&claim, lifecycleLock);
    DeviceHandle device = m_rhiDevice;
    if(lifecycleLock.owns_lock())
        lifecycleLock.unlock();
    if(!result && device && device->isDeviceLost())
        device->captureDeviceLoss("presentation signal cancellation");
    return result;
}

bool BackendContext::cancelFramePresentationSignalDeferred(
    const QueueSubmissionPreSubmitHook* const claim,
    UniqueLock<Futex>& lifecycleLock
){
    if(
        !lifecycleLock.owns_lock()
        || m_swapChainLifecycleState != SwapChainLifecycleState::Ready
        || m_lifecycleDrainActive
    )
        return false;

    UniqueLock<Futex> presentationLock(m_framePresentationMutex);
    if(
        !m_framePresentationClaimsEnabled
        || (
            claim
            && (
                claim->context != this
                || claim->identity == 0u
                || claim->identity != m_framePresentationClaimIdentity
                || claim->invoke != &BackendContext::PrepareFramePresentationSignal
                || claim->resolved != &BackendContext::ResolveFramePresentationSignal
            )
        )
    )
        return false;
    m_framePresentationCondition.wait(presentationLock, [this](){
        return m_framePresentationSignalState != FramePresentationSignalState::Queued;
    });
    if(
        claim
        && (
            claim->identity != m_framePresentationClaimIdentity
            || claim->invoke != &BackendContext::PrepareFramePresentationSignal
            || claim->resolved != &BackendContext::ResolveFramePresentationSignal
        )
    )
        return false;
    if(m_framePresentationSignalState == FramePresentationSignalState::Idle)
        return claim == nullptr;

    if(
        m_framePresentationSignalState == FramePresentationSignalState::Claimed
        || m_framePresentationSignalState == FramePresentationSignalState::Rejected
    ){
        resetFramePresentationSignal();
        return true;
    }

    if(
        m_framePresentationSignalState != FramePresentationSignalState::Accepted
        && m_framePresentationSignalState != FramePresentationSignalState::Failed
    )
        return false;

    DeviceHandle device = m_rhiDevice;
    if(!device || device->requiresRecreation()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }
    const u64 lifecycleEpoch = m_swapChainLifecycleEpoch;
    const VkDevice vulkanDevice = m_vulkanDevice;
    const VkSwapchainKHR swapChain = m_swapChain;
    const VkSemaphore presentationSemaphore = m_framePresentationSemaphore;
    const QueueSubmissionToken presentationSubmission = m_framePresentationSubmission;
    const u64 presentationClaimIdentity = m_framePresentationClaimIdentity;
    const u32 presentationSwapChainIndex = m_framePresentationSwapChainIndex;
    const u32 swapChainIndex = m_swapChainIndex;
    const bool frameAcquired = m_frameAcquired;
    if(
        !frameAcquired
        || presentationSemaphore == VK_NULL_HANDLE
        || presentationSwapChainIndex >= m_presentSemaphores.size()
        || m_presentSemaphores[presentationSwapChainIndex] != presentationSemaphore
    )
        return false;

    // Retiring blocks all new presentation claims and lifecycle operations while the queue locks are acquired in
    // their canonical order by the native device join.
    m_framePresentationSignalState = FramePresentationSignalState::Retiring;
    m_swapChainLifecycleState = SwapChainLifecycleState::RetiringPresentation;
    presentationLock.unlock();
    lifecycleLock.unlock();

    const VkResult idleResult = device->waitForNativeIdle();
    if(idleResult == VK_ERROR_DEVICE_LOST)
        device->markDeviceLost();

    lifecycleLock.lock();
    presentationLock.lock();
    const bool retirementIdentityMatches =
        m_swapChainLifecycleState == SwapChainLifecycleState::RetiringPresentation
        && m_swapChainLifecycleEpoch == lifecycleEpoch
        && m_rhiDevice.get() == device.get()
        && m_vulkanDevice == vulkanDevice
        && m_swapChain == swapChain
        && m_frameAcquired == frameAcquired
        && m_swapChainIndex == swapChainIndex
        && m_framePresentationSignalState == FramePresentationSignalState::Retiring
        && m_framePresentationSemaphore == presentationSemaphore
        && m_framePresentationSubmission.queue == presentationSubmission.queue
        && m_framePresentationSubmission.value == presentationSubmission.value
        && m_framePresentationSubmission.physicalQueueIndex == presentationSubmission.physicalQueueIndex
        && m_framePresentationSubmission.deviceGeneration == presentationSubmission.deviceGeneration
        && m_framePresentationClaimIdentity == presentationClaimIdentity
        && m_framePresentationSwapChainIndex == presentationSwapChainIndex
        && presentationSwapChainIndex < m_presentSemaphores.size()
        && m_presentSemaphores[presentationSwapChainIndex] == presentationSemaphore
    ;
    if(!retirementIdentityMatches || idleResult != VK_SUCCESS){
        if(m_swapChainLifecycleState == SwapChainLifecycleState::RetiringPresentation)
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        device->quarantineDevice();
        return false;
    }
    if(!replaceFramePresentationSemaphoreAfterIdle()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        device->quarantineDevice();
        return false;
    }
    resetFramePresentationSignal();
    m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    return true;
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

bool BackendContext::recreateSemaphores(SemaphoreVector& semaphores, const usize count, const AStringView operationName){
    VkResult res = VK_SUCCESS;

    clearSemaphores(semaphores);
    semaphores.reserve(count);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for(usize i = 0; i < count; ++i){
        VkSemaphore sem = VK_NULL_HANDLE;
        res = m_deviceDispatch.vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}. {}"), StringConvert(operationName), ResultToString(res));
            clearSemaphores(semaphores);
            return false;
        }
        semaphores.push_back(sem);
    }

    return true;
}

bool BackendContext::createFrameSyncQueries(){
    if(!m_rhiDevice)
        return false;

    // One fence per in-flight frame, created with device/swap-chain resources so present() only recycles.
    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();
    m_queryPool.reserve(m_maxFramesInFlight);

    for(u32 index = 0u; index < m_maxFramesInFlight; ++index){
        EventQueryHandle query = m_rhiDevice->createEventQuery();
        if(!query){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create frame synchronization query {} of {}")
                , index + 1u
                , m_maxFramesInFlight
            );
            m_queryPool.clear();
            return false;
        }
        m_queryPool.push_back(Move(query));
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
    m_activeAcquireSyncSlotIndex = Limit<u32>::s_Max;
}

bool BackendContext::recreateAcquireSyncSlots(const usize count){
    clearAcquireSyncSlots();
    m_acquireSyncSlots.reserve(count);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for(usize index = 0u; index < count; ++index){
        AcquireSyncSlot slot;
        VkResult result = m_deviceDispatch.vkCreateSemaphore(m_vulkanDevice, &semaphoreInfo, nullptr, &slot.semaphore);
        if(result == VK_SUCCESS)
            result = m_deviceDispatch.vkCreateFence(m_vulkanDevice, &fenceInfo, nullptr, &slot.fence);
        if(result != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create acquire synchronization slot {}. {}")
                , index
                , ResultToString(result)
            );
            if(slot.fence)
                m_deviceDispatch.vkDestroyFence(m_vulkanDevice, slot.fence, nullptr);
            if(slot.semaphore)
                m_deviceDispatch.vkDestroySemaphore(m_vulkanDevice, slot.semaphore, nullptr);
            clearAcquireSyncSlots();
            return false;
        }
        m_acquireSyncSlots.push_back(slot);
    }
    return true;
}

bool BackendContext::prepareAcquireSyncSlot(AcquireSyncSlot& slot){
    if(slot.state == AcquireSyncSlotState::Idle){
        return slot.semaphore != VK_NULL_HANDLE
            && slot.fence != VK_NULL_HANDLE
            && !slot.consumerToken.valid()
        ;
    }
    if(
        slot.state != AcquireSyncSlotState::ConsumerAccepted
        || !m_rhiDevice
        || !m_rhiDevice->waitForSubmissionTokenInternal(
            slot.consumerToken,
            Device::DeviceLossDiagnosticPolicy::Defer
        )
    )
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

bool BackendContext::waitAcquireSyncSlotsForLifecycle(){
    if(!m_vulkanDevice)
        return m_acquireSyncSlots.empty();

    for(AcquireSyncSlot& slot : m_acquireSyncSlots){
        if(slot.state == AcquireSyncSlotState::Idle)
            continue;
        const VkResult result = m_deviceDispatch.vkWaitForFences(
            m_vulkanDevice,
            1u,
            &slot.fence,
            VK_TRUE,
            UINT64_MAX
        );
        if(result == VK_SUCCESS)
            continue;
        if(result == VK_ERROR_DEVICE_LOST && m_rhiDevice)
            m_rhiDevice->markDeviceLost();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to join a WSI acquire fence during lifecycle transition. {}"), ResultToString(result));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

