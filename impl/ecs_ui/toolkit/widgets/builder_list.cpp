// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult Builder::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    ListResult result;
    if(itemDeclarationBlocked() || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f){
        m_context.fail();
        return result;
    }
    // A body callback may have closed or reopened its popup; no model loan is allowed past that boundary.
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::VirtualList);
    if(!widget)
        return result;
    const b8 previouslyFocused = m_context.input().focus() == (*widget)->id;
    const u64 previousSelection = state.selectedKey();
    if(!ListBehavior::Reconcile(state, source)){
        m_context.fail();
        return result;
    }
    const ControlToken token{ state.inputGeneration(), source.instanceGeneration(), source.revision() };
    m_context.input().fenceControl((*widget)->id, (*widget)->declarationGeneration, token);
    if(!options.enabled)
        m_context.input().invalidateTarget((*widget)->id);
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.focused = options.enabled && m_context.input().focus() == (*widget)->id;
    if(!consumeListInput(
        **widget,
        token,
        options.enabled,
        nullptr,
        state,
        []()noexcept{ return true; },
        [&](const ControlAction& action){
            return ListBehavior::Apply(source, options, action, state, result) ? ListActionOutcome::Continue : ListActionOutcome::Failed;
        }
    )){
        m_context.fail();
        result.valid = false;
        return result;
    }
    ListFrame frame(m_arena);
    const UiSkinRegion* background = prepareListFrame(source, state, options, token, source.rowCount(), options.enabled && previouslyFocused && !result.focused, {}, frame);
    if(!background){
        m_context.fail();
        result.valid = false;
        return result;
    }
    // A fractional offset or a skin minimum can expose more than one row in a natural viewport.
    // Finish that bounded measurement before its intrinsic height enters the parent layout.
    if(options.height.policy == LayoutSizePolicy::Content && frame.rowCount != 0u){
        for(;;){
            LayoutBox provisional;
            provisional.rectangle = ResolveListProbeRect(*background, frame.padding, frame.style.scrollbarWidth, frame.rowHeight);
            provisional.clip = provisional.rectangle;
            const auto placement = measureListPlacement(frame, provisional, provisional.clip);
            if(!placement){
                m_context.fail();
                result.valid = false;
                return result;
            }
            const f32 height = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight).y;
            if(height == placement->bounds.height)
                break;
        }
    }
    Item item(m_arena);
    item.state = **widget;
    item.enabled = options.enabled;
    item.list = static_cast<u32>(m_scope->m_lists.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveListIntrinsicSize(*background, frame.padding, frame.rowHeight);
    if(!addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_scope->m_lists.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return result;
}

const UiSkinRegion* Builder::prepareListFrame(
    const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token,
    const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame
){
    outFrame.source = &source;
    outFrame.state = &state;
    outFrame.widgetStyle = m_style;
    outFrame.style = m_listStyle;
    outFrame.options = options;
    outFrame.token = token;
    outFrame.rowCount = rowCount;
    outFrame.focusOnCommit = focusOnCommit;
    outFrame.commandFocus = commandFocus;
    outFrame.padding = outFrame.style.padding;
    const UiSkinRegion* background = region(outFrame.style.background, outFrame.style.backgroundFallback);
    if(!background)
        return nullptr;
    outFrame.padding = MaxUiPadding(outFrame.padding, background->padding);
    return prepareListMetrics(outFrame) ? background : nullptr;
}

b8 Builder::prepareListMetrics(ListFrame& frame){
    if(!listMatches(frame) || !m_skin)
        return false;
    const auto metrics = MeasureSelectable(*m_skin, frame.style.row);
    if(!metrics)
        return false;
    frame.style.row.padding = metrics->padding;
    const auto keyResult = TryBuildListMeasureKey(
        frame.token.contentGeneration,
        frame.token.contentRevision,
        frame.rowCount,
        m_text.identity(),
        m_text.generation(),
        frame.widgetStyle.fontSize,
        m_paint.displayMetrics(),
        *metrics,
        m_textScriptTag,
        StringView(m_textLanguage.data(), m_textLanguage.size())
    );
    if(!keyResult)
        return false;
    const ListMeasureKey key = *keyResult;
    frame.measureKey = key;
    ListState& state = *frame.state;
    if(key != state.m_measureKey || state.m_rowHeight <= 0.0f){
        auto empty = m_text.layout(textShapeRequest({}, key.fontSize));
        if(!empty)
            return false;
        const auto bounds = m_text.measure(*empty, m_paint.displayMetrics());
        if(!bounds)
            return false;
        const f32 height = ResolveListRowBaseHeight(*metrics, bounds->height);
        if(!IsFinite(height) || height <= 0.0f || !listMatches(frame))
            return false;
        state.m_measureKey = key;
        state.m_rowHeight = height;
    }
    // The last painted offset may belong to a different styled declaration than the measurement cache.
    if(state.m_scrollRowHeight > 0.0f && state.m_scrollSourceGeneration == key.sourceGeneration && state.m_scrollRowHeight != state.m_rowHeight){
        const f64 offset = ComputeListRescaledScrollOffset(
            state.m_placement.contentClip.y,
            state.m_placement.viewport.y,
            state.scrollOffset(),
            state.m_scrollRowHeight,
            state.m_rowHeight
        );
        if(!state.m_scroll.setOffset(offset))
            return false;
    }
    state.m_scrollRowHeight = state.m_rowHeight;
    state.m_scrollSourceGeneration = key.sourceGeneration;
    frame.rowHeight = state.m_rowHeight;
    frame.scrollOffset = state.scrollOffset();
    frame.cursorKey = state.cursorKey();
    frame.revealCursor = state.m_ensureCursor;
    return listMatches(frame);
}

b8 Builder::applyListGesture(ListState& state, const PointerGesture& gesture)noexcept{
    const Expected<f64> offset = TryComputeListGestureScrollOffset(gesture);
    if(!offset)
        return false;
    state.m_ensureCursor = false;
    return state.m_scroll.setOffset(*offset);
}

b8 Builder::listMatches(const ListFrame& frame)const{
    return
        frame.source && listStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && listStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && listStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount
        && listStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

