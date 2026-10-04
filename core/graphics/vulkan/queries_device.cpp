// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "timer_query_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQueryHandle Device::createTimerQuery(){
    u64 priorIncarnation = m_nextTimerQueryIncarnation.load(MemoryOrder::relaxed);
    while(
        priorIncarnation != Limit<u64>::s_Max
        && !m_nextTimerQueryIncarnation.compare_exchange_weak(
            priorIncarnation,
            priorIncarnation + 1u,
            MemoryOrder::relaxed
        )
    ){}
    if(priorIncarnation == Limit<u64>::s_Max){
        NWB_LOGGER_CRITICAL_WARNING(GLOBAL_TEXT("Vulkan: Timer-query identity space is exhausted"));
        return nullptr;
    }
    const u64 incarnation = priorIncarnation + 1u;
    auto* query = NewArenaObject<TimerQuery>(m_context.objectArena, m_context, incarnation);
    if(!query || !query->m_queryPool){
        DestroyArenaObject(m_context.objectArena, query);
        return nullptr;
    }
    return TimerQueryHandle(query, TimerQueryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

bool Device::pollTimerQuery(TimerQuery& query){
    if(query.m_queryPool == VK_NULL_HANDLE || &query.m_context != &m_context)
        return false;

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query.m_mutex);
        if(
            query.m_resetRecordingOwner.commandBuffer
            || query.m_cycleGeneration != 0u
            || query.m_recordingActive
            || !query.m_completedCycleSubmission.valid()
            || query.m_completedCycleGeneration == 0u
        )
            return false;
        completedSubmission = query.m_completedCycleSubmission;
        completedGeneration = query.m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{
        .index = completedSubmission.physicalQueueIndex,
        .deviceGeneration = completedSubmission.deviceGeneration,
    };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return false;

    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query.m_mutex);
        const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query.m_timestampQueue);
        if(
            query.m_resetRecordingOwner.commandBuffer
            || query.m_cycleGeneration != 0u
            || query.m_recordingActive
            || query.m_completedCycleGeneration != completedGeneration
            || !VulkanTimerQueryDetail::MatchesSubmissionToken(query.m_completedCycleSubmission, completedSubmission)
            || !queueInfo
            || query.m_timestampValidBits == 0u
            || queueInfo->timestampValidBits != query.m_timestampValidBits
        )
            return false;

        u64 timestamps[s_TimerQueryTimestampCount] = {};
        res = VulkanTimerQueryDetail::GetTimerQueryResults(m_context, query.m_queryPool, timestamps);
        if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }
    if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("timer query poll");
    return res == VK_SUCCESS;
}

bool Device::getTimerQueryResult(TimerQuery& query, TimerQueryResult& outResult){
    outResult = TimerQueryResult{};

    if(query.m_queryPool == VK_NULL_HANDLE || &query.m_context != &m_context)
        return false;

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query.m_mutex);
        if(
            query.m_resetRecordingOwner.commandBuffer
            || query.m_cycleGeneration != 0u
            || query.m_recordingActive
            || !query.m_completedCycleSubmission.valid()
            || query.m_completedCycleGeneration == 0u
        )
            return false;
        completedSubmission = query.m_completedCycleSubmission;
        completedGeneration = query.m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{
        .index = completedSubmission.physicalQueueIndex,
        .deviceGeneration = completedSubmission.deviceGeneration,
    };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return false;

    u64 timestamps[s_TimerQueryTimestampCount] = {};
    VkResult res = VK_SUCCESS;
    {
        ScopedLock queryLock(query.m_mutex);
        const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query.m_timestampQueue);
        if(
            query.m_resetRecordingOwner.commandBuffer
            || query.m_cycleGeneration != 0u
            || query.m_recordingActive
            || query.m_completedCycleGeneration != completedGeneration
            || !VulkanTimerQueryDetail::MatchesSubmissionToken(query.m_completedCycleSubmission, completedSubmission)
            || !queueInfo
            || query.m_timestampValidBits == 0u
            || queueInfo->timestampValidBits != query.m_timestampValidBits
        )
            return false;

        res = VulkanTimerQueryDetail::GetTimerQueryResults(m_context, query.m_queryPool, timestamps);
        if(res == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
        if(res == VK_SUCCESS){
            const f64 secondsPerTick = static_cast<f64>(m_context.physicalDeviceProperties.limits.timestampPeriod)
                * VulkanTimerQueryDetail::s_TimestampNanosecondsToSeconds
            ;
            outResult.beginTicks = timestamps[s_TimerQueryBeginIndex];
            outResult.endTicks = timestamps[s_TimerQueryEndIndex];
            outResult.secondsPerTick = secondsPerTick;
            outResult.timestampValidBits = query.m_timestampValidBits;
            outResult.physicalQueue = query.m_timestampQueue;
            outResult.comparableAcrossSubmissions = supportsComparableGpuTimestamps(query.m_timestampQueue);
        }
    }
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("timer query results");
        if(res != VK_NOT_READY)
            NWB_LOGGER_WARNING(GLOBAL_TEXT("Vulkan: Failed to retrieve timer query results: {}"), ResultToString(res));
        return false;
    }

    return outResult.valid();
}

f32 Device::getTimerQueryTime(TimerQuery& query){
    TimerQueryResult result;
    if(!getTimerQueryResult(query, result))
        return 0.f;
    return static_cast<f32>(result.durationSeconds());
}

bool Device::resetTimerQuery(TimerQuery& query){
    if(
        query.m_queryPool == VK_NULL_HANDLE
        || &query.m_context != &m_context
        || !m_context.hostQueryResetFeatureEnabled
        || !m_context.deviceDispatch.vkResetQueryPool
    )
        return false;

    QueueSubmissionToken priorSubmission;
    {
        ScopedLock queryLock(query.m_mutex);
        if(
            query.m_resetRecordingOwner.commandBuffer
            || query.m_cycleGeneration != 0u
            || query.m_recordingActive
            || query.m_nextResetAuthorizationGeneration == Limit<u64>::s_Max
            || requiresRecreation()
        )
            return false;
        priorSubmission = query.m_completedCycleSubmission.valid()
            ? query.m_completedCycleSubmission
            : query.m_resetAuthorizationSubmission
        ;
    }
    if(!VulkanTimerQueryDetail::IsSubmissionComplete(*this, priorSubmission))
        return false;

    ScopedLock queryLock(query.m_mutex);
    const QueueSubmissionToken currentPriorSubmission = query.m_completedCycleSubmission.valid()
        ? query.m_completedCycleSubmission
        : query.m_resetAuthorizationSubmission
    ;
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

