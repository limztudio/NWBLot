// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/perf/memory_source_types.adhd.h>

#include <core/alloc/module.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MemorySource{
    enum Enum : u8{
        ExplicitScope = NWB::Core::Perf::MemorySourceTypes::MemorySource::ExplicitScope,
        Arena = NWB::Core::Perf::MemorySourceTypes::MemorySource::Arena,
        HeapBacking = NWB::Core::Perf::MemorySourceTypes::MemorySource::HeapBacking,
    };
};


class MemoryRecorder;
class MemoryView;

struct MemoryScopeId{
    u32 index = Limit<u32>::s_Max;
    u32 generation = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};

struct MemorySnapshot{
    Name scopeName = s_NameNone;
    u64 frameIndex = 0u;
    u64 reservedBytes = 0u;
    u64 usedBytes = 0u;
    u64 peakUsedBytes = 0u;
    u64 allocationCount = 0u;
    u64 reallocationCount = 0u;
    u64 deallocationCount = 0u;
    MemorySource::Enum source = MemorySource::ExplicitScope;


public:
    [[nodiscard]] b8 valid()const noexcept{ return scopeName != s_NameNone; }
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
    // False when snapshots are incomparable or any signed counter difference is unrepresentable.
    b8 hasSamples = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return hasSamples; }
};

[[nodiscard]] MemorySnapshot MakeMemorySnapshot(const Name& scopeName, u64 frameIndex, const ::ArenaMemoryStats& stats, MemorySource::Enum source = MemorySource::ExplicitScope)noexcept;

template<typename Arena>
[[nodiscard]] inline MemorySnapshot MakeMemorySnapshot(const Name& scopeName, const u64 frameIndex, const Arena& arena)noexcept(noexcept(MakeMemorySnapshot(scopeName, frameIndex, arena.memoryStats()))){
    return MakeMemorySnapshot(scopeName, frameIndex, arena.memoryStats());
}

[[nodiscard]] MemoryDelta Difference(const MemorySnapshot& current, const MemorySnapshot& previous)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryRecorder final : NoCopy{
private:
    struct ScopeKey{
        Name name = s_NameNone;
        MemorySource::Enum source = MemorySource::ExplicitScope;
    };
    struct ScopeKeyHash{
        usize operator()(const ScopeKey& key)const noexcept{
            return Hasher<Name>{}(key.name) ^ static_cast<usize>(key.source);
        }
    };
    struct ScopeKeyEqual{
        b8 operator()(const ScopeKey& lhs, const ScopeKey& rhs)const noexcept{
            return lhs.name == rhs.name && lhs.source == rhs.source;
        }
    };

    struct ScopeRecord : NoCopy{
        Name name = s_NameNone;
        MemorySnapshot previousSnapshot;
        MemorySnapshot lastSnapshot;
        MemoryDelta lastDelta;
        u32 generation = 0u;
        MemorySource::Enum source = MemorySource::ExplicitScope;


    public:
        explicit ScopeRecord(const Name& scopeName, const MemorySource::Enum memorySource)noexcept
            : name(scopeName)
            , source(memorySource)
        {}
    };


private:
    using ScopeRecordPtr = GlobalUniquePtr<ScopeRecord>;
    using ScopeVector = Vector<ScopeRecordPtr, Alloc::GlobalArena>;
    using ScopeMap = HashMap<ScopeKey, MemoryScopeId, Alloc::GlobalArena, ScopeKeyHash, ScopeKeyEqual>;


public:
    explicit MemoryRecorder(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_scopes(arena)
        , m_scopeMap(0, ScopeKeyHash{}, ScopeKeyEqual{}, arena)
    {}


public:
    void setEnabled(b8 enabled)noexcept;
    void clear()noexcept;
    [[nodiscard]] Expected<MemoryScopeId> registerScope(const Name& scopeName, MemorySource::Enum source = MemorySource::ExplicitScope);
    void recordSnapshot(MemoryScopeId scope, const ::ArenaMemoryStats& stats, u64 frameIndex)noexcept;
    void recordSnapshot(const Name& scopeName, const ::ArenaMemoryStats& stats, u64 frameIndex, MemorySource::Enum source = MemorySource::ExplicitScope)noexcept;

    [[nodiscard]] const MemorySnapshot& snapshot(const Name& scopeName, MemorySource::Enum source = MemorySource::ExplicitScope)const noexcept;
    [[nodiscard]] const MemorySnapshot& snapshot(MemoryScopeId scope)const noexcept;
    [[nodiscard]] const MemoryDelta& delta(const Name& scopeName, MemorySource::Enum source = MemorySource::ExplicitScope)const noexcept;
    [[nodiscard]] const MemoryDelta& delta(MemoryScopeId scope)const noexcept;
    [[nodiscard]] usize scopeCount()const noexcept{ return m_scopes.size(); }
    [[nodiscard]] MemoryScopeId scopeAt(usize index)const noexcept;
    [[nodiscard]] Name scopeNameAt(usize index)const noexcept;
    [[nodiscard]] const MemorySnapshot& snapshotAt(usize index)const noexcept;
    [[nodiscard]] const MemoryDelta& deltaAt(usize index)const noexcept;


private:
    [[nodiscard]] ScopeRecord* findScope(MemoryScopeId scope)const noexcept;


private:
    Alloc::GlobalArena& m_arena;
    ScopeVector m_scopes;
    ScopeMap m_scopeMap;
    MemorySnapshot m_emptySnapshot;
    MemoryDelta m_emptyDelta;
    u32 m_generation = 1u;
    b8 m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryView final{
public:
    MemoryView()noexcept = default;
    explicit MemoryView(const MemoryRecorder& recorder)noexcept
        : m_recorder(&recorder)
    {}


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_recorder != nullptr; }
    [[nodiscard]] const MemorySnapshot& snapshot(const Name& scopeName, MemorySource::Enum source = MemorySource::ExplicitScope)const noexcept;
    [[nodiscard]] const MemorySnapshot& snapshot(MemoryScopeId scope)const noexcept;
    [[nodiscard]] const MemoryDelta& delta(const Name& scopeName, MemorySource::Enum source = MemorySource::ExplicitScope)const noexcept;
    [[nodiscard]] const MemoryDelta& delta(MemoryScopeId scope)const noexcept;
    [[nodiscard]] usize scopeCount()const noexcept{ return m_recorder ? m_recorder->scopeCount() : 0u; }
    [[nodiscard]] MemoryScopeId scopeAt(const usize index)const noexcept{ return m_recorder ? m_recorder->scopeAt(index) : MemoryScopeId{}; }
    [[nodiscard]] Name scopeNameAt(const usize index)const noexcept{ return m_recorder ? m_recorder->scopeNameAt(index) : s_NameNone; }
    [[nodiscard]] const MemorySnapshot& snapshotAt(usize index)const noexcept;
    [[nodiscard]] const MemoryDelta& deltaAt(usize index)const noexcept;


private:
    const MemoryRecorder* m_recorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

