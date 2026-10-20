// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_nested_popup_gallery.h"
#include "ui_gallery_constants.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_gallery{
static constexpr f32 s_CompactGap = 4.0f;
static constexpr AStringView s_ActionKey = "action";
static constexpr AStringView s_EditKey = "edit";
static constexpr AStringView s_MenuAnchorKey = "menu_anchor";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupGallery::UiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const bool initialized = m_beforeText.setText("Before child") && m_afterText.setText("After child") && m_childText.setText("Inside child");
    NWB_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
}

void UiNestedPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_FontSize = 12.0f;
    constexpr f32 s_PanelHeight = 92.0f;
    constexpr f32 s_ParentAnchorInset = 8.0f;
    constexpr f32 s_ParentAnchorTop = 28.0f;
    constexpr f32 s_ParentAnchorWidth = 264.0f;
    constexpr f32 s_ParentAnchorHeight = 28.0f;
    constexpr f32 s_ParentWidth = 300.0f;
    constexpr f32 s_ParentHeight = 420.0f;
    Builder& ui = context.ui;
    m_source.beginFrame();
    const f32 previousFontSize = ui.style().fontSize;
    const f32 previousGap = ui.style().gap;
    ui.style().fontSize = s_FontSize;
    ui.style().gap = __hidden_ui_nested_popup_gallery::s_CompactGap;
    if(!ui.beginPanel("nested_popup_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    bool valid = ui.label(s_TitleKey, "Nested popup / parent scope");
    if(ui.button("open", "Open parent and right child"))
        m_parent.open();
    valid = ui.label(s_HintKey, "Escape/outside closes the top child") && valid;
    valid = ui.endPanel() && valid;
    PopupOptions parent;
    const SIMDVectorSingle parentOrigin = VectorAdd(VectorSet(x, y, 0.0f, 0.0f), VectorSet(s_ParentAnchorInset, s_ParentAnchorTop, 0.0f, 0.0f));
    parent.anchor = { VectorGetX(parentOrigin), VectorGetY(parentOrigin), s_ParentAnchorWidth, s_ParentAnchorHeight };
    parent.size = { s_ParentWidth, s_ParentHeight };
    if(ui.beginPopup("nested_parent", m_parent, parent)){
        valid = paintParent(ui) && valid;
        valid = ui.endPopup() && valid;
    }
    ui.style().fontSize = previousFontSize;
    ui.style().gap = previousGap;
    if(!valid || ui.failed())
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom nested popup declaration failed"));
}

bool UiNestedPopupGallery::paintParent(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    using namespace __hidden_ui_nested_popup_gallery;
    constexpr f32 s_ButtonWidth = 100.0f;
    constexpr f32 s_ListHeight = 56.0f;
    constexpr f32 s_ComboPopupHeight = 172.0f;
    constexpr f32 s_SearchPopupHeight = 196.0f;
    constexpr f32 s_MenuWidth = 200.0f;
    constexpr f32 s_MenuHeight = 172.0f;
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, s_ButtonWidth }, {} };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    ContainerOptions row;
    row.gap = s_CompactGap;
    EditBoxOptions edit;
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, s_ListHeight };
    if(!ui.label(s_TitleKey, "Parent: controls before and after child") || !ui.beginRow("before", row))
        return false;
    if(ui.button(s_ActionKey, "Before", button))
        ++m_beforeClicks;
    const EditBoxResult before = ui.editBox(s_EditKey, m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("open_child", "Open child outside parent clip", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    if(ui.button(s_ActionKey, "After", button))
        ++m_afterClicks;
    const EditBoxResult after = ui.editBox(s_EditKey, m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.popupHeight = s_ComboPopupHeight;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = s_SearchPopupHeight;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    if(ui.button(s_MenuAnchorKey, "Right-click / Menu for commands", wide))
        m_command = 0u;
    ContextMenuOptions menu;
    menu.size = { s_MenuWidth, s_MenuHeight };
    const ContextMenuResult result = ui.contextMenu(s_CommandsKey, s_MenuAnchorKey, m_source, m_menu, menu);
    if(result.activated){
        m_command = result.key;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: nested context command={}"), m_command);
    }
    return result.valid;
}

bool UiNestedPopupGallery::paintChild(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    using namespace __hidden_ui_nested_popup_gallery;
    constexpr f32 s_ChildAnchorTop = 124.0f;
    constexpr f32 s_ChildAnchorWidth = 8.0f;
    constexpr f32 s_ChildAnchorHeight = 32.0f;
    constexpr f32 s_ChildWidth = 220.0f;
    constexpr f32 s_ChildHeight = 178.0f;
    const Rect& parent = m_parent.placement().bounds;
    PopupOptions options;
    const SIMDVectorSingle parentEnd = VectorAdd(VectorSet(parent.x, parent.y, 0.0f, 0.0f), VectorSet(parent.width, s_ChildAnchorTop, 0.0f, 0.0f));
    const SIMDVectorSingle childOrigin = VectorSubtract(parentEnd, VectorSet(s_ChildAnchorWidth, 0.0f, 0.0f, 0.0f));
    options.anchor = { VectorGetX(childOrigin), VectorGetY(parentEnd), s_ChildAnchorWidth, s_ChildAnchorHeight };
    options.side = PopupPlacementSide::Right;
    options.size = { s_ChildWidth, s_ChildHeight };
    if(!ui.beginPopup("right_child", m_child, options))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    if(!ui.label(s_TitleKey, "Independent child overlay"))
        return false;
    if(ui.button(s_ActionKey, "Child action", button))
        ++m_childClicks;
    EditBoxOptions edit;
    const EditBoxResult result = ui.editBox(s_EditKey, m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close ancestor and entire branch", button))
        m_parent.close();
    return result.valid && ui.endPopup();
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

