// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_presentation_coordinator.h"

#include "backend_context.h"
#include "backend_context_detail.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QueueSubmissionPreSubmitHook FramePresentationCoordinator::claimFramePresentationSignal()noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    if(!m_framePresentationClaimsEnabled || !m_context.m_rhiDevice || !m_context.m_frameAcquired || m_framePresentationSignalState != FramePresentationSignalState::Idle || !m_context.m_swapChain || m_context.m_swapChainIndex >= m_context.m_presentSemaphores.size())
        return {};

    const VkSemaphore semaphore = m_context.m_presentSemaphores[m_context.m_swapChainIndex];
    if(semaphore == VK_NULL_HANDLE)
        return {};
    if(m_framePresentationClaimIdentityExhausted || m_nextFramePresentationClaimIdentity == Limit<u64>::s_Max)
        return {};

    m_framePresentationSemaphore = semaphore;
    m_framePresentationSwapChainIndex = m_context.m_swapChainIndex;
    m_framePresentationQueue = {};
    m_framePresentationSubmission = {};
    ++m_nextFramePresentationClaimIdentity;
    if(m_nextFramePresentationClaimIdentity == Limit<u64>::s_Max)
        m_framePresentationClaimIdentityExhausted = true;
    m_framePresentationClaimIdentity = m_nextFramePresentationClaimIdentity;
    m_framePresentationSignalState = FramePresentationSignalState::Claimed;
    return QueueSubmissionPreSubmitHook{ .context = &m_context, .identity = m_framePresentationClaimIdentity, .invoke = &BackendContext::InvokeFramePresentationSignalPreparation, .resolved = &BackendContext::InvokeFramePresentationSignalResolution, };
}

b8 FramePresentationCoordinator::confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    return
        m_framePresentationClaimsEnabled
        && claim.context == &m_context
        && claim.identity != 0u
        && claim.identity == m_framePresentationClaimIdentity
        && claim.invoke == &BackendContext::InvokeFramePresentationSignalPreparation
        && claim.resolved == &BackendContext::InvokeFramePresentationSignalResolution
        && m_framePresentationSignalState == FramePresentationSignalState::Accepted
        && token.valid()
        && token == m_framePresentationSubmission
    ;
}

b8 FramePresentationCoordinator::cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim){
    UniqueLock<Futex> lifecycleLock(m_context.m_swapChainLifecycle.m_swapChainLifecycleMutex);
    const b8 result = cancelFramePresentationSignalDeferred(&claim, lifecycleLock);
    DeviceHandle device = m_context.m_rhiDevice;
    if(lifecycleLock.owns_lock())
        lifecycleLock.unlock();
    if(!result && device && device->isDeviceLost())
        device->captureDeviceLoss(VulkanArenaScope::s_PresentationSignalCancellationContext);
    return result;
}

Expected<QueueSubmissionNativeSignal> FramePresentationCoordinator::prepareFramePresentationSignal(const u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept{
    NothrowScopedLock presentationLock(m_framePresentationMutex);
    QueueSubmissionNativeSignal signal;
    if(
        !m_framePresentationClaimsEnabled
        || !m_context.m_rhiDevice
        || !m_context.m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Claimed
        || identity == 0u
        || identity != m_framePresentationClaimIdentity
        || !executionQueue.valid()
        || !m_context.m_swapChain
        || m_framePresentationSemaphore == VK_NULL_HANDLE
        || m_framePresentationSwapChainIndex != m_context.m_swapChainIndex
        || m_context.m_swapChainIndex >= m_context.m_presentSemaphores.size()
        || m_context.m_presentSemaphores[m_context.m_swapChainIndex] != m_framePresentationSemaphore
    )
        return MakeUnexpected(Failure{});

    const GpuPhysicalQueueInfo* const queueInfo = m_context.m_rhiDevice->getPhysicalQueueInfo(executionQueue);
    const GpuPhysicalQueueId primaryGraphicsQueue = m_context.m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!VulkanDetail::IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, queueInfo))
        return MakeUnexpected(Failure{});

#if VK_USE_64_BIT_PTR_DEFINES
    signal.semaphore = Object(static_cast<void*>(m_framePresentationSemaphore));
#else
    signal.semaphore = Object(static_cast<u64>(m_framePresentationSemaphore));
#endif
    signal.value = 0u;
    if(!signal.valid())
        return MakeUnexpected(Failure{});

    m_framePresentationQueue = executionQueue;
    m_framePresentationSignalState = FramePresentationSignalState::Queued;
    return signal;
}

b8 FramePresentationCoordinator::resolveFramePresentationSignal(const u64 identity, const QueueSubmissionToken& token)noexcept{
    b8 resolved = false;
    {
        NothrowScopedLock presentationLock(m_framePresentationMutex);
        if(
            !m_framePresentationClaimsEnabled
            || !m_context.m_rhiDevice
            || !m_context.m_frameAcquired
            || m_framePresentationSignalState != FramePresentationSignalState::Queued
            || identity == 0u
            || identity != m_framePresentationClaimIdentity
            || m_framePresentationSwapChainIndex != m_context.m_swapChainIndex
            || m_context.m_swapChainIndex >= m_context.m_presentSemaphores.size()
            || m_context.m_presentSemaphores[m_context.m_swapChainIndex] != m_framePresentationSemaphore
        )
            return false;

        if(!token.valid()){
            m_framePresentationQueue = {};
            m_framePresentationSignalState = FramePresentationSignalState::Rejected;
            resolved = true;
        }
        else{
            const GpuPhysicalQueueInfo* const queueInfo = m_context.m_rhiDevice->getPhysicalQueueInfo(m_framePresentationQueue);
            const GpuPhysicalQueueId primaryGraphicsQueue = m_context.m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
            if(token.queue != CommandQueue::Graphics || !token.matchesPhysicalQueue(m_framePresentationQueue.index, m_framePresentationQueue.deviceGeneration) || !VulkanDetail::IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, queueInfo)){
                m_framePresentationSignalState = FramePresentationSignalState::Failed;
                m_context.m_rhiDevice->quarantineDevice();
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

b8 FramePresentationCoordinator::replaceFramePresentationSemaphoreAfterIdle(){
    if(!m_context.m_vulkanDevice || m_framePresentationSemaphore == VK_NULL_HANDLE || m_framePresentationSwapChainIndex >= m_context.m_presentSemaphores.size() || m_context.m_presentSemaphores[m_framePresentationSwapChainIndex] != m_framePresentationSemaphore)
        return false;

    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore replacement = VK_NULL_HANDLE;
    const VkResult result = m_context.m_deviceDispatch.vkCreateSemaphore(m_context.m_vulkanDevice, &createInfo, nullptr, &replacement);
    if(result != VK_SUCCESS){
        if(result == VK_ERROR_DEVICE_LOST && m_context.m_rhiDevice)
            m_context.m_rhiDevice->markDeviceLost();
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to replace an abandoned presentation semaphore. {}"), ResultToString(result));
        return false;
    }

    m_context.m_deviceDispatch.vkDestroySemaphore(m_context.m_vulkanDevice, m_framePresentationSemaphore, nullptr);
    m_context.m_presentSemaphores[m_framePresentationSwapChainIndex] = replacement;
    return true;
}

void FramePresentationCoordinator::resetFramePresentationSignal()noexcept{
    m_framePresentationSemaphore = VK_NULL_HANDLE;
    m_framePresentationQueue = {};
    m_framePresentationSubmission = {};
    m_framePresentationClaimIdentity = 0u;
    m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    m_framePresentationSignalState = FramePresentationSignalState::Idle;
}

b8 FramePresentationCoordinator::cancelFramePresentationSignalDeferred(const QueueSubmissionPreSubmitHook* const claim, UniqueLock<Futex>& lifecycleLock){
    if(!lifecycleLock.owns_lock() || m_context.m_swapChainLifecycle.m_swapChainLifecycleState != SwapChainLifecycleState::Ready || m_context.m_swapChainLifecycle.m_lifecycleDrainActive)
        return false;

    UniqueLock<Futex> presentationLock(m_framePresentationMutex);
    if(
        !m_framePresentationClaimsEnabled
        || (
            claim
            && (
                claim->context != &m_context
                || claim->identity == 0u
                || claim->identity != m_framePresentationClaimIdentity
                || claim->invoke != &BackendContext::InvokeFramePresentationSignalPreparation
                || claim->resolved != &BackendContext::InvokeFramePresentationSignalResolution
            )
        )
    )
        return false;
    m_framePresentationCondition.wait(presentationLock, [this]()noexcept{
        return m_framePresentationSignalState != FramePresentationSignalState::Queued;
    });
    if(claim && (claim->identity != m_framePresentationClaimIdentity || claim->invoke != &BackendContext::InvokeFramePresentationSignalPreparation || claim->resolved != &BackendContext::InvokeFramePresentationSignalResolution))
        return false;
    if(m_framePresentationSignalState == FramePresentationSignalState::Idle)
        return claim == nullptr;

    if(m_framePresentationSignalState == FramePresentationSignalState::Claimed || m_framePresentationSignalState == FramePresentationSignalState::Rejected){
        resetFramePresentationSignal();
        return true;
    }

    if(m_framePresentationSignalState != FramePresentationSignalState::Accepted && m_framePresentationSignalState != FramePresentationSignalState::Failed)
        return false;

    DeviceHandle device = m_context.m_rhiDevice;
    if(!device || device->requiresRecreation()){
        m_context.m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }
    const u64 lifecycleEpoch = m_context.m_swapChainLifecycle.m_swapChainLifecycleEpoch;
    const VkDevice vulkanDevice = m_context.m_vulkanDevice;
    const VkSwapchainKHR swapChain = m_context.m_swapChain;
    const VkSemaphore presentationSemaphore = m_framePresentationSemaphore;
    const QueueSubmissionToken presentationSubmission = m_framePresentationSubmission;
    const u64 presentationClaimIdentity = m_framePresentationClaimIdentity;
    const u32 presentationSwapChainIndex = m_framePresentationSwapChainIndex;
    const u32 swapChainIndex = m_context.m_swapChainIndex;
    const b8 frameAcquired = m_context.m_frameAcquired;
    if(!frameAcquired || presentationSemaphore == VK_NULL_HANDLE || presentationSwapChainIndex >= m_context.m_presentSemaphores.size() || m_context.m_presentSemaphores[presentationSwapChainIndex] != presentationSemaphore)
        return false;

    // Retiring blocks new presentation claims while queue locks join in canonical order.
    m_framePresentationSignalState = FramePresentationSignalState::Retiring;
    m_context.m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::RetiringPresentation;
    presentationLock.unlock();
    lifecycleLock.unlock();

    const VkResult idleResult = device->waitForNativeIdle();
    if(idleResult == VK_ERROR_DEVICE_LOST)
        device->markDeviceLost();

    lifecycleLock.lock();
    presentationLock.lock();
    const b8 retirementIdentityMatches =
        m_context.m_swapChainLifecycle.m_swapChainLifecycleState == SwapChainLifecycleState::RetiringPresentation
        && m_context.m_swapChainLifecycle.m_swapChainLifecycleEpoch == lifecycleEpoch
        && m_context.m_rhiDevice.get() == device.get()
        && m_context.m_vulkanDevice == vulkanDevice
        && m_context.m_swapChain == swapChain
        && m_context.m_frameAcquired == frameAcquired
        && m_context.m_swapChainIndex == swapChainIndex
        && m_framePresentationSignalState == FramePresentationSignalState::Retiring
        && m_framePresentationSemaphore == presentationSemaphore
        && m_framePresentationSubmission == presentationSubmission
        && m_framePresentationClaimIdentity == presentationClaimIdentity
        && m_framePresentationSwapChainIndex == presentationSwapChainIndex
        && presentationSwapChainIndex < m_context.m_presentSemaphores.size()
        && m_context.m_presentSemaphores[presentationSwapChainIndex] == presentationSemaphore
    ;
    if(!retirementIdentityMatches || idleResult != VK_SUCCESS){
        if(m_context.m_swapChainLifecycle.m_swapChainLifecycleState == SwapChainLifecycleState::RetiringPresentation)
            m_context.m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        device->quarantineDevice();
        return false;
    }
    if(!replaceFramePresentationSemaphoreAfterIdle()){
        m_context.m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        device->quarantineDevice();
        return false;
    }
    resetFramePresentationSignal();
    m_context.m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

