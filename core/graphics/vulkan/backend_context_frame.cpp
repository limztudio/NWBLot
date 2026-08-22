// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture* BackendContext::getCurrentBackBuffer()const{
    if(m_swapChainIndex < m_swapChainImages.size())
        return m_swapChainImages[m_swapChainIndex].rhiHandle.get();
    return nullptr;
}

Texture* BackendContext::getBackBuffer(u32 index)const{
    if(index < m_swapChainImages.size())
        return m_swapChainImages[index].rhiHandle.get();
    return nullptr;
}

QueueSubmissionPreSubmitHook BackendContext::claimFramePresentationSignal()noexcept{
    if(
        !m_rhiDevice
        || !m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Idle
        || !m_swapChain
        || m_swapChainIndex >= m_presentSemaphores.size()
    )
        return {};

    const VkSemaphore semaphore = m_presentSemaphores[m_swapChainIndex];
    if(semaphore == VK_NULL_HANDLE)
        return {};

    m_framePresentationSemaphore = semaphore;
    m_framePresentationSwapChainIndex = m_swapChainIndex;
    m_framePresentationQueue = {};
    m_framePresentationSignalState = FramePresentationSignalState::Claimed;
    return QueueSubmissionPreSubmitHook{
        .context = this,
        .invoke = &BackendContext::PrepareFramePresentationSignal,
    };
}

bool BackendContext::PrepareFramePresentationSignal(
    void* const context,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    if(!context)
        return false;
    return static_cast<BackendContext*>(context)->prepareFramePresentationSignal(executionQueue, outSignal);
}

bool BackendContext::prepareFramePresentationSignal(
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
)noexcept{
    outSignal = {};
    if(
        !m_rhiDevice
        || !m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Claimed
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

bool BackendContext::confirmFramePresentationSignal(const QueueSubmissionToken& token)noexcept{
    if(
        !m_rhiDevice
        || !m_frameAcquired
        || m_framePresentationSignalState != FramePresentationSignalState::Queued
        || !token.valid()
        || token.queue != CommandQueue::Graphics
        || !token.matchesPhysicalQueue(
            m_framePresentationQueue.index,
            m_framePresentationQueue.deviceGeneration
        )
        || m_framePresentationSwapChainIndex != m_swapChainIndex
        || m_swapChainIndex >= m_presentSemaphores.size()
        || m_presentSemaphores[m_swapChainIndex] != m_framePresentationSemaphore
    )
        return false;

    const GpuPhysicalQueueInfo* const queueInfo = m_rhiDevice->getPhysicalQueueInfo(m_framePresentationQueue);
    const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!VulkanDetail::IsPrimaryGraphicsPresentationQueue(primaryGraphicsQueue, queueInfo))
        return false;

    m_framePresentationSignalState = FramePresentationSignalState::Accepted;
    return true;
}

void BackendContext::replaceFramePresentationSemaphoreAfterIdle()noexcept{
    if(
        !m_vulkanDevice
        || m_framePresentationSemaphore == VK_NULL_HANDLE
        || m_framePresentationSwapChainIndex >= m_presentSemaphores.size()
        || m_presentSemaphores[m_framePresentationSwapChainIndex] != m_framePresentationSemaphore
    )
        return;

    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore replacement = VK_NULL_HANDLE;
    const VkResult result = vkCreateSemaphore(m_vulkanDevice, &createInfo, nullptr, &replacement);
    if(result != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to replace an abandoned presentation semaphore. {}"), ResultToString(result));
        return;
    }

    vkDestroySemaphore(m_vulkanDevice, m_framePresentationSemaphore, nullptr);
    m_presentSemaphores[m_framePresentationSwapChainIndex] = replacement;
}

void BackendContext::resetFramePresentationSignal()noexcept{
    m_framePresentationSemaphore = VK_NULL_HANDLE;
    m_framePresentationQueue = {};
    m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    m_framePresentationSignalState = FramePresentationSignalState::Idle;
}

void BackendContext::cancelFramePresentationSignal()noexcept{
    if(m_framePresentationSignalState == FramePresentationSignalState::Idle)
        return;

    if(m_framePresentationSignalState == FramePresentationSignalState::Claimed){
        resetFramePresentationSignal();
        return;
    }

    // The callback runs before the Vulkan submit returns. If graph acceptance then fails, Queued may still have
    // reached the driver, so wait and replace the binary semaphore rather than risking a second signal on it.
    if(m_rhiDevice && m_rhiDevice->waitForIdle())
        replaceFramePresentationSemaphoreAfterIdle();
    resetFramePresentationSignal();
}

void BackendContext::clearSemaphores(SemaphoreVector& semaphores){
    if(m_vulkanDevice){
        for(auto& semaphore : semaphores){
            if(semaphore)
                vkDestroySemaphore(m_vulkanDevice, semaphore, nullptr);
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
        res = vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
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

    // Presentation acquires one fence per in-flight frame. Create the fixed pool with the device/swap-chain
    // resources so present() only recycles completed fences instead of allocating Vulkan objects in the frame loop.
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

void BackendContext::resizeSwapChain(){
    if(m_vulkanDevice){
        destroySwapChain();
        if(!createVulkanSwapChain()){
            clearSemaphores(m_presentSemaphores);
            clearSemaphores(m_acquireSemaphores);
            return;
        }

        const usize requiredPresentSemaphores = m_swapChainImages.size();
        if(m_presentSemaphores.size() != requiredPresentSemaphores){
            if(!recreateSemaphores(m_presentSemaphores, requiredPresentSemaphores, "recreate present semaphores during resize")){
                clearSemaphores(m_acquireSemaphores);
                destroySwapChain();
                return;
            }
        }

        const usize requiredAcquireSemaphores = Max<usize>(m_maxFramesInFlight, m_swapChainImages.size());
        if(m_acquireSemaphores.size() != requiredAcquireSemaphores){
            if(!recreateSemaphores(m_acquireSemaphores, requiredAcquireSemaphores, "recreate acquire semaphores during resize")){
                clearSemaphores(m_presentSemaphores);
                destroySwapChain();
                return;
            }
        }

        if(m_presentSemaphores.empty() || m_acquireSemaphores.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swap chain resize left semaphore pools empty; destroying swap chain."));
            clearSemaphores(m_presentSemaphores);
            clearSemaphores(m_acquireSemaphores);
            destroySwapChain();
            return;
        }

        m_swapChainIndex = 0;
        m_acquireSemaphoreIndex = 0;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

