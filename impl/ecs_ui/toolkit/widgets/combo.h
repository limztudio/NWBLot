// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include "popup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ComboOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    LayoutSize height;
    f32 popupHeight = 240.0f;
    f32 rowHeight = 32.0f;
    f32 wheelRows = 3.0f;
    bool enabled = true;
    StringView placeholder = "Select...";
};

struct ComboResult{
    bool valid = false;
    bool selectionChanged = false;
    bool committed = false;
    bool opened = false;
    bool closed = false;
    bool focused = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hosts own the committed selection; the popup list keeps navigation separate until an explicit activation.
class ComboState final : NoCopy{
    friend class Builder;
    friend class ComboBehavior;


public:
    ComboState();


public:
    ComboState(ComboState&&) = delete;
    ComboState& operator=(ComboState&&) = delete;


public:
    [[nodiscard]] u64 inputGeneration()const{ return m_inputGeneration; }
    [[nodiscard]] u64 selectedKey()const{ return m_selectedKey; }
    [[nodiscard]] bool isOpen()const{ return m_popup.isOpen(); }
    [[nodiscard]] const PopupPlacement& placement()const{ return m_popup.placement(); }
    [[nodiscard]] const Rect& bounds()const{ return m_bounds; }
    [[nodiscard]] const ListState& listState()const{ return m_list; }
    // Explicit application changes renew the field input lifetime even when the value is unchanged.
    void select(u64 key);
    void open();
    void close();


private:
    ListState m_list;
    PopupState m_popup;
    Rect m_bounds;
    WidgetId m_owner;
    u64 m_ownerDeclaration = 0u;
    u64 m_inputGeneration = 0u;
    u64 m_selectedKey = 0u;
    u64 m_sourceGeneration = 0u;
    u64 m_sourceRevision = 0u;
};

class ComboBehavior final{
public:
    // Rebinding after omission or to another field retires popup interaction while preserving committed selection.
    [[nodiscard]] static bool bind(ComboState& state, WidgetId owner, u64 declarationGeneration);
    [[nodiscard]] static bool reconcile(ComboState& state, const IListDataSource& source);
    static void open(ComboState& state);
    static void close(ComboState& state);
    [[nodiscard]] static bool commit(ComboState& state, const IListDataSource& source, u64 key);
    [[nodiscard]] static ListState& preview(ComboState& state){ return state.m_list; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

