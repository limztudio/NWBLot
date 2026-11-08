// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physical_queue_registration_policy.h"
#include "device_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


PhysicalQueueRegistrationPolicy::Result PhysicalQueueRegistrationPolicy::validate(
    const VulkanPhysicalQueueDesc& desc,
    const NativeQueueState& nativeQueue,
    const Span<const GpuPhysicalQueueInfo> existingQueues,
    const Array<b8, static_cast<u32>(CommandQueue::kCount)>& primaryQueues
)noexcept{
    const u32 queueClassIndex = static_cast<u32>(desc.queueClass);
    const u8 requiredCapabilities = static_cast<u8>(VulkanDetail::DeviceMinimumQueueCapabilities(desc.queueClass));
    const u8 providedCapabilities = static_cast<u8>(desc.capabilities);
    const u8 knownCapabilities = static_cast<u8>(static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute) | static_cast<u8>(GpuQueueCapability::Transfer));
    const u8 graphicsOrComputeCapabilities = static_cast<u8>(static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute));
    constexpr u32 s_MinTimestampValidBits = VulkanDefaults::s_VulkanDefaultMinTimestampValidBits;
    constexpr u32 s_MaxTimestampValidBits = VulkanDefaults::s_VulkanDefaultMaxTimestampValidBits;
    const b8 missingRequiredTransferCapability = (providedCapabilities & graphicsOrComputeCapabilities) != 0u && (providedCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u;
    if(
        nativeQueue.queue == VK_NULL_HANDLE
        || nativeQueue.familyIndex == Limit<u32>::s_Max
        || nativeQueue.queueIndex == Limit<u32>::s_Max
        || queueClassIndex >= static_cast<u32>(CommandQueue::kCount)
        || requiredCapabilities == 0u
        || (providedCapabilities & requiredCapabilities) != requiredCapabilities
        || (providedCapabilities & static_cast<u8>(~knownCapabilities)) != 0u
        || missingRequiredTransferCapability
        || (desc.timestampValidBits != 0u && (desc.timestampValidBits < s_MinTimestampValidBits || desc.timestampValidBits > s_MaxTimestampValidBits))
        || existingQueues.size() >= static_cast<usize>(Limit<u16>::s_Max)
    ){
        return Result::Invalid;
    }

    for(const GpuPhysicalQueueInfo& existing : existingQueues){
        if(existing.familyIndex == nativeQueue.familyIndex && existing.queueIndex == nativeQueue.queueIndex){
            return Result::DuplicateQueue;
        }
    }
    if(desc.primaryForClass && primaryQueues[queueClassIndex]){
        return Result::DuplicatePrimary;
    }

    return Result::Valid;
}

PhysicalQueueRegistrationPolicy::Result PhysicalQueueRegistrationPolicy::validateCanonical(const Span<const VulkanNativeQueueDesc> nativeQueues, const Span<const VkQueueFamilyProperties> families)noexcept{
    for(usize nativeQueueIndex = 0u; nativeQueueIndex < nativeQueues.size(); ++nativeQueueIndex){
        const VulkanNativeQueueDesc& nativeQueue = nativeQueues[nativeQueueIndex];
        if(
            nativeQueue.queue == VK_NULL_HANDLE
            || nativeQueue.familyIndex == Limit<u32>::s_Max
            || nativeQueue.queueIndex == Limit<u32>::s_Max
            || nativeQueue.familyIndex >= families.size()
            || nativeQueue.queueIndex >= families[nativeQueue.familyIndex].queueCount
        ){
            return Result::Invalid;
        }
        for(usize existingIndex = 0u; existingIndex < nativeQueueIndex; ++existingIndex){
            const VulkanNativeQueueDesc& existing = nativeQueues[existingIndex];
            const b8 duplicateCoordinates = existing.familyIndex == nativeQueue.familyIndex && existing.queueIndex == nativeQueue.queueIndex;
            if(duplicateCoordinates || existing.queue == nativeQueue.queue){
                return Result::DuplicateQueue;
            }
        }
    }
    return Result::Valid;
}



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

