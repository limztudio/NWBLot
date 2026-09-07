// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "mesh.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One liveness query owns its requested identities and found flags. Providers only mark matches; no state survives
// the owning prune operation. The capacity hint is the number of retained runtime entries, including duplicates.
class RuntimeMeshRequestSet final : NoCopy{
private:
    struct Key{
        NameHash name;
        u64 version;
    };
    struct Lookup{
        const NameHash& name;
        u64 version;
    };
    struct KeyHash{
        [[nodiscard]] usize operator()(const Key& key)const noexcept;
        [[nodiscard]] usize operator()(const Lookup& key)const noexcept;
    };
    struct KeyEqual{
        using is_transparent = void;

        [[nodiscard]] bool operator()(const Key& lhs, const Key& rhs)const noexcept;
        [[nodiscard]] bool operator()(const Key& lhs, const Lookup& rhs)const noexcept;
    };
    struct Entry{
        Key key;
        bool found;
    };
    using Index = HashMap<Key, bool, KeyHash, KeyEqual, Core::Alloc::ScratchArena>;


private:
    static constexpr usize s_InlineCapacity = 32u;


public:
    RuntimeMeshRequestSet(Core::Alloc::ScratchArena& scratchArena, usize expectedRequestCount);
    RuntimeMeshRequestSet(RuntimeMeshRequestSet&&) = delete;


public:
    void add(const Name& meshKey, u64 version);
    void markLive(const Name& meshKey, u64 version){
        if(!meshKey)
            return;
        if(m_index){
            markIndexedLive(meshKey, version);
            return;
        }
        const usize index = findInline(meshKey.identityHash(), version);
        if(index < m_inlineCount && !m_inlineEntries[index].found){
            m_inlineEntries[index].found = true;
            --m_remainingCount;
        }
    }
    [[nodiscard]] bool containsLive(const Name& meshKey, u64 version)const{
        if(!meshKey)
            return false;
        if(m_index)
            return containsIndexedLive(meshKey, version);
        const usize index = findInline(meshKey.identityHash(), version);
        return index < m_inlineCount && m_inlineEntries[index].found;
    }
    [[nodiscard]] bool complete()const noexcept{ return m_remainingCount == 0u; }


private:
    [[nodiscard]] usize findInline(const NameHash& name, const u64 version)const noexcept{
        for(usize index = 0u; index < m_inlineCount; ++index){
            const Key& key = m_inlineEntries[index].key;
            if(key.version == version && key.name == name)
                return index;
        }
        return m_inlineCount;
    }
    void promoteIndex();
    void markIndexedLive(const Name& meshKey, u64 version);
    [[nodiscard]] bool containsIndexedLive(const Name& meshKey, u64 version)const;


private:
    Core::Alloc::ScratchArena& m_scratchArena;
    usize m_expectedRequestCount;
    Entry m_inlineEntries[s_InlineCapacity];
    usize m_inlineCount = 0u;
    usize m_remainingCount = 0u;
    Optional<Index> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

