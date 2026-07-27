// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingAccumulator::collect(Device& device, Perf::TimingSink& timing, const u32 epoch){
    if(!m_enabled)
        return;

    for(QueryRecord& record : m_queries){
        if(!record.pending || !record.query)
            continue;
        if(!device.pollTimerQuery(record.query.get()))
            continue;

        if(record.epoch == epoch)
            timing.recordSample(m_timingScope, static_cast<f64>(device.getTimerQueryTime(record.query.get())), record.frameIndex);
        record.pending = false;
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
        if(!record.query || record.pending)
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

    commandList.beginTimerQuery(record.query.get());
    record.frameIndex = frameIndex;
    record.epoch = epoch;
    return GpuTimingScope{ this, record.query.get(), index };
}

void GpuTimingAccumulator::endQuery(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.query.get() != scope.query)
        return;

    commandList.endTimerQuery(record.query.get());
    record.pending = true;
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
        if(!m_queries[i].pending)
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
{}

void GpuTimingRecorder::setQueryCollectionEnabled(const bool enabled){
    m_enabled = enabled;
    syncActiveState();
}

void GpuTimingRecorder::resetQueries(){
    m_accumulators.clear();
    advanceEpoch();
    m_accumulatorsActive = false;
    m_currentFrameIndex = 0u;
}

void GpuTimingRecorder::collect(Device& device){
    collect(device, m_currentFrameIndex);
}

void GpuTimingRecorder::collect(Device& device, const u64 publishFrameIndex){
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->collect(device, m_timing, m_epoch);
    m_timing.publishFrame(publishFrameIndex);
}

void GpuTimingRecorder::beginFrame(const u64 frameIndex){
    m_currentFrameIndex = frameIndex;
}

bool GpuTimingRecorder::prepareScopeQueries(const Name& scopeName, Device* device, const u32 queryCount){
    syncActiveState();
    if(!scopeName || !device)
        return false;

    GpuTimingAccumulator* accumulator = findOrCreateAccumulator(scopeName);
    if(!accumulator)
        return false;

    accumulator->requestQueries(queryCount);
    return !m_accumulatorsActive || accumulator->materializeRequestedQueries(*device);
}

bool GpuTimingRecorder::materializeRequestedQueries(Device& device){
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
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->recordFrameReset(commandList);
}

void GpuTimingRecorder::confirmFrameReset(){
    syncActiveState();
    if(!m_accumulatorsActive){
        discardFrameReset();
        return;
    }

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->confirmFrameReset();
}

void GpuTimingRecorder::discardFrameReset(){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->discardFrameReset();
}

GpuTimingScope GpuTimingRecorder::beginScope(const Name& scopeName, Device* device, CommandList& commandList){
    syncActiveState();
    if(!m_accumulatorsActive || !scopeName || !device)
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

    return accumulator->beginQuery(*device, commandList, m_currentFrameIndex, m_epoch);
}

void GpuTimingRecorder::endScope(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    scope.accumulator->endQuery(commandList, scope);
}

GpuTimingAccumulator* GpuTimingRecorder::findOrCreateAccumulator(const Name& scopeName){
    auto found = m_accumulators.find(scopeName);
    if(found != m_accumulators.end())
        return found.value().get();

    const Perf::TimingScopeId timingScope = m_timing.registerScope(scopeName);
    if(!timingScope.valid())
        return nullptr;

    AccumulatorPtr accumulator = MakeGlobalUnique<GpuTimingAccumulator>(m_arena, m_arena, timingScope);
    if(!accumulator)
        return nullptr;

    auto [it, inserted] = m_accumulators.try_emplace(scopeName, Move(accumulator));
    if(!inserted)
        return it.value().get();

    it.value()->setEnabled(m_accumulatorsActive);
    return it.value().get();
}

void GpuTimingRecorder::syncActiveState(){
    const bool active = m_enabled && m_timing.enabled();
    if(active == m_accumulatorsActive)
        return;

    if(!active)
        advanceEpoch();
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


GpuTimingMeasure::GpuTimingMeasure(
    GpuTimingRecorder& recorder,
    const GpuTimingScopeDefinition& scopeDefinition,
    Device* device,
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
    m_scope = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

