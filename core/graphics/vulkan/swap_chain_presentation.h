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


[[nodiscard]] inline constexpr b8 IsQueuePresentationAccepted(const VkResult result)noexcept{
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

[[nodiscard]] QueuePresentWaitDisposition::Enum ClassifyQueuePresentWaitDisposition(VkResult result)noexcept;

[[nodiscard]] DirectPresentTransitionPolicy::Enum ResolveDirectPresentTransitionPolicy(ResourceStates::Mask nativeInitialState)noexcept;

[[nodiscard]] inline b8 IsPrimaryGraphicsPresentationQueue(const GpuPhysicalQueueId& primaryGraphicsQueue, const GpuPhysicalQueueInfo* const executionQueue)noexcept{
    return primaryGraphicsQueue.valid() && executionQueue && executionQueue->queueClass == CommandQueue::Graphics && executionQueue->id == primaryGraphicsQueue;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

