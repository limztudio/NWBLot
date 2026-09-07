// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_requests.h"

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RuntimeMeshRequestSet::KeyHash::operator()(const Key& key)const noexcept{
    usize hash = Hasher<NameHash>{}(key.name);
    HashCombine(hash, key.version);
    return hash;
}

usize RuntimeMeshRequestSet::KeyHash::operator()(const Lookup& key)const noexcept{
    usize hash = Hasher<NameHash>{}(key.name);
    HashCombine(hash, key.version);
    return hash;
}

bool RuntimeMeshRequestSet::KeyEqual::operator()(const Key& lhs, const Key& rhs)const noexcept{
    return lhs.version == rhs.version && lhs.name == rhs.name;
}

bool RuntimeMeshRequestSet::KeyEqual::operator()(const Key& lhs, const Lookup& rhs)const noexcept{
    return lhs.version == rhs.version && lhs.name == rhs.name;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RuntimeMeshRequestSet::RuntimeMeshRequestSet(Core::Alloc::ScratchArena& scratchArena, const usize expectedRequestCount)
    : m_scratchArena(scratchArena)
    , m_expectedRequestCount(expectedRequestCount)
{
    if(expectedRequestCount > s_InlineCapacity)
        m_index.emplace(SizeOf<2u>(expectedRequestCount), KeyHash{}, KeyEqual{}, m_scratchArena);
}

void RuntimeMeshRequestSet::add(const Name& meshKey, const u64 version){
    if(!meshKey)
        return;
    if(!m_index){
        if(findInline(meshKey.identityHash(), version) < m_inlineCount)
            return;
        if(m_inlineCount < s_InlineCapacity){
            m_inlineEntries[m_inlineCount] = Entry{ { meshKey.identityHash(), version }, false };
            ++m_inlineCount;
            ++m_remainingCount;
            return;
        }
        promoteIndex();
    }
    if(m_index->try_emplace(Key{ meshKey.identityHash(), version }, false).second)
        ++m_remainingCount;
}

void RuntimeMeshRequestSet::promoteIndex(){
    Index index(SizeOf<2u>(Max(m_expectedRequestCount, s_InlineCapacity + 1u)), KeyHash{}, KeyEqual{}, m_scratchArena);
    for(usize entryIndex = 0u; entryIndex < m_inlineCount; ++entryIndex){
        const Entry& entry = m_inlineEntries[entryIndex];
        index[entry.key] = entry.found;
    }
    static_assert(IsNothrowMoveConstructible_V<Index>);
    m_index.emplace(Move(index));
}

void RuntimeMeshRequestSet::markIndexedLive(const Name& meshKey, const u64 version){
    const auto found = m_index->find(Lookup{ meshKey.identityHash(), version });
    if(found != m_index->end() && !found.value()){
        found.value() = true;
        --m_remainingCount;
    }
}

bool RuntimeMeshRequestSet::containsIndexedLive(const Name& meshKey, const u64 version)const{
    const auto found = m_index->find(Lookup{ meshKey.identityHash(), version });
    return found != m_index->end() && found.value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

