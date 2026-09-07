// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{

class VulkanBinarySubmissionSignal final : NoCopy{
private:
    [[nodiscard]] static bool prepare(
        void* const context,
        const u64,
        const GpuPhysicalQueueId& executionQueue,
        QueueSubmissionNativeSignal& outSignal
    ){
        VulkanBinarySubmissionSignal* const signal = static_cast<VulkanBinarySubmissionSignal*>(context);
        if(
            !signal
            || !signal->valid()
            || signal->m_invocationCount != 0u
            || executionQueue != signal->m_expectedQueue
        )
            return false;

        outSignal = signal->m_semaphore.nativeSignal();
        ++signal->m_invocationCount;
        return true;
    }


public:
    VulkanBinarySubmissionSignal(
        GraphicsBackend::Device& device,
        const GpuPhysicalQueueId& expectedQueue
    )
        : m_semaphore(device)
        , m_expectedQueue(expectedQueue)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_expectedQueue.valid() && m_semaphore.valid();
    }
    [[nodiscard]] QueueSubmissionPreSubmitHook hook()noexcept{
        return QueueSubmissionPreSubmitHook{
            .context = this,
            .invoke = &VulkanBinarySubmissionSignal::prepare,
        };
    }
    [[nodiscard]] u32 invocationCount()const noexcept{ return m_invocationCount; }


private:
    VulkanTestBinarySemaphore m_semaphore;
    GpuPhysicalQueueId m_expectedQueue;
    u32 m_invocationCount = 0u;
};

};


namespace DescriptorBufferRoundTripDetail{

// Deterministically holds one presentation-style signal claim after it publishes Queued but before Queue::submit.
// This recreates the lifetime interval where cancellation and lifecycle drain must wait for exact native resolution
// instead of retiring the binary semaphore while the submitting thread still owns its raw VkSemaphore handle.
class BlockingPresentationSubmissionSignal final : NoCopy{
private:
    [[nodiscard]] static bool prepare(
        void* const context,
        const u64 identity,
        const GpuPhysicalQueueId& executionQueue,
        QueueSubmissionNativeSignal& outSignal
    ){
        BlockingPresentationSubmissionSignal* const signal =
            static_cast<BlockingPresentationSubmissionSignal*>(context);
        if(
            !signal
            || !signal->valid()
            || identity != s_ClaimIdentity
            || executionQueue != signal->m_expectedQueue
            || signal->m_invocationCount != 0u
        )
            return false;

        outSignal = signal->m_semaphore.nativeSignal();
        ++signal->m_invocationCount;
        signal->m_queued.test_and_set(MemoryOrder::release);
        signal->m_queued.notify_all();
        while(!signal->m_releasePrepare.test(MemoryOrder::acquire))
            signal->m_releasePrepare.wait(false, MemoryOrder::acquire);
        return true;
    }

    [[nodiscard]] static bool resolved(
        void* const context,
        const u64 identity,
        const QueueSubmissionToken& submissionToken
    )noexcept{
        BlockingPresentationSubmissionSignal* const signal =
            static_cast<BlockingPresentationSubmissionSignal*>(context);
        if(!signal || identity != s_ClaimIdentity)
            return false;

        signal->m_resolvedToken = submissionToken;
        signal->m_resolvedBeforeExecuteReturned = !signal->m_executeReturned.test(MemoryOrder::acquire);
        signal->m_resolved.test_and_set(MemoryOrder::release);
        signal->m_resolved.notify_all();
        return true;
    }


public:
    BlockingPresentationSubmissionSignal(
        GraphicsBackend::Device& device,
        const GpuPhysicalQueueId& expectedQueue
    )
        : m_semaphore(device)
        , m_expectedQueue(expectedQueue)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_expectedQueue.valid() && m_semaphore.valid();
    }
    [[nodiscard]] QueueSubmissionPreSubmitHook hook()noexcept{
        return QueueSubmissionPreSubmitHook{
            .context = this,
            .identity = s_ClaimIdentity,
            .invoke = &BlockingPresentationSubmissionSignal::prepare,
            .resolved = &BlockingPresentationSubmissionSignal::resolved,
        };
    }
    void waitUntilQueued()const noexcept{
        while(!m_queued.test(MemoryOrder::acquire))
            m_queued.wait(false, MemoryOrder::acquire);
    }
    void waitUntilResolved()const noexcept{
        while(!m_resolved.test(MemoryOrder::acquire))
            m_resolved.wait(false, MemoryOrder::acquire);
    }
    void releasePrepare()noexcept{
        m_releasePrepare.test_and_set(MemoryOrder::release);
        m_releasePrepare.notify_all();
    }
    void markExecuteReturned()noexcept{
        m_executeReturned.test_and_set(MemoryOrder::release);
        m_executeReturned.notify_all();
    }
    [[nodiscard]] bool isQueued()const noexcept{ return m_queued.test(MemoryOrder::acquire); }
    [[nodiscard]] bool isResolved()const noexcept{ return m_resolved.test(MemoryOrder::acquire); }
    [[nodiscard]] bool executeReturned()const noexcept{ return m_executeReturned.test(MemoryOrder::acquire); }
    [[nodiscard]] bool resolvedBeforeExecuteReturned()const noexcept{ return m_resolvedBeforeExecuteReturned; }
    [[nodiscard]] const QueueSubmissionToken& resolvedToken()const noexcept{ return m_resolvedToken; }
    [[nodiscard]] u32 invocationCount()const noexcept{ return m_invocationCount; }


private:
    static constexpr u64 s_ClaimIdentity = 1u;

    VulkanTestBinarySemaphore m_semaphore;
    GpuPhysicalQueueId m_expectedQueue;
    AtomicFlag m_queued;
    AtomicFlag m_releasePrepare;
    AtomicFlag m_resolved;
    AtomicFlag m_executeReturned;
    QueueSubmissionToken m_resolvedToken;
    u32 m_invocationCount = 0u;
    bool m_resolvedBeforeExecuteReturned = false;
};

};


namespace DescriptorBufferRoundTripDetail{

inline void ExpectNativeTimelineDependency(
    const VulkanTestQueueSubmit2Capture& producerCapture,
    const VkQueue expectedProducerQueue,
    const QueueSubmissionToken& producerToken,
    const VulkanTestQueueSubmit2Capture& consumerCapture,
    const VkQueue expectedConsumerQueue,
    const QueueSubmissionToken& consumerToken
){
    ASSERT_EQ(producerCapture.result, VK_SUCCESS);
    ASSERT_FALSE(producerCapture.overflowed);
    ASSERT_EQ(producerCapture.queue, expectedProducerQueue);
    ASSERT_EQ(producerCapture.submitCount, 1u);
    const VulkanTestSubmitInfo2& producerSubmit = producerCapture.submits[0u];
    ASSERT_FALSE(producerSubmit.overflowed);
    ASSERT_GT(producerSubmit.signalCount, 0u);
    const VulkanTestSemaphoreSubmitInfo& producerSignal = producerSubmit.signals[0u];
    ASSERT_NE(producerSignal.semaphore, VK_NULL_HANDLE);
    EXPECT_EQ(producerSignal.value, producerToken.value);
    EXPECT_EQ(producerSignal.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(producerSignal.deviceIndex, 0u);

    ASSERT_EQ(consumerCapture.result, VK_SUCCESS);
    ASSERT_FALSE(consumerCapture.overflowed);
    ASSERT_EQ(consumerCapture.queue, expectedConsumerQueue);
    ASSERT_EQ(consumerCapture.submitCount, 1u);
    const VulkanTestSubmitInfo2& consumerSubmit = consumerCapture.submits[0u];
    ASSERT_FALSE(consumerSubmit.overflowed);
    ASSERT_EQ(consumerSubmit.waitCount, 1u);
    const VulkanTestSemaphoreSubmitInfo& consumerWait = consumerSubmit.waits[0u];
    EXPECT_EQ(consumerWait.semaphore, producerSignal.semaphore);
    EXPECT_EQ(consumerWait.value, producerSignal.value);
    EXPECT_EQ(consumerWait.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(consumerWait.deviceIndex, 0u);
    ASSERT_GT(consumerSubmit.signalCount, 0u);
    const VulkanTestSemaphoreSubmitInfo& consumerSignal = consumerSubmit.signals[0u];
    ASSERT_NE(consumerSignal.semaphore, VK_NULL_HANDLE);
    EXPECT_EQ(consumerSignal.value, consumerToken.value);
    EXPECT_EQ(consumerSignal.stageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(consumerSignal.deviceIndex, 0u);
}

};


#if !defined(NWB_FINAL)

namespace DescriptorBufferRoundTripDetail{

// The production queue submits one VkSubmitInfo2 whose final signal is its tracking timeline. Split that submit into
// an executable command batch and a signal-only tail blocked by a test-owned timeline. This leaves the real command
// buffer and timestamps complete while the production token remains naturally incomplete.
class VulkanSubmissionTailGate final : NoCopy{
public:
    class ScopedSubmitInterception final : NoCopy{
    public:
        explicit ScopedSubmitInterception(VulkanSubmissionTailGate& gate)
            : m_dispatchOverride(gate.m_device, &VolkDeviceTable::vkQueueSubmit2)
        {
            if(!gate.valid() || s_activeGate || !m_dispatchOverride.valid())
                return;

            s_forwardQueueSubmit2 = m_dispatchOverride.original();
            s_activeGate = &gate;
            if(!m_dispatchOverride.replace(&VulkanSubmissionTailGate::InterceptQueueSubmit2)){
                s_activeGate = nullptr;
                return;
            }
            m_armed = true;
        }
        ~ScopedSubmitInterception(){
            if(!m_armed)
                return;

            s_activeGate = nullptr;
        }


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_armed; }


    private:
        ScopedVulkanDeviceDispatchOverride<PFN_vkQueueSubmit2> m_dispatchOverride;
        bool m_armed = false;
    };


private:
    static thread_local VulkanSubmissionTailGate* s_activeGate;
    static PFN_vkQueueSubmit2 s_forwardQueueSubmit2;

    [[nodiscard]] static VKAPI_ATTR VkResult VKAPI_CALL InterceptQueueSubmit2(
        const VkQueue queue,
        const u32 submitCount,
        const VkSubmitInfo2* const submits,
        const VkFence fence
    ){
        if(!s_forwardQueueSubmit2)
            return VK_ERROR_INITIALIZATION_FAILED;

        VulkanSubmissionTailGate* const gate = s_activeGate;
        if(!gate || queue != gate->m_nativeQueue)
            return s_forwardQueueSubmit2(queue, submitCount, submits, fence);
        return gate->splitQueueSubmit(submitCount, submits, fence);
    }


public:
    VulkanSubmissionTailGate(GraphicsBackend::Device& device, const GpuPhysicalQueueId& queue)
        : m_device(device)
        , m_queue(device.getQueue(queue))
        , m_nativeQueue(static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, queue).pointer()
        ))
    {
        const VulkanTestDeviceContext capture = VulkanTestDeviceProbe::capture(device);
        m_nativeDevice = capture.device;
        m_allocationCallbacks = capture.allocationCallbacks;
        m_deviceDispatch = capture.deviceDispatch;
        if(!m_queue || m_nativeQueue == VK_NULL_HANDLE || !capture.valid())
            return;

        if(!createTimeline(m_readySemaphore))
            return;
        if(!createTimeline(m_releaseSemaphore)){
            m_deviceDispatch->vkDestroySemaphore(m_nativeDevice, m_readySemaphore, m_allocationCallbacks);
            m_readySemaphore = VK_NULL_HANDLE;
        }
    }
    ~VulkanSubmissionTailGate(){
        if(m_splitSubmissionAccepted && !m_queueDrained){
            if(release()){
                m_queue->waitForIdle();
                m_queueDrained = true;
            }
        }
        if(m_splitSubmissionAccepted && !m_queueDrained)
            return;

        if(m_releaseSemaphore != VK_NULL_HANDLE)
            m_deviceDispatch->vkDestroySemaphore(m_nativeDevice, m_releaseSemaphore, m_allocationCallbacks);
        if(m_readySemaphore != VK_NULL_HANDLE)
            m_deviceDispatch->vkDestroySemaphore(m_nativeDevice, m_readySemaphore, m_allocationCallbacks);
    }


public:
    [[nodiscard]] bool valid()const noexcept{
        return
            m_nativeDevice != VK_NULL_HANDLE
            && m_queue
            && m_nativeQueue != VK_NULL_HANDLE
            && m_readySemaphore != VK_NULL_HANDLE
            && m_releaseSemaphore != VK_NULL_HANDLE
        ;
    }
    [[nodiscard]] bool splitSubmissionAccepted()const noexcept{ return m_splitSubmissionAccepted; }
    [[nodiscard]] bool waitUntilReady()const{
        if(!m_splitSubmissionAccepted || !m_deviceDispatch->vkWaitSemaphores)
            return false;

        auto waitInfo = HostSync::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        const u64 readyValue = 1u;
        waitInfo.semaphoreCount = 1u;
        waitInfo.pSemaphores = &m_readySemaphore;
        waitInfo.pValues = &readyValue;
        return m_deviceDispatch->vkWaitSemaphores(m_nativeDevice, &waitInfo, 30'000'000'000u) == VK_SUCCESS;
    }
    [[nodiscard]] bool release(){
        if(!m_splitSubmissionAccepted || !m_deviceDispatch->vkSignalSemaphore)
            return false;
        if(m_releaseSignaled)
            return true;

        auto signalInfo = HostSync::MakeVkStruct<VkSemaphoreSignalInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO);
        signalInfo.semaphore = m_releaseSemaphore;
        signalInfo.value = 1u;
        if(m_deviceDispatch->vkSignalSemaphore(m_nativeDevice, &signalInfo) != VK_SUCCESS)
            return false;
        m_releaseSignaled = true;
        return true;
    }
    [[nodiscard]] bool releaseAndWait(){
        if(!release())
            return false;
        m_queue->waitForIdle();
        m_queueDrained = true;
        return true;
    }


private:
    [[nodiscard]] bool createTimeline(VkSemaphore& semaphore)const{
        if(!m_deviceDispatch->vkCreateSemaphore)
            return false;

        auto typeInfo = HostSync::MakeVkStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0u;
        auto createInfo = HostSync::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        createInfo.pNext = &typeInfo;
        return m_deviceDispatch->vkCreateSemaphore(m_nativeDevice, &createInfo, m_allocationCallbacks, &semaphore) == VK_SUCCESS;
    }
    [[nodiscard]] VkResult splitQueueSubmit(
        const u32 submitCount,
        const VkSubmitInfo2* const submits,
        const VkFence fence
    ){
        if(
            m_splitSubmissionObserved
            || submitCount != 1u
            || !submits
            || submits[0u].pNext
            || submits[0u].commandBufferInfoCount == 0u
            || !submits[0u].pCommandBufferInfos
            || submits[0u].signalSemaphoreInfoCount == 0u
            || !submits[0u].pSignalSemaphoreInfos
        )
            return s_forwardQueueSubmit2(m_nativeQueue, submitCount, submits, fence);

        m_splitSubmissionObserved = true;
        const VkSubmitInfo2& source = submits[0u];
        auto readySignal = HostSync::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        readySignal.semaphore = m_readySemaphore;
        readySignal.value = 1u;
        readySignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        auto releaseWait = HostSync::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        releaseWait.semaphore = m_releaseSemaphore;
        releaseWait.value = 1u;
        releaseWait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 batches[2u] = { source, HostSync::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2) };
        batches[0u].signalSemaphoreInfoCount = 1u;
        batches[0u].pSignalSemaphoreInfos = &readySignal;
        batches[1u].flags = source.flags;
        batches[1u].waitSemaphoreInfoCount = 1u;
        batches[1u].pWaitSemaphoreInfos = &releaseWait;
        batches[1u].signalSemaphoreInfoCount = source.signalSemaphoreInfoCount;
        batches[1u].pSignalSemaphoreInfos = source.pSignalSemaphoreInfos;

        const VkResult result = s_forwardQueueSubmit2(m_nativeQueue, LengthOf(batches), batches, fence);
        m_splitSubmissionAccepted = result == VK_SUCCESS;
        return result;
    }


private:
    GraphicsBackend::Device& m_device;
    GraphicsBackend::Queue* m_queue = nullptr;
    VkQueue m_nativeQueue = VK_NULL_HANDLE;
    VkDevice m_nativeDevice = VK_NULL_HANDLE;
    const VkAllocationCallbacks* m_allocationCallbacks = nullptr;
    VolkDeviceTable* m_deviceDispatch = nullptr;
    VkSemaphore m_readySemaphore = VK_NULL_HANDLE;
    VkSemaphore m_releaseSemaphore = VK_NULL_HANDLE;
    bool m_splitSubmissionObserved = false;
    bool m_splitSubmissionAccepted = false;
    bool m_releaseSignaled = false;
    bool m_queueDrained = false;
};

};

#endif


#if !defined(NWB_FINAL)

namespace DescriptorBufferRoundTripDetail{

inline thread_local VulkanSubmissionTailGate* VulkanSubmissionTailGate::s_activeGate = nullptr;

};

#endif


#if !defined(NWB_FINAL)

namespace DescriptorBufferRoundTripDetail{

inline PFN_vkQueueSubmit2 VulkanSubmissionTailGate::s_forwardQueueSubmit2 = nullptr;

};

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

