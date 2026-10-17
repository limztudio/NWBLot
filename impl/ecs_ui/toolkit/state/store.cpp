// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "store.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WidgetStateStore::WidgetStateStore(Core::Alloc::GlobalArena& arena)
    : m_entries(arena)
    , m_index(arena)
{
    // Keep both the prior frame and candidate declarations stable until frame retirement.
    m_entries.reserve(s_RetainedCapacity);
    m_index.reserve(s_RetainedCapacity);
}

Expected<NotNull<WidgetState*>> WidgetStateStore::touch(const WidgetId id, const WidgetRoot& root, const WidgetKind::Enum kind, const u64 frameGeneration){
    if(!id.valid() || root.generation == 0u || frameGeneration == 0u || kind > WidgetKind::Image)
        return MakeUnexpected(Failure{});
    const usize index = findIndex(id);
    if(index != m_entries.size()){
        WidgetState& entry = m_entries[index];
        if(!(entry.root == root) || entry.lastSeenFrame == frameGeneration)
            return MakeUnexpected(Failure{});
        if(entry.kind != kind){
            if(m_nextDeclarationGeneration == Limit<u64>::s_Max)
                TerminateInvariant();
            entry.declarationGeneration = m_nextDeclarationGeneration;
            ++m_nextDeclarationGeneration;
            entry.kind = kind;
        }
        entry.lastSeenFrame = frameGeneration;
        return MakeNotNull(&entry);
    }
    if(m_entries.size() >= s_RetainedCapacity)
        return MakeUnexpected(Failure{});
    if(m_nextDeclarationGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    const usize newIndex = m_entries.size();
    m_entries.push_back({ id, root, m_nextDeclarationGeneration, frameGeneration, kind });
    if(!m_index.emplace(id.value, newIndex).second){
        m_entries.pop_back();
        TerminateInvariant();
    }
    ++m_nextDeclarationGeneration;
    return MakeNotNull(&m_entries.back());
}

WidgetState* WidgetStateStore::find(const WidgetId id)noexcept{
    const usize index = findIndex(id);
    return index == m_entries.size() ? nullptr : &m_entries[index];
}

const WidgetState* WidgetStateStore::find(const WidgetId id)const noexcept{
    const usize index = findIndex(id);
    return index == m_entries.size() ? nullptr : &m_entries[index];
}

void WidgetStateStore::erase(const usize index)noexcept{
    if(index >= m_entries.size())
        return;
    m_entries.erase(m_entries.begin() + index);
    // Context retires states in batches; rebuild shifted indices on the next lookup.
    m_indexDirty = true;
}

usize WidgetStateStore::findIndex(const WidgetId id)const noexcept{
    if(m_indexDirty)
        rebuildIndex();
    const auto found = m_index.find(id.value);
    if(found == m_index.end())
        return m_entries.size();
    const usize index = found.value();
    NWB_ASSERT(index < m_entries.size() && m_entries[index].id == id);
    return index;
}

void WidgetStateStore::rebuildIndex()const noexcept{
    m_index.clear();
    for(usize index = 0u; index < m_entries.size(); ++index){
        if(!m_index.emplace(m_entries[index].id.value, index).second)
            TerminateInvariant();
    }
    m_indexDirty = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

