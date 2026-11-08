// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "swap_chain_lifecycle.h"

#include "backend_context.h"
#include "backend_context_detail.h"

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SwapChainLifecycle::createSwapChain(){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready || m_context.m_swapChain || !m_context.m_swapChainImages.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swapchain creation requires an empty ready lifecycle."));
        return false;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_SwapChainPresentModeArena);

    if(createSwapChainResources(scratchArena))
        return true;

    if(m_context.m_rhiDevice && !m_lifecycleDrainActive){
        m_lifecycleDrainActive = m_context.m_rhiDevice->beginLifecycleDrain();
        if(!m_lifecycleDrainActive)
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to close submissions after swapchain creation failure."));
    }
    {
        ScopedLock presentationLock(m_context.m_framePresentation.m_framePresentationMutex);
        m_context.m_framePresentation.m_framePresentationClaimsEnabled = false;
    }
    m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
    return false;
}

b8 SwapChainLifecycle::destroy(){
    {
        ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
        if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed)
            return true;
    }

    auto ticket = prepareSwapChainTransition(SwapChainTransitionKind::Destroy);
    if(!ticket)
        return false;
    return commitDestroy(Move(*ticket));
}

Expected<SwapChainTransitionTicket> SwapChainLifecycle::prepareSwapChainTransition(const SwapChainTransitionKind::Enum kind){
    UniqueLock<Futex> lifecycleLock(m_swapChainLifecycleMutex);
    SwapChainTransitionTicket ticket;
    if(kind >= SwapChainTransitionKind::Count)
        return MakeUnexpected(Failure{});
    if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
        if(kind != SwapChainTransitionKind::Destroy || m_swapChainLifecycleEpoch == 0u)
            return MakeUnexpected(Failure{});
        ticket.owner = &m_context;
        ticket.epoch = m_swapChainLifecycleEpoch;
        ticket.kind = kind;
        return ticket;
    }
    if(m_swapChainLifecycleState == SwapChainLifecycleState::PreparedResize || m_swapChainLifecycleState == SwapChainLifecycleState::PreparedDestroy){
        const b8 preparedForRequestedKind =
            (m_swapChainLifecycleState == SwapChainLifecycleState::PreparedResize && kind == SwapChainTransitionKind::Resize)
            || (m_swapChainLifecycleState == SwapChainLifecycleState::PreparedDestroy && kind == SwapChainTransitionKind::Destroy)
        ;
        if(preparedForRequestedKind){
            ticket.owner = &m_context;
            ticket.epoch = m_swapChainLifecycleEpoch;
            ticket.kind = kind;
            return ticket;
        }
        if(m_swapChainLifecycleState != SwapChainLifecycleState::PreparedResize || kind != SwapChainTransitionKind::Destroy)
            return MakeUnexpected(Failure{});
        m_swapChainLifecycleState = SwapChainLifecycleState::Preparing;
        DeviceHandle device = m_context.m_rhiDevice;
        if(device){
            lifecycleLock.unlock();
            device->prepareForDestructionAfterIdleOrLoss();
            lifecycleLock.lock();
        }
        if(m_context.m_rhiDevice && !m_context.m_rhiDevice->sealLifecycleDrainForDestruction()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to seal an already prepared resize for destruction."));
            return MakeUnexpected(Failure{});
        }

        AdvanceWrappingNonzero(m_swapChainLifecycleEpoch);
        m_swapChainLifecycleState = SwapChainLifecycleState::PreparedDestroy;
        ticket.owner = &m_context;
        ticket.epoch = m_swapChainLifecycleEpoch;
        ticket.kind = kind;
        return ticket;
    }
    if(
        m_swapChainLifecycleState == SwapChainLifecycleState::RetiringPresentation
        || m_swapChainLifecycleState == SwapChainLifecycleState::Preparing
        || (m_swapChainLifecycleState == SwapChainLifecycleState::NeedsDestroy && kind != SwapChainTransitionKind::Destroy)
    )
        return MakeUnexpected(Failure{});

    if(m_context.m_rhiDevice && !m_lifecycleDrainActive){
        if(m_context.m_rhiDevice->submissionOperationActiveOnCurrentThread()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: A submission callback cannot synchronously prepare a swapchain lifecycle transition."));
            return MakeUnexpected(Failure{});
        }
        if(!m_context.m_rhiDevice->beginLifecycleDrain()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to close the submission gate for a swapchain transition."));
            return MakeUnexpected(Failure{});
        }
        m_lifecycleDrainActive = true;
    }
    {
        ScopedLock presentationLock(m_context.m_framePresentation.m_framePresentationMutex);
        m_context.m_framePresentation.m_framePresentationClaimsEnabled = false;
    }
    m_swapChainLifecycleState = SwapChainLifecycleState::Preparing;

    if(!m_context.prepareSwapChainImageRevocation()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return MakeUnexpected(Failure{});
    }

    const b8 acquireProofsComplete = m_context.waitAcquireSyncSlotsForLifecycle();
    if(!acquireProofsComplete && (!m_context.m_rhiDevice || !m_context.m_rhiDevice->isDeviceLost())){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Swapchain transition could not prove WSI acquire completion."));
        return MakeUnexpected(Failure{});
    }
    if(!acquireProofsComplete && m_context.m_rhiDevice && m_context.m_rhiDevice->isDeviceLost()){
        DeviceHandle device = m_context.m_rhiDevice;
        lifecycleLock.unlock();
        device->captureDeviceLoss("acquire lifecycle fence wait");
        lifecycleLock.lock();
    }

    if(m_context.m_rhiDevice){
        const VkResult idleResult = m_context.m_rhiDevice->waitForNativeIdle();
        if(idleResult == VK_ERROR_DEVICE_LOST)
            m_context.m_rhiDevice->markDeviceLost();
        if(idleResult == VK_ERROR_DEVICE_LOST){
            DeviceHandle device = m_context.m_rhiDevice;
            lifecycleLock.unlock();
            device->captureDeviceLoss("swapchain lifecycle idle");
            lifecycleLock.lock();
        }
        if(idleResult != VK_SUCCESS && !m_context.m_rhiDevice->isDeviceLost()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Swapchain transition could not prove device idle."));
            return MakeUnexpected(Failure{});
        }
        if(kind == SwapChainTransitionKind::Resize && m_context.m_rhiDevice->requiresRecreation()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            return MakeUnexpected(Failure{});
        }
        if(kind == SwapChainTransitionKind::Destroy){
            DeviceHandle device = m_context.m_rhiDevice;
            lifecycleLock.unlock();
            device->prepareForDestructionAfterIdleOrLoss();
            lifecycleLock.lock();
            if(!m_context.m_rhiDevice->sealLifecycleDrainForDestruction()){
                m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to seal the prepared device destruction state."));
                return MakeUnexpected(Failure{});
            }
        }
    }

    AdvanceWrappingNonzero(m_swapChainLifecycleEpoch);
    m_swapChainLifecycleState = kind == SwapChainTransitionKind::Resize ? SwapChainLifecycleState::PreparedResize : SwapChainLifecycleState::PreparedDestroy;
    ticket.epoch = m_swapChainLifecycleEpoch;
    ticket.owner = &m_context;
    ticket.kind = kind;
    return ticket;
}

b8 SwapChainLifecycle::commitSwapChainResize(SwapChainTransitionTicket&& ticket){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(!validPreparedTicket(ticket, SwapChainTransitionKind::Resize))
        return false;

    m_context.commitPreparedSwapChainDestruction();
    while(!m_context.m_framesInFlight.empty())
        m_context.m_framesInFlight.pop();
    m_context.m_queryPool.clear();
    m_context.clearSemaphores(m_context.m_presentSemaphores);
    m_context.clearAcquireSyncSlots();

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_SwapChainPresentModeArena);

    if(!createSwapChainResources(scratchArena)){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }

    NWB_ASSERT(m_context.m_rhiDevice && m_lifecycleDrainActive);
    m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    {
        ScopedLock presentationLock(m_context.m_framePresentation.m_framePresentationMutex);
        m_context.m_framePresentation.m_framePresentationClaimsEnabled = true;
    }
    m_context.m_rhiDevice->endLifecycleDrain();
    m_lifecycleDrainActive = false;
    ticket = {};
    return true;
}

b8 SwapChainLifecycle::commitDestroy(SwapChainTransitionTicket&& ticket)noexcept{
    NothrowScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
        const b8 validNoOpTicket = ticket.valid() && ticket.owner == &m_context && ticket.kind == SwapChainTransitionKind::Destroy && ticket.epoch == m_swapChainLifecycleEpoch;
        ticket = {};
        return validNoOpTicket;
    }
    if(!validPreparedTicket(ticket, SwapChainTransitionKind::Destroy))
        return false;

    m_context.commitPreparedSwapChainDestruction();

    while(!m_context.m_framesInFlight.empty())
        m_context.m_framesInFlight.pop();
    m_context.m_queryPool.clear();

    m_context.clearSemaphores(m_context.m_presentSemaphores);
    m_context.clearAcquireSyncSlots();

    m_context.m_rhiDevice = nullptr;
    m_lifecycleDrainActive = false;
    m_context.m_sameClassQueues.clear();
    m_context.m_nativeQueues.clear();
    m_context.m_presentNativeQueueIndex = Limit<u32>::s_Max;
    m_context.m_rendererString.clear();

    if(m_context.m_vulkanDevice){
        m_context.m_deviceDispatch.vkDestroyDevice(m_context.m_vulkanDevice, nullptr);
        m_context.m_vulkanDevice = VK_NULL_HANDLE;
    }

    if(m_context.m_windowSurface && m_context.m_vulkanInstance){
        m_context.m_instanceDispatch.vkDestroySurfaceKHR(m_context.m_vulkanInstance, m_context.m_windowSurface, nullptr);
        m_context.m_windowSurface = VK_NULL_HANDLE;
    }

    if(m_context.m_debugUtilsMessenger){
        if(m_context.m_instanceDispatch.vkDestroyDebugUtilsMessengerEXT)
            m_context.m_instanceDispatch.vkDestroyDebugUtilsMessengerEXT(m_context.m_vulkanInstance, m_context.m_debugUtilsMessenger, nullptr);
        m_context.m_debugUtilsMessenger = VK_NULL_HANDLE;
    }

    if(m_context.m_vulkanInstance){
        m_context.m_instanceDispatch.vkDestroyInstance(m_context.m_vulkanInstance, nullptr);
        m_context.m_vulkanInstance = VK_NULL_HANDLE;
    }

    Aftermath::Shutdown();
    m_swapChainLifecycleState = SwapChainLifecycleState::Destroyed;
    ticket = {};
    return true;
}

b8 SwapChainLifecycle::createSwapChainResources(Alloc::ScratchArena& scratchArena){
    if(!m_context.createVulkanSwapChain(scratchArena))
        return false;

    usize const numPresentSemaphores = m_context.m_swapChainImages.size();
    if(!m_context.recreateSemaphores(m_context.m_presentSemaphores, numPresentSemaphores, "create present semaphores")){
        if(!m_context.destroySwapChainAfterCreateFailure())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after present-semaphore creation failure."));
        return false;
    }

    const usize numAcquireSyncSlots = Max(static_cast<usize>(m_context.m_maxFramesInFlight), m_context.m_swapChainImages.size());
    if(!m_context.recreateAcquireSyncSlots(numAcquireSyncSlots)){
        m_context.clearSemaphores(m_context.m_presentSemaphores);
        if(!m_context.destroySwapChainAfterCreateFailure())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after acquire-slot creation failure."));
        return false;
    }

    if(!m_context.createFrameSyncQueries()){
        m_context.clearSemaphores(m_context.m_presentSemaphores);
        m_context.clearAcquireSyncSlots();
        if(!m_context.destroySwapChainAfterCreateFailure())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after frame-query creation failure."));
        return false;
    }

    // Direct presentation list is swap-chain lifetime state; create it here so present() only reopens it.
    m_context.resetDirectPresentCommandList();
    {
        const GpuPhysicalQueueId primaryGraphicsQueue = m_context.m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
        if(!primaryGraphicsQueue.valid() || !m_context.ensureDirectPresentCommandList(primaryGraphicsQueue)){
            m_context.clearSemaphores(m_context.m_presentSemaphores);
            m_context.clearAcquireSyncSlots();
            m_context.resetDirectPresentCommandList();
            if(!m_context.destroySwapChainAfterCreateFailure())
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after direct-list creation failure."));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create the direct presentation command list."));
            return false;
        }
    }

    m_context.m_swapChainIndex = Limit<u32>::s_Max;
    m_context.m_acquireSyncSlotIndex = 0u;
    {
        ScopedLock presentationLock(m_context.m_framePresentation.m_framePresentationMutex);
        m_context.resetFramePresentationSignal();
        m_context.m_framePresentation.m_framePresentationClaimsEnabled = true;
    }
    m_context.m_frameAcquired = false;
    m_context.m_frameAbandonmentComplete = false;

    return true;
}

b8 SwapChainLifecycle::validPreparedTicket(const SwapChainTransitionTicket& ticket, const SwapChainTransitionKind::Enum kind)const noexcept{
    const SwapChainLifecycleState::Enum requiredState = kind == SwapChainTransitionKind::Resize ? SwapChainLifecycleState::PreparedResize : SwapChainLifecycleState::PreparedDestroy;
    return ticket.valid() && ticket.owner == &m_context && ticket.kind == kind && ticket.epoch == m_swapChainLifecycleEpoch && m_swapChainLifecycleState == requiredState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

