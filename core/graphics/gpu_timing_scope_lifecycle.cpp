// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuTimingSubmissionTicket* GpuTimingRecorder::s_activeSubmissionTicket = nullptr;


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTimingRecorder::beginScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    const bool requiresComparableTimestamps,
    GpuTimingScope& outScope
){
    outScope = {};
    GpuTimingSubmissionTicket* ticket = nullptr;
    QueueSubmissionToken resetSubmission;
    {
        ScopedLock lock(m_mutex);
        syncActiveState();
        if(!scopeName)
            return true;
        ++m_statistics.scopeAttemptCount;
        if(
            !m_performanceCollectionActive
            && (!attribution.valid() || !feedbackScopeDemandedLocked(scopeName))
        ){
            noteSkippedScope(GpuTimingScopeSkipReason::CollectionInactive);
            return true;
        }
        if(&commandList.getDevice() != &device){
            ++m_statistics.beginFailureCount;
            return false;
        }

        const CommandListParameters commandListDescription = commandList.getResolvedDescription();
        const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(commandListDescription.physicalQueue);
        if(!queueInfo){
            ++m_statistics.beginFailureCount;
            return false;
        }
        if(queueInfo->timestampValidBits == 0u){
            noteSkippedScope(GpuTimingScopeSkipReason::QueueTimestampsUnsupported);
            return true;
        }
        if(requiresComparableTimestamps && !device.supportsComparableGpuTimestamps(queueInfo->id)){
            noteSkippedScope(GpuTimingScopeSkipReason::ComparableTimestampsUnsupported);
            return true;
        }

        ticket = activeSubmissionTicket();
        NWB_ASSERT_MSG(ticket, NWB_TEXT("GPU timing scopes must be recorded inside a submission ticket"));
        if(!ticket){
            ++m_statistics.beginFailureCount;
            return false;
        }

        const auto found = m_accumulators.find(scopeName);
        if(found == m_accumulators.end()){
            noteSkippedScope(GpuTimingScopeSkipReason::ScopeNotPrepared);
            return true;
        }

        const usize publicationIndex = ticket->reserveScopePublication();
        if(publicationIndex == Limit<usize>::s_Max){
            ++m_statistics.beginFailureCount;
            return false;
        }

        GpuTimingAccumulator& accumulator = *found.value();
        BeginQueryPublicationUnwindScope publicationUnwind(
            *this,
            *ticket,
            accumulator,
            outScope,
            publicationIndex
        );
        const bool began = accumulator.beginQuery(
            commandList,
            m_currentFrameIndex,
            m_epoch,
            m_performanceCaptureEpoch,
            attribution,
            outScope,
            resetSubmission
        );
        if(!began)
            return false;
        if(!outScope.valid()){
            ticket->cancelScopePublication(publicationIndex);
            publicationUnwind.release();
            return true;
        }

        outScope.submissionTicket = ticket;
        outScope.submissionPublicationIndex = publicationIndex;
        if(!ticket->bindScopePublication(publicationIndex, outScope, commandList))
            return false;
        publicationUnwind.release();
    }

    if(outScope.valid() && resetSubmission.valid()){
        PrerequisiteTrackingUnwindScope prerequisiteUnwind(*this, outScope);
        const bool prerequisiteTracked = ticket->trackSubmissionPrerequisite(resetSubmission);
        prerequisiteUnwind.release();
        if(!prerequisiteTracked){
            abandonScopeWithoutCallbacks(outScope);
            ScopedLock lock(m_mutex);
            ++m_statistics.beginFailureCount;
            return false;
        }
    }
    return true;
}

bool GpuTimingRecorder::beginDeferredScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    GpuTimingScope& outScope
){
    if(!beginScope(scopeName, device, commandList, attribution, false, outScope))
        return false;
    // The frame transaction owns this reservation until the accepted end packet. The begin ticket has no rollback
    // handle: a recovery endpoint may be needed after that begin already executed on the device timeline.
    if(outScope.submissionTicket)
        outScope.submissionTicket->cancelScopePublication(outScope.submissionPublicationIndex);
    outScope.submissionTicket = nullptr;
    outScope.submissionPublicationIndex = Limit<usize>::s_Max;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

