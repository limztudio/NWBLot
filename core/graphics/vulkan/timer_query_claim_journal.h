// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <core/graphics/vulkan/timer_query_types.adhd.h>

#include <global/span.h>


NWB_VULKAN_BEGIN


struct QueueSubmissionWait;

class TimerQueryClaimJournal final : NoCopy{
    friend class TrackedCommandBuffer;


public:
    struct Claim{
        NotNull<TimerQuery*> query;
        GpuPhysicalQueueId queue;
        QueueSubmissionToken resetAuthorizationSubmission;
        QueueSubmissionToken prerequisiteSubmission;
        u64 queryIncarnation = 0u;
        u64 generation = 0u;
        u64 resetRecordingAuthorizationGeneration = 0u;
        u64 consumedResetAuthorizationGeneration = 0u;
        u64 recordingID = 0u;
        b8 prerequisiteObservedComplete = false;
        b8 recordsReset = false;
        b8 recordsBegin = false;
        b8 recordsEnd = false;
        b8 consumesResetAuthorization = false;
    };


private:
    using OwnerMatches = VulkanDetail::TimerQueryClaimOwnerMatches;


public:
    TimerQueryClaimJournal(TrackedCommandBuffer& owner, Alloc::GlobalArena& arena)noexcept : m_owner(owner), m_claims(arena){}
    [[nodiscard]] const Vector<Claim, Alloc::GlobalArena>& claims()const noexcept{ return m_claims; }


private:
    [[nodiscard]] Claim& findOrAppendTimerQueryRecordingClaim(TimerQuery& query);
    [[nodiscard]] Claim* findTimerQueryRecordingClaim(TimerQuery& query, u64 generation)noexcept;
    [[nodiscard]] b8 recordsTimerQueryBegin(const TimerQuery& query, u64 generation)const noexcept;
    [[nodiscard]] OwnerMatches resolveTimerQueryClaimOwnerMatches(const TimerQuery& query, const Claim& claim)const noexcept;
    [[nodiscard]] b8 validateTimerQueryRecordingClaims(const Queue& submissionQueue, Span<const QueueSubmissionWait> localWaits, Span<const NotNull<TrackedCommandBuffer*>> precedingCommandBuffers)const noexcept;
    void commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept;
    void discardTimerQueryRecordingClaims()noexcept;


private:
    TrackedCommandBuffer& m_owner;
    Vector<Claim, Alloc::GlobalArena> m_claims;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

