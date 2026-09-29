// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult Builder::comboBox(
    const AStringView stableKey, const IListDataSource& source, ComboState& state, const ComboOptions& options){
    ComboResult result;
    if(
        !m_panelActive || m_popupState || (m_windowActive && m_window.state->collapsed) || m_context.failed()
        || m_items.size() >= s_LayoutMaxNodes || !IsFinite(options.popupHeight) || options.popupHeight <= 0.0f
        || !IsFinite(options.rowHeight) || options.rowHeight <= 0.0f || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f
        || !IsFinite(m_comboStyle.arrowExtent) || m_comboStyle.arrowExtent <= 0.0f
        || !IsFinite(m_comboStyle.arrowGap) || m_comboStyle.arrowGap < 0.0f
    ){
        m_context.fail();
        return result;
    }
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::ComboBox);
    if(!widget)
        return result;
    const WidgetState field = *widget;
    const u64 previousSelection = state.selectedKey();
    const bool previouslyOpen = state.isOpen();
    const bool previouslyFocused = m_context.input().focus() == field.id;
    if(
        !m_context.claimState(field, state.m_popup.instanceGeneration())
        || !ComboBehavior::Bind(state, field.id, field.declarationGeneration)
    )
        return result;
    if(!ComboBehavior::Reconcile(state, source) || !ListBehavior::Reconcile(state.m_list, source)){
        m_context.fail();
        return result;
    }
    ComboFrame frame;
    frame.source = &source;
    frame.state = &state;
    frame.options = options;
    frame.token = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    WidgetState* popup = m_context.declarePart(field, "popup", WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = *popup;
    WidgetState* rows = m_context.declarePart(field, "rows", WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = *rows;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration,
        state.m_popup.instanceGeneration(), state.m_popup.openGeneration() };
    frame.listToken = { state.m_list.inputGeneration(), frame.token.contentGeneration, frame.token.contentRevision };
    frame.open = state.isOpen();
    const bool previouslyListFocused = m_context.input().focus() == frame.rows.id;
    if(!comboMatches(frame) || !applyComboInput(field, frame, result)){
        m_context.fail();
        return {};
    }
    frame.open = state.isOpen();
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.focusOnCommit = options.enabled && previouslyFocused && m_context.input().focus() != field.id && !frame.open;
    frame.listFocusOnCommit = options.enabled && previouslyListFocused && m_context.input().focus() != frame.rows.id;
    Item item(m_arena);
    item.state = field;
    item.enabled = options.enabled;
    item.combo = static_cast<u32>(m_combos.size());
    item.padding = m_comboStyle.padding;
    Point minimum;
    const Name names[]{ m_comboStyle.normal, m_comboStyle.hover, m_comboStyle.open,
        m_comboStyle.focused, m_comboStyle.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_comboStyle.fallback);
        if(!skinRegion){
            m_context.fail();
            return {};
        }
        minimum.x = Max(minimum.x, skinRegion->minimumWidth);
        minimum.y = Max(minimum.y, skinRegion->minimumHeight);
        item.padding.left = Max(item.padding.left, skinRegion->padding.left);
        item.padding.top = Max(item.padding.top, skinRegion->padding.top);
        item.padding.right = Max(item.padding.right, skinRegion->padding.right);
        item.padding.bottom = Max(item.padding.bottom, skinRegion->padding.bottom);
    }
    const UiSkinRegion* arrow = region(m_comboStyle.arrow, m_comboStyle.arrowFallback);
    if(!arrow){
        m_context.fail();
        return {};
    }
    frame.arrowExtent = Max(m_comboStyle.arrowExtent, Max(arrow->minimumWidth, arrow->minimumHeight));
    u64 index = 0u;
    const bool selected = state.selectedKey() != 0u;
    if(
        selected && (!source.indexOf(state.selectedKey(), index) || index >= frame.rowCount
            || source.key(index) != state.selectedKey() || !source.enabled(index))
    ){
        m_context.fail();
        return {};
    }
    // Text is shaped before another source callback can invalidate a temporary row label.
    const ShapeRequest request{ selected ? source.text(index) : options.placeholder, m_style.fontSize };
    if(m_text.layout(request, item.text) != TextLayoutStatus::Success || !comboMatches(frame)){
        m_context.fail();
        return {};
    }
    frame.options.placeholder = {};
    ListFrame list;
    list.source = &source;
    list.state = &state.m_list;
    list.options.rowHeight = options.rowHeight;
    list.options.wheelRows = options.wheelRows;
    list.options.enabled = options.enabled;
    list.options.selectOnNavigate = false;
    list.token = frame.listToken;
    list.focusOnCommit = frame.listFocusOnCommit;
    list.rowCount = frame.rowCount;
    list.padding = m_listStyle.padding;
    const UiSkinRegion* background = region(m_listStyle.background, m_listStyle.backgroundFallback);
    if(!background){
        m_context.fail();
        return {};
    }
    list.padding.left = Max(list.padding.left, background->padding.left);
    list.padding.top = Max(list.padding.top, background->padding.top);
    list.padding.right = Max(list.padding.right, background->padding.right);
    list.padding.bottom = Max(list.padding.bottom, background->padding.bottom);
    const Point measured = item.text.measure();
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = { Max(minimum.x, measured.x + frame.arrowExtent + m_comboStyle.arrowGap
        + item.padding.left + item.padding.right), Max(minimum.y, Max(measured.y, frame.arrowExtent)
        + item.padding.top + item.padding.bottom) };
    if(!m_layout.addNode(m_stack.back(), description, item.node)){
        m_context.fail();
        return {};
    }
    frame.list = static_cast<u32>(m_lists.size());
    m_lists.push_back(list);
    m_combos.push_back(frame);
    m_items.push_back(Move(item));
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    result.focused = options.enabled && (m_context.input().focus() == field.id || m_context.input().focus() == frame.rows.id);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

