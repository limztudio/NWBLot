// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextSampleAttributionIdentity{ 1u };
inline constexpr Name s_GpuTimingScratchArena("graphics.gpu_timing.scratch");


[[nodiscard]] static u64 AllocateMonotonicIdentity(Atomic<u64>& nextIdentity)noexcept{
    u64 identity = nextIdentity.load(MemoryOrder::relaxed);
    while(identity != Limit<u64>::s_Max){
        if(nextIdentity.compare_exchange_weak(identity, identity + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
            return identity;
    }
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


void GpuTimingRecorder::endScope(CommandList& commandList, GpuTimingScope& scope){
    if(!scope.valid())
        return;

    GpuTimingAccumulator::QueryEndResult endResult = GpuTimingAccumulator::QueryEndResult::Invalid;
    {
        // Do not retain this lock while publishing into the ticket: terminal ticket paths take the ticket first and
        // then release or quarantine query ownership through the recorder.
        ScopedLock lock(m_mutex);
        GpuTimingAccumulator* accumulator = findAccumulator(scope);
        if(accumulator)
            endResult = accumulator->endQuery(commandList, scope);
    }
    if(endResult == GpuTimingAccumulator::QueryEndResult::Invalid){
        abandonScopeWithoutCallbacks(scope);
        return;
    }

    if(scope.submissionTicket && !scope.submissionTicket->publishScope(scope, commandList)){
        abandonScopeWithoutCallbacks(scope);
        return;
    }
    scope = {};
}

void GpuTimingRecorder::endScopeFromOpeningCommandList(CommandList& commandList, GpuTimingScope& scope)noexcept{
    if(!scope.valid())
        return;

    GpuTimingAccumulator::QueryEndResult endResult = GpuTimingAccumulator::QueryEndResult::Invalid;
    {
        // Preserve recorder -> accumulator lock ordering while the already-reserved scope is closed.
        NothrowScopedLock lock(m_mutex);
        GpuTimingAccumulator* const accumulator = findAccumulator(scope);
        if(accumulator)
            endResult = accumulator->endQueryFromExistingClaim(commandList, scope);
    }
    if(endResult == GpuTimingAccumulator::QueryEndResult::Invalid){
        abandonScopeWithoutCallbacks(scope);
        return;
    }
    if(scope.submissionTicket && !scope.submissionTicket->publishScope(scope, commandList)){
        abandonScopeWithoutCallbacks(scope);
        return;
    }
    scope = {};
}

bool GpuTimingRecorder::recordDeferredScopeEnd(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    return accumulator && accumulator->recordQueryEnd(commandList, scope);
}

bool GpuTimingRecorder::validateScopeSubmission(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token
){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    return accumulator && accumulator->validateQuerySubmission(scope, token);
}

bool GpuTimingRecorder::confirmScope(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token,
    const bool publishSample
)noexcept{
    if(!scope.valid())
        return true;

    NothrowScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(!accumulator)
        return false;

    const bool confirmed = accumulator->confirmQuery(scope, token, publishSample);
    if(
        !confirmed
        && accumulator->quarantineQuery(
            scope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
    return confirmed;
}

bool GpuTimingRecorder::prepareDeferredScopeForRecovery(const GpuTimingScope& scope){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(!accumulator)
        return false;

    const bool prepared = accumulator->prepareQueryForRecovery(scope);
    if(
        !prepared
        && accumulator->quarantineQuery(
            scope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
    return prepared;
}

bool GpuTimingRecorder::retireScope(const GpuTimingScope& scope, const QueueSubmissionToken& token)noexcept{
    if(!scope.valid())
        return true;

    NothrowScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(!accumulator)
        return false;

    const bool retired = accumulator->retireQuery(scope, token);
    if(
        !retired
        && accumulator->quarantineQuery(
            scope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
    return retired;
}

void GpuTimingRecorder::discardScope(GpuTimingScope& scope){
    if(!scope.valid()){
        scope = {};
        return;
    }

    GpuTimingScope discardedScope = scope;
    if(discardedScope.submissionTicket)
        discardedScope.submissionTicket->cancelScopePublication(discardedScope.submissionPublicationIndex);
    discardedScope.submissionTicket = nullptr;
    discardedScope.submissionPublicationIndex = Limit<usize>::s_Max;
    scope = {};

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(discardedScope);
    if(
        accumulator
        && accumulator->discardQuery(
            discardedScope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
}

void GpuTimingRecorder::abandonScopeWithoutCallbacks(GpuTimingScope& scope)noexcept{
    if(!scope.valid()){
        scope = {};
        return;
    }

    GpuTimingScope abandonedScope = scope;
    if(abandonedScope.submissionTicket)
        abandonedScope.submissionTicket->cancelScopePublication(abandonedScope.submissionPublicationIndex);
    abandonedScope.submissionTicket = nullptr;
    abandonedScope.submissionPublicationIndex = Limit<usize>::s_Max;
    scope = {};

    NothrowScopedLock lock(m_mutex);
    GpuTimingAccumulator* const accumulator = findAccumulator(abandonedScope);
    if(
        accumulator
        && accumulator->abandonQuery(
            abandonedScope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
}

void GpuTimingRecorder::quarantineScope(const GpuTimingScope& scope)noexcept{
    if(!scope.valid())
        return;

    NothrowScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(
        accumulator
        && accumulator->quarantineQuery(
            scope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
}

GpuTimingSubmissionTicket* GpuTimingRecorder::activeSubmissionTicket()const{
    GpuTimingSubmissionTicket* ticket = s_activeSubmissionTicket;
    return ticket && &ticket->m_recorder == this ? ticket : nullptr;
}

GpuTimingAccumulator* GpuTimingRecorder::findAccumulator(const GpuTimingScope& scope)noexcept{
    const auto found = m_accumulators.find(scope.scopeName);
    return found != m_accumulators.end() ? found.value().get() : nullptr;
}

GpuTimingAccumulator* GpuTimingRecorder::findOrCreateAccumulator(const Name& scopeName){
    if(m_metricCorrelator.hasOutputRole(scopeName))
        return nullptr;

    auto found = m_accumulators.find(scopeName);
    if(found != m_accumulators.end())
        return found.value().get();

    const Perf::TimingScopeId timingScope = m_timing.registerScope(scopeName);
    if(!timingScope.valid())
        return nullptr;

    AccumulatorPtr accumulator = MakeGlobalUnique<GpuTimingAccumulator>(m_arena, m_arena, scopeName, timingScope);
    if(!accumulator)
        return nullptr;

    auto [it, inserted] = m_accumulators.try_emplace(scopeName, Move(accumulator));
    if(!inserted)
        return it.value().get();

    if(it.value()->setCaptureEnabled(
        m_performanceCollectionActive || feedbackScopeDemandedLocked(scopeName),
        m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
    ))
        m_pendingAttributionRetirements = true;
    return it.value().get();
}

bool GpuTimingRecorder::collectLocked(
    Device& device,
    const u64 subscriptionIdentityLimit,
    SampleDispatchVector& completedSamples,
    GpuTimingSinkSampleVector& performanceSamples,
    Alloc::ScratchArena& scratchArena
){
    syncActiveState();
    reservePendingAttributionSamplesLocked(completedSamples);
    const bool publishPerformanceSamples = m_performanceCollectionActive;
    bool hasPendingAcceptedQueries = false;
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        hasPendingAcceptedQueries = hasPendingAcceptedQueries || it.value()->m_pendingAcceptedQueryCount != 0u;
    if(hasPendingAcceptedQueries){
        const GpuPhysicalQueueTopology queueTopology = device.getPhysicalQueueTopology();
        m_queueCompletions.clear();
        m_queueCompletions.resize(queueTopology.queueCount);
    }
    if(m_pendingAttributionRetirements){
        retireMarkedPendingAttributionsLocked(completedSamples);
    }
    if(!hasPendingAcceptedQueries)
        return publishPerformanceSamples;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(
            device,
            *this,
            m_epoch,
            m_performanceCaptureEpoch,
            subscriptionIdentityLimit,
            publishPerformanceSamples,
            completedSamples,
            performanceSamples,
            scratchArena
        );
    return publishPerformanceSamples;
}

bool GpuTimingRecorder::submissionCompleted(Device& device, const QueueSubmissionToken& token){
    const GpuPhysicalQueueId physicalQueue{ token.physicalQueueIndex, token.deviceGeneration };
    NWB_ASSERT(static_cast<usize>(physicalQueue.index) < m_queueCompletions.size());
    if(static_cast<usize>(physicalQueue.index) >= m_queueCompletions.size())
        return false;

    QueueCompletion& completion = m_queueCompletions[physicalQueue.index];
    if(completion.queue != physicalQueue){
        completion.queue = physicalQueue;
        completion.value = device.queueGetCompletedInstance(physicalQueue);
    }
    return completion.value >= token.value;
}

void GpuTimingRecorder::discardFrameResetLocked(){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->discardFrameReset();
}

void GpuTimingRecorder::noteSkippedScope(const GpuTimingScopeSkipReason::Enum reason){
    NWB_ASSERT(reason < GpuTimingScopeSkipReason::kCount);
    if(reason < GpuTimingScopeSkipReason::kCount)
        ++m_statistics.skippedScopeCountByReason[reason];
}

void GpuTimingRecorder::syncActiveState(const u64 subscriptionIdentityLimit)noexcept{
    const bool performanceCollectionActive = m_enabled && m_timing.enabled();
    if(m_performanceCollectionActive != performanceCollectionActive){
        advancePerformanceCaptureEpoch();
        if(m_performanceCollectionActive)
            m_metricCorrelator.discardPendingRanges();
    }
    m_performanceCollectionActive = performanceCollectionActive;
    m_accumulatorsActive = m_performanceCollectionActive || !m_feedbackScopeDemands.empty();
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it){
        GpuTimingAccumulator& accumulator = *it.value();
        if(accumulator.setCaptureEnabled(
            m_performanceCollectionActive || feedbackScopeDemandedLocked(it.key()),
            subscriptionIdentityLimit
        ))
            m_pendingAttributionRetirements = true;
    }
}

void GpuTimingRecorder::syncActiveState()noexcept{
    syncActiveState(m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire));
}

void GpuTimingRecorder::advancePerformanceCaptureEpoch()noexcept{
    ++m_performanceCaptureEpoch;
    if(m_performanceCaptureEpoch == 0u)
        ++m_performanceCaptureEpoch;
}

void GpuTimingRecorder::advanceEpoch()noexcept{
    ++m_epoch;
    if(m_epoch == 0u)
        m_epoch = 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

