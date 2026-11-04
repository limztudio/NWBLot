// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"
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
    {
        ScopedLock lock(m_mutex);
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
        NWB_ASSERT_MSG(ticket, NWB_TEXT("GPU timing scopes must be recorded inside a submission ticket"));
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

        const usize publicationIndex = *publicationIndexResult;
        GpuTimingAccumulator& accumulator = *found.value();
        ScopeExit publicationUnwind([this, ticket, &accumulator, &scope, publicationIndex]()noexcept{
            ticket->cancelScopePublication(publicationIndex);
            if(scope.valid()){
                if(accumulator.abandonQuery(scope, m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)))
                    m_pendingAttributionRetirements = true;
                scope = {};
            }
            ++m_statistics.beginFailureCount;
        });
        const auto began = accumulator.beginQuery(commandList, m_currentFrameIndex, m_epoch, m_performanceCaptureEpoch, attribution);
        if(!began)
            return MakeUnexpected(Failure{});
        scope = began->scope;
        resetSubmission = began->resetSubmission;
        if(!scope.valid()){
            ticket->cancelScopePublication(publicationIndex);
            publicationUnwind.release();
            return scope;
        }

        scope.submissionTicket = ticket;
        scope.submissionPublicationIndex = publicationIndex;
        if(!ticket->bindScopePublication(publicationIndex, scope, commandList))
            return MakeUnexpected(Failure{});
        publicationUnwind.release();
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
    // The frame transaction owns this reservation until the accepted end packet.
    // The begin ticket has no rollback handle: a recovery endpoint may be needed after that begin already executed on the device timeline.
    if(scope->submissionTicket)
        scope->submissionTicket->cancelScopePublication(scope->submissionPublicationIndex);
    scope->submissionTicket = nullptr;
    scope->submissionPublicationIndex = Limit<usize>::s_Max;
    return *scope;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

