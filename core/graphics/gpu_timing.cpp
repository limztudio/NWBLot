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

    // Reset every pool this accumulator owns on the device timeline. A pool with a still-pending result would be
    // clobbered, but collect() polls and clears every pending record before this runs each frame, so only settled
    // (or never-used) pools are reset here. Recording the reset here also marks the pool deviceReady, which gates
    // its first timestamp write: a pool created mid-frame (inside a render pass) cannot be device-reset until the
    // NEXT frame open, so its first write waits until then rather than tripping the validator on a host-only reset.
    for(QueryRecord& record : m_queries){
        if(record.query){
            commandList.resetTimerQuery(record.query.get());
            record.deviceReady = true;
        }
    }
}

GpuTimingScope GpuTimingAccumulator::beginQuery(
    Device& device,
    CommandList& commandList,
    const u64 frameIndex,
    const u32 epoch
){
    if(!m_enabled)
        return {};

    const u32 index = acquireQuery(device);
    if(index == Limit<u32>::s_Max)
        return {};

    QueryRecord& record = m_queries[index];
    // beginTimerQuery self-resets the pool on the device timeline when it is called outside a render pass, so an
    // outside-pass scope is always safe. Inside a render pass that reset is illegal, so a pool that has not yet
    // been device-reset at a frame open (a brand-new pool first acquired inside a render pass) must not be written
    // this frame -- doing so would trip VUID-vkCmdWriteTimestamp-None-00830. Defer its first use one frame, until
    // recordFrameReset() has made it deviceReady; the skipped scope simply reports no sample that one frame, a
    // negligible startup/growth gap the interval averaging absorbs.
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

u32 GpuTimingAccumulator::acquireQuery(Device& device){
    for(usize i = 0u; i < m_queries.size(); ++i){
        if(!m_queries[i].pending)
            return static_cast<u32>(i);
    }

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

void GpuTimingRecorder::recordFrameReset(CommandList& commandList){
    syncActiveState();
    if(!m_accumulatorsActive)
        return;

    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->recordFrameReset(commandList);
}

GpuTimingScope GpuTimingRecorder::beginScope(const Name& scopeName, Device* device, CommandList& commandList){
    syncActiveState();
    if(!m_accumulatorsActive || !scopeName || !device)
        return {};

    GpuTimingAccumulator* accumulator = findOrCreateAccumulator(scopeName);
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
    const Name& scopeName,
    Device* device,
    CommandList& commandList
)
    : m_recorder(recorder)
    , m_commandList(commandList)
{
    // The marker brackets the whole scope: it opens before the begin timestamp and closes after the end
    // timestamp (see dtor), so a GPU crash anywhere inside the pass resolves to this scope. The marker label
    // is the scope Name itself (its readable string under NWB_DEBUG, its hash string otherwise; a future
    // NamePool decodes that hash back to text in release). beginMarker self-gates on the active marker
    // backends, so it is a cheap no-op when none are enabled, and fires independently of timer-query state.
    m_commandList.beginMarker(scopeName.c_str());
    m_scope = m_recorder.beginScope(scopeName, device, commandList);
}
GpuTimingMeasure::~GpuTimingMeasure(){
    m_recorder.endScope(m_commandList, m_scope);
    m_commandList.endMarker();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

