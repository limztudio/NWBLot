// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "memory.h"

#include <global/named_registry.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MemorySnapshot MakeMemorySnapshot(const Name& scopeName, const u64 frameIndex, const ::ArenaMemoryStats& stats, const MemorySource::Enum source)noexcept{
    MemorySnapshot snapshot;
    snapshot.scopeName = scopeName;
    snapshot.source = source;
    snapshot.frameIndex = frameIndex;
    snapshot.reservedBytes = stats.reservedBytes;
    snapshot.usedBytes = stats.usedBytes;
    snapshot.peakUsedBytes = stats.peakUsedBytes;
    snapshot.allocationCount = stats.allocationCount;
    snapshot.reallocationCount = stats.reallocationCount;
    snapshot.deallocationCount = stats.deallocationCount;
    return snapshot;
}

MemoryDelta Difference(const MemorySnapshot& current, const MemorySnapshot& previous)noexcept{
    if(!current.valid() || !previous.valid() || current.scopeName != previous.scopeName || current.source != previous.source)
        return {};

    const auto reservedBytes = SignedDifference(current.reservedBytes, previous.reservedBytes);
    const auto usedBytes = SignedDifference(current.usedBytes, previous.usedBytes);
    const auto peakUsedBytes = SignedDifference(current.peakUsedBytes, previous.peakUsedBytes);
    const auto allocationCount = SignedDifference(current.allocationCount, previous.allocationCount);
    const auto reallocationCount = SignedDifference(current.reallocationCount, previous.reallocationCount);
    const auto deallocationCount = SignedDifference(current.deallocationCount, previous.deallocationCount);
    if(!reservedBytes || !usedBytes || !peakUsedBytes || !allocationCount || !reallocationCount || !deallocationCount)
        return {};

    MemoryDelta delta;
    delta.previousFrameIndex = previous.frameIndex;
    delta.currentFrameIndex = current.frameIndex;
    delta.reservedBytes = *reservedBytes;
    delta.usedBytes = *usedBytes;
    delta.peakUsedBytes = *peakUsedBytes;
    delta.allocationCount = *allocationCount;
    delta.reallocationCount = *reallocationCount;
    delta.deallocationCount = *deallocationCount;
    delta.hasSamples = true;
    return delta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MemoryRecorder::setEnabled(const b8 enabled)noexcept{
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

void MemoryRecorder::clear()noexcept{
    for(ScopeRecordPtr& scope : m_scopes){
        scope->previousSnapshot = MemorySnapshot{};
        scope->lastSnapshot = MemorySnapshot{};
        scope->lastDelta = MemoryDelta{};
    }
    m_emptySnapshot = MemorySnapshot{};
    m_emptyDelta = MemoryDelta{};
}

Expected<MemoryScopeId> MemoryRecorder::registerScope(const Name& scopeName, const MemorySource::Enum source){
    if(!scopeName || source > MemorySource::HeapBacking)
        return MakeUnexpected(Failure{});
    const ScopeKey key{ scopeName, source };
    const auto found = m_scopeMap.find(key);
    if(found != m_scopeMap.end())
        return found.value();
    if(m_scopes.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    ScopeRecordPtr record = MakeGlobalUnique<ScopeRecord>(m_arena, scopeName, source);
    if(!record)
        return MakeUnexpected(Failure{});
    const MemoryScopeId scope{ static_cast<u32>(m_scopes.size()), m_generation };
    record->generation = scope.generation;
    ContainerDetail::ReserveGrowingCapacity(m_scopes, m_scopes.size() + 1u);
    m_scopes.push_back(Move(record));
    ScopeExit discardScope([&]()noexcept{ m_scopes.pop_back(); });
    if(!m_scopeMap.try_emplace(key, scope).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    discardScope.release();
    return scope;
}

void MemoryRecorder::recordSnapshot(const MemoryScopeId scope, const ::ArenaMemoryStats& stats, const u64 frameIndex)noexcept{
    if(!m_enabled)
        return;

    ScopeRecord* record = findScope(scope);
    if(!record)
        return;

    const MemorySnapshot snapshot = MakeMemorySnapshot(record->name, frameIndex, stats, record->source);
    if(!record->lastSnapshot.valid() || record->lastSnapshot.frameIndex != frameIndex)
        record->previousSnapshot = record->lastSnapshot;
    record->lastSnapshot = snapshot;
    record->lastDelta = Difference(record->lastSnapshot, record->previousSnapshot);
}

void MemoryRecorder::recordSnapshot(const Name& scopeName, const ::ArenaMemoryStats& stats, const u64 frameIndex, const MemorySource::Enum source)noexcept{
    if(!m_enabled || !scopeName)
        return;

    // Record paths only consume scopes registered during setup; scope creation belongs to registerScope callers, never to per-frame record calls.
    const auto found = m_scopeMap.find(ScopeKey{ scopeName, source });
    if(found == m_scopeMap.end())
        return;

    recordSnapshot(found.value(), stats, frameIndex);
}

const MemorySnapshot& MemoryRecorder::snapshot(const Name& scopeName, const MemorySource::Enum source)const noexcept{
    const auto found = m_scopeMap.find(ScopeKey{ scopeName, source });
    if(found == m_scopeMap.end())
        return m_emptySnapshot;

    return snapshot(found.value());
}

const MemorySnapshot& MemoryRecorder::snapshot(const MemoryScopeId scope)const noexcept{
    const ScopeRecord* record = findScope(scope);
    if(!record)
        return m_emptySnapshot;

    return record->lastSnapshot;
}

const MemoryDelta& MemoryRecorder::delta(const Name& scopeName, const MemorySource::Enum source)const noexcept{
    const auto found = m_scopeMap.find(ScopeKey{ scopeName, source });
    if(found == m_scopeMap.end())
        return m_emptyDelta;

    return delta(found.value());
}

const MemoryDelta& MemoryRecorder::delta(const MemoryScopeId scope)const noexcept{
    const ScopeRecord* record = findScope(scope);
    if(!record)
        return m_emptyDelta;

    return record->lastDelta;
}

MemoryScopeId MemoryRecorder::scopeAt(const usize index)const noexcept{
    return ScopeAt<MemoryScopeId>(Span<const ScopeRecordPtr>(m_scopes), index);
}

Name MemoryRecorder::scopeNameAt(const usize index)const noexcept{
    return ScopeNameAt(Span<const ScopeRecordPtr>(m_scopes), index);
}

const MemorySnapshot& MemoryRecorder::snapshotAt(const usize index)const noexcept{
    if(index >= m_scopes.size())
        return m_emptySnapshot;

    NWB_ASSERT(m_scopes[index]);
    return m_scopes[index]->lastSnapshot;
}

const MemoryDelta& MemoryRecorder::deltaAt(const usize index)const noexcept{
    if(index >= m_scopes.size())
        return m_emptyDelta;

    NWB_ASSERT(m_scopes[index]);
    return m_scopes[index]->lastDelta;
}

MemoryRecorder::ScopeRecord* MemoryRecorder::findScope(const MemoryScopeId scope)const noexcept{
    return FindNamedScope<MemoryScopeId>(Span<const ScopeRecordPtr>(m_scopes), scope);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MemorySnapshot& MemoryView::snapshot(const Name& scopeName, const MemorySource::Enum source)const noexcept{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshot(scopeName, source) : s_EmptySnapshot;
}

const MemorySnapshot& MemoryView::snapshot(const MemoryScopeId scope)const noexcept{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshot(scope) : s_EmptySnapshot;
}

const MemoryDelta& MemoryView::delta(const Name& scopeName, const MemorySource::Enum source)const noexcept{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->delta(scopeName, source) : s_EmptyDelta;
}

const MemoryDelta& MemoryView::delta(const MemoryScopeId scope)const noexcept{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->delta(scope) : s_EmptyDelta;
}

const MemorySnapshot& MemoryView::snapshotAt(const usize index)const noexcept{
    static const MemorySnapshot s_EmptySnapshot;
    return m_recorder ? m_recorder->snapshotAt(index) : s_EmptySnapshot;
}

const MemoryDelta& MemoryView::deltaAt(const usize index)const noexcept{
    static const MemoryDelta s_EmptyDelta;
    return m_recorder ? m_recorder->deltaAt(index) : s_EmptyDelta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

