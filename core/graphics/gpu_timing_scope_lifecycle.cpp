// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"
#include "gpu_timing_submission_ticket.h"
#include "backend_selection.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuTimingSubmissionTicket* GpuTimingRecorder::s_ActiveSubmissionTicket = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuTimingScope> GpuTimingRecorder::beginScope(const Name& scopeName, const GpuTimingSampleAttribution attribution, const b8 requiresComparableTimestamps, Device& device, CommandList& commandList){
    GpuTimingScope scope;
    GpuTimingSubmissionTicket* ticket = nullptr;
    QueueSubmissionToken resetSubmission;
    TimerQuery* claimedQuery = nullptr;
    b8 previousDeviceReady = false;
    usize publicationIndex = Limit<usize>::s_Max;
    {
        UniqueLock lock(m_mutex);
        syncActiveState();
        if(!scopeName)
            return scope;
        ++m_statistics.scopeAttemptCount;
        if(!m_performanceCollectionActive && (!attribution.valid() || !feedbackScopeDemandedLocked(scopeName))){
            noteSkippedScope(GpuTimingScopeSkipReason::CollectionInactive);
            return scope;
        }
        if(&commandList.getDevice() != &device){
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }

        const CommandListParameters commandListDescription = commandList.getResolvedDescription();
        const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(commandListDescription.physicalQueue);
        if(!queueInfo){
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }
        if(queueInfo->timestampValidBits == 0u){
            noteSkippedScope(GpuTimingScopeSkipReason::QueueTimestampsUnsupported);
            return scope;
        }
        if(requiresComparableTimestamps && !device.supportsComparableGpuTimestamps(queueInfo->id)){
            noteSkippedScope(GpuTimingScopeSkipReason::ComparableTimestampsUnsupported);
            return scope;
        }

        ticket = activeSubmissionTicket();
        FOUNDATION_ASSERT_MSG(ticket, FOUNDATION_TEXT("GPU timing scopes must be recorded inside a submission ticket"));
        if(!ticket){
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }

        const auto found = m_accumulators.find(scopeName);
        if(found == m_accumulators.end()){
            noteSkippedScope(GpuTimingScopeSkipReason::ScopeNotPrepared);
            return scope;
        }

        const auto publicationIndexResult = ticket->reserveScopePublication();
        if(!publicationIndexResult){
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }

        publicationIndex = *publicationIndexResult;
        GpuTimingAccumulator& accumulator = *found.value();
        const auto claimed = accumulator.claimQueryForBegin(
            commandList,
            commandList.canRecordTimerQueryHere(),
            commandList.canResetTimerQueryHere(),
            commandListDescription,
            m_currentFrameIndex,
            m_epoch,
            m_performanceCaptureEpoch,
            attribution,
            claimedQuery,
            previousDeviceReady
        );
        if(!claimed){
            ticket->cancelScopePublication(publicationIndex);
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }
        scope = claimed->scope;
        resetSubmission = claimed->resetSubmission;
        if(!scope.valid()){
            ticket->cancelScopePublication(publicationIndex);
            return scope;
        }
    }

    const auto timerQueryRecording = commandList.beginTimerQuery(*claimedQuery);
    if(!timerQueryRecording){
        ticket->cancelScopePublication(publicationIndex);
        {
            NothrowScopedLock lock(m_mutex);
            if(GpuTimingAccumulator* const currentAccumulator = findAccumulator(scope))
                currentAccumulator->releaseFailedBeginClaim(scope, previousDeviceReady);
            ++m_statistics.beginFailureCount;
        }
        return MakeUnexpected(Failure{});
    }

    scope.timerQueryRecording = *timerQueryRecording;
    scope.submissionTicket = ticket;
    scope.submissionPublicationIndex = publicationIndex;
    if(!ticket->bindScopePublication(publicationIndex, scope, commandList)){
        abandonScopeWithoutCallbacks(scope);
        NothrowScopedLock lock(m_mutex);
        ++m_statistics.beginFailureCount;
        return MakeUnexpected(Failure{});
    }

    if(scope.valid() && resetSubmission.valid()){
        ScopeExit prerequisiteUnwind([this, &scope]()noexcept{
            abandonScopeWithoutCallbacks(scope);
            NothrowScopedLock lock(m_mutex);
            ++m_statistics.beginFailureCount;
        });
        const b8 prerequisiteTracked = ticket->trackSubmissionPrerequisite(resetSubmission);
        prerequisiteUnwind.release();
        if(!prerequisiteTracked){
            abandonScopeWithoutCallbacks(scope);
            ScopedLock lock(m_mutex);
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }
    }
    return scope;
}

Expected<GpuTimingScope> GpuTimingRecorder::beginDeferredScope(const Name& scopeName, const GpuTimingSampleAttribution attribution, Device& device, CommandList& commandList){
    auto scope = beginScope(scopeName, attribution, false, device, commandList);
    if(!scope)
        return MakeUnexpected(Failure{});
    if(scope->submissionTicket)
        scope->submissionTicket->cancelScopePublication(scope->submissionPublicationIndex);
    scope->submissionTicket = nullptr;
    scope->submissionPublicationIndex = Limit<usize>::s_Max;
    return *scope;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

