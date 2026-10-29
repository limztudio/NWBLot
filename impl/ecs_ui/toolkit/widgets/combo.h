// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include "popup.h"
#include <impl/ecs_ui/toolkit/widgets/combo_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hosts own the committed selection; the popup list keeps navigation separate until an explicit activation.
class ComboState final : NoCopy{
    friend class Builder;
    friend class ComboBehavior;


public:
    ComboState()noexcept;
    ComboState(ComboState&&) = delete;


public:
    ComboState& operator=(ComboState&&) = delete;


public:
    [[nodiscard]] u64 inputGeneration()const noexcept{ return m_inputGeneration; }

    [[nodiscard]] u64 selectedKey()const noexcept{ return m_selectedKey; }

    [[nodiscard]] b8 isOpen()const noexcept{ return m_popup.isOpen(); }

    [[nodiscard]] const PopupPlacement& placement()const noexcept{ return m_popup.placement(); }

    [[nodiscard]] const Rect& bounds()const noexcept{ return m_bounds; }

    [[nodiscard]] const ListState& listState()const noexcept{ return m_list; }
    // Explicit application changes renew the field input lifetime even when the value is unchanged.
    void select(u64 key)noexcept;
    void open()noexcept;
    void close()noexcept;


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
    [[nodiscard]] static b8 Bind(ComboState& state, WidgetId owner, u64 declarationGeneration)noexcept;
    [[nodiscard]] static b8 Reconcile(ComboState& state, const IListDataSource& source);
    static void Open(ComboState& state)noexcept;
    static void Close(ComboState& state)noexcept;
    [[nodiscard]] static b8 Commit(ComboState& state, const IListDataSource& source, u64 key);
    [[nodiscard]] static ListState& Preview(ComboState& state)noexcept{ return state.m_list; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

