// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "vulkan/backend_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


thread_local GpuTimingSubmissionTicket* GpuTimingRecorder::s_activeSubmissionTicket = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingAccumulator::collect(Device& device, GpuTimingRecorder& recorder, const u32 epoch){
    if(!m_enabled)
        return;

    for(QueryRecord& record : m_queries){
        if(!record.pending || !record.query)
            continue;
        if(!device.pollTimerQuery(record.query.get()))
            continue;

        TimerQueryResult result;
        if(!device.getTimerQueryResult(record.query.get(), result))
            continue;

        if(record.epoch == epoch && record.publishSample){
            recorder.m_timing.recordSample(m_timingScope, result.durationSeconds(), record.frameIndex);
            recorder.recordTimestampRange(
                m_scopeName,
                record.frameIndex,
                GpuTimingRecorder::TimestampRange{ result.beginSeconds, result.endSeconds }
            );
        }
        record.pending = false;
        record.publishSample = true;
    }
}

void GpuTimingAccumulator::recordFrameReset(CommandList& commandList){
    if(!m_enabled)
        return;

    // Retain pending pools until their result is observable instead of erasing a late GPU sample. Pools that are
    // already available this frame are reset on the device timeline, but do not become usable by dynamic-rendering
    // scopes until the caller confirms this command list submitted successfully.
    for(QueryRecord& record : m_queries){
        record.frameResetRecorded = false;
        // A render-pass scope must observe this frame's reset, not merely a reset that happened during an earlier
        // frame. Leave the pool unavailable until confirmFrameReset() observes a successful preamble submission.
        record.deviceReady = false;
        if(!record.query || record.recording || record.pending)
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
        if(m_enabled)
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

GpuTimingScope GpuTimingAccumulator::beginQuery(
    Device& device,
    CommandList& commandList,
    const u64 frameIndex,
    const u32 epoch
){
    if(!m_enabled)
        return {};

    u32 index = findAvailableQuery();
    if(index == Limit<u32>::s_Max){
        if(!commandList.canResetTimerQueryHere())
            return {};

        index = appendQuery(device);
    }
    if(index == Limit<u32>::s_Max)
        return {};

    QueryRecord& record = m_queries[index];
    // beginTimerQuery self-resets the pool on the device timeline when it is called outside a render pass, so an
    // outside-pass scope can grow on demand. Inside a render pass that reset is illegal, so only prewarmed pools that
    // recordFrameReset() made deviceReady are eligible; if validation under-reserved a scope, that sample is skipped
    // rather than creating a query pool while rendering.
    if(!record.deviceReady && !commandList.canResetTimerQueryHere())
        return {};

    record.recording = true;
    record.publishSample = true;
    commandList.beginTimerQuery(record.query.get());
    record.frameIndex = frameIndex;
    record.epoch = epoch;
    ++m_nextReservation;
    if(m_nextReservation == 0u)
        ++m_nextReservation;
    record.reservation = m_nextReservation;
    return GpuTimingScope{ this, record.query.get(), index, record.reservation };
}

bool GpuTimingAccumulator::endQuery(CommandList& commandList, const GpuTimingScope& scope){
    if(!recordQueryEnd(commandList, scope))
        return false;
    return confirmQuery(scope, true);
}

bool GpuTimingAccumulator::recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.query.get() != scope.query || record.reservation != scope.reservation || !record.recording)
        return false;

    commandList.endTimerQuery(record.query.get());
    return true;
}

bool GpuTimingAccumulator::confirmQuery(const GpuTimingScope& scope, const bool publishSample){
    if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.query.get() != scope.query || record.reservation != scope.reservation || !record.recording)
        return false;

    record.recording = false;
    record.pending = true;
    record.publishSample = publishSample;
    return true;
}

void GpuTimingAccumulator::discardQuery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.query.get() != scope.query || record.reservation != scope.reservation)
        return;

    // The command buffer that wrote this pair never reached the device, so its timestamp values cannot become
    // observable. Keep the preamble-established deviceReady state intact: a later scope may still use this pool.
    record.recording = false;
    record.pending = false;
    record.publishSample = true;
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
        if(!m_queries[i].recording && !m_queries[i].pending)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingRecorder::GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing)
    : m_arena(arena)
    , m_timing(timing)
    , m_accumulators(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_overlapRecords(arena)
{}

void GpuTimingRecorder::setQueryCollectionEnabled(const bool enabled){
    ScopedLock lock(m_mutex);
    m_enabled = enabled;
    syncActiveState();
}

void GpuTimingRecorder::resetQueries(){
    ScopedLock lock(m_mutex);
    m_accumulators.clear();
    m_overlapRecords.clear();
    advanceEpoch();
    m_accumulatorsActive = false;
    m_currentFrameIndex = 0u;
}

void GpuTimingRecorder::collect(Device& device){
    ScopedLock lock(m_mutex);
    collectLocked(device, m_currentFrameIndex);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    ScopedLock lock(m_mutex);
    collectLocked(device, publishFrameIndex);
}

void GpuTimingRecorder::collectLocked(Device& device, const u64 publishFrameIndex){
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(device, *this, m_epoch);
    m_timing.publishFrame(publishFrameIndex);
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

void GpuTimingRecorder::discardFrameResetLocked(){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->discardFrameReset();
}

GpuTimingScope GpuTimingRecorder::beginScope(const Name& scopeName, Device& device, CommandList& commandList){
    ScopedLock lock(m_mutex);
    syncActiveState();
    if(!m_accumulatorsActive || !scopeName)
        return {};

    GpuTimingSubmissionTicket* const ticket = activeSubmissionTicket();
    NWB_ASSERT_MSG(ticket, NWB_TEXT("GPU timing scopes must be recorded inside a submission ticket"));
    if(!ticket)
        return {};

    GpuTimingAccumulator* accumulator = nullptr;
    auto found = m_accumulators.find(scopeName);
    if(found != m_accumulators.end())
        accumulator = found.value().get();
    else{
        if(!commandList.canResetTimerQueryHere())
            return {};

        accumulator = findOrCreateAccumulator(scopeName);
    }
    if(!accumulator)
        return {};

    GpuTimingScope scope = accumulator->beginQuery(device, commandList, m_currentFrameIndex, m_epoch);
    scope.submissionTicket = ticket;
    return scope;
}

GpuTimingScope GpuTimingRecorder::beginDeferredScope(const Name& scopeName, Device& device, CommandList& commandList){
    GpuTimingScope scope = beginScope(scopeName, device, commandList);
    // The frame transaction owns this reservation until the end packet is accepted. The begin packet's submission
    // ticket deliberately has no rollback handle for it: a later recovery endpoint may be required after that begin
    // has already executed on the device timeline.
    scope.submissionTicket = nullptr;
    return scope;
}

void GpuTimingRecorder::endScope(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    bool ended = false;
    {
        // Do not retain this lock while trackScope() takes the ticket lock: discard() rolls tickets back in the
        // opposite direction (ticket first, recorder second).
        ScopedLock lock(m_mutex);
        ended = scope.accumulator->endQuery(commandList, scope);
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
    return scope.accumulator->recordQueryEnd(commandList, scope);
}

bool GpuTimingRecorder::confirmDeferredScope(const GpuTimingScope& scope, const bool publishSample){
    if(!scope.valid())
        return true;

    ScopedLock lock(m_mutex);
    return scope.accumulator->confirmQuery(scope, publishSample);
}

void GpuTimingRecorder::discardScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    scope.accumulator->discardQuery(scope);
}

GpuTimingSubmissionTicket* GpuTimingRecorder::activeSubmissionTicket()const{
    GpuTimingSubmissionTicket* ticket = s_activeSubmissionTicket;
    return ticket && &ticket->m_recorder == this ? ticket : nullptr;
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
    const TimestampRange& range
){
    // collectLocked() holds m_mutex. Keep this deliberately bounded: a rejected packet yields at most one endpoint,
    // and that orphan must not turn a long-running capture into an unbounded correlation cache.
    constexpr u64 s_PendingOverlapFrameRetention = static_cast<u64>(s_MaxFramesInFlight) * 8u;

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

        const f64 beginSeconds = Max(frame->first.beginSeconds, frame->second.beginSeconds);
        const f64 endSeconds = Min(frame->first.endSeconds, frame->second.endSeconds);
        m_timing.recordSample(record.outputScope, Max(0.0, endSeconds - beginSeconds), frameIndex);
        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ++it){
            if(&*it == frame){
                record.pendingFrames.erase(it);
                break;
            }
        }
    }
}

void GpuTimingRecorder::syncActiveState(){
    const bool active = m_enabled && m_timing.enabled();
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


GpuTimingSubmissionTicket::RecordingScope::RecordingScope(GpuTimingSubmissionTicket& ticket)
    : m_ticket(ticket)
    , m_previousTicket(m_ticket.activateOnCurrentThread())
{}

GpuTimingSubmissionTicket::RecordingScope::~RecordingScope(){
    m_ticket.deactivateOnCurrentThread(m_previousTicket);
}


GpuTimingSubmissionTicket::GpuTimingSubmissionTicket(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
    , m_scopes(recorder.m_arena)
{}

GpuTimingSubmissionTicket::~GpuTimingSubmissionTicket(){
    discard();
}

bool GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const CommandQueue::Enum executionQueue
){
    {
        ScopedLock lock(m_mutex);
        NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket submitted while command recording is still active"));
        if(m_resolved || m_recordingScopeCount != 0u)
            return false;
    }

    if(!commandLists || commandListCount == 0u){
        discard();
        return false;
    }

    // Queue::submit omits command lists whose buffer is absent. Reject that condition before submission rather than
    // allowing a producer from a split timing scope to execute without the consumer that contains its end timestamp.
    for(usize i = 0u; i < commandListCount; ++i){
        if(!commandLists[i] || !commandLists[i]->hasCommandBuffer()){
            discard();
            return false;
        }
    }

    bool submitted = false;
    device.executeCommandLists(commandLists, commandListCount, executionQueue, &submitted);
    if(submitted)
        confirm();
    else
        discard();
    return submitted;
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const RenderLane::Enum executionLane,
    const QueueSubmissionDesc& submitDesc
){
    {
        ScopedLock lock(m_mutex);
        NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket submitted while command recording is still active"));
        if(m_resolved || m_recordingScopeCount != 0u)
            return {};
    }

    if(!commandLists || commandListCount == 0u){
        discard();
        return {};
    }

    for(usize i = 0u; i < commandListCount; ++i){
        if(!commandLists[i] || !commandLists[i]->hasCommandBuffer()){
            discard();
            return {};
        }
    }

    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        commandListCount,
        executionLane,
        submitDesc
    );
    if(token.valid())
        confirm();
    else
        discard();
    return token;
}

void GpuTimingSubmissionTicket::discard(){
    ScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket discarded while command recording is still active"));
    if(m_recordingScopeCount != 0u)
        return;

    for(const GpuTimingScope& scope : m_scopes)
        m_recorder.discardScope(scope);
    m_scopes.clear();
    m_resolved = true;
}

void GpuTimingSubmissionTicket::trackScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved, NWB_TEXT("GPU timing scope ended after its submission ticket was resolved"));
    if(m_resolved){
        m_recorder.discardScope(scope);
        return;
    }

    m_scopes.push_back(scope);
}

GpuTimingSubmissionTicket* GpuTimingSubmissionTicket::activateOnCurrentThread(){
    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved, NWB_TEXT("GPU timing submission ticket activated after it was resolved"));
    if(m_resolved)
        return GpuTimingRecorder::s_activeSubmissionTicket;

    GpuTimingSubmissionTicket* previousTicket = GpuTimingRecorder::s_activeSubmissionTicket;
    GpuTimingRecorder::s_activeSubmissionTicket = this;
    ++m_recordingScopeCount;
    return previousTicket;
}

void GpuTimingSubmissionTicket::deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket){
    NWB_ASSERT_MSG(GpuTimingRecorder::s_activeSubmissionTicket == this, NWB_TEXT("GPU timing submission ticket recording scope closed out of order"));
    if(GpuTimingRecorder::s_activeSubmissionTicket != this)
        return;

    GpuTimingRecorder::s_activeSubmissionTicket = previousTicket;
    ScopedLock lock(m_mutex);
    NWB_ASSERT(m_recordingScopeCount > 0u);
    if(m_recordingScopeCount > 0u)
        --m_recordingScopeCount;
}

void GpuTimingSubmissionTicket::confirm(){
    ScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket confirmed while command recording is still active"));
    if(m_recordingScopeCount != 0u)
        return;

    // endQuery() already marked these records pending so no later scope can reuse them. Successful submission means
    // collect() now owns retirement; clearing the ticket only drops its rollback handles.
    m_scopes.clear();
    m_resolved = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingFrameTransaction::GpuTimingFrameTransaction(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
{}

GpuTimingFrameTransaction::~GpuTimingFrameTransaction(){
    discard();
}

bool GpuTimingFrameTransaction::begin(
    const GpuTimingScopeDefinition& scopeDefinition,
    Device& device,
    CommandList& commandList
){
    if(m_state != State::Idle)
        return false;
    if(!scopeDefinition.valid()){
        m_state = State::Inactive;
        return true;
    }

    m_scope = m_recorder.beginDeferredScope(scopeDefinition.identity, device, commandList);
    m_state = m_scope.valid() ? State::BeginRecorded : State::Inactive;
    return true;
}

bool GpuTimingFrameTransaction::recordEnd(CommandList& commandList){
    if(m_state == State::Inactive)
        return true;
    if(
        m_state != State::BeginRecorded
        && m_state != State::BeginAccepted
    )
        return false;

    if(!m_recorder.recordDeferredScopeEnd(commandList, m_scope))
        return false;
    m_state = State::EndRecorded;
    return true;
}

void GpuTimingFrameTransaction::confirmBeginSubmission(){
    if(m_state == State::Inactive || m_state == State::Resolved)
        return;

    NWB_ASSERT(m_state == State::BeginRecorded || m_state == State::EndRecorded);
    if(m_state != State::BeginRecorded && m_state != State::EndRecorded)
        return;

    m_beginAccepted = true;
    if(m_state == State::BeginRecorded)
        m_state = State::BeginAccepted;
}

bool GpuTimingFrameTransaction::confirmEndSubmission(const bool publishSample){
    if(m_state == State::Inactive || m_state == State::Resolved)
        return true;
    if(!m_beginAccepted || m_state != State::EndRecorded)
        return false;
    if(!m_recorder.confirmDeferredScope(m_scope, publishSample))
        return false;

    m_scope = {};
    m_state = State::Resolved;
    return true;
}

bool GpuTimingFrameTransaction::needsRetirement()const{
    return m_beginAccepted && m_scope.valid();
}

void GpuTimingFrameTransaction::prepareForRecovery(){
    if(!needsRetirement())
        return;

    // The old endpoint lives only in a packet that will not reach Vulkan, so it is safe for the recovery command
    // list to overwrite the timestamp query's end slot. The begin reservation remains held throughout.
    if(m_state == State::EndRecorded)
        m_state = State::BeginAccepted;
}

void GpuTimingFrameTransaction::discard(){
    if(m_scope.valid())
        m_recorder.discardScope(m_scope);
    m_scope = {};
    if(m_state != State::Inactive)
        m_state = State::Resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingMeasure::GpuTimingMeasure(
    GpuTimingRecorder& recorder,
    const GpuTimingScopeDefinition& scopeDefinition,
    Device& device,
    CommandList& commandList
)
    : m_recorder(recorder)
    , m_commandList(commandList)
{
    // For a normal one-command-list scope, the marker brackets the whole timing range. A split scope closes the
    // marker before its producer list closes and writes its ending timestamp on the ordered consumer list. Scope
    // identity is retained separately for timing aggregation; the marker keeps the authored text so release
    // diagnostics never receive a Name hash in place of the original label.
    if(!scopeDefinition.valid()){
        NWB_ASSERT(!scopeDefinition.identity && scopeDefinition.markerLabel.empty());
        return;
    }

    m_commandList.beginMarker(scopeDefinition.markerLabel);
    m_markerOpen = true;
    m_scope = m_recorder.beginScope(scopeDefinition.identity, device, commandList);
}
GpuTimingMeasure::~GpuTimingMeasure(){
    finishTiming(m_commandList);
    finishMarker();
}

void GpuTimingMeasure::finishMarker(){
    if(m_markerOpen)
        m_commandList.endMarker();
    m_markerOpen = false;
}

void GpuTimingMeasure::finishTiming(CommandList& commandList){
    m_recorder.endScope(commandList, m_scope);
    m_scope = {};
}

void GpuTimingMeasure::discardTiming(){
    m_recorder.discardScope(m_scope);
    m_scope = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

