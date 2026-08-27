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

namespace CompatibilityPresentTransitionPolicy{
    enum Enum : u8{
        Invalid,
        TransitionFromUnknown,
        PreservePresent,
    };
};

struct SwapChainImagePresentationState{
    bool hasPresented = false;

    [[nodiscard]] ResourceStates::Mask nativeInitialState()const noexcept{
        return hasPresented ? ResourceStates::Present : ResourceStates::Unknown;
    }

    void observeQueuePresentWaitDisposition(const QueuePresentWaitDisposition::Enum disposition)noexcept{
        if(disposition == QueuePresentWaitDisposition::Consumed)
            hasPresented = true;
    }
};

struct SwapChainSurfaceFormatSelection{
    VkSurfaceFormatKHR surfaceFormat = {};
    Format::Enum backBufferFormat = Format::UNKNOWN;
    SwapChainOutputMode::Enum outputMode = SwapChainOutputMode::SDR;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Vulkan specifies that presentation waits are still executed for the WSI failures listed below. Host/device OOM
// rejects the operation before it is enqueued and leaves synchronization untouched. Unknown errors stay distinct so
// callers can quarantine the acquired frame instead of guessing that its binary semaphore was consumed.
inline QueuePresentWaitDisposition::Enum ClassifyQueuePresentWaitDisposition(const VkResult result)noexcept{
    switch(result){
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
    case VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT:
        return QueuePresentWaitDisposition::Consumed;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return QueuePresentWaitDisposition::NotConsumed;
    case VK_ERROR_DEVICE_LOST:
        return QueuePresentWaitDisposition::DeviceLost;
    default:
        return QueuePresentWaitDisposition::Unknown;
    }
}

// A never-presented acquired image has no reusable native layout. Its compatibility command list must transition
// from Unknown directly instead of seeding the descriptor's retained Present state as if WSI had established it.
inline CompatibilityPresentTransitionPolicy::Enum ResolveCompatibilityPresentTransitionPolicy(
    const ResourceStates::Mask nativeInitialState
)noexcept{
    if(nativeInitialState == ResourceStates::Unknown)
        return CompatibilityPresentTransitionPolicy::TransitionFromUnknown;
    if(nativeInitialState == ResourceStates::Present)
        return CompatibilityPresentTransitionPolicy::PreservePresent;
    return CompatibilityPresentTransitionPolicy::Invalid;
}

// Frame acquisition queues its binary semaphore on the primary physical Graphics transport. A secondary Graphics
// queue therefore cannot safely become the terminal swap-chain signal source until the acquired-image and
// swap-chain sharing contract is extended to name that transport explicitly. Keep this policy separate from broad
// CommandQueue::Graphics validation so optional same-class routing cannot accidentally make a windowed
// presentation packet cross an unprepared ownership boundary.
inline bool IsPrimaryGraphicsPresentationQueue(
    const GpuPhysicalQueueId& primaryGraphicsQueue,
    const GpuPhysicalQueueInfo* const executionQueue
)noexcept{
    return primaryGraphicsQueue.valid()
        && executionQueue
        && executionQueue->queueClass == CommandQueue::Graphics
        && executionQueue->id == primaryGraphicsQueue
    ;
}

inline bool SurfaceFormatSupports(
    const VkSurfaceFormatKHR& supported,
    const VkFormat format,
    const VkColorSpaceKHR colorSpace
){
    return supported.colorSpace == colorSpace && (supported.format == format || supported.format == VK_FORMAT_UNDEFINED);
}

inline bool SelectSurfaceFormat(
    const VkSurfaceFormatKHR* const supportedFormats,
    const u32 supportedFormatCount,
    const Format::Enum requestedSdrFormat,
    const bool hdr10Allowed,
    SwapChainSurfaceFormatSelection& outSelection
){
    outSelection = {};
    if(!supportedFormats || supportedFormatCount == 0u)
        return false;

    if(hdr10Allowed){
        for(u32 formatIndex = 0u; formatIndex < supportedFormatCount; ++formatIndex){
            const VkSurfaceFormatKHR& supported = supportedFormats[formatIndex];
            if(!SurfaceFormatSupports(
                supported,
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_COLOR_SPACE_HDR10_ST2084_EXT
            ))
                continue;

            outSelection.surfaceFormat = supported;
            outSelection.surfaceFormat.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            outSelection.surfaceFormat.colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
            outSelection.backBufferFormat = Format::R10G10B10A2_UNORM;
            outSelection.outputMode = SwapChainOutputMode::HDR10;
            return true;
        }
    }

    struct SdrCandidate{
        VkFormat format = VK_FORMAT_UNDEFINED;
        Format::Enum rhiFormat = Format::UNKNOWN;
    };
    const VkFormat requestedVkFormat = ConvertFormat(requestedSdrFormat);
    const SdrCandidate candidates[] = {
        { requestedVkFormat, requestedSdrFormat },
        // RGBA8/BGRA8 sRGB are interchangeable presentation fallbacks for the engine's default request.
        { VK_FORMAT_B8G8R8A8_SRGB, Format::BGRA8_UNORM_SRGB },
        { VK_FORMAT_R8G8B8A8_SRGB, Format::RGBA8_UNORM_SRGB },
    };
    for(const SdrCandidate& candidate : candidates){
        if(candidate.format == VK_FORMAT_UNDEFINED)
            continue;
        // Respect a caller's non-default SDR format. The two portable fallbacks are only for the engine default.
        if(
            candidate.rhiFormat != requestedSdrFormat
            && requestedSdrFormat != Format::RGBA8_UNORM_SRGB
            && requestedSdrFormat != Format::BGRA8_UNORM_SRGB
        )
            continue;

        for(u32 formatIndex = 0u; formatIndex < supportedFormatCount; ++formatIndex){
            const VkSurfaceFormatKHR& supported = supportedFormats[formatIndex];
            if(!SurfaceFormatSupports(supported, candidate.format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
                continue;

            outSelection.surfaceFormat = supported;
            outSelection.surfaceFormat.format = candidate.format;
            outSelection.surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            outSelection.backBufferFormat = candidate.rhiFormat;
            outSelection.outputMode = SwapChainOutputMode::SDR;
            return true;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

