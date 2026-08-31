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
inline constexpr Name s_PacketEnvelopeMetricScratchArena("graphics.gpu_timing.packet_envelope_metric_scratch");


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


void GpuTimingAccumulator::collect(
    Device& device,
    GpuTimingRecorder& recorder,
    const u32 epoch,
    Vector<GpuTimingSample, Alloc::GlobalArena>* const completedSamples,
    Alloc::ScratchArena& scratchArena
){
    if(!m_enabled)
        return;

    if(completedSamples)
        completedSamples->reserve(completedSamples->size() + m_queries.size());

    for(QueryRecord& record : m_queries){
        if(record.state != QueryState::PendingAccepted || !record.query)
            continue;
        if(
            !record.acceptedSubmission.valid()
            || !record.acceptedSubmission.hasPhysicalQueueIdentity()
            || record.acceptedSubmission.queue != record.queueClass
            || !record.acceptedSubmission.matchesPhysicalQueue(
                record.physicalQueue.index,
                record.physicalQueue.deviceGeneration
            )
            || !device.matchesPhysicalQueueIdentity(
                record.acceptedSubmission.queue,
                record.acceptedSubmission.physicalQueueIndex,
                record.acceptedSubmission.deviceGeneration
            )
        ){
            record.state = QueryState::Quarantined;
            record.frameResetRecorded = false;
            record.deviceReady = false;
            ++m_quarantinedScopeCount;
            NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined after its accepted submission lost exact queue identity"));
            continue;
        }
        if(!recorder.submissionCompleted(device, record.acceptedSubmission))
            continue;
        if(!device.pollTimerQuery(record.query.get()))
            continue;

        TimerQueryResult result;
        if(!device.getTimerQueryResult(record.query.get(), result))
            continue;

        const bool publishSample = record.epoch == epoch && record.publishSample;
        const f64 durationSeconds = result.durationSeconds();
        GpuComparableTimestampRange comparableRange;
        if(publishSample){
            ++m_publishedSampleCount;
            recorder.m_timing.recordSample(m_timingScope, durationSeconds, record.frameIndex);
            if(result.hasComparableRange()){
                comparableRange = GpuComparableTimestampRange{
                    result.beginTicks,
                    result.endTicks,
                    result.secondsPerTick,
                    result.physicalQueue,
                };
                recorder.m_metricCorrelator.recordTimestampRange(
                    m_scopeName,
                    record.frameIndex,
                    comparableRange,
                    scratchArena
                );
            }
        }
        else
            ++m_unpublishedSampleCount;
        if(completedSamples && record.attribution != s_NoGpuTimingSampleAttribution){
            completedSamples->push_back(GpuTimingSample{
                .scopeName = m_scopeName,
                .sourceFrameIndex = record.frameIndex,
                .durationSeconds = publishSample ? durationSeconds : 0.0,
                .physicalQueue = record.physicalQueue,
                .attribution = record.attribution,
                .published = publishSample,
                .comparableRange = comparableRange,
            });
        }
        releaseQuery(record);
    }
}

void GpuTimingAccumulator::recordFrameReset(CommandList& commandList){
    if(!m_enabled)
        return;

    const bool canReset = commandList.canResetTimerQueryHere();
    // Retain pending pools until their result is observable instead of erasing a late GPU sample. Pools that are
    // already available this frame are reset on the device timeline, but do not become usable by dynamic-rendering
    // scopes until the caller confirms this command list submitted successfully.
    for(QueryRecord& record : m_queries){
        record.frameResetRecorded = false;
        // A render-pass scope must observe this frame's reset, not merely a reset that happened during an earlier
        // frame. Leave the pool unavailable until confirmFrameReset() observes a successful preamble submission.
        record.deviceReady = false;
        if(!canReset || !record.query || record.state != QueryState::Available)
            continue;

        commandList.resetTimerQuery(record.query.get());
        record.frameResetRecorded = true;
    }
}

void GpuTimingAccumulator::confirmFrameReset(){
    for(QueryRecord& record : m_queries){
        if(!record.frameResetRecorded)
            continue;

        record.frameResetRecorded = false;
        if(m_enabled && record.state == QueryState::Available)
            record.deviceReady = true;
    }
}

void GpuTimingAccumulator::discardFrameReset(){
    for(QueryRecord& record : m_queries){
        record.frameResetRecorded = false;
        // A failed (or not-yet-recorded) preamble must not let a dynamic-rendering scope reuse a previous frame's
        // reset. Outside a render pass beginTimerQuery() still performs its own device-timeline reset.
        record.deviceReady = false;
    }
}

void GpuTimingAccumulator::requestQueries(const u32 queryCount){
    m_requestedQueryCount = Max(m_requestedQueryCount, queryCount);
}

bool GpuTimingAccumulator::materializeRequestedQueries(Device& device){
    return m_requestedQueryCount == 0u || reserveQueries(device, m_requestedQueryCount);
}

bool GpuTimingAccumulator::beginQuery(
    CommandList& commandList,
    const u64 frameIndex,
    const u32 epoch,
    const GpuTimingSampleAttribution attribution,
    GpuTimingScope& outScope
){
    outScope = {};
    if(!m_enabled){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::CollectionInactive];
        return true;
    }

    const u32 index = findAvailableQuery();
    if(index == Limit<u32>::s_Max){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::QueryCapacityUnavailable];
        return true;
    }

    QueryRecord& record = m_queries[index];
    // beginTimerQuery self-resets an already prepared pool when recording outside a render pass. Inside a render pass
    // that reset is illegal, so only pools that recordFrameReset() made deviceReady are eligible. Under-reserved or
    // undeclared scopes skip their sample instead of allocating persistent query pools from a recording path.
    if(!commandList.isRecording() || !commandList.hasCommandBuffer() || commandList.commandRecordingFailed())
        return false;
    if(!commandList.canRecordTimerQueryHere()){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
        return true;
    }
    if(commandList.isRenderPassActive()){
        if(!record.deviceReady){
            ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
            return true;
        }
    }
    else if(!commandList.canResetTimerQueryHere()){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
        return true;
    }

    // A device-timeline reset authorizes exactly one timestamp pair. Consume it as soon as the reservation records
    // its begin endpoint, even if that command buffer is later discarded before submission.
    if(!commandList.beginTimerQuery(record.query.get()))
        return false;

    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    record.physicalQueue = commandListDescription.physicalQueue;
    record.acceptedSubmission = {};
    record.queueClass = commandListDescription.queueType;
    record.state = QueryState::Recording;
    record.publishSample = true;
    record.frameResetRecorded = false;
    record.deviceReady = false;
    record.frameIndex = frameIndex;
    record.attribution = attribution;
    record.epoch = epoch;
    ++m_nextReservation;
    if(m_nextReservation == 0u)
        ++m_nextReservation;
    record.reservation = m_nextReservation;
    outScope = GpuTimingScope{ m_scopeName, index, epoch, record.reservation };
    ++m_recordedScopeCount;
    return true;
}

bool GpuTimingAccumulator::endQuery(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(
        record.epoch != scope.epoch
        || record.reservation != scope.reservation
        || record.state != QueryState::Recording
    )
        return false;
    if(!commandList.endTimerQuery(record.query.get())){
        quarantineQuery(scope);
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined after its ending timestamp failed to record"));
        return false;
    }
    record.state = QueryState::EndedUnaccepted;
    return true;
}

bool GpuTimingAccumulator::recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(
        record.epoch != scope.epoch
        || record.reservation != scope.reservation
        || record.state != QueryState::Recording
    )
        return false;

    if(!commandList.endTimerQuery(record.query.get()))
        return false;

    record.state = QueryState::EndedUnaccepted;
    return true;
}

bool GpuTimingAccumulator::validateQuerySubmission(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token
)const{
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    const QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(record.state != QueryState::Recording && record.state != QueryState::EndedUnaccepted)
        return false;

    return token.valid()
        && token.hasPhysicalQueueIdentity()
        && token.queue == record.queueClass
        && token.matchesPhysicalQueue(record.physicalQueue.index, record.physicalQueue.deviceGeneration)
    ;
}

bool GpuTimingAccumulator::confirmQuery(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token,
    const bool publishSample
){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(record.state != QueryState::EndedUnaccepted || !validateQuerySubmission(scope, token)){
        quarantineQuery(scope);
        return false;
    }

    record.acceptedSubmission = token;
    record.state = QueryState::PendingAccepted;
    record.publishSample = publishSample;
    ++m_acceptedScopeCount;
    return true;
}

bool GpuTimingAccumulator::prepareQueryForRecovery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(record.state != QueryState::EndedUnaccepted){
        quarantineQuery(scope);
        return false;
    }

    record.state = QueryState::Recording;
    return true;
}

void GpuTimingAccumulator::discardQuery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return;

    if(record.state == QueryState::PendingAccepted){
        quarantineQuery(scope);
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined because accepted work attempted rollback release"));
        return;
    }
    if(record.state == QueryState::Quarantined)
        return;

    releaseQuery(record);
    ++m_discardedScopeCount;
}

void GpuTimingAccumulator::quarantineQuery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return;
    if(record.state == QueryState::Quarantined)
        return;

    record.state = QueryState::Quarantined;
    record.publishSample = false;
    record.frameResetRecorded = false;
    record.deviceReady = false;
    ++m_quarantinedScopeCount;
}

bool GpuTimingAccumulator::reserveQueries(Device& device, const u32 queryCount){
    while(m_queries.size() < static_cast<usize>(queryCount)){
        if(appendQuery(device) == Limit<u32>::s_Max)
            return false;
    }
    return true;
}

u32 GpuTimingAccumulator::findAvailableQuery()const{
    for(usize i = 0u; i < m_queries.size(); ++i){
        if(m_queries[i].state == QueryState::Available)
            return static_cast<u32>(i);
    }

    return Limit<u32>::s_Max;
}

u32 GpuTimingAccumulator::appendQuery(Device& device){
    QueryRecord record;
    record.query = device.createTimerQuery();
    if(!record.query)
        return Limit<u32>::s_Max;

    m_queries.push_back(Move(record));
    return static_cast<u32>(m_queries.size() - 1u);
}

void GpuTimingAccumulator::releaseQuery(QueryRecord& record){
    // A discarded command buffer may already contain timestamp writes. Require another accepted preamble reset
    // before a dynamic-rendering scope reuses this pool; outside a render pass beginTimerQuery() resets it itself.
    record.physicalQueue = {};
    record.acceptedSubmission = {};
    record.frameIndex = 0u;
    record.epoch = 0u;
    record.reservation = 0u;
    record.queueClass = CommandQueue::kCount;
    record.state = QueryState::Available;
    record.publishSample = true;
    record.attribution = s_NoGpuTimingSampleAttribution;
    record.frameResetRecorded = false;
    record.deviceReady = false;
}

void GpuTimingAccumulator::retireAttributions(Vector<GpuTimingSample, Alloc::GlobalArena>& outSamples){
    for(QueryRecord& record : m_queries){
        if(record.attribution == s_NoGpuTimingSampleAttribution)
            continue;

        outSamples.push_back(GpuTimingSample{
            .scopeName = m_scopeName,
            .sourceFrameIndex = record.frameIndex,
            .physicalQueue = record.physicalQueue,
            .attribution = record.attribution,
            .comparableRange = {},
        });
        record.attribution = s_NoGpuTimingSampleAttribution;
    }
}

void GpuTimingAccumulator::appendStatistics(GpuTimingRecorderStatistics& outStatistics)const noexcept{
    outStatistics.requestedQueryCount += static_cast<u64>(m_requestedQueryCount);
    outStatistics.materializedQueryCount += static_cast<u64>(m_queries.size());
    outStatistics.recordedScopeCount += m_recordedScopeCount;
    outStatistics.acceptedScopeCount += m_acceptedScopeCount;
    outStatistics.publishedSampleCount += m_publishedSampleCount;
    outStatistics.unpublishedSampleCount += m_unpublishedSampleCount;
    outStatistics.discardedScopeCount += m_discardedScopeCount;
    outStatistics.quarantinedScopeCount += m_quarantinedScopeCount;
    for(u8 reason = 0u; reason < GpuTimingScopeSkipReason::kCount; ++reason)
        outStatistics.skippedScopeCountByReason[reason] += m_skippedScopeCountByReason[reason];
}


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
    SampleVector retiredSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        snapshotSampleSubscriptionsLocked(subscriptions);

        ScopedLock recorderLock(m_mutex);
        const bool wasActive = m_accumulatorsActive;
        m_enabled = enabled;
        syncActiveState();
        if(wasActive && !m_accumulatorsActive)
            retirePendingAttributionsLocked(retiredSamples);
    }
    dispatchCompletedSamples(retiredSamples, subscriptions);
}

GpuTimingSampleSubscription GpuTimingRecorder::subscribeSampleListener(const GpuTimingSampleListener& listener){
    if(!listener.valid())
        return {};

    const u64 identity = __hidden_gpu_timing::AllocateMonotonicIdentity(
        __hidden_gpu_timing::s_NextSampleSubscriptionIdentity
    );
    if(identity == 0u)
        return {};

    ScopedLock lock(m_sampleListenerMutex);
    const GpuTimingSampleSubscription subscription(identity);
    m_sampleListeners.push_back(SampleListenerRecord{
        .subscription = subscription,
        .listener = listener,
    });
    return subscription;
}

void GpuTimingRecorder::unsubscribeSampleListener(const GpuTimingSampleSubscription& subscription){
    SampleVector retiredSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        SampleListenerRecord* const record = findSampleListenerLocked(subscription);
        if(!record)
            return;

        const usize listenerIndex = static_cast<usize>(record - m_sampleListeners.data());
        const bool synchronizedCollection = record->feedbackCollectionEnabled;
        m_sampleListeners.erase(m_sampleListeners.begin() + static_cast<isize>(listenerIndex));

        if(synchronizedCollection){
            ScopedLock recorderLock(m_mutex);
            NWB_ASSERT(m_feedbackCollectionRequestCount > 0u);
            if(m_feedbackCollectionRequestCount > 0u)
                --m_feedbackCollectionRequestCount;

            const bool wasActive = m_accumulatorsActive;
            syncActiveState();
            if(wasActive && !m_accumulatorsActive)
                retirePendingAttributionsLocked(retiredSamples);
        }
        snapshotSampleSubscriptionsLocked(subscriptions);
    }
    dispatchCompletedSamples(retiredSamples, subscriptions);
}

bool GpuTimingRecorder::setFeedbackCollectionEnabled(
    const GpuTimingSampleSubscription& subscription,
    const bool enabled
){
    SampleVector retiredSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        SampleListenerRecord* const record = findSampleListenerLocked(subscription);
        if(!record)
            return false;
        if(record->feedbackCollectionEnabled == enabled)
            return true;

        ScopedLock recorderLock(m_mutex);
        if(enabled){
            if(m_feedbackCollectionRequestCount == Limit<u64>::s_Max)
                return false;
            ++m_feedbackCollectionRequestCount;
        }
        else{
            NWB_ASSERT(m_feedbackCollectionRequestCount > 0u);
            if(m_feedbackCollectionRequestCount == 0u)
                return false;
            --m_feedbackCollectionRequestCount;
        }
        record->feedbackCollectionEnabled = enabled;

        const bool wasActive = m_accumulatorsActive;
        syncActiveState();
        if(wasActive && !m_accumulatorsActive)
            retirePendingAttributionsLocked(retiredSamples);
        snapshotSampleSubscriptionsLocked(subscriptions);
    }
    dispatchCompletedSamples(retiredSamples, subscriptions);
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
    SampleVector retiredSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        snapshotSampleSubscriptionsLocked(subscriptions);

        ScopedLock recorderLock(m_mutex);
        if(!subscriptions.empty())
            retirePendingAttributionsLocked(retiredSamples);
        m_accumulators.clear();
        m_queueCompletions.clear();
        m_metricCorrelator.reset();
        advanceEpoch();
        m_accumulatorsActive = false;
        m_currentFrameIndex = 0u;
        m_statistics = {};
    }
    dispatchCompletedSamples(retiredSamples, subscriptions);
}

void GpuTimingRecorder::collect(Device& device){
    SampleVector completedSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        snapshotSampleSubscriptionsLocked(subscriptions);

        ScopedLock recorderLock(m_mutex);
        collectLocked(device, m_currentFrameIndex, subscriptions.empty() ? nullptr : &completedSamples);
    }
    dispatchCompletedSamples(completedSamples, subscriptions);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    SampleVector completedSamples{ m_arena };
    SampleSubscriptionVector subscriptions{ m_arena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        snapshotSampleSubscriptionsLocked(subscriptions);

        ScopedLock recorderLock(m_mutex);
        collectLocked(device, publishFrameIndex, subscriptions.empty() ? nullptr : &completedSamples);
    }
    dispatchCompletedSamples(completedSamples, subscriptions);
}

void GpuTimingRecorder::collectLocked(
    Device& device,
    const u64 publishFrameIndex,
    Vector<GpuTimingSample, Alloc::GlobalArena>* const completedSamples
){
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    Alloc::ScratchArena scratchArena(__hidden_gpu_timing::s_PacketEnvelopeMetricScratchArena);
    m_queueCompletions.clear();
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(device, *this, m_epoch, completedSamples, scratchArena);
    m_timing.publishFrame(publishFrameIndex);
}

bool GpuTimingRecorder::submissionCompleted(Device& device, const QueueSubmissionToken& token){
    const GpuPhysicalQueueId physicalQueue{ token.physicalQueueIndex, token.deviceGeneration };
    if(m_queueCompletions.size() <= static_cast<usize>(physicalQueue.index))
        m_queueCompletions.resize(static_cast<usize>(physicalQueue.index) + 1u);

    QueueCompletion& completion = m_queueCompletions[physicalQueue.index];
    if(completion.queue != physicalQueue){
        completion.queue = physicalQueue;
        completion.value = device.queueGetCompletedInstance(physicalQueue);
    }
    return completion.value >= token.value;
}

GpuTimingRecorder::SampleListenerRecord* GpuTimingRecorder::findSampleListenerLocked(
    const GpuTimingSampleSubscription& subscription
)noexcept{
    if(!subscription.valid())
        return nullptr;

    for(SampleListenerRecord& record : m_sampleListeners){
        if(record.subscription == subscription && record.listener.valid())
            return &record;
    }
    return nullptr;
}

void GpuTimingRecorder::snapshotSampleSubscriptionsLocked(SampleSubscriptionVector& outSubscriptions)const{
    outSubscriptions.clear();
    outSubscriptions.reserve(m_sampleListeners.size());
    for(const SampleListenerRecord& record : m_sampleListeners){
        if(record.subscription.valid() && record.listener.valid())
            outSubscriptions.push_back(record.subscription);
    }
}

void GpuTimingRecorder::dispatchCompletedSamples(
    const SampleVector& samples,
    const SampleSubscriptionVector& subscriptions
){
    for(const GpuTimingSample& sample : samples){
        for(const GpuTimingSampleSubscription& subscription : subscriptions){
            ScopedLock lock(m_sampleListenerMutex);
            SampleListenerRecord* const record = findSampleListenerLocked(subscription);
            if(!record)
                continue;

            const GpuTimingSampleListener listener = record->listener;
            listener.invoke(listener.context, sample);
        }
    }
}

void GpuTimingRecorder::retirePendingAttributionsLocked(Vector<GpuTimingSample, Alloc::GlobalArena>& outSamples){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->retireAttributions(outSamples);
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

void GpuTimingRecorder::confirmFrameReset(){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive){
        discardFrameResetLocked();
        return;
    }

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->confirmFrameReset();
}

void GpuTimingRecorder::discardFrameReset(){
    ScopedLock lock(m_mutex);
    discardFrameResetLocked();
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

bool GpuTimingRecorder::beginScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    const bool requiresComparableTimestamps,
    GpuTimingScope& outScope
){
    outScope = {};
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

    GpuTimingSubmissionTicket* const ticket = activeSubmissionTicket();
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

    if(!found.value()->beginQuery(commandList, m_currentFrameIndex, m_epoch, attribution, outScope)){
        ++m_statistics.beginFailureCount;
        return false;
    }
    if(outScope.valid())
        outScope.submissionTicket = ticket;
    return true;
}

bool GpuTimingRecorder::beginDeferredScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    GpuTimingScope& outScope
){
    if(!beginScope(scopeName, device, commandList, attribution, true, outScope))
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

    bool ended = false;
    {
        // Do not retain this lock while trackScope() takes the ticket lock: discard() rolls tickets back in the
        // opposite direction (ticket first, recorder second).
        ScopedLock lock(m_mutex);
        GpuTimingAccumulator* accumulator = findAccumulator(scope);
        ended = accumulator && accumulator->endQuery(commandList, scope);
    }
    if(!ended)
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
    return accumulator && accumulator->confirmQuery(scope, token, publishSample);
}

bool GpuTimingRecorder::prepareDeferredScopeForRecovery(const GpuTimingScope& scope){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    return accumulator && accumulator->prepareQueryForRecovery(scope);
}

void GpuTimingRecorder::discardScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(accumulator)
        accumulator->discardQuery(scope);
}

void GpuTimingRecorder::quarantineScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    GpuTimingAccumulator* accumulator = findAccumulator(scope);
    if(accumulator)
        accumulator->quarantineQuery(scope);
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

void GpuTimingRecorder::syncActiveState(){
    const bool active = (m_enabled && m_timing.enabled()) || m_feedbackCollectionRequestCount != 0u;
    if(active == m_accumulatorsActive)
        return;

    if(!active){
        advanceEpoch();
        m_metricCorrelator.discardPendingRanges();
    }
    m_accumulatorsActive = active;
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->setEnabled(active);
}

void GpuTimingRecorder::advanceEpoch(){
    ++m_epoch;
    if(m_epoch == 0u)
        m_epoch = 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

