// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "arena_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Frame management


BeginFrameResult BackendContext::beginFrame(){
    UniqueLock<Futex> lifecycleLock(m_swapChainLifecycleMutex);
    BeginFrameResult result;
    const auto captureDeviceLossAfterUnlock = [&](const AStringView context){
        DeviceHandle device = m_rhiDevice;
        if(!device || !device->isDeviceLost())
            return;
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        lifecycleLock.unlock();
        device->captureDeviceLoss(context);
    };

    if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready || m_frameAcquired){
        NWB_LOGGER_ERROR(GLB_TEXT("Cannot begin a Vulkan frame while the previous acquired swap-chain image remains unresolved"));
        return result;
    }

    if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        if(m_rhiDevice && m_rhiDevice->isDeviceLost())
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
        return result;
    }
    m_swapChainIndex = Limit<u32>::s_Max;
    m_frameAbandonmentComplete = false;

    if(!m_rhiDevice || !m_swapChain || m_acquireSyncSlots.empty()){
        NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: beginFrame skipped because swap chain or acquire synchronization is not ready."));
        return result;
    }

    if(m_acquireSyncSlotIndex >= m_acquireSyncSlots.size())
        m_acquireSyncSlotIndex = 0u;
    AcquireSyncSlot& slot = m_acquireSyncSlots[m_acquireSyncSlotIndex];
    if(!prepareAcquireSyncSlot(slot)){
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        if(m_rhiDevice->isDeviceLost()){
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_AcquireSlotReuseContext);
            return result;
        }
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to prepare the next acquire synchronization slot."));
        return result;
    }

    u32 acquiredIndex = Limit<u32>::s_Max;
    const VkResult acquireResult = m_deviceDispatch.vkAcquireNextImageKHR(
        m_vulkanDevice,
        m_swapChain,
        UINT64_MAX,
        slot.semaphore,
        slot.fence,
        &acquiredIndex
    );
    if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR){
        VkSurfaceCapabilitiesKHR surfaceCaps = {};
        const VkResult capabilitiesResult = m_instanceDispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_vulkanPhysicalDevice,
            m_windowSurface,
            &surfaceCaps
        );
        if(capabilitiesResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: Failed to query surface capabilities for a requested resize. {}")
                , ResultToString(capabilitiesResult)
            );
            return result;
        }
        if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
            result.suggestedWidth = surfaceCaps.currentExtent.width;
            result.suggestedHeight = surfaceCaps.currentExtent.height;
        }
        else{
            const SIMDVector requestedExtent = VectorSet(static_cast<f32>(m_swapChainState.backBufferWidth), static_cast<f32>(m_swapChainState.backBufferHeight), 0.0f, 0.0f);
            const SIMDVector minExtent = VectorSet(static_cast<f32>(surfaceCaps.minImageExtent.width), static_cast<f32>(surfaceCaps.minImageExtent.height), 0.0f, 0.0f);
            const SIMDVector maxExtent = VectorSet(static_cast<f32>(surfaceCaps.maxImageExtent.width), static_cast<f32>(surfaceCaps.maxImageExtent.height), 0.0f, 0.0f);
            const SIMDVector clampedExtent = VectorClamp(requestedExtent, minExtent, maxExtent);
            result.suggestedWidth = static_cast<u32>(VectorGetX(clampedExtent));
            result.suggestedHeight = static_cast<u32>(VectorGetY(clampedExtent));
        }
        result.status = BeginFrameStatus::ResizeRequired;
        return result;
    }
    if(acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR){
        if(acquireResult == VK_ERROR_DEVICE_LOST){
            m_rhiDevice->markDeviceLost();
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_AcquireNextImageContext);
            return result;
        }
        NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: Failed to acquire next swap chain image. {}"), ResultToString(acquireResult));
        return result;
    }

    slot.state = AcquireSyncSlotState::AcquirePending;
    m_activeAcquireSyncSlotIndex = m_acquireSyncSlotIndex;
    m_acquireSyncSlotIndex = (m_acquireSyncSlotIndex + 1u) % static_cast<u32>(m_acquireSyncSlots.size());
    m_frameAcquired = true;
    slot.consumerToken = m_rhiDevice->consumeAcquiredImageSemaphore(slot.semaphore);
    if(!slot.consumerToken.valid()){
        slot.state = AcquireSyncSlotState::Unconsumed;
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        if(m_rhiDevice->isDeviceLost()){
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_AcquiredImageSemaphoreBridgeContext);
            return result;
        }
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to bridge an acquired WSI semaphore into a queue completion token."));
        return result;
    }
    slot.state = AcquireSyncSlotState::ConsumerAccepted;

    if(acquiredIndex >= m_swapChainImages.size() || !m_swapChainImages[acquiredIndex].rhiHandle){
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Acquired swap-chain image index does not identify a live back buffer."));
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return result;
    }

    const SwapChainImage& swapChainImage = m_swapChainImages[acquiredIndex];
    result.backBuffer.texture = swapChainImage.rhiHandle;
    result.backBuffer.availabilityCompletion = slot.consumerToken;
    result.backBuffer.nativeInitialState = swapChainImage.presentationState.nativeInitialState();
    result.backBuffer.index = acquiredIndex;
    result.status = BeginFrameStatus::Acquired;
    GLB_ASSERT(result.acquired());

    m_swapChainIndex = acquiredIndex;
    m_frameAbandonmentComplete = false;
    return result;
}

bool BackendContext::abandonAcquiredFrame(){
    UniqueLock<Futex> lifecycleLock(m_swapChainLifecycleMutex);
    if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready)
        return false;
    UniqueLock<Futex> presentationLock(m_framePresentationMutex);
    m_framePresentationCondition.wait(presentationLock, [this](){
        return m_framePresentationSignalState != FramePresentationSignalState::Queued;
    });
    if(!m_frameAcquired)
        return true;
    if(m_frameAbandonmentComplete){
        GLB_ASSERT(m_swapChainIndex == Limit<u32>::s_Max);
        GLB_ASSERT(m_framePresentationSignalState == FramePresentationSignalState::Idle);
        return true;
    }
    if(!m_rhiDevice){
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Cannot abandon an acquired frame without a live RHI device."));
        GLB_ASSERT_MSG(false, GLB_TEXT("A valid acquired frame must retain its RHI device"));
        m_swapChainIndex = Limit<u32>::s_Max;
        return false;
    }
    if(m_rhiDevice->requiresRecreation()){
        m_swapChainIndex = Limit<u32>::s_Max;
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }

    const bool currentImageReady =
        m_swapChain
        && m_swapChainIndex < m_swapChainImages.size()
        && m_swapChainIndex < m_presentSemaphores.size()
        && m_swapChainImages[m_swapChainIndex].rhiHandle
        && m_presentSemaphores[m_swapChainIndex] != VK_NULL_HANDLE
    ;
    const bool presentationSignalIdle =
        m_framePresentationSignalState == FramePresentationSignalState::Idle
        && m_framePresentationSemaphore == VK_NULL_HANDLE
        && !m_framePresentationQueue.valid()
        && m_framePresentationSwapChainIndex == Limit<u32>::s_Max
    ;
    const bool presentationSignalTracked =
        currentImageReady
        && m_framePresentationSignalState != FramePresentationSignalState::Idle
        && m_framePresentationSemaphore == m_presentSemaphores[m_swapChainIndex]
        && m_framePresentationSwapChainIndex == m_swapChainIndex
    ;
    const bool abandonmentReady = currentImageReady && (presentationSignalIdle || presentationSignalTracked);
    // Invalidate presentation identity before touching signal state. Keeping m_frameAcquired set quarantines the
    // WSI image even if retirement or draining fails, so no later beginFrame, claim, or present can reuse it.
    m_swapChainIndex = Limit<u32>::s_Max;
    if(!abandonmentReady){
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Acquired-frame abandonment preconditions failed; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    presentationLock.unlock();
    if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
        DeviceHandle device = m_rhiDevice;
        if(lifecycleLock.owns_lock()){
            if(m_swapChainLifecycleState == SwapChainLifecycleState::Ready)
                m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            lifecycleLock.unlock();
        }
        if(device)
            device->quarantineDevice();
        if(device && device->isDeviceLost())
            device->captureDeviceLoss(VulkanArenaScope::s_AbandonedPresentationSignalIdleContext);
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to retire the abandoned presentation signal; forcing device teardown."));
        return false;
    }

    m_frameAbandonmentComplete = true;
    return true;
}

bool BackendContext::present(bool& outPresentationAccepted){
    outPresentationAccepted = false;
    UniqueLock<Futex> lifecycleLock(m_swapChainLifecycleMutex);
    VkResult res = VK_SUCCESS;
    const auto captureDeviceLossAfterUnlock = [&](const AStringView context, UniqueLock<Futex>* const presentationLock = nullptr){
        DeviceHandle device = m_rhiDevice;
        if(!device || !device->isDeviceLost())
            return;
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        if(presentationLock && presentationLock->owns_lock())
            presentationLock->unlock();
        if(lifecycleLock.owns_lock())
            lifecycleLock.unlock();
        device->captureDeviceLoss(context);
    };

    if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready)
        return false;

    if(!m_rhiDevice || !m_frameAcquired || !m_swapChain || m_presentSemaphores.empty() || m_swapChainImages.empty()){
        NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: present skipped because its device, acquired frame, or swap-chain resources are not ready."));
        if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
            NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization after an invalid present."));
        }
        return false;
    }

    if(m_swapChainIndex >= m_presentSemaphores.size() || m_swapChainIndex >= m_swapChainImages.size()){
        if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
            NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization for an invalid image index."));
        }
        NWB_LOGGER_ERROR(GLB_TEXT("Cannot present Vulkan swap-chain image because its acquired index is invalid"));
        return false;
    }

    const VkSemaphore& semaphore = m_presentSemaphores[m_swapChainIndex];

    bool frameSignalAccepted = false;
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        frameSignalAccepted =
            m_framePresentationSignalState == FramePresentationSignalState::Accepted
            && m_framePresentationSwapChainIndex == m_swapChainIndex
            && m_framePresentationSemaphore == semaphore
        ;
    }

    if(!frameSignalAccepted){
        if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
            NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel the previous presentation-signal claim."));
            return false;
        }

        SwapChainImage& swapChainImage = m_swapChainImages[m_swapChainIndex];
        const VulkanDetail::DirectPresentTransitionPolicy::Enum transitionPolicy =
            VulkanDetail::ResolveDirectPresentTransitionPolicy(
                swapChainImage.presentationState.nativeInitialState()
            )
        ;
        const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
        if(
            !swapChainImage.rhiHandle
            || transitionPolicy == VulkanDetail::DirectPresentTransitionPolicy::Invalid
            || !primaryGraphicsQueue.valid()
        ){
            if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
                captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
                NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization after invalid direct presentation state."));
            }
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Direct presentation transition preconditions failed."));
            return false;
        }

        // Swap-chain-lifetime list created with swap-chain resources; reopen it here instead of creating per frame.
        if(!ensureDirectPresentCommandList(primaryGraphicsQueue)){
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to acquire the direct presentation command list."));
            return false;
        }
        if(!recordDirectPresentTransition(transitionPolicy, swapChainImage.rhiHandle.get()))
            return false;
        CommandList* const directCommandList = m_directPresentCommandList.get();

        const QueueSubmissionPreSubmitHook presentationSignalHook = claimFramePresentationSignal();
        if(!presentationSignalHook.valid()){
            if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
                captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
                NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization after claim rejection."));
            }
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to claim direct presentation on the primary Graphics queue."));
            return false;
        }

        QueueSubmissionDesc submitDesc;
        submitDesc.setPreSubmitHook(presentationSignalHook);
        CommandList* const directCommandLists[] = { directCommandList };
        const QueueSubmissionToken directToken = m_rhiDevice->executeCommandListsInternal(
            directCommandLists,
            LengthOf(directCommandLists),
            primaryGraphicsQueue,
            submitDesc,
            false,
            Device::DeviceLossDiagnosticPolicy::Defer
        );
        if(!directToken.valid()){
            if(!cancelFramePresentationSignalDeferred(&presentationSignalHook, lifecycleLock))
                NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization after submit rejection."));
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_QueueSubmitContext);
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Direct presentation transition/signal submission was rejected."));
            return false;
        }
        if(!confirmFramePresentationSignal(presentationSignalHook, directToken)){
            if(!cancelFramePresentationSignalDeferred(&presentationSignalHook, lifecycleLock))
                NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to cancel presentation synchronization after confirmation rejection."));
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentationSignalCancellationContext);
            NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Accepted direct presentation submission failed signal confirmation/tracking."));
            return false;
        }

        frameSignalAccepted = true;
    }

    UniqueLock<Futex> presentationLock(m_framePresentationMutex);
    frameSignalAccepted =
        m_framePresentationSignalState == FramePresentationSignalState::Accepted
        && m_framePresentationSwapChainIndex == m_swapChainIndex
        && m_framePresentationSemaphore == semaphore
    ;
    if(!frameSignalAccepted){
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Presentation signal state changed before native present."));
        return false;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_swapChainIndex;

    if(!m_rhiDevice){
        NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Presentation requires a synchronized RHI device owner."));
        return false;
    }
    if(!m_rhiDevice->presentNativeQueue(m_presentNativeQueueIndex, presentInfo, res)){
        presentationLock.unlock();
        if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock))
            NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to retire a presentation signal after native present rejection."));
        captureDeviceLossAfterUnlock(VulkanArenaScope::s_NativePresentAdmissionContext, &presentationLock);
        return false;
    }
    outPresentationAccepted = VulkanDetail::IsQueuePresentationAccepted(res);
    const VulkanDetail::QueuePresentWaitDisposition::Enum presentWaitDisposition =
        VulkanDetail::ClassifyQueuePresentWaitDisposition(res);
    m_swapChainImages[m_swapChainIndex].presentationState.observeQueuePresentWaitDisposition(presentWaitDisposition);
    if(presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed){
        // Host/device OOM and unknown failures do not prove the accepted binary signal was consumed. Preserve its
        // tracking until successful idle-and-replacement quarantine or terminal teardown.
        if(presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost){
            m_rhiDevice->markDeviceLost();
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_PresentContext, &presentationLock);
            return false;
        }
        presentationLock.unlock();
        if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){
            NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to retire the unconsumed presentation signal; forcing device teardown."));
            captureDeviceLossAfterUnlock(VulkanArenaScope::s_UnconsumedPresentationSignalIdleContext, &presentationLock);
            return false;
        }
        NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: Queue present failed. {}"), ResultToString(res));
        return false;
    }

    // Every consumed disposition has retired the binary wait even when the surface itself must be recreated.
    resetFramePresentationSignal();
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;
    m_swapChainIndex = Limit<u32>::s_Max;
    m_activeAcquireSyncSlotIndex = Limit<u32>::s_Max;
    presentationLock.unlock();
    if(!(res == VK_SUCCESS || res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)){
        NWB_LOGGER_WARNING(GLB_TEXT("Vulkan: Queue present consumed synchronization but requires recreation. {}")
            , ResultToString(res)
        );
        return false;
    }

    while(m_framesInFlight.size() >= m_maxFramesInFlight){
        auto query = m_framesInFlight.front();
        if(!m_rhiDevice->waitEventQueryInternal(*query, Device::DeviceLossDiagnosticPolicy::Defer)){
            if(!m_rhiDevice->isDeviceLost())
                m_rhiDevice->quarantineDevice();
            captureDeviceLossAfterUnlock("frame synchronization query wait", &presentationLock);
            return false;
        }
        m_framesInFlight.pop();
        m_queryPool.push_back(query);
    }

    EventQueryHandle query;
    if(!m_queryPool.empty()){
        query = m_queryPool.back();
        m_queryPool.pop_back();
    }
    else{
        GLB_ASSERT_MSG(false, GLB_TEXT("Vulkan: frame synchronization query pool was exhausted"));
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Frame synchronization query pool was exhausted; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    if(!query){
        NWB_LOGGER_CRITICAL_WARNING(GLB_TEXT("Vulkan: Failed to acquire a frame synchronization query; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    if(!m_rhiDevice->setEventQueryInternal(
        *query,
        CommandQueue::Graphics,
        Device::DeviceLossDiagnosticPolicy::Defer
    )){
        m_queryPool.push_back(query);
        if(!m_rhiDevice->isDeviceLost())
            m_rhiDevice->quarantineDevice();
        captureDeviceLossAfterUnlock("frame synchronization query submit", &presentationLock);
        return false;
    }
    m_framesInFlight.push(query);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BackendContext::ensureDirectPresentCommandList(const GpuPhysicalQueueId& executionQueue){
    if(!m_rhiDevice || !executionQueue.valid())
        return false;
    if(m_directPresentCommandList && m_directPresentQueue == executionQueue)
        return true;
    resetDirectPresentCommandList();
    CommandListParameters commandListParams;
    commandListParams.setPhysicalQueue(executionQueue);
    m_directPresentCommandList = m_rhiDevice->createCommandList(commandListParams);
    if(!m_directPresentCommandList)
        return false;
    m_directPresentQueue = executionQueue;
    return true;
}

void BackendContext::resetDirectPresentCommandList()noexcept{
    m_directPresentCommandList.reset();
    m_directPresentQueue = {};
}

bool BackendContext::recordDirectPresentTransition(
    const VulkanDetail::DirectPresentTransitionPolicy::Enum transitionPolicy,
    Texture* backbufferTexture
){
    CommandList* const directCommandList = m_directPresentCommandList.get();
    if(!directCommandList || !backbufferTexture)
        return false;
    directCommandList->open();
    if(
        !directCommandList->hasCommandBuffer()
        || !directCommandList->isRecording()
        || directCommandList->commandRecordingFailed()
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to open the direct presentation command list."));
        return false;
    }
    if(transitionPolicy == VulkanDetail::DirectPresentTransitionPolicy::PreservePresent){
        directCommandList->beginTrackingTextureState(
            backbufferTexture,
            s_AllSubresources,
            ResourceStates::Present
        );
    }
    directCommandList->setTextureState(
        backbufferTexture,
        s_AllSubresources,
        ResourceStates::Present
    );
    directCommandList->commitBarriers();
    if(
        !directCommandList->hasCommandBuffer()
        || !directCommandList->isRecording()
        || directCommandList->commandRecordingFailed()
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to record the direct presentation transition."));
        return false;
    }
    directCommandList->close();
    if(
        !directCommandList->hasCommandBuffer()
        || directCommandList->isRecording()
        || directCommandList->commandRecordingFailed()
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("Vulkan: Failed to close the direct presentation command list."));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

