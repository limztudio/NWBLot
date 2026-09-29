// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace WidgetKind{
    enum Enum : u8{
        Panel, Container, Label, Button, Checkbox, Window, Separator, EditBox, Popup, Selectable, VirtualList,
        ComboBox, SearchComboBox, Tooltip, ContextMenu, TextArea, RadioGroup
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
public:
    explicit WidgetStateStore(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] WidgetState* touch(WidgetId id, const WidgetRoot& root, WidgetKind::Enum kind, u64 frameGeneration);
    [[nodiscard]] const PaintVector<WidgetState>& entries()const{ return m_entries; }
    void erase(usize index);


private:
    PaintVector<WidgetState> m_entries;
    u64 m_nextDeclarationGeneration = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

