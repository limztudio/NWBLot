// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/combo_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/ui_scope_descriptor_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/list_input.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult ComboCoordinator::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    ComboResult result;
    if(m_builder.itemDeclarationBlocked() || !IsValidComboDeclarationOptions(options, m_builder.m_comboStyle)){
        m_builder.m_context.fail();
        return result;
    }
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, search ? WidgetKind::SearchComboBox : WidgetKind::ComboBox);
    if(!widget)
        return result;
    const WidgetState field = **widget;
    const u64 previousSelection = state.selectedKey();
    const b8 previouslyOpen = state.isOpen();
    const b8 previouslyFocused = m_builder.m_context.input().focus() == field.id;
    if(!m_builder.m_context.claimState(field, state.m_popup.instanceGeneration()) || !ComboBehavior::Bind(state, field.id, field.declarationGeneration))
        return result;
    state.m_popup.bindParent(m_builder.m_context.popupToken());
    if(previouslyOpen && !state.isOpen())
        ComboBehavior::Close(state);
    const TextAreaModelStamp queryStamp = search ? search->query().stamp() : TextAreaModelStamp{};
    if(!ComboBehavior::Reconcile(state, source) || (search && !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    if(search && (!SearchComboBehavior::Filter(*search, *searchSource) || !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    const IListDataSource& results = search ? searchSource->filtered() : source;
    if(!ListBehavior::Reconcile(state.m_list, results) || (search && !search->query().matchesStamp(queryStamp))){
        m_builder.m_context.fail();
        return result;
    }
    ComboFrame frame;
    frame.parentToken = m_builder.m_context.popupToken();
    frame.source = &source;
    frame.results = &results;
    frame.searchSource = searchSource;
    frame.search = search;
    frame.style = m_builder.m_comboStyle;
    frame.popupStyle = m_builder.m_popupStyle;
    frame.queryHeight = searchOptions ? searchOptions->queryHeight : 0.0f;
    frame.queryGap = searchOptions ? searchOptions->queryGap : 0.0f;
    snapshotComboQuery(frame);
    frame.state = &state;
    frame.options = options;
    frame.token = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    frame.resultCount = results.rowCount();
    const auto popup = m_builder.m_context.declarePart(field, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_builder.m_context.declarePart(field, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.m_popup.instanceGeneration(), state.m_popup.openGeneration() };
    frame.listToken = { state.m_list.inputGeneration(), results.instanceGeneration(), results.revision() };
    frame.open = state.isOpen();
    const b8 previouslyListFocused = m_builder.m_context.input().focus() == frame.rows.id;
    if(!m_builder.comboMatches(frame) || !applyComboInput(field, frame, result)){
        m_builder.m_context.fail();
        return {};
    }
    frame.open = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(search && !frame.open)
        search->m_editor.focused = false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.focusOnCommit = options.enabled && previouslyFocused && m_builder.m_context.input().focus() != field.id && !frame.open;
    frame.listFocusOnCommit = options.enabled && previouslyListFocused && m_builder.m_context.input().focus() != frame.rows.id;
    Item item(m_builder.m_arena);
    item.state = field;
    item.style = m_builder.m_style;
    item.enabled = options.enabled;
    item.combo = static_cast<u32>(m_builder.m_scope->m_combos.size());
    Point minimumSize{};
    if(!m_builder.m_skin || !ControlStyleMetrics().comboMetrics(*m_builder.m_skin, frame.style, minimumSize, item.padding, frame.arrowExtent)){
        m_builder.m_context.fail();
        return {};
    }
    const b8 selected = state.selectedKey() != 0u;
    const auto index = selected ? source.indexOf(state.selectedKey()) : Expected<u64>{ 0u };
    if(selected && (!index || *index >= frame.rowCount || source.key(*index) != state.selectedKey() || !source.enabled(*index))){
        m_builder.m_context.fail();
        return {};
    }
    // Text is shaped before another source callback can invalidate a temporary row label.
    const ShapeRequest request = m_builder.textShapeRequest(selected ? source.text(*index) : options.placeholder, item.style.fontSize);
    auto layout = m_builder.m_text.layout(request);
    if(!layout){
        m_builder.m_context.fail();
        return {};
    }
    item.text = Move(*layout);
    if(!m_builder.comboMatches(frame)){
        m_builder.m_context.fail();
        return {};
    }
    frame.options.placeholder = {};
    ListOptions listOptions;
    listOptions.wheelRows = options.wheelRows;
    listOptions.enabled = options.enabled;
    listOptions.selectOnNavigate = false;
    const WidgetId commandFocus = frame.editor != s_LayoutNoParent ? m_builder.m_scope->m_comboEditors[frame.editor].state.id : WidgetId{};
    ListFrame list(m_builder.m_arena);
    if(!m_builder.prepareListFrame(*frame.results, state.m_list, listOptions, frame.listToken, frame.resultCount, frame.listFocusOnCommit, commandFocus, list) || !m_builder.comboMatches(frame)){
        m_builder.m_context.fail();
        return {};
    }
    const auto textBounds = m_builder.m_text.measure(item.text, m_builder.m_paint.displayMetrics());
    if(!textBounds){
        m_builder.m_context.fail();
        return {};
    }
    item.textBounds = *textBounds;
    const Point measured{ textBounds->width, textBounds->height };
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ComputeComboFieldIntrinsicSize(minimumSize, measured, frame.arrowExtent, frame.style.arrowGap, item.padding);
    if(!m_builder.addItemNode(description, item))
        return {};
    frame.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    if(frame.open && !m_builder.reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_builder.m_context.fail();
        return {};
    }
    m_builder.m_scope->m_lists.push_back(Move(list));
    m_builder.m_scope->m_combos.push_back(frame);
    m_builder.m_scope->m_items.push_back(Move(item));
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    result.focused = options.enabled && (m_builder.m_context.input().focus() == field.id || m_builder.m_context.input().focus() == frame.rows.id || (frame.editor != s_LayoutNoParent && m_builder.m_context.input().focus() == m_builder.m_scope->m_comboEditors[frame.editor].state.id));
    return result;
}

SearchComboResult ComboCoordinator::searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options){
    if(m_builder.declarationBlocked() || !IsValidSearchComboDeclarationOptions(options)){
        m_builder.m_context.fail();
        return {};
    }
    const usize previousCount = m_builder.m_scope->m_combos.size();
    SearchComboResult result;
    result.combo = declareCombo(stableKey, source, options.combo, &options, state.combo(), &state, &source);
    if(result.combo.valid && m_builder.m_scope->m_combos.size() > previousCount)
        result.queryChanged = m_builder.m_scope->m_combos.back().queryChanged;
    return result;
}

b8 ComboCoordinator::prepareComboSearch(const WidgetState& field, ComboFrame& frame){
    SearchComboState& state = *frame.search;
    EditModel& query = state.query();
    if(!frame.open){
        query.cancelComposition();
        snapshotComboQuery(frame);
        return m_builder.comboMatches(frame);
    }
    const auto declared = m_builder.m_context.declarePart(field, s_WidgetPartQuery, WidgetKind::EditBox);
    if(!declared)
        return false;
    const WidgetState editor = **declared;
    if(!m_builder.m_context.claimState(editor, query.instanceGeneration()))
        return false;
    Item item(m_builder.m_arena);
    item.state = editor;
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.editOptions.enabled = frame.options.enabled;
    EditBoxResult result;
    result.valid = true;
    result.focused = m_builder.m_context.input().focus() == editor.id;
    if(m_builder.m_editHost)
        result = m_builder.m_editHost->editInPopup(editor, item.editOptions, frame.popupToken, query);
    if(!result.valid)
        return false;
    frame.queryChanged = result.textChanged;
    frame.editorSubmitted = result.submitted;
    if(result.cancelled){
        ComboBehavior::Close(*frame.state);
        m_builder.m_context.input().closePopup(frame.popupToken);
        frame.open = false;
    }
    snapshotComboQuery(frame);
    if(!SearchComboBehavior::Filter(state, *frame.searchSource))
        return false;
    frame.results = &frame.searchSource->filtered();
    if(!ListBehavior::Reconcile(frame.state->m_list, *frame.results))
        return false;
    frame.listToken = { frame.state->m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
    frame.resultCount = frame.results->rowCount();
    m_builder.m_context.input().fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!m_builder.comboMatches(frame))
        return false;
    if(!frame.open)
        return true;
    if(!m_builder.prepareEditBox(result, query, state.m_editor, item))
        return false;
    frame.editor = static_cast<u32>(m_builder.m_scope->m_comboEditors.size());
    m_builder.m_scope->m_comboEditors.push_back(Move(item));
    return m_builder.comboMatches(frame);
}

void ComboCoordinator::snapshotComboQuery(ComboFrame& frame)noexcept{
    if(!frame.search)
        return;
    frame.queryStamp = frame.search->query().stamp();
}

b8 ComboCoordinator::paintComboQuery(ComboFrame& frame, LayoutBox& content){
    if(frame.editor >= m_builder.m_scope->m_comboEditors.size() || !m_builder.comboMatches(frame))
        return false;
    const Item& item = m_builder.m_scope->m_comboEditors[frame.editor];
    const f32 measuredHeight = (item.editView.visualBounds().height + item.padding.top) + item.padding.bottom;
    Rect nextContentRect = content.rectangle;
    LayoutBox box = content;
    box.rectangle = ResolveComboQueryBoxAndAdvanceContent(measuredHeight, frame.queryHeight, frame.queryGap, nextContentRect);
    const HitTarget navigation = HitTargetFactory().makeComboQueryNavigationTarget(
        frame.options.enabled,
        item.editView.composing(),
        frame.rows.id,
        frame.rows.declarationGeneration,
        frame.listToken
    );
    if(!m_builder.paintEditBox(item, box, &navigation))
        return false;
    content.rectangle = nextContentRect;
    return m_builder.comboMatches(frame);
}

ComboResult Builder::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    return ComboCoordinator(*this).declareCombo(stableKey, source, options, searchOptions, state, search, searchSource);
}

SearchComboResult Builder::searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options){
    return ComboCoordinator(*this).searchComboBox(stableKey, source, state, options);
}

b8 Builder::comboStateMatches(const ComboFrame& frame)const noexcept{
    if(!frame.state)
        return false;
    return MatchesComboFrameStateValues(
        popupAncestorsVisible(),
        frame.state->inputGeneration(),
        frame.token.instanceGeneration,
        frame.state->m_list.inputGeneration(),
        frame.listToken.instanceGeneration,
        frame.state->isOpen(),
        frame.open,
        frame.state->m_popup.instanceGeneration(),
        frame.popupToken.instanceGeneration,
        frame.state->m_popup.openGeneration(),
        frame.popupToken.openGeneration,
        frame.state->m_popup.m_parent,
        frame.parentToken,
        !frame.search || frame.search->query().matchesStamp(frame.queryStamp)
    );
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


b8 ComboCoordinator::applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result){
    InputRouter& input = m_builder.m_context.input();
    ComboState& state = *frame.state;
    input.fenceControl(field.id, field.declarationGeneration, frame.token);
    input.fencePopup(frame.popupToken);
    if(!frame.options.enabled){
        input.invalidateTarget(field.id);
        ComboBehavior::Close(state);
        input.closePopup(frame.popupToken);
        if(frame.search){
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        return true;
    }
    if(input.consumePopupDismissal(frame.popupToken))
        ComboBehavior::Close(state);
    if(!state.isOpen())
        input.closePopup(frame.popupToken);
    if(m_builder.m_context.takeActivation(field, true))
        ComboBehavior::Open(state);
    while(auto action = m_builder.m_context.takeControlAction(field, true, frame.token)){
        if(state.isOpen())
            continue;
        if(!IsComboActionEligibleToOpen(action->kind))
            continue;
        ComboBehavior::Open(state);
        if(!ListBehavior::Reconcile(state.m_list, *frame.results))
            return false;
        if(action->kind != ControlActionKind::Submit){
            action->control = { state.m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
            ListOptions options;
            options.wheelRows = frame.options.wheelRows;
            options.selectOnNavigate = false;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, *action, state.m_list, preview))
                return false;
        }
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.results))
        return false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.open = state.isOpen();
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!m_builder.comboMatches(frame))
        return false;
    if(frame.search && !prepareComboSearch(field, frame))
        return false;
    return !state.isOpen() || applyComboListInput(frame, result);
}

b8 ComboCoordinator::applyComboListInput(ComboFrame& frame, ComboResult& result){
    ComboState& state = *frame.state;
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    return ListInput(m_builder.m_context).consume(
        frame.rows,
        frame.listToken,
        true,
        &frame.popupToken,
        state.m_list,
        [&](){ return m_builder.comboMatches(frame); },
        [&](const ControlAction& action){
            const b8 fromEditor = frame.editor != s_LayoutNoParent && action.source == m_builder.m_scope->m_comboEditors[frame.editor].state.id;
            const b8 composing = frame.search != nullptr && frame.search->query().composition().active;
            if(ShouldIgnoreComboEditorAction(fromEditor, composing, action.kind, frame.editorSubmitted))
                return ListActionOutcome::Continue;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, action, state.m_list, preview) || !m_builder.comboMatches(frame))
                return ListActionOutcome::Failed;
            if(!preview.activated)
                return ListActionOutcome::Continue;
            if(!ComboBehavior::Commit(state, *frame.source, state.m_list.selectedKey()))
                return ListActionOutcome::Failed;
            result.committed = true;
            m_builder.m_context.input().closePopup(frame.popupToken);
            return ListActionOutcome::Stop;
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComboCoordinator::paintCombo(const Item& item, const LayoutBox& box){
    if(item.combo >= m_builder.m_scope->m_combos.size())
        return false;
    ComboFrame& frame = m_builder.m_scope->m_combos[item.combo];
    if(!m_builder.comboMatches(frame))
        return false;
    const InputRouter& input = m_builder.m_context.input();
    const b8 focused = input.focus() == item.state.id;
    const b8 hovered = input.hover() == item.state.id;
    const Rect clip = m_builder.visibleClip(box.clip);
    if(!ControlVisualPainter().paintComboFieldVisual(
        m_builder.m_paint,
        m_builder.m_text,
        *m_builder.m_skin,
        frame.style,
        item.style,
        item.text,
        item.textBounds,
        item.padding,
        frame.arrowExtent,
        box.rectangle,
        clip,
        item.enabled,
        frame.open,
        focused,
        hovered
    ) || !m_builder.comboMatches(frame))
        return false;
    const HitTarget target = HitTargetFactory().makeComboFieldHitTarget(box.rectangle, clip, item.enabled, item.contextMenu, frame.token, frame.focusOnCommit);
    if(!m_builder.m_context.addTarget(item.state, target))
        return false;
    frame.state->m_bounds = box.rectangle;
    frame.visibleField = IntersectUiRectsBounds(box.rectangle, clip);
    return true;
}

b8 ComboCoordinator::paintCombos(){
    for(auto& frame : m_builder.m_scope->m_combos){
        if(!m_builder.comboMatches(frame) || frame.list >= m_builder.m_scope->m_lists.size())
            return false;
        if(frame.open && frame.options.enabled && !paintComboPopup(frame))
            return false;
    }
    return true;
}

b8 ComboCoordinator::paintComboPopup(ComboFrame& frame){
    const UiSkinRegion* background = m_builder.region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!background || !m_builder.comboMatches(frame))
        return false;
    const Rect& anchor = frame.state->bounds();
    if(frame.visibleField.width <= 0.0f || frame.visibleField.height <= 0.0f){
        m_builder.m_context.discardPopupScope(frame.popupToken);
        ComboBehavior::Close(*frame.state);
        frame.open = false;
        if(frame.search){
            frame.search->m_editor.focused = false;
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
        m_builder.m_scope->m_lists[frame.list].token = frame.listToken;
        return true;
    }
    PopupOptions options;
    options.anchor = anchor;
    options.size = { anchor.width, frame.options.popupHeight };
    const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
    if(!placement)
        return false;
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
    const PopupScope scope = UiScopeDescriptorFactory().comboPopup(
        frame.popupToken,
        frame.parentToken,
        *placement,
        frame.state->m_owner,
        frame.state->m_ownerDeclaration
    );
    if(!m_builder.m_context.updatePopupScope(frame.popupToken, scope) || !m_builder.m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_builder.m_paint.beginOverlay(m_builder.m_context.popupLayer())){
        const b8 ended = m_builder.m_context.endPopupScope(false);
        if(!ended)
            m_builder.m_context.fail();
        return false;
    }
    m_builder.m_paint.pushClip(placement->viewport);
    const b8 backgroundPainted = m_builder.m_paint.drawRegion(background->name, placement->bounds);
    const b8 clipPopped = m_builder.m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(placement->bounds, placement->viewport);
    b8 painted = backgroundPainted && clipPopped && m_builder.m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item listItem(m_builder.m_arena);
        listItem.state = frame.rows;
        listItem.list = frame.list;
        LayoutBox box;
        box.rectangle = InsetUiRectBounds(placement->bounds, padding);
        box.clip = placement->bounds;
        painted = (!frame.search || paintComboQuery(frame, box)) && m_builder.paintList(listItem, box) && m_builder.comboMatches(frame);
    }
    const b8 overlayEnded = m_builder.m_paint.endOverlay();
    const b8 scopeEnded = m_builder.m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}

b8 Builder::paintCombo(const Item& item, const LayoutBox& box){
    return ComboCoordinator(*this).paintCombo(item, box);
}

b8 Builder::paintCombos(){
    return ComboCoordinator(*this).paintCombos();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

