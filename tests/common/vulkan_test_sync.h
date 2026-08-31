// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/global.h>
#include <global/sync.h>
#include <core/graphics/vulkan/backend.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct VulkanTestDeviceContext{
    VkDevice device = VK_NULL_HANDLE;
    const VkAllocationCallbacks* allocationCallbacks = nullptr;


    [[nodiscard]] bool valid()const noexcept{ return device != VK_NULL_HANDLE; }
};


class VulkanTestDeviceProbe final : NoCopy{
private:
    inline static thread_local VulkanTestDeviceContext* s_activeCapture = nullptr;
    inline static PFN_vkCreateQueryPool s_forwardCreateQueryPool = nullptr;
    inline static Futex s_captureMutex;

    [[nodiscard]] static VKAPI_ATTR VkResult VKAPI_CALL captureCreateQueryPool(
        const VkDevice device,
        const VkQueryPoolCreateInfo* const createInfo,
        const VkAllocationCallbacks* const allocationCallbacks,
        VkQueryPool* const queryPool
    ){
        VulkanTestDeviceContext* const capture = s_activeCapture;
        if(capture){
            capture->device = device;
            capture->allocationCallbacks = allocationCallbacks;
        }
        if(!s_forwardCreateQueryPool)
            return VK_ERROR_INITIALIZATION_FAILED;
        return s_forwardCreateQueryPool(device, createInfo, allocationCallbacks, queryPool);
    }


public:
    [[nodiscard]] static VulkanTestDeviceContext capture(Core::GraphicsBackend::Device& device){
        VulkanTestDeviceContext capture;
        if(!vkCreateQueryPool)
            return capture;

        ScopedLock lock(s_captureMutex);
        if(s_activeCapture)
            return capture;

        s_forwardCreateQueryPool = vkCreateQueryPool;
        s_activeCapture = &capture;
        vkCreateQueryPool = &VulkanTestDeviceProbe::captureCreateQueryPool;
        auto probe = device.createTimerQuery();
        vkCreateQueryPool = s_forwardCreateQueryPool;
        s_activeCapture = nullptr;
        if(!probe)
            capture = {};
        return capture;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct VulkanTestSemaphoreSubmitInfo{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    u64 value = 0u;
    VkPipelineStageFlags2 stageMask = 0u;
    u32 deviceIndex = 0u;
};

struct VulkanTestCommandBufferSubmitInfo{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    u32 deviceMask = 0u;
};

struct VulkanTestSubmitInfo2{
    Array<VulkanTestSemaphoreSubmitInfo, 8u> waits = {};
    Array<VulkanTestCommandBufferSubmitInfo, 8u> commandBuffers = {};
    Array<VulkanTestSemaphoreSubmitInfo, 8u> signals = {};
    VkSubmitFlags flags = 0u;
    u32 waitCount = 0u;
    u32 commandBufferCount = 0u;
    u32 signalCount = 0u;
    bool overflowed = false;
};

struct VulkanTestQueueSubmit2Capture{
    Array<VulkanTestSubmitInfo2, 2u> submits = {};
    VkQueue queue = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_UNKNOWN;
    u32 submitCount = 0u;
    bool overflowed = false;
};


class VulkanTestQueueSubmit2Observer final : NoCopy{
private:
    inline static Atomic<VulkanTestQueueSubmit2Observer*> s_activeObserver{ nullptr };
    inline static PFN_vkQueueSubmit2 s_forwardQueueSubmit2 = nullptr;
    inline static Futex s_installMutex;
    inline static Atomic<u32> s_activeInterceptionCount = 0u;

    [[nodiscard]] static VKAPI_ATTR VkResult VKAPI_CALL interceptQueueSubmit2(
        const VkQueue queue,
        const u32 submitCount,
        const VkSubmitInfo2* const submits,
        const VkFence fence
    ){
        PFN_vkQueueSubmit2 forward = nullptr;
        VulkanTestQueueSubmit2Observer* observer = nullptr;
        {
            ScopedLock lock(s_installMutex);
            forward = s_forwardQueueSubmit2;
            observer = s_activeObserver.load(MemoryOrder::acquire);
            if(observer)
                s_activeInterceptionCount.fetch_add(1u, MemoryOrder::acq_rel);
        }
        if(!forward)
            return VK_ERROR_INITIALIZATION_FAILED;
        if(!observer)
            return forward(queue, submitCount, submits, fence);

        const VkResult result = observer->captureAndForward(forward, queue, submitCount, submits, fence);
        s_activeInterceptionCount.fetch_sub(1u, MemoryOrder::release);
        return result;
    }

    [[nodiscard]] VkResult captureAndForward(
        const PFN_vkQueueSubmit2 forward,
        const VkQueue queue,
        const u32 submitCount,
        const VkSubmitInfo2* const submits,
        const VkFence fence
    ){
        const u32 captureIndex = m_reservedCaptureCount.fetch_add(1u, MemoryOrder::acq_rel);
        if(captureIndex >= LengthOf(m_captures)){
            m_overflowed.store(true, MemoryOrder::release);
            if(consumeSubmissionFailure(queue))
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            return forward(queue, submitCount, submits, fence);
        }

        VulkanTestQueueSubmit2Capture& capture = m_captures[captureIndex];
        capture = VulkanTestQueueSubmit2Capture{};
        capture.queue = queue;
        capture.fence = fence;
        capture.submitCount = submitCount;
        if(submitCount > LengthOf(capture.submits) || (submitCount > 0u && !submits))
            capture.overflowed = true;

        const u32 copiedSubmitCount = submitCount < LengthOf(capture.submits)
            ? submitCount
            : static_cast<u32>(LengthOf(capture.submits))
        ;
        for(u32 submitIndex = 0u; submitIndex < copiedSubmitCount && submits; ++submitIndex){
            const VkSubmitInfo2& source = submits[submitIndex];
            VulkanTestSubmitInfo2& destination = capture.submits[submitIndex];
            destination.flags = source.flags;
            destination.waitCount = source.waitSemaphoreInfoCount;
            destination.commandBufferCount = source.commandBufferInfoCount;
            destination.signalCount = source.signalSemaphoreInfoCount;
            if(
                source.waitSemaphoreInfoCount > LengthOf(destination.waits)
                || source.commandBufferInfoCount > LengthOf(destination.commandBuffers)
                || source.signalSemaphoreInfoCount > LengthOf(destination.signals)
                || (source.waitSemaphoreInfoCount > 0u && !source.pWaitSemaphoreInfos)
                || (source.commandBufferInfoCount > 0u && !source.pCommandBufferInfos)
                || (source.signalSemaphoreInfoCount > 0u && !source.pSignalSemaphoreInfos)
            )
                destination.overflowed = true;

            const u32 copiedWaitCount = source.waitSemaphoreInfoCount < LengthOf(destination.waits)
                ? source.waitSemaphoreInfoCount
                : static_cast<u32>(LengthOf(destination.waits))
            ;
            for(u32 waitIndex = 0u; waitIndex < copiedWaitCount && source.pWaitSemaphoreInfos; ++waitIndex){
                const VkSemaphoreSubmitInfo& wait = source.pWaitSemaphoreInfos[waitIndex];
                destination.waits[waitIndex] = VulkanTestSemaphoreSubmitInfo{
                    .semaphore = wait.semaphore,
                    .value = wait.value,
                    .stageMask = wait.stageMask,
                    .deviceIndex = wait.deviceIndex,
                };
            }

            const u32 copiedCommandBufferCount = source.commandBufferInfoCount < LengthOf(destination.commandBuffers)
                ? source.commandBufferInfoCount
                : static_cast<u32>(LengthOf(destination.commandBuffers))
            ;
            for(
                u32 commandBufferIndex = 0u;
                commandBufferIndex < copiedCommandBufferCount && source.pCommandBufferInfos;
                ++commandBufferIndex
            ){
                const VkCommandBufferSubmitInfo& commandBuffer = source.pCommandBufferInfos[commandBufferIndex];
                destination.commandBuffers[commandBufferIndex] = VulkanTestCommandBufferSubmitInfo{
                    .commandBuffer = commandBuffer.commandBuffer,
                    .deviceMask = commandBuffer.deviceMask,
                };
            }

            const u32 copiedSignalCount = source.signalSemaphoreInfoCount < LengthOf(destination.signals)
                ? source.signalSemaphoreInfoCount
                : static_cast<u32>(LengthOf(destination.signals))
            ;
            for(u32 signalIndex = 0u; signalIndex < copiedSignalCount && source.pSignalSemaphoreInfos; ++signalIndex){
                const VkSemaphoreSubmitInfo& signal = source.pSignalSemaphoreInfos[signalIndex];
                destination.signals[signalIndex] = VulkanTestSemaphoreSubmitInfo{
                    .semaphore = signal.semaphore,
                    .value = signal.value,
                    .stageMask = signal.stageMask,
                    .deviceIndex = signal.deviceIndex,
                };
            }

            capture.overflowed = capture.overflowed || destination.overflowed;
        }
        if(capture.overflowed)
            m_overflowed.store(true, MemoryOrder::release);

        if(consumeSubmissionFailure(queue))
            capture.result = VK_ERROR_OUT_OF_HOST_MEMORY;
        else
            capture.result = forward(queue, submitCount, submits, fence);
        m_captureComplete[captureIndex].store(true, MemoryOrder::release);
        return capture.result;
    }

    [[nodiscard]] bool consumeSubmissionFailure(const VkQueue queue)noexcept{
        ScopedLock lock(m_submissionFailureMutex);
        if(m_pendingSubmissionFailureCount == 0u || queue != m_submissionFailureQueue)
            return false;

        --m_pendingSubmissionFailureCount;
        if(m_injectedSubmissionFailureCount != s_MaxU32)
            ++m_injectedSubmissionFailureCount;
        if(m_pendingSubmissionFailureCount == 0u)
            m_submissionFailureQueue = VK_NULL_HANDLE;
        return true;
    }


public:
    // Volk's dispatch slot is plain storage. Construct and destroy this observer only while new submissions and
    // other vkQueueSubmit2 wrappers are quiescent; already registered interceptor calls are drained at teardown.
    VulkanTestQueueSubmit2Observer(){
        ScopedLock lock(s_installMutex);
        if(s_activeObserver.load(MemoryOrder::acquire))
            return;

        m_originalQueueSubmit2 = vkQueueSubmit2;
        if(!m_originalQueueSubmit2)
            return;

        s_forwardQueueSubmit2 = m_originalQueueSubmit2;
        s_activeObserver.store(this, MemoryOrder::release);
        vkQueueSubmit2 = &VulkanTestQueueSubmit2Observer::interceptQueueSubmit2;
        m_armed = true;
    }
    ~VulkanTestQueueSubmit2Observer(){
        ScopedLock lock(s_installMutex);
        if(!m_armed)
            return;

        s_activeObserver.store(nullptr, MemoryOrder::release);
        while(s_activeInterceptionCount.load(MemoryOrder::acquire) != 0u)
            YieldThread();
        NWB_ASSERT(vkQueueSubmit2 == &VulkanTestQueueSubmit2Observer::interceptQueueSubmit2);
        vkQueueSubmit2 = m_originalQueueSubmit2;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_armed; }
    [[nodiscard]] bool overflowed()const noexcept{ return m_overflowed.load(MemoryOrder::acquire); }
    [[nodiscard]] bool armSubmissionFailures(VkQueue queue, u32 count = 1u){
        if(!m_armed || queue == VK_NULL_HANDLE || count == 0u)
            return false;

        ScopedLock lock(m_submissionFailureMutex);
        if(
            (m_pendingSubmissionFailureCount != 0u && queue != m_submissionFailureQueue)
            || count > s_MaxU32 - m_pendingSubmissionFailureCount
        )
            return false;

        m_submissionFailureQueue = queue;
        m_pendingSubmissionFailureCount += count;
        return true;
    }
    [[nodiscard]] u32 injectedSubmissionFailureCount()const noexcept{
        ScopedLock lock(m_submissionFailureMutex);
        return m_injectedSubmissionFailureCount;
    }
    [[nodiscard]] u32 pendingSubmissionFailureCount()const noexcept{
        ScopedLock lock(m_submissionFailureMutex);
        return m_pendingSubmissionFailureCount;
    }
    [[nodiscard]] usize capturedSubmissionCount()const noexcept{
        const u32 reservedCount = m_reservedCaptureCount.load(MemoryOrder::acquire);
        const u32 boundedCount = reservedCount < LengthOf(m_captures)
            ? reservedCount
            : static_cast<u32>(LengthOf(m_captures))
        ;
        usize completedCount = 0u;
        for(u32 captureIndex = 0u; captureIndex < boundedCount; ++captureIndex){
            if(m_captureComplete[captureIndex].load(MemoryOrder::acquire))
                ++completedCount;
        }
        return completedCount;
    }
    [[nodiscard]] usize successfulSubmissionCount()const noexcept{
        const u32 reservedCount = m_reservedCaptureCount.load(MemoryOrder::acquire);
        const u32 boundedCount = reservedCount < LengthOf(m_captures)
            ? reservedCount
            : static_cast<u32>(LengthOf(m_captures))
        ;
        usize successfulCount = 0u;
        for(u32 captureIndex = 0u; captureIndex < boundedCount; ++captureIndex){
            if(
                m_captureComplete[captureIndex].load(MemoryOrder::acquire)
                && m_captures[captureIndex].result == VK_SUCCESS
            )
                ++successfulCount;
        }
        return successfulCount;
    }
    [[nodiscard]] usize successfulWaitCount()const noexcept{
        const u32 reservedCount = m_reservedCaptureCount.load(MemoryOrder::acquire);
        const u32 boundedCount = reservedCount < LengthOf(m_captures)
            ? reservedCount
            : static_cast<u32>(LengthOf(m_captures))
        ;
        usize waitCount = 0u;
        for(u32 captureIndex = 0u; captureIndex < boundedCount; ++captureIndex){
            if(
                !m_captureComplete[captureIndex].load(MemoryOrder::acquire)
                || m_captures[captureIndex].result != VK_SUCCESS
            )
                continue;

            const VulkanTestQueueSubmit2Capture& capture = m_captures[captureIndex];
            const u32 submitCount = capture.submitCount < LengthOf(capture.submits)
                ? capture.submitCount
                : static_cast<u32>(LengthOf(capture.submits))
            ;
            for(u32 submitIndex = 0u; submitIndex < submitCount; ++submitIndex)
                waitCount += capture.submits[submitIndex].waitCount;
        }
        return waitCount;
    }
    [[nodiscard]] bool capturedSubmission(
        const usize index,
        VulkanTestQueueSubmit2Capture& outCapture
    )const noexcept{
        if(index >= LengthOf(m_captures) || !m_captureComplete[index].load(MemoryOrder::acquire))
            return false;
        outCapture = m_captures[index];
        return true;
    }


private:
    PFN_vkQueueSubmit2 m_originalQueueSubmit2 = nullptr;
    Array<VulkanTestQueueSubmit2Capture, 16u> m_captures = {};
    Array<Atomic<bool>, 16u> m_captureComplete = {};
    Atomic<u32> m_reservedCaptureCount = 0u;
    Atomic<bool> m_overflowed = false;
    mutable Futex m_submissionFailureMutex;
    VkQueue m_submissionFailureQueue = VK_NULL_HANDLE;
    u32 m_injectedSubmissionFailureCount = 0u;
    u32 m_pendingSubmissionFailureCount = 0u;
    bool m_armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanTestBinarySemaphore final : NoCopy{
private:
    [[nodiscard]] static Core::Object encode(const VkSemaphore semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
        return Core::Object(static_cast<void*>(semaphore));
#else
        return Core::Object(static_cast<u64>(semaphore));
#endif
    }


public:
    explicit VulkanTestBinarySemaphore(Core::GraphicsBackend::Device& device)
        : m_device(device)
        , m_context(VulkanTestDeviceProbe::capture(device))
    {
        if(!m_context.valid() || !vkCreateSemaphore)
            return;

        const VkSemaphoreCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0u,
        };
        if(vkCreateSemaphore(m_context.device, &createInfo, m_context.allocationCallbacks, &m_semaphore) != VK_SUCCESS)
            m_semaphore = VK_NULL_HANDLE;
    }
    ~VulkanTestBinarySemaphore(){
        if(m_semaphore == VK_NULL_HANDLE || !m_device.waitForIdle())
            return;

        vkDestroySemaphore(m_context.device, m_semaphore, m_context.allocationCallbacks);
        m_semaphore = VK_NULL_HANDLE;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_semaphore != VK_NULL_HANDLE; }
    [[nodiscard]] Core::QueueSubmissionNativeSignal nativeSignal()const noexcept{
        return Core::QueueSubmissionNativeSignal{
            .semaphore = encode(m_semaphore),
            .value = 0u,
        };
    }


private:
    Core::GraphicsBackend::Device& m_device;
    VulkanTestDeviceContext m_context;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanTestSubmissionBlocker final : NoCopy{
public:
    VulkanTestSubmissionBlocker(
        Core::GraphicsBackend::Device& device,
        const Core::GpuPhysicalQueueId& queue
    )
        : m_device(device)
        , m_queue(queue)
        , m_context(VulkanTestDeviceProbe::capture(device))
    {
        Core::GraphicsBackend::Queue* const nativeQueue = device.getQueue(queue);
        if(!nativeQueue || !m_context.valid() || !vkCreateSemaphore)
            return;

        const VkSemaphoreTypeCreateInfo timelineInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0u,
        };
        const VkSemaphoreCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timelineInfo,
            .flags = 0u,
        };
        if(vkCreateSemaphore(m_context.device, &createInfo, m_context.allocationCallbacks, &m_semaphore) != VK_SUCCESS)
            m_semaphore = VK_NULL_HANDLE;
        if(m_semaphore == VK_NULL_HANDLE)
            return;

        nativeQueue->addWaitSemaphore(m_semaphore, s_ReleaseValue);
        m_armed = true;
    }
    ~VulkanTestSubmissionBlocker(){
        if(m_semaphore == VK_NULL_HANDLE || !release() || !drain() || !m_device.waitForIdle())
            return;

        vkDestroySemaphore(m_context.device, m_semaphore, m_context.allocationCallbacks);
        m_semaphore = VK_NULL_HANDLE;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_semaphore != VK_NULL_HANDLE && m_armed; }
    [[nodiscard]] bool release(){
        if(m_released)
            return true;
        if(m_semaphore == VK_NULL_HANDLE || !vkSignalSemaphore)
            return false;

        const VkSemaphoreSignalInfo signalInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .pNext = nullptr,
            .semaphore = m_semaphore,
            .value = s_ReleaseValue,
        };
        if(vkSignalSemaphore(m_context.device, &signalInfo) != VK_SUCCESS)
            return false;

        m_released = true;
        return true;
    }
    [[nodiscard]] bool drain(){
        if(m_drained)
            return true;
        if(!m_armed)
            return false;

        Core::QueueSubmissionDesc submission;
        submission.forceNativeSubmission = true;
        const Core::QueueSubmissionToken token = m_device.executeCommandLists(nullptr, 0u, m_queue, submission);
        if(!token.valid())
            return false;

        m_drained = true;
        return true;
    }


private:
    static inline constexpr u64 s_ReleaseValue = 1u;

    Core::GraphicsBackend::Device& m_device;
    Core::GpuPhysicalQueueId m_queue;
    VulkanTestDeviceContext m_context;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
    bool m_armed = false;
    bool m_released = false;
    bool m_drained = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

