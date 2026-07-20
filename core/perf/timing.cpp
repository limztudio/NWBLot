// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "timing.h"
#include <global/named_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TimingAccumulator::clear(){
    m_currentStats = TimingStats{};
    m_lastStats = TimingStats{};
}

void TimingAccumulator::setEnabled(const bool enabled){
    m_enabled = enabled;
    if(!m_enabled)
        clear();
}

void TimingAccumulator::record(const f64 seconds, const u64 sampleFrameIndex){
    if(!m_enabled)
        return;

    if(m_currentStats.sampleCount == 0u){
        m_currentStats.firstSampleFrameIndex = sampleFrameIndex;
        m_currentStats.minSeconds = seconds;
        m_currentStats.maxSeconds = seconds;
    }
    else{
        if(seconds < m_currentStats.minSeconds)
            m_currentStats.minSeconds = seconds;
        if(seconds > m_currentStats.maxSeconds)
            m_currentStats.maxSeconds = seconds;
    }
    m_currentStats.lastSampleFrameIndex = sampleFrameIndex;
    m_currentStats.lastSeconds = seconds;
    m_currentStats.seconds += seconds;
    ++m_currentStats.sampleCount;
}

void TimingAccumulator::publish(const u64 publishFrameIndex){
    m_currentStats.publishFrameIndex = publishFrameIndex;
    m_lastStats = m_currentStats;
    m_currentStats = TimingStats{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TimingRecorder::setEnabled(const bool enabled){
    m_enabled = enabled;
    for(ScopeRecordPtr& scope : m_scopes)
        scope->accumulator.setEnabled(enabled);
    if(!m_enabled)
        m_emptyStats = TimingStats{};
}

void TimingRecorder::clear(){
    for(ScopeRecordPtr& scope : m_scopes)
        scope->accumulator.clear();
    m_emptyStats = TimingStats{};
    m_nextPublishFrameIndex = 0u;
}

TimingScopeId TimingRecorder::registerScope(const Name& scopeName){
    return ::RegisterNamedScope<TimingScopeId>(
        m_scopes,
        m_scopeMap,
        m_generation,
        scopeName,
        [&](const Name& name){ return MakeGlobalUnique<ScopeRecord>(m_arena, name); },
        [&](ScopeRecord& scope, const TimingScopeId& scopeId){
            scope.generation = scopeId.generation;
            scope.accumulator.setEnabled(m_enabled);
        }
    );
}

void TimingRecorder::recordSample(const TimingScopeId scope, const f64 seconds, const u64 sampleFrameIndex){
    if(!m_enabled)
        return;

    ScopeRecord* record = findScope(scope);
    if(!record)
        return;

    record->accumulator.record(seconds, sampleFrameIndex);
}

void TimingRecorder::publishFrame(){
    publishFrame(m_nextPublishFrameIndex);
}

void TimingRecorder::publishFrame(const u64 publishFrameIndex){
    if(!m_enabled)
        return;

    for(ScopeRecordPtr& scope : m_scopes)
        scope->accumulator.publish(publishFrameIndex);
    m_nextPublishFrameIndex = Max(m_nextPublishFrameIndex, publishFrameIndex + 1u);
}

const TimingStats& TimingRecorder::stats(const Name& scopeName)const{
    const auto found = m_scopeMap.find(scopeName);
    if(found == m_scopeMap.end())
        return m_emptyStats;

    return stats(found.value());
}

const TimingStats& TimingRecorder::stats(const TimingScopeId scope)const{
    const ScopeRecord* record = findScope(scope);
    if(!record)
        return m_emptyStats;

    return record->accumulator.lastStats();
}

TimingScopeId TimingRecorder::scopeAt(const usize index)const{
    if(index >= m_scopes.size())
        return {};

    const ScopeRecord* record = m_scopes[index].get();
    if(!record)
        return {};

    TimingScopeId scope;
    scope.index = static_cast<u32>(index);
    scope.generation = record->generation;
    return scope;
}

Name TimingRecorder::scopeNameAt(const usize index)const{
    if(index >= m_scopes.size() || !m_scopes[index])
        return NAME_NONE;

    return m_scopes[index]->name;
}

const TimingStats& TimingRecorder::statsAt(const usize index)const{
    if(index >= m_scopes.size() || !m_scopes[index])
        return m_emptyStats;

    return m_scopes[index]->accumulator.lastStats();
}

void TimingRecorder::recordSample(const Name& scopeName, const f64 seconds){
    recordSample(scopeName, seconds, m_nextPublishFrameIndex);
}

void TimingRecorder::recordSample(const Name& scopeName, const f64 seconds, const u64 sampleFrameIndex){
    if(!m_enabled || !scopeName)
        return;

    recordSample(registerScope(scopeName), seconds, sampleFrameIndex);
}

TimingRecorder::ScopeRecord* TimingRecorder::findScope(const TimingScopeId scope)const{
    if(!scope.valid() || scope.index >= m_scopes.size())
        return nullptr;

    ScopeRecord* record = m_scopes[scope.index].get();
    if(!record || record->generation != scope.generation)
        return nullptr;

    return record;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const TimingStats& TimingView::stats(const Name& scopeName)const{
    static const TimingStats s_EmptyStats;
    return m_recorder ? m_recorder->stats(scopeName) : s_EmptyStats;
}

const TimingStats& TimingView::stats(const TimingScopeId scope)const{
    static const TimingStats s_EmptyStats;
    return m_recorder ? m_recorder->stats(scope) : s_EmptyStats;
}

usize TimingView::scopeCount()const{
    return m_recorder ? m_recorder->scopeCount() : 0u;
}

TimingScopeId TimingView::scopeAt(const usize index)const{
    return m_recorder ? m_recorder->scopeAt(index) : TimingScopeId{};
}

Name TimingView::scopeNameAt(const usize index)const{
    return m_recorder ? m_recorder->scopeNameAt(index) : NAME_NONE;
}

const TimingStats& TimingView::statsAt(const usize index)const{
    static const TimingStats s_EmptyStats;
    return m_recorder ? m_recorder->statsAt(index) : s_EmptyStats;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

