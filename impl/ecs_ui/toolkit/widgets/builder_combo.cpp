// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_combo{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ComboQueryCache{
    u64 revision = 0u;
    u64 externalRevision = 0u;
    u64 compositionGeneration = 0u;
    u64 selectionGeneration = 0u;
    usize anchor = 0u;
    usize caret = 0u;

    [[nodiscard]] static ComboQueryCache Capture(const SearchComboState* const search)noexcept{
        if(!search)
            return {};
        const EditModel& query = search->query();
        return {
            query.revision(),
            query.externalRevision(),
            query.compositionGeneration(),
            query.selectionGeneration(),
            query.anchor(),
            query.caret(),
        };
    }

    [[nodiscard]] b8 matches(const SearchComboState* const search)const noexcept{
        if(!search)
            return true;
        const EditModel& query = search->query();
        return
            query.revision() == revision
            && query.externalRevision() == externalRevision
            && query.compositionGeneration() == compositionGeneration
            && query.selectionGeneration() == selectionGeneration
            && query.anchor() == anchor
            && query.caret() == caret
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult Builder::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    ComboResult result;
    if(
        itemDeclarationBlocked() || !IsFinite(options.popupHeight) || options.popupHeight <= 0.0f
        || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f
        || !IsFinite(m_comboStyle.arrowExtent) || m_comboStyle.arrowExtent <= 0.0f
        || !IsFinite(m_comboStyle.arrowGap) || m_comboStyle.arrowGap < 0.0f
    ){
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
    const __hidden_builder_combo::ComboQueryCache queryCache = __hidden_builder_combo::ComboQueryCache::Capture(search);
    if(!ComboBehavior::Reconcile(state, source) || !queryCache.matches(search)){
        m_context.fail();
        return result;
    }
    if(search && (!SearchComboBehavior::Filter(*search, *searchSource) || !queryCache.matches(search))){
        m_context.fail();
        return result;
    }
    const IListDataSource& results = search ? searchSource->filtered() : source;
    if(!ListBehavior::Reconcile(state.m_list, results) || !queryCache.matches(search)){
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
    ListFrame list(m_arena);
    list.source = frame.results;
    list.state = &state.m_list;
    list.widgetStyle = m_style;
    list.style = m_listStyle;
    list.options.wheelRows = options.wheelRows;
    list.options.enabled = options.enabled;
    list.options.selectOnNavigate = false;
    list.token = frame.listToken;
    list.focusOnCommit = frame.listFocusOnCommit;
    list.rowCount = frame.resultCount;
    if(frame.editor != s_LayoutNoParent)
        list.commandFocus = m_scope->m_comboEditors[frame.editor].state.id;
    list.padding = list.style.padding;
    const UiSkinRegion* background = region(list.style.background, list.style.backgroundFallback);
    if(!background){
        m_context.fail();
        return {};
    }
    list.padding = MaxUiPadding(list.padding, background->padding);
    if(!prepareListMetrics(list) || !comboMatches(frame)){
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
    const f32 paddedWidth = ((measured.x + frame.arrowExtent) + frame.style.arrowGap) + item.padding.left + item.padding.right;
    const f32 paddedHeight = Max(measured.y, frame.arrowExtent) + item.padding.top + item.padding.bottom;
    description.intrinsicSize = { Max(minimumSize.x, paddedWidth), Max(minimumSize.y, paddedHeight) };
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
        && (!frame.search || (frame.search->query().instanceGeneration() == frame.queryGeneration && frame.search->query().revision() == frame.queryRevision && frame.search->query().compositionGeneration() == frame.queryCompositionGeneration
        && frame.search->query().selectionGeneration() == frame.querySelectionGeneration && frame.search->query().externalRevision() == frame.queryExternalRevision && frame.search->query().anchor() == frame.queryAnchor
        && frame.search->query().caret() == frame.queryCaret))
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

