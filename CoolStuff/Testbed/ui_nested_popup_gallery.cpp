// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_nested_popup_gallery.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TestbedUiNestedPopupGallery::TestbedUiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const bool initialized = m_beforeText.setText("Before child") && m_afterText.setText("After child")
        && m_childText.setText("Inside child");
    GLOBAL_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
}

void TestbedUiNestedPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    m_source.beginFrame();
    const f32 previousFontSize = ui.style().fontSize;
    const f32 previousGap = ui.style().gap;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 4.0f;
    if(!ui.beginPanel("nested_popup_gallery", { x, y, 280.0f, 92.0f }))
        return;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ {}, { LayoutSizePolicy::Fixed, 28.0f } };
    bool valid = ui.label("title", "Nested popup / parent scope", title);
    if(ui.button("open", "Open parent and right child", button))
        m_parent.open();
    valid = ui.label("hint", "Escape/outside closes the top child", title) && valid;
    valid = ui.endPanel() && valid;
    PopupOptions parent;
    parent.anchor = { x + 8.0f, y + 28.0f, 264.0f, 28.0f };
    parent.size = { 300.0f, 420.0f };
    if(ui.beginPopup("nested_parent", m_parent, parent)){
        valid = paintParent(ui) && valid;
        valid = ui.endPopup() && valid;
    }
    ui.style().fontSize = previousFontSize;
    ui.style().gap = previousGap;
    if(!valid || ui.failed())
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Testbed: custom nested popup declaration failed"));
}

bool TestbedUiNestedPopupGallery::paintParent(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, 100.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 32.0f };
    row.gap = 4.0f;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, 56.0f };
    list.rowHeight = 20.0f;
    if(!ui.label("title", "Parent: controls before and after child", caption) || !ui.beginRow("before", row))
        return false;
    if(ui.button("action", "Before", button))
        ++m_beforeClicks;
    const EditBoxResult before = ui.editBox("edit", m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("open_child", "Open child outside parent clip", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    if(ui.button("action", "After", button))
        ++m_afterClicks;
    const EditBoxResult after = ui.editBox("edit", m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.height = { LayoutSizePolicy::Fixed, 32.0f };
    combo.popupHeight = 172.0f;
    combo.rowHeight = 24.0f;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = 196.0f;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    if(ui.button("menu_anchor", "Right-click / Menu for commands", wide))
        m_command = 0u;
    ContextMenuOptions menu;
    menu.size = { 200.0f, 172.0f };
    const ContextMenuResult result = ui.contextMenu("commands", "menu_anchor", m_source, m_menu, menu);
    if(result.activated){
        m_command = result.key;
        NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("Testbed: nested context command={}"), m_command);
    }
    return result.valid;
}

bool TestbedUiNestedPopupGallery::paintChild(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const Rect& parent = m_parent.placement().bounds;
    PopupOptions options;
    options.anchor = { parent.x + parent.width - 8.0f, parent.y + 124.0f, 8.0f, 32.0f };
    options.side = PopupPlacementSide::Right;
    options.size = { 220.0f, 178.0f };
    if(!ui.beginPopup("right_child", m_child, options))
        return !ui.failed();
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    if(!ui.label("title", "Independent child overlay", caption))
        return false;
    if(ui.button("action", "Child action", button))
        ++m_childClicks;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    const EditBoxResult result = ui.editBox("edit", m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close ancestor and entire branch", button))
        m_parent.close();
    return result.valid && ui.endPopup();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

