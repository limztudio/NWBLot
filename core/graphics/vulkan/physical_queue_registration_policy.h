// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "native_queue_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class PhysicalQueueRegistrationPolicy final{
public:
    enum class Result : u8{ Valid, Invalid, DuplicateQueue, DuplicatePrimary };
    [[nodiscard]] static Result validateCanonical(Span<const VulkanNativeQueueDesc> nativeQueues, Span<const VkQueueFamilyProperties> families)noexcept;
    [[nodiscard]] static Result validate(
        const VulkanPhysicalQueueDesc& desc,
        const NativeQueueState& nativeQueue,
        Span<const GpuPhysicalQueueInfo> existingQueues,
        const Array<b8, static_cast<u32>(CommandQueue::kCount)>& primaryQueues
    )noexcept;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
