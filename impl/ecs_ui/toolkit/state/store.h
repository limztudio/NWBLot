// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>

#include <impl/ecs_ui/toolkit/id.adhd.h>

#include <global/containers.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retain identity and typed lifetime; application values stay in the host model and are never copied into draw snapshots.
class WidgetStateStore final : NoCopy{
private:
    using StateIndex = HashMap<u64, usize, Core::Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>>;


public:
    explicit WidgetStateStore(Core::Alloc::GlobalArena& arena)
        : m_entries(arena)
        , m_index(arena)
    {
        // Keep both the prior frame and candidate declarations stable until frame retirement.
        m_entries.reserve(s_WidgetStateRetainedCapacity);
        m_index.reserve(s_WidgetStateRetainedCapacity);
    }


public:
    [[nodiscard]] Expected<NotNull<WidgetState*>> touch(const WidgetId id, const WidgetRoot& root, const WidgetKind::Enum kind, const u64 frameGeneration){
        if(!ValidateWidgetStateTouchRequest(id, root, kind, frameGeneration))
            return MakeUnexpected(Failure{});
        const usize index = findIndex(id);
        if(index != m_entries.size()){
            WidgetState& entry = m_entries[index];
            if(!CanTouchExistingWidgetState(entry, root, frameGeneration))
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
        if(m_entries.size() >= s_WidgetStateRetainedCapacity)
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

    [[nodiscard]] WidgetState* find(const WidgetId id)noexcept{
        const usize index = findIndex(id);
        return index == m_entries.size() ? nullptr : &m_entries[index];
    }

    [[nodiscard]] const WidgetState* find(const WidgetId id)const noexcept{
        const usize index = findIndex(id);
        return index == m_entries.size() ? nullptr : &m_entries[index];
    }

    [[nodiscard]] const PaintVector<WidgetState>& entries()const noexcept{ return m_entries; }

    void erase(const usize index)noexcept{
        if(index >= m_entries.size())
            return;
        m_entries.erase(m_entries.begin() + index);
        // Context retires states in batches; rebuild shifted indices on the next lookup.
        m_indexDirty = true;
    }


private:
    [[nodiscard]] usize findIndex(const WidgetId id)const noexcept{
        if(m_indexDirty)
            rebuildIndex();
        const auto found = m_index.find(id.value);
        if(found == m_index.end())
            return m_entries.size();
        const usize index = found.value();
        NWB_ASSERT(index < m_entries.size() && m_entries[index].id == id);
        return index;
    }

    void rebuildIndex()const noexcept{
        m_index.clear();
        for(usize index = 0u; index < m_entries.size(); ++index){
            if(!m_index.emplace(m_entries[index].id.value, index).second)
                TerminateInvariant();
        }
        m_indexDirty = false;
    }


private:
    PaintVector<WidgetState> m_entries;
    mutable StateIndex m_index;
    u64 m_nextDeclarationGeneration = 1u;
    mutable b8 m_indexDirty = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

