// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult Builder::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    ComboResult result;
    if(itemDeclarationBlocked() || !IsValidComboDeclarationOptions(options, m_comboStyle)){
        m_context.fail();
        return result;
    }
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, search ? WidgetKind::SearchComboBox : WidgetKind::ComboBox);
    if(!widget)
        return result;
    const WidgetState field = **widget;
    const u64 previousSelection = state.selectedKey();
    const b8 previouslyOpen = state.isOpen();
    const b8 previouslyFocused = m_context.input().focus() == field.id;
    if(!m_context.claimState(field, state.m_popup.instanceGeneration()) || !ComboBehavior::Bind(state, field.id, field.declarationGeneration))
        return result;
    state.m_popup.bindParent(m_context.popupToken());
    if(previouslyOpen && !state.isOpen())
        ComboBehavior::Close(state);
    const TextAreaModelStamp queryStamp = search ? search->query().stamp() : TextAreaModelStamp{};
    if(!ComboBehavior::Reconcile(state, source) || (search && !search->query().matchesStamp(queryStamp))){
        m_context.fail();
        return result;
    }
    if(search && (!SearchComboBehavior::Filter(*search, *searchSource) || !search->query().matchesStamp(queryStamp))){
        m_context.fail();
        return result;
    }
    const IListDataSource& results = search ? searchSource->filtered() : source;
    if(!ListBehavior::Reconcile(state.m_list, results) || (search && !search->query().matchesStamp(queryStamp))){
        m_context.fail();
        return result;
    }
    ComboFrame frame;
    frame.parentToken = m_context.popupToken();
    frame.source = &source;
    frame.results = &results;
    frame.searchSource = searchSource;
    frame.search = search;
    frame.style = m_comboStyle;
    frame.popupStyle = m_popupStyle;
    frame.queryHeight = searchOptions ? searchOptions->queryHeight : 0.0f;
    frame.queryGap = searchOptions ? searchOptions->queryGap : 0.0f;
    snapshotComboQuery(frame);
    frame.state = &state;
    frame.options = options;
    frame.token = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    frame.resultCount = results.rowCount();
    const auto popup = m_context.declarePart(field, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_context.declarePart(field, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.m_popup.instanceGeneration(), state.m_popup.openGeneration() };
    frame.listToken = { state.m_list.inputGeneration(), results.instanceGeneration(), results.revision() };
    frame.open = state.isOpen();
    const b8 previouslyListFocused = m_context.input().focus() == frame.rows.id;
    if(!comboMatches(frame) || !applyComboInput(field, frame, result)){
        m_context.fail();
        return {};
    }
    frame.open = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(search && !frame.open)
        search->m_editor.focused = false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.focusOnCommit = options.enabled && previouslyFocused && m_context.input().focus() != field.id && !frame.open;
    frame.listFocusOnCommit = options.enabled && previouslyListFocused && m_context.input().focus() != frame.rows.id;
    Item item(m_arena);
    item.state = field;
    item.style = m_style;
    item.enabled = options.enabled;
    item.combo = static_cast<u32>(m_scope->m_combos.size());
    Point minimumSize{};
    const Array<Name, 5> names{ frame.style.normal, frame.style.hover, frame.style.open, frame.style.focused, frame.style.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, frame.style.fallback);
        if(!skinRegion){
            m_context.fail();
            return {};
        }
        minimumSize = { Max(minimumSize.x, skinRegion->minimumWidth), Max(minimumSize.y, skinRegion->minimumHeight) };
    }
    item.padding = resolveRegionsPadding(frame.style.padding, names, frame.style.fallback);
    const UiSkinRegion* arrow = region(frame.style.arrow, frame.style.arrowFallback);
    if(!arrow){
        m_context.fail();
        return {};
    }
    frame.arrowExtent = ResolveUiSkinSquareExtent(frame.style.arrowExtent, *arrow);
    const b8 selected = state.selectedKey() != 0u;
    const auto index = selected ? source.indexOf(state.selectedKey()) : Expected<u64>{ 0u };
    if(selected && (!index || *index >= frame.rowCount || source.key(*index) != state.selectedKey() || !source.enabled(*index))){
        m_context.fail();
        return {};
    }
    // Text is shaped before another source callback can invalidate a temporary row label.
    const ShapeRequest request = textShapeRequest(selected ? source.text(*index) : options.placeholder, item.style.fontSize);
    auto layout = m_text.layout(request);
    if(!layout){
        m_context.fail();
        return {};
    }
    item.text = Move(*layout);
    if(!comboMatches(frame)){
        m_context.fail();
        return {};
    }
    frame.options.placeholder = {};
    ListOptions listOptions;
    listOptions.wheelRows = options.wheelRows;
    listOptions.enabled = options.enabled;
    listOptions.selectOnNavigate = false;
    const WidgetId commandFocus = frame.editor != s_LayoutNoParent ? m_scope->m_comboEditors[frame.editor].state.id : WidgetId{};
    ListFrame list(m_arena);
    if(!prepareListFrame(*frame.results, state.m_list, listOptions, frame.listToken, frame.resultCount, frame.listFocusOnCommit, commandFocus, list) || !comboMatches(frame)){
        m_context.fail();
        return {};
    }
    const auto textBounds = m_text.measure(item.text, m_paint.displayMetrics());
    if(!textBounds){
        m_context.fail();
        return {};
    }
    item.textBounds = *textBounds;
    const Point measured{ textBounds->width, textBounds->height };
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ComputeComboFieldIntrinsicSize(minimumSize, measured, frame.arrowExtent, frame.style.arrowGap, item.padding);
    if(!addItemNode(description, item))
        return {};
    frame.list = static_cast<u32>(m_scope->m_lists.size());
    if(frame.open && !reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_context.fail();
        return {};
    }
    m_scope->m_lists.push_back(Move(list));
    m_scope->m_combos.push_back(frame);
    m_scope->m_items.push_back(Move(item));
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    result.focused = options.enabled && (m_context.input().focus() == field.id || m_context.input().focus() == frame.rows.id || (frame.editor != s_LayoutNoParent && m_context.input().focus() == m_scope->m_comboEditors[frame.editor].state.id));
    return result;
}

b8 Builder::comboStateMatches(const ComboFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.state && frame.state->inputGeneration() == frame.token.instanceGeneration && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration && frame.state->isOpen() == frame.open
        && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration && frame.state->m_popup.m_parent == frame.parentToken
        && (!frame.search || frame.search->query().matchesStamp(frame.queryStamp))
    ;
}

b8 Builder::comboMatches(const ComboFrame& frame)const{
    return
        frame.source && comboStateMatches(frame) && (!frame.searchSource || &frame.searchSource->filtered() == frame.results) && comboStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && comboStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && comboStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && comboStateMatches(frame)
        && frame.results && frame.results->instanceGeneration() == frame.listToken.contentGeneration && comboStateMatches(frame)
        && frame.results->revision() == frame.listToken.contentRevision && comboStateMatches(frame)
        && frame.results->rowCount() == frame.resultCount && comboStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

