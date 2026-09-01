// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextSampleSubscriptionIdentity{ 1u };
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


GpuTimingRecorder::GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing)
    : m_arena(arena)
    , m_timing(timing)
    , m_metricCorrelator(arena, timing)
    , m_accumulators(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_queueCompletions(arena)
    , m_sampleListeners(arena)
{}

void GpuTimingRecorder::setQueryCollectionEnabled(const bool enabled){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector retiredSamples{ scratchArena };
    u64 subscriptionIdentityLimit = 0u;
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();

        ScopedLock recorderLock(m_mutex);
        const bool wasActive = m_accumulatorsActive;
        const bool nextActive = (enabled && m_timing.enabled()) || m_feedbackCollectionRequestCount != 0u;
        if(wasActive && !nextActive && subscriptionIdentityLimit != 0u)
            reservePendingAttributionSamplesLocked(retiredSamples);
        m_enabled = enabled;
        setActiveState(nextActive);
        if(wasActive && !nextActive){
            if(subscriptionIdentityLimit != 0u)
                retireMarkedPendingAttributionsLocked(retiredSamples);
            else
                discardMarkedPendingAttributionsLocked();
        }
    }
    dispatchCompletedSamples(retiredSamples);
}

GpuTimingSampleSubscription GpuTimingRecorder::subscribeSampleListener(const GpuTimingSampleListener& listener){
    if(!listener.valid())
        return {};

    SampleListenerRecord* const listenerRecord = NewArenaObject<SampleListenerRecord>(m_arena);
    if(!listenerRecord)
        return {};
    SampleListenerRecordPtr record(
        listenerRecord,
        SampleListenerRecordPtr::deleter_type(&m_arena),
        AdoptRef
    );

    ScopedLock lock(m_sampleListenerMutex);
    const u64 identity = __hidden_gpu_timing::AllocateMonotonicIdentity(
        __hidden_gpu_timing::s_NextSampleSubscriptionIdentity
    );
    if(identity == 0u)
        return {};

    const GpuTimingSampleSubscription subscription(identity);
    record->subscription = subscription;
    record->listener = listener;
    m_sampleListeners.push_back(Move(record));
    publishSampleSubscriptionIdentityLimitLocked();
    return subscription;
}

void GpuTimingRecorder::unsubscribeSampleListener(const GpuTimingSampleSubscription& subscription)noexcept{
    SampleListenerRecordPtr record;
    u64 subscriptionIdentityLimit = 0u;
    bool retirePendingAttributions = false;
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        record = findSampleListenerLocked(subscription);
        if(!record)
            return;

        if(!record->removing){
            record->removing = true;
            publishSampleSubscriptionIdentityLimitLocked();
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
            if(record->feedbackCollectionEnabled){
                ScopedLock recorderLock(m_mutex);
                NWB_ASSERT(m_feedbackCollectionRequestCount > 0u);
                if(m_feedbackCollectionRequestCount > 0u){
                    const u64 nextRequestCount = m_feedbackCollectionRequestCount - 1u;
                    const bool wasActive = m_accumulatorsActive;
                    const bool nextActive = (m_enabled && m_timing.enabled()) || nextRequestCount != 0u;
                    m_feedbackCollectionRequestCount = nextRequestCount;
                    record->feedbackCollectionEnabled = false;
                    setActiveState(nextActive);
                    retirePendingAttributions = wasActive && !nextActive;
                }
            }
            if(subscriptionIdentityLimit == 0u){
                ScopedLock recorderLock(m_mutex);
                discardMarkedPendingAttributionsLocked();
            }
        }
    }

    {
        ScopedLock callbackLock(m_sampleCallbackMutex);
        ScopedLock listenerLock(m_sampleListenerMutex);

        eraseSampleListenerLocked(*record);
    }

    if(!retirePendingAttributions)
        return;
    if(subscriptionIdentityLimit == 0u)
        return;

    while(true){
        SampleDispatch retiredSample;
        {
            ScopedLock recorderLock(m_mutex);
            if(!retireMarkedPendingAttributionLocked(retiredSample))
                return;
        }
        dispatchCompletedSample(retiredSample.sample, retiredSample.subscriptionIdentityLimit);
    }
}

bool GpuTimingRecorder::setFeedbackCollectionEnabled(
    const GpuTimingSampleSubscription& subscription,
    const bool enabled
){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector retiredSamples{ scratchArena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        SampleListenerRecordPtr record = findSampleListenerLocked(subscription);
        if(!record || record->removing)
            return false;
        if(record->feedbackCollectionEnabled == enabled)
            return true;

        ScopedLock recorderLock(m_mutex);
        if(enabled){
            if(m_feedbackCollectionRequestCount == Limit<u64>::s_Max)
                return false;
        }
        else{
            NWB_ASSERT(m_feedbackCollectionRequestCount > 0u);
            if(m_feedbackCollectionRequestCount == 0u)
                return false;
        }
        const u64 nextRequestCount = enabled
            ? m_feedbackCollectionRequestCount + 1u
            : m_feedbackCollectionRequestCount - 1u
        ;
        const bool wasActive = m_accumulatorsActive;
        const bool nextActive = (m_enabled && m_timing.enabled()) || nextRequestCount != 0u;
        if(wasActive && !nextActive)
            reservePendingAttributionSamplesLocked(retiredSamples);
        m_feedbackCollectionRequestCount = nextRequestCount;
        record->feedbackCollectionEnabled = enabled;
        setActiveState(nextActive);
        if(wasActive && !nextActive)
            retireMarkedPendingAttributionsLocked(retiredSamples);
    }
    dispatchCompletedSamples(retiredSamples);
    return true;
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
    return (m_enabled && m_timing.enabled()) || m_feedbackCollectionRequestCount != 0u;
}

GpuTimingRecorderStatistics GpuTimingRecorder::statistics(const Device& device)const{
    ScopedLock lock(m_mutex);
    GpuTimingRecorderStatistics result = m_statistics;
    result.deviceGeneration = device.getDeviceGeneration();
    result.preparedScopeCount = static_cast<u64>(m_accumulators.size());
    result.queryCollectionEnabled = m_enabled;
    result.timingSinkEnabled = m_timing.enabled();
    result.feedbackCollectionEnabled = m_feedbackCollectionRequestCount != 0u;
    result.collectionActive = (m_enabled && m_timing.enabled()) || m_feedbackCollectionRequestCount != 0u;
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
        m_currentFrameIndex = 0u;
        m_statistics = {};
        m_pendingAttributionRetirements = false;
    }
    dispatchCompletedSamples(retiredSamples);
}

void GpuTimingRecorder::collect(Device& device){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector completedSamples{ scratchArena };
    try{
        u64 subscriptionIdentityLimit = 0u;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
        }

        {
            ScopedLock recorderLock(m_mutex);
            collectLocked(device, m_currentFrameIndex, subscriptionIdentityLimit, completedSamples, scratchArena);
        }
    }
    catch(...){
        dispatchCompletedSamples(completedSamples);
        throw;
    }
    dispatchCompletedSamples(completedSamples);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_GpuTimingScratchArena);
    SampleDispatchVector completedSamples{ scratchArena };
    try{
        u64 subscriptionIdentityLimit = 0u;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
        }

        {
            ScopedLock recorderLock(m_mutex);
            collectLocked(device, publishFrameIndex, subscriptionIdentityLimit, completedSamples, scratchArena);
        }
    }
    catch(...){
        dispatchCompletedSamples(completedSamples);
        throw;
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
    const bool materialized = !m_accumulatorsActive || accumulator->materializeRequestedQueries(device);
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
    if(m_accumulators.find(outputScope) != m_accumulators.end())
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
    if(m_accumulators.find(queueOverlapScope) != m_accumulators.end())
        return false;
    for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
        if(m_accumulators.find(queueOutputs[outputIndex].internalIdleScopeName) != m_accumulators.end())
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
        if(!m_accumulatorsActive){
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

        if(!found.value()->beginQuery(
            commandList,
            m_currentFrameIndex,
            m_epoch,
            attribution,
            outScope,
            resetSubmission
        )){
            ++m_statistics.beginFailureCount;
            return false;
        }
        if(outScope.valid())
            outScope.submissionTicket = ticket;
    }

    if(outScope.valid() && resetSubmission.valid() && !ticket->trackSubmissionPrerequisite(resetSubmission)){
        ScopedLock lock(m_mutex);
        if(GpuTimingAccumulator* const accumulator = findAccumulator(outScope)){
            if(accumulator->discardQuery(
                outScope,
                m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
            ))
                m_pendingAttributionRetirements = true;
        }
        outScope = {};
        ++m_statistics.beginFailureCount;
        return false;
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
    // The frame transaction owns this reservation until the end packet is accepted. The begin packet's submission
    // ticket deliberately has no rollback handle for it: a later recovery endpoint may be required after that begin
    // has already executed on the device timeline.
    outScope.submissionTicket = nullptr;
    return true;
}

void GpuTimingRecorder::endScope(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    GpuTimingAccumulator::QueryEndResult endResult = GpuTimingAccumulator::QueryEndResult::Invalid;
    {
        // Do not retain this lock while trackScope() takes the ticket lock: discard() rolls tickets back in the
        // opposite direction (ticket first, recorder second).
        ScopedLock lock(m_mutex);
        GpuTimingAccumulator* accumulator = findAccumulator(scope);
        if(accumulator)
            endResult = accumulator->endQuery(commandList, scope);
    }
    if(endResult == GpuTimingAccumulator::QueryEndResult::Invalid)
        return;

    if(scope.submissionTicket)
        scope.submissionTicket->trackScope(scope);
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
){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
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

bool GpuTimingRecorder::retireScope(const GpuTimingScope& scope, const QueueSubmissionToken& token){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
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

void GpuTimingRecorder::discardScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(
        accumulator
        && accumulator->discardQuery(
            scope,
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        )
    )
        m_pendingAttributionRetirements = true;
}

void GpuTimingRecorder::quarantineScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
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

GpuTimingAccumulator* GpuTimingRecorder::findAccumulator(const GpuTimingScope& scope){
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

    it.value()->setEnabled(m_accumulatorsActive);
    return it.value().get();
}

void GpuTimingRecorder::collectLocked(
    Device& device,
    const u64 publishFrameIndex,
    const u64 subscriptionIdentityLimit,
    SampleDispatchVector& completedSamples,
    Alloc::ScratchArena& scratchArena
){
    syncActiveState();
    reservePendingAttributionSamplesLocked(completedSamples);
    const bool publishPerformanceSamples = m_enabled && m_timing.enabled();
    if(m_accumulatorsActive){
        const GpuPhysicalQueueTopology queueTopology = device.getPhysicalQueueTopology();
        m_queueCompletions.clear();
        m_queueCompletions.resize(queueTopology.queueCount);
    }
    if(m_pendingAttributionRetirements){
        retireMarkedPendingAttributionsLocked(completedSamples);
    }
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(
            device,
            *this,
            m_epoch,
            subscriptionIdentityLimit,
            publishPerformanceSamples,
            completedSamples,
            scratchArena
        );
    if(publishPerformanceSamples)
        m_timing.publishFrame(publishFrameIndex);
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

void GpuTimingRecorder::setActiveState(const bool active)noexcept{
    if(active == m_accumulatorsActive)
        return;

    if(!active){
        markPendingAttributionsForRetirementLocked(
            m_sampleSubscriptionIdentityLimit.load(MemoryOrder::acquire)
        );
        advanceEpoch();
        m_metricCorrelator.discardPendingRanges();
    }
    m_accumulatorsActive = active;
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->setEnabled(active);
}

void GpuTimingRecorder::syncActiveState()noexcept{
    setActiveState((m_enabled && m_timing.enabled()) || m_feedbackCollectionRequestCount != 0u);
}

void GpuTimingRecorder::advanceEpoch()noexcept{
    ++m_epoch;
    if(m_epoch == 0u)
        m_epoch = 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

