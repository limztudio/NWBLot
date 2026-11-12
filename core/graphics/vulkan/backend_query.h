// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EventQuery final : public RefCounter<GraphicsResource>{
    friend class Device;


public:
    EventQuery(const VulkanContext& context);
    ~EventQuery()noexcept;


private:
    VkFence m_fence = VK_NULL_HANDLE;
    const VulkanContext& m_context;
    Futex m_mutex;

    b8 m_started = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimerQuery final : public RefCounter<GraphicsResource>{
    friend class Device;
    friend class CommandList;
    friend class TrackedCommandBuffer;
    friend class TimerQueryClaimJournal;


private:
    struct RecordingOwner{
        TrackedCommandBuffer* commandBuffer = nullptr;
        u64 recordingID = 0u;
    };


public:
    TimerQuery(const VulkanContext& context, u64 incarnation);
    ~TimerQuery()noexcept;


public:
    // The caller owns this unaccepted recording transaction; its command buffers cannot submit concurrently.
    [[nodiscard]] b8 discardUnacceptedRecording(const TimerQueryRecordingToken& token)noexcept;
    // The caller has made its prior endpoint packet permanently non-submittable.
    // Preserve the accepted begin cycle while relinquishing only the stale end owner so a recovery command buffer can record its replacement endpoint.
    [[nodiscard]] b8 releaseUnacceptedEndForRecovery(const TimerQueryRecordingToken& token)noexcept;


private:
    // Caller holds m_mutex while consulting prerequisite and completed-cycle predicates.
    [[nodiscard]] QueueSubmissionToken prerequisiteSubmissionWithinLock()const noexcept{
        return m_completedCycleSubmission.valid() ? m_completedCycleSubmission : m_resetAuthorizationSubmission;
    }
    [[nodiscard]] b8 hasCompletedCycleWithinLock()const noexcept;
    [[nodiscard]] b8 completedCycleMatchesWithinLock(u64 generation, const QueueSubmissionToken& submission, const GpuPhysicalQueueInfo* queueInfo)const noexcept;


private:
    Futex m_mutex;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    RecordingOwner m_resetRecordingOwner;
    RecordingOwner m_beginRecordingOwner;
    RecordingOwner m_endRecordingOwner;
    u64 m_incarnation = 0u;
    u64 m_nextRecordingGeneration = 0u;
    u64 m_nextResetAuthorizationGeneration = 0u;
    u64 m_lastAcceptedRecordingGeneration = 0u;
    u64 m_resetRecordingAuthorizationGeneration = 0u;
    u64 m_cycleGeneration = 0u;
    u64 m_cycleBaselineCompletionGeneration = 0u;
    u64 m_completedCycleGeneration = 0u;
    u64 m_resetAuthorizationGeneration = 0u;
    QueueSubmissionToken m_cycleBaselineCompletion;
    QueueSubmissionToken m_completedCycleSubmission;
    QueueSubmissionToken m_resetAuthorizationSubmission;
    u32 m_timestampValidBits = 0u;
    u32 m_cycleBaselineValidBits = 0u;
    u32 m_cycleValidBits = 0u;
    b8 m_cycleBaselineActive = false;
    b8 m_beginAccepted = false;
    b8 m_cycleInvalidated = false;
    b8 m_resetAuthorizationAvailable = false;
    b8 m_recordingActive = false;
    GpuPhysicalQueueId m_timestampQueue;
    GpuPhysicalQueueId m_cycleBaselineQueue;
    GpuPhysicalQueueId m_cycleQueue;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

