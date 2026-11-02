// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_requests.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RuntimeMeshRequestSet::RuntimeMeshRequestSet(Core::Alloc::ScratchArena& scratchArena, const usize expectedRequestCount)
    : m_scratchArena(scratchArena)
    , m_expectedRequestCount(expectedRequestCount)
{
    if(expectedRequestCount > s_RuntimeMeshRequestInlineCapacity)
        m_index.emplace(SizeOf<s_RuntimeMeshIndexBucketsPerRequest>(expectedRequestCount), KeyHash{}, KeyEqual{}, m_scratchArena);
}

void RuntimeMeshRequestSet::add(const Name& meshKey, const u64 version){
    if(!meshKey)
        return;
    if(!m_index){
        if(FindInlineRuntimeMeshRequest(m_inlineEntries, m_inlineCount, meshKey.identityHash(), version) < m_inlineCount)
            return;
        if(m_inlineCount < s_RuntimeMeshRequestInlineCapacity){
            m_inlineEntries[m_inlineCount] = Entry(Key(meshKey.identityHash(), version), false);
            ++m_inlineCount;
            ++m_remainingCount;
            return;
        }
        promoteIndex();
    }
    if(m_index->try_emplace(Key(meshKey.identityHash(), version), false).second)
        ++m_remainingCount;
}

void RuntimeMeshRequestSet::markLive(const Name& meshKey, const u64 version)noexcept{
    if(!meshKey)
        return;
    if(m_index){
        markIndexedLive(meshKey, version);
        return;
    }
    const usize index = FindInlineRuntimeMeshRequest(m_inlineEntries, m_inlineCount, meshKey.identityHash(), version);
    if(index < m_inlineCount && !m_inlineEntries[index].found){
        m_inlineEntries[index].found = true;
        --m_remainingCount;
    }
}

b8 RuntimeMeshRequestSet::containsLive(const Name& meshKey, const u64 version)const noexcept{
    if(!meshKey)
        return false;
    if(m_index)
        return containsIndexedLive(meshKey, version);
    const usize index = FindInlineRuntimeMeshRequest(m_inlineEntries, m_inlineCount, meshKey.identityHash(), version);
    return index < m_inlineCount && m_inlineEntries[index].found;
}

void RuntimeMeshRequestSet::promoteIndex(){
    Index index(SizeOf<s_RuntimeMeshIndexBucketsPerRequest>(Max(m_expectedRequestCount, s_RuntimeMeshRequestInlineCapacity + 1u)), KeyHash{}, KeyEqual{}, m_scratchArena);
    for(usize entryIndex = 0u; entryIndex < m_inlineCount; ++entryIndex){
        const Entry& entry = m_inlineEntries[entryIndex];
        index[entry.key] = entry.found;
    }
    static_assert(IsNothrowMoveConstructible_V<Index>);
    m_index.emplace(Move(index));
}

void RuntimeMeshRequestSet::markIndexedLive(const Name& meshKey, const u64 version)noexcept{
    const auto found = m_index->find(Lookup{ meshKey.identityHash(), version });
    if(found != m_index->end() && !found.value()){
        found.value() = true;
        --m_remainingCount;
    }
}

b8 RuntimeMeshRequestSet::containsIndexedLive(const Name& meshKey, const u64 version)const noexcept{
    const auto found = m_index->find(Lookup{ meshKey.identityHash(), version });
    return found != m_index->end() && found.value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

