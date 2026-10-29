// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_nested_popup_source.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupGallery final : NoCopy{
public:
    explicit UiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    [[nodiscard]] b8 paintParent(NWB::Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintChild(NWB::Impl::Ui::Builder& ui);


private:
    UiNestedPopupSource m_source;
    NWB::Impl::Ui::PopupState m_parent;
    NWB::Impl::Ui::PopupState m_child;
    NWB::Impl::Ui::ListState m_beforeList;
    NWB::Impl::Ui::ListState m_afterList;
    NWB::Impl::Ui::ComboState m_combo;
    NWB::Impl::Ui::SearchComboState m_search;
    NWB::Impl::Ui::ContextMenuState m_menu;
    NWB::Impl::Ui::EditModel m_beforeText;
    NWB::Impl::Ui::EditModel m_afterText;
    NWB::Impl::Ui::EditModel m_childText;
    NWB::Impl::Ui::EditBoxState m_beforeEdit;
    NWB::Impl::Ui::EditBoxState m_afterEdit;
    NWB::Impl::Ui::EditBoxState m_childEdit;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

