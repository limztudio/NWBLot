// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"
#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuTimingSubmissionTicket* GpuTimingRecorder::s_ActiveSubmissionTicket = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder::BeginQueryPublicationUnwindScope final : NoCopy{
public:
    BeginQueryPublicationUnwindScope(
        GpuTimingRecorder& recorder,
        GpuTimingSubmissionTicket& ticket,
        GpuTimingAccumulator& accumulator,
        GpuTimingScope& scope,
        const usize publicationIndex
    )noexcept
        : m_recorder(recorder)
        , m_ticket(ticket)
        , m_accumulator(accumulator)
        , m_scope(scope)
        , m_publicationIndex(publicationIndex)
    {}
    ~BeginQueryPublicationUnwindScope()noexcept{
        if(!m_active)
            return;

        m_ticket.cancelScopePublication(m_publicationIndex);
        if(m_scope.valid()){
            if(m_accumulator.abandonQuery(
                m_scope,
                m_recorder.m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
            ))
                m_recorder.m_pendingAttributionRetirements = true;
            m_scope = {};
        }
        ++m_recorder.m_statistics.beginFailureCount;
    }


public:
    void release()noexcept{ m_active = false; }


private:
    GpuTimingRecorder& m_recorder;
    GpuTimingSubmissionTicket& m_ticket;
    GpuTimingAccumulator& m_accumulator;
    GpuTimingScope& m_scope;
    usize m_publicationIndex = Limit<usize>::s_Max;
    bool m_active = true;
};


class GpuTimingRecorder::PrerequisiteTrackingUnwindScope final : NoCopy{
public:
    PrerequisiteTrackingUnwindScope(GpuTimingRecorder& recorder, GpuTimingScope& scope)noexcept
        : m_recorder(recorder)
        , m_scope(scope)
    {}
    ~PrerequisiteTrackingUnwindScope()noexcept{
        if(!m_active)
            return;

        m_recorder.abandonScopeWithoutCallbacks(m_scope);
        NothrowScopedLock lock(m_recorder.m_mutex);
        ++m_recorder.m_statistics.beginFailureCount;
    }


public:
    void release()noexcept{ m_active = false; }


private:
    GpuTimingRecorder& m_recorder;
    GpuTimingScope& m_scope;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuTimingScope> GpuTimingRecorder::beginScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    const bool requiresComparableTimestamps
){
    GpuTimingScope scope;
    GpuTimingSubmissionTicket* ticket = nullptr;
    QueueSubmissionToken resetSubmission;
    {
        ScopedLock lock(m_mutex);
        syncActiveState();
        if(!scopeName)
            return scope;
        ++m_statistics.scopeAttemptCount;
        if(
            !m_performanceCollectionActive
            && (!attribution.valid() || !feedbackScopeDemandedLocked(scopeName))
        ){
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

        const usize publicationIndex = ticket->reserveScopePublication();
        if(publicationIndex == Limit<usize>::s_Max){
            ++m_statistics.beginFailureCount;
            return MakeUnexpected(Failure{});
        }

        GpuTimingAccumulator& accumulator = *found.value();
        BeginQueryPublicationUnwindScope publicationUnwind(
            *this,
            *ticket,
            accumulator,
            scope,
            publicationIndex
        );
        const auto began = accumulator.beginQuery(
            commandList,
            m_currentFrameIndex,
            m_epoch,
            m_performanceCaptureEpoch,
            attribution
        );
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
        PrerequisiteTrackingUnwindScope prerequisiteUnwind(*this, scope);
        const bool prerequisiteTracked = ticket->trackSubmissionPrerequisite(resetSubmission);
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

Expected<GpuTimingScope> GpuTimingRecorder::beginDeferredScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution
){
    auto scope = beginScope(scopeName, device, commandList, attribution, false);
    if(!scope)
        return MakeUnexpected(Failure{});
    // The frame transaction owns this reservation until the accepted end packet. The begin ticket has no rollback
    // handle: a recovery endpoint may be needed after that begin already executed on the device timeline.
    if(scope->submissionTicket)
        scope->submissionTicket->cancelScopePublication(scope->submissionPublicationIndex);
    scope->submissionTicket = nullptr;
    scope->submissionPublicationIndex = Limit<usize>::s_Max;
    return *scope;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

