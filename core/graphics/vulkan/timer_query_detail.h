// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTimerQueryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f64 s_TimestampNanosecondsToSeconds = 1e-9;

inline VkResult GetTimerQueryResults(const VulkanContext& context, const VkQueryPool queryPool, u64 (&timestamps)[s_TimerQueryTimestampCount]){
    return context.deviceDispatch.vkGetQueryPoolResults(
        context.device,
        queryPool,
        s_TimerQueryBeginIndex,
        s_TimerQueryTimestampCount,
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT
    );
}

[[nodiscard]] inline bool MatchesSubmissionToken(
    const QueueSubmissionToken& lhs,
    const QueueSubmissionToken& rhs
)noexcept{
    return
        lhs.queue == rhs.queue
        && lhs.value == rhs.value
        && lhs.physicalQueueIndex == rhs.physicalQueueIndex
        && lhs.deviceGeneration == rhs.deviceGeneration
    ;
}

[[nodiscard]] inline bool IsSubmissionComplete(Device& device, const QueueSubmissionToken& token){
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity())
        return false;

    return device.queueGetCompletedInstance(GpuPhysicalQueueId{
        .index = token.physicalQueueIndex,
        .deviceGeneration = token.deviceGeneration,
    }) >= token.value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

