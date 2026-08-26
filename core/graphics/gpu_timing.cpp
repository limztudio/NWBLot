// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuTimingSubmissionTicket* GpuTimingRecorder::s_activeSubmissionTicket = nullptr;

// Retain enough rejected-frame endpoints to pair an asynchronously arriving overlap timestamp without allowing the
// correlation cache to grow unbounded.
static constexpr u64 s_PendingOverlapRetentionFramesPerInFlightFrame = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingAccumulator::collect(
    Device& device,
    GpuTimingRecorder& recorder,
    const u32 epoch,
    Vector<GpuTimingSample, Alloc::GlobalArena>* const completedSamples
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
        if(publishSample){
            recorder.m_timing.recordSample(m_timingScope, durationSeconds, record.frameIndex);
            if(result.hasComparableRange()){
                recorder.recordTimestampRange(
                    m_scopeName,
                    record.frameIndex,
                    GpuComparableTimestampRange{
                        result.beginTicks,
                        result.endTicks,
                        result.secondsPerTick,
                        result.physicalQueue,
                    }
                );
            }
        }
        if(completedSamples && record.attribution != s_NoGpuTimingSampleAttribution){
            completedSamples->push_back(GpuTimingSample{
                .scopeName = m_scopeName,
                .sourceFrameIndex = record.frameIndex,
                .durationSeconds = publishSample ? durationSeconds : 0.0,
                .attribution = record.attribution,
                .published = publishSample,
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
    if(!m_enabled)
        return true;

    const u32 index = findAvailableQuery();
    if(index == Limit<u32>::s_Max)
        return true;

    QueryRecord& record = m_queries[index];
    // beginTimerQuery self-resets an already prepared pool when recording outside a render pass. Inside a render pass
    // that reset is illegal, so only pools that recordFrameReset() made deviceReady are eligible. Under-reserved or
    // undeclared scopes skip their sample instead of allocating persistent query pools from a recording path.
    if(!commandList.isRecording() || !commandList.hasCommandBuffer() || commandList.commandRecordingFailed())
        return false;
    if(!commandList.canRecordTimerQueryHere())
        return true;
    if(commandList.isRenderPassActive()){
        if(!record.deviceReady)
            return true;
    }
    else if(!commandList.canResetTimerQueryHere())
        return true;

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
}

void GpuTimingAccumulator::quarantineQuery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return;

    record.state = QueryState::Quarantined;
    record.publishSample = false;
    record.frameResetRecorded = false;
    record.deviceReady = false;
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
            .attribution = record.attribution,
        });
        record.attribution = s_NoGpuTimingSampleAttribution;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingRecorder::GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing)
    : m_arena(arena)
    , m_timing(timing)
    , m_accumulators(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_queueCompletions(arena)
    , m_overlapRecords(arena)
{}

void GpuTimingRecorder::setQueryCollectionEnabled(const bool enabled){
    const bool collectSamples = m_hasSampleListener.load(MemoryOrder::acquire);
    const u64 listenerGeneration = collectSamples
        ? m_sampleListenerGeneration.load(MemoryOrder::acquire)
        : 0u
    ;
    Vector<GpuTimingSample, Alloc::GlobalArena> retiredSamples{ m_arena };
    {
        ScopedLock lock(m_mutex);
        const bool wasActive = m_accumulatorsActive;
        m_enabled = enabled;
        syncActiveState();
        if(wasActive && !m_accumulatorsActive && collectSamples)
            retirePendingAttributionsLocked(retiredSamples);
    }
    dispatchCompletedSamples(retiredSamples, listenerGeneration);
}

void GpuTimingRecorder::setFeedbackCollectionEnabled(const bool enabled){
    const bool collectSamples = m_hasSampleListener.load(MemoryOrder::acquire);
    const u64 listenerGeneration = collectSamples
        ? m_sampleListenerGeneration.load(MemoryOrder::acquire)
        : 0u
    ;
    Vector<GpuTimingSample, Alloc::GlobalArena> retiredSamples{ m_arena };
    {
        ScopedLock lock(m_mutex);
        const bool wasActive = m_accumulatorsActive;
        m_feedbackCollectionEnabled = enabled;
        syncActiveState();
        if(wasActive && !m_accumulatorsActive && collectSamples)
            retirePendingAttributionsLocked(retiredSamples);
    }
    dispatchCompletedSamples(retiredSamples, listenerGeneration);
}

void GpuTimingRecorder::setSampleListener(const GpuTimingSampleListener& listener){
    ScopedLock lock(m_sampleListenerMutex);
    m_sampleListener = listener;

    u64 generation = m_sampleListenerGeneration.load(MemoryOrder::relaxed) + 1u;
    if(generation == 0u)
        generation = 1u;
    m_sampleListenerGeneration.store(generation, MemoryOrder::release);
    m_hasSampleListener.store(listener.valid(), MemoryOrder::release);
}

void GpuTimingRecorder::resetQueries(){
    const bool collectSamples = m_hasSampleListener.load(MemoryOrder::acquire);
    const u64 listenerGeneration = collectSamples
        ? m_sampleListenerGeneration.load(MemoryOrder::acquire)
        : 0u
    ;
    Vector<GpuTimingSample, Alloc::GlobalArena> retiredSamples{ m_arena };
    {
        ScopedLock lock(m_mutex);
        if(collectSamples)
            retirePendingAttributionsLocked(retiredSamples);
        m_accumulators.clear();
        m_queueCompletions.clear();
        m_overlapRecords.clear();
#if !defined(NWB_FINAL)
        m_heldSubmissionCompletion = {};
#endif
        advanceEpoch();
        m_accumulatorsActive = false;
        m_currentFrameIndex = 0u;
    }
    dispatchCompletedSamples(retiredSamples, listenerGeneration);
}

void GpuTimingRecorder::collect(Device& device){
    const bool collectSamples = m_hasSampleListener.load(MemoryOrder::acquire);
    const u64 listenerGeneration = collectSamples
        ? m_sampleListenerGeneration.load(MemoryOrder::acquire)
        : 0u
    ;
    Vector<GpuTimingSample, Alloc::GlobalArena> completedSamples{ m_arena };
    {
        ScopedLock lock(m_mutex);
        collectLocked(device, m_currentFrameIndex, collectSamples ? &completedSamples : nullptr);
    }
    dispatchCompletedSamples(completedSamples, listenerGeneration);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    const bool collectSamples = m_hasSampleListener.load(MemoryOrder::acquire);
    const u64 listenerGeneration = collectSamples
        ? m_sampleListenerGeneration.load(MemoryOrder::acquire)
        : 0u
    ;
    Vector<GpuTimingSample, Alloc::GlobalArena> completedSamples{ m_arena };
    {
        ScopedLock lock(m_mutex);
        collectLocked(device, publishFrameIndex, collectSamples ? &completedSamples : nullptr);
    }
    dispatchCompletedSamples(completedSamples, listenerGeneration);
}

void GpuTimingRecorder::collectLocked(
    Device& device,
    const u64 publishFrameIndex,
    Vector<GpuTimingSample, Alloc::GlobalArena>* const completedSamples
){
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    m_queueCompletions.clear();
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(device, *this, m_epoch, completedSamples);
    m_timing.publishFrame(publishFrameIndex);
}

bool GpuTimingRecorder::submissionCompleted(Device& device, const QueueSubmissionToken& token){
#if !defined(NWB_FINAL)
    if(
        m_heldSubmissionCompletion.queue == token.queue
        && m_heldSubmissionCompletion.value == token.value
        && m_heldSubmissionCompletion.physicalQueueIndex == token.physicalQueueIndex
        && m_heldSubmissionCompletion.deviceGeneration == token.deviceGeneration
    )
        return false;
#endif

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

void GpuTimingRecorder::dispatchCompletedSamples(
    const Vector<GpuTimingSample, Alloc::GlobalArena>& samples,
    const u64 listenerGeneration
){
    if(samples.empty() || listenerGeneration == 0u)
        return;

    for(const GpuTimingSample& sample : samples){
        ScopedLock lock(m_sampleListenerMutex);
        if(
            !m_sampleListener.valid()
            || m_sampleListenerGeneration.load(MemoryOrder::acquire) != listenerGeneration
        )
            return;

        const GpuTimingSampleListener listener = m_sampleListener;
        listener.invoke(listener.context, sample);
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
    return !m_accumulatorsActive || accumulator->materializeRequestedQueries(device);
}

bool GpuTimingRecorder::prepareOverlapMetric(
    const Name& firstScope,
    const Name& secondScope,
    const Name& outputScope
){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!firstScope || !secondScope || !outputScope || firstScope == secondScope)
        return false;

    for(const OverlapRecord& record : m_overlapRecords){
        if(
            record.firstScope == firstScope
            && record.secondScope == secondScope
            && record.outputScope.valid()
        )
            return true;
    }

    const Perf::TimingScopeId output = m_timing.registerScope(outputScope);
    if(!output.valid())
        return false;

    m_overlapRecords.emplace_back(m_arena);
    OverlapRecord& record = m_overlapRecords.back();
    record.firstScope = firstScope;
    record.secondScope = secondScope;
    record.outputScope = output;
    return true;
}

bool GpuTimingRecorder::materializeRequestedQueries(Device& device){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive)
        return true;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it){
        if(!it.value()->materializeRequestedQueries(device))
            return false;
    }
    return true;
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

#if !defined(NWB_FINAL)

bool GpuTimingRecorder::holdSubmissionCompletionForTesting(const QueueSubmissionToken& token){
    if(!token.valid() || !token.hasPhysicalQueueIdentity())
        return false;

    ScopedLock lock(m_mutex);
    if(m_heldSubmissionCompletion.valid()){
        return
            m_heldSubmissionCompletion.queue == token.queue
            && m_heldSubmissionCompletion.value == token.value
            && m_heldSubmissionCompletion.physicalQueueIndex == token.physicalQueueIndex
            && m_heldSubmissionCompletion.deviceGeneration == token.deviceGeneration
        ;
    }

    m_heldSubmissionCompletion = token;
    return true;
}

void GpuTimingRecorder::releaseSubmissionCompletionForTesting(const QueueSubmissionToken& token){
    ScopedLock lock(m_mutex);
    if(
        m_heldSubmissionCompletion.queue == token.queue
        && m_heldSubmissionCompletion.value == token.value
        && m_heldSubmissionCompletion.physicalQueueIndex == token.physicalQueueIndex
        && m_heldSubmissionCompletion.deviceGeneration == token.deviceGeneration
    )
        m_heldSubmissionCompletion = {};
}

#endif

void GpuTimingRecorder::discardFrameResetLocked(){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->discardFrameReset();
}

bool GpuTimingRecorder::beginScope(
    const Name& scopeName,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution,
    GpuTimingScope& outScope
){
    outScope = {};
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive || !scopeName)
        return true;
    if(&commandList.getDevice() != &device)
        return false;

    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(commandListDescription.physicalQueue);
    if(!queueInfo)
        return false;
    if(queueInfo->timestampValidBits == 0u)
        return true;

    GpuTimingSubmissionTicket* const ticket = activeSubmissionTicket();
    NWB_ASSERT_MSG(ticket, NWB_TEXT("GPU timing scopes must be recorded inside a submission ticket"));
    if(!ticket)
        return false;

    const auto found = m_accumulators.find(scopeName);
    if(found == m_accumulators.end())
        return true;

    if(!found.value()->beginQuery(commandList, m_currentFrameIndex, m_epoch, attribution, outScope))
        return false;
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
    if(!beginScope(scopeName, device, commandList, attribution, outScope))
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

void GpuTimingRecorder::recordTimestampRange(
    const Name& scopeName,
    const u64 frameIndex,
    const GpuComparableTimestampRange& range
){
    // collectLocked() holds m_mutex. Keep this deliberately bounded: a rejected packet yields at most one endpoint,
    // and that orphan must not turn a long-running capture into an unbounded correlation cache.
    constexpr u64 s_PendingOverlapFrameRetention = static_cast<u64>(s_MaxFramesInFlight) * s_PendingOverlapRetentionFramesPerInFlightFrame;

    for(OverlapRecord& record : m_overlapRecords){
        if(scopeName != record.firstScope && scopeName != record.secondScope)
            continue;

        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ){
            if(frameIndex > it->frameIndex && frameIndex - it->frameIndex > s_PendingOverlapFrameRetention)
                it = record.pendingFrames.erase(it);
            else
                ++it;
        }

        PendingOverlapFrame* frame = nullptr;
        for(PendingOverlapFrame& candidate : record.pendingFrames){
            if(candidate.frameIndex == frameIndex){
                frame = &candidate;
                break;
            }
        }
        if(!frame){
            record.pendingFrames.emplace_back();
            frame = &record.pendingFrames.back();
            frame->frameIndex = frameIndex;
        }

        if(scopeName == record.firstScope){
            frame->first = range;
            frame->hasFirst = true;
        }
        else{
            frame->second = range;
            frame->hasSecond = true;
        }

        if(!frame->hasFirst || !frame->hasSecond)
            continue;

        u64 overlapTicks = 0u;
        if(TryComputeGpuTimestampOverlap(frame->first, frame->second, overlapTicks)){
            const f64 overlapSeconds = static_cast<f64>(overlapTicks) * frame->first.secondsPerTick;
            m_timing.recordSample(record.outputScope, overlapSeconds, frameIndex);
        }
        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ++it){
            if(&*it == frame){
                record.pendingFrames.erase(it);
                break;
            }
        }
    }
}

void GpuTimingRecorder::syncActiveState(){
    const bool active = (m_enabled && m_timing.enabled()) || m_feedbackCollectionEnabled;
    if(active == m_accumulatorsActive)
        return;

    if(!active){
        advanceEpoch();
        for(OverlapRecord& record : m_overlapRecords)
            record.pendingFrames.clear();
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

