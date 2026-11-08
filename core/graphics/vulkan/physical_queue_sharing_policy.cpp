// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physical_queue_sharing_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


PhysicalQueueSharingPolicy::Result PhysicalQueueSharingPolicy::Resolve(const Span<const GpuPhysicalQueueInfo> queues, const u32 graphics, const u32 compute, const u32 transfer)noexcept{
    const u32 invalid = Limit<u32>::s_Max;
    const auto primaryFamily = [](const u32 family)noexcept{
        return family == invalid ? s_InvalidQueueFamilyIndex : static_cast<i32>(family);
    };
    const auto auxiliaryFamily = [queues](const u32 primary, const CommandQueue::Enum queueClass)noexcept{
        if(primary == invalid)
            return s_InvalidQueueFamilyIndex;
        for(const GpuPhysicalQueueInfo& queue : queues){
            if(queue.queueClass == queueClass && queue.familyIndex != primary)
                return static_cast<i32>(queue.familyIndex);
        }
        return s_InvalidQueueFamilyIndex;
    };
    return Result{
        .graphics = primaryFamily(graphics),
        .auxiliaryGraphics = auxiliaryFamily(graphics, CommandQueue::Graphics),
        .compute = primaryFamily(compute),
        .auxiliaryCompute = auxiliaryFamily(compute, CommandQueue::Compute),
        .transfer = primaryFamily(transfer),
        .auxiliaryTransfer = auxiliaryFamily(transfer, CommandQueue::Transfer),
        .asyncComputeLaneEnabled = graphics != invalid && compute != invalid && compute != graphics,
        .transferQueueEnabled = transfer != invalid && (graphics == invalid || transfer != graphics) && (compute == invalid || transfer != compute),
    };
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
