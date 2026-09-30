// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace WidgetKind{
    enum Enum : u8{
        Panel, Container, Label, Button, Checkbox, Window, Separator, EditBox, Popup, Selectable, VirtualList,
        ComboBox, SearchComboBox, Tooltip, ContextMenu, TextArea, RadioGroup, Slider, Progress, Image
    };
};

struct WidgetState{
    WidgetId id;
    WidgetRoot root;
    u64 declarationGeneration = 0u;
    u64 lastSeenFrame = 0u;
    WidgetKind::Enum kind = WidgetKind::Panel;
};

// Retain identity and typed lifetime; application values stay in the host model and are never copied into draw snapshots.
class WidgetStateStore final : NoCopy{
private:
    using StateIndex = HashMap<u64, usize, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena>;


public:
    explicit WidgetStateStore(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] WidgetState* touch(WidgetId id, const WidgetRoot& root, WidgetKind::Enum kind, u64 frameGeneration);
    [[nodiscard]] WidgetState* find(WidgetId id);
    [[nodiscard]] const WidgetState* find(WidgetId id)const;
    [[nodiscard]] const PaintVector<WidgetState>& entries()const{ return m_entries; }
    void erase(usize index);


private:
    [[nodiscard]] usize findIndex(WidgetId id)const;
    void rebuildIndex()const;


private:
    PaintVector<WidgetState> m_entries;
    mutable StateIndex m_index;
    u64 m_nextDeclarationGeneration = 1u;
    mutable bool m_indexDirty = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

