// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext;
class SwapChainLifecycle;

struct FramePresentationSignalState{
    enum Enum : u8{
        Idle,
        Claimed,
        Queued,
        Rejected,
        Accepted,
        Failed,
        Retiring,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FramePresentationCoordinator final : NoCopy{
    friend class BackendContext;
    friend class SwapChainLifecycle;


public:
    explicit FramePresentationCoordinator(BackendContext& context)noexcept
        : m_context(context)
    {}


public:
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    [[nodiscard]] b8 confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim);
    [[nodiscard]] Expected<QueueSubmissionNativeSignal> prepareFramePresentationSignal(u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept;
    [[nodiscard]] b8 resolveFramePresentationSignal(u64 identity, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 replaceFramePresentationSemaphoreAfterIdle();
    void resetFramePresentationSignal()noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignalDeferred(const QueueSubmissionPreSubmitHook* claim, UniqueLock<Futex>& lifecycleLock);


private:
    BackendContext& m_context;
    GpuPhysicalQueueId m_framePresentationQueue;
    VkSemaphore m_framePresentationSemaphore = VK_NULL_HANDLE;
    QueueSubmissionToken m_framePresentationSubmission;
    u64 m_nextFramePresentationClaimIdentity = 0u;
    u64 m_framePresentationClaimIdentity = 0u;
    u32 m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    // Hooks run inside queue submission, so they use this lock instead of the lifecycle lock.
    Futex m_framePresentationMutex;
    ConditionVariableAny m_framePresentationCondition;
    b8 m_framePresentationClaimIdentityExhausted = false;
    FramePresentationSignalState::Enum m_framePresentationSignalState = FramePresentationSignalState::Idle;
    b8 m_framePresentationClaimsEnabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

