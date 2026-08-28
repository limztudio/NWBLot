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

