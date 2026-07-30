// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryRecorder;
class MemoryView;

struct MemoryScopeId{
    u32 index = Limit<u32>::s_Max;
    u32 generation = 0u;

    [[nodiscard]] bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};

struct MemorySnapshot{
    Name scopeName = NAME_NONE;
    u64 frameIndex = 0u;
    u64 reservedBytes = 0u;
    u64 usedBytes = 0u;
    u64 peakUsedBytes = 0u;
    u64 allocationCount = 0u;
    u64 reallocationCount = 0u;
    u64 deallocationCount = 0u;

    [[nodiscard]] bool valid()const{ return scopeName != NAME_NONE; }
};

struct MemoryDelta{
    u64 previousFrameIndex = 0u;
    u64 currentFrameIndex = 0u;
    i64 reservedBytes = 0;
    i64 usedBytes = 0;
    i64 peakUsedBytes = 0;
    i64 allocationCount = 0;
    i64 reallocationCount = 0;
    i64 deallocationCount = 0;
    bool hasSamples = false;

    [[nodiscard]] bool valid()const{ return hasSamples; }
};

[[nodiscard]] inline MemorySnapshot MakeMemorySnapshot(
    const Name& scopeName,
    const u64 frameIndex,
    const Alloc::ArenaMemoryStats& stats
){
    MemorySnapshot snapshot;
    snapshot.scopeName = scopeName;
    snapshot.frameIndex = frameIndex;
    snapshot.reservedBytes = stats.reservedBytes;
    snapshot.usedBytes = stats.usedBytes;
    snapshot.peakUsedBytes = stats.peakUsedBytes;
    snapshot.allocationCount = stats.allocationCount;
    snapshot.reallocationCount = stats.reallocationCount;
    snapshot.deallocationCount = stats.deallocationCount;
    return snapshot;
}

template<typename Arena>
[[nodiscard]] inline MemorySnapshot MakeMemorySnapshot(const Name& scopeName, const u64 frameIndex, const Arena& arena){
    return MakeMemorySnapshot(scopeName, frameIndex, arena.memoryStats());
}

[[nodiscard]] inline MemoryDelta Difference(const MemorySnapshot& current, const MemorySnapshot& previous){
    if(!current.valid() || !previous.valid())
        return {};

    MemoryDelta delta;
    delta.previousFrameIndex = previous.frameIndex;
    delta.currentFrameIndex = current.frameIndex;
    delta.reservedBytes = static_cast<i64>(current.reservedBytes) - static_cast<i64>(previous.reservedBytes);
    delta.usedBytes = static_cast<i64>(current.usedBytes) - static_cast<i64>(previous.usedBytes);
    delta.peakUsedBytes = static_cast<i64>(current.peakUsedBytes) - static_cast<i64>(previous.peakUsedBytes);
    delta.allocationCount = static_cast<i64>(current.allocationCount) - static_cast<i64>(previous.allocationCount);
    delta.reallocationCount = static_cast<i64>(current.reallocationCount) - static_cast<i64>(previous.reallocationCount);
    delta.deallocationCount = static_cast<i64>(current.deallocationCount) - static_cast<i64>(previous.deallocationCount);
    delta.hasSamples = true;
    return delta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryRecorder final : NoCopy{
private:
    struct ScopeRecord : NoCopy{
        explicit ScopeRecord(const Name& scopeName)
            : name(scopeName)
        {}

        Name name = NAME_NONE;
        MemorySnapshot previousSnapshot;
        MemorySnapshot lastSnapshot;
        MemoryDelta lastDelta;
        u32 generation = 0u;
    };

    using ScopeRecordPtr = GlobalUniquePtr<ScopeRecord>;
    using ScopeVector = Vector<ScopeRecordPtr, Alloc::GlobalArena>;
    using ScopeMap = HashMap<Name, MemoryScopeId, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit MemoryRecorder(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_scopes(arena)
        , m_scopeMap(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    void setEnabled(bool enabled);
    void clear();
    [[nodiscard]] MemoryScopeId registerScope(const Name& scopeName);
    void recordSnapshot(MemoryScopeId scope, const Alloc::ArenaMemoryStats& stats, u64 frameIndex);
    void recordSnapshot(const Name& scopeName, const Alloc::ArenaMemoryStats& stats, u64 frameIndex);

    template<typename Arena>
    void recordArenaSnapshot(const MemoryScopeId scope, const Arena& arena, const u64 frameIndex){
        recordSnapshot(scope, arena.memoryStats(), frameIndex);
    }

    template<typename Arena>
    void recordArenaSnapshot(const Name& scopeName, const Arena& arena, const u64 frameIndex){
        recordSnapshot(scopeName, arena.memoryStats(), frameIndex);
    }

    [[nodiscard]] const MemorySnapshot& snapshot(const Name& scopeName)const;
    [[nodiscard]] const MemorySnapshot& snapshot(MemoryScopeId scope)const;
    [[nodiscard]] const MemoryDelta& delta(const Name& scopeName)const;
    [[nodiscard]] const MemoryDelta& delta(MemoryScopeId scope)const;
    [[nodiscard]] usize scopeCount()const{ return m_scopes.size(); }
    [[nodiscard]] MemoryScopeId scopeAt(usize index)const;
    [[nodiscard]] Name scopeNameAt(usize index)const;
    [[nodiscard]] const MemorySnapshot& snapshotAt(usize index)const;
    [[nodiscard]] const MemoryDelta& deltaAt(usize index)const;


private:
    [[nodiscard]] ScopeRecord* findScope(MemoryScopeId scope)const;


private:
    Alloc::GlobalArena& m_arena;
    ScopeVector m_scopes;
    ScopeMap m_scopeMap;
    MemorySnapshot m_emptySnapshot;
    MemoryDelta m_emptyDelta;
    u32 m_generation = 1u;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryView final{
public:
    MemoryView() = default;
    explicit MemoryView(const MemoryRecorder& recorder)
        : m_recorder(&recorder)
    {}


public:
    [[nodiscard]] bool valid()const{ return m_recorder != nullptr; }
    [[nodiscard]] const MemorySnapshot& snapshot(const Name& scopeName)const;
    [[nodiscard]] const MemorySnapshot& snapshot(MemoryScopeId scope)const;
    [[nodiscard]] const MemoryDelta& delta(const Name& scopeName)const;
    [[nodiscard]] const MemoryDelta& delta(MemoryScopeId scope)const;
    [[nodiscard]] usize scopeCount()const;
    [[nodiscard]] MemoryScopeId scopeAt(usize index)const;
    [[nodiscard]] Name scopeNameAt(usize index)const;
    [[nodiscard]] const MemorySnapshot& snapshotAt(usize index)const;
    [[nodiscard]] const MemoryDelta& deltaAt(usize index)const;


private:
    const MemoryRecorder* m_recorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

