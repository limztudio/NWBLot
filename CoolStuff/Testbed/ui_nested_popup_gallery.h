// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ui_nested_popup_source.h"

#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/context_menu.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiNestedPopupGallery final : NoCopy{
public:
    explicit TestbedUiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    [[nodiscard]] bool paintParent(NWB::Impl::Ui::Builder& ui);
    [[nodiscard]] bool paintChild(NWB::Impl::Ui::Builder& ui);


private:
    TestbedUiNestedPopupSource m_source;
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
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_childClicks = 0u;
    u64 m_command = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

