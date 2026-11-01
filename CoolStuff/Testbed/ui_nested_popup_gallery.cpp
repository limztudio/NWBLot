// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_nested_popup_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupGallery::UiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const b8 initialized = m_beforeText.setText("Before child") && m_afterText.setText("After child") && m_childText.setText("Inside child");
    NWB_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
}

void UiNestedPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    const f32 previousFontSize = ui.style().fontSize;
    const f32 previousGap = ui.style().gap;
    ui.style().fontSize = s_NestedPopupFontSize;
    ui.style().gap = s_NestedPopupCompactGap;
    if(!ui.beginPanel("nested_popup_gallery", { x, y, s_OrdinaryGalleryWidth, s_NestedPopupPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Nested popup / parent scope");
    if(ui.button("open", "Open parent and right child"))
        m_parent.open();
    valid = ui.label(s_HintKey, "Escape/outside closes the top child") && valid;
    valid = ui.endPanel() && valid;
    PopupOptions parent;
    const SIMDVectorSingle parentOrigin = VectorAdd(VectorSet(x, y, 0.0f, 0.0f), VectorSet(s_NestedPopupParentAnchorInset, s_NestedPopupParentAnchorTop, 0.0f, 0.0f));
    parent.anchor = { VectorGetX(parentOrigin), VectorGetY(parentOrigin), s_NestedPopupParentAnchorWidth, s_NestedPopupParentAnchorHeight };
    parent.size = { s_NestedPopupParentWidth, s_NestedPopupParentHeight };
    if(ui.beginPopup("nested_parent", m_parent, parent)){
        valid = paintParent(ui) && valid;
        valid = ui.endPopup() && valid;
    }
    ui.style().fontSize = previousFontSize;
    ui.style().gap = previousGap;
    if(!valid || ui.failed())
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom nested popup declaration failed"));
}

b8 UiNestedPopupGallery::paintParent(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, s_NestedPopupButtonWidth }, {} };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    ContainerOptions row;
    row.gap = s_NestedPopupCompactGap;
    EditBoxOptions edit;
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, s_NestedPopupListHeight };
    if(!ui.label(s_TitleKey, "Parent: controls before and after child") || !ui.beginRow("before", row))
        return false;
    ui.button(s_NestedPopupActionKey, "Before", button);
    const EditBoxResult before = ui.editBox(s_NestedPopupEditKey, m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("open_child", "Open child outside parent clip", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    ui.button(s_NestedPopupActionKey, "After", button);
    const EditBoxResult after = ui.editBox(s_NestedPopupEditKey, m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.popupHeight = s_NestedPopupComboPopupHeight;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = s_NestedPopupSearchPopupHeight;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    ui.button(s_NestedPopupMenuAnchorKey, "Right-click / Menu for commands", wide);
    ContextMenuOptions menu;
    menu.size = { s_NestedPopupMenuWidth, s_NestedPopupMenuHeight };
    const ContextMenuResult result = ui.contextMenu(s_CommandsKey, s_NestedPopupMenuAnchorKey, m_source, m_menu, menu);
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: nested context command={}"), result.key);
    return result.valid;
}

b8 UiNestedPopupGallery::paintChild(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const Rect& parent = m_parent.placement().bounds;
    PopupOptions options;
    const SIMDVectorSingle parentEnd = VectorAdd(VectorSet(parent.x, parent.y, 0.0f, 0.0f), VectorSet(parent.width, s_NestedPopupChildAnchorTop, 0.0f, 0.0f));
    const SIMDVectorSingle childOrigin = VectorSubtract(parentEnd, VectorSet(s_NestedPopupChildAnchorWidth, 0.0f, 0.0f, 0.0f));
    options.anchor = { VectorGetX(childOrigin), VectorGetY(parentEnd), s_NestedPopupChildAnchorWidth, s_NestedPopupChildAnchorHeight };
    options.side = PopupPlacementSide::Right;
    options.size = { s_NestedPopupChildWidth, s_NestedPopupChildHeight };
    if(!ui.beginPopup("right_child", m_child, options))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    if(!ui.label(s_TitleKey, "Independent child overlay"))
        return false;
    ui.button(s_NestedPopupActionKey, "Child action", button);
    EditBoxOptions edit;
    const EditBoxResult result = ui.editBox(s_NestedPopupEditKey, m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close ancestor and entire branch", button))
        m_parent.close();
    return result.valid && ui.endPopup();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

