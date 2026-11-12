// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "native_queue_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class PhysicalQueueRegistrationPolicy final{
public:
    struct Result{ enum Enum : u8{ Valid, Invalid, DuplicateQueue, DuplicatePrimary }; };
    [[nodiscard]] static Result::Enum ValidateCanonical(Span<const VulkanNativeQueueDesc> nativeQueues, Span<const VkQueueFamilyProperties> families)noexcept;
    [[nodiscard]] static Result::Enum Validate(
        const VulkanPhysicalQueueDesc& desc,
        const NativeQueueState& nativeQueue,
        Span<const GpuPhysicalQueueInfo> existingQueues,
        const Array<b8, static_cast<u32>(CommandQueue::kCount)>& primaryQueues
    )noexcept;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

