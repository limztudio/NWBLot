// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "timer_query_claim_journal.h"

#include <global/termination.h>


NWB_VULKAN_BEGIN


TimerQueryClaimJournal::Claim& TimerQueryClaimJournal::findOrAppendTimerQueryRecordingClaim(TimerQuery& query){
    for(Claim& claim : m_claims){
        if(claim.query.get() == &query)
            return claim;
    }

    m_owner.m_resourceReferences.retainResource(query);
    m_claims.push_back(Claim{ .query = MakeNotNull(&query), .queue = {}, .resetAuthorizationSubmission = {}, .prerequisiteSubmission = {}, .queryIncarnation = query.m_incarnation, });
    return m_claims.back();
}

TimerQueryClaimJournal::Claim* TimerQueryClaimJournal::findTimerQueryRecordingClaim(TimerQuery& query, const u64 generation)noexcept{
    for(Claim& claim : m_claims){
        if(claim.query.get() == &query && claim.queryIncarnation == query.m_incarnation && claim.generation == generation && claim.recordingID == m_owner.m_recordingID)
            return &claim;
    }
    return nullptr;
}

b8 TimerQueryClaimJournal::recordsTimerQueryBegin(const TimerQuery& query, const u64 generation)const noexcept{
    for(const Claim& claim : m_claims){
        if(claim.query.get() == &query && claim.generation == generation && claim.recordingID == m_owner.m_recordingID && claim.recordsBegin)
            return true;
    }
    return false;
}

TimerQueryClaimJournal::OwnerMatches TimerQueryClaimJournal::resolveTimerQueryClaimOwnerMatches(const TimerQuery& query, const Claim& claim)const noexcept{
    return {
        .resetOwnerMatches =
            query.m_resetRecordingOwner.commandBuffer == &m_owner
            && query.m_resetRecordingOwner.recordingID == claim.recordingID
            && query.m_resetRecordingAuthorizationGeneration != 0u
            && query.m_resetRecordingAuthorizationGeneration == claim.resetRecordingAuthorizationGeneration,
        .beginOwnerMatches = query.m_beginRecordingOwner.commandBuffer == &m_owner && query.m_beginRecordingOwner.recordingID == claim.recordingID && query.m_cycleGeneration == claim.generation,
        .endOwnerMatches = query.m_endRecordingOwner.commandBuffer == &m_owner && query.m_endRecordingOwner.recordingID == claim.recordingID && query.m_cycleGeneration == claim.generation,
    };
}

b8 TimerQueryClaimJournal::validateTimerQueryRecordingClaims(
    const Queue& submissionQueue,
    const Span<const QueueSubmissionWait> localWaits,
    const Span<const NotNull<TrackedCommandBuffer*>> precedingCommandBuffers
)const noexcept{
    for(const Claim& claim : m_claims){
        TimerQuery& query = *claim.query;
        if(claim.queryIncarnation == 0u || query.m_incarnation != claim.queryIncarnation || claim.recordingID == 0u || claim.recordingID != m_owner.m_recordingID || claim.queue != submissionQueue.m_physicalQueue)
            return false;

        NothrowScopedLock queryLock(query.m_mutex);
        const QueueSubmissionToken currentPrerequisite = query.prerequisiteSubmissionWithinLock();
        const b8 prerequisiteMatches = currentPrerequisite == claim.prerequisiteSubmission;
        if((claim.recordsReset || claim.recordsBegin) && (!prerequisiteMatches || !submissionQueue.coversTimerQueryPrerequisite(claim.prerequisiteSubmission, claim.prerequisiteObservedComplete, localWaits)))
            return false;
        const OwnerMatches ownerMatches = resolveTimerQueryClaimOwnerMatches(query, claim);
        if(claim.recordsReset && !claim.recordsBegin && !ownerMatches.resetOwnerMatches)
            return false;
        if(
            claim.recordsBegin
            && (
                !ownerMatches.beginOwnerMatches
                || query.m_cycleInvalidated
                || query.m_cycleQueue != claim.queue
                || (
                    claim.consumesResetAuthorization
                    && (
                        !query.m_resetAuthorizationAvailable
                        || query.m_resetAuthorizationGeneration != claim.consumedResetAuthorizationGeneration
                        || query.m_resetAuthorizationSubmission != claim.resetAuthorizationSubmission
                    )
                )
            )
        )
            return false;
        if(claim.recordsEnd){
            if(!ownerMatches.endOwnerMatches || query.m_cycleInvalidated || query.m_cycleQueue != claim.queue)
                return false;
            if(query.m_beginAccepted || claim.recordsBegin)
                continue;

            b8 orderedAfterBatchBegin = false;
            for(usize precedingIndex = 0u; precedingIndex < precedingCommandBuffers.size(); ++precedingIndex){
                TrackedCommandBuffer& preceding = *precedingCommandBuffers[precedingIndex];
                if(&preceding == query.m_beginRecordingOwner.commandBuffer && preceding.m_recordingID == query.m_beginRecordingOwner.recordingID && preceding.m_timerQueryClaims.recordsTimerQueryBegin(query, claim.generation)){
                    orderedAfterBatchBegin = true;
                    break;
                }
            }
            if(!orderedAfterBatchBegin)
                return false;
        }
    }
    return true;
}

void TimerQueryClaimJournal::commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept{
    static_assert(noexcept(m_claims.clear()), "accepted timer-query claim release must be non-throwing");
    for(const Claim& claim : m_claims){
        TimerQuery& query = *claim.query;
        if(claim.queryIncarnation == 0u || query.m_incarnation != claim.queryIncarnation || claim.recordingID == 0u || claim.recordingID != m_owner.m_recordingID || (!claim.recordsReset && !claim.recordsBegin && !claim.recordsEnd))
            TerminateInvariant();

        NothrowScopedLock queryLock(query.m_mutex);
        const OwnerMatches ownerMatches = resolveTimerQueryClaimOwnerMatches(query, claim);

        if(claim.recordsReset && !claim.recordsBegin){
            if(!ownerMatches.resetOwnerMatches)
                TerminateInvariant();
            query.m_resetRecordingOwner = {};
            query.m_resetAuthorizationGeneration = query.m_resetRecordingAuthorizationGeneration;
            query.m_resetRecordingAuthorizationGeneration = 0u;
            query.m_timestampQueue = {};
            query.m_timestampValidBits = 0u;
            query.m_completedCycleSubmission = {};
            query.m_completedCycleGeneration = 0u;
            query.m_resetAuthorizationSubmission = submissionToken;
            query.m_resetAuthorizationAvailable = true;
            query.m_recordingActive = false;
        }
        if(claim.recordsBegin){
            if(!ownerMatches.beginOwnerMatches)
                TerminateInvariant();
            query.m_beginRecordingOwner = {};
            query.m_beginAccepted = true;
            query.m_lastAcceptedRecordingGeneration = claim.generation;
            query.m_timestampQueue = query.m_cycleQueue;
            query.m_timestampValidBits = query.m_cycleValidBits;
            query.m_completedCycleSubmission = {};
            query.m_completedCycleGeneration = 0u;
            query.m_resetAuthorizationSubmission = {};
            query.m_resetAuthorizationGeneration = 0u;
            query.m_resetAuthorizationAvailable = false;
            query.m_recordingActive = true;
        }
        if(claim.recordsEnd){
            if(!ownerMatches.endOwnerMatches || (!query.m_beginAccepted && !claim.recordsBegin))
                TerminateInvariant();
            query.m_endRecordingOwner = {};
            query.m_recordingActive = false;
            query.m_completedCycleSubmission = submissionToken;
            query.m_completedCycleGeneration = claim.generation;
            query.m_cycleBaselineQueue = {};
            query.m_cycleQueue = {};
            query.m_cycleBaselineValidBits = 0u;
            query.m_cycleValidBits = 0u;
            query.m_cycleBaselineCompletion = {};
            query.m_cycleBaselineCompletionGeneration = 0u;
            query.m_cycleBaselineActive = false;
            query.m_beginRecordingOwner = {};
            query.m_cycleGeneration = 0u;
            query.m_beginAccepted = false;
            query.m_cycleInvalidated = false;
        }
    }
    m_claims.clear();
}

void TimerQueryClaimJournal::discardTimerQueryRecordingClaims()noexcept{
    static_assert(noexcept(m_claims.clear()), "timer-query claim discard must be non-throwing");
    for(const Claim& claim : m_claims){
        TimerQuery& query = *claim.query;
        if(claim.queryIncarnation == 0u || query.m_incarnation != claim.queryIncarnation)
            continue;
        NothrowScopedLock queryLock(query.m_mutex);
        const OwnerMatches ownerMatches = resolveTimerQueryClaimOwnerMatches(query, claim);
        if(claim.recordsReset && !claim.recordsBegin && ownerMatches.resetOwnerMatches){
            query.m_resetRecordingOwner = {};
            query.m_resetRecordingAuthorizationGeneration = 0u;
        }
        if(claim.recordsEnd && ownerMatches.endOwnerMatches){
            query.m_endRecordingOwner = {};
        }
        if(claim.recordsBegin && ownerMatches.beginOwnerMatches){
            query.m_timestampQueue = query.m_cycleBaselineQueue;
            query.m_timestampValidBits = query.m_cycleBaselineValidBits;
            query.m_completedCycleSubmission = query.m_cycleBaselineCompletion;
            query.m_completedCycleGeneration = query.m_cycleBaselineCompletionGeneration;
            query.m_recordingActive = query.m_cycleBaselineActive;
            query.m_beginRecordingOwner = {};
            query.m_beginAccepted = false;
            query.m_cycleInvalidated = query.m_endRecordingOwner.commandBuffer != nullptr;
        }
        if(query.m_cycleGeneration == claim.generation && !query.m_beginRecordingOwner.commandBuffer && !query.m_endRecordingOwner.commandBuffer && !query.m_beginAccepted){
            query.m_cycleBaselineQueue = {};
            query.m_cycleQueue = {};
            query.m_cycleBaselineValidBits = 0u;
            query.m_cycleValidBits = 0u;
            query.m_cycleBaselineCompletion = {};
            query.m_cycleBaselineCompletionGeneration = 0u;
            query.m_cycleBaselineActive = false;
            query.m_cycleGeneration = 0u;
            query.m_cycleInvalidated = false;
        }
    }
    m_claims.clear();
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

