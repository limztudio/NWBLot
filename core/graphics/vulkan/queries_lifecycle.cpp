// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_queries{


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
        token.physicalQueueIndex,
        token.deviceGeneration,
    }) >= token.value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context, const u64 incarnation)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_incarnation(incarnation)
    , m_context(context)
{
    auto queryPoolInfo = VulkanDetail::MakeVkStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = s_TimerQueryTimestampCount;

    const VkResult res = m_context.deviceDispatch.vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &m_queryPool);
    if(res != VK_SUCCESS){
        m_queryPool = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery: {}"), ResultToString(res));
    }
}
TimerQuery::~TimerQuery(){
    if(m_queryPool != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyQueryPool(m_context.device, m_queryPool, m_context.allocationCallbacks);
        m_queryPool = VK_NULL_HANDLE;
    }
}

bool TimerQuery::discardUnacceptedRecording(const TimerQueryRecordingToken& token)noexcept{
    if(!token.valid() || token.query != this || token.queryIncarnation != m_incarnation)
        return false;

    NothrowScopedLock lock(m_mutex);
    if(
        m_beginAccepted
        || m_nextRecordingGeneration != token.generation
        || m_lastAcceptedRecordingGeneration == token.generation
        || (
            m_completedCycleGeneration == token.generation
            && m_completedCycleSubmission.valid()
        )
    )
        return false;
    const bool ownsResetAuthorization =
        token.resetAuthorizationGeneration != 0u
        && m_resetAuthorizationAvailable
        && m_resetAuthorizationGeneration == token.resetAuthorizationGeneration
    ;
    if(m_cycleGeneration == 0u){
        if(ownsResetAuthorization){
            m_resetAuthorizationAvailable = false;
            m_resetAuthorizationGeneration = 0u;
        }
        return true;
    }
    if(m_cycleGeneration != token.generation)
        return false;

    m_timestampQueue = m_cycleBaselineQueue;
    m_timestampValidBits = m_cycleBaselineValidBits;
    m_completedCycleSubmission = m_cycleBaselineCompletion;
    m_completedCycleGeneration = m_cycleBaselineCompletionGeneration;
    m_recordingActive = m_cycleBaselineActive;
    m_cycleBaselineQueue = {};
    m_cycleQueue = {};
    m_beginRecordingOwner = {};
    m_endRecordingOwner = {};
    m_cycleBaselineValidBits = 0u;
    m_cycleValidBits = 0u;
    m_cycleBaselineCompletion = {};
    m_cycleBaselineCompletionGeneration = 0u;
    m_cycleGeneration = 0u;
    m_cycleBaselineActive = false;
    m_cycleInvalidated = false;
    if(ownsResetAuthorization){
        m_resetAuthorizationAvailable = false;
        m_resetAuthorizationGeneration = 0u;
    }
    return true;
}

bool TimerQuery::releaseUnacceptedEndForRecovery(const TimerQueryRecordingToken& token)noexcept{
    if(!token.valid() || token.query != this || token.queryIncarnation != m_incarnation)
        return false;

    NothrowScopedLock lock(m_mutex);
    if(
        m_cycleGeneration == 0u
        || m_cycleGeneration != token.generation
        || m_lastAcceptedRecordingGeneration != token.generation
        || !m_beginAccepted
        || !m_recordingActive
        || m_cycleInvalidated
        || m_cycleQueue != token.physicalQueue
        || (
            m_completedCycleGeneration == token.generation
            && m_completedCycleSubmission.valid()
        )
    )
        return false;

    m_endRecordingOwner = {};
    return true;
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

