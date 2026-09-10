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


GpuTimingRecorder::GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing)
    : m_arena(arena)
    , m_timing(timing)
    , m_metricCorrelator(arena, timing)
    , m_accumulators(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_queueCompletions(arena)
    , m_sampleListeners(arena)
    , m_feedbackScopeDemands(arena)
{}

void GpuTimingRecorder::setQueryCollectionEnabled(const bool enabled){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector retiredSamples{ scratchArena };
    u64 subscriptionIdentityLimit = 0u;
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();

        ScopedLock recorderLock(m_mutex);
        if(subscriptionIdentityLimit != 0u)
            reservePendingAttributionSamplesLocked(retiredSamples);
        m_enabled = enabled;
        syncActiveState(subscriptionIdentityLimit);
        if(m_pendingAttributionRetirements){
            if(subscriptionIdentityLimit != 0u)
                retireMarkedPendingAttributionsLocked(retiredSamples);
            else
                discardMarkedPendingAttributionsLocked();
        }
    }
    dispatchCompletedSamples(retiredSamples);
}

GpuTimingSampleAttribution GpuTimingRecorder::allocateSampleAttribution()noexcept{
    return GpuTimingSampleAttribution(__hidden_gpu_timing::AllocateMonotonicIdentity(
        __hidden_gpu_timing::s_NextSampleAttributionIdentity
    ));
}

bool GpuTimingRecorder::queryCollectionEnabled()const{
    ScopedLock lock(m_mutex);
    return m_enabled;
}

bool GpuTimingRecorder::collectionActive()const{
    ScopedLock lock(m_mutex);
    return (m_enabled && m_timing.enabled()) || !m_feedbackScopeDemands.empty();
}

GpuTimingRecorderStatistics GpuTimingRecorder::statistics(const Device& device)const{
    ScopedLock lock(m_mutex);
    GpuTimingRecorderStatistics result = m_statistics;
    result.deviceGeneration = device.getDeviceGeneration();
    result.preparedScopeCount = static_cast<u64>(m_accumulators.size());
    result.queryCollectionEnabled = m_enabled;
    result.timingSinkEnabled = m_timing.enabled();
    result.feedbackCollectionEnabled = !m_feedbackScopeDemands.empty();
    result.collectionActive = (m_enabled && m_timing.enabled()) || !m_feedbackScopeDemands.empty();
    result.comparableTimestampsSupported = device.supportsComparableGpuTimestamps();
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->appendStatistics(result);
    return result;
}

void GpuTimingRecorder::resetQueries(){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector retiredSamples{ scratchArena };
    u64 subscriptionIdentityLimit = 0u;
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
    }

    {
        ScopedLock recorderLock(m_mutex);
        if(subscriptionIdentityLimit != 0u){
            reservePendingAttributionSamplesLocked(retiredSamples);
            retirePendingAttributionsLocked(retiredSamples, subscriptionIdentityLimit);
        }
        m_accumulators.clear();
        m_queueCompletions.clear();
        m_metricCorrelator.reset();
        advanceEpoch();
        m_accumulatorsActive = false;
        m_performanceCollectionActive = false;
        m_currentFrameIndex = 0u;
        m_statistics = {};
        m_pendingAttributionRetirements = false;
    }
    dispatchCompletedSamples(retiredSamples);
}

void GpuTimingRecorder::collect(Device& device){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector completedSamples{ scratchArena };
    GpuTimingSinkSampleVector performanceSamples{ scratchArena };

    {
        ScopedLock collectionLock(m_collectionMutex);
        u64 subscriptionIdentityLimit = 0u;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
        }

        u64 publishFrameIndex = 0u;
        bool publishPerformanceSamples = false;
        {
            ScopedLock recorderLock(m_mutex);
            publishFrameIndex = m_currentFrameIndex;
            publishPerformanceSamples = collectLocked(
                device,
                subscriptionIdentityLimit,
                completedSamples,
                performanceSamples,
                scratchArena
            );
        }
        for(const GpuTimingSinkSample& sample : performanceSamples)
            m_timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
        if(publishPerformanceSamples)
            m_timing.publishFrame(publishFrameIndex);
    }
    dispatchCompletedSamples(completedSamples);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector completedSamples{ scratchArena };
    GpuTimingSinkSampleVector performanceSamples{ scratchArena };

    {
        ScopedLock collectionLock(m_collectionMutex);
        u64 subscriptionIdentityLimit = 0u;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
        }

        bool publishPerformanceSamples = false;
        {
            ScopedLock recorderLock(m_mutex);
            publishPerformanceSamples = collectLocked(
                device,
                subscriptionIdentityLimit,
                completedSamples,
                performanceSamples,
                scratchArena
            );
        }
        for(const GpuTimingSinkSample& sample : performanceSamples)
            m_timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
        if(publishPerformanceSamples)
            m_timing.publishFrame(publishFrameIndex);
    }
    dispatchCompletedSamples(completedSamples);
}

void GpuTimingRecorder::beginFrame(const u64 frameIndex){
    ScopedLock lock(m_mutex);
    m_currentFrameIndex = frameIndex;
}

bool GpuTimingRecorder::prepareScopeQueries(const Name& scopeName, Device& device, const u32 queryCount){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!scopeName)
        return false;

    GpuTimingAccumulator* accumulator = findOrCreateAccumulator(scopeName);
    if(!accumulator)
        return false;

    accumulator->requestQueries(queryCount);
    const bool materialized = accumulator->materializeRequestedQueries(device);
    if(!materialized)
        ++m_statistics.queryMaterializationFailureCount;
    return materialized;
}

bool GpuTimingRecorder::prepareOverlapMetric(
    const Name& firstScope,
    const Name& secondScope,
    const Name& outputScope
){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(m_accumulators.find(outputScope) != m_accumulators.end() || feedbackScopeDemandedLocked(outputScope))
        return false;
    return m_metricCorrelator.prepareOverlapMetric(firstScope, secondScope, outputScope);
}

bool GpuTimingRecorder::preparePacketEnvelopeMetrics(
    const u64 sourceFrameIndex,
    const NotNull<const GpuPacketEnvelopeMetricScope*> scopeInputs,
    const usize scopeCount,
    const Name& queueOverlapScope,
    const NotNull<const GpuPacketEnvelopeMetricQueueOutput*> queueOutputInputs,
    const usize queueOutputCount
){
    ScopedLock lock(m_mutex);
    syncActiveState();
    const GpuPacketEnvelopeMetricQueueOutput* const queueOutputs = queueOutputInputs.get();
    if(
        scopeCount == 0u
        || !queueOverlapScope
        || queueOutputCount == 0u
        || queueOutputCount > scopeCount
    )
        return false;
    if(
        m_accumulators.find(queueOverlapScope) != m_accumulators.end()
        || feedbackScopeDemandedLocked(queueOverlapScope)
    )
        return false;
    for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
        if(
            m_accumulators.find(queueOutputs[outputIndex].internalIdleScopeName) != m_accumulators.end()
            || feedbackScopeDemandedLocked(queueOutputs[outputIndex].internalIdleScopeName)
        )
            return false;
    }
    return m_metricCorrelator.preparePacketEnvelopeMetrics(
        sourceFrameIndex,
        scopeInputs,
        scopeCount,
        queueOverlapScope,
        queueOutputInputs,
        queueOutputCount
    );
}

bool GpuTimingRecorder::materializeRequestedQueries(Device& device){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive)
        return true;

    bool materialized = true;
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        materialized = it.value()->materializeRequestedQueries(device) && materialized;
    if(!materialized)
        ++m_statistics.queryMaterializationFailureCount;
    return materialized;
}

void GpuTimingRecorder::recordFrameReset(CommandList& commandList){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->recordFrameReset(commandList);
}

void GpuTimingRecorder::confirmFrameReset(const QueueSubmissionToken& token){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive){
        discardFrameResetLocked();
        return;
    }

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->confirmFrameReset(token);
}

void GpuTimingRecorder::discardFrameReset(){
    ScopedLock lock(m_mutex);
    discardFrameResetLocked();
}

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

