// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "memory.h"
#include <global/named_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MemoryRecorder::setEnabled(const bool enabled){
    m_enabled = enabled;
    if(m_enabled)
        return;

    for(ScopeRecordPtr& scope : m_scopes){
        scope->previousSnapshot = MemorySnapshot{};
        scope->lastSnapshot = MemorySnapshot{};
        scope->lastDelta = MemoryDelta{};
    }
    m_emptySnapshot = MemorySnapshot{};
    m_emptyDelta = MemoryDelta{};
}

void MemoryRecorder::clear(){
    for(ScopeRecordPtr& scope : m_scopes){
        scope->previousSnapshot = MemorySnapshot{};
        scope->lastSnapshot = MemorySnapshot{};
        scope->lastDelta = MemoryDelta{};
    }
    m_emptySnapshot = MemorySnapshot{};
    m_emptyDelta = MemoryDelta{};
}

MemoryScopeId MemoryRecorder::registerScope(const Name& scopeName){
    return ::RegisterNamedScope<MemoryScopeId>(
        m_scopes,
        m_scopeMap,
        m_generation,
        scopeName,
        [&](const Name& name){ return MakeGlobalUnique<ScopeRecord>(m_arena, name); },
        [](ScopeRecord& scope, const MemoryScopeId& scopeId){ scope.generation = scopeId.generation; }
    );
}

void MemoryRecorder::recordSnapshot(
    const MemoryScopeId scope,
    const Alloc::ArenaMemoryStats& stats,
    const u64 frameIndex
){
    if(!m_enabled)
        return;

    ScopeRecord* record = findScope(scope);
    if(!record)
        return;

    const MemorySnapshot snapshot = MakeMemorySnapshot(record->name, frameIndex, stats);
    record->previousSnapshot = record->lastSnapshot;
    record->lastSnapshot = snapshot;
    record->lastDelta = Difference(record->lastSnapshot, record->previousSnapshot);
}

void MemoryRecorder::recordSnapshot(
    const Name& scopeName,
    const Alloc::ArenaMemoryStats& stats,
    const u64 frameIndex
){
    if(!m_enabled || !scopeName)
        return;

    recordSnapshot(registerScope(scopeName), stats, frameIndex);
}

const MemorySnapshot& MemoryRecorder::snapshot(const Name& scopeName)const{
    const auto found = m_scopeMap.find(scopeName);
    if(found == m_scopeMap.end())
        return m_emptySnapshot;

    return snapshot(found.value());
}

const MemorySnapshot& MemoryRecorder::snapshot(const MemoryScopeId scope)const{
    const ScopeRecord* record = findScope(scope);
    if(!record)
        return m_emptySnapshot;

    return record->lastSnapshot;
}

const MemoryDelta& MemoryRecorder::delta(const Name& scopeName)const{
    const auto found = m_scopeMap.find(scopeName);
    if(found == m_scopeMap.end())
        return m_emptyDelta;

    return delta(found.value());
}

const MemoryDelta& MemoryRecorder::delta(const MemoryScopeId scope)const{
    const ScopeRecord* record = findScope(scope);
    if(!record)
        return m_emptyDelta;

    return record->lastDelta;
}

MemoryScopeId MemoryRecorder::scopeAt(const usize index)const{
    return ::ScopeAt<MemoryScopeId>(m_scopes, index);
}

Name MemoryRecorder::scopeNameAt(const usize index)const{
    return ::ScopeNameAt(m_scopes, index);
}

const MemorySnapshot& MemoryRecorder::snapshotAt(const usize index)const{
    if(index >= m_scopes.size() || !m_scopes[index])
        return m_emptySnapshot;

    return m_scopes[index]->lastSnapshot;
}

const MemoryDelta& MemoryRecorder::deltaAt(const usize index)const{
    if(index >= m_scopes.size() || !m_scopes[index])
        return m_emptyDelta;

    return m_scopes[index]->lastDelta;
}

MemoryRecorder::ScopeRecord* MemoryRecorder::findScope(const MemoryScopeId scope)const{
    return ::FindNamedScope<MemoryScopeId>(m_scopes, scope);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MemorySnapshot& MemoryView::snapshot(const Name& scopeName)const{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshot(scopeName) : s_EmptySnapshot;
}

const MemorySnapshot& MemoryView::snapshot(const MemoryScopeId scope)const{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshot(scope) : s_EmptySnapshot;
}

const MemoryDelta& MemoryView::delta(const Name& scopeName)const{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->delta(scopeName) : s_EmptyDelta;
}

const MemoryDelta& MemoryView::delta(const MemoryScopeId scope)const{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->delta(scope) : s_EmptyDelta;
}

usize MemoryView::scopeCount()const{
    return m_recorder ? m_recorder->scopeCount() : 0u;
}

MemoryScopeId MemoryView::scopeAt(const usize index)const{
    return m_recorder ? m_recorder->scopeAt(index) : MemoryScopeId{};
}

Name MemoryView::scopeNameAt(const usize index)const{
    return m_recorder ? m_recorder->scopeNameAt(index) : NAME_NONE;
}

const MemorySnapshot& MemoryView::snapshotAt(const usize index)const{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshotAt(index) : s_EmptySnapshot;
}

const MemoryDelta& MemoryView::deltaAt(const usize index)const{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->deltaAt(index) : s_EmptyDelta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

