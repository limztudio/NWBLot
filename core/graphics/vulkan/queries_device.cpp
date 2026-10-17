// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "timer_query_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TimerQuery::hasCompletedCycleWithinLock()const noexcept{
    return !m_resetRecordingOwner.commandBuffer && m_cycleGeneration == 0u && !m_recordingActive && m_completedCycleSubmission.valid() && m_completedCycleGeneration != 0u;
}

bool TimerQuery::completedCycleMatchesWithinLock(const u64 generation, const QueueSubmissionToken& submission, const GpuPhysicalQueueInfo* const queueInfo)const noexcept{
    return
        !m_resetRecordingOwner.commandBuffer
        && m_cycleGeneration == 0u
        && !m_recordingActive
        && m_completedCycleGeneration == generation
        && VulkanTimerQueryDetail::MatchesSubmissionToken(m_completedCycleSubmission, submission)
        && queueInfo
        && m_timestampValidBits != 0u
        && queueInfo->timestampValidBits == m_timestampValidBits
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TimerQueryHandle> Device::createTimerQuery(){
    u64 priorIncarnation = m_nextTimerQueryIncarnation.load(MemoryOrder::relaxed);
    while(priorIncarnation != Limit<u64>::s_Max && !m_nextTimerQueryIncarnation.compare_exchange_weak(priorIncarnation, priorIncarnation + 1u, MemoryOrder::relaxed)){}
    if(priorIncarnation == Limit<u64>::s_Max){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Timer-query identity space is exhausted"));
        return MakeUnexpected(Failure{});
    }
    const u64 incarnation = priorIncarnation + 1u;
    auto* query = NewArenaObject<TimerQuery>(m_context.objectArena, m_context, incarnation);
    if(!query || !query->m_queryPool){
        DestroyArenaObject(m_context.objectArena, query);
        return MakeUnexpected(Failure{});
    }
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

Expected<bool> Device::pollTimerQuery(TimerQuery& query){
    if(query.m_queryPool == VK_NULL_HANDLE || &query.m_context != &m_context)
        return MakeUnexpected(Failure{});

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query.m_mutex);
        if(!query.hasCompletedCycleWithinLock())
            return false;
        completedSubmission = query.m_completedCycleSubmission;
        completedGeneration = query.m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{ .index = completedSubmission.physicalQueueIndex, .deviceGeneration = completedSubmission.deviceGeneration, };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return false;

    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query.m_mutex);
        const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query.m_timestampQueue);
        if(!query.completedCycleMatchesWithinLock(completedGeneration, completedSubmission, queueInfo))
            return false;

        const auto timestamps = VulkanTimerQueryDetail::GetTimerQueryResults(m_context, query.m_queryPool);
        res = timestamps ? VK_SUCCESS : timestamps.error();
        if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }
    if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("timer query poll");
    if(res != VK_SUCCESS && res != VK_NOT_READY){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to poll timer query results: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }
    return res == VK_SUCCESS;
}

Expected<TimerQueryResult> Device::getTimerQueryResult(TimerQuery& query){
    TimerQueryResult result{};

    if(query.m_queryPool == VK_NULL_HANDLE || &query.m_context != &m_context)
        return MakeUnexpected(Failure{});

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query.m_mutex);
        if(!query.hasCompletedCycleWithinLock())
            return MakeUnexpected(Failure{});
        completedSubmission = query.m_completedCycleSubmission;
        completedGeneration = query.m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{ .index = completedSubmission.physicalQueueIndex, .deviceGeneration = completedSubmission.deviceGeneration, };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return MakeUnexpected(Failure{});

    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query.m_mutex);
        const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query.m_timestampQueue);
        if(!query.completedCycleMatchesWithinLock(completedGeneration, completedSubmission, queueInfo))
            return MakeUnexpected(Failure{});

        const auto timestamps = VulkanTimerQueryDetail::GetTimerQueryResults(m_context, query.m_queryPool);
        res = timestamps ? VK_SUCCESS : timestamps.error();
        if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
        if(res == VK_SUCCESS){
            const f64 periodNanoseconds = static_cast<f64>(m_context.physicalDeviceProperties.limits.timestampPeriod);
            const f64 secondsPerTick = periodNanoseconds * VulkanTimerQueryDetail::s_TimestampNanosecondsToSeconds;
            result.beginTicks = (*timestamps)[s_TimerQueryBeginIndex];
            result.endTicks = (*timestamps)[s_TimerQueryEndIndex];
            result.secondsPerTick = secondsPerTick;
            result.timestampValidBits = query.m_timestampValidBits;
            result.physicalQueue = query.m_timestampQueue;
            result.comparableAcrossSubmissions = supportsComparableGpuTimestamps(query.m_timestampQueue);
        }
    }
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("timer query results");
        if(res != VK_NOT_READY)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve timer query results: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
}

Expected<f32> Device::getTimerQueryTime(TimerQuery& query){
    const auto result = getTimerQueryResult(query);
    if(!result)
        return MakeUnexpected(result.error());
    return static_cast<f32>(result->durationSeconds());
}

bool Device::resetTimerQuery(TimerQuery& query){
    if(query.m_queryPool == VK_NULL_HANDLE || &query.m_context != &m_context || !m_context.hostQueryResetFeatureEnabled || !m_context.deviceDispatch.vkResetQueryPool)
        return false;

    QueueSubmissionToken priorSubmission;
    {
        ScopedLock queryLock(query.m_mutex);
        if(query.m_resetRecordingOwner.commandBuffer || query.m_cycleGeneration != 0u || query.m_recordingActive || query.m_nextResetAuthorizationGeneration == Limit<u64>::s_Max || requiresRecreation())
            return false;
        priorSubmission = query.m_completedCycleSubmission.valid() ? query.m_completedCycleSubmission : query.m_resetAuthorizationSubmission;
    }
    if(!VulkanTimerQueryDetail::IsSubmissionComplete(*this, priorSubmission))
        return false;

    ScopedLock queryLock(query.m_mutex);
    const QueueSubmissionToken currentPriorSubmission = query.m_completedCycleSubmission.valid() ? query.m_completedCycleSubmission : query.m_resetAuthorizationSubmission;
    if(
        query.m_resetRecordingOwner.commandBuffer
        || query.m_cycleGeneration != 0u
        || query.m_recordingActive
        || query.m_nextResetAuthorizationGeneration == Limit<u64>::s_Max
        || requiresRecreation()
        || !VulkanTimerQueryDetail::MatchesSubmissionToken(currentPriorSubmission, priorSubmission)
    )
        return false;
    m_context.deviceDispatch.vkResetQueryPool(m_context.device, query.m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);
    query.m_timestampQueue = {};
    query.m_timestampValidBits = 0u;
    query.m_completedCycleSubmission = {};
    query.m_completedCycleGeneration = 0u;
    query.m_resetAuthorizationSubmission = {};
    ++query.m_nextResetAuthorizationGeneration;
    query.m_resetAuthorizationGeneration = query.m_nextResetAuthorizationGeneration;
    query.m_resetAuthorizationAvailable = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

