// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace QueuePresentWaitDisposition{
    enum Enum : u8{
        Consumed,
        NotConsumed,
        DeviceLost,
        Unknown,
    };
};

namespace DirectPresentTransitionPolicy{
    enum Enum : u8{
        Invalid,
        TransitionFromUnknown,
        PreservePresent,
    };
};

struct SwapChainImagePresentationState{
    b8 hasPresented = false;


public:
    [[nodiscard]] ResourceStates::Mask nativeInitialState()const noexcept{
        return hasPresented ? ResourceStates::Present : ResourceStates::Unknown;
    }

    void observeQueuePresentWaitDisposition(QueuePresentWaitDisposition::Enum disposition)noexcept;
};

struct SwapChainSurfaceFormatSelection{
    VkSurfaceFormatKHR surfaceFormat = {};
    Format::Enum backBufferFormat = Format::UNKNOWN;
    SwapChainOutputMode::Enum outputMode = SwapChainOutputMode::SDR;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Acceptance is narrower than binary-wait consumption: an out-of-date surface consumes its wait without presenting.
[[nodiscard]] inline constexpr b8 IsQueuePresentationAccepted(const VkResult result)noexcept{
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

// Vulkan specifies that presentation waits are still executed for the WSI failures listed below.
// Host/device OOM rejects the operation before it is enqueued and leaves synchronization untouched.
// Unknown errors stay distinct so callers can quarantine the acquired frame instead of guessing that its binary semaphore was consumed.
[[nodiscard]] QueuePresentWaitDisposition::Enum ClassifyQueuePresentWaitDisposition(VkResult result)noexcept;

// A never-presented image has no reusable layout. Its list transitions from Unknown, never seeding retained Present as if WSI set it.
[[nodiscard]] DirectPresentTransitionPolicy::Enum ResolveDirectPresentTransitionPolicy(ResourceStates::Mask nativeInitialState)noexcept;

// Acquisition signals on the primary Graphics transport; a secondary transport needs an explicit sharing-contract extension first.
// Kept separate from broad Graphics validation so same-class routing stays safe.
[[nodiscard]] inline b8 IsPrimaryGraphicsPresentationQueue(const GpuPhysicalQueueId& primaryGraphicsQueue, const GpuPhysicalQueueInfo* const executionQueue)noexcept{
    return primaryGraphicsQueue.valid() && executionQueue && executionQueue->queueClass == CommandQueue::Graphics && executionQueue->id == primaryGraphicsQueue;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

