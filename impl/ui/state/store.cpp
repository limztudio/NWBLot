// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "store.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WidgetStateStore::WidgetStateStore(Core::Alloc::GlobalArena& arena)
    : m_entries(arena)
{
    m_entries.reserve(4096u);
}

WidgetState* WidgetStateStore::touch(
    const WidgetId id, const WidgetRoot& root, const WidgetKind::Enum kind, const u64 frameGeneration){
    if(!id.valid() || root.generation == 0u || frameGeneration == 0u || kind > WidgetKind::SearchComboBox)
        return nullptr;
    for(auto& entry : m_entries){
        if(entry.id != id)
            continue;
        if(!(entry.root == root) || entry.lastSeenFrame == frameGeneration)
            return nullptr;
        if(entry.kind != kind){
            if(m_nextDeclarationGeneration == Limit<u64>::s_Max)
                TerminateInvariant();
            entry.declarationGeneration = m_nextDeclarationGeneration;
            ++m_nextDeclarationGeneration;
            entry.kind = kind;
        }
        entry.lastSeenFrame = frameGeneration;
        return &entry;
    }
    if(m_entries.size() >= 4096u)
        return nullptr;
    if(m_nextDeclarationGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    m_entries.push_back({ id, root, m_nextDeclarationGeneration, frameGeneration, kind });
    ++m_nextDeclarationGeneration;
    return &m_entries.back();
}

void WidgetStateStore::erase(const usize index){
    if(index < m_entries.size())
        m_entries.erase(m_entries.begin() + index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

