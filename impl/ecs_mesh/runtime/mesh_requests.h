// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "mesh.h"

#include <core/alloc/scratch.h>

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One liveness query owns identities and flags; no state survives the prune.
class RuntimeMeshRequestSet final : NoCopy{
private:
    using Key = RuntimeMeshRequestKey;
    using Entry = RuntimeMeshRequestEntry;


private:
    struct Lookup{
        const NameHash& name;
        u64 version;
    };
    struct KeyHash{
        [[nodiscard]] usize operator()(const Key& key)const noexcept{ return key.hash(); }
        [[nodiscard]] usize operator()(const Lookup& key)const noexcept;
    };
    struct KeyEqual{
        using is_transparent = void;


    public:
        [[nodiscard]] b8 operator()(const Key& lhs, const Key& rhs)const noexcept{ return lhs == rhs; }
        [[nodiscard]] b8 operator()(const Key& lhs, const Lookup& rhs)const noexcept{ return lhs.version == rhs.version && lhs.name == rhs.name; }
    };


private:
    using Index = HashMap<Key, b8, Core::Alloc::ScratchArena, KeyHash, KeyEqual>;


public:
    RuntimeMeshRequestSet(Core::Alloc::ScratchArena& scratchArena, usize expectedRequestCount);
    RuntimeMeshRequestSet(RuntimeMeshRequestSet&&) = delete;


public:
    void add(const Name& meshKey, u64 version);
    void markLive(const Name& meshKey, u64 version)noexcept;

    [[nodiscard]] b8 containsLive(const Name& meshKey, u64 version)const noexcept;

    [[nodiscard]] b8 complete()const noexcept{ return m_remainingCount == 0u; }


private:
    [[nodiscard]] usize findInline(const NameHash& name, u64 version)const noexcept;
    void promoteIndex();
    void markIndexedLive(const Name& meshKey, u64 version)noexcept;
    [[nodiscard]] b8 containsIndexedLive(const Name& meshKey, u64 version)const noexcept;


private:
    Core::Alloc::ScratchArena& m_scratchArena;
    usize m_expectedRequestCount;
    Array<Entry, s_RuntimeMeshRequestInlineCapacity> m_inlineEntries{};
    usize m_inlineCount = 0u;
    usize m_remainingCount = 0u;
    Optional<Index> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

