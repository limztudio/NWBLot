// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retain identity and typed lifetime; application values stay in the host model and are never copied into draw snapshots.
class WidgetStateStore final : NoCopy{
private:
    using StateIndex = HashMap<u64, usize, Core::Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>>;


private:
    static constexpr usize s_RetainedCapacity = s_WidgetMaxStates * 2u;


public:
    explicit WidgetStateStore(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<NotNull<WidgetState*>> touch(WidgetId id, const WidgetRoot& root, WidgetKind::Enum kind, u64 frameGeneration);
    [[nodiscard]] WidgetState* find(WidgetId id)noexcept;
    [[nodiscard]] const WidgetState* find(WidgetId id)const noexcept;
    [[nodiscard]] const PaintVector<WidgetState>& entries()const noexcept{ return m_entries; }
    void erase(usize index)noexcept;


private:
    [[nodiscard]] usize findIndex(WidgetId id)const noexcept;
    void rebuildIndex()const noexcept;


private:
    PaintVector<WidgetState> m_entries;
    mutable StateIndex m_index;
    u64 m_nextDeclarationGeneration = 1u;
    mutable b8 m_indexDirty = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

