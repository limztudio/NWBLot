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


EventQuery::EventQuery(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{
    auto fenceInfo = VulkanDetail::MakeVkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

    const VkResult res = m_context.deviceDispatch.vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &m_fence);
    if(res != VK_SUCCESS){
        m_fence = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create fence for EventQuery: {}"), ResultToString(res));
    }
}

EventQuery::~EventQuery(){
    if(m_fence != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyFence(m_context.device, m_fence, m_context.allocationCallbacks);
        m_fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context, const u64 incarnation)
    : RefCounter<GraphicsResource>(context.threadPool)
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

    ScopedLock lock(m_mutex);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQueryHandle Device::createEventQuery(){
    auto* query = NewArenaObject<EventQuery>(m_context.objectArena, m_context);
    if(!query || !query->m_fence){
        DestroyArenaObject(m_context.objectArena, query);
        return nullptr;
    }
    return EventQueryHandle(query, EventQueryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

bool Device::setEventQuery(EventQuery* queryResource, CommandQueue::Enum queue){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return false;

    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    Queue* const q = getQueue(queue);
    if(!q){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to set event query: requested queue is not available"));
        return false;
    }

    ScopedLock queryLock(query->m_mutex);
    if(query->m_started){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Refusing to reset an event query fence while its submission is pending."));
        return false;
    }
    ScopedLock lock(q->m_mutex);
    if(submissionsBlocked())
        return false;

    VkResult res = m_context.deviceDispatch.vkResetFences(m_context.device, 1, &query->m_fence);
    if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("event query reset");
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset event query fence before submit: {}"), ResultToString(res));
        return false;
    }

    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    bool submissionSuppressed = false;
    {
        ScopedLock hostLock(q->m_nativeQueue.hostMutex);
        submissionSuppressed = submissionsBlocked();
        if(!submissionSuppressed)
            res = m_context.deviceDispatch.vkQueueSubmit(q->m_nativeQueue.queue, 1, &submitInfo, query->m_fence);
    }
    if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("event query submit");
    if(submissionSuppressed || res != VK_SUCCESS){
        if(submissionSuppressed)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Event query submission was suppressed because the device requires recreation."));
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to submit event query fence: {}"), ResultToString(res));
        return false;
    }

    query->m_started = true;
    return true;
}

bool Device::pollEventQuery(EventQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    ScopedLock queryLock(query->m_mutex);
    if(!query->m_started)
        return true;

    const VkResult res = m_context.deviceDispatch.vkGetFenceStatus(m_context.device, query->m_fence);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("event query poll");
    return res == VK_SUCCESS;
}

bool Device::waitEventQuery(EventQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_fence == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    ScopedLock queryLock(query->m_mutex);
    if(!query->m_started)
        return true;

    const VkResult res = m_context.deviceDispatch.vkWaitForFences(m_context.device, 1, &query->m_fence, VK_TRUE, UINT64_MAX);
    if(res == VK_SUCCESS)
        query->m_started = false;
    else{
        if(res == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("event query wait");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait event query fence: {}"), ResultToString(res));
    }
    return res == VK_SUCCESS;
}

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
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Timer-query identity space is exhausted"));
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

bool Device::pollTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query->m_mutex);
        if(
            query->m_resetRecordingOwner.commandBuffer
            || query->m_cycleGeneration != 0u
            || query->m_recordingActive
            || !query->m_completedCycleSubmission.valid()
            || query->m_completedCycleGeneration == 0u
        )
            return false;
        completedSubmission = query->m_completedCycleSubmission;
        completedGeneration = query->m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{
        completedSubmission.physicalQueueIndex,
        completedSubmission.deviceGeneration,
    };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return false;

    ScopedLock queryLock(query->m_mutex);
    const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query->m_timestampQueue);
    if(
        query->m_resetRecordingOwner.commandBuffer
        || query->m_cycleGeneration != 0u
        || query->m_recordingActive
        || query->m_completedCycleGeneration != completedGeneration
        || !__hidden_vulkan_queries::MatchesSubmissionToken(query->m_completedCycleSubmission, completedSubmission)
        || !queueInfo
        || query->m_timestampValidBits == 0u
        || queueInfo->timestampValidBits != query->m_timestampValidBits
    )
        return false;

    u64 timestamps[s_TimerQueryTimestampCount] = {};
    const VkResult res = __hidden_vulkan_queries::GetTimerQueryResults(m_context, query->m_queryPool, timestamps);
    if(res == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("timer query poll");
    return res == VK_SUCCESS;
}

bool Device::getTimerQueryResult(TimerQuery* queryResource, TimerQueryResult& outResult){
    outResult = TimerQueryResult{};

    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE || &query->m_context != &m_context)
        return false;

    QueueSubmissionToken completedSubmission;
    u64 completedGeneration = 0u;
    {
        ScopedLock queryLock(query->m_mutex);
        if(
            query->m_resetRecordingOwner.commandBuffer
            || query->m_cycleGeneration != 0u
            || query->m_recordingActive
            || !query->m_completedCycleSubmission.valid()
            || query->m_completedCycleGeneration == 0u
        )
            return false;
        completedSubmission = query->m_completedCycleSubmission;
        completedGeneration = query->m_completedCycleGeneration;
    }

    const GpuPhysicalQueueId completionQueue{
        completedSubmission.physicalQueueIndex,
        completedSubmission.deviceGeneration,
    };
    if(queueGetCompletedInstance(completionQueue) < completedSubmission.value)
        return false;

    ScopedLock queryLock(query->m_mutex);
    const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(query->m_timestampQueue);
    if(
        query->m_resetRecordingOwner.commandBuffer
        || query->m_cycleGeneration != 0u
        || query->m_recordingActive
        || query->m_completedCycleGeneration != completedGeneration
        || !__hidden_vulkan_queries::MatchesSubmissionToken(query->m_completedCycleSubmission, completedSubmission)
        || !queueInfo
        || query->m_timestampValidBits == 0u
        || queueInfo->timestampValidBits != query->m_timestampValidBits
    )
        return false;

    u64 timestamps[s_TimerQueryTimestampCount] = {};
    const VkResult res = __hidden_vulkan_queries::GetTimerQueryResults(m_context, query->m_queryPool, timestamps);
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("timer query results");
        if(res != VK_NOT_READY)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve timer query results: {}"), ResultToString(res));
        return false;
    }

    const f64 secondsPerTick = static_cast<f64>(m_context.physicalDeviceProperties.limits.timestampPeriod)
        * __hidden_vulkan_queries::s_TimestampNanosecondsToSeconds
    ;
    outResult.beginTicks = timestamps[s_TimerQueryBeginIndex];
    outResult.endTicks = timestamps[s_TimerQueryEndIndex];
    outResult.secondsPerTick = secondsPerTick;
    outResult.timestampValidBits = query->m_timestampValidBits;
    outResult.physicalQueue = query->m_timestampQueue;
    outResult.comparableAcrossSubmissions = supportsComparableGpuTimestamps(query->m_timestampQueue);
    return outResult.valid();
}

f32 Device::getTimerQueryTime(TimerQuery* queryResource){
    TimerQueryResult result;
    if(!getTimerQueryResult(queryResource, result))
        return 0.f;
    return static_cast<f32>(result.durationSeconds());
}

bool Device::resetTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(
        !query
        || query->m_queryPool == VK_NULL_HANDLE
        || &query->m_context != &m_context
        || !m_context.hostQueryResetFeatureEnabled
        || !m_context.deviceDispatch.vkResetQueryPool
    )
        return false;

    QueueSubmissionToken priorSubmission;
    {
        ScopedLock queryLock(query->m_mutex);
        if(
            query->m_resetRecordingOwner.commandBuffer
            || query->m_cycleGeneration != 0u
            || query->m_recordingActive
            || query->m_nextResetAuthorizationGeneration == Limit<u64>::s_Max
            || requiresRecreation()
        )
            return false;
        priorSubmission = query->m_completedCycleSubmission.valid()
            ? query->m_completedCycleSubmission
            : query->m_resetAuthorizationSubmission
        ;
    }
    if(!__hidden_vulkan_queries::IsSubmissionComplete(*this, priorSubmission))
        return false;

    ScopedLock queryLock(query->m_mutex);
    const QueueSubmissionToken currentPriorSubmission = query->m_completedCycleSubmission.valid()
        ? query->m_completedCycleSubmission
        : query->m_resetAuthorizationSubmission
    ;
    if(
        query->m_resetRecordingOwner.commandBuffer
        || query->m_cycleGeneration != 0u
        || query->m_recordingActive
        || query->m_nextResetAuthorizationGeneration == Limit<u64>::s_Max
        || requiresRecreation()
        || !__hidden_vulkan_queries::MatchesSubmissionToken(currentPriorSubmission, priorSubmission)
    )
        return false;
    m_context.deviceDispatch.vkResetQueryPool(m_context.device, query->m_queryPool, s_TimerQueryBeginIndex, s_TimerQueryTimestampCount);
    query->m_timestampQueue = {};
    query->m_timestampValidBits = 0u;
    query->m_completedCycleSubmission = {};
    query->m_completedCycleGeneration = 0u;
    query->m_resetAuthorizationSubmission = {};
    ++query->m_nextResetAuthorizationGeneration;
    query->m_resetAuthorizationGeneration = query->m_nextResetAuthorizationGeneration;
    query->m_resetAuthorizationAvailable = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::resetTimerQuery(TimerQuery* queryResource){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE || &query->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot reset an invalid or foreign timer query"));
        invalidateCommandRecording();
        return false;
    }

    if(!validateCommandRecordingScope(NWB_TEXT("reset timer query")))
        return false;
    if(!canResetTimerQueryHere()){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Cannot reset a timer query outside recording or on an exact physical queue without Graphics or Compute capability")
        );
        invalidateCommandRecording();
        return false;
    }

    QueueSubmissionToken priorSubmission;
    {
        ScopedLock queryLock(query->m_mutex);
        const bool resetOwnedByCurrentCommandBuffer =
            query->m_resetRecordingOwner.commandBuffer == m_currentCmdBuf.get()
            && query->m_resetRecordingOwner.recordingID == m_currentCmdBuf->m_recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
        ;
        if(
            query->m_recordingActive
            || query->m_cycleGeneration != 0u
            || (query->m_resetRecordingOwner.commandBuffer && !resetOwnedByCurrentCommandBuffer)
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot reset a timer query while another recording transaction is active"));
            invalidateCommandRecording();
            return false;
        }
        priorSubmission = query->m_completedCycleSubmission.valid()
            ? query->m_completedCycleSubmission
            : query->m_resetAuthorizationSubmission
        ;
    }
    const bool priorSubmissionObservedComplete =
        __hidden_vulkan_queries::IsSubmissionComplete(m_device, priorSubmission)
    ;

    bool resetRecorded = false;
    {
        ScopedLock queryLock(query->m_mutex);
        const QueueSubmissionToken currentPriorSubmission = query->m_completedCycleSubmission.valid()
            ? query->m_completedCycleSubmission
            : query->m_resetAuthorizationSubmission
        ;
        if(
            !query->m_recordingActive
            && query->m_cycleGeneration == 0u
            && __hidden_vulkan_queries::MatchesSubmissionToken(currentPriorSubmission, priorSubmission)
            && (
                query->m_resetRecordingOwner.commandBuffer
                || query->m_nextResetAuthorizationGeneration != Limit<u64>::s_Max
            )
            && (
                !query->m_resetRecordingOwner.commandBuffer
                || (
                    query->m_resetRecordingOwner.commandBuffer == m_currentCmdBuf.get()
                    && query->m_resetRecordingOwner.recordingID == m_currentCmdBuf->m_recordingID
                )
            )
        ){
            TrackedCommandBuffer::TimerQueryRecordingClaim& claim =
                m_currentCmdBuf->findOrAppendTimerQueryRecordingClaim(*query)
            ;
            if(!query->m_resetRecordingOwner.commandBuffer){
                ++query->m_nextResetAuthorizationGeneration;
                query->m_resetRecordingOwner = TimerQuery::RecordingOwner{
                    .commandBuffer = m_currentCmdBuf.get(),
                    .recordingID = m_currentCmdBuf->m_recordingID,
                };
                query->m_resetRecordingAuthorizationGeneration = query->m_nextResetAuthorizationGeneration;
            }
            claim.queue = m_creationDesc.physicalQueue;
            claim.prerequisiteSubmission = priorSubmission;
            claim.prerequisiteObservedComplete = priorSubmissionObservedComplete;
            claim.resetRecordingAuthorizationGeneration = query->m_resetRecordingAuthorizationGeneration;
            claim.recordingID = m_currentCmdBuf->m_recordingID;
            claim.recordsReset = true;

            // Device-timeline reset: recorded into the command buffer so the validation layer can order it before
            // timestamp writes. vkCmdResetQueryPool is illegal inside a render pass instance.
            m_context.deviceDispatch.vkCmdResetQueryPool(
                m_currentCmdBuf->m_cmdBuf,
                query->m_queryPool,
                s_TimerQueryBeginIndex,
                s_TimerQueryTimestampCount
            );
            resetRecorded = true;
        }
    }
    if(!resetRecorded){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot reset a timer query while another recording transaction is active"));
        invalidateCommandRecording();
    }
    return resetRecorded;
}

bool CommandList::canRecordTimerQueryHere()const{
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    constexpr u8 s_KnownCapabilityBits = static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Transfer)
    ;
    return
        !m_commandRecordingFailed
        && matchesActiveNativeLeaseIdentity()
        && m_creationDesc.physicalQueue.valid()
        && queueInfo
        && queueInfo->id == m_creationDesc.physicalQueue
        && queueInfo->queueClass == m_creationDesc.queueType
        && (static_cast<u8>(queueInfo->capabilities) & s_KnownCapabilityBits) != 0u
        && queueInfo->timestampValidBits > 0u
        && queueInfo->timestampValidBits <= 64u
    ;
}

bool CommandList::canResetTimerQueryHere()const{
    if(m_renderPassActive || !canRecordTimerQueryHere())
        return false;

    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    constexpr u8 s_ResetCapableBits = static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
    ;
    return (static_cast<u8>(queueInfo->capabilities) & s_ResetCapableBits) != 0u;
}

bool CommandList::beginTimerQuery(TimerQuery* queryResource, TimerQueryRecordingToken& outToken){
    outToken = {};
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE || &query->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to begin an invalid or foreign timer query"));
        invalidateCommandRecording();
        return false;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("begin timer query")))
        return false;

    if(!m_renderPassActive && !canResetTimerQueryHere()){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Cannot begin a timer query outside rendering on an exact physical queue without Graphics or Compute capability")
        );
        invalidateCommandRecording();
        return false;
    }

    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    if(!queueInfo || queueInfo->timestampValidBits == 0u || queueInfo->timestampValidBits > 64u){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to begin timer query on a queue without timestamp support"));
        invalidateCommandRecording();
        return false;
    }

    QueueSubmissionToken priorSubmission;
    {
        ScopedLock queryLock(query->m_mutex);
        const bool resetOwnedByCurrentCommandBuffer =
            query->m_resetRecordingOwner.commandBuffer == m_currentCmdBuf.get()
            && query->m_resetRecordingOwner.recordingID == m_currentCmdBuf->m_recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
        ;
        if(
            query->m_recordingActive
            || query->m_cycleGeneration != 0u
            || (query->m_resetRecordingOwner.commandBuffer && !resetOwnedByCurrentCommandBuffer)
            || (m_renderPassActive && !resetOwnedByCurrentCommandBuffer && !query->m_resetAuthorizationAvailable)
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Refusing a timer-query begin without an exclusive cycle and ordered reset"));
            invalidateCommandRecording();
            return false;
        }

        if(m_renderPassActive && !resetOwnedByCurrentCommandBuffer){
            priorSubmission = query->m_resetAuthorizationSubmission;
        }
        else{
            priorSubmission = query->m_completedCycleSubmission.valid()
                ? query->m_completedCycleSubmission
                : query->m_resetAuthorizationSubmission
            ;
        }
    }
    const bool priorSubmissionObservedComplete =
        __hidden_vulkan_queries::IsSubmissionComplete(m_device, priorSubmission)
    ;

    bool beginRecorded = false;
    {
        ScopedLock queryLock(query->m_mutex);
        const bool resetOwnedByCurrentCommandBuffer =
            query->m_resetRecordingOwner.commandBuffer == m_currentCmdBuf.get()
            && query->m_resetRecordingOwner.recordingID == m_currentCmdBuf->m_recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
        ;
        const QueueSubmissionToken currentPriorSubmission = m_renderPassActive && !resetOwnedByCurrentCommandBuffer
            ? query->m_resetAuthorizationSubmission
            : (
                query->m_completedCycleSubmission.valid()
                ? query->m_completedCycleSubmission
                : query->m_resetAuthorizationSubmission
            )
        ;
        if(
            !query->m_recordingActive
            && query->m_cycleGeneration == 0u
            && (!query->m_resetRecordingOwner.commandBuffer || resetOwnedByCurrentCommandBuffer)
            && (!m_renderPassActive || resetOwnedByCurrentCommandBuffer || query->m_resetAuthorizationAvailable)
            && __hidden_vulkan_queries::MatchesSubmissionToken(currentPriorSubmission, priorSubmission)
            && query->m_nextRecordingGeneration != Limit<u64>::s_Max
        ){
            TrackedCommandBuffer::TimerQueryRecordingClaim& claim =
                m_currentCmdBuf->findOrAppendTimerQueryRecordingClaim(*query)
            ;
            if(!claim.recordsBegin && !claim.recordsEnd){
                ++query->m_nextRecordingGeneration;
                query->m_cycleBaselineQueue = query->m_timestampQueue;
                query->m_cycleBaselineValidBits = query->m_timestampValidBits;
                query->m_cycleBaselineCompletion = query->m_completedCycleSubmission;
                query->m_cycleBaselineCompletionGeneration = query->m_completedCycleGeneration;
                query->m_cycleBaselineActive = query->m_recordingActive;
                query->m_cycleQueue = queueInfo->id;
                query->m_cycleValidBits = queueInfo->timestampValidBits;
                query->m_cycleGeneration = query->m_nextRecordingGeneration;
                query->m_beginRecordingOwner = TimerQuery::RecordingOwner{
                    .commandBuffer = m_currentCmdBuf.get(),
                    .recordingID = m_currentCmdBuf->m_recordingID,
                };
                query->m_endRecordingOwner = {};
                query->m_beginAccepted = false;
                query->m_cycleInvalidated = false;
                if(resetOwnedByCurrentCommandBuffer){
                    query->m_resetRecordingOwner = {};
                    query->m_resetRecordingAuthorizationGeneration = 0u;
                }

                claim.queue = queueInfo->id;
                if(!claim.recordsReset){
                    claim.prerequisiteSubmission = priorSubmission;
                    claim.prerequisiteObservedComplete = priorSubmissionObservedComplete;
                }
                claim.generation = query->m_cycleGeneration;
                claim.recordingID = m_currentCmdBuf->m_recordingID;
                claim.recordsBegin = true;
                claim.recordsReset = claim.recordsReset || !m_renderPassActive;
                claim.consumesResetAuthorization = m_renderPassActive && !resetOwnedByCurrentCommandBuffer;
                claim.resetAuthorizationSubmission = claim.consumesResetAuthorization
                    ? query->m_resetAuthorizationSubmission
                    : QueueSubmissionToken{}
                ;
                claim.consumedResetAuthorizationGeneration = claim.consumesResetAuthorization
                    ? query->m_resetAuthorizationGeneration
                    : 0u
                ;

            // Outside rendering, reset and begin are ordered in this command buffer. Inside rendering, the
            // frame-open reset packet owns the reset because vkCmdResetQueryPool is not legal in a render pass.
                if(!m_renderPassActive){
                    m_context.deviceDispatch.vkCmdResetQueryPool(
                        m_currentCmdBuf->m_cmdBuf,
                        query->m_queryPool,
                        s_TimerQueryBeginIndex,
                        s_TimerQueryTimestampCount
                    );
                }
                m_context.deviceDispatch.vkCmdWriteTimestamp(
                    m_currentCmdBuf->m_cmdBuf,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    query->m_queryPool,
                    s_TimerQueryBeginIndex
                );
                outToken = TimerQueryRecordingToken{
                    .query = query,
                    .physicalQueue = queueInfo->id,
                    .queryIncarnation = query->m_incarnation,
                    .generation = query->m_cycleGeneration,
                    .resetAuthorizationGeneration = claim.consumedResetAuthorizationGeneration,
                };
                beginRecorded = true;
            }
        }
    }
    if(!beginRecorded){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Refusing a timer-query begin while another recording cycle is active"));
        invalidateCommandRecording();
        return false;
    }
    return true;
}

bool CommandList::endTimerQuery(TimerQuery* queryResource, const TimerQueryRecordingToken& token){
    auto* query = queryResource;
    if(!query || query->m_queryPool == VK_NULL_HANDLE || &query->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to end an invalid or foreign timer query"));
        invalidateCommandRecording();
        return false;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("end timer query")))
        return false;

    if(!token.valid() || token.query != query || token.queryIncarnation != query->m_incarnation){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to end a timer query with an invalid recording token"));
        invalidateCommandRecording();
        return false;
    }

    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    bool matchingActiveCycle = false;
    {
        ScopedLock queryLock(query->m_mutex);
        matchingActiveCycle =
            query->m_cycleGeneration == token.generation
            && query->m_cycleGeneration != 0u
            && !query->m_cycleInvalidated
            && queueInfo
            && token.physicalQueue == queueInfo->id
            && query->m_cycleQueue == queueInfo->id
            && query->m_cycleValidBits != 0u
            && query->m_cycleValidBits == queueInfo->timestampValidBits
            && (query->m_beginAccepted || query->m_beginRecordingOwner.commandBuffer)
        ;
        if(matchingActiveCycle){
            const bool endOwnedByCurrentCommandBuffer =
                query->m_endRecordingOwner.commandBuffer == m_currentCmdBuf.get()
                && query->m_endRecordingOwner.recordingID == m_currentCmdBuf->m_recordingID
            ;
            if(query->m_endRecordingOwner.commandBuffer && !endOwnedByCurrentCommandBuffer)
                matchingActiveCycle = false;
            if(matchingActiveCycle){
                TrackedCommandBuffer::TimerQueryRecordingClaim& claim =
                    m_currentCmdBuf->findOrAppendTimerQueryRecordingClaim(*query)
                ;
                if(claim.recordsEnd)
                    matchingActiveCycle = false;
                else{
                    query->m_endRecordingOwner = TimerQuery::RecordingOwner{
                        .commandBuffer = m_currentCmdBuf.get(),
                        .recordingID = m_currentCmdBuf->m_recordingID,
                    };
                    claim.queue = queueInfo->id;
                    claim.generation = query->m_cycleGeneration;
                    claim.recordingID = m_currentCmdBuf->m_recordingID;
                    claim.recordsEnd = true;
                    m_context.deviceDispatch.vkCmdWriteTimestamp(
                        m_currentCmdBuf->m_cmdBuf,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query->m_queryPool,
                        s_TimerQueryEndIndex
                    );
                }
            }
        }
    }
    if(!matchingActiveCycle){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to end a timer query without an active cycle on the same physical queue"));
        invalidateCommandRecording();
        return false;
    }
    return true;
}

void CommandList::beginMarker(const AStringView name){
    if(!validateCommandRecordingScope(NWB_TEXT("begin command-list marker")))
        return;

    ++m_markerDepth;

    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useNvCheckpoint = m_device.isGpuCrashDiagnosticsEnabled();
    const bool useAmdBreadcrumb = m_device.isAmdBreadcrumbEnabled();
    const bool useGpuMarkers = useNvCheckpoint || useAmdBreadcrumb;
    if(!useDebugUtils && !useGpuMarkers)
        return;

    const GraphicsString markerName(name, m_context.objectArena);

    if(useDebugUtils){
        auto label = VulkanDetail::MakeVkStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = markerName.c_str();
        m_context.instanceDispatch.vkCmdBeginDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf, &label);
    }

    // Both vendors share one nested-marker hash so resolveMarker works regardless of which (or both) is active.
    if(useGpuMarkers){
        const usize gpuCrashMarker = m_gpuCrashMarkerTracker.pushEvent(markerName.c_str());
        if(useNvCheckpoint)
            m_context.deviceDispatch.vkCmdSetCheckpointNV(m_currentCmdBuf->m_cmdBuf, reinterpret_cast<const void*>(gpuCrashMarker));
        if(useAmdBreadcrumb){
            const Device::AmdBreadcrumbWrite breadcrumb = m_device.reserveAmdBreadcrumb(
                m_creationDesc.physicalQueue,
                gpuCrashMarker
            );
            if(breadcrumb.valid){
                m_hostReadbackBarrierTracker.registerDeviceOwnedBuffer(breadcrumb.buffer);
                m_context.deviceDispatch.vkCmdWriteBufferMarkerAMD(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, breadcrumb.buffer, breadcrumb.offset, breadcrumb.marker);
            }
        }
    }
}

void CommandList::endMarker(){
    if(m_markerDepth == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring an unmatched command-list marker end"));
        return;
    }
    if(!validateCommandRecordingScope(NWB_TEXT("end command-list marker")))
        return;

    const bool useDebugUtils = m_context.extensions.EXT_debug_utils;
    const bool useGpuMarkers = m_device.isAnyGpuMarkerEnabled();

    if(useDebugUtils)
        m_context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);

    if(useGpuMarkers)
        m_gpuCrashMarkerTracker.popEvent();

    --m_markerDepth;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

